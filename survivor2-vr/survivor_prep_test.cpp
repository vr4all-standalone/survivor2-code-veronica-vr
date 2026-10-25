#include "rend/vr/survivor_prep.h"
#include "rend/vr/hands_build.h"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <cassert>
#include <iostream>
#include <stdexcept>
using namespace vr::hands;
int main() {
 std::array<bool,8> cached{};
 SurvivorPreparation p;
 p.begin(true,cached,true);
 assert(p.preparing());
 int work=0;
 auto build=[&](size_t index,std::string&){assert(index==(size_t)work);++work;return true;};
 for(int i=0;i<3;++i)p.tick(false,build);
 assert(work==0); // progress panel must have reached a presentation boundary
 p.presented();p.tick(false,build);
 assert(work==0); // announce first asset before doing costly work
 for(int i=0;i<8;++i) {
  p.presented();const bool ready=p.tick(false,build);
  assert(work==i+1);assert(ready==(i==7));
  assert(p.progress()==float(i+1)/8);
  if(i!=7){p.tick(false,build);assert(work==i+1);} // no second asset in same frame
 }
 assert(p.phase==SurvivorPreparation::Phase::Ready);
 p.reset();cached.fill(true);p.begin(true,cached,false);
 assert(p.phase==SurvivorPreparation::Phase::Ready);
 p.tick(false,[&](size_t,std::string&){++work;return true;});assert(work==8);
 p.reset();cached[3]=false;p.begin(true,cached,true);p.tick(true,build);
 assert(p.phase==SurvivorPreparation::Phase::Skipped);
 p.begin(true,cached,true);p.presented();p.tick(false,build);assert(work==8);
 p.reset();p.begin(true,cached,false);p.tick(false,build);
 p.tick(false,[](size_t index,std::string& error){assert(index==3);error="disk full";return false;});
 assert(p.phase==SurvivorPreparation::Phase::Failed&&p.error=="disk full");
 p.begin(true,cached,false);p.tick(false,build);assert(work==8); // resume/load never retries
 p.reset();p.begin(true,cached,false);p.tick(false,build);
 assert(p.tick(false,[](size_t index,std::string&){return index==3;}));
 p.reset();p.begin(true,cached,false);p.tick(false,build);
 p.tick(false,[](size_t,std::string&)->bool{throw std::runtime_error("read failed");});
 assert(p.phase==SurvivorPreparation::Phase::Failed&&p.error=="read failed");
 p.reset();p.begin(false,cached,false);assert(!p.preparing());
 // Exercise the actual same-directory writer used by direct cartridge preparation.
 const auto dir=std::filesystem::temp_directory_path()/"survivor-prep-atomic-test";
 std::filesystem::create_directories(dir);
 const auto final=dir/"cache.bin", part=dir/"cache.bin.part";
 assert(saveModelCacheAtomic({1,2,3},final.u8string()));
 assert(saveModelCacheAtomic({4,5},final.u8string()));
 {std::ifstream in(final,std::ios::binary);std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),{});assert((bytes==std::vector<uint8_t>{4,5}));}
 assert(!std::filesystem::exists(part));
 std::filesystem::create_directory(part); // no writable staging file: preserve previous final
 assert(!saveModelCacheAtomic({7},final.u8string()));
 {std::ifstream in(final,std::ios::binary);assert(in.get()==4&&in.get()==5&&in.get()==EOF);}
 std::filesystem::remove(part);
 std::filesystem::remove(final);
 std::filesystem::create_directory(final);
 {std::ofstream marker(final/"keep");marker<<"previous final";}
 assert(!saveModelCacheAtomic({9},final.u8string())); // failed replace cleans staging only
 assert(std::filesystem::exists(final/"keep")&&!std::filesystem::exists(part));
 std::filesystem::remove(final/"keep");std::filesystem::remove(final);std::filesystem::remove(dir);
 constexpr int fallback=4;
 assert(survivorHandChoice(0,true,true,true,true,8,true,fallback)==1032);
 for(int player:{-1,1,2})assert(survivorHandChoice(player,true,true,true,true,8,true,fallback)==fallback);
 for(int id:{-1,0,256,1024})assert(survivorHandChoice(0,true,true,true,true,id,true,fallback)==fallback);
 assert(survivorHandChoice(0,false,true,true,true,8,true,9)==9); // custom GLB precedence
 assert(survivorHandChoice(0,true,false,true,true,8,true,fallback)==fallback);
 assert(survivorHandChoice(0,true,true,false,true,8,true,fallback)==fallback);
 assert(survivorHandChoice(0,true,true,true,false,8,true,fallback)==fallback);
 assert(survivorHandChoice(0,true,true,true,true,9,false,fallback)==fallback);
 assert(!survivorKind(1023)&&survivorKind(1024)&&survivorKind(1279)&&!survivorKind(1280));
 std::cout<<"Survivor preparation and selection tests passed\n";
}
