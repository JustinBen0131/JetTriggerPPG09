//////////////////////////////////////////////////////////////////////////////
// Run‑by‑run segment merger + scaler for JetTriggerPlotter output
// --------------------------------------------------------------------------
//   • Input   : /sphenix/tg/tg01/bulk/jbennett/TriggerAna/<run>/*.root
//               (or flat files TriggerAna_<run>_segXXX.root in the same dir)
//   • Per run : merge  →  scale  →  TriggerAna_<run>.root
//   • Final   : hadd all per‑run files → TriggerAnaFinal.root
//
// Histogram‑scaling rules
//   scale TH1 whose names start with  h_leadingJetET
//     ─ but skip if directory == "COMBINED"      or  name contains "doNotScale"
//
//////////////////////////////////////////////////////////////////////////////

#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <dirent.h>
#include <sys/stat.h>
#include <cstdio>
#include <memory>

// ── ROOT -------------------------------------------------------------------
#include <TSystem.h>
#include <TFile.h>
#include <TDirectory.h>
#include <TH1.h>
#include <TKey.h>
#include <TFileMerger.h>
#include <TSQLServer.h>
#include <TSQLResult.h>
#include <TSQLRow.h>

// ── Colours ---------------------------------------------------------------
#define C_RST  "\033[0m"
#define C_RED  "\033[31m"
#define C_GRN  "\033[32m"
#define C_YEL  "\033[33m"
#define C_CYN  "\033[36m"
#define C_BLU  "\033[34m"

// ── Global verbosity switch -----------------------------------------------
static const bool VERBOSE = true;
#define vout if(VERBOSE) std::cout
#define verr if(VERBOSE) std::cerr

// ── Helpers ----------------------------------------------------------------
static bool isDirectory(const std::string& p)
{
  struct stat st{};
  return ::stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// ── 1) FAST sanity check of a ROOT file ------------------------------------
static bool validateRoot(const std::string& fn)
{
  std::unique_ptr<TFile> f(TFile::Open(fn.c_str(), "READ"));
  if(!f || f->IsZombie()){
    std::cerr<<C_RED<<"[ERROR] cannot open "<<fn<<C_RST<<"\n"; return false;
  }
  bool ok=true;
  TIter next(f->GetListOfKeys());
  while(auto* k = static_cast<TKey*>(next()))
  {
    std::unique_ptr<TObject> o(k->ReadObj());
    if(!o){ ok=false; continue; }
    if(o->InheritsFrom("TH1") && static_cast<TH1*>(o.get())->Integral()<0) ok=false;
  }
  if(!ok) std::cerr<<C_RED<<"[ERROR] validation failed "<<fn<<C_RST<<"\n";
  return ok;
}

// ── 2) DB‑name → JetTriggerPlotter directory -------------------------------
static const std::map<std::string,std::string> g_trigMap = {
  {"MBD N&S >= 1",               "MBD_NandS_geq_1"},
  {"Jet 8 GeV + MBD NS >= 1",    "Jet_8_GeV_plus_MBD_NS_geq_1"},
  {"Jet 10 GeV + MBD NS >= 1",   "Jet_10_GeV_plus_MBD_NS_geq_1"},
  {"Jet 12 GeV + MBD NS >= 1",   "Jet_12_GeV_plus_MBD_NS_geq_1"}
};

// ── 3) Fetch live / scaled → scale factor ----------------------------------
static bool fetchScaleMap(int run, std::map<std::string,double>& fac)
{
  fac.clear();
  std::unique_ptr<TSQLServer> db(
      TSQLServer::Connect("pgsql://sphnxdaqdbreplica:5432/daq","phnxro",""));
  if(!db || db->IsZombie()){
    std::cerr<<C_RED<<"[ERROR] DB connect failed"<<C_RST<<"\n"; return false;
  }

  char q[512];
  snprintf(q,sizeof(q),
           "SELECT t.triggername, s.live, s.scaled "
           "FROM gl1_scalers s "
           "JOIN gl1_triggernames t ON "
           "(s.index=t.index AND s.runnumber BETWEEN t.runnumber AND t.runnumber_last) "
           "WHERE s.runnumber=%d;", run);

  std::unique_ptr<TSQLResult> res(db->Query(q));
  if(!res){ std::cerr<<C_RED<<"[ERROR] query failed"<<C_RST<<"\n"; return false; }

  while(auto* row=res->Next())
  {
    std::string dbName = row->GetField(0);
    double live   = std::atof(row->GetField(1));
    double scaled = std::atof(row->GetField(2));
    delete row;

    auto it = g_trigMap.find(dbName);
    if(it==g_trigMap.end()) continue;
    fac[it->second] = (scaled>0) ? live/scaled : -1.;
  }

  vout<<C_CYN<<"┌─ scale factors for run "<<run<<" ─────────────┐"<<C_RST<<"\n";
  for(auto& kv:fac)
    vout<<"│  "<<std::setw(35)<<std::left<<kv.first<<"  : "
        <<std::setw(8)<<std::right<<kv.second<<" │\n";
  vout<<C_CYN<<"└──────────────────────────────────────────────┘"<<C_RST<<"\n";
  return true;
}

// ── 4) scale candidate histograms ------------------------------------------
static void applyScale(TH1* h,const std::string& dir,
                       const std::map<std::string,double>& fac,
                       unsigned depth)
{
  if(!h) return;
  const std::string n = h->GetName();
  const std::string indent(depth*2,' ');
  std::string flag  = " ";

  if(dir=="COMBINED" || n.find("doNotScale")!=std::string::npos ||
     n.rfind("h_leadingJetET",0)!=0)
  {
    vout<<indent<<"└─ "<<n<<"  ("<<h->GetEntries()<<" entries – kept)\n";
    return;
  }

  auto it = fac.find(dir);
  if(it==fac.end() || it->second<=0){
    vout<<indent<<"└─ "<<n<<"  ("<<h->GetEntries()<<" entries – "<<C_YEL<<"NO SCALE"<<C_RST<<")\n";
    return;
  }

  h->Scale(it->second);
  vout<<indent<<"└─ "<<n<<"  ("<<h->GetEntries()
      <<" entries, "<<C_YEL<<"×"<<it->second<<C_RST<<")\n";
}

// ── 5) deep copy with per‑object logging -----------------------------------
static void copyDir(TDirectory* src,TDirectory* dst,
                    const std::map<std::string,double>& fac,
                    unsigned depth=0,
                    std::map<std::string,int>* dirWritten=nullptr)
{
  src->cd();
  TIter next(src->GetListOfKeys());
  while(auto* k=static_cast<TKey*>(next()))
  {
    std::unique_ptr<TObject> obj(k->ReadObj());
    if(!obj) continue;

    dst->cd();
    const std::string indent(depth*2,' ');
    if(obj->InheritsFrom("TDirectory"))
    {
      auto* sdir=static_cast<TDirectory*>(obj.get());
      auto* ddir=dst->mkdir(sdir->GetName());
      vout<<indent<<C_BLU<<sdir->GetName()<<"/"<<C_RST<<"\n";
      copyDir(sdir,ddir,fac,depth+1,dirWritten);
    }
    else if(obj->InheritsFrom("TH1"))
    {
      applyScale(static_cast<TH1*>(obj.get()),src->GetName(),fac,depth);
      obj->Write(obj->GetName(),TObject::kOverwrite);
      if(dirWritten) (*dirWritten)[src->GetName()]++;
    }
    else
    {
      vout<<indent<<"└─ "<<obj->GetName()<<" (non‑TH1)\n";
      obj->Write(obj->GetName(),TObject::kOverwrite);
      if(dirWritten) (*dirWritten)[src->GetName()]++;
    }
  }
}

// ── 6) process one run ------------------------------------------------------
static bool handleRun(int run,
                      const std::string& inDir,
                      const std::string& outDir)
{
  const std::string rStr = std::to_string(run);

  // ─ collect segments
  std::vector<std::string> segs;
  std::string sub = inDir + "/" + rStr;
  if(isDirectory(sub)){
    DIR* d=opendir(sub.c_str());
    while(auto* e=readdir(d)){
      std::string n=e->d_name;
      if(n.size()>5 && n.substr(n.size()-5)==".root") segs.emplace_back(sub+"/"+n);
    }
    closedir(d);
  }else{
    DIR* d=opendir(inDir.c_str());
    const std::string pat = "TriggerAna_" + rStr + "_";
    while(auto* e=readdir(d)){
      std::string n=e->d_name;
      if(n.find(pat)==0 && n.substr(n.size()-5)==".root") segs.emplace_back(inDir+"/"+n);
    }
    closedir(d);
  }
  if(segs.empty()){
    std::cerr<<C_RED<<"[ERROR] run "<<run<<" : no segments"<<C_RST<<"\n";
    return false;
  }
  std::sort(segs.begin(), segs.end());

  vout<<C_CYN<<"┌─ merging "<<segs.size()<<" segments"<<C_RST<<"\n";

  // ─ names
  const std::string merged = outDir + "/tmp_" + rStr + "_merge.root";
  const std::string scaled = outDir + "/tmp_" + rStr + "_scale.root";
  const std::string final  = outDir + "/TriggerAna_" + rStr + ".root";

  // ─ merge
  TFileMerger fm; fm.OutputFile(merged.c_str(),"RECREATE");
  for(auto& f:segs) fm.AddFile(f.c_str());
  if(!fm.Merge() || !validateRoot(merged)){ gSystem->Unlink(merged.c_str()); return false; }

  // ─ scale
  std::map<std::string,double> fac;
  if(!fetchScaleMap(run,fac)){ gSystem->Unlink(merged.c_str()); return false; }

  std::unique_ptr<TFile> fin(TFile::Open(merged.c_str(),"READ"));
  std::unique_ptr<TFile> fout(TFile::Open(scaled.c_str(),"RECREATE"));
  if(!fin || fin->IsZombie() || !fout || fout->IsZombie()){
    std::cerr<<C_RED<<"[ERROR] cannot open tmp files"<<C_RST<<"\n";
    return false;
  }

  std::map<std::string,int> writtenPerDir;
  copyDir(fin.get(),fout.get(),fac,0,&writtenPerDir);
  fout->Write(); fout->Close(); fin->Close();

  // ─ summary table
  vout<<C_CYN<<"┌─ objects written"<<C_RST<<"\n";
  for(auto& kv:writtenPerDir)
    vout<<"│  "<<std::setw(35)<<std::left<<kv.first<<" : "
        <<std::setw(6)<<kv.second<<"\n";
  vout<<C_CYN<<"└──────────────────"<<C_RST<<"\n";

  if(!validateRoot(scaled)){
    gSystem->Unlink(merged.c_str()); gSystem->Unlink(scaled.c_str()); return false;
  }

  // move into place
  if(std::rename(scaled.c_str(), final.c_str())!=0){
    perror("rename"); return false;
  }
  gSystem->Unlink(merged.c_str());
  std::cout<<C_GRN<<"[OK] "<<final<<C_RST<<"\n";
  return true;
}

// ── 7) Driver ---------------------------------------------------------------
static int driver()
{
  const std::string inDir  = "/sphenix/tg/tg01/bulk/jbennett/TriggerAna";
  const std::string outDir = "/sphenix/u/patsfan753/scratch/TriggerAnalysis/output";
  gSystem->mkdir(outDir.c_str(), true);

  // discover runs
  std::set<int> runs;
  DIR* d=opendir(inDir.c_str());
  if(!d){ std::cerr<<C_RED<<"[FATAL] cannot open "<<inDir<<C_RST<<"\n"; return 2; }
  while(auto* e=readdir(d)){
    std::string n=e->d_name;
    if(std::all_of(n.begin(), n.end(), ::isdigit)) runs.insert(std::stoi(n));
    else if(n.find("TriggerAna_")==0){
      size_t p=n.find('_',12); if(p!=std::string::npos)
        runs.insert(std::atoi(n.substr(12, p-12).c_str()));
    }
  }
  closedir(d);
  if(runs.empty()){ std::cerr<<C_RED<<"[FATAL] no runs"<<C_RST<<"\n"; return 3; }

  bool allOK=true;
  for(int r: runs){
    std::cout<<"\n"<<C_BLU<<"================ RUN "<<r<<" ==============="<<C_RST<<"\n";
    if(!handleRun(r, inDir, outDir)) allOK=false;
  }

  // final file
  if(allOK){
    const std::string final=outDir+"/TriggerAnaFinal.root";
    gSystem->Unlink(final.c_str());

    TFileMerger fm; fm.OutputFile(final.c_str(),"RECREATE");
    DIR* od=opendir(outDir.c_str());
    while(auto* e=readdir(od)){
      std::string n=e->d_name;
      if(n.find("TriggerAna_")==0 && n.substr(n.size()-5)==".root"
         && n!="TriggerAnaFinal.root")
        fm.AddFile((outDir+"/"+n).c_str());
    }
    closedir(od);
    if(!fm.Merge() || !validateRoot(final)){
      std::cerr<<C_RED<<"[ERROR] final merge failed"<<C_RST<<"\n"; return 4;
    }
    std::cout<<C_GRN<<"[SUCCESS] "<<final<<C_RST<<"\n";
  }
  return allOK?0:1;
}

// ── Entry points ------------------------------------------------------------
#ifdef __CLING__              // ROOT macro
int mergeSegmentsForRun(){ return driver(); }
#else                          // standalone
int main(){ return driver(); }
#endif
