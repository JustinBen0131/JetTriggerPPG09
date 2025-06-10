#!/usr/bin/env bash
##############################################################################
# RunTriggerPlotter_Condor_submit.sh  [condor]
#
# * Reads run numbers from  Final_RunNumbers_After_All_Cuts.txt
# * For every run creates lists of 5 files from
#     dst_list/dst_jet_run2pp-000<run>.list
# * Produces & submits one condor job per chunk
# * Log / out / err go to   $SCRATCH/{log,stdout,error}
##############################################################################

set -euo pipefail

if [[ "${1:-}" != "condor" ]]; then
  echo "Usage: $0 condor"
  exit 1
fi

USER="$(id -un)"
HOME="/sphenix/u/${USER}"
SCRATCH="${HOME}/scratch/TriggerAnalysis"
EXEC="${SCRATCH}/RunTriggerPlotter_Condor.sh"
LOGDIR="${SCRATCH}/log"
OUTDIR="${SCRATCH}/stdout"
ERRDIR="${SCRATCH}/error"
TMP_LIST_DIR="${SCRATCH}/condor_lists"
mkdir -p "${LOGDIR}" "${OUTDIR}" "${ERRDIR}" "${TMP_LIST_DIR}"

runFile="${SCRATCH}/Final_RunNumbers_After_All_Cuts.txt"
readarray -t runs < "${runFile}"

for run in "${runs[@]}"; do
  masterList="${SCRATCH}/dst_list/dst_jet_run2pp-000${run}.list"
  if [[ ! -s "${masterList}" ]]; then
    echo "[WARN] No list for run ${run}, skipping."
    continue
  fi

  # split into chunks of 5
  split -l 5 -d -a 3 "${masterList}" "${TMP_LIST_DIR}/run${run}_chunk_"

  for listFile in ${TMP_LIST_DIR}/run${run}_chunk_*; do
    [[ ! -s "${listFile}" ]] && continue
    chunkTag="$(basename "${listFile}")"

    cat > "${SCRATCH}/TrigPlot_${chunkTag}.sub" <<EOL
universe      = vanilla
executable    = ${EXEC}
arguments     = ${run} ${listFile} \$(Cluster)
log           = ${LOGDIR}/TrigPlot_\$(Cluster).\$(Process).log
output        = ${OUTDIR}/TrigPlot_\$(Cluster).\$(Process).out
error         = ${ERRDIR}/TrigPlot_\$(Cluster).\$(Process).err
request_memory= 1GB
+JobFlavour   = "tomorrow"
queue
EOL

    condor_submit "${SCRATCH}/TrigPlot_${chunkTag}.sub"
  done
done


