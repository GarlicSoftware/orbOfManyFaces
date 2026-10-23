"""Compile and run the firmware's input/time helpers on Windows with MSVC."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
vcvars=Path('C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat')
command=f'cmd.exe /s /c ""{vcvars}" >nul && set"'
environment=subprocess.check_output(command,text=True)
env={key.upper():value for key,value in os.environ.items()}
for line in environment.splitlines():
    if '=' in line and not line.startswith('='):
        key,value=line.split('=',1);env[key.upper()]=value

source=r'''
#include "clock_logic.h"
#include <cassert>
#include <cstdio>
int main() {
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
    assert(wrapFace(0,-1)==1 && wrapFace(1,1)==0);
    assert(wrapFace(0,-100)==0 && wrapFace(1,100)==1);
    assert(wrapFace(9,1,10)==0 && wrapFace(0,-1,10)==9);
    assert(wrapFace(1,23,10)==4 && wrapFace(1,-23,10)==8);
    assert(stepVolume(60,1)==55 && stepVolume(60,-1)==65);
    assert(stepVolume(5,1)==0 && stepVolume(0,-1)==5);
    assert(stepVolume(0,1)==0 && stepVolume(100,-1)==100);
    assert(stepVolume(35,1000000)==0 && stepVolume(35,-1000000)==100);
    Encoder e;e.previous=3;
    for(int i=0;i<100;++i) {e.edge(1);e.edge(3);}
    assert(e.detents==0);
    for(int i:{1,0,2,3}) e.edge(i);
    assert(e.detents==1);
    for(int i:{2,0,1,3}) e.edge(i);
    assert(e.detents==0);
    for(int turn=0;turn<20;++turn) for(int i:{2,0,1,3}) e.edge(i);
    assert(e.detents==-20);
    Swipe swipe;
    assert(swipe.update(true,200,200,1000)==0);
    assert(swipe.update(true,100,205,1300)==0);
    assert(swipe.update(false,0,0,1400)==1);
    assert(swipe.update(false,0,0,1500)==0);
    swipe.update(true,100,200,2000);swipe.update(true,210,200,2300);
    assert(swipe.update(false,0,0,2400)==-1);
    swipe.update(true,100,200,3000);swipe.update(true,164,200,3300);
    assert(swipe.update(false,0,0,3400)==0);
    swipe.update(true,100,200,4000);swipe.update(true,180,280,4300);
    assert(swipe.update(false,0,0,4400)==0);
    swipe.update(true,100,200,5000);swipe.update(true,200,200,7000);
    assert(swipe.update(false,0,0,7100)==0);
    assert(utcEpoch(1970,1,1,0,0,0)==0);
    assert(utcEpoch(2024,2,29,0,0,0)==1709164800);
    assert(utcEpoch(2024,3,1,0,0,0)-utcEpoch(2024,2,28,0,0,0)==172800);
    assert(utcEpoch(2026,10,18,0,0,0)-utcEpoch(2026,10,17,23,59,59)==1);
    ClockButton button;
    assert(button.update(true,1000)==ClockButton::NONE);
    assert(button.update(false,1010)==ClockButton::NONE); // contact bounce
    assert(button.update(false,1040)==ClockButton::NONE);
    button.update(true,2000);button.update(true,2030);
    button.update(false,2200);
    assert(button.update(false,2230)==ClockButton::SHORT_PRESS);
    button.update(true,3000);button.update(true,3030);
    assert(button.update(true,4029)==ClockButton::NONE);
    assert(button.update(true,4030)==ClockButton::LONG_PRESS);
    assert(button.update(true,8000)==ClockButton::NONE); // holding cannot repeat
    button.update(false,8100);
    assert(button.update(false,8130)==ClockButton::NONE); // release cannot also switch face
    button.update(true,9000);button.update(true,9030);
    assert(button.update(false,10030)==ClockButton::LONG_PRESS); // skipped deadline sample
    assert(button.update(false,10060)==ClockButton::NONE);
    TickGate ticks;
    assert(!ticks.update(0,false,true));
    assert(!ticks.update(100,true,true)); // no boot tick
    assert(!ticks.update(100,true,true)); // no duplicate on face redraw
    assert(ticks.update(101,true,true));
    assert(!ticks.update(102,true,false)); // muted seconds still advance the gate
    assert(!ticks.update(102,true,true)); // enabling midway through a second
    assert(ticks.update(103,true,true));
    assert(ticks.update(200,true,true)); // one tick, not a catch-up burst
    assert(!ticks.update(200,true,true));
    HourlyChimeGate chime;
    assert(!chime.update(3599,59,59,true,true));
    assert(chime.update(3600,0,0,true,true));
    assert(!chime.update(3600,0,0,true,true));
    assert(!chime.update(3601,0,1,true,true));
    assert(!chime.update(7199,59,59,true,true));
    assert(chime.update(7201,0,1,true,true)); // renderer missed second zero
    assert(!chime.update(7202,0,2,true,true));
    assert(!chime.update(10800,0,0,true,true)); // NTP jump, no catch-up chime
    assert(!chime.update(10799,59,59,true,true));
    assert(!chime.update(10800,0,0,true,false)); // volume zero
    assert(!chime.update(10801,0,1,true,true)); // unmute does not replay
    assert(!chime.update(0,0,0,false,true));
    assert(!chime.update(14400,0,0,true,true)); // startup / recovered time
    std::puts("PASS: hourly chime, grace window, no duplicates/startup/catch-up, mute");
    std::puts("PASS: encoder bounce, reversals, face wrap, swipe direction/limits/one-shot, UTC leap day and midnight");
    std::puts("PASS: short/long press separation, hold one-shot, button bounce, tick cadence, mute and time jumps");
}
'''
# The initializer-list syntax in the harness needs its standard declaration.
source='#include <initializer_list>\n'+source
with tempfile.TemporaryDirectory(prefix='meridian-clock-test-') as temporary:
    work=Path(temporary);(work/'test.cpp').write_text(source,encoding='utf-8')
    compiler=Path(env['VCTOOLSINSTALLDIR'])/'bin/Hostx64/x64/cl.exe'
    subprocess.run([str(compiler),'/nologo','/EHsc','/std:c++17','/I'+str(ROOT/'include'),
                    'test.cpp','/Fe:test.exe'],cwd=work,env=env,check=True)
    subprocess.run([str(work/'test.exe')],check=True)
