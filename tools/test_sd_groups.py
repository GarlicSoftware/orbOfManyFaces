"""Exercise the exact firmware pack parser and renderer on every generated face."""
from pathlib import Path
import json,os,struct,subprocess,tempfile,zlib
ROOT=Path(__file__).resolve().parents[1]
# Reuse only the host platform stubs/compiler setup, without rerunning its suite.
scope={"__file__":str(ROOT/'tools/test_renderer.py')}
exec((ROOT/'tools/test_renderer.py').read_text().split("with tempfile.TemporaryDirectory(prefix='meridian-panel-test-')")[0],scope)
source=r'''
#include "group_format.h"
#include "renderer.h"
#include "Arduino_GFX_Library.h"
#include <fstream>
#include <cassert>
#include <cstdio>
std::vector<uint8_t> read(const char *name){std::ifstream f(name,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char **argv){
 auto data=read(argv[1]);
 auto group=clockpack::parse(data.data(),data.size());
 if(argc==2){
  for(auto &f:group->faces){
   Packed *images[10]={&f.plate,&f.hands[0].image,&f.hands[1].image,&f.hands[2].image,&f.hands[3].image,&f.hands[4].image,&f.shadows[0].image,&f.shadows[1].image,&f.shadows[2].image,&f.overlay.image};
   for(auto p:images)std::printf("%zu %u %u\n",size_t(p->data-data.data()),p->size,p->rawSize);
  }
  auto broken=data;broken.back()^=1;bool rejected=false;try{clockpack::parse(broken.data(),broken.size());}catch(...){rejected=true;}assert(rejected);
  for(size_t n:{size_t(0),size_t(23),data.size()-1}){rejected=false;try{clockpack::parse(data.data(),n);}catch(...){rejected=true;}assert(rejected);}
  // Recompute CRC after tampering so semantic checks, not just CRC, reject it.
  auto fixCrc=[](std::vector<uint8_t> &d){uint32_t c=clockpack::crc32(d.data()+24,d.size()-24);for(int i=0;i<4;++i)d[16+i]=uint8_t(c>>(i*8));};
  auto bad=data;size_t offset=size_t(group->faces[0].plate.data-data.data());bad[offset-1]^=1;fixCrc(bad);rejected=false;try{clockpack::parse(bad.data(),bad.size());}catch(...){rejected=true;}assert(rejected);
  bad=data;bad[20]^=1;rejected=false;try{clockpack::parse(bad.data(),bad.size());}catch(...){rejected=true;}assert(rejected);
  return 0;
 }
 auto pixels=read(argv[2]);clockpack::Reader raw{pixels.data(),pixels.data()+pixels.size()};
 for(auto &f:group->faces){
  Packed *images[10]={&f.plate,&f.hands[0].image,&f.hands[1].image,&f.hands[2].image,&f.hands[3].image,&f.hands[4].image,&f.shadows[0].image,&f.shadows[1].image,&f.shadows[2].image,&f.overlay.image};
  for(auto p:images){p->size=p->rawSize;p->data=raw.take(p->rawSize);}
 }
 assert(rendererBegin(200,0));rendererSetFaces(group->faces.data(),group->faces.size());
 tm t={};t.tm_year=126;t.tm_mon=9;t.tm_mday=20;t.tm_wday=2;t.tm_hour=10;t.tm_min=10;
 std::vector<uint16_t> partial(466*466);
 for(int i=0;i<int(group->faces.size());++i){
  assert(rendererPrepare(i));
  for(int tick:{0,1,30,59}){
   t.tm_sec=tick;renderClock(i,t,true,tick==0);memcpy(partial.data(),physicalPanel,466*466*2);
   renderClock(i,t,true,true);assert(!memcmp(partial.data(),physicalPanel,466*466*2));
  }
 }
 const char *names[]={"Built-in clocks","Library 01","Library 02"};rendererMenu(names,3,2,nullptr);rendererMenu(nullptr,0,0,"Loading group...");
 std::printf("PASS: %zu SD faces; rotated plates, text effects, partial/full redraw and menu alignment\n",group->faces.size());
}
'''
def main():
    with tempfile.TemporaryDirectory(prefix='orb-groups-test-') as tmp:
        work=Path(tmp)
        for name,text in [('Arduino.h',scope['arduino']),('Arduino_GFX_Library.h',scope['gfx']),('esp_heap_caps.h',scope['heap']),('test.cpp',source)]:
            (work/name).write_text(text)
        compiler=Path(scope['env']['VCTOOLSINSTALLDIR'])/'bin/Hostx64/x64/cl.exe'
        subprocess.run([str(compiler),'/nologo','/O2','/EHsc','/std:c++17','/D_USE_MATH_DEFINES','/I'+str(work),'/I'+str(ROOT/'include'),str(ROOT/'src/renderer.cpp'),str(ROOT/'tools/generated/assets_host.cpp'),str(work/'test.cpp'),'/Fe:groups.exe'],cwd=work,env=scope['env'],check=True)
        for group in json.loads((ROOT/'sd-groups/index.json').read_text()):
            path=ROOT/'sd-groups'/group['file'];data=path.read_bytes()
            result=subprocess.check_output([str(work/'groups.exe'),str(path)],text=True)
            pixels=bytearray()
            for line in result.splitlines():
                offset,size,raw=map(int,line.split());decoded=zlib.decompress(data[offset:offset+size]) if raw else b'';assert len(decoded)==raw;pixels.extend(decoded)
            raw_path=work/'pixels.bin';raw_path.write_bytes(pixels)
            subprocess.run([str(work/'groups.exe'),str(path),str(raw_path)],check=True)
        print('PASS: every group checksum, every compressed image, truncated/corrupted pack rejection',flush=True)
if __name__=='__main__':main()
