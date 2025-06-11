#!/usr/bin/env bash
##############################################################################
# RunTriggerPlotter_Condor_submit.sh
#
#  local      – run Fun4All_getJetTrigs.C once on the first DST file
#  condor     – submit one job per 5‑file chunk for every run
#  condorTest – same as condor but stop after 2 jobs (10 files)
##############################################################################

set -euo pipefail

mode="${1:-}"
if [[ "${mode}" != "local" && "${mode}" != "condor" && "${mode}" != "condorTest" ]]; then
  echo "Usage: $0  {local | condor | condorTest}"
  exit 1
fi
testLimit=$([[ "${mode}" == "condorTest" ]] && echo 2 || echo 0)

# --------------------------------------------------------------------------
USER="$(id -un)"
HOME="/sphenix/u/${USER}"
SCRATCH="${HOME}/scratch/TriggerAnalysis"
EXEC="${SCRATCH}/RunTriggerPlotter_Condor.sh"

# ---------- new global Condor I/O base ------------------------------------
CONDOR_BASE="/sphenix/tg/tg01/bulk/jbennett/TriggerAna"
LOGDIR="${CONDOR_BASE}/log"
OUTDIR="${CONDOR_BASE}/stdout"
ERRDIR="${CONDOR_BASE}/error"
mkdir -p "${LOGDIR}" "${OUTDIR}" "${ERRDIR}"
# --------------------------------------------------------------------------

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
  masterList="${SCRATCH}/dst_list/dst_jet_run2pp-000${run}.list"
  [[ -s "${masterList}" ]] || { echo "[WARN] No list for run ${run}, skipping."; continue; }

  rm -f "${TMP_LIST_DIR}/run${run}_chunk_"*
  split -l 5 -d -a 3 "${masterList}" "${TMP_LIST_DIR}/run${run}_chunk_"

  for listFile in "${TMP_LIST_DIR}/run${run}_chunk_"*; do
    [[ -s "${listFile}" ]] || continue

    # ---------- build job‑specific names from the *first file* -------------
    firstDST="$(head -n1 "${listFile}")"
    baseTag="$(basename "${firstDST}" .root)"        # strip .root
    logFile="${LOGDIR}/${baseTag}.log"
    outFile="${OUTDIR}/${baseTag}.out"
    errFile="${ERRDIR}/${baseTag}.err"
    # ----------------------------------------------------------------------

    subFile="${SCRATCH}/TrigPlot_${baseTag}.sub"

    cat > "${subFile}" <<EOL
universe      = vanilla
executable    = ${EXEC}
arguments     = ${run} ${listFile} \$(Cluster)
log           = ${logFile}
output        = ${outFile}
error         = ${errFile}
request_memory= 1000MB
queue
EOL

    condor_submit "${subFile}"
    ((submitted++))

    [[ $testLimit -gt 0 && $submitted -ge $testLimit ]] && {
      echo "[INFO] condorTest mode – submitted ${submitted} job(s), stopping."
      exit 0
    }
  done
done

echo "[INFO] Submitted ${submitted} Condor job(s)."
