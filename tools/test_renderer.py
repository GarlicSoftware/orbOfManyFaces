"""Run the actual renderer against a strict CO5300 panel model on Windows."""
from pathlib import Path
import os
import subprocess
import tempfile
from PIL import Image,ImageDraw

ROOT=Path(__file__).resolve().parents[1]
vcvars=Path('C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat')
environment=subprocess.check_output(f'cmd.exe /s /c ""{vcvars}" >nul && set"',text=True)
env={key.upper():value for key,value in os.environ.items()}
for line in environment.splitlines():
    if '=' in line and not line.startswith('='):
        key,value=line.split('=',1);env[key.upper()]=value

arduino=r'''
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <algorithm>
#include <initializer_list>
using std::min;using std::max;
#define PROGMEM
#define IRAM_ATTR
#define INPUT 0
#define RISING 0
inline uint32_t fakeUs=0;
inline uint32_t micros(){return fakeUs+=100;}
inline uint32_t millis(){return fakeUs/1000;}
inline void delayMicroseconds(unsigned){}
inline void pinMode(int,int){}
inline int digitalPinToInterrupt(int pin){return pin;}
inline void attachInterrupt(int,void(*)(),int){}
template<class T> T constrain(T x,T low,T high){return min(high,max(low,x));}
'''
gfx=r'''
#pragma once
#include <stdint.h>
#include <cassert>
#include <cstring>
inline uint16_t physicalPanel[466*466]={};
inline unsigned partialWrites=0;
class Arduino_DataBus {
public:void beginWrite(){}void writeC8D8(int,int){}void endWrite(){}
};
class Arduino_ESP32QSPI:public Arduino_DataBus {
public:template<class... T> Arduino_ESP32QSPI(T...){}
};
class Arduino_CO5300 {
public:
 template<class... T> Arduino_CO5300(T...){}
 bool begin(int){return true;}
 void setBrightness(int){}
 void fillScreen(uint16_t color){for(auto &p:physicalPanel)p=color;}
 void draw16bitRGBBitmap(int x,int y,uint16_t *pixels,int w,int h){
   assert(x>=0&&y>=0&&x+w<=466&&y+h<=466);
   // These hardware requirements were absent from the first firmware's flush.
   assert((x%2)==0&&(y%2)==0&&(w%2)==0&&(h%2)==0);
   if(w<466||h<466)++partialWrites;
   for(int row=0;row<h;++row)
     memcpy(physicalPanel+(y+row)*466+x,pixels+row*w,w*2);
 }
};
'''
heap=r'''
#pragma once
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_INTERNAL 2
#define MALLOC_CAP_DMA 4
inline void *heap_caps_malloc(size_t bytes,int){return malloc(bytes);}
'''
test=r'''
#define _USE_MATH_DEFINES
#include "Arduino.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <vector>
#include "Arduino_GFX_Library.h"
#include "renderer.h"
#include "assets.h"
int main(){
 setvbuf(stdout,nullptr,_IONBF,0);
 _set_error_mode(_OUT_TO_STDERR);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
 assert(rendererBegin(200,0));
 std::vector<uint16_t> incremental(466*466);
 unsigned frames=0;
 for(int face=0;face<FACE_COUNT;++face){
   tm start={};start.tm_year=126;start.tm_mon=9;start.tm_mday=17;
   start.tm_hour=23;start.tm_min=59;
   time_t epoch=_mkgmtime(&start);
   for(int tick=0;tick<125;++tick){
     time_t stamp=epoch+tick;tm local={};gmtime_s(&local,&stamp);
     renderClock(face,local,true,tick==0);
     memcpy(incremental.data(),physicalPanel,466*466*2);
     assert(memcmp(physicalPanel,rendererCapture(),466*466*2)==0);
     // Independent fresh full-frame delivery must agree with each partial result.
     renderClock(face,local,true,true);
     assert(memcmp(incremental.data(),physicalPanel,466*466*2)==0);
     ++frames;
   }
 }
 assert(partialWrites>1000);
 tm preview={};preview.tm_year=126;preview.tm_mon=9;preview.tm_mday=17;preview.tm_wday=6;
 preview.tm_hour=10;preview.tm_min=10;preview.tm_sec=30;
 for(int face=0;face<FACE_COUNT;++face){
   renderClock(face,preview,true,true);
   char name[32];std::sprintf(name,"face-%02d.ppm",face);FILE *file=std::fopen(name,"wb");assert(file);
   std::fprintf(file,"P6\n466 466\n255\n");
   for(uint16_t pixel:physicalPanel){
     unsigned char rgb[3]={static_cast<unsigned char>((pixel>>11)*255/31),static_cast<unsigned char>(((pixel>>5)&63)*255/63),static_cast<unsigned char>((pixel&31)*255/31)};
     std::fwrite(rgb,1,3,file);
   }
   std::fclose(file);
 }
 for(int angle:{0,90,37}){
  std::printf("Testing star orientation %d\n",angle);
  assert(rendererBegin(200,angle));tm local={};local.tm_year=126;local.tm_mon=9;local.tm_mday=22;local.tm_wday=4;local.tm_hour=10;local.tm_min=9;local.tm_sec=13;rendererFavorite(0,1,false);renderClock(0,local,true,true);
  std::vector<uint16_t> clean(physicalPanel,physicalPanel+466*466);unsigned before=rendererPushedPixels();
  for(int style:{1,-1,2}){for(int opacity:{255,220,170,120,70,20,0}){rendererFavorite(opacity,style);assert(!memcmp(physicalPanel,rendererCapture(),466*466*2));}assert(!memcmp(physicalPanel,clean.data(),466*466*2));}
  assert(rendererPushedPixels()-before<60000); // tiny rectangles, not full dials
 }
 const char *labels[]={"The Favorites Have Formed a Committee and Asked for Even More Favorites"};
 std::puts("Testing menu scrolling");rendererMenu(labels,1,0,nullptr);assert(rendererMenuScrolling());std::vector<uint16_t> initial(physicalPanel,physicalPanel+466*466);fakeUs+=2000000;rendererMenu(labels,1,0,nullptr);assert(memcmp(initial.data(),physicalPanel,466*466*2));assert(!memcmp(physicalPanel,rendererCapture(),466*466*2));
 std::puts("PASS: favorite-star fade restores pixels exactly at 0/90/37 degrees; small aligned writes; long names scroll");
 std::printf("PASS: %u tick frames on all faces, all panel windows aligned, partial/full delivery identical across midnight; %u partial writes\n",frames,partialWrites);
}
'''

def run_renderer(work,renderer,name):
    compiler=Path(env['VCTOOLSINSTALLDIR'])/'bin/Hostx64/x64/cl.exe'
    result=subprocess.run([str(compiler),'/nologo','/O2','/EHsc','/std:c++17',
        '/D_USE_MATH_DEFINES','/I'+str(work),'/I'+str(ROOT/'include'),
        str(renderer),str(ROOT/'tools/generated/assets_host.cpp'),str(work/'test.cpp'),'/Fe:'+name+'.exe'],
        cwd=work,env=env,text=True,capture_output=True)
    if result.returncode:raise RuntimeError(result.stdout+result.stderr)
    return subprocess.run([str(work/(name+'.exe'))],cwd=work,text=True,capture_output=True)

with tempfile.TemporaryDirectory(prefix='meridian-panel-test-') as temporary:
    work=Path(temporary)
    for name,text in [('Arduino.h',arduino),('Arduino_GFX_Library.h',gfx),
                      ('esp_heap_caps.h',heap),('test.cpp',test)]:
        (work/name).write_text(text,encoding='utf-8')
    # Prove this test detects the original bug rather than only exercising new code.
    source=(ROOT/'src/renderer.cpp').read_text()
    old=source.replace('            left &= ~1;\n','').replace('            right = min(W-1,right|1);\n','')
    assert old!=source
    (work/'old_renderer.cpp').write_text(old,encoding='utf-8')
    broken=run_renderer(work,work/'old_renderer.cpp','broken')
    assert broken.returncode!=0,'Pre-fix renderer unexpectedly passed panel alignment checks'
    print('Confirmed: original renderer fails the panel-alignment assertion.',flush=True)
    fixed=run_renderer(work,ROOT/'src/renderer.cpp','fixed')
    assert fixed.returncode==0,str(fixed.returncode)+"\n"+fixed.stdout+fixed.stderr
    print(fixed.stdout,flush=True)
    output=ROOT/'previews';output.mkdir(exist_ok=True)
    sheet=Image.new('RGB',(5*260,2*290),'#20232a');draw=ImageDraw.Draw(sheet)
    import json
    names=[entry['name'] for entry in json.loads((ROOT/'face-credits.json').read_text(encoding='utf-8'))]
    for i,path in enumerate(sorted(work.glob('face-*.ppm'))):
        image=Image.open(path);image.save(output/(path.stem+'.png'))
        sheet.paste(image.resize((250,250)),((i%5)*260+5,(i//5)*290+5))
        draw.text(((i%5)*260+7,(i//5)*290+260),names[i],fill='white')
    sheet.save(output/'all-clock-faces.png')
