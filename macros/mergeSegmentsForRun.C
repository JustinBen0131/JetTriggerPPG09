#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <cstdlib>
#include <cstdio>
#include <dirent.h>
#include <sys/stat.h>
#include <algorithm>

// ROOT
#include <TSystem.h>
#include <TFile.h>
#include <TKey.h>
#include <TDirectory.h>
#include <TH1.h>
#include <TFileMerger.h>
#include <TSQLServer.h>
#include <TSQLResult.h>
#include <TSQLRow.h>

// ANSI Color Macros
#define ANSI_RESET  "\x1b[0m"
#define ANSI_RED    "\x1b[31m"
#define ANSI_GREEN  "\x1b[32m"
#define ANSI_YELLOW "\x1b[33m"
#define ANSI_CYAN   "\x1b[36m"

// ------------------------------------------------------------------------
// 1) Validate a ROOT file by reading all objects
// ------------------------------------------------------------------------
bool validate_root_file(const std::string &filename)
{
    TFile *f = TFile::Open(filename.c_str(), "READ");
    if (!f || f->IsZombie())
    {
        std::cerr << ANSI_RED << "[ERROR] Cannot open file for validation: "
                  << filename << ANSI_RESET << std::endl;
        if (f) { f->Close(); delete f; }
        return false;
    }

    bool isValid = true;
    TIter nextkey(f->GetListOfKeys());
    TKey* key;
    while ((key = (TKey*)nextkey()))
    {
        TObject* obj = nullptr;
        try {
            obj = key->ReadObj();
            if (!obj)
            {
                std::cerr << ANSI_RED << "[ERROR] Failed to read object: "
                          << key->GetName() << ANSI_RESET << std::endl;
                isValid = false;
                continue;
            }
            // example check: negative integrals for TH1
            if (obj->InheritsFrom("TH1"))
            {
                TH1 *h = (TH1*)obj;
                if (h->Integral() < 0)
                {
                    std::cerr << ANSI_RED << "[ERROR] Negative integral in hist: "
                              << h->GetName() << ANSI_RESET << std::endl;
                    isValid = false;
                }
            }
            delete obj;
        }
        catch(const std::exception &e) {
            std::cerr << ANSI_RED << "[EXCEPTION] While reading " << key->GetName()
                      << ": " << e.what() << ANSI_RESET << std::endl;
            isValid = false;
            if (obj) delete obj;
        }
    }

    f->Close();
    delete f;

    if (isValid) {
        std::cout << ANSI_GREEN << "[INFO] File validation successful: "
                  << filename << ANSI_RESET << std::endl;
    } else {
        std::cerr << ANSI_RED << "[ERROR] File validation failed: "
                  << filename << ANSI_RESET << std::endl;
    }

    return isValid;
}

// ------------------------------------------------------------------------
// 2) Hard-coded map from DB trigger names => folder/histogram directory
// ------------------------------------------------------------------------
static std::map<std::string, std::string> g_dbNameToFolderName = {
    {"MBD N&S >= 1",               "MBD_NandS_geq_1"},
    {"Jet 8 GeV + MBD NS >= 1",    "Jet_8_GeV_plus_MBD_NS_geq_1"},
    {"Jet 10 GeV + MBD NS >= 1",   "Jet_10_GeV_plus_MBD_NS_geq_1"},
    {"Jet 12 GeV + MBD NS >= 1",   "Jet_12_GeV_plus_MBD_NS_geq_1"}
};

// ------------------------------------------------------------------------
// 3) Query DB for scale factors => (folderName -> scaleValue)
// ------------------------------------------------------------------------
void getTriggerScaleFactorsFromDB(int runNumber, std::map<std::string, double> &folderScaleMap)
{
    folderScaleMap.clear();
    TSQLServer* db = TSQLServer::Connect("pgsql://sphnxdaqdbreplica:5432/daq","phnxro","");
    if (!db || db->IsZombie())
    {
        std::cerr << ANSI_RED << "[ERROR] DB connection failed for run "
                  << runNumber << ANSI_RESET << std::endl;
        if (db) delete db;
        return;
    }
    std::cout << "[DB INFO] " << db->ServerInfo() << std::endl;

    char query[512];
    snprintf(query, sizeof(query),
             "SELECT s.index, t.triggername, s.live, s.scaled "
             "FROM gl1_scalers s "
             "JOIN gl1_triggernames t ON (s.index = t.index AND s.runnumber BETWEEN t.runnumber AND t.runnumber_last) "
             "WHERE s.runnumber=%d ORDER BY s.index;", runNumber);

    auto *res = db->Query(query);
    if (!res)
    {
        std::cerr << ANSI_RED << "[ERROR] Query failed for run "
                  << runNumber << ANSI_RESET << std::endl;
        delete db;
        return;
    }

    while (auto row = res->Next())
    {
        const char* dbTrig = row->GetField(1);
        const char* liveStr = row->GetField(2);
        const char* scaledStr = row->GetField(3);
        if (!dbTrig || !liveStr || !scaledStr) { delete row; continue; }

        std::string trigName(dbTrig);
        double live   = std::atof(liveStr);
        double scaled = std::atof(scaledStr);

        // check map
        auto it = g_dbNameToFolderName.find(trigName);
        if (it == g_dbNameToFolderName.end())
        {
            // skip unknown triggers
            delete row;
            continue;
        }

        std::string folderName = it->second;
        double scaleVal = (scaled>0) ? (live/scaled) : -1;
        folderScaleMap[folderName] = scaleVal;

        delete row;
    }

    delete res; delete db;

    // debug
    std::cout << "[DEBUG] scale factors for run " << runNumber << ":\n";
    for (auto &p : folderScaleMap)
    {
        std::cout << "   folder='" << p.first
                  << "' scale=" << p.second << "\n";
    }
}

// ------------------------------------------------------------------------
// 4) Scale a single histogram in memory
// ------------------------------------------------------------------------
int scaleHistogram(TH1 *hist, double factor, const std::string &folder)
{
    if (!hist)
    {
        std::cerr << ANSI_RED << "[ERROR] Null histogram in " << folder << ANSI_RESET << std::endl;
        return -1;
    }

    double integral = hist->Integral();
    if (integral==0)
    {
        std::cout << ANSI_CYAN << "[SKIP] " << hist->GetName()
                  << " in " << folder << ": zero content." << ANSI_RESET << std::endl;
        return 1;
    }
    if (factor<=0)
    {
        std::cout << ANSI_CYAN << "[SKIP] " << hist->GetName()
                  << " in " << folder << ": factor<=0 => trigger off." << ANSI_RESET << std::endl;
        return 2;
    }

    hist->Scale(factor);
    std::cout << ANSI_YELLOW << "[SCALE] " << hist->GetName()
              << " in " << folder
              << " by " << factor << ANSI_RESET << std::endl;
    return 0;
}

void copyAndScaleDirectory(TDirectory *sourceDir,
                           TDirectory *destDir,
                           const std::map<std::string, double> &folderScaleMap)
{
    sourceDir->cd();
    TList *keys = sourceDir->GetListOfKeys();
    if (!keys) return;

    destDir->cd();

    TIter nextkey(keys);
    TKey *key;
    while ((key = (TKey*)nextkey()))
    {
        // read object from source
        TObject* obj = key->ReadObj();
        if (!obj) continue;

        if (obj->InheritsFrom("TDirectory"))
        {
            // Recursively handle subdirectories
            TDirectory* subSrc = (TDirectory*)obj;
            std::string dname = subSrc->GetName();

            TDirectory* subDest = destDir->mkdir(dname.c_str());
            copyAndScaleDirectory(subSrc, subDest, folderScaleMap);
        }
        else if (obj->InheritsFrom("TH1"))
        {
            TH1* h = static_cast<TH1*>(obj);
            // The name of the directory we’re copying from
            std::string parentName(sourceDir->GetName());
            // The name of the histogram
            std::string hName(h->GetName());

            // We skip scaling in these two cases:
            //  1) This is the 'COMBINED' directory.
            //  2) The histogram's name contains "doNotScale".
            bool skipScaling = (parentName == "COMBINED")
                               || (hName.find("doNotScale") != std::string::npos);

            if (!skipScaling)
            {
                // If not skipping => see if we have a scale factor
                auto it = folderScaleMap.find(parentName);
                if (it != folderScaleMap.end())
                {
                    double fac = it->second;
                    // scale the histogram
                    scaleHistogram(h, fac, parentName);
                }
            }
            else
            {
                // debug message if you want
                std::cout << "[INFO] Skipping scale for histogram '"
                          << hName << "' in folder '"
                          << parentName << "'\n";
            }

            // Now write the histogram to destination
            destDir->cd();
            h->Write(h->GetName(), TObject::kOverwrite);
        }
        else
        {
            // Non-TH1 objects => directly write
            destDir->cd();
            obj->Write(obj->GetName(), TObject::kOverwrite);
        }
        delete obj;
    }
}


// ------------------------------------------------------------------------
// 6) Merge all input files => "merged_tmp.root" with TFileMerger ("RECREATE")
// ------------------------------------------------------------------------
bool doSegmentMerge(const std::vector<std::string> &validRootFiles,
                    const std::string &outMergedFile)
{
    std::cout << "[INFO] Merging segments into " << outMergedFile << std::endl;

    // remove if exists
    gSystem->Unlink(outMergedFile.c_str());

    TFileMerger merger;
    // "RECREATE" => brand-new file, ensures no leftover partial keys
    merger.OutputFile(outMergedFile.c_str(), "RECREATE");

    for (auto &f : validRootFiles)
    {
        bool ok = merger.AddFile(f.c_str());
        if (!ok)
        {
            std::cerr << ANSI_RED << "[ERROR] TFileMerger failed to add "
                      << f << ANSI_RESET << std::endl;
            return false;
        }
    }

    bool mergeRes = merger.Merge();
    if (!mergeRes)
    {
        std::cerr << ANSI_RED << "[ERROR] TFileMerger.Merge() failed for "
                  << outMergedFile << ANSI_RESET << std::endl;
        return false;
    }

    // validate the merged result
    bool good = validate_root_file(outMergedFile);
    if (!good)
    {
        std::cerr << ANSI_RED << "[ERROR] Merged file " << outMergedFile
                  << " is invalid." << ANSI_RESET << std::endl;
        return false;
    }

    return true;
}

// ------------------------------------------------------------------------
// 7) Scale the merged file => produce "scaled_tmp.root", brand new
//    then rename to final if valid
// ------------------------------------------------------------------------
bool scaleMergedFile(const std::string &mergedFile,
                     const std::string &scaledFile,
                     int runNumber)
{
    // remove if exists
    gSystem->Unlink(scaledFile.c_str());

    // read scale factors
    std::map<std::string,double> folderScaleMap;
    getTriggerScaleFactorsFromDB(runNumber, folderScaleMap);

    // open input read-only
    TFile *fin = TFile::Open(mergedFile.c_str(), "READ");
    if (!fin || fin->IsZombie())
    {
        std::cerr << ANSI_RED << "[ERROR] cannot open merged file "
                  << mergedFile << ANSI_RESET << std::endl;
        if (fin) { fin->Close(); delete fin; }
        return false;
    }

    // create brand-new output
    TFile *fout = TFile::Open(scaledFile.c_str(), "RECREATE");
    if (!fout || fout->IsZombie())
    {
        std::cerr << ANSI_RED << "[ERROR] cannot create scaled output "
                  << scaledFile << ANSI_RESET << std::endl;
        if (fout) { fout->Close(); delete fout; }
        fin->Close(); delete fin;
        return false;
    }

    // top-level copy & scale
    copyAndScaleDirectory(fin, fout, folderScaleMap);

    // close both
    fout->Write();
    fout->Close();
    delete fout;

    fin->Close();
    delete fin;

    // validate scaled
    bool good = validate_root_file(scaledFile);
    if (!good)
    {
        std::cerr << ANSI_RED << "[ERROR] scaled file " << scaledFile
                  << " invalid." << ANSI_RESET << std::endl;
        return false;
    }
    return true;
}

// ------------------------------------------------------------------------
// 8) The main function for a single run
// ------------------------------------------------------------------------
void mergeSegmentFilesForRuns(int runNumber)
{
    std::cout << "\n================ mergeSegmentFilesForRuns => run "
              << runNumber << " ================\n";

    // Example input and output directories
    std::string baseDir  = "/sphenix/tg/tg01/bulk/jbennett/DirectPhotons/output/CALOout/";
    std::string outputDir= "/sphenix/user/patsfan753/tutorials/tutorials/CaloDataAnaRun24pp/output/";

    std::string runStr   = std::to_string(runNumber);
    std::string runPath  = baseDir + runStr + "/";
    std::string finalFile= outputDir + runStr + "_HistOutput.root";

    // If finalFile already exists, skip
    struct stat sb;
    if (::stat(finalFile.c_str(), &sb)==0)
    {
        std::cout << "[INFO] Final file already exists " << finalFile
                  << ", skipping.\n";
        return;
    }

    // gather segment files
    DIR *dir = opendir(runPath.c_str());
    if (!dir)
    {
        std::cerr << "[ERROR] cannot open " << runPath << std::endl;
        return;
    }
    std::vector<std::string> allSegments;
    struct dirent *ent;
    while ((ent=readdir(dir)))
    {
        std::string fn = ent->d_name;
        if (fn.find(".root") != std::string::npos)
            allSegments.push_back(runPath + fn);
    }
    closedir(dir);

    if (allSegments.empty())
    {
        std::cerr << "[WARNING] no .root segments found in "
                  << runPath << std::endl;
        return;
    }

    // check for zombie
    std::vector<std::string> validSegments;
    std::ofstream problemList("problematicHaddRuns.txt", std::ios::app);

    for (auto & seg : allSegments)
    {
        TFile *tf = TFile::Open(seg.c_str(), "READ");
        if (!tf || tf->IsZombie())
        {
            std::cerr << "[ERROR] problem file: " << seg << std::endl;
            if (problemList.is_open())
                problemList << runNumber << " " << seg << "\n";
            if (tf) { tf->Close(); delete tf; }
            continue;
        }
        validSegments.push_back(seg);
        tf->Close(); delete tf;
    }

    if (validSegments.empty())
    {
        std::cerr << "[WARNING] no valid segments for run "
                  << runNumber << std::endl;
        return;
    }

    // step1: merge => "merged_tmp.root"
    std::string mergedTmp = outputDir + runStr + "_merged_tmp.root";
    bool mergedOK = doSegmentMerge(validSegments, mergedTmp);
    if (!mergedOK)
    {
        std::cerr << ANSI_RED << "[ERROR] merging segments failed for run "
                  << runNumber << ANSI_RESET << std::endl;
        return;
    }

    // step2: scale => "scaled_tmp.root"
    std::string scaledTmp = outputDir + runStr + "_scaled_tmp.root";
    bool scaledOK = scaleMergedFile(mergedTmp, scaledTmp, runNumber);
    if (!scaledOK)
    {
        std::cerr << ANSI_RED << "[ERROR] scaling failed for run "
                  << runNumber << ANSI_RESET << std::endl;
        ::remove(mergedTmp.c_str());
        return;
    }

    // If that is good, rename scaled => final
    int renameRet = std::rename(scaledTmp.c_str(), finalFile.c_str());
    if (renameRet!=0)
    {
        perror("[ERROR] rename failed");
        std::cerr << ANSI_RED << "Please rename " << scaledTmp << " => "
                  << finalFile << " manually." << ANSI_RESET << std::endl;
        return;
    }

    // Optionally remove mergedTmp if you want
    ::remove(mergedTmp.c_str());

    std::cout << ANSI_GREEN << "[SUCCESS] final scaled file => "
              << finalFile << ANSI_RESET << std::endl;
}
