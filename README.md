# Jet Trigger Turn-On Analysis – **Project README**

---

## 1. What does this code do?

This repository streamlines the complete workflow for producing **jet-trigger efficiency curves**:

* **Scaled (GL1) trigger bits** – applies the official run-by-run scale-down factors, merges only those runs in which the *scaled* bit was enabled, and builds the efficiency distributions.
* **Live (unscaled) trigger bits** – merges all runs (the bit is always present) to visualise the raw, pre-scale efficiency distribution.

* **Inputs** – raw “DST” files for each run, split by segment.  
* **Processing chain**  
  1. **Event loop** (`Fun4All_getJetTrigs.C` → `JetTriggerPlotter`) books/fills QA histograms run-by-run for the jet triggers  
     * `Jet_X_GeV + MBD NS ≥ 1` where **X ∈ {8, 10, 12} GeV**  
     * Jet-radii handled **in parallel** for *R = 0.3* and *R = 0.5*  
  2. Condor (or local) submission scripts farm the job over all runs/segments.  
  3. `mergeSegmentsForRun.C` applies the official trigger scale-downs and produces **one merged ROOT file per run**.  
  4. `makeJetTriggerOverlays.C` merges across runs, splits **before / after the 47289 firmware update**, and writes  
     * combined spectra  
     * efficiency curves (turn-ons)  
     * page-style run-by-run QA overlays.  

The final figures live under `plotOutput/<combination>/…`. Note radii and paths may need to be updated, which can be done in the source code header and anywhere else found necessary.

---

## 2. Prerequisites

| Requirement                | Notes                                                                                               |
|----------------------------|-------------------------------------------------------------------------------------------------------|
| **sPHENIX software stack** | Load with `source /opt/sphenix/core/bin/sphenix_setup.sh -n` (already done for you by the scripts).  |
| **ROOT 6**                 | Must match the sPHENIX build.                                                                        |
| **gcc / clang**            | Same tool-chain used to build the stack.                                                             |

---

## 3. Directory layout (after cloning)

```
.
├── macros/
│   ├── Fun4All_getJetTrigs.C
│   ├── makeJetTriggerOverlays.C
│   ├── makeJetTriggerOverlays.h
│   └── mergeSegmentsForRun.C
├── src/
│   ├── JetTriggerPlotter.cc
│   └── JetTriggerPlotter.h
├── RunTriggerPlotter_Condor.sh
├── RunTriggerPlotter_Condor_submit.sh
└── dst_lists/           ← (populated in step 4)
```

---

## 4. Getting the DST file lists

On an **sPHENIX interactive node** run

```bash
cp -r /sphenix/u/patsfan753/scratch/TriggerAnalysis/dst_lists .
```

This clones the run/segment lists into your working directory so the submission script can find them.

---

## 5. Building the analysis code

```bash
cd src
autoreconf -i        # if the repo ships an autogen.sh run that instead
./configure --prefix=$HOME/install
make -j4
make install         # installs into $HOME/install
```
---

## 6. Running over the data

### 6.1 Quick test on a single DST file (interactive)

```bash
./RunTriggerPlotter_Condor_submit.sh local
```

* Runs **exactly one event-loop job** on the first file of the first run and writes  
  `~/scratch/TriggerAnalysis/output_local/<run>/TrigPlot_local_<run>.root`

### 6.2 Full production on Condor

```bash
# unlimited jobs (all runs, every file)
./RunTriggerPlotter_Condor_submit.sh condor

# cap the total number of jobs to N (= MAX_JOBS variable)
./RunTriggerPlotter_Condor_submit.sh condor firstTen

# submit only the first run (handy for debugging condor issues)
./RunTriggerPlotter_Condor_submit.sh condorTest
```

Key tunables live at the **top of the script**: `CHUNK_SIZE` (files per job) and `MAX_JOBS` (global cap).

Under Condor each job executes `RunTriggerPlotter_Condor.sh`, which sets up the environment, chooses an output directory and launches ROOT in batch mode:

```bash
root -b -l -q "macro/Fun4All_getJetTrigs.C(0, \"$fileList\", \"$rootOut\")"
```

---

## 7. Merging segment-level outputs (per run)

When all Condor jobs have finished:

```bash
root -b -q -l 'mergeSegmentsForRun.C'
```

* Reads every `TrigPlot_run<run>*root` file  
* Applies the trigger prescale factors  
* Writes a **per-run** merged file to `output/<run>/TriggerAna_<run>.root` as well as a final combined root file.

---

## 8. Cross-run merging & plotting

Edit the **paths** at the top of `makeJetTriggerOverlays.C` as necessary, then run

```bash
root -b -q -l 'makeJetTriggerOverlays.C'
```

The macro will:

1. Inspect `triggerAnalysisCombined.csv` to learn which **golden runs** belong to each trigger combo.  
2. Merge the per-run files into *before* / *after* firmware groups.  
3. Draw summary overlays, efficiencies and run-by-run pages into `plotOutput/…`.  

---

## 9. Typical end-to-end recipe

```bash
# 0.  Prepare
source /opt/sphenix/core/bin/sphenix_setup.sh -n
mkdir -p ~/work/jets && cd ~/work/jets
# (clone repo, then…)

# 1.  Get DST lists
cp -r /sphenix/u/patsfan753/scratch/TriggerAnalysis/dst_lists .

# 2.  Build
cd src && autoreconf -i && ./configure --prefix=$HOME/install && make -j8 && make install && cd ..

# 3.  Submit Condor production
./RunTriggerPlotter_Condor_submit.sh condor

# 4.  Wait for jobs ⇒ merge segments
root -b -q -l 'mergeSegmentsForRun.C'

# 5.  Global merge + plotting
root -b -q -l 'makeJetTriggerOverlays.C'
```

---
