#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <esp_heap_caps.h>
#include "face_groups.h"
#include "group_format.h"
#include "renderer.h"
#include <algorithm>

namespace {
SPIClass sdSpi(HSPI);
bool mounted=false;
struct Entry {String id,name;};
std::vector<Entry> entries;
std::unique_ptr<clockpack::Group> loaded;
std::vector<Face> rotation;
uint8_t *pack=nullptr;
int current=0;
uint32_t sdReads=0,cachedBytes=0;
String error;
File upload;String uploadPath;uint32_t remaining=0,uploadSize=0;
uint32_t uploadActivity=0;
bool mount() {
    if(mounted)return true;
    sdSpi.begin(2,3,1,41);
    for(uint32_t hz:{20000000u,10000000u,4000000u}) {
        if(SD.begin(41,sdSpi,hz)&&SD.cardType()!=CARD_NONE){mounted=true;return true;}
        SD.end();delay(20);
    }
    error="SD card unavailable";return false;
}
void builtin() {
    rendererSetFaces(faces,FACE_COUNT);rotation.clear();loaded.reset();free(pack);pack=nullptr;current=0;cachedBytes=0;
}
bool fail(const char *why){error=why;return false;}
}
void groupsScan() {
    if(upload)return;
    Entry old=current&&current<int(entries.size())?entries[current]:Entry{};
    String oldId=old.id;
    entries.clear();entries.push_back({"","Built-in clocks"});
    File dir;if(mount())dir=SD.open("/clock-groups");
    while(File f=dir.openNextFile()) {
        String name=f.name();int slash=name.lastIndexOf('/');if(slash>=0)name=name.substring(slash+1);
        if(strspn(name.c_str(),"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.")==name.length()&&!f.isDirectory()&&name.endsWith(".ocg")&&name.length()<64&&entries.size()<64) {
            uint8_t head[24];
            if(f.read(head,24)==24&&!memcmp(head,"ORBGRP01",8)) {
                uint8_t len[2];if(f.read(len,2)!=2)continue;unsigned n=len[0]|unsigned(len[1])<<8;
                char label[61]={};if(n&&n<=60&&f.read((uint8_t*)label,n)==n){bool safe=true;for(unsigned j=0;j<n;++j)if(label[j]<32||label[j]>126||label[j]=='"'||label[j]=='\\')safe=false;if(safe)entries.push_back({name,label});}
            }
        }
        f.close();
    }
    std::sort(entries.begin()+1,entries.end(),[](const Entry&a,const Entry&b){return a.id<b.id;});
    if(oldId.length()) {
        bool found=false;for(int i=1;i<int(entries.size());++i)if(entries[i].id==oldId){current=i;found=true;break;}
        // A removed card does not invalidate the group already cached in RAM.
        if(!found){if(entries.size()>=64)entries.pop_back();entries.push_back(old);current=entries.size()-1;}
    }
}
int groupsCount(){return entries.size();}
const char *groupName(int i){return entries[i].name.c_str();}
const char *groupId(int i){return entries[i].id.c_str();}
int groupCurrent(){return current;}
int groupFaceCount(){return current?rotation.size():FACE_COUNT;}
const Face &groupFace(int i){return current?rotation[i]:faces[i];}
const char *groupError(){return error.c_str();}
bool groupSelect(int index) {
    error="";if(upload)return fail("Upload in progress");
    if(index<0||index>=groupsCount())return fail("Unknown group");
    if(!index){builtin();return rendererPrepare(0);}
    String path="/clock-groups/"+entries[index].id;
    // Release the old group first: two compressed groups need not coexist.
    // Any failure falls back to the firmware's built-in blue Meridian.
    builtin();
    File f=SD.open(path);if(!f)return fail("Cannot open group");
    const size_t size=f.size();if(size<24||size>clockpack::MAX_PACK)return fail("Invalid group size");
    uint8_t header[24];if(f.read(header,24)!=24)return fail("SD read failed");
    clockpack::Reader h{header+20,header+24};uint32_t raw=h.u32();
    if(raw>clockpack::MAX_RAW||size+size_t(raw)+700000+262144>ESP.getFreePsram())return fail("Group exceeds free RAM");
    pack=(uint8_t*)heap_caps_malloc(size,MALLOC_CAP_SPIRAM);
    if(!pack)return fail("Not enough group RAM");
    memcpy(pack,header,24);size_t offset=24;
    while(offset<size){size_t n=min(size-offset,size_t(16384));if(f.read(pack+offset,n)!=n){builtin();return fail("SD read failed");}offset+=n;delay(1);}f.close();
    try {loaded=clockpack::parse(pack,size);}catch(const std::exception &e){String why=e.what();builtin();error=why;return false;}
    uint32_t largest=700000,second=700000;
    for(const auto &item:loaded->faces){uint32_t n=item.plate.rawSize+item.overlay.image.rawSize;for(auto &s:item.hands)n+=s.image.rawSize;for(auto &s:item.shadows)n+=s.image.rawSize;if(n>largest){second=largest;largest=n;}else if(n>second)second=n;}
    if(size_t(largest)+second+262144>ESP.getFreePsram()){builtin();return fail("Group decode RAM exceeded");}
    rotation.reserve(loaded->faces.size()+1);rotation.push_back(faces[0]); // Blue Meridian is shared by every group.
    for(const auto &item:loaded->faces)rotation.push_back(item);
    rendererSetFaces(rotation.data(),rotation.size());
    // Decode every face once to reject broken compressed images before committing.
    for(int i=0;i<int(rotation.size());++i)if(!rendererPrepare(i)){builtin();return fail("Invalid image / face RAM");}
    if(!rendererPrepare(0)){builtin();return fail("Cannot prepare Meridian");}
    current=index;cachedBytes=size;sdReads+=size;return true;
}
bool groupUploading(){return bool(upload);}
void groupUploadPoll(){if(upload&&millis()-uploadActivity>15000)groupUploadAbort();}
uint32_t groupSdReadBytes(){return sdReads;}
uint32_t groupCachedBytes(){return cachedBytes;}
void groupUploadAbort(){if(upload){upload.close();SD.remove(uploadPath+".part");Serial.setRxBufferSize(256);}remaining=0;}
bool groupUploadBegin(const char *name,uint32_t bytes) {
    groupUploadAbort();
    error="";
    if(!mount())return false;
    size_t n=strlen(name);if(n<5||n>60||strcmp(name+n-4,".ocg")||strspn(name,"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.")!=n||strstr(name,".."))return fail("Invalid pack filename");
    if(bytes<24||bytes>clockpack::MAX_PACK)return fail("Invalid upload size");
    SD.mkdir("/clock-groups");uploadPath=String("/clock-groups/")+name;
    if(SD.exists(uploadPath))return fail("Group already exists");
    if(SD.totalBytes()-SD.usedBytes()<bytes+65536)return fail("SD card full");
    // SD writes can stall USB consumption. Buffer a whole acknowledged chunk;
    // return this temporary internal RAM when copying finishes.
    if(Serial.setRxBufferSize(32768)<32768)return fail("USB buffer allocation failed");
    SD.remove(uploadPath+".part");upload=SD.open(uploadPath+".part",FILE_WRITE);remaining=bytes;uploadSize=bytes;
    uploadActivity=millis();
    if(!upload){Serial.setRxBufferSize(256);return fail("Cannot create group");}return true;
}
bool groupUploadChunk(uint32_t offset,uint32_t bytes) {
    uploadActivity=millis();
    uint32_t written=uploadSize-remaining;
    bool duplicate=offset<written&&bytes<=written-offset;
    if(!upload||!bytes||bytes>16384||offset>uploadSize||bytes>uploadSize-offset||(!duplicate&&(offset!=written||bytes>remaining)))return fail("Invalid upload offset");
    uint8_t data[1024];size_t total=0;uint32_t start=millis();
    while(total<bytes&&millis()-start<5000) {
        size_t n=min(size_t(Serial.available()),min(sizeof(data),size_t(bytes-total)));
        if(!n){delay(1);continue;}
        Serial.readBytes(data,n);if(!duplicate&&upload.write(data,n)!=n){groupUploadAbort();return fail("SD write failed");}total+=n;
    }
    if(total!=bytes){groupUploadAbort();return fail("Upload timed out");}if(!duplicate)remaining-=bytes;return true;
}
bool groupUploadFinish() {
    if(!upload||remaining)return fail("Upload incomplete");
    upload.flush();upload.close();Serial.setRxBufferSize(256);
    if(!SD.rename(uploadPath+".part",uploadPath))return fail("Cannot finalize upload");
    // Scan only names here. Full checksum, geometry and decode checks precede selection.
    groupsScan();return true;
}
