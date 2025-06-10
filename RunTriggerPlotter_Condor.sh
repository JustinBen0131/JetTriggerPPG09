#!/usr/bin/env bash
##############################################################################
# RunTriggerPlotter_Condor.sh
#  ‑ argv[1]  run number   (e.g. 47289)
#  ‑ argv[2]  path to a text file that lists ≤5 DST files (one per line)
#  ‑ argv[3]  Condor cluster‑id (optional, used only for unique filenames)
#
# The script
#   1) sets up the sPHENIX environment (core + local install)
#   2) constructs an output directory: $SCRATCH/output/<run>
#   3) calls the Fun4All macro  (macros/Fun4All_getJetTrigs.C)
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

# ---------------- environment ------------------------------------------------
source /opt/sphenix/core/bin/sphenix_setup.sh -n
source /opt/sphenix/core/bin/setup_local.sh "${MYINSTALL}"
export ROOT_INCLUDE_PATH=$ROOT_INCLUDE_PATH:"${MYINSTALL}/include"
# -----------------------------------------------------------------------------

# ---------------- output -----------------------------------------------------
outDir="${SCRATCH}/output/${runNumber}"
mkdir -p "${outDir}"

firstFile="$(head -n1 "${fileList}")"
baseName="$(basename "${firstFile}")"
rootOut="${outDir}/TrigPlot_run${runNumber}_c${clusterID}_${baseName%.*}.root"
# -----------------------------------------------------------------------------

echo "[INFO] $(date)  Run=${runNumber}  Files=$(wc -l < "${fileList}")"
echo "[INFO] Output → ${rootOut}"

root -b -l -q "${MACRO_DIR}/Fun4All_getJetTrigs.C(0, \"${fileList}\", \"${rootOut}\")"

echo "[INFO] Completed $(date)"

