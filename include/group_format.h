#pragma once
#include "assets.h"
#include <vector>
#include <memory>
#include <string>
#include <cstring>
#include <stdexcept>

// Portable, explicitly little-endian wire format. No native pointers on disk.
namespace clockpack {
constexpr uint32_t MAX_PACK=3*1024*1024, MAX_RAW=4000000;
inline uint32_t crc32(const uint8_t *p,size_t n) {
    uint32_t c=~0u;
    while(n--) {c^=*p++;for(int i=0;i<8;++i)c=(c>>1)^(0xedb88320u&uint32_t(-int(c&1)));}
    return ~c;
}
struct FontStorage {Font font={};Glyph glyphs[95]={};};
struct Group {
    std::string name;
    std::vector<Face> faces;
    std::vector<std::unique_ptr<FontStorage>> fonts;
    uint32_t maxRaw=0;
};
struct Reader {
    const uint8_t *p,*end;
    const uint8_t *take(size_t n) {if(n>size_t(end-p))throw std::runtime_error("truncated pack");auto v=p;p+=n;return v;}
    uint32_t u32(){auto b=take(4);return b[0]|(uint32_t(b[1])<<8)|(uint32_t(b[2])<<16)|(uint32_t(b[3])<<24);}
    uint16_t u16(){auto b=take(2);return b[0]|(uint16_t(b[1])<<8);}
    int16_t i16(){return int16_t(u16());}
    uint8_t u8(){return *take(1);}
    const char *str(size_t max=160) {auto n=u16();if(n>max)throw std::runtime_error("long string");auto b=take(n+1);if(b[n]||memchr(b,0,n))throw std::runtime_error("bad string");for(size_t i=0;i<n;++i)if(b[i]<32||b[i]>126)throw std::runtime_error("non ASCII string");return (const char*)b;}
    Packed image() {auto size=u32(),raw=u32();if(size>MAX_PACK||raw>MAX_RAW||(!size!=!raw))throw std::runtime_error("bad image");return {take(size),size,raw};}
    Sprite sprite() {Sprite s={};s.image=image();s.width=u16();s.height=u16();s.pivotX=i16();s.pivotY=i16();s.centerX=i16();s.centerY=i16();s.blend=u8();if(s.width>1024||s.height>1024||s.image.rawSize!=uint32_t(s.width)*s.height*3||s.blend>2)throw std::runtime_error("bad sprite");return s;}
    const Font *font(Group &g) {
        if(!u8())return nullptr;
        auto f=std::unique_ptr<FontStorage>(new FontStorage);f->font.baselineOffset=int8_t(u8());f->font.lineHeight=u8();auto n=u32();if(n>1024*1024)throw std::runtime_error("bad font");
        for(auto &a:f->glyphs) {a.offset=u32();a.width=u8();a.height=u8();a.advance=u8();a.left=int8_t(u8());a.top=int8_t(u8());if(a.offset>n||uint32_t(a.width)*a.height>n-a.offset)throw std::runtime_error("bad glyph");}
        f->font.pixels=take(n);f->font.glyphs=f->glyphs;auto result=&f->font;g.fonts.push_back(std::move(f));return result;
    }
};
inline std::unique_ptr<Group> parse(const uint8_t *data,size_t size) {
    if(size<24||size>MAX_PACK||memcmp(data,"ORBGRP01",8))throw std::runtime_error("bad pack header");
    Reader r{data+8,data+size};auto count=r.u32(),payload=r.u32(),crc=r.u32(),declared=r.u32();
    if(!count||count>32||payload!=size-24||crc32(data+24,payload)!=crc)throw std::runtime_error("bad pack checksum/count");
    auto g=std::unique_ptr<Group>(new Group);g->name=r.str(60);g->faces.reserve(count);
    for(unsigned i=0;i<count;++i) {
        Face f={};f.name=r.str(96);f.plate=r.image();if(f.plate.rawSize!=466*466*2)throw std::runtime_error("bad plate");
        uint32_t raw=f.plate.rawSize;
        for(auto &s:f.hands){s=r.sprite();raw+=s.image.rawSize;}
        for(auto &s:f.shadows){s=r.sprite();raw+=s.image.rawSize;}
        f.overlay=r.sprite();raw+=f.overlay.image.rawSize;
        unsigned seen=0;for(auto &o:f.order){o=r.u8();if(o>4||(seen&(1<<o)))throw std::runtime_error("bad order");seen|=1<<o;}
        for(auto &t:f.text){t.font=r.font(*g);if(!t.font)continue;t.format=r.str(79);t.x=r.i16();t.y=r.i16();t.color=r.u16();t.opacity=r.u8();t.upper=r.u8();t.curved=r.u8();t.radius=r.i16();t.arcDegrees=r.i16()/10.0f;t.align=r.u8();t.glowFont=r.font(*g);t.glowColor=r.u16();t.bgColor=r.u16();t.bgOpacity=r.u8();t.bgRadius=r.u8();if(t.align>2||t.radius<0||t.radius>1000)throw std::runtime_error("bad text");}
        f.textOverHands=r.u8();f.plateFollow=r.u8();if(f.plateFollow>3||raw>MAX_RAW)throw std::runtime_error("face exceeds RAM limit");
        if(raw>g->maxRaw)g->maxRaw=raw;g->faces.push_back(f);
    }
    if(r.p!=r.end||g->maxRaw!=declared)throw std::runtime_error("pack size mismatch");return g;
}
}
