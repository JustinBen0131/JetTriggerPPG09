#include "makeJetTriggerOverlays.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <map>
#include <utility>
#include <set>
#include <algorithm>
#include <memory>

// ANSI escape codes for colors
#define RESET   "\033[0m"
#define RED     "\033[31m"      /* Red */
#define GREEN   "\033[32m"      /* Green */
#define YELLOW  "\033[33m"      /* Yellow */
#define BLUE    "\033[34m"      /* Blue */
#define MAGENTA "\033[35m"      /* Magenta */
#define CYAN    "\033[36m"      /* Cyan */
#define BOLD    "\033[1m"

bool enableFits = true; // Set to true if you want to enable the fits


std::map<std::set<std::string>, DataStructures::RunInfo>
AnalyzeWhatTriggerGroupsAvailable(
    const std::string& csvFilePath,
    bool debugMode,
    const std::map<int, std::map<std::string, std::string>>& overrideTriggerStatus)
{
    using namespace std;

    // 1) Pull from TriggerConfig
    const vector<string>& allTriggers    = TriggerConfig::allTriggers;
    const vector<string>& jetTriggers = TriggerConfig::jetTriggers;
    // 2) Data structures
    map<string, int> triggerToIndex;
    map<int, set<string>> runToActiveTriggers;

    int totalRunsProcessed = 0;

    // For debug printing
    map<set<string>, vector<int>> initialCombinationToRuns;
    vector<pair<set<string>, DataStructures::RunInfo>> finalCombinations;

    // 3) Open CSV + parse header
    ifstream file(csvFilePath);
    if (!file.is_open()) {
        cerr << "Failed to open file: " << csvFilePath << endl;
        return {};
    }

    string line;
    if (!getline(file, line)) {
        cerr << "Failed to read header from CSV file." << endl;
        return {};
    }

    vector<string> headers;
    {
        istringstream headerStream(line);
        string hdr;
        int colIdx = 0;
        while (getline(headerStream, hdr, ',')) {
            // Trim
            hdr.erase(0, hdr.find_first_not_of(" \t\r\n"));
            hdr.erase(hdr.find_last_not_of(" \t\r\n") + 1);

            headers.push_back(hdr);

            // If "runNumber" or recognized trigger, store col index
            if (hdr == "runNumber" ||
                find(allTriggers.begin(), allTriggers.end(), hdr) != allTriggers.end())
            {
                triggerToIndex[hdr] = colIdx;
            }
            colIdx++;
        }
    }
    if (triggerToIndex.find("runNumber") == triggerToIndex.end()) {
        cerr << "runNumber column not found in CSV header." << endl;
        return {};
    }

    // 4) Read CSV lines => fill runToActiveTriggers
    while (getline(file, line)) {
        vector<string> cells;
        {
            istringstream rowStream(line);
            string c;
            while (getline(rowStream, c, ',')) {
                c.erase(0, c.find_first_not_of(" \t\r\n"));
                c.erase(c.find_last_not_of(" \t\r\n") + 1);
                cells.push_back(c);
            }
        }
        if (cells.size() != headers.size()) {
            cerr << "Mismatch (#cells vs. #headers) in line: " << line << endl;
            continue;
        }

        int runNumber = stoi(cells[triggerToIndex["runNumber"]]);

        set<string> activeTriggers;
        for (const auto& trig : allTriggers) {
            auto itIdx = triggerToIndex.find(trig);
            if (itIdx == triggerToIndex.end()) {
                // e.g. triggers not in CSV or ones you skip
                continue;
            }
            int idx = itIdx->second;
            string status = cells[idx];

            // Trim
            status.erase(0, status.find_first_not_of(" \t\r\n"));
            status.erase(status.find_last_not_of(" \t\r\n") + 1);

            // Overrides
            auto runOver = overrideTriggerStatus.find(runNumber);
            if (runOver != overrideTriggerStatus.end()) {
                auto trigOver = runOver->second.find(trig);
                if (trigOver != runOver->second.end()) {
                    status = trigOver->second;
                }
            }
            if (status == "ON") {
                activeTriggers.insert(trig);
            }
        }
        // Use std::move to quiet the unqualified call warnings
        runToActiveTriggers[runNumber] = std::move(activeTriggers);
    }
    file.close();
    totalRunsProcessed = static_cast<int>(runToActiveTriggers.size());
    
    vector<set<string>> jetCombinations;
    {
        int nJet = static_cast<int>(jetTriggers.size());
        int totalJetSubsets = (1 << nJet);
        for (int mask = 0; mask < totalJetSubsets; ++mask) {
            set<string> combo;
            combo.insert("MBD_NandS_geq_1");
            for (int i = 0; i < nJet; i++) {
                if (mask & (1 << i)) {
                    combo.insert(jetTriggers[i]);
                }
            }
            // Again, use std::move to avoid the warning
            jetCombinations.push_back(std::move(combo));
        }
    }
    {
        int nJet = static_cast<int>(jetTriggers.size());
        int totalJetSubsets = (1 << nJet);
        for (int mask = 0; mask < totalJetSubsets; ++mask) {
            set<string> combo;
            combo.insert("MBD_NandS_geq_1");
            for (int i = 0; i < nJet; i++) {
                if (mask & (1 << i)) {
                    combo.insert(jetTriggers[i]);
                }
            }
            jetCombinations.push_back(std::move(combo));
        }
    }

    // unify them
    vector<set<string>> triggerCombinations;
    triggerCombinations.reserve(jetCombinations.size() + jetCombinations.size());

    for (auto& pc : jetCombinations) {
        triggerCombinations.push_back(std::move(pc));
    }
    // push jet combos
    for (auto& jc : jetCombinations) {
        triggerCombinations.push_back(std::move(jc));
    }

    // 6) For each run => which combos are satisfied
    map<set<string>, vector<int>> tempCombinationToRuns;
    for (auto& kv : runToActiveTriggers) {
        int runNumber = kv.first;
        const set<string>& active = kv.second;

        // must have MBD
        if (active.find("MBD_NandS_geq_1") == active.end()) {
            continue;
        }
        for (auto& combo : triggerCombinations) {
            bool satisfies = true;
            for (auto& trig : combo) {
                if (active.find(trig) == active.end()) {
                    satisfies = false;
                    break;
                }
            }
            if (satisfies) {
                tempCombinationToRuns[combo].push_back(runNumber);
            }
        }
    }
    initialCombinationToRuns = tempCombinationToRuns;

    // 7) Split runs by firmware
    struct TempRunInfo {
        vector<int> runsBeforeFirmwareUpdate;
        vector<int> runsAfterFirmwareUpdate;
    };
    map<set<string>, TempRunInfo> tempComboRunInfo;
    set<set<string>> combosWithRun47289;

    for (auto& kv2 : tempCombinationToRuns) {
        const auto& combo = kv2.first;
        const auto& runs  = kv2.second;

        TempRunInfo runInfo;
        bool has47289 = false;
        for (int rn : runs) {
            if (rn == 47289) {
                has47289 = true;
            }
            if (rn < 47289) {
                runInfo.runsBeforeFirmwareUpdate.push_back(rn);
            } else {
                runInfo.runsAfterFirmwareUpdate.push_back(rn);
            }
        }
        if (has47289) {
            combosWithRun47289.insert(combo);
        }
        tempComboRunInfo[combo] = std::move(runInfo);
    }

    // 8) Group combos by identical run-lists => pick largest combos
    map<vector<int>, vector<set<string>>> runListToCombinationsBeforeFW;
    map<vector<int>, vector<set<string>>> runListToCombinationsAfterFW;

    for (auto& kv3 : tempComboRunInfo) {
        const auto& combo   = kv3.first;
        const auto& runInfo = kv3.second;

        if (!runInfo.runsBeforeFirmwareUpdate.empty()) {
            vector<int> tmp = runInfo.runsBeforeFirmwareUpdate;
            sort(tmp.begin(), tmp.end());
            runListToCombinationsBeforeFW[tmp].push_back(combo);
        }
        if (!runInfo.runsAfterFirmwareUpdate.empty()) {
            vector<int> tmp = runInfo.runsAfterFirmwareUpdate;
            sort(tmp.begin(), tmp.end());
            runListToCombinationsAfterFW[tmp].push_back(combo);
        }
    }

    map<set<string>, vector<int>> filteredCombinationToRunsBeforeFirmwareUpdate;
    {
        for (auto& kvB : runListToCombinationsBeforeFW) {
            const auto& runList = kvB.first;
            const auto& combos  = kvB.second;

            size_t maxSize = 0;
            for (auto& c : combos) {
                if (c.size() > maxSize) {
                    maxSize = c.size();
                }
            }
            for (auto& c : combos) {
                if (c.size() == maxSize) {
                    filteredCombinationToRunsBeforeFirmwareUpdate[c] = runList;
                }
            }
        }
    }

    map<set<string>, vector<int>> filteredCombinationToRunsAfterFirmwareUpdate;
    {
        for (auto& kvA : runListToCombinationsAfterFW) {
            const auto& runList = kvA.first;
            const auto& combos  = kvA.second;

            size_t maxSize = 0;
            for (auto& c : combos) {
                if (c.size() > maxSize) {
                    maxSize = c.size();
                }
            }
            for (auto& c : combos) {
                if (c.size() == maxSize) {
                    filteredCombinationToRunsAfterFirmwareUpdate[c] = runList;
                }
            }
        }
    }

    // 9) Build final map => combinationToRuns
    map<set<string>, DataStructures::RunInfo> combinationToRuns;

    // fill "before FW"
    for (auto& kvBB : filteredCombinationToRunsBeforeFirmwareUpdate) {
        const auto& combo  = kvBB.first;
        const auto& runVec = kvBB.second;

        DataStructures::RunInfo& runInfo = combinationToRuns[combo];
        runInfo.runsBeforeFirmwareUpdate = runVec;
    }
    // fill "after FW"
    for (auto& kvAA : filteredCombinationToRunsAfterFirmwareUpdate) {
        const auto& combo  = kvAA.first;
        const auto& runVec = kvAA.second;

        DataStructures::RunInfo& runInfo = combinationToRuns[combo];
        runInfo.runsAfterFirmwareUpdate = runVec;
    }

    // for debug
    for (auto& kvC : combinationToRuns) {
        finalCombinations.emplace_back(kvC.first, kvC.second);
    }

    // 10) debugMode printing
    if (debugMode) {
        cout << BOLD << BLUE << "\n===== Processing Summary =====\n" << RESET;
        cout << BOLD << "Total runs processed: " << totalRunsProcessed << RESET << "\n";

        // 1) initial combos
        cout << BOLD << "\nInitial Active Trigger Combinations (before splitting due to firmware update):\n" << RESET;
        cout << BOLD << left << setw(60) << "Combination" << right << setw(20) << "Number of Runs" << RESET << "\n";
        cout << string(80, '=') << "\n";
        for (auto& kv0 : initialCombinationToRuns) {
            const auto& combo = kv0.first;
            const auto& runs  = kv0.second;

            string comboStr;
            for (auto& trig : combo) {
                comboStr += trig + " ";
            }
            cout << left << setw(60) << comboStr << right << setw(20) << runs.size() << "\n";
        }

        // 2) combos that had run 47289
        cout << BOLD << "\nCombinations that included run 47289 and were split due to firmware update:\n" << RESET;
        if (combosWithRun47289.empty()) {
            cout << "  None\n";
        } else {
            cout << BOLD << left << setw(60) << "Combination" << RESET << "\n";
            cout << string(60, '=') << "\n";
            for (auto& combination : combosWithRun47289) {
                string comboStr;
                for (auto& trig : combination) {
                    comboStr += trig + " ";
                }
                cout << left << setw(60) << comboStr << "\n";
            }
        }

        // 3) groups w/ same run numbers (before FW)
        cout << BOLD << "\nGroups with identical run numbers before firmware update:\n" << RESET;
        {
            int groupIndex = 1;
            for (auto& kvB : runListToCombinationsBeforeFW) {
                const auto& runList = kvB.first;
                const auto& combos  = kvB.second;

                cout << YELLOW << BOLD << "\nGroup " << groupIndex++ << RESET << "\n";
                cout << "Run List (size " << runList.size() << "):\n";
                for (size_t i=0; i<runList.size(); ++i) {
                    cout << setw(8) << runList[i];
                    if((i+1)%10==0 || i==runList.size()-1) {
                        cout << "\n";
                    }
                }
                cout << "  Combinations:\n";
                for (auto& c : combos) {
                    string comboStr;
                    for (auto& t : c) {
                        comboStr += t + " ";
                    }
                    cout << "    " << comboStr << "\n";
                }
            }
        }

        // 4) groups w/ same run numbers (after FW)
        cout << BOLD << "\nGroups with identical run numbers after firmware update:\n" << RESET;
        {
            int groupIndex = 1;
            for (auto& kvA : runListToCombinationsAfterFW) {
                const auto& runList = kvA.first;
                const auto& combos  = kvA.second;

                cout << YELLOW << BOLD << "\nGroup " << groupIndex++ << RESET << "\n";
                cout << "Run List (size " << runList.size() << "):\n";
                for (size_t i=0; i<runList.size(); ++i) {
                    cout << setw(8) << runList[i];
                    if((i+1)%10==0 || i==runList.size()-1) {
                        cout << "\n";
                    }
                }
                cout << "  Combinations:\n";
                for (auto& c : combos) {
                    string comboStr;
                    for (auto& t : c) {
                        comboStr += t + " ";
                    }
                    cout << "    " << comboStr << "\n";
                }
            }
        }

        // 5) final combos
        cout << BOLD << "\nFinal Active Trigger Combinations (after splitting and filtering):\n" << RESET;
        cout << BOLD << left << setw(60) << "Combination"
             << right << setw(25) << "Runs Before Firmware Update"
             << setw(25) << "Runs After Firmware Update" << RESET << "\n";
        cout << string(110, '=') << "\n";

        for (auto& fc : finalCombinations) {
            const auto& combo   = fc.first;
            const auto& runInfo = fc.second;

            string comboStr;
            for (auto& trig : combo) {
                comboStr += trig + " ";
            }
            cout << left << setw(60) << comboStr;
            cout << right << setw(25) << runInfo.runsBeforeFirmwareUpdate.size();
            cout << setw(25) << runInfo.runsAfterFirmwareUpdate.size() << "\n";

            if (!runInfo.runsBeforeFirmwareUpdate.empty()) {
                cout << "  Runs before firmware update ("
                     << runInfo.runsBeforeFirmwareUpdate.size() << " runs):\n";
                for (size_t i=0; i<runInfo.runsBeforeFirmwareUpdate.size(); i++) {
                    cout << setw(8) << runInfo.runsBeforeFirmwareUpdate[i];
                    if ((i+1)%10==0 || i==runInfo.runsBeforeFirmwareUpdate.size()-1) {
                        cout << "\n";
                    }
                }
            }
            if (!runInfo.runsAfterFirmwareUpdate.empty()) {
                cout << "  Runs after firmware update ("
                     << runInfo.runsAfterFirmwareUpdate.size() << " runs):\n";
                for (size_t i=0; i<runInfo.runsAfterFirmwareUpdate.size(); i++) {
                    cout << setw(8) << runInfo.runsAfterFirmwareUpdate[i];
                    if ((i+1)%10==0 || i==runInfo.runsAfterFirmwareUpdate.size()-1) {
                        cout << "\n";
                    }
                }
            }
        }

        cout << BOLD << BLUE << "\n===== End of Processing Summary =====\n" << RESET;
        // Possibly exit(0);
    }

    return combinationToRuns;
}



void PrintSortedCombinations(const std::map<std::set<std::string>, DataStructures::RunInfo>& combinationToRuns) {
    std::vector<std::pair<std::set<std::string>, DataStructures::RunInfo>> sortedCombinations(
        combinationToRuns.begin(), combinationToRuns.end());
    
    // Sort combinations by the number of triggers in the set (descending)
    std::sort(sortedCombinations.begin(), sortedCombinations.end(),
              [](const auto& a, const auto& b) {
                  return a.first.size() > b.first.size();
              });

    for (const auto& entry : sortedCombinations) {
        const std::set<std::string>& combination = entry.first;
        const DataStructures::RunInfo& runInfo = entry.second;

        std::string combinationName;
        for (const auto& trigger : combination) {
            combinationName += trigger + " ";
        }

        std::cout << "Combination: " << combinationName << "\n";
        if (!runInfo.runsBeforeFirmwareUpdate.empty()) {
            std::cout << "  Runs before firmware update (" << runInfo.runsBeforeFirmwareUpdate.size() << " runs): ";
            for (int run : runInfo.runsBeforeFirmwareUpdate) {
                std::cout << run << " ";
            }
            std::cout << "\n";
        }
        if (!runInfo.runsAfterFirmwareUpdate.empty()) {
            std::cout << "  Runs after firmware update (" << runInfo.runsAfterFirmwareUpdate.size() << " runs): ";
            for (int run : runInfo.runsAfterFirmwareUpdate) {
                std::cout << run << " ";
            }
            std::cout << "\n";
        }
    }
}

void ProcessRunsForCombination(
    const std::string& combinationName,
    const std::vector<int>& runs,
    const std::set<std::string>& triggers,
    const std::string& outputDirectory,
    std::map<std::string, std::vector<int>>& combinationToValidRuns)
{
    // Define the final output ROOT file path
    std::string finalRootFilePath = outputDirectory + "/" + combinationName + "_Combined.root";
    // Define the text file path to store valid runs
    std::string validRunsFilePath = outputDirectory + "/" + combinationName + "_ValidRuns.txt";

    // New file to track runs that have zero data in all histograms
    std::string zeroDataFilePath = outputDirectory + "/RunsWithNoData.txt";
    std::ofstream zeroDataFile(zeroDataFilePath, std::ios::app); // append mode

    bool rootFileExists = !gSystem->AccessPathName(finalRootFilePath.c_str());
    bool validRunsFileExists = !gSystem->AccessPathName(validRunsFilePath.c_str());

    // If both files already exist, skip
    if (rootFileExists && validRunsFileExists) {
        std::cout << "Final ROOT file and valid runs file already exist for combination: "
                  << combinationName << ". Skipping merge.\n";
        // Load valid runs from text file
        std::vector<int> validRuns;
        std::ifstream validRunsFile(validRunsFilePath);
        if (validRunsFile.is_open()) {
            int runNumber;
            while (validRunsFile >> runNumber) {
                validRuns.push_back(runNumber);
            }
            validRunsFile.close();
            combinationToValidRuns[combinationName] = validRuns;
        } else {
            std::cerr << "Failed to open valid runs file: " << validRunsFilePath << std::endl;
        }
        return;
    }

    // Map: triggerName -> (histogramName -> unique_ptr<TH1>)
    std::map<std::string, std::map<std::string, std::unique_ptr<TH1>>> mergedHistograms;

    // Vector of run numbers that have at least some valid histograms
    std::vector<int> validRuns;

    // Loop over each run in this combination
    for (int runNumber : runs) {
        std::stringstream ss;
        ss << outputDirectory << "/" << runNumber << "_HistOutput.root";
        std::string runRootFilePath = ss.str();

        std::cout << "\nProcessing run: " << runNumber
                  << ", file: " << runRootFilePath << std::endl;

        if (gSystem->AccessPathName(runRootFilePath.c_str())) {
            std::cerr << "Run ROOT file does not exist: " << runRootFilePath
                      << ". Skipping run.\n";
            continue;
        }
        std::cout << "Opening run file...\n";
        TFile runFile(runRootFilePath.c_str(), "READ");
        if (runFile.IsZombie() || !runFile.IsOpen()) {
            std::cerr << "Failed to open run ROOT file: " << runRootFilePath
                      << ". Skipping run.\n";
            continue;
        }
        std::cout << "Run file opened successfully.\n";

        bool runHasValidHistogram = false;   // indicates we found at least one histogram
        bool runHasNonZeroEntries = false;   // indicates at least one histogram has >0 entries

        // Loop over each trigger in the combination
        for (const auto& trigger : triggers) {
            // Grab the trigger directory
            TDirectory* triggerDir = runFile.GetDirectory(trigger.c_str());
            if (!triggerDir) {
                std::cerr << "Trigger directory '" << trigger
                          << "' not found for run " << runNumber << ".\n";
                continue;
            }

            // Read keys in that directory
            TIter nextKey(triggerDir->GetListOfKeys());
            TKey* key;
            while ((key = (TKey*)nextKey())) {
                std::string className = key->GetClassName();
                TObject* obj = key->ReadObj();
                if (!obj) {
                    continue;
                }

                // *** CRITICAL CHECKS FOR SEGV AVOIDANCE ***
                // 1) If it's not a TH1 or TH2, skip
                // 2) If you only want to handle 1D vs 2D separately, do it here:
                //    e.g. skip TProfile
                if (!obj->InheritsFrom(TH1::Class())) {
                    std::cout << "[DEBUG] Skipping non-TH1 object: "
                              << key->GetName() << " of class " << className << "\n";
                    delete obj;
                    continue;
                }
                // Optionally skip TProfile:
                if (obj->InheritsFrom(TProfile::Class())) {
                    std::cout << "[DEBUG] Skipping TProfile: "
                              << key->GetName() << "\n";
                    delete obj;
                    continue;
                }

                // Now we can treat it as a TH1
                TH1* hist = dynamic_cast<TH1*>(obj);
                if (!hist) {
                    // This should not happen given the check, but just in case:
                    std::cerr << "[ERROR] dynamic_cast<TH1*> failed for "
                              << key->GetName() << "\n";
                    delete obj;
                    continue;
                }

                std::string histName = hist->GetName();
                // *** If you want to skip merging 1D with 2D, check dimensions ***
                // e.g. if you want only 1D:
                // if (hist->GetDimension() != 1) { skip or handle 2D separately }

                // Clone the histogram
                TH1* histClone = dynamic_cast<TH1*>(hist->Clone());
                if (!histClone) {
                    std::cerr << "[ERROR] Clone failed for histogram: "
                              << histName << "\n";
                    delete obj;
                    continue;
                }
                histClone->SetDirectory(nullptr);

                // Merging logic
                auto& histMap = mergedHistograms[trigger];
                auto it = histMap.find(histName);
                if (it != histMap.end()) {
                    // *** Binning check to avoid Add(...) mismatch
                    TH1* existing = it->second.get();
                    bool canMerge = (existing->GetDimension() == histClone->GetDimension());

                    // (Optional) further check: same number of bins, same edges, etc.
                    if (canMerge) {
                        // e.g. check if # bins match
                        if (existing->GetNbinsX() != histClone->GetNbinsX()) {
                            std::cerr << "[WARNING] Bin mismatch merging '"
                                      << histName << "' from run " << runNumber
                                      << " => skipping.\n";
                            delete histClone;
                            delete obj;
                            continue;
                        }
                        // If 2D, also check Y dimension, etc.
                    }
                    else {
                        std::cerr << "[WARNING] Dimension mismatch merging '"
                                  << histName << "' => skipping.\n";
                        delete histClone;
                        delete obj;
                        continue;
                    }

                    // Safe to merge
                    existing->Add(histClone);
                    delete histClone;
                    std::cout << "Added histogram '" << histName
                              << "' from run " << runNumber
                              << " to existing histogram in trigger '"
                              << trigger << "'.\n";
                } else {
                    // First time seeing this histogram name for this trigger
                    histMap[histName] = std::unique_ptr<TH1>(histClone);
                    std::cout << "Added histogram '" << histName
                              << "' from run " << runNumber
                              << " to merged histograms in trigger '"
                              << trigger << "'.\n";
                }

                // Mark that we had at least one valid histogram
                runHasValidHistogram = true;
                // Check if this histogram has >0 entries
                if (hist->GetEntries() > 0) {
                    runHasNonZeroEntries = true;
                }

                delete obj; // Done with original
            }
        } // end trigger loop

        // If at least one valid histogram was found => record this run
        if (runHasValidHistogram) {
            validRuns.push_back(runNumber);
        }

        // If no histogram in this run had non-zero entries, log it
        if (!runHasNonZeroEntries) {
            zeroDataFile << runNumber << "\n";
            std::cout << "[INFO] Run " << runNumber
                      << " had NO non-zero hist entries => appended to: "
                      << zeroDataFilePath << "\n";
        }

        runFile.Close();
    } // end runs loop

    // If no runs found
    if (validRuns.empty()) {
        std::cout << "[INFO] No valid runs found for combination: "
                  << combinationName << ". Skipping.\n";
        return;
    }

    // If no histograms aggregated
    if (mergedHistograms.empty()) {
        std::cout << "[INFO] No histograms were merged for combination: "
                  << combinationName << ". Skipping output.\n";
        return;
    }

    // Write merged histograms to final ROOT file
    std::cout << "[INFO] Writing merged histograms => "
              << finalRootFilePath << "\n";
    TFile finalFile(finalRootFilePath.c_str(), "RECREATE");
    if (finalFile.IsZombie() || !finalFile.IsOpen()) {
        std::cerr << "[ERROR] Could not create " << finalRootFilePath << "\n";
        mergedHistograms.clear();
        return;
    }
    finalFile.cd();

    // For each trigger => create directory => write histograms
    for (const auto& [trigger, histMap] : mergedHistograms) {
        TDirectory* trigDir = finalFile.mkdir(trigger.c_str());
        if (!trigDir) {
            std::cerr << "[ERROR] Could not mkdir for trigger " << trigger << "\n";
            continue;
        }
        trigDir->cd();

        for (const auto& [histName, histPtr] : histMap) {
            if (!histPtr) {
                std::cerr << "[WARNING] Null histogram? Skipping " << histName << "\n";
                continue;
            }
            histPtr->Write();
            std::cout << "[INFO] Wrote '" << histName
                      << "' in directory '" << trigger << "'\n";
        }
    }

    finalFile.Write();
    finalFile.Close();
    mergedHistograms.clear();

    std::cout << "[INFO] Successfully created combined ROOT file: "
              << finalRootFilePath << "\n";

    // Update valid runs text file
    combinationToValidRuns[combinationName] = validRuns;
    std::ofstream validRunsFile(validRunsFilePath);
    if (validRunsFile.is_open()) {
        for (int rn : validRuns) {
            validRunsFile << rn << "\n";
        }
        validRunsFile.close();
        std::cout << "[INFO] Valid runs => " << validRunsFilePath << "\n";
    } else {
        std::cerr << "[ERROR] Failed to write " << validRunsFilePath << "\n";
    }
    
    // --- NEW DIFF PRINT SECTION ---
    //  At this point, 'runs' is the full list from the CSV
    //  and 'validRuns' is what actually ended up merged.
    //  Here we print out which runs were excluded (if any).
    {
        std::set<int> allRunsSet(runs.begin(), runs.end());
        std::set<int> validRunsSet(validRuns.begin(), validRuns.end());

        std::vector<int> excludedRuns;
        std::set_difference(allRunsSet.begin(),
                            allRunsSet.end(),
                            validRunsSet.begin(),
                            validRunsSet.end(),
                            std::back_inserter(excludedRuns));

        if (!excludedRuns.empty()) {
            std::cout << "\n[DEBUG] For combination " << combinationName
                      << ", the following runs were found in the CSV but NOT "
                      << "included in the final ROOT file:\n   ";
            for (int rn : excludedRuns) {
                std::cout << rn << " ";
            }
            std::cout << "\n";
        } else {
            std::cout << "\n[DEBUG] All runs for combination " << combinationName
                      << " were successfully included in the final ROOT file.\n";
        }
    }
    // zeroDataFile closes automatically when function ends
}


static std::string buildSortedCombinationName(const std::set<std::string>& triggers)
{
    // If the set is just MBD alone => quick return
    if (triggers.size() == 1 && triggers.count("MBD_NandS_geq_1") == 1) {
        return "MBD_NandS_geq_1";
    }

    // We'll gather all triggers except "MBD_NandS_geq_1" in a vector
    // Then parse their threshold and sort them ascending.
    std::vector<std::string> otherTriggers;
    otherTriggers.reserve(triggers.size() - 1);

    for (const auto& trig : triggers) {
        if (trig == "MBD_NandS_geq_1") {
            continue; // skip
        }
        otherTriggers.push_back(trig);
    }

    auto extractThreshold = [&](const std::string& triggerName) -> int {
        // Regex approach: match e.g. "Photon_(\d+)_GeV_plus" or "Jet_(\d+)_GeV_plus"
        static const std::regex re(R"((?:Photon|Jet)_(\d+)_GeV_plus)");
        std::smatch match;
        if (std::regex_search(triggerName, match, re)) {
            // capture group #1 = the digits
            return std::stoi(match[1].str());
        }
        return 0; // fallback if something goes wrong
    };

    // Sort otherTriggers by the integer threshold
    std::sort(otherTriggers.begin(), otherTriggers.end(),
              [&](const std::string& A, const std::string& B) {
                  int thrA = extractThreshold(A);
                  int thrB = extractThreshold(B);
                  return (thrA < thrB);
              }
    );

    // Now build final name, always start with "MBD_NandS_geq_1"
    // then underscore, then sorted triggers.
    std::ostringstream oss;
    oss << "MBD_NandS_geq_1";  // always first
    for (const auto& trig : otherTriggers) {
        oss << "_" << trig;
    }

    return oss.str();
}

/**
 * @brief Merges the histograms for each unique trigger combination into a
 *        single combined ROOT file, labeling them with a sorted combination name.
 *
 *        If a combination is exactly "MBD_NandS_geq_1", we combine all runs
 *        before/after firmware into one file. Otherwise, we split runs
 *        into "beforeTriggerFirmwareUpdate" and "afterTriggerFirmwareUpdate"
 *        as usual.
 *
 * @param combinationToRuns        The map of (trigger set) -> (RunInfo with runsBefore/After)
 * @param outputDirectory          Where to output the combined ROOT files
 * @param combinationToValidRuns   [Output] This map is filled with the final valid run list for each combination
 */
void ProcessAndMergeRootFiles(
    const std::map<std::set<std::string>, DataStructures::RunInfo>& combinationToRuns,
    const std::string& outputDirectory,
    std::map<std::string, std::vector<int>>& combinationToValidRuns)
{
    std::cout << "Starting ProcessAndMergeRootFiles" << std::endl;

    // Disable automatic addition of histograms to directories
    TH1::AddDirectory(false);

    // -------------------------------------------------------------------------
    //  Iterate over each trigger combination
    // -------------------------------------------------------------------------
    for (const auto& kv : combinationToRuns) {
        const std::set<std::string>& triggers = kv.first;
        const DataStructures::RunInfo& runInfo = kv.second;

        // ---------------------------------------------------------------------
        // Build the sorted combination name: always MBD first, then ascending thresholds
        // ---------------------------------------------------------------------
        std::string baseCombinationName = buildSortedCombinationName(triggers);

        // ---------------------------------------------------------------------
        // If combination is exactly {"MBD_NandS_geq_1"} => combine runs (before/after)
        // ---------------------------------------------------------------------
        if (triggers.size() == 1 && triggers.count("MBD_NandS_geq_1") == 1) {
            // Merge runs before & after FW into one
            std::vector<int> allRuns = runInfo.runsBeforeFirmwareUpdate;
            allRuns.insert(allRuns.end(),
                           runInfo.runsAfterFirmwareUpdate.begin(),
                           runInfo.runsAfterFirmwareUpdate.end());

            // This name is simply "MBD_NandS_geq_1" in that scenario
            std::string combinationName = baseCombinationName;
            
            // Merge ROOT files for these runs
            ProcessRunsForCombination(
                combinationName, allRuns, triggers,
                outputDirectory, combinationToValidRuns
            );
        }
        // ---------------------------------------------------------------------
        // Otherwise, handle runsBeforeFirmwareUpdate & runsAfterFirmwareUpdate separately
        // ---------------------------------------------------------------------
        else {
            // 1) Runs before firmware update
            if (!runInfo.runsBeforeFirmwareUpdate.empty()) {
                std::string combinationName = baseCombinationName;
                if (!runInfo.runsAfterFirmwareUpdate.empty()) {
                    // If we also have runsAfter, append suffix
                    combinationName += "_beforeTriggerFirmwareUpdate";
                }
                const std::vector<int>& runs = runInfo.runsBeforeFirmwareUpdate;

                ProcessRunsForCombination(
                    combinationName, runs, triggers,
                    outputDirectory, combinationToValidRuns
                );
            }

            // 2) Runs after firmware update
            if (!runInfo.runsAfterFirmwareUpdate.empty()) {
                std::string combinationName = baseCombinationName;
                if (!runInfo.runsBeforeFirmwareUpdate.empty()) {
                    combinationName += "_afterTriggerFirmwareUpdate";
                }
                const std::vector<int>& runs = runInfo.runsAfterFirmwareUpdate;

                ProcessRunsForCombination(
                    combinationName, runs, triggers,
                    outputDirectory, combinationToValidRuns
                );
            }
        }
    }

    // Re-enable automatic addition of histograms to directories
    TH1::AddDirectory(true);
}



std::vector<std::string> ExtractTriggersFromFilename(const std::string& filename, const std::vector<std::string>& allTriggers) {
    std::string baseName = filename;
    std::string suffix = "_Combined.root";
    if (Utils::EndsWith(baseName, suffix)) {
        baseName = baseName.substr(0, baseName.length() - suffix.length());
    }

    // Strip firmware tag if present
    baseName = Utils::stripFirmwareTag(baseName);
    
    // Split baseName into tokens using underscores
    std::vector<std::string> filenameTokens;
    std::istringstream iss(baseName);
    std::string token;
    while (std::getline(iss, token, '_')) {
        filenameTokens.push_back(token);
    }

    // Tokenize each trigger
    std::vector<std::vector<std::string>> triggerTokensList;
    for (const auto& trigger : allTriggers) {
        std::vector<std::string> triggerTokens;
        std::istringstream triggerStream(trigger);
        std::string triggerToken;
        while (std::getline(triggerStream, triggerToken, '_')) {
            triggerTokens.push_back(triggerToken);
        }
        triggerTokensList.push_back(triggerTokens);
    }

    std::vector<std::string> triggersFound;

    size_t i = 0;
    while (i < filenameTokens.size()) {
        bool foundTrigger = false;
        // Try to match any trigger starting at position i
        for (size_t t = 0; t < allTriggers.size(); ++t) {
            const auto& triggerTokens = triggerTokensList[t];
            if (i + triggerTokens.size() <= filenameTokens.size()) {
                bool matches = true;
                for (size_t j = 0; j < triggerTokens.size(); ++j) {
                    if (filenameTokens[i + j] != triggerTokens[j]) {
                        matches = false;
                        break;
                    }
                }
                if (matches) {
                    // Found a trigger
                    triggersFound.push_back(allTriggers[t]);
                    i += triggerTokens.size();
                    foundTrigger = true;
                    break;
                }
            }
        }
        if (!foundTrigger) {
            // Move to next token
            ++i;
        }
    }

    return triggersFound;
}


// ============================================================================
//  PlotRunByRunHistograms  (jet only, r03 | r05 side‑by‑side, updated)
// ============================================================================
void PlotRunByRunHistograms(
        const std::string& outputDirectory,
        const std::string& plotDirectory,
        const std::vector<std::string>& triggers,
        const std::vector<int>&         runNumbers,
        const std::map<std::string,int>&            triggerColorMap,
        const std::map<std::string,std::string>&    triggerNameMap,
        const std::string& firmwareStatus)
{
    //----------------------------------------------------------------------
    // 0)  constants that must mirror the filling step
    //----------------------------------------------------------------------
    extern const std::array<std::pair<const char*,const char*>,2> kJetRadii;
    constexpr const char* kSuffixRaw    = "_NewTriggerFilling_doNotScale";
    constexpr const char* kSuffixScaled = "";                // “normal / scaled” version

    //----------------------------------------------------------------------
    // 1)  output directories
    //----------------------------------------------------------------------
    const std::string runByRunDir      = plotDirectory + "/runByRunJetOverlays";
    const std::string runByRunIndivDir = plotDirectory + "/runByRunIndividual";
    gSystem->mkdir(runByRunDir.c_str(),      /*recursive=*/true);
    gSystem->mkdir(runByRunIndivDir.c_str(), /*recursive=*/true);

    //----------------------------------------------------------------------
    // 2)  grid parameters
    //----------------------------------------------------------------------
    constexpr int nColumns    = 9;
    constexpr int nRows       = 5;
    const     int runsPerPage = nColumns * nRows;

    const size_t totalRuns  = runNumbers.size();
    const size_t totalPages = (totalRuns + runsPerPage - 1) / runsPerPage;

    //----------------------------------------------------------------------
    // helper: return a “pretty” trigger label irrespective of map orientation
    //----------------------------------------------------------------------
    auto prettyName = [&](const std::string& internal)->std::string
    {
        auto it = triggerNameMap.find(internal);         // internal → pretty ?
        if (it != triggerNameMap.end()) return it->second;

        for (const auto& kv : triggerNameMap)            // pretty → internal ?
            if (kv.second == internal) return kv.first;

        return internal;                                 // fallback
    };

    // =========================================================================
    // 3)  PAGE‑WIDE OVERLAYS  (45 runs per page)
    // =========================================================================
    for (size_t pageIdx = 0; pageIdx < totalPages; ++pageIdx)
    {
        std::unique_ptr<TCanvas> c(
            new TCanvas(Form("overlay_p%zu",pageIdx),
                         "Run‑by‑Run Jet Overlay", 4800, 1500));
        c->Divide(2,1);           // 0 = r03 | 1 = r05

        // subdivide each half into a 9×5 mosaic
        TPad* padRadius[2]{};
        for (int r=0;r<2;++r) {
            c->cd(r+1);
            padRadius[r] = static_cast<TPad*>(c->GetPad(r+1));
            padRadius[r]->Divide(nColumns,nRows);
        }

        std::vector<TH1*>     keepHists;      // page ownership
        std::vector<TLegend*> keepLegends;

        //------------------------------------------------------------------
        // loop over the runs on this page
        //------------------------------------------------------------------
        for (int localIdx = 0; localIdx < runsPerPage; ++localIdx)
        {
            const size_t globalIdx = pageIdx * runsPerPage + localIdx;
            if (globalIdx >= totalRuns) break;

            const int        run   = runNumbers[globalIdx];
            const std::string fInp = outputDirectory + "/" +
                                     std::to_string(run) + "_HistOutput.root";

            std::unique_ptr<TFile> fin( TFile::Open(fInp.c_str(),"READ") );
            if (!fin || fin->IsZombie()) { std::cerr<<"[WARN] cannot open "<<fInp<<"\n"; continue; }

            TLegend* legRadius[2]{};

            //-------------------------------
            // two radii (left / right pad)
            //-------------------------------
            for (int r=0;r<2;++r)
            {
                const std::string tag = "_"+std::string(kJetRadii[r].first); // "_r03"/"_r05"
                const int         subPadId = localIdx + 1;                  // 1…45

                padRadius[r]->cd(subPadId);
                gPad->SetLogy();

                if (!legRadius[r]) {
                    legRadius[r] = new TLegend(0.40,0.60,0.90,0.90);
                    legRadius[r]->SetBorderSize(0);
                    legRadius[r]->SetTextSize(0.07);
                    keepLegends.push_back(legRadius[r]);
                }

                bool firstDraw = true;

                //---------------------------
                // loop over all triggers
                //---------------------------
                for (const std::string& trig : triggers)
                {
                    // try RAW first, then SCALED
                    const std::string names[2] = {
                        "h_leadingJetET" + tag + kSuffixRaw    + "_" + trig,
                        "h_leadingJetET" + tag + kSuffixScaled + "_" + trig
                    };

                    TH1* hFound = nullptr;
                    for (const std::string& hname : names)
                    {
                        if (TDirectory* td = fin->GetDirectory(trig.c_str()))
                        {
                            if (TH1* h = static_cast<TH1*>(td->Get(hname.c_str())))
                            { hFound = h; break; }
                        }
                    }
                    if (!hFound) continue;    // neither variant exists → skip

                    auto* hc = static_cast<TH1*>(hFound->Clone());
                    hc->SetDirectory(nullptr);
                    keepHists.push_back(hc);

                    int col = kBlack;
                    auto itCol = triggerColorMap.find(trig);
                    if (itCol != triggerColorMap.end()) col = itCol->second;

                    hc->SetLineColor(col); hc->SetLineWidth(2);
                    hc->GetXaxis()->SetRangeUser(0.,50.);

                    // kill <7 GeV bins
                    for (int b=1;b<=hc->GetNbinsX();++b)
                        if (hc->GetBinCenter(b) < 7.) {
                            hc->SetBinContent(b,0); hc->SetBinError(b,0);
                        }

                    firstDraw ? hc->Draw("hist") : hc->Draw("hist same");
                    firstDraw = false;

                    legRadius[r]->AddEntry(hc, prettyName(trig).c_str(), "l");
                } // trigger loop

                legRadius[r]->Draw();

                TLatex lt; lt.SetNDC(); lt.SetTextSize(0.10); lt.SetTextAlign(13);
                lt.DrawLatex(0.55,0.45,Form("Run %d",run));
            } // radius loop
        }     // run loop

        //--------------------------------------------------------------
        // firmware banner (once per canvas)
        //--------------------------------------------------------------
        if (!firmwareStatus.empty()) {
            c->cd();
            TLatex tx; tx.SetNDC(); tx.SetTextAlign(22); tx.SetTextSize(0.025);
            tx.DrawLatex(0.5,0.96, firmwareStatus.c_str());
        }

        //--------------------------------------------------------------
        // save & clean
        //--------------------------------------------------------------
        std::string pngOut = runByRunDir + "/RunOverlayJet_Page" +
                             std::to_string(pageIdx+1) + ".png";
        c->SaveAs(pngOut.c_str());
        std::cout<<"[INFO] wrote "<<pngOut<<"\n";

        for (TH1* h: keepHists)     delete h;
        for (TLegend* l: keepLegends) delete l;
    } // page loop



    // =========================================================================
    // 4)  INDIVIDUAL RUN PNGs  (side‑by‑side radii)
    // =========================================================================
    for (int run : runNumbers)
    {
        const std::string fInp = outputDirectory + "/" +
                                 std::to_string(run) + "_HistOutput.root";

        std::unique_ptr<TFile> fin( TFile::Open(fInp.c_str(),"READ") );
        if (!fin || fin->IsZombie()) { std::cerr<<"[WARN] cannot open "<<fInp<<"\n"; continue; }

        std::unique_ptr<TCanvas> c(
            new TCanvas(Form("indiv_%d",run), "Individual Run", 1600,600));
        c->Divide(2,1);

        std::vector<TH1*> keepH;

        for (int r=0;r<2;++r)
        {
            const std::string tag = "_"+std::string(kJetRadii[r].first);
            c->cd(r+1)->SetLogy();

            TLegend* leg = new TLegend(0.45,0.60,0.90,0.90);
            leg->SetBorderSize(0); leg->SetTextSize(0.04);

            bool first = true;
            for (const std::string& trig : triggers)
            {
                const std::string names[2] = {
                    "h_leadingJetET" + tag + kSuffixRaw    + "_" + trig,
                    "h_leadingJetET" + tag + kSuffixScaled + "_" + trig
                };

                TH1* hFound = nullptr;
                for (const std::string& hname : names)
                {
                    if (TDirectory* td = fin->GetDirectory(trig.c_str()))
                        if (TH1* h = static_cast<TH1*>(td->Get(hname.c_str()))) { hFound = h; break; }
                }
                if (!hFound) continue;

                auto* hc = static_cast<TH1*>(hFound->Clone());
                hc->SetDirectory(nullptr); keepH.push_back(hc);

                int col = kBlack;
                auto itCol = triggerColorMap.find(trig);
                if (itCol!=triggerColorMap.end()) col = itCol->second;

                hc->SetLineColor(col); hc->SetLineWidth(2);

                for (int b=1;b<=hc->GetNbinsX();++b)
                    if (hc->GetBinCenter(b) < 7.) {
                        hc->SetBinContent(b,0); hc->SetBinError(b,0);
                    }

                first ? hc->Draw("hist") : hc->Draw("hist same");
                first = false;

                leg->AddEntry(hc, prettyName(trig).c_str(), "l");
            }
            leg->Draw();
        }

        TLatex lt; lt.SetNDC(); lt.SetTextAlign(22); lt.SetTextSize(0.05);
        lt.DrawLatex(0.5,0.04,Form("Run %d",run));

        std::string outPng = runByRunIndivDir + "/Run" + std::to_string(run) + ".png";
        c->SaveAs(outPng.c_str());
        std::cout<<"[INFO] wrote "<<outPng<<"\n";

        for (TH1* h: keepH) delete h;
    }
}




Double_t newExpFunction(Double_t *x, Double_t *p)
{
    double baseline = p[0];
    double scale    = p[1];
    double alpha    = p[2];   // log(slope)
    double xOff     = p[3];

    double slope = TMath::Exp(alpha);
    double expo  = TMath::Exp( slope * (x[0] - xOff) );
    return baseline + scale * expo;
}



Double_t logisticFreeAmp(Double_t *x, Double_t *p)
{
    // p[0] = amp      (asymptotic plateau)
    // p[1] = alpha    (log of slope)
    // p[2] = xOffset

    double amp     = p[0];
    double alpha   = p[1];
    double xOffset = p[2];
    double slope   = TMath::Exp(alpha); // always > 0

    // logistic: amp / (1 + e^{-slope*(x - xOffset)})
    return amp / (1.0 + TMath::Exp(-slope * (x[0] - xOffset)));
}

Double_t logistic4Param(Double_t *x, Double_t *p)
{
    double low     = p[0];
    double high    = p[1];
    double alpha   = p[2];        // log(slope)
    double xOffset = p[3];
    double slope   = TMath::Exp(alpha);
    // Now the logistic has two asymptotes:
    //    as x -> -∞, f(x) -> low
    //    as x -> +∞, f(x) -> high
    return low + (high - low) / (1.0 + TMath::Exp(-slope * (x[0] - xOffset)));
}


Double_t erfFreeAmp(Double_t *x, Double_t *p)
{
    // p[0] = amp
    // p[1] = alpha  (log of slope)
    // p[2] = xOffset

    double amp     = p[0];
    double alpha   = p[1];
    double xOffset = p[2];
    double slope   = TMath::Exp(alpha);

    // erf:  0.5 * amp * [1 + erf( slope*(x - xOffset)/sqrt(2) )]
    double arg = slope * (x[0] - xOffset) / TMath::Sqrt2();
    return 0.5 * amp * (1.0 + TMath::Erf(arg));
}


Double_t erf4Param(Double_t *x, Double_t *p)
{
    // p[0] = baseline
    // p[1] = scale   (the height of the turn-on above the baseline)
    // p[2] = alpha   (log(slope))
    // p[3] = xOffset

    double base    = p[0];
    double height  = p[1];
    double alpha   = p[2];
    double xOffset = p[3];
    double slope   = TMath::Exp(alpha);

    // f(x) = baseline + height * 0.5 [1 + erf(...)]
    double arg = slope * (x[0] - xOffset) / TMath::Sqrt2();
    double erfTerm = 0.5 * (1.0 + TMath::Erf(arg));
    return base + height * erfTerm;
}



Double_t gumbelFreeAmp(Double_t *x, Double_t *p)
{
    // p[0] = amp
    // p[1] = alpha   (log of slope)
    // p[2] = xOffset

    double amp     = p[0];
    double alpha   = p[1];
    double xOffset = p[2];
    double slope   = TMath::Exp(alpha);

    // Gumbel: amp * exp( - exp( - slope*(x - xOffset) ) )
    double z   = slope * (x[0] - xOffset);
    double val = TMath::Exp(-TMath::Exp(-z));
    return amp * val;
}


Double_t gumbel4Param(Double_t *x, Double_t *p)
{
    // p[0] = baseline
    // p[1] = scale
    // p[2] = alpha   (log(slope))
    // p[3] = xOffset

    double base    = p[0];
    double scale   = p[1];
    double alpha   = p[2];
    double xOffset = p[3];
    double slope   = TMath::Exp(alpha);

    // f(x) = base + scale * exp( - exp( -slope*(x - xOffset) ) )
    double z   = slope * (x[0] - xOffset);
    double val = TMath::Exp(-TMath::Exp(-z));
    return base + scale * val;
}




// Tries to find a ~50% crossing of ratio, and an approximate slope from
// how quickly the ratio goes from ~0.2 to ~0.8.
// If it can’t find them well, it falls back on some default values.
void autoEstimateAlphaOffset(TH1* h, double &alphaGuess, double &xOffGuess)
{
    // Fallback defaults (like your old -0.7 and 10)
    alphaGuess = -0.7;
    xOffGuess  = 10.0;

    if (!h || h->GetNbinsX()<5) return;

    // We'll record the energies where ratio ~ 0.2, 0.5, 0.8
    // (just scanning bins)
    double x20=-999, x50=-999, x80=-999;

    for (int i=1; i<=h->GetNbinsX(); ++i) {
        double y    = h->GetBinContent(i);
        double xc   = h->GetBinCenter(i);
        // store approximate points
        if (y>0.2 && x20<0)  x20 = xc;
        if (y>0.5 && x50<0)  x50 = xc;
        if (y>0.8 && x80<0)  x80 = xc;
    }

    // If we found some decent points (all in order), estimate slope
    if (x20>0 && x50>0 && x80>0 && x20<x50 && x50<x80) {
        xOffGuess  = x50;  // offset ~ 50% crossing

        // approximate slope from 20->80 range:
        double dx  = (x80 - x20);
        if (dx < 1e-6) dx = 1.0; // safety
        // logistic goes from 0.2->0.8 => ratio changes by ln(0.8/0.2)=ln(4)=1.386...
        // so slope ~ 1.386/dx
        double slopeGuess = 1.386 / dx;
        if (slopeGuess<1e-9) slopeGuess=1e-9; // be safe
        alphaGuess = TMath::Log(slopeGuess);
    }
}



// -------------------------------------------------------------------------
// 1) Define new simpler versions of logistic/erf/gumbel that do NOT use log(slope)
// -------------------------------------------------------------------------
Double_t logisticSimpleAmp(Double_t *x, Double_t *p)
{
    // p[0] = Amp       (asymptotic plateau)
    // p[1] = slope     (positive slope)
    // p[2] = xOffset   (where the transition is ~50%)
    // logistic: p[0] / (1 + exp( -p[1]*(x - p[2]) ))
    double amp     = p[0];
    double slope   = p[1];
    double xOffset = p[2];
    return amp / (1.0 + TMath::Exp(-slope * (x[0] - xOffset)));
}

Double_t erfSimpleAmp(Double_t *x, Double_t *p)
{
    // p[0] = Amp
    // p[1] = slope
    // p[2] = xOffset
    // Return 0.5 * amp * [1 + erf( slope*(x-xOffset)/sqrt(2) )]
    double amp     = p[0];
    double slope   = p[1];
    double xOffset = p[2];
    double arg     = slope * (x[0] - xOffset) / TMath::Sqrt2();
    return 0.5 * amp * (1.0 + TMath::Erf(arg));
}

Double_t gumbelSimpleAmp(Double_t *x, Double_t *p)
{
    // p[0] = Amp
    // p[1] = slope
    // p[2] = xOffset
    // Gumbel:  amp * exp( - exp( -slope*(x - xOffset) ) )
    double amp     = p[0];
    double slope   = p[1];
    double xOffset = p[2];
    double z       = slope * (x[0] - xOffset);
    return amp * TMath::Exp(-TMath::Exp(-z));
}

// -------------------------------------------------------------------------
// 2) A new auto-estimate helper to guess (amp, slope, xOffset)
//    without taking log(slope).  You can adapt if your histograms differ.
// -------------------------------------------------------------------------
void autoEstimateSlopeOffsetSimple(TH1* h,
                                   double &ampGuess,
                                   double &slopeGuess,
                                   double &xOffGuess)
{
    // Default guesses
    ampGuess   = 1.0;
    slopeGuess = 1.0;
    xOffGuess  = 5.0;
    if (!h || h->GetNbinsX()<5) return;

    // 2a) Guess the amplitude from the largest bin content
    //     (If your final plateau is near 1, this will be close to 1.0)
    double maxVal = 0.0;
    for (int i=1; i<=h->GetNbinsX(); ++i) {
        double y = h->GetBinContent(i);
        if (y > maxVal) maxVal = y;
    }
    ampGuess = maxVal;

    // 2b) Find approximate 20%, 50%, 80% crossing
    //     That is 0.2*ampGuess, 0.5*ampGuess, 0.8*ampGuess.
    double y20 = 0.20 * ampGuess;
    double y50 = 0.50 * ampGuess;
    double y80 = 0.80 * ampGuess;

    double x20=-999, x50=-999, x80=-999;
    for (int i=1; i<=h->GetNbinsX(); ++i)
    {
        double y  = h->GetBinContent(i);
        double xc = h->GetBinCenter(i);

        // store approximate points
        if (y > y20 && x20<0) x20 = xc;
        if (y > y50 && x50<0) x50 = xc;
        if (y > y80 && x80<0) x80 = xc;
    }

    // 2c) If we found good points, estimate slope from x20->x80,
    //     and xOff from x50.  logistic or erf range from ~0->amp
    //     so going from 0.2->0.8 is ln(4)=1.386 for logistic,
    //     or (erf^-1(0.6)-erf^-1( -0.3 ), etc.) — we’ll just assume
    //     a small approximate factor.  Gumbel is similar.  This is
    //     not exact, but usually good enough for initialization.
    if (x20>0 && x50>0 && x80>0 && (x20 < x80))
    {
        xOffGuess  = x50; // ~50% point

        double dx = x80 - x20;
        if (dx < 1e-6) dx = 1.0;
        // For a logistic shape from 20%->80% we have ratio=4 => log(4)=1.386...
        // For erf/gumbel you could use a similar factor ~1–2 for the slope guess
        // This is a crude guess but usually good enough for a first iteration
        slopeGuess = 1.386 / dx;
    }
}


TFitResultPtr iterativeFit(TH1* hist, TF1* func, const char* fitOpts,
                           int maxIters=20, double minEdm=1e-9)
{
    // Combine user’s fit options with "N" so no lines are automatically drawn.
    std::string localOpts = fitOpts;
    if (localOpts.find('N') == std::string::npos) {
        localOpts += "N";  // Add "N" if not already present
    }

    TFitResultPtr res;
    for (int i = 0; i < maxIters; ++i)
    {
        // Fit with Minuit2, but do NOT draw the function
        res = hist->Fit(func, localOpts.c_str());
        
        // Break out early if EDM is below threshold
        if (res->Edm() < minEdm) break;
    }
    return res;
}



void fitComparison6in1_4row(TH1* ratioJet,
                            const std::string& jetTrig,
                            const std::string& combinationName,
                            Color_t color,
                            const std::string& plotDirectory)
{

    /// ------------------------------------------------------------------
    /// hard‑coded name used to decide which combination gets the 9‑fit plot
    static const std::string specialCombo = "MBD_NandS_geq_1_Jet_12_GeV_plus_MBD_NS_geq_1_afterTriggerFirmwareUpdate";
    /// ------------------------------------------------------------------
    if (combinationName != specialCombo) {
        // Immediately return if not the desired combination
        return;
    }

    if (!ratioJet) {
        std::cerr << "[WARN] ratioJet is null!\n";
        return;
    }

    //----------------------------------------------------------------------
    // Prepare the output ROOT file (in UPDATE mode) so we can store TF1’s
    //----------------------------------------------------------------------
    const std::string rootOutName = plotDirectory + "/FitFunctions_" + jetTrig + ".root";
    TFile* fOut = TFile::Open(rootOutName.c_str(), "UPDATE");
    if (!fOut || fOut->IsZombie()) {
        std::cerr << "[ERROR] Could not open or create ROOT file: " << rootOutName << std::endl;
        return;
    }

    // Create/get the top directory for this trigger
    TDirectory* trigDir = dynamic_cast<TDirectory*>( fOut->Get(jetTrig.c_str()) );
    if (!trigDir) {
        trigDir = fOut->mkdir(jetTrig.c_str());
    }
    trigDir->cd();

    // (A) We have 9 methods total:
    std::vector<std::string> methodList = {
        // Original 3-parameter fits
        "logisticFreeAmp",
        "erfFreeAmp",
        "gumbelFreeAmp",

        // Original 4-parameter fits
        "logistic4Param",
        "erf4Param",
        "gumbel4Param",

        // New "simple" 3-parameter fits
        "logisticSimpleAmp",
        "erfSimpleAmp",
        "gumbelSimpleAmp"
    };

    // Titles for each method
    std::map<std::string, std::string> methodTitle = {
        {"logisticFreeAmp",   "Logistic (3p)"},
        {"erfFreeAmp",        "Erf (3p)"},
        {"gumbelFreeAmp",     "Gumbel (3p)"},
        {"logistic4Param",    "Logistic (4p)"},
        {"erf4Param",         "Erf (4p)"},
        {"gumbel4Param",      "Gumbel (4p)"},
        {"logisticSimpleAmp", "Logistic (simple 3p)"},
        {"erfSimpleAmp",      "Erf (simple 3p)"},
        {"gumbelSimpleAmp",   "Gumbel (simple 3p)"}
    };

    // Parameter labels (used in TF1::SetParName)
    std::map<std::string, std::vector<std::string>> paramLabels = {
        {"logisticFreeAmp",   {"Amplitude", "Alpha (log slope)", "Offset"}},
        {"erfFreeAmp",        {"Amplitude", "Alpha (log slope)", "Offset"}},
        {"gumbelFreeAmp",     {"Amplitude", "Alpha (log slope)", "Offset"}},

        {"logistic4Param",    {"Low Plateau", "High Plateau", "Alpha (log slope)", "Offset"}},
        {"erf4Param",         {"Baseline",    "Scale",        "Alpha (log slope)", "Offset"}},
        {"gumbel4Param",      {"Baseline",    "Scale",        "Alpha (log slope)", "Offset"}},

        // The "simple" variants use direct slope, not log(slope)
        {"logisticSimpleAmp", {"Amp", "Slope", "Offset"}},
        {"erfSimpleAmp",      {"Amp", "Slope", "Offset"}},
        {"gumbelSimpleAmp",   {"Amp", "Slope", "Offset"}}
    };

    // Store final results in this struct (same as before)
    struct FitResults {
        double chi2;
        double ndf;
        double chi2NDF;
        double x95;
        std::vector<std::string> parName;
        std::vector<double> par;
        std::vector<double> parErr;
    };

    const int nMethods = (int)methodList.size(); // 9
    std::vector<FitResults> results(nMethods);

    // Official fit range in code is [8.0, 30.0] for jets
    double fitXmin = 8.0;
    double fitXmax = 30.0;

    // Create a 3×3 canvas => 9 pads (one per method)
    TCanvas* cFit = new TCanvas("cFit",
                                "Comparison (logistic/erf/gumbel, 3p/4p/simple)",
                                1600, 1200);
    cFit->Divide(3, 3);

    //----------------------------------------------------------------------
    // (B) Loop over the 9 methods
    //----------------------------------------------------------------------
    for (int i = 0; i < nMethods; i++)
    {
        // Go to sub-pad i+1
        cFit->cd(i + 1);
        gPad->SetGrid();

        const std::string& fitMethod = methodList[i];
        bool is4p = (fitMethod.find("4Param") != std::string::npos);

        // Number of parameters
        int nPars = (is4p ? 4 : 3);
        bool usesLogSlope = (fitMethod.find("Simple") == std::string::npos);

        // Create function over [fitXmin, fitXmax]
        TF1* finalFunc = nullptr;
        if      (fitMethod == "logisticFreeAmp")
            finalFunc = new TF1(Form("finalFunc_log3p_%d", i),
                                logisticFreeAmp, fitXmin, fitXmax, nPars);
        else if (fitMethod == "erfFreeAmp")
            finalFunc = new TF1(Form("finalFunc_erf3p_%d", i),
                                erfFreeAmp, fitXmin, fitXmax, nPars);
        else if (fitMethod == "gumbelFreeAmp")
            finalFunc = new TF1(Form("finalFunc_gum3p_%d", i),
                                gumbelFreeAmp, fitXmin, fitXmax, nPars);
        else if (fitMethod == "logistic4Param")
            finalFunc = new TF1(Form("finalFunc_log4p_%d", i),
                                logistic4Param, fitXmin, fitXmax, nPars);
        else if (fitMethod == "erf4Param")
            finalFunc = new TF1(Form("finalFunc_erf4p_%d", i),
                                erf4Param, fitXmin, fitXmax, nPars);
        else if (fitMethod == "gumbel4Param")
            finalFunc = new TF1(Form("finalFunc_gum4p_%d", i),
                                gumbel4Param, fitXmin, fitXmax, nPars);
        else if (fitMethod == "logisticSimpleAmp")
            finalFunc = new TF1(Form("finalFunc_logSimple_%d", i),
                                logisticSimpleAmp, fitXmin, fitXmax, nPars);
        else if (fitMethod == "erfSimpleAmp")
            finalFunc = new TF1(Form("finalFunc_erfSimple_%d", i),
                                erfSimpleAmp, fitXmin, fitXmax, nPars);
        else // gumbelSimpleAmp
            finalFunc = new TF1(Form("finalFunc_gumSimple_%d", i),
                                gumbelSimpleAmp, fitXmin, fitXmax, nPars);

        // Assign parameter names (for clarity / CSV)
        for (int ip = 0; ip < nPars; ip++) {
            finalFunc->SetParName(ip, paramLabels[fitMethod][ip].c_str());
        }

        // Style
        finalFunc->SetLineColor(color);
        finalFunc->SetLineWidth(3);

        // Auto-init guesses
        if (usesLogSlope && !is4p) {
            // 3p with log-slope
            double alphaGuess = -0.7;
            double xOffGuess  = 7.0;
            autoEstimateAlphaOffset(ratioJet, alphaGuess, xOffGuess);
            finalFunc->SetParameter(0, 1.0);
            finalFunc->SetParameter(1, alphaGuess);
            finalFunc->SetParameter(2, xOffGuess);
        }
        else if (usesLogSlope && is4p) {
            // 4p with log-slope
            double alphaGuess = -0.7;
            double xOffGuess  = 7.0;
            autoEstimateAlphaOffset(ratioJet, alphaGuess, xOffGuess);
            if (fitMethod == "logistic4Param") {
                finalFunc->SetParameter(0, 0.0); // Low
                finalFunc->SetParameter(1, 1.0); // High
            } else {
                finalFunc->SetParameter(0, 0.0); // Baseline
                finalFunc->SetParameter(1, 1.0); // Scale
            }
            finalFunc->SetParameter(2, alphaGuess);
            finalFunc->SetParameter(3, xOffGuess);
        }
        else {
            // "Simple" 3p
            double ampGuess   = 1.0;
            double slopeGuess = 1.0;
            double xOffGuess  = 5.0;
            autoEstimateSlopeOffsetSimple(ratioJet, ampGuess, slopeGuess, xOffGuess);
            finalFunc->SetParameter(0, ampGuess);
            finalFunc->SetParameter(1, slopeGuess);
            finalFunc->SetParameter(2, xOffGuess);
        }

        // Perform the fit
        TFitResultPtr fitRes = iterativeFit(ratioJet, finalFunc, "R S Q");

        // Extract final parameters
        std::vector<std::string> paramNames(nPars);
        std::vector<double> pars(nPars), errs(nPars);
        for (int ip = 0; ip < nPars; ip++) {
            paramNames[ip] = finalFunc->GetParName(ip);
            pars[ip]       = finalFunc->GetParameter(ip);
            errs[ip]       = finalFunc->GetParError(ip);
        }

        // Draw the histogram
        ratioJet->SetMarkerStyle(20);
        ratioJet->SetMarkerSize(0.9);
        ratioJet->Draw("E1");
        ratioJet->GetXaxis()->SetRangeUser(0., 50.);
        ratioJet->GetYaxis()->SetRangeUser(0., 1.4);

        // Mark [fitXmin, fitXmax]
        {
            TLine* leftBound  = new TLine(fitXmin, 0.0, fitXmin, 1.4);
            leftBound->SetLineStyle(3);
            leftBound->SetLineColor(kGray+2);
            leftBound->Draw("SAME");

            TLine* rightBound = new TLine(fitXmax, 0.0, fitXmax, 1.4);
            rightBound->SetLineStyle(3);
            rightBound->SetLineColor(kGray+2);
            rightBound->Draw("SAME");
        }

        // Build an extension function from 0..50 with same params
        TF1* extFunc = (TF1*)finalFunc->Clone(Form("extFunc_%s_%d", fitMethod.c_str(), i));
        extFunc->SetRange(0.0, 50.0);
        extFunc->SetLineColor(color);
        extFunc->SetLineStyle(2);
        extFunc->SetLineWidth(2);

        // Draw extended (dashed), then the official fitted function
        extFunc->Draw("SAME");
        finalFunc->Draw("SAME");

        // Horizontal line y=1
        {
            double hMin = ratioJet->GetXaxis()->GetXmin();
            double hMax = ratioJet->GetXaxis()->GetXmax();
            TLine* hLine = new TLine(hMin, 1.0, hMax, 1.0);
            hLine->SetLineStyle(2);
            hLine->SetLineColor(kBlack);
            hLine->Draw("SAME");
        }

        // Compute x95 exactly
        double x95 = 0.0;
        if (fitMethod == "logisticFreeAmp") {
            double slopeF = TMath::Exp(pars[1]);
            double val    = (1./0.95) - 1.;
            double exponent = -TMath::Log(val);
            x95 = pars[2] + exponent / slopeF;
        }
        else if (fitMethod == "erfFreeAmp") {
            double slopeF = TMath::Exp(pars[1]);
            double frac   = 2.*0.95 - 1.; // =>0.90
            double zVal   = TMath::ErfInverse(frac);
            x95 = pars[2] + (TMath::Sqrt2() * zVal / slopeF);
        }
        else if (fitMethod == "gumbelFreeAmp") {
            double slopeF = TMath::Exp(pars[1]);
            x95 = pars[2] + 2.97 / slopeF; // ~2.97 = -ln(-ln(0.95))
        }
        else if (fitMethod == "logistic4Param") {
            double slopeF = TMath::Exp(pars[2]);
            double val    = (1./0.95) - 1.;
            double exponent = -TMath::Log(val);
            x95 = pars[3] + exponent / slopeF;
        }
        else if (fitMethod == "erf4Param") {
            double slopeF = TMath::Exp(pars[2]);
            double frac   = 2.*0.95 - 1.;
            double zVal   = TMath::ErfInverse(frac);
            x95 = pars[3] + (TMath::Sqrt2() * zVal / slopeF);
        }
        else if (fitMethod == "gumbel4Param") {
            double slopeF = TMath::Exp(pars[2]);
            double lhs    = -TMath::Log(0.95);
            double zVal   = -TMath::Log(lhs); // ~2.97
            x95 = pars[3] + (zVal / slopeF);
        }
        else if (fitMethod == "logisticSimpleAmp") {
            double slope = pars[1];
            double val   = (1./0.95) - 1.;
            double exponent = -TMath::Log(val);
            x95 = pars[2] + exponent / slope;
        }
        else if (fitMethod == "erfSimpleAmp") {
            double slope = pars[1];
            double frac  = 2.*0.95 - 1.;
            double zVal  = TMath::ErfInverse(frac);
            x95 = pars[2] + (TMath::Sqrt2() * zVal / slope);
        }
        else if (fitMethod == "gumbelSimpleAmp") {
            double amp   = pars[0];
            double slope = pars[1];
            double ratio = 0.95 / amp;
            if (ratio>0 && ratio<1) {
                double zVal = -TMath::Log(-TMath::Log(ratio));
                x95 = pars[2] + zVal / slope;
            }
        }

        // Mark x95 if in [fitXmin, fitXmax]
        if (x95 > fitXmin && x95 < fitXmax) {
            double y95 = finalFunc->Eval(x95);
            if (y95 > 1.4) y95 = 1.4; // clip at the pad top
            TLine* line95 = new TLine(x95, 0., x95, y95);
            line95->SetLineStyle(2);
            line95->SetLineColor(color);
            line95->SetLineWidth(2);
            line95->Draw("SAME");
        }

        // Retrieve stats
        double chi2  = fitRes->Chi2();
        double ndf   = fitRes->Ndf();
        double c2Ndf = (ndf > 0) ? (chi2 / ndf) : 0.0;

        // (NEW) Draw text for method name, x95, and chi2/ndf in this pad
        {
            TLatex latPad;
            latPad.SetNDC(true);
            latPad.SetTextFont(42);
            latPad.SetTextSize(0.045);
            latPad.SetTextAlign(13); // top-left

            double x0 = 0.57;
            double y0 = 0.65;

            latPad.DrawLatex(x0, y0,
                Form("#bf{%s}", methodTitle.at(fitMethod).c_str()));
            y0 -= 0.07;
            latPad.DrawLatex(x0, y0,
                Form("#bf{x_{95} = %.1f GeV}", x95));
            y0 -= 0.07;
            latPad.DrawLatex(x0, y0,
                Form("#bf{#chi^{2}/NDF = %.3f}", c2Ndf));
        }

        // Also top-right label with the trigger name
        {
            TLatex latTrig;
            latTrig.SetNDC(true);
            latTrig.SetTextFont(42);
            latTrig.SetTextSize(0.05);
            latTrig.SetTextAlign(33); // top-right
            latTrig.DrawLatex(0.90, 0.90, Form("#bf{%s}", jetTrig.c_str()));
        }

        // Extra legend at bottom-right
        {
            TLegend* extraLegend = new TLegend(0.5, 0.2, 0.8, 0.33);
            extraLegend->SetBorderSize(0);
            extraLegend->SetFillStyle(0);
            extraLegend->SetTextSize(0.05);
            extraLegend->AddEntry((TObject*)nullptr, "#it{#bf{sPHENIX}} Internal", "");
            extraLegend->AddEntry((TObject*)nullptr, "p+p #sqrt{s} = 200 GeV", "");
            extraLegend->Draw();
        }

        // Save numeric results
        results[i].chi2      = chi2;
        results[i].ndf       = ndf;
        results[i].chi2NDF   = c2Ndf;
        results[i].x95       = x95;
        results[i].parName   = paramNames;
        results[i].par       = pars;
        results[i].parErr    = errs;

        // ------------------------------------------------------------------
        // (C) Write this TF1 and the histogram into the ROOT file
        //     in subdirectories named after the method (NOT the combination)
        // ------------------------------------------------------------------
        trigDir->cd();
        TDirectory* methodDir =
            dynamic_cast<TDirectory*>( trigDir->Get(fitMethod.c_str()) );
        if (!methodDir) {
            methodDir = trigDir->mkdir(fitMethod.c_str());
        }
        methodDir->cd();

        finalFunc->SetName("finalFunc");
        finalFunc->Write("", TObject::kOverwrite);

        // Optionally store the histogram in the same subdir
        // (If you call this function multiple times on the same histogram,
        //  or repeatedly, it will keep overwriting the same object name.)
        ratioJet->SetName("ratioHist");
        ratioJet->Write("", TObject::kOverwrite);

        delete extFunc;
    } // end for (methods)

    // ----------------------------------------------------------------------
    // Save the big canvas as PDF
    // ----------------------------------------------------------------------
    cFit->cd();
    cFit->Update();
    std::string outName = plotDirectory + "/Comparison9in1_" + jetTrig + ".pdf";
    cFit->SaveAs(outName.c_str());
    delete cFit;

    // ----------------------------------------------------------------------
    // Write the same info to CSV
    // ----------------------------------------------------------------------
    std::string csvName = plotDirectory + "/FitResults_" + jetTrig + ".csv";
    std::ofstream ofs(csvName.c_str());
    if (!ofs.is_open()) {
        std::cerr << "[ERROR] Could not open CSV file for writing: " << csvName << std::endl;
        fOut->Close();
        return;
    }

    // Header line
    ofs << "Method,Chi2NDF,Chi2,NDF,x95GeV";
    for (int pIndex = 0; pIndex < 4; pIndex++) {
        ofs << ",Par" << pIndex << "Name"
            << ",Par" << pIndex << "Val"
            << ",Par" << pIndex << "Err";
    }
    ofs << "\n";

    // Fill lines
    for (int i = 0; i < nMethods; i++)
    {
        const std::string& fitMeth = methodList[i];
        int nPars = (fitMeth.find("4Param") != std::string::npos) ? 4 : 3;

        ofs << fitMeth
            << "," << results[i].chi2NDF
            << "," << results[i].chi2
            << "," << results[i].ndf
            << "," << results[i].x95;

        for (int pIndex = 0; pIndex < 4; pIndex++) {
            if (pIndex < nPars) {
                ofs << "," << results[i].parName[pIndex]
                    << "," << results[i].par[pIndex]
                    << "," << results[i].parErr[pIndex];
            } else {
                ofs << ",,,";
            }
        }
        ofs << "\n";
    }

    ofs.close();
    std::cout << "[INFO] Wrote CSV file: " << csvName << std::endl;

    //----------------------------------------------------------------------
    // Finally, close the ROOT file
    //----------------------------------------------------------------------
    fOut->Close();
    std::cout << "[INFO] ROOT file updated: " << rootOutName << std::endl;
}




// ============================================================================
//  PlotCombinedHistograms  (compile‑clean version, Jet R=0.3 / 0.5 naming)
// ============================================================================
void PlotCombinedHistograms(
        const std::string&                        outputDirectory,
        const std::vector<std::string>&           combinedRootFiles,
        const std::map<std::string,std::vector<int>>& combinationToValidRuns,
        const std::string&                        fitFunctionType = "sigmoid",
        bool                                      fitOnly          = true,
        const std::string&                        histogramType   = "maxEnergy")
{
    // ------------------------------------------------------------------ 0. helpers
    const auto& allTriggers   = TriggerConfig::allTriggers;
    const auto& trigColor     = TriggerConfig::triggerColorMap;
    const auto& pretty2Int    = TriggerConfig::triggerNameMap;    // Pretty → internal

    /* quick reverse lookup so we can print the pretty label when we only
       know the internal name ------------------------------------------------*/
    const auto pretty = [&](const std::string& internal)->std::string
    {
        for (const auto& kv : pretty2Int)
            if (kv.second == internal) return kv.first;
        return internal;
    };

    /* jet‑radius tags used in the histogram names
       declared **once** in your header – we only reference them here         */
    using JetRadiusArray = std::array<std::pair<const char*,const char*>,2>;
    extern const JetRadiusArray kJetRadii;   // { {"r03","_r03"},{"r05","_r05"} }

    /* “…Scaled” vs “…doNotScale” suffix handling --------------------------- */
    static const std::array<std::pair<std::string,std::string>,2> kScaleSuffix {{
        {"",                              "Scaled"},
        {"_NewTriggerFilling_doNotScale", "doNotScale"}
    }};

    // photon prefix (unchanged) ---------------------------------------------
    std::string photPrefix, xTitlePhot;
    if (histogramType == "maxEnergy") {
        photPrefix   = "h_maxEnergyClus_NewTriggerFilling_doNotScale_";
        xTitlePhot   = "Maximum Cluster Energy [GeV]";
    } else {
        photPrefix   = "h8by8TowerEnergySum_";
        xTitlePhot   = "Maximum 8×8 EMCal Tower‑Sum [GeV]";
    }
    std::string photPrefixFile = photPrefix;
    if (!photPrefixFile.empty() && photPrefixFile.back()=='_') photPrefixFile.pop_back();

    // =================================================================== file loop
    for (const std::string& rootFileName : combinedRootFiles)
    {
        /* open ----------------------------------------------------------------*/
        const std::string rootPath = outputDirectory + "/" + rootFileName;
        std::unique_ptr<TFile> inFile( TFile::Open(rootPath.c_str(),"READ") );
        if (!inFile || inFile->IsZombie()) {
            std::cerr<<"[WARN] cannot open "<<rootPath<<"\n"; continue;
        }

        /* --- figure out triggers in this file --------------------------------*/
        const std::vector<std::string> triggers =
            ExtractTriggersFromFilename(rootFileName, allTriggers);

        /* firmware tag --------------------------------------------------------*/
        std::string firmwareTag, firmwareStatus;
        if (Utils::EndsWith(rootFileName,"_beforeTriggerFirmwareUpdate_Combined.root")) {
            firmwareTag    = "_beforeTriggerFirmwareUpdate";
            firmwareStatus = "#bf{Before firmware update at run 47289}";
        } else if (Utils::EndsWith(rootFileName,"_afterTriggerFirmwareUpdate_Combined.root")) {
            firmwareTag    = "_afterTriggerFirmwareUpdate";
            firmwareStatus = "#bf{After firmware update at run 47289}";
        }

        /* canonical combination name (internal) ------------------------------ */
        std::string combinationName;
        for (const auto& t:triggers) combinationName += t + "_";
        if (!combinationName.empty()) combinationName.pop_back();
        combinationName += firmwareTag;

        /* plot directory ------------------------------------------------------ */
        const std::string plotDir =
            "/Users/patsfan753/Desktop/DirectPhotonAna/Plots_v2/" + combinationName;
        gSystem->mkdir(plotDir.c_str(),true);

        // ========================================================== 1. photons
        {
            TCanvas c("cPhot","Photon overlay",800,600); c.SetLogy();
            TLegend leg(0.55,0.60,0.88,0.88); leg.SetTextSize(0.03);
            bool first = true;

            for (const std::string& trg : triggers)
            {
                if (trg.rfind("Jet_",0)==0) continue;  // skip jet triggers

                if (TDirectory* dir = inFile->GetDirectory(trg.c_str()))
                {
                    if (auto* h = dynamic_cast<TH1*>( dir->Get((photPrefix+trg).c_str()) ))
                    {
                        auto* hc = static_cast<TH1*>( h->Clone() ); hc->SetDirectory(nullptr);
                        int col  = (trigColor.count(trg)?trigColor.at(trg):kBlack);

                        hc->SetLineColor(col); hc->SetLineWidth(2);
                        hc->GetXaxis()->SetTitle(xTitlePhot.c_str());
                        hc->GetYaxis()->SetTitle("Prescaled Counts");
                        (first?hc->Draw("hist"):hc->Draw("hist same"));
                        first=false;
                        leg.AddEntry(hc, pretty(trg).c_str(),"l");
                    }
                }
            }
            leg.Draw();
            c.SaveAs( (plotDir+"/"+photPrefixFile+"_Overlay.png").c_str() );
        }

        // ============================================================ 2. jets
        std::vector<std::string> jetTrigList;
        for (const auto& t : triggers)
            if (t.rfind("Jet_",0)==0) jetTrigList.push_back(t);

        if (!jetTrigList.empty())
        {
            // -------------------------------------------------- 2A. raw spectra
            for (const auto& sc : kScaleSuffix)
            {
                TCanvas cOv(("cOv_"+sc.second).c_str(),
                            ("Jet overlay ("+sc.second+")").c_str(),
                            1600,600);
                cOv.Divide(2,1);   // left = r03, right = r05

                for (int rIdx=0;rIdx<2;++rIdx)
                {
                    const std::string tag = kJetRadii[rIdx].second;   // "_r03" / "_r05"
                    TLegend leg(0.55,0.60,0.88,0.88); leg.SetTextSize(0.03);
                    cOv.cd(rIdx+1)->SetLogy();

                    bool   first = true;
                    double ymax  = 0.;

                    for (const std::string& jt : jetTrigList)
                    {
                        std::string hName = "h_leadingJetET"+tag+sc.first+"_"+jt;
                        if (TDirectory* d = inFile->GetDirectory(jt.c_str()))
                        {
                            if (auto* h = dynamic_cast<TH1*>( d->Get(hName.c_str()) ))
                            {
                                auto* hc = static_cast<TH1*>(h->Clone());
                                hc->SetDirectory(nullptr);

                                hc->GetXaxis()->SetTitle("Leading Jet E_{T} [GeV]");
                                hc->GetYaxis()->SetTitle("Prescaled Counts");
                                for (int b=1;b<=hc->GetNbinsX();++b)
                                    if (hc->GetBinCenter(b)<7.)
                                        hc->SetBinContent(b,0), hc->SetBinError(b,0);

                                int col = (trigColor.count(jt)?trigColor.at(jt):kBlack);
                                hc->SetLineColor(col); hc->SetLineWidth(2);

                                ymax = std::max(ymax, hc->GetMaximum());
                                (first?hc->Draw("hist"):hc->Draw("hist same"));
                                first=false;

                                leg.AddEntry(hc, pretty(jt).c_str(),"l");
                            }
                        }
                    }
                    if (auto* frame = dynamic_cast<TH1*>(gPad->GetPrimitive("hist")))
                        frame->SetMaximum(ymax*1.4);

                    leg.Draw();
                } // radius loop

                cOv.SaveAs( (plotDir+"/JetOverlay_"+sc.second+".png").c_str() );
            }

            // ----------------------------------------------- 2B. turn‑on curves
            if (TDirectory* mbdDir = inFile->GetDirectory("MBD_NandS_geq_1"))
            {
                for (const auto& sc : kScaleSuffix)
                {
                    TCanvas cTo(("cTo_"+sc.second).c_str(),
                                ("Jet turn‑on ("+sc.second+")").c_str(),
                                1600,600);
                    cTo.Divide(2,1);

                    for (int rIdx=0;rIdx<2;++rIdx)
                    {
                        const std::string tag = kJetRadii[rIdx].second;
                        TLegend leg(0.18,0.72,0.45,0.88); leg.SetTextSize(0.03);
                        cTo.cd(rIdx+1);

                        std::string mbName = "h_leadingJetET"+tag+sc.first+"_MBD_NandS_geq_1";
                        TH1* hMB = dynamic_cast<TH1*>( mbdDir->Get(mbName.c_str()) );
                        if (!hMB) continue;

                        bool first = true;
                        for (const std::string& jt : jetTrigList)
                        {
                            std::string hName = "h_leadingJetET"+tag+sc.first+"_"+jt;
                            if (TDirectory* d = inFile->GetDirectory(jt.c_str()))
                            {
                                if (auto* h = dynamic_cast<TH1*>( d->Get(hName.c_str()) ))
                                {
                                    auto* ratio = static_cast<TH1*>(h->Clone());
                                    ratio->SetDirectory(nullptr);
                                    ratio->Divide(h, hMB, 1, 1, "B");

                                    for (int b=1;b<=ratio->GetNbinsX();++b)
                                        if (ratio->GetBinCenter(b)<7.)
                                            ratio->SetBinContent(b,0), ratio->SetBinError(b,0);

                                    ratio->GetXaxis()->SetRangeUser(0.,50.);
                                    ratio->GetYaxis()->SetRangeUser(0.,1.4);
                                    ratio->GetXaxis()->SetTitle("Leading Jet E_{T} [GeV]");
                                    ratio->GetYaxis()->SetTitle("Efficiency");

                                    int col = (trigColor.count(jt)?trigColor.at(jt):kBlack);
                                    ratio->SetMarkerStyle(20);
                                    ratio->SetMarkerColor(col);
                                    ratio->SetLineColor(col);

                                    (first?ratio->Draw("E1"):ratio->Draw("E1 same"));
                                    first=false;

                                    leg.AddEntry(ratio, pretty(jt).c_str(),"p");
                                }
                            }
                        }
                        leg.Draw();

                        // reference line y=1
                        TLine ln(0,1,50,1); ln.SetLineStyle(2); ln.Draw();
                    } // radius loop

                    cTo.SaveAs( (plotDir+"/JetTurnOn_"+sc.second+".png").c_str() );
                }
            }
        } // if jet triggers present

        // ========================================================== 3. run‑by‑run
        if (const auto it = combinationToValidRuns.find(combinationName);
            it != combinationToValidRuns.end() )
        {
            PlotRunByRunHistograms(outputDirectory, plotDir, triggers,
                                   it->second, trigColor, pretty2Int,
                                   firmwareStatus);
        }
    } // combined‑file loop

    std::cout<<"[INFO] PlotCombinedHistograms finished.\n";
}


/*
 To process only a specific trigger combination, you can call AnalyzeTriggerGroupings with the desired combination name:
 */
void makeJetTriggerOverlays(std::string specificCombinationName = "") {
    std::string csvFilePath = "/Users/patsfan753/Desktop/JetTriggerPPG09/triggerAnalysisCombined.csv";
    std::string outputDirectory = "/Users/patsfan753/Desktop/JetTriggerPPG09/plotOutput";
    
    
    bool debugMode = false;
    std::map<int, std::map<std::string, std::string>> overrideTriggerStatus; // Empty map
    
//    overrideTriggerStatus[47333]["Jet_12_GeV_plus_MBD_NS_geq_1"] = "OFF";
    
    // Get the map of trigger combinations to run numbers
    std::map<std::set<std::string>, DataStructures::RunInfo> combinationToRuns = AnalyzeWhatTriggerGroupsAvailable(csvFilePath, debugMode, overrideTriggerStatus);

    // Now, loop through the map and print out the groupings and structure
    std::cout << "\nSummary of Trigger Combinations:\n";

    // Call the new function to sort and print the combinations
    PrintSortedCombinations(combinationToRuns);

    // Map to store valid runs for each combination
    std::map<std::string, std::vector<int>> combinationToValidRuns;

    // Check if all combined ROOT files and valid runs files already exist
    bool allOutputFilesExist = true;
    for (const auto& kv : combinationToRuns) {
        const std::set<std::string>& triggers = kv.first;
        const DataStructures::RunInfo& runInfo = kv.second;

        // Create a string to represent the combination for naming
        std::string baseCombinationName;
        for (const auto& trigger : triggers) {
            baseCombinationName += trigger + "_";
        }
        // Remove the trailing underscore
        if (!baseCombinationName.empty()) {
            baseCombinationName.pop_back();
        }

        // Check runs before firmware update
        if (!runInfo.runsBeforeFirmwareUpdate.empty()) {
            std::string combinationName = baseCombinationName;
            if (!runInfo.runsAfterFirmwareUpdate.empty()) {
                combinationName += "_beforeTriggerFirmwareUpdate";
            }
            std::string finalRootFilePath = outputDirectory + "/" + combinationName + "_Combined.root";
            std::string validRunsFilePath = outputDirectory + "/" + combinationName + "_ValidRuns.txt";

            bool rootFileExists = !gSystem->AccessPathName(finalRootFilePath.c_str());
            bool validRunsFileExists = !gSystem->AccessPathName(validRunsFilePath.c_str());

            if (!rootFileExists || !validRunsFileExists) {
                allOutputFilesExist = false;
                break;
            } else {
                // Read valid runs from the text file
                std::vector<int> validRuns;
                std::ifstream validRunsFile(validRunsFilePath);
                if (validRunsFile.is_open()) {
                    int runNumber;
                    while (validRunsFile >> runNumber) {
                        validRuns.push_back(runNumber);
                    }
                    validRunsFile.close();
                    combinationToValidRuns[combinationName] = validRuns;
                    std::cout << "Combination: " << combinationName << ", Number of valid runs: " << validRuns.size() << std::endl;
                } else {
                    std::cerr << "Failed to open valid runs file: " << validRunsFilePath << std::endl;
                    allOutputFilesExist = false;
                    break;
                }
            }
        }

        // Check runs after firmware update
        if (!runInfo.runsAfterFirmwareUpdate.empty()) {
            std::string combinationName = baseCombinationName;
            if (!runInfo.runsBeforeFirmwareUpdate.empty()) {
                combinationName += "_afterTriggerFirmwareUpdate";
            }
            std::string finalRootFilePath = outputDirectory + "/" + combinationName + "_Combined.root";
            std::string validRunsFilePath = outputDirectory + "/" + combinationName + "_ValidRuns.txt";

            bool rootFileExists = !gSystem->AccessPathName(finalRootFilePath.c_str());
            bool validRunsFileExists = !gSystem->AccessPathName(validRunsFilePath.c_str());

            if (!rootFileExists || !validRunsFileExists) {
                allOutputFilesExist = false;
                break;
            } else {
                // Read valid runs from the text file
                std::vector<int> validRuns;
                std::ifstream validRunsFile(validRunsFilePath);
                if (validRunsFile.is_open()) {
                    int runNumber;
                    while (validRunsFile >> runNumber) {
                        validRuns.push_back(runNumber);
                    }
                    validRunsFile.close();
                    combinationToValidRuns[combinationName] = validRuns;
                    std::cout << "Combination: " << combinationName << ", Number of valid runs: " << validRuns.size() << std::endl;
                } else {
                    std::cerr << "Failed to open valid runs file: " << validRunsFilePath << std::endl;
                    allOutputFilesExist = false;
                    break;
                }
            }
        }
    }

    if (!allOutputFilesExist) {
        // Some output files are missing, proceed to process and merge root files
        ProcessAndMergeRootFiles(combinationToRuns, outputDirectory, combinationToValidRuns);
    } else {
        std::cout << "All output ROOT files and valid runs files already exist. Skipping ProcessAndMergeRootFiles." << std::endl;
    }

    // Get list of combined ROOT files
    std::vector<std::string> combinedRootFiles;
    void* dirp = gSystem->OpenDirectory(outputDirectory.c_str());
    const char* entry;
    while ((entry = gSystem->GetDirEntry(dirp))) {
        std::string fileName = entry;
        if (Utils::EndsWith(fileName, "_Combined.root")) {
            // If specificCombinationName is set, only add matching files
            if (specificCombinationName.empty() || fileName.find(specificCombinationName + "_Combined.root") != std::string::npos) {
                combinedRootFiles.push_back(fileName);
            }
        }
    }
    gSystem->FreeDirectory(dirp);

    // Now plot the combined histograms
    PlotCombinedHistograms(outputDirectory, combinedRootFiles, combinationToValidRuns, "sigmoid", false);
}
