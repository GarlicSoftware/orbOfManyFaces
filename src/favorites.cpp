#include <Arduino.h>
#include <SD.h>
#include <esp_heap_caps.h>
#include <mbedtls/sha256.h>
#include <memory>
#include <vector>
#include "favorites.h"
#include "favorites_logic.h"
#include "face_serialization.h"
#include "group_format.h"
#include "face_groups.h"

namespace {
constexpr unsigned MAX_BOOKMARKS=512;
constexpr char BASE[]="/clock-favorites";
template<class T> struct Psram {
    using value_type=T;
    Psram()=default;template<class U> Psram(const Psram<U>&){}
    T *allocate(size_t n){auto p=(T*)heap_caps_malloc(n*sizeof(T),MALLOC_CAP_SPIRAM);if(!p)throw std::bad_alloc();return p;}
    void deallocate(T *p,size_t){free(p);}
    template<class U> bool operator==(const Psram<U>&)const{return true;}
    template<class U> bool operator!=(const Psram<U>&)const{return false;}
};
using Bytes=std::vector<uint8_t,Psram<uint8_t>>;
struct Memory:clockpack::Output {
    Bytes data;
    void bytes(const uint8_t *p,size_t n)override{if(n)data.insert(data.end(),p,p+n);}
};
struct Hash:clockpack::Output {
    mbedtls_sha256_context ctx;File file;uint32_t size=0;uint8_t *pending=nullptr;size_t buffered=0;
    Hash(){mbedtls_sha256_init(&ctx);mbedtls_sha256_starts(&ctx,0);}
    ~Hash(){free(pending);mbedtls_sha256_free(&ctx);}
    void flushBuffer(){if(!buffered)return;if(file.write(pending,buffered)!=buffered)throw std::runtime_error("Favorites SD write failed");buffered=0;delay(1);}
    void stored(const uint8_t *p,size_t n){
        while(n){
            if(!buffered&&n>=4096){size_t k=std::min(n,size_t(16384));if(file.write(p,k)!=k)throw std::runtime_error("Favorites SD write failed");p+=k;n-=k;delay(1);continue;}
            if(!pending){pending=(uint8_t*)heap_caps_malloc(4096,MALLOC_CAP_INTERNAL);if(!pending)throw std::bad_alloc();}
            size_t k=std::min(n,size_t(4096-buffered));memcpy(pending+buffered,p,k);buffered+=k;p+=k;n-=k;if(buffered==4096)flushBuffer();
        }
    }
    void bytes(const uint8_t *p,size_t n)override{
        size+=n;
        while(n){size_t k=std::min(n,size_t(16384));if(file)stored(p,k);mbedtls_sha256_update(&ctx,p,k);p+=k;n-=k;}
    }
    void finish(uint8_t *digest){if(file){flushBuffer();file.flush();file.close();}mbedtls_sha256_finish(&ctx,digest);}

};
struct Record {uint8_t key[32];char source[61];uint16_t index=0;bool selected=false;};
using Records=std::vector<Record,Psram<Record>>;
struct Pack {uint32_t size=0;uint8_t hash[32]={};};
struct Catalog {uint32_t sequence=0,revision=0;String folder;std::vector<Pack> packs;int bank=-1;};
Records records;Catalog catalog;
uint32_t sequence=0,revision=0,deferred=0;
int stateBank=-1,currentIndex=0;
bool ready=false,currentSelected=false;
uint8_t currentKey[32]={};char currentSource[61]={};uint16_t currentSourceIndex=0;
String error,directory;std::vector<String> groupNames;
bool safeSource(const char *s){size_t n=strlen(s);return !n||(n>=5&&n<=60&&!strcmp(s+n-4,".ocg")&&!strstr(s,"..")&&strspn(s,"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.")==n);}
String bankPath(bool cat,int slot){return String(BASE)+(cat?"/catalog-":"/selections-")+slot+".bin";}
void identify(const Face &f,uint8_t *key){Hash h;clockpack::writeFace(h,f);h.finish(key);}
int find(const uint8_t *key){for(unsigned i=0;i<records.size();++i)if(!memcmp(records[i].key,key,32))return i;return -1;}
bool envelope(const String &path,const char *magic,uint32_t &seq,Bytes &payload){
    File f=SD.open(path);if(!f||f.size()<20||f.size()>131072)return false;
    uint8_t header[20];if(f.read(header,20)!=20||memcmp(header,magic,8))return false;
    clockpack::Reader r{header+8,header+20};seq=r.u32();uint32_t n=r.u32(),crc=r.u32();if(n!=f.size()-20)return false;
    payload.resize(n);if(f.read(payload.data(),n)!=n)return false;
    return clockpack::crc32(payload.data(),n)==crc;
}
void commit(bool cat,int &bank,uint32_t &seq,const Bytes &payload){
    if(!groupsStorageReady())throw std::runtime_error("SD card unavailable");
    if(!SD.exists(BASE)&&!SD.mkdir(BASE))throw std::runtime_error("Cannot create Favorites storage");
    const int next=bank==0?1:0;String path=bankPath(cat,next);
    Memory h;h.bytes((const uint8_t*)(cat?"ORBFAC01":"ORBFAV01"),8);h.u32(seq+1);h.u32(payload.size());h.u32(clockpack::crc32(payload.data(),payload.size()));
    // Only the inactive, application-owned bank is replaced. The active copy
    // survives a reset at any point before readback has succeeded.
    if(SD.exists(path)&&!SD.remove(path))throw std::runtime_error("Cannot update Favorites storage");
    File f=SD.open(path,FILE_WRITE);if(!f)throw std::runtime_error("Cannot save Favorites");
    bool ok=f.write(h.data.data(),h.data.size())==h.data.size()&&f.write(payload.data(),payload.size())==payload.size();f.flush();f.close();
    Bytes check;uint32_t checkSeq=0;
    if(!ok||!envelope(path,cat?"ORBFAC01":"ORBFAV01",checkSeq,check)||checkSeq!=seq+1||check!=payload)throw std::runtime_error("Favorites save verification failed");
    bank=next;seq=checkSeq;
}
void save(){
    Memory w;w.u32(revision);w.u32(deferred);w.u16(records.size());
    for(auto &r:records){w.bytes(r.key,32);w.str(r.source,60);w.u16(r.index);w.u8(r.selected);}
    commit(false,stateBank,sequence,w.data);
}
bool readState(int bank,uint32_t &seq,uint32_t &rev,uint32_t &defer,Records &out){
    try{
        Bytes data;if(!envelope(bankPath(false,bank),"ORBFAV01",seq,data))return false;
        clockpack::Reader r{data.data(),data.data()+data.size()};rev=r.u32();defer=r.u32();unsigned count=r.u16();if(count>MAX_BOOKMARKS)return false;
        out.clear();out.reserve(count);
        for(unsigned i=0;i<count;++i){Record record={};memcpy(record.key,r.take(32),32);strcpy(record.source,r.str(60));record.index=r.u16();unsigned selected=r.u8();if(!safeSource(record.source)||record.index>255||selected>1)return false;record.selected=selected;for(auto &old:out)if(!memcmp(old.key,record.key,32))return false;out.push_back(record);}
        return r.p==r.end;
    }catch(...){return false;}
}
String packPath(const String &folder,unsigned i){char suffix[40];snprintf(suffix,sizeof(suffix),"/favorites-%03u.ocg",i+1);return String(BASE)+"/"+folder+suffix;}
bool readCatalog(int bank,Catalog &out){
    try{
        Bytes data;if(!envelope(bankPath(true,bank),"ORBFAC01",out.sequence,data))return false;
        clockpack::Reader r{data.data(),data.data()+data.size()};out.revision=r.u32();out.folder=r.str(32);unsigned count=r.u16();
        if(count>MAX_BOOKMARKS)return false;
        if(count&&(out.folder.length()!=12||!out.folder.startsWith("set-")||strspn(out.folder.c_str()+4,"0123456789abcdef")!=8))return false;
        if(!count&&out.folder.length())return false;
        out.packs.resize(count);
        for(unsigned i=0;i<count;++i){auto &p=out.packs[i];p.size=r.u32();memcpy(p.hash,r.take(32),32);File f=SD.open(packPath(out.folder,i));if(!f||f.size()!=p.size)return false;}
        out.bank=bank;return r.p==r.end;
    }catch(...){return false;}
}
void saveCatalog(Catalog &next){Memory w;w.u32(next.revision);w.str(next.folder.c_str(),32);w.u16(next.packs.size());for(auto &p:next.packs){w.u32(p.size);w.bytes(p.hash,32);}commit(true,next.bank,next.sequence,w.data);}
void removeCatalogFiles(const Catalog &old){
    // Names come only from a validated catalog; never recurse through SD paths.
    if(!old.folder.length())return;
    for(unsigned i=0;i<old.packs.size();++i)SD.remove(packPath(old.folder,i));
    SD.rmdir(String(BASE)+"/"+old.folder);
}
struct Source {
    uint8_t *data=nullptr;std::unique_ptr<clockpack::Group> group;String path;
    ~Source(){clear();}
    void clear(){group.reset();free(data);data=nullptr;path="";}
    const Face &get(const Record &record){
        if(!record.source[0]){
            uint8_t hash[32];if(record.index<FACE_COUNT){identify(faces[record.index],hash);if(!memcmp(hash,record.key,32))return faces[record.index];}
            for(int i=0;i<FACE_COUNT;++i){identify(faces[i],hash);if(!memcmp(hash,record.key,32))return faces[i];}
            throw std::runtime_error("Original built-in face is unavailable");
        }
        String next=String("/clock-groups/")+record.source;
        if(path!=next){
            clear();File f=SD.open(next);if(!f||f.size()<24||f.size()>clockpack::MAX_PACK)throw std::runtime_error("Original library missing or invalid");
            size_t n=f.size();data=(uint8_t*)heap_caps_malloc(n,MALLOC_CAP_SPIRAM);if(!data)throw std::runtime_error("Not enough source RAM");
            for(size_t offset=0;offset<n;){size_t k=std::min(n-offset,size_t(16384));if(f.read(data+offset,k)!=k)throw std::runtime_error("Cannot read original artwork");offset+=k;delay(1);}
            group=clockpack::parse(data,n);path=next;
        }
        uint8_t hash[32];
        if(record.index<group->faces.size()){const Face &f=group->faces[record.index];identify(f,hash);if(!memcmp(hash,record.key,32))return f;}
        for(auto &f:group->faces){identify(f,hash);if(!memcmp(hash,record.key,32))return f;}
        throw std::runtime_error("Original face changed; rebuild postponed");
    }
};
String tempFace(const String &folder,unsigned i){return String(BASE)+"/"+folder+"/face-"+i+".part";}
void hashFile(const String &path,Pack &result){File f=SD.open(path);if(!f)throw std::runtime_error("Cannot verify Favorites file");Hash h;uint8_t data[2048];while(f.available()){size_t n=f.read(data,sizeof(data));if(!n)throw std::runtime_error("Favorites SD read failed");h.bytes(data,n);delay(1);}result.size=h.size;h.finish(result.hash);}
}

void favoritesBegin(){
    error="";ready=false;
    if(!groupsStorageReady()){error="SD card unavailable";return;}
    Catalog c[2];bool valid[2]={readCatalog(0,c[0]),readCatalog(1,c[1])};
    if(valid[0]||valid[1]){int i=!valid[0]?1:!valid[1]?0:int(int32_t(c[1].sequence-c[0].sequence)>0);catalog=std::move(c[i]);}
    directory=catalog.folder.length()?String(BASE)+"/"+catalog.folder:"";
    groupNames.clear();for(unsigned i=0;i<catalog.packs.size();++i)groupNames.emplace_back(favorites::groupName(i).c_str());
    Records choices[2];uint32_t seq[2]={},rev[2]={},def[2]={};bool ok[2]={readState(0,seq[0],rev[0],def[0],choices[0]),readState(1,seq[1],rev[1],def[1],choices[1])};
    if(ok[0]||ok[1]){int i=!ok[0]?1:!ok[1]?0:int(int32_t(seq[1]-seq[0])>0);records=std::move(choices[i]);stateBank=i;sequence=seq[i];revision=rev[i];deferred=def[i];ready=true;}
    else if(SD.exists(bankPath(false,0))||SD.exists(bankPath(false,1))){error="Favorites list damaged; saved copies kept";}
    else {records.clear();stateBank=-1;sequence=revision=deferred=0;ready=true;}
}
void favoritesFace(int face){
    currentIndex=face;identify(groupFace(face),currentKey);int i=find(currentKey);currentSelected=i>=0&&records[i].selected;
    currentSource[0]=0;currentSourceIndex=0;
    if(!groupCurrent()){currentSourceIndex=face;}
    else if(strncmp(groupId(groupCurrent()),"favorites/",10)&&face>0){strncpy(currentSource,groupId(groupCurrent()),60);currentSource[60]=0;currentSourceIndex=face-1;}
}
bool favoritesCurrent(){return currentSelected;}
int favoritesCount(){int n=0;for(auto &r:records)if(r.selected)++n;return n;}
bool favoritesDirty(){return revision!=catalog.revision;}
bool favoritesPrompt(){return favorites::needsPrompt(revision,catalog.revision,deferred);}
const char *favoritesError(){return error.c_str();}
bool favoritesToggle(){
    if(!ready){favoritesBegin();favoritesFace(currentIndex);}
    if(!ready)return false;
    int i=find(currentKey);bool created=i<0;uint32_t oldRev=revision;
    try{
        if(created){if(records.size()>=MAX_BOOKMARKS)throw std::runtime_error("Bookmark storage is full");if(groupCurrent()&&!strncmp(groupId(groupCurrent()),"favorites/",10))throw std::runtime_error("Original bookmark reference unavailable");Record r={};memcpy(r.key,currentKey,32);strcpy(r.source,currentSource);r.index=currentSourceIndex;records.push_back(r);i=records.size()-1;}
        records[i].selected=!records[i].selected;++revision;
        save();currentSelected=records[i].selected;error="";return true;
    }catch(const std::exception &e){error=e.what();revision=oldRev;if(created&&i>=0)records.pop_back();else if(i>=0)records[i].selected=!records[i].selected;return false;}
}
bool favoritesLater(){uint32_t old=deferred;deferred=revision;try{save();return true;}catch(const std::exception&e){deferred=old;error=e.what();return false;}}
int favoritesGroupCount(){return catalog.packs.size();}
const char *favoritesDirectory(){return directory.c_str();}
void favoritesStatus(){Serial.printf("!clock {\"ok\":true,\"favorites\":%d,\"current_favorite\":%s,\"dirty\":%s,\"prompt\":%s,\"favorite_groups\":%d,\"revision\":%lu,\"built_revision\":%lu}\n",favoritesCount(),currentSelected?"true":"false",favoritesDirty()?"true":"false",favoritesPrompt()?"true":"false",favoritesGroupCount(),(unsigned long)revision,(unsigned long)catalog.revision);}

bool favoritesBuild(void (*progress)(const char *)){
    error="";if(!ready){error="Favorites storage unavailable";return false;}
    if(groupUploading()){error="USB upload in progress";return false;}
    String folder;bool published=false;unsigned copies=0;std::vector<favorites::Bin> bins;
    try{
        if(!groupsStorageReady())throw std::runtime_error("SD card unavailable");
        groupsMaintenanceReset();
        std::vector<unsigned> selected;for(unsigned i=0;i<records.size();++i)if(records[i].selected)selected.push_back(i);
        std::stable_sort(selected.begin(),selected.end(),[](unsigned a,unsigned b){int order=strcmp(records[a].source,records[b].source);return order?order<0:records[a].index<records[b].index;});
        Catalog next;next.bank=catalog.bank;next.sequence=catalog.sequence;next.revision=revision;
        if(!selected.empty()){
            if(!SD.exists(BASE)&&!SD.mkdir(BASE))throw std::runtime_error("Cannot create Favorites storage");
            do{char name[20];snprintf(name,sizeof(name),"set-%08lx",(unsigned long)esp_random());folder=name;}while(SD.exists(String(BASE)+"/"+folder));
            if(!SD.mkdir(String(BASE)+"/"+folder))throw std::runtime_error("Cannot stage Favorites");
            next.folder=folder;
            std::vector<favorites::Size> sizes;Source source;
            for(unsigned i=0;i<selected.size();++i){
                char message[48];snprintf(message,sizeof(message),"Copying artwork %u/%u",i+1,unsigned(selected.size()));progress(message);
                const Record &record=records[selected[i]];const Face &face=source.get(record);
                Hash w;w.file=SD.open(tempFace(folder,i),FILE_WRITE);if(!w.file)throw std::runtime_error("Cannot stage favorite artwork");
                ++copies;clockpack::writeFace(w,face);uint8_t hash[32];w.finish(hash);if(memcmp(hash,record.key,32))throw std::runtime_error("Favorite identity verification failed");
                sizes.push_back({w.size,clockpack::decodedBytes(face),clockpack::metadataBytes(face)});
            }
            source.clear();
            // Keep working headroom for allocator fragmentation and retained configuration.
            uint32_t ram=std::min(ESP.getFreePsram(),uint32_t(6*1024*1024)),heap=heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
            bins=favorites::partition(sizes,ram,heap);
            for(unsigned b=0;b<bins.size();++b){
                char message[48];snprintf(message,sizeof(message),"Packing Favorites %u/%u",b+1,unsigned(bins.size()));progress(message);
                File f=SD.open(packPath(folder,b),FILE_WRITE);if(!f)throw std::runtime_error("Cannot create Favorites group");
                uint8_t zeros[24]={};if(f.write(zeros,24)!=24)throw std::runtime_error("Favorites header write failed");
                Memory prefix;std::string label=favorites::groupName(b);prefix.str(label.c_str(),60);
                uint32_t crc=~0u,bytes=0,raw=0;
                auto write=[&](const uint8_t *p,size_t n){if(f.write(p,n)!=n)throw std::runtime_error("Favorites group write failed");bytes+=n;for(size_t j=0;j<n;++j){crc^=p[j];for(int k=0;k<8;++k)crc=(crc>>1)^(0xedb88320u&uint32_t(-int(crc&1)));}};
                write(prefix.data.data(),prefix.data.size());
                for(unsigned i:bins[b].items){File input=SD.open(tempFace(folder,i));if(!input||input.size()!=sizes[i].packed)throw std::runtime_error("Staged artwork is incomplete");uint8_t block[2048];while(input.available()){size_t n=input.read(block,sizeof(block));if(!n)throw std::runtime_error("Staged artwork read failed");write(block,n);delay(1);}raw=std::max(raw,sizes[i].decoded);}
                Memory head;head.bytes((const uint8_t*)"ORBGRP01",8);head.u32(bins[b].items.size());head.u32(bytes);head.u32(~crc);head.u32(raw);
                if(!f.seek(0)||f.write(head.data.data(),24)!=24)throw std::runtime_error("Favorites header save failed");f.flush();f.close();
                snprintf(message,sizeof(message),"Validating Favorites %u/%u",b+1,unsigned(bins.size()));progress(message);
                if(!groupsValidateFile(packPath(folder,b).c_str()))throw std::runtime_error(groupError());
                Pack pack;hashFile(packPath(folder,b),pack);next.packs.push_back(pack);
            }
        }
        // Preserve both committed generations. Only the older, superseded bank's
        // directory can be collected after the new catalog has been verified.
        Catalog obsolete;bool haveOld=readCatalog(catalog.bank==0?1:0,obsolete);
        String nextDirectory=next.folder.length()?String(BASE)+"/"+next.folder:"";
        std::vector<String> nextNames;nextNames.reserve(next.packs.size());for(unsigned i=0;i<next.packs.size();++i)nextNames.emplace_back(favorites::groupName(i).c_str());
        progress("Saving Favorites libraries");saveCatalog(next);published=true;catalog=std::move(next);directory=std::move(nextDirectory);groupNames.swap(nextNames);
        if(haveOld&&obsolete.folder!=catalog.folder)removeCatalogFiles(obsolete);
        for(unsigned i=0;i<copies;++i)SD.remove(tempFace(folder,i));
        return true;
    }catch(const std::exception &e){if(published)return true;error=e.what();groupsMaintenanceReset();for(unsigned i=0;i<copies;++i)SD.remove(tempFace(folder,i));for(unsigned b=0;b<bins.size();++b)SD.remove(packPath(folder,b));if(folder.length())SD.rmdir(String(BASE)+"/"+folder);return false;}
}

const char *favoritesGroupName(int index){return groupNames[index].c_str();}
