#!/usr/bin/env bash
##############################################################################
# RunTriggerPlotter_Condor_submit.sh
#
#  local      – run Fun4All_getJetTrigs.C once on the first DST file found
#  condor     – submit every 5-file chunk for every run to HTCondor
#  condorTest – same as condor but stop after 2 jobs (10 files) for a quick test
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

LOGDIR="${SCRATCH}/log"
OUTDIR="${SCRATCH}/stdout"
ERRDIR="${SCRATCH}/error"
TMP_LIST_DIR="${SCRATCH}/condor_lists"
MACRO_DIR="macro"           # Fun4All_getJetTrigs.C lives here

mkdir -p "${LOGDIR}" "${OUTDIR}" "${ERRDIR}" "${TMP_LIST_DIR}"
# --------------------------------------------------------------------------
runFile="${SCRATCH}/Final_RunNumbers_After_All_Cuts.txt"
readarray -t runs < "${runFile}"

##############################################################################
#  LOCAL MODE  (single macro call, no condor submission)
##############################################################################
if [[ "${mode}" == "local" ]]; then
  firstRun="${runs[0]}"
  dstMaster="${SCRATCH}/dst_list/dst_jet_run2pp-000${firstRun}.list"

  if [[ ! -s "${dstMaster}" ]]; then
    echo "[ERROR] No DST list for first run (${firstRun}) – aborting local run."
    exit 1
  fi

  firstFile="$(head -n1 "${dstMaster}")"
  echo "[INFO] Local mode – first run: ${firstRun}"
  echo "[INFO] Using DST file: ${firstFile}"

  # Make a temporary single-file list
  tmpList=$(mktemp "${TMP_LIST_DIR}/local_${firstRun}_XXXX.list")
  echo "${firstFile}" > "${tmpList}"

  outDir="${SCRATCH}/output_local/${firstRun}"
  mkdir -p "${outDir}"
  rootOut="${outDir}/TrigPlot_local_${firstRun}.root"
  
  set +u                         # ----- disable nounset temporarily
  export PGHOST=localhost        # define before sourcing
  source /opt/sphenix/core/bin/sphenix_setup.sh -n
  export PGHOST=localhost        # re‑define (the setup script unsets it)
  set -u                         # ----- re‑enable nounset
  source /opt/sphenix/core/bin/setup_local.sh "${HOME}/install"

  export ROOT_INCLUDE_PATH=$ROOT_INCLUDE_PATH:"${HOME}/install/include"
  # ------------------------------------------------------------------------

  echo "[INFO] Running Fun4All_getJetTrigs.C locally..."
  root -b -l -q "macro/Fun4All_getJetTrigs.C(0, \"${tmpList}\", \"${rootOut}\")"

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
  if [[ ! -s "${masterList}" ]]; then
    echo "[WARN] No list for run ${run}, skipping."
    continue
  fi

  # remove any old chunks for this run, then split into 5-file chunks
  rm -f "${TMP_LIST_DIR}/run${run}_chunk_"*
  split -l 5 -d -a 3 "${masterList}" "${TMP_LIST_DIR}/run${run}_chunk_"

  for listFile in "${TMP_LIST_DIR}/run${run}_chunk_"*; do
    [[ ! -s "${listFile}" ]] && continue   # skip zero-byte files

    chunkTag="$(basename "${listFile}")"
    subFile="${SCRATCH}/TrigPlot_${chunkTag}.sub"

    cat > "${subFile}" <<EOL
universe      = vanilla
executable    = ${EXEC}
arguments     = ${run} ${listFile} \$(Cluster)
log           = ${LOGDIR}/TrigPlot_\$(Cluster).\$(Process).log
output        = ${OUTDIR}/TrigPlot_\$(Cluster).\$(Process).out
error         = ${ERRDIR}/TrigPlot_\$(Cluster).\$(Process).err
request_memory= 1000MB
queue
EOL

    condor_submit "${subFile}"
    ((submitted++))

    # ---------------- test-mode stop condition ---------------------------
    if [[ $testLimit -gt 0 && $submitted -ge $testLimit ]]; then
      echo "[INFO] condorTest mode – submitted ${submitted} job(s), stopping."
      exit 0
    fi
    # --------------------------------------------------------------------
  done
done

echo "[INFO] Submitted ${submitted} Condor job(s)."
