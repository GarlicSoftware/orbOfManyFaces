"""Compile the firmware's real rotation scheduler and shuffle logic on the host."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = r'''
#include "rotation_logic.h"
#include <cassert>
#include <cstdio>
#include <set>
int main(){
 using namespace rotation;
 for(int i=0;i<intervalCount;++i)for(int j=0;j<intervalCount;++j)for(int f=0;f<2;++f)for(int g=0;g<2;++g){
  Settings s;s.face=i;s.group=j;s.faceShuffle=f;s.groupShuffle=g;
  auto restored=Settings::unpack(s.packed());assert(restored.packed()==s.packed());
 }
 auto invalid=Settings::unpack(0xffffffff);assert(invalid.face==0&&invalid.group==0);
 // Every supported interval aligns from local midnight, including the next day.
 const int64_t midnight=20000ll*86400;
 for(int i=1;i<intervalCount;++i){
  Boundary b;unsigned changes=0;
  for(int64_t t=0;t<=86400;++t){bool due=b.update(midnight+t,midnight+t,intervals[i],true,false);
   assert(due==(t>0&&t%intervals[i]==0));changes+=due;
   assert(!b.update(midnight+t,midnight+t,intervals[i],true,false));
  }assert(changes==86400/intervals[i]);
 }
 Boundary b;assert(!b.update(299,299,300,true,false));assert(b.update(300,300,300,true,false));
 assert(!b.update(300,300,300,true,false)); // once, regardless of render count
 assert(!b.update(301,301,300,true,false)); // a manual swipe has no gate effect
 assert(!b.update(599,599,300,true,false)); // long gap rebases, no catch-up
 assert(b.update(600,600,300,true,false));
 b.reset();assert(!b.update(300,300,300,true,false)); // startup exactly on boundary
 assert(!b.update(0,0,300,false,false));assert(!b.update(600,600,300,true,false));
 assert(!b.update(601,601,0,true,false));assert(!b.update(899,899,300,true,false));
 assert(b.update(900,900,300,true,false));
 // A menu/touch/USB lock defers one step. Multiple intervals never form a queue.
 b.reset();assert(!b.update(299,299,300,true,true));
 for(int64_t t=300;t<=900;++t)assert(!b.update(t,t,300,true,true));
 assert(b.update(901,901,300,true,false));assert(!b.update(902,902,300,true,false));
 // Reconfiguration arms the next boundary, not an immediate switch.
 assert(!b.update(903,903,900,true,false));
 // Forward and backward synchronization jumps are silent; ordinary ticks resume.
 assert(!b.update(5000,5000,300,true,false));assert(!b.update(100,100,300,true,false));
 for(int t=101;t<300;++t)assert(!b.update(t,t,300,true,false));
 assert(b.update(300,300,300,true,false));
 // Fall-back repeats an hour, not the rotations already shown in that hour.
 b.reset();assert(!b.update(7199,7199,900,true,false));
 assert(!b.update(7200,3600,900,true,false));
 for(int t=7201;t<=10799;++t){if(t==8000)b.synchronize();assert(!b.update(t,t-3600,900,true,false));}
 assert(b.update(10800,7200,900,true,false));
 // Spring forward skips missing slots without catch-up.
 b.reset();assert(!b.update(7199,7199,900,true,false));
 assert(!b.update(7200,10800,900,true,false));
 for(int t=7201;t<8100;++t)assert(!b.update(t,t+3600,900,true,false));
 assert(b.update(8100,11700,900,true,false));
 b.synchronize();assert(!b.update(8101,11701,900,true,false));
 b.synchronize();assert(!b.update(8110,12600,900,true,false)); // small NTP jump across a boundary
 assert(!b.update(8111,12601,900,true,false));
 uint32_t seed=1234;auto rng=[&](){seed=seed*1664525u+1013904223u;return seed;};
 for(int count:{0,1,2,3,10,33,80}){
  Shuffle sh;sh.reset(count,0,rng);if(count<2){assert(sh.next(0,rng)==0);continue;}
  std::set<int> seen={0};int current=0;
  for(int n=1;n<count;++n){int next=sh.next(current,rng);assert(next>=0&&next<count&&next!=current);assert(seen.insert(next).second);current=next;}
  for(int cycle=0;cycle<50;++cycle){seen.clear();
   for(int n=0;n<count;++n){int next=sh.next(current,rng);assert(next!=current);assert(seen.insert(next).second);current=next;}
  }
  sh.reset(count,0,rng);sh.visit(1);seen={0,1};current=1;
  for(int n=2;n<count;++n){current=sh.next(current,rng);assert(seen.insert(current).second);}
  auto capacity=sh.bag.capacity();for(int n=0;n<1000;++n)current=sh.next(current,rng);assert(sh.bag.capacity()==capacity);
 }
 std::puts("PASS: all intervals, settings, boundaries, startup/sync/DST, deferred UI, shuffle cycles/manual visits and allocation reuse");
}
'''

def main():
    env = os.environ.copy()
    with tempfile.TemporaryDirectory(prefix='orb-rotation-test-') as temporary:
        work = Path(temporary)
        (work/'test.cpp').write_text(SOURCE, encoding='utf-8')
        if os.name == 'nt':
            vcvars = Path('C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat')
            output = subprocess.check_output(f'cmd.exe /s /c ""{vcvars}" >nul && set"', text=True)
            env = {k.upper(): v for k, v in env.items()}
            for line in output.splitlines():
                if '=' in line and not line.startswith('='):
                    k, v = line.split('=',1);env[k.upper()] = v
            compiler = Path(env['VCTOOLSINSTALLDIR'])/'bin/Hostx64/x64/cl.exe'
            command = [str(compiler),'/nologo','/EHsc','/std:c++17','/I'+str(ROOT/'include'),'test.cpp','/Fe:test.exe']
            executable = work/'test.exe'
        else:
            command = [shutil.which('g++') or 'c++','-std=c++17','-I'+str(ROOT/'include'),'test.cpp','-o','test']
            executable = work/'test'
        subprocess.run(command,cwd=work,env=env,check=True)
        subprocess.run([str(executable)],check=True)

if __name__ == '__main__':
    main()
