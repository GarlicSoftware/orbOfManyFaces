#include "Arduino.h"
#include "SD.h"
#include "favorites.h"
#include "face_groups.h"
#include "group_format.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <vector>
static bool validateFailure=false;
bool groupsStorageReady(){return fakeMounted;}
void groupsMaintenanceReset(){}
bool groupUploading(){return false;}
int groupCurrent(){return 0;}
const char *groupId(int){return "";}
const Face &groupFace(int i){return faces[i];}
const char *groupError(){return "Injected validation failure";}
static std::vector<uint8_t> read(const std::filesystem::path &p){std::ifstream f(p,std::ios::binary);return {(std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>()};}
bool groupsValidateFile(const char *path){if(validateFailure)return false;auto data=read(SD.resolve(path));auto g=clockpack::parse(data.data(),data.size());return !g->faces.empty();}
void progress(const char*){}
void toggle(int i){favoritesFace(i);assert(favoritesToggle());}
int main(int argc,char **argv){
 assert(argc==2);fakeRoot=argv[1];std::filesystem::create_directories(fakeRoot);
 favoritesBegin();assert(favoritesCount()==0&&!favoritesDirty());
 toggle(1);toggle(2);assert(favoritesCount()==2&&favoritesDirty()&&favoritesPrompt());
 favoritesBegin();favoritesFace(1);assert(favoritesCount()==2&&favoritesCurrent());assert(favoritesLater());favoritesBegin();assert(!favoritesPrompt());
 toggle(3);assert(favoritesPrompt());assert(favoritesBuild(progress));assert(!favoritesDirty()&&favoritesGroupCount()==1);
 String originalDirectory=favoritesDirectory();auto original=read(SD.resolve(originalDirectory+"/favorites-001.ocg"));auto parsed=clockpack::parse(original.data(),original.size());assert(parsed->name=="Favorites"&&parsed->faces.size()==3);for(auto &f:parsed->faces)assert(std::strcmp(f.name,faces[0].name)); // no automatic blue Meridian
 toggle(4);validateFailure=true;assert(!favoritesBuild(progress));validateFailure=false;assert(String(favoritesDirectory())==originalDirectory&&favoritesDirty());assert(read(SD.resolve(originalDirectory+"/favorites-001.ocg"))==original);
 failWritePath="catalog-";failWriteRemaining=17;assert(!favoritesBuild(progress));failWritePath.clear();failWriteRemaining=-1;favoritesBegin();assert(favoritesCount()==4&&favoritesDirty()&&String(favoritesDirectory())==originalDirectory);assert(read(SD.resolve(originalDirectory+"/favorites-001.ocg"))==original);
 assert(favoritesBuild(progress));assert(!favoritesDirty());favoritesBegin();assert(favoritesCount()==4&&!favoritesDirty());
 toggle(1);toggle(2);toggle(3);toggle(4);assert(favoritesCount()==0);assert(favoritesBuild(progress));assert(favoritesGroupCount()==0&&!favoritesDirty());
 favoritesFace(5);failWritePath="selections-";failWriteRemaining=17;assert(!favoritesToggle());failWritePath.clear();failWriteRemaining=-1;favoritesBegin();assert(favoritesCount()==0);
 fakeMounted=false;favoritesFace(5);assert(!favoritesToggle());fakeMounted=true;favoritesBegin();assert(favoritesCount()==0);
 toggle(5);favoritesBegin();assert(favoritesCount()==1);
 // Corrupt the most recent selection bank: the previous committed bank survives.
 auto a=read(SD.resolve("/clock-favorites/selections-0.bin")),b=read(SD.resolve("/clock-favorites/selections-1.bin"));auto seq=[](const std::vector<uint8_t>&v){return v[8]|uint32_t(v[9])<<8|uint32_t(v[10])<<16|uint32_t(v[11])<<24;};int newest=seq(a)>seq(b)?0:1;
 auto corrupt=[&](int bank){auto path=SD.resolve(String("/clock-favorites/selections-")+bank+".bin");auto data=read(path);data.back()^=1;std::ofstream f(path,std::ios::binary|std::ios::trunc);f.write((const char*)data.data(),data.size());};
 corrupt(newest);favoritesBegin();assert(favoritesCount()==0);corrupt(1-newest);favoritesBegin();favoritesFace(1);assert(!favoritesToggle());assert(std::strstr(favoritesError(),"damaged"));
 std::puts("PASS: actual firmware storage: restart persistence, scoped Later, explicit-only favorites, validation rollback, torn catalog/selection writes, no-card rollback, CRC fallback, damaged-list protection");
}
