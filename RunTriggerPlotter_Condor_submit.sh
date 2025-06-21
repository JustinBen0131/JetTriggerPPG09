#!/usr/bin/env bash
##############################################################################
# RunTriggerPlotter_Condor_submit.sh
#
#  local      – run Fun4All_getJetTrigs.C once on the first DST file
#  condor     – submit one job per 5‑file chunk for every run
#  condorTest – same as condor but stop after 2 jobs (10 files)
#  condor + firstTen  – submit as many full‑run chunks as possible
#                       without exceeding 10 000 jobs in total
##############################################################################
set -euo pipefail

mode="${1:-}"
limitSwitch="${2:-}"               # <‑‑ NEW (may be empty or 'firstTen')

if [[ "${mode}" != "local" && "${mode}" != "condor" && "${mode}" != "condorTest" ]]; then
  echo "Usage: $0  {local | condor | condorTest}  [firstTen]"
  exit 1
fi

##############################################################################
#  VERBOSITY HELPER
#  • switch on for condorTest OR condor+firstTen
##############################################################################
VERBOSE=0
if [[ "${mode}" == "condorTest" || ( "${mode}" == "condor" && "${limitSwitch}" == "firstTen" ) ]]; then
  VERBOSE=1
fi
vecho() { (( VERBOSE )) && echo "$@"; }

# ------------------ job‑count limits ---------------------------------------
testLimit=$([[ "${mode}" == "condorTest" ]] && echo 2 || echo 0)   # unchanged
jobLimit=0                                                        # 0 = ∞
if [[ "${mode}" == "condor" && "${limitSwitch}" == "firstTen" ]]; then
  jobLimit=10000
fi
# ---------------------------------------------------------------------------

# --------------------------------------------------------------------------
USER="$(id -un)"
HOME="/sphenix/u/${USER}"
SCRATCH="${HOME}/scratch/TriggerAnalysis"
EXEC="${SCRATCH}/RunTriggerPlotter_Condor.sh"

# ---------- global Condor I/O base ----------------------------------------
CONDOR_BASE="/sphenix/tg/tg01/bulk/jbennett/TriggerAna"
LOGDIR="${SCRATCH}/log"
OUTDIR="${SCRATCH}/stdout"
ERRDIR="${SCRATCH}/error"
mkdir -p "${LOGDIR}" "${OUTDIR}" "${ERRDIR}"
# --------------------------------------------------------------------------

vecho "[VERBOSE] Mode               : ${mode}"
vecho "[VERBOSE] Limit switch       : ${limitSwitch}"
vecho "[VERBOSE] Scratch directory  : ${SCRATCH}"
vecho "[VERBOSE] Condor base (ROOT) : ${CONDOR_BASE}"
vecho "[VERBOSE] Log/Out/Err dirs   : ${LOGDIR}  ${OUTDIR}  ${ERRDIR}"

TMP_LIST_DIR="${SCRATCH}/condor_lists"
MACRO_DIR="macro"                     # Fun4All_getJetTrigs.C lives here
mkdir -p "${TMP_LIST_DIR}"

runFile="${SCRATCH}/Final_RunNumbers_After_All_Cuts.txt"
readarray -t runs < "${runFile}"

##############################################################################
#  LOCAL MODE
##############################################################################
if [[ "${mode}" == "local" ]]; then
  firstRun="${runs[0]}"
  dstMaster="${SCRATCH}/dst_list/dst_jet_run2pp-000${firstRun}.list"
  [[ -s "${dstMaster}" ]] || { echo "[ERROR] ${dstMaster} missing"; exit 1; }

  firstFile="$(head -n1 "${dstMaster}")"
  echo "[INFO] Local mode – first run: ${firstRun}"
  echo "[INFO] Using DST file: ${firstFile}"

  tmpList=$(mktemp "${TMP_LIST_DIR}/local_${firstRun}_XXXX.list")
  echo "${firstFile}" > "${tmpList}"

  outDir="${SCRATCH}/output_local/${firstRun}"
  mkdir -p "${outDir}"
  rootOut="${outDir}/TrigPlot_local_${firstRun}.root"

  # --- environment --------------------------------------------------------
  set +u
  export PGHOST=localhost
  source /opt/sphenix/core/bin/sphenix_setup.sh -n
  export PGHOST=localhost
  set -u
  source /opt/sphenix/core/bin/setup_local.sh "${HOME}/install"
  export ROOT_INCLUDE_PATH=$ROOT_INCLUDE_PATH:"${HOME}/install/include"
  # ------------------------------------------------------------------------

  root -b -l -q "${MACRO_DIR}/Fun4All_getJetTrigs.C(0, \"${tmpList}\", \"${rootOut}\")"

  echo "[INFO] Local run finished. Output → ${rootOut}"
  rm -f "${tmpList}"
  exit 0
fi

##############################################################################
#  CONDOR / CONDORTEST MODE
##############################################################################
submitted=0

for run in "${runs[@]}"; do
  vecho "[VERBOSE] --------------"
  vecho "[VERBOSE] Considering run ${run}"
  masterList="${SCRATCH}/dst_list/dst_jet_run2pp-000${run}.list"
  [[ -s "${masterList}" ]] || { echo "[WARN] No list for run ${run}, skipping."; continue; }

  rm -f "${TMP_LIST_DIR}/run${run}_chunk_"*
  split -l 5 -d -a 3 "${masterList}" "${TMP_LIST_DIR}/run${run}_chunk_"
  vecho "[VERBOSE] Split ${masterList} → ${TMP_LIST_DIR}/run${run}_chunk_*** (5 per chunk)"

  # how many jobs would this run add?
  mapfile -t chunks < <(ls "${TMP_LIST_DIR}/run${run}_chunk_"*)
  nChunks=${#chunks[@]}
  vecho "[VERBOSE] Chunks to submit for run ${run}: ${nChunks}"

  # ------ 10 000‑job guard (only if jobLimit > 0) --------------------------
  if (( jobLimit > 0 )); then
    prospective=$((submitted + nChunks))
    vecho "[VERBOSE] Prospective total after this run: ${prospective} (cap ${jobLimit})"
    if (( prospective > jobLimit )); then
      echo "[INFO] Reached the ${jobLimit}‑job cap "
      echo "       (would exceed it by adding run ${run})."
      echo "[INFO] Stopping before submitting any jobs for run ${run}."
      break
    fi
  fi
  # ------------------------------------------------------------------------

  for listFile in "${chunks[@]}"; do
    [[ -s "${listFile}" ]] || continue

    # ---------- build job‑specific names from the *first file* -------------
    firstDST="$(head -n1 "${listFile}")"
    baseTag="$(basename "${firstDST}" .root)"        # strip .root
    logFile="${LOGDIR}/${baseTag}.log"
    outFile="${OUTDIR}/${baseTag}.out"
    errFile="${ERRDIR}/${baseTag}.err"
    # ----------------------------------------------------------------------

    subFile="${SCRATCH}/TrigPlot_${baseTag}.sub"

    vecho "[VERBOSE]   Preparing job for list ${listFile}"
    vecho "[VERBOSE]   • first DST : ${firstDST}"
    vecho "[VERBOSE]   • log/out/err → $(basename "${logFile}") / $(basename "${outFile}") / $(basename "${errFile}")"
    vecho "[VERBOSE]   • ROOT output dir will be ${CONDOR_BASE}/${run}"

    cat > "${subFile}" <<EOL
universe      = vanilla
executable    = ${EXEC}
arguments     = ${run} ${listFile} \$(Cluster) ${CONDOR_BASE}
log           = ${logFile}
output        = ${outFile}
error         = ${errFile}
request_memory= 1000MB
queue
EOL

    vecho "[VERBOSE]   Submitting with condor_submit ${subFile}"
    condor_submit "${subFile}"
    ((submitted++))

    # -------- condorTest two‑job guard (unchanged) -------------------------
    [[ $testLimit -gt 0 && $submitted -ge $testLimit ]] && {
      echo "[INFO] condorTest mode – submitted ${submitted} job(s), stopping."
      exit 0
    }
    # ----------------------------------------------------------------------
  done
done

echo "[INFO] Submitted ${submitted} Condor job(s)."
if (( jobLimit > 0 )); then
  echo "[INFO] Job‑cap mode (firstTen) was active – cap = ${jobLimit}."
fi
