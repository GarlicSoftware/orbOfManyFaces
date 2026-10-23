#pragma once
#include "assets.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

// Canonical .ocg face encoding: hash and copy compressed assets without decoding.
namespace clockpack {
struct Output {
    virtual ~Output()=default;
    virtual void bytes(const uint8_t *p,size_t n)=0;
    void u8(uint8_t n){bytes(&n,1);}
    void u16(uint16_t n){uint8_t b[]={uint8_t(n),uint8_t(n>>8)};bytes(b,2);}
    void u32(uint32_t n){uint8_t b[]={uint8_t(n),uint8_t(n>>8),uint8_t(n>>16),uint8_t(n>>24)};bytes(b,4);}
    void str(const char *s,size_t max){size_t n=strlen(s);if(n>max)throw std::runtime_error("long face string");u16(n);bytes((const uint8_t*)s,n+1);}
    void image(const Packed &p){u32(p.size);u32(p.rawSize);if(p.size)bytes(p.data,p.size);}
    void sprite(const Sprite &s){image(s.image);u16(s.width);u16(s.height);u16(s.pivotX);u16(s.pivotY);u16(s.centerX);u16(s.centerY);u8(s.blend);}
    void font(const Font *f){
        u8(f!=nullptr);if(!f)return;u8(f->baselineOffset);u8(f->lineHeight);uint32_t n=0;
        for(int i=0;i<95;++i){auto &g=f->glyphs[i];n=std::max(n,g.offset+uint32_t(g.width)*g.height);}
        if(n>1024*1024)throw std::runtime_error("font too large");u32(n);
        for(int i=0;i<95;++i){auto &g=f->glyphs[i];u32(g.offset);u8(g.width);u8(g.height);u8(g.advance);u8(g.left);u8(g.top);}
        bytes(f->pixels,n);
    }
};
inline void writeFace(Output &w,const Face &f){
    w.str(f.name,96);w.image(f.plate);for(auto &s:f.hands)w.sprite(s);for(auto &s:f.shadows)w.sprite(s);w.sprite(f.overlay);
    w.bytes(f.order,5);
    for(auto &t:f.text){w.font(t.font);if(!t.font)continue;w.str(t.format,79);w.u16(t.x);w.u16(t.y);w.u16(t.color);w.u8(t.opacity);w.u8(t.upper);w.u8(t.curved);w.u16(t.radius);w.u16(int16_t(std::lround(t.arcDegrees*10)));w.u8(t.align);w.font(t.glowFont);w.u16(t.glowColor);w.u16(t.bgColor);w.u8(t.bgOpacity);w.u8(t.bgRadius);}
    w.u8(f.textOverHands);w.u8(f.plateFollow);
}
inline uint32_t decodedBytes(const Face &f){uint32_t n=f.plate.rawSize+f.overlay.image.rawSize;for(auto &s:f.hands)n+=s.image.rawSize;for(auto &s:f.shadows)n+=s.image.rawSize;return n;}
inline uint32_t metadataBytes(const Face &f){uint32_t n=sizeof(Face)+64;for(auto &t:f.text){if(t.font)n+=1400;if(t.glowFont)n+=1400;}return n;}
}
