#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <string.h>
#ifdef ARDUINO
#include <miniz.h>
#endif
#include "assets.h"
#include "renderer.h"

namespace {
constexpr int W=466, H=466, CX=233, CY=233;
Arduino_DataBus *bus=nullptr;
Arduino_CO5300 *panel=nullptr;
uint16_t *frame=nullptr, *previous=nullptr, *rotated=nullptr, *strip=nullptr;
int rotationDeg=0;
uint32_t frames=0, lastUs=0, pushed=0;
volatile uint32_t teCount=0;
#ifdef ARDUINO
tinfl_decompressor *inflater=nullptr;
#endif
uint8_t *decoded[10]={}; // plate, five layers, three shadows, overlay
int decodedFace=-1;
const Face *activeFaces=faces;
int activeCount=FACE_COUNT;

bool loadFace(int index) {
    if (index==decodedFace) return true;
    const Face &f=activeFaces[index];
    const Packed *images[10]={&f.plate,&f.hands[0].image,&f.hands[1].image,&f.hands[2].image,&f.hands[3].image,&f.hands[4].image,&f.shadows[0].image,&f.shadows[1].image,&f.shadows[2].image,&f.overlay.image};
    uint8_t *next[10]={};bool ok=true;
    for (int i=0;i<10;++i) {
        const Packed &image=*images[i];if (!image.rawSize) continue;
        next[i]=static_cast<uint8_t *>(heap_caps_malloc(image.rawSize,MALLOC_CAP_SPIRAM));
        if (!next[i]) {ok=false;break;}
#ifdef ARDUINO
        tinfl_init(inflater);size_t input=image.size,output=image.rawSize;
        const auto status=tinfl_decompress(inflater,image.data,&input,next[i],next[i],&output,TINFL_FLAG_PARSE_ZLIB_HEADER|TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
        if (status!=TINFL_STATUS_DONE || output!=image.rawSize) {ok=false;break;}
#else
        if (image.size!=image.rawSize) {ok=false;break;}
        memcpy(next[i],image.data,image.rawSize);
#endif
    }
    if (!ok) {for (auto p:next) free(p);return false;}
    for (int i=0;i<10;++i) {free(decoded[i]);decoded[i]=next[i];}
    decodedFace=index;return true;
}
void IRAM_ATTR teRise() { ++teCount; }

uint16_t blend(uint16_t dst, uint16_t src, unsigned alpha) {
    if (alpha>=255) return src;
    if (!alpha) return dst;
    const unsigned inverse=255-alpha;
    return ((((dst>>11)*inverse+(src>>11)*alpha+127)/255)<<11)
         | (((((dst>>5)&63)*inverse+((src>>5)&63)*alpha+127)/255)<<5)
         | (((dst&31)*inverse+(src&31)*alpha+127)/255);
}

void drawText(const Font &font, const char *text, int centerX, int centerY, uint16_t color,unsigned opacity=255,int align=1) {
    int width=0;
    for (const char *c=text; *c; ++c)
        if (*c>=32 && *c<=126) width+=font.glyphs[*c-32].advance;
    int x=centerX-(align==1 ? width/2 : align==2 ? width : 0);
    const int baseline=centerY+font.baselineOffset;
    for (const char *c=text; *c; ++c) {
        if (*c<32 || *c>126) continue;
        const Glyph &g=font.glyphs[*c-32];
        for (int sy=0;sy<g.height;++sy) {
            const int dy=baseline+g.top+sy;
            if (dy<0 || dy>=H) continue;
            for (int sx=0;sx<g.width;++sx) {
                const int dx=x+g.left+sx;
                if (dx<0 || dx>=W) continue;
                const unsigned a=font.pixels[g.offset+sy*g.width+sx]*opacity/255;
                if (a) frame[dy*W+dx]=blend(frame[dy*W+dx],color,a);
            }
        }
        x+=g.advance;
    }
}

uint16_t spriteInk(uint16_t src,uint16_t dst,int mode) {
    if (!mode) return src;
    const int r=src>>11,g=(src>>5)&63,b=src&31;
    const int dr=dst>>11,dg=(dst>>5)&63,db=dst&31;
    if (mode==1) return ((r*dr/31)<<11)|((g*dg/63)<<5)|(b*db/31);
    return ((31-(31-r)*(31-dr)/31)<<11)|((63-(63-g)*(63-dg)/63)<<5)|(31-(31-b)*(31-db)/31);
}

void drawSprite(const Sprite &s,const uint8_t *pixels,float degrees) {
    if (!pixels || !s.width || !s.height) return;
    if (degrees==0) {
        const int left=s.centerX-s.pivotX,top=s.centerY-s.pivotY;
        for (int y=max(0,-top);y<min(int(s.height),H-top);++y) for (int x=max(0,-left);x<min(int(s.width),W-left);++x) {
            const uint8_t *p=pixels+(y*s.width+x)*3;if (!p[2]) continue;
            uint16_t &dst=frame[(top+y)*W+left+x];
            dst=blend(dst,spriteInk(p[0]|(uint16_t(p[1])<<8),dst,s.blend),p[2]);
        }
        return;
    }
    const float angle=degrees*float(M_PI)/180.0f;
    const float cs=cosf(angle), sn=sinf(angle);
    float minX=W,maxX=0,minY=H,maxY=0;
    for (int y:{0,int(s.height)}) for (int x:{0,int(s.width)}) {
        const float px=x-s.pivotX, py=y-s.pivotY;
        const float dx=s.centerX+px*cs-py*sn,dy=s.centerY+px*sn+py*cs;
        minX=fminf(minX,dx);maxX=fmaxf(maxX,dx);
        minY=fminf(minY,dy);maxY=fmaxf(maxY,dy);
    }
    const int x0=max(0,int(floorf(minX))-1),x1=min(W-1,int(ceilf(maxX))+1);
    const int y0=max(0,int(floorf(minY))-1),y1=min(H-1,int(ceilf(maxY))+1);
    // Oversized imported dial layers can otherwise exceed a whole second in
    // software. Sample those at native resolution; retain bilinear filtering
    // for the small precision hands (including both Meridian faces).
    const bool nativeSampling=uint32_t(s.width)*s.height>100000;
    for (int y=y0;y<=y1;++y) {
        float sx=(x0-s.centerX)*cs+(y-s.centerY)*sn+s.pivotX;
        float sy=-(x0-s.centerX)*sn+(y-s.centerY)*cs+s.pivotY;
        for (int x=x0;x<=x1;++x,sx+=cs,sy-=sn) {
            if(nativeSampling) {
                const int ix=int(sx+.5f),iy=int(sy+.5f);
                if(sx<-.5f||sy<-.5f||ix>=s.width||iy>=s.height)continue;
                const uint8_t *p=pixels+(iy*s.width+ix)*3;
                if(p[2])frame[y*W+x]=blend(frame[y*W+x],spriteInk(p[0]|(uint16_t(p[1])<<8),frame[y*W+x],s.blend),p[2]);
                continue;
            }
            if (sx < -1 || sy < -1 || sx >= s.width || sy >= s.height) continue;
            const int ix=int(floorf(sx)),iy=int(floorf(sy));
            const unsigned ax=unsigned((sx-ix)*256),ay=unsigned((sy-iy)*256);
            uint32_t alphaSum=0,red=0,green=0,blue=0;
            for (int yy=0;yy<2;++yy) for (int xx=0;xx<2;++xx) {
                const int tx=ix+xx,ty=iy+yy;
                if (tx<0 || ty<0 || tx>=s.width || ty>=s.height) continue;
                const uint8_t *p=pixels+(ty*s.width+tx)*3;
                const uint32_t a=p[2]*(xx ? ax : 256-ax)*(yy ? ay : 256-ay);
                if (!a) continue;
                const uint16_t color=p[0]|(uint16_t(p[1])<<8);
                alphaSum+=a;red+=(color>>11)*a;
                green+=((color>>5)&63)*a;blue+=(color&31)*a;
            }
            if (!alphaSum) continue;
            const uint16_t color=((red/alphaSum)<<11)|((green/alphaSum)<<5)|(blue/alphaSum);
            frame[y*W+x]=blend(frame[y*W+x],spriteInk(color,frame[y*W+x],s.blend),(alphaSum+32768)>>16);
        }
    }
}

void drawArc(const ClockText &text,const char *line) {
    const Font &font=*text.font;float total=0;
    for (const char *c=line;*c;++c) if (*c>=32&&*c<=126) total+=font.glyphs[*c-32].advance;
    float cursor=-total/2;const float norm=fmodf(fmodf(text.arcDegrees,360)+360,360);
    const bool bottom=norm>90&&norm<270;
    const float base=text.arcDegrees*float(M_PI)/180,radius=max(1,int(text.radius));
    for (const char *c=line;*c;++c) {
        if (*c<32||*c>126) continue;
        const Glyph &g=font.glyphs[*c-32];
        const float angle=base+(bottom ? -1:1)*(cursor+g.advance/2.0f)/radius;
        cursor+=g.advance;if (!g.width||!g.height) continue;
        const float rotation=angle+(bottom ? float(M_PI):0),cs=cosf(rotation),sn=sinf(rotation);
        const float offsetY=font.baselineOffset+g.top+g.height/2.0f;
        const float dx=CX+sinf(angle)*radius-offsetY*sn,dy=CY-cosf(angle)*radius+offsetY*cs;
        const float reach=hypotf(g.width/2.0f,g.height/2.0f)+1;
        for (int y=max(0,int(floorf(dy-reach)));y<=min(H-1,int(ceilf(dy+reach)));++y)
        for (int x=max(0,int(floorf(dx-reach)));x<=min(W-1,int(ceilf(dx+reach)));++x) {
            const float sx=(x-dx)*cs+(y-dy)*sn+g.width/2.0f;
            const float sy=-(x-dx)*sn+(y-dy)*cs+g.height/2.0f;
            const int ix=int(floorf(sx)),iy=int(floorf(sy));
            if (ix < -1||iy < -1||ix>=g.width||iy>=g.height) continue;
            const float fx=sx-ix,fy=sy-iy;float alpha=0;
            for (int yy=0;yy<2;++yy) for (int xx=0;xx<2;++xx) {
                const int tx=ix+xx,ty=iy+yy;
                if (tx<0||ty<0||tx>=g.width||ty>=g.height) continue;
                alpha+=font.pixels[g.offset+ty*g.width+tx]*(xx ? fx:1-fx)*(yy ? fy:1-fy);
            }
            if (alpha>0) frame[y*W+x]=blend(frame[y*W+x],text.color,unsigned(alpha*text.opacity/255+.5f));
        }
    }
}

void drawBanners(const Face &f,const tm &local,bool valid) {
    for (const ClockText &text:f.text) {
        if (!text.font || !text.format || !valid) continue;
        char line[80]={};strftime(line,sizeof(line),text.format,&local);
        if (text.upper) for (char *c=line;*c;++c) if (*c>='a'&&*c<='z') *c-=32;
        if (text.curved) drawArc(text,line);
        else {
            if(text.bgOpacity) {
                int width=0;for(const char *c=line;*c;++c)if(*c>=32&&*c<=126)width+=text.font->glyphs[*c-32].advance;
                int left=text.x-(text.align==1?width/2:text.align==2?width:0)-8;
                int top=text.y-text.font->lineHeight/2-2,height=text.font->lineHeight+4;
                int radius=min(int(text.bgRadius),height/2);
                for(int y=max(0,top);y<min(H,top+height);++y)for(int x=max(0,left);x<min(W,left+width+16);++x){
                    int dx=max(max(left+radius-x,x-(left+width+15-radius)),0),dy=max(max(top+radius-y,y-(top+height-1-radius)),0);
                    if(dx*dx+dy*dy<=radius*radius)frame[y*W+x]=blend(frame[y*W+x],text.bgColor,text.bgOpacity);
                }
            }
            if(text.glowFont)drawText(*text.glowFont,line,text.x,text.y,text.glowColor,text.opacity,text.align);
            drawText(*text.font,line,text.x,text.y,text.color,text.opacity,text.align);
        }
    }
}

const uint16_t *orientedFrame() {
    if (!rotationDeg) return frame;
    const float rad=rotationDeg*float(M_PI)/180.0f,cs=cosf(rad),sn=sinf(rad);
    for (int y=0;y<H;++y) {
        float sx=-232.5f*cs+(y-232.5f)*sn+232.5f;
        float sy=232.5f*sn+(y-232.5f)*cs+232.5f;
        for (int x=0;x<W;++x,sx+=cs,sy-=sn) {
            const int ix=int(lroundf(sx)),iy=int(lroundf(sy));
            rotated[y*W+x]=(ix>=0&&iy>=0&&ix<W&&iy<H) ? frame[iy*W+ix] : 0;
        }
    }
    return rotated;
}

void flush(bool force) {
    const uint16_t *pixels=orientedFrame();
    // Wait at most one refresh for the panel's vertical-blank signal.
    const uint32_t before=teCount,start=micros();
    while (teCount==before && micros()-start<18000) delayMicroseconds(50);
    if (force) {
        panel->draw16bitRGBBitmap(0,0,const_cast<uint16_t *>(pixels),W,H);
        memcpy(previous,pixels,W*H*2);pushed+=W*H;
    } else {
        // Compare 16-row bands and send only their changed horizontal extent.
        // A stationary dial therefore never costs a full-screen update per tick.
        for (int top=0;top<H;top+=16) {
            const int bottom=min(H,top+16);int left=W,right=-1;
            for (int y=top;y<bottom;++y) for (int x=0;x<W;++x) {
                const int p=y*W+x;
                if (pixels[p]!=previous[p]) {left=min(left,x);right=max(right,x);}
            }
            if (right<left) continue;
            // CO5300 requires even starts and even sizes in BOTH axes. Bands
            // already have even top/height; expand X before packing the pixels.
            // An odd dirty span can otherwise be shifted/truncated by the panel
            // while our previous-frame cache incorrectly records it as drawn.
            left &= ~1;
            right = min(W-1,right|1);
            const int width=right-left+1,height=bottom-top;
            for (int y=0;y<height;++y) {
                memcpy(strip+y*width,pixels+(top+y)*W+left,width*2);
                memcpy(previous+(top+y)*W+left,pixels+(top+y)*W+left,width*2);
            }
            panel->draw16bitRGBBitmap(left,top,strip,width,height);
            pushed+=width*height;
        }
    }
    ++frames;
}
}

bool rendererBegin(int brightness,int degrees) {
#ifdef ARDUINO
    inflater=static_cast<tinfl_decompressor *>(heap_caps_malloc(sizeof(tinfl_decompressor),MALLOC_CAP_INTERNAL));
    if (!inflater) return false;
#endif
    frame=static_cast<uint16_t *>(heap_caps_malloc(W*H*2,MALLOC_CAP_SPIRAM));
    previous=static_cast<uint16_t *>(heap_caps_malloc(W*H*2,MALLOC_CAP_SPIRAM));
    strip=static_cast<uint16_t *>(heap_caps_malloc(W*16*2,MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA));
    rotationDeg=((degrees%360)+360)%360;
    if (rotationDeg) rotated=static_cast<uint16_t *>(heap_caps_malloc(W*H*2,MALLOC_CAP_SPIRAM));
    if (!frame || !previous || !strip || (rotationDeg&&!rotated)) return false;
    bus=new Arduino_ESP32QSPI(12,38,4,5,6,7);
    panel=new Arduino_CO5300(bus,39,0,W,H,6,0,0,0);
    if (!panel->begin(80000000)) return false;
    pinMode(13,INPUT);attachInterrupt(digitalPinToInterrupt(13),teRise,RISING);
    bus->beginWrite();bus->writeC8D8(0x35,0x00);bus->endWrite();
    panel->fillScreen(0);panel->setBrightness(constrain(brightness,0,255));
    return true;
}

void renderClock(int face,const struct tm &local,bool valid,bool force) {
    const uint32_t started=micros();
    if (face<0 || face>=activeCount || !loadFace(face)) return;
    const Face &f=activeFaces[face];
    memcpy(frame,decoded[0],W*H*2);
    if(f.plateFollow&&valid) {
        const float minutes=local.tm_min+local.tm_sec/60.0f;
        const float angle=(f.plateFollow==1?((local.tm_hour%12)+minutes/60)*30:f.plateFollow==2?minutes*6:local.tm_sec*6)*float(M_PI)/180;
        const float cs=cosf(angle),sn=sinf(angle);
        for(int y=0;y<H;++y)for(int x=0;x<W;++x){int sx=int(lroundf((x-CX)*cs+(y-CY)*sn+CX)),sy=int(lroundf(-(x-CX)*sn+(y-CY)*cs+CY));frame[y*W+x]=sx>=0&&sy>=0&&sx<W&&sy<H?reinterpret_cast<uint16_t*>(decoded[0])[sy*W+sx]:0;}
    }
    if (!f.textOverHands) drawBanners(f,local,valid);
    if (valid) {
        const float minutes=local.tm_min+local.tm_sec/60.0f;
        const float angles[5]={((local.tm_hour%12)+minutes/60.0f)*30,minutes*6,local.tm_sec*6.0f,0,0};
        for (int k:f.order) if (k<3) drawSprite(f.shadows[k],decoded[6+k],angles[k]);
        for (int k:f.order) drawSprite(f.hands[k],decoded[1+k],angles[k]);
    }
    if (f.textOverHands) drawBanners(f,local,valid);
    drawSprite(f.overlay,decoded[9],0);
    flush(force);lastUs=micros()-started;
}

void rendererBrightness(int value) { if(panel) panel->setBrightness(constrain(value,0,255)); }
uint32_t rendererFrames() {return frames;}
uint32_t rendererLastUs() {return lastUs;}
uint32_t rendererPushedPixels() {return pushed;}
const uint16_t *rendererCapture() {return previous;}
void rendererSetFaces(const Face *items,int count) {
    for(auto &p:decoded){free(p);p=nullptr;}decodedFace=-1;
    activeFaces=items;activeCount=count;
}
bool rendererPrepare(int face){return face>=0&&face<activeCount&&loadFace(face);}
void rendererMenu(const char *const *names,int count,int selected,const char *message) {
    for(int i=0;i<W*H;++i)frame[i]=0x0841;
    const Font &font=*faces[0].text[0].font;
    drawText(font,"CLOCK GROUPS",233,80,0xeed5);
    int start=max(0,min(selected-2,count-5));
    for(int i=start;i<min(count,start+5);++i) {
        int y=150+(i-start)*35;
        if(i==selected)for(int yy=y-15;yy<y+17;++yy)for(int x=65;x<401;++x)frame[yy*W+x]=0x31a6;
        char label[40];snprintf(label,sizeof(label),"%.32s",names[i]);drawText(font,label,233,y,i==selected?0xffff:0xad75);
    }
    drawText(font,message?message:"Turn to choose / press to open",233,360,0xeed5);
    drawText(font,"Hold to return",233,390,0xad75);flush(true);
}
