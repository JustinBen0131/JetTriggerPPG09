#!/usr/bin/env bash
##############################################################################
# RunTriggerPlotter_Condor.sh
#  ‑ argv[1]  run number         (e.g. 47289)
#  ‑ argv[2]  file‑list path     (≤ 5 lines, one DST file per line)
#  ‑ argv[3]  Condor cluster ID  (used only for unique filenames)
#  ‑ argv[4]  DEST_BASE          (optional: base dir for .root output;
#                                if absent we fall back to $SCRATCH/output)
##############################################################################
set -euo pipefail

# ---------------- user paths -------------------------------------------------
USER="$(id -un)"
HOME="/sphenix/u/${USER}"
SCRATCH="/sphenix/u/${USER}/scratch/TriggerAnalysis"
MYINSTALL="${HOME}/install"                       # your local install prefix
MACRO_DIR="${SCRATCH}/../macros"                 # one directory up
# -----------------------------------------------------------------------------

runNumber="$1"
fileList="$2"
clusterID="${3:-0}"

set +u
export PGHOST=localhost
source /opt/sphenix/core/bin/sphenix_setup.sh -n
export PGHOST=localhost         # restore after the script unsets it
set -u
source /opt/sphenix/core/bin/setup_local.sh "${HOME}/install"

# ---------------- output location -------------------------------------------
# If a 4‑th argument (base directory) was provided, drop results into
#   ${destBase}/${runNumber}/
# otherwise stay in the old $SCRATCH/output/<run>.
destBase="${4:-}"
if [[ -n "${destBase}" ]]; then
  outDir="${destBase}/${runNumber}"
else
  outDir="${SCRATCH}/output/${runNumber}"
fi
mkdir -p "${outDir}"
# ----------------------------------------------------------------------------

firstFile="$(head -n1 "${fileList}")"
baseName="$(basename "${firstFile}")"
rootOut="${outDir}/TrigPlot_run${runNumber}_c${clusterID}_${baseName%.*}.root"
# -----------------------------------------------------------------------------

echo "[INFO] $(date)  Run=${runNumber}  Files=$(wc -l < "${fileList}")"
echo "[INFO] Output → ${rootOut}"

root -b -l -q "${MACRO_DIR}/Fun4All_getJetTrigs.C(0, \"${fileList}\", \"${rootOut}\")"

echo "[INFO] Completed $(date)"

