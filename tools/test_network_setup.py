"""Compile the real Wi-Fi setup server with controlled Wi-Fi/NVS/HTTP test doubles."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT/'tests/network_host/main.cpp').read_text()

def main():
    env = os.environ.copy()
    with tempfile.TemporaryDirectory(prefix='orb-network-test-') as temporary:
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
            command = [str(compiler),'/nologo','/EHsc','/std:c++17','/I'+str(ROOT/'include'),'/I'+str(ROOT/'tests/network_host'),'test.cpp',str(ROOT/'src/network_setup.cpp'),'/Fe:test.exe']
            executable = work/'test.exe'
        else:
            command = [shutil.which('g++') or 'c++','-std=c++17','-I'+str(ROOT/'include'),'-I'+str(ROOT/'tests/network_host'),'test.cpp',str(ROOT/'src/network_setup.cpp'),'-o','test']
            executable = work/'test'
        subprocess.run(command,cwd=work,env=env,check=True)
        subprocess.run([str(executable)],check=True)

if __name__ == '__main__':
    main()
