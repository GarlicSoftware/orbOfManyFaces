# SPDX-License-Identifier: GPL-2.0-or-later
"""Build the self-contained Windows x64 flasher, including verified public payload."""
from pathlib import Path
import argparse
import hashlib
import importlib.metadata
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import uuid
import zipfile
import zlib

ROOT=Path(__file__).resolve().parents[1]


def icon(path):
    size=64;raw=bytearray()
    for y in range(size):
        raw.append(0)
        for x in range(size):
            dx,dy=x-31.5,y-31.5;r=(dx*dx+dy*dy)**.5
            color=(15,29,43,255) if r<30 else (0,0,0,0)
            if 27<r<30:color=(198,163,83,255)
            if abs(dx)<1.8 and -20<dy<2:color=(235,229,206,255)
            if -1<dx<18 and abs(dy-dx*.6)<1.7:color=(235,229,206,255)
            if r<3:color=(198,163,83,255)
            raw.extend(color)
    def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
    png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',size,size,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(bytes(raw)))+chunk(b'IEND',b'')
    path.write_bytes(struct.pack('<HHH',0,1,1)+struct.pack('<BBBBHHII',size,size,0,0,1,32,len(png),22)+png)


def copy_payload(source,destination):
    import install
    install.ROOT=source;manifest=install.validate()
    credits=json.loads((source/'ARTWORK_CREDITS.json').read_text(encoding='utf-8'))
    if len(credits)!=42 or any(c['license'] not in ('MIT','CC0 1.0') for c in credits):raise ValueError('Only the 42-face public collection may be bundled')
    destination.mkdir(parents=True)
    files=['manifest.json','ARTWORK_CREDITS.json','NOTICE.md','LICENSE']
    files += [i['file'] for i in manifest['images']+manifest['sd_packs']]
    for name in files:
        target=destination/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(source/name,target)
    return manifest


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--payload-dir',type=Path)
    args=parser.parse_args()
    if os.name!='nt' or sys.version_info[:2]!=(3,12) or struct.calcsize('P')!=8:raise SystemExit('Build with 64-bit Python 3.12 on Windows.')
    for package,version in [('esptool','4.8.1'),('pyserial','3.5'),('pyinstaller','6.22.3')]:
        if importlib.metadata.version(package)!=version:raise SystemExit(f'Build requires {package}=={version}')
    sys.path.insert(0,str(ROOT))
    work=ROOT/'.flasher-build'/uuid.uuid4().hex[:12];work.mkdir(parents=True)
    if args.payload_dir:
        source=args.payload_dir.resolve()
    else:
        subprocess.run([sys.executable,str(ROOT/'tools/package_release.py')],check=True)
        version=re.search(r'Clock ([0-9.]+)',(ROOT/'src/main.cpp').read_text()).group(1)
        source=work/'release';source.mkdir()
        with zipfile.ZipFile(ROOT/'releases'/f'orbOfManyFaces-{version}.zip') as archive:
            # The archive is built locally from the public allowlist above.
            archive.extractall(source)
        source=next(source.iterdir())
    payload=work/'payload';manifest=copy_payload(source,payload)
    version=re.search(r'(\d+\.\d+\.\d+)',manifest['release']).group(1)
    licenses=work/'licenses';licenses.mkdir()
    shutil.copyfile(ROOT/'licenses/Flasher-GPL-2.0.txt',licenses/'Flasher-GPL-2.0.txt')
    esptool_dist=importlib.metadata.distribution('esptool')
    shutil.copyfile(esptool_dist.locate_file('esptool-4.8.1.dist-info/licenses/LICENSE'),licenses/'ESPTOOL-GPL-2.0.txt')
    python_license=Path(sys.base_prefix)/'LICENSE.txt'
    if python_license.exists():shutil.copyfile(python_license,licenses/'PYTHON-LICENSE.txt')
    for dist in importlib.metadata.distributions():
        for name in dist.files or ():
            if any(t in str(name).lower() for t in ('license','copying')) and dist.locate_file(name).is_file():
                target=licenses/dist.metadata['Name']/str(name).replace('..','_').replace(':','_')
                target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(dist.locate_file(name),target)
    icon(work/'orb.ico')
    command=[sys.executable,'-m','PyInstaller','--noconfirm','--onefile','--windowed',
             '--name','OrbOfManyFaces-Flasher','--paths',str(ROOT),'--collect-all','esptool',
             '--add-data',str(payload)+';payload','--add-data',str(licenses)+';licenses',
             '--add-data',str(work/'orb.ico')+';.', '--icon',str(work/'orb.ico'),
             '--distpath',str(work/'dist'),'--workpath',str(work/'build'),'--specpath',str(work),str(ROOT/'flasher.py')]
    subprocess.run(command,check=True,cwd=ROOT)
    executable=work/'dist/OrbOfManyFaces-Flasher.exe'
    # Test the frozen executable itself, not the source interpreter.
    job=work/'smoke';completed=subprocess.run([str(executable),'--worker','--operation','verify','--job-dir',str(job)],check=True)
    receipt=json.loads((job/'result.json').read_text())
    if not receipt.get('verified'):raise RuntimeError('Frozen payload verification failed')
    stage=work/f'OrbOfManyFaces-Flasher-{version}';stage.mkdir();shutil.copyfile(executable,stage/executable.name)
    shutil.copytree(licenses,stage/'licenses')
    (stage/'README-WINDOWS.txt').write_text(
        'ORB OF MANY FACES - WINDOWS FLASHER\n\n'
        '1. Extract this ZIP, then double-click OrbOfManyFaces-Flasher.exe.\n'
        '2. Connect the Orb by USB data cable, keeping its SD card installed.\n'
        '3. Select its port if needed and click Install / Update all 42 faces.\n'
        '4. Keep USB connected through backup, flashing and SD verification.\n'
        '5. Click Wi-Fi & time setup and follow the instructions on the Orb.\n\n'
        'No Python installation, pip, terminal, administrator access or stock firmware is required.\n'
        'Use Repair SD libraries to retry SD transfers without flashing.\n'
        'Backups/logs: %LOCALAPPDATA%\\OrbOfManyFaces. Backups contain private device settings.\n'
        'This is an unsigned app; Windows may ask you to confirm its publisher.\n'
        'Download only from https://github.com/GarlicSoftware/orbOfManyFaces/releases\n\n'
        'The Windows flasher is GPL-2.0-or-later, including bundled esptool 4.8.1.\n'
        'Firmware remains MIT; public artwork retains its MIT/CC0 attribution.\n'
        'Full flasher source and the unmodified esptool Python source are included in SOURCE.zip.\n'
        'To rebuild: extract SOURCE.zip, use Windows Python 3.12, install requirements-flasher-build.txt,\n'
        'then run python tools/build_windows_flasher.py --payload-dir payload.\n',encoding='utf-8')
    source_zip=stage/'SOURCE.zip'
    with zipfile.ZipFile(source_zip,'w',zipfile.ZIP_DEFLATED) as archive:
        for name in ('flasher.py','install.py','requirements-flasher-build.txt','requirements-install.txt','LICENSE','NOTICE.md','platformio.ini','partitions.csv'):
            archive.write(ROOT/name,name)
        for folder in ('tools','src','include','tests'):
            for path in (ROOT/folder).rglob('*'):
                if path.is_file() and path.suffix in ('.py','.cpp','.h') and '__pycache__' not in path.parts and 'generated' not in path.parts:archive.write(path,path.relative_to(ROOT))
        for folder,prefix in ((payload,'payload'),(licenses,'licenses')):
            for path in folder.rglob('*'):
                if path.is_file():archive.write(path,Path(prefix)/path.relative_to(folder))
        import esptool
        esptool_source=Path(esptool.__file__).parent
        for path in esptool_source.rglob('*'):
            if path.is_file() and '__pycache__' not in path.parts:archive.write(path,Path('vendor/esptool')/path.relative_to(esptool_source))
        freeze=subprocess.check_output([sys.executable,'-m','pip','freeze'],text=True)
        archive.writestr('BUILD-DEPENDENCIES.txt',freeze)
    out=ROOT/'releases'/f'orbOfManyFaces-Flasher-{version}-windows-x64.zip';out.parent.mkdir(exist_ok=True)
    with zipfile.ZipFile(out,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as archive:
        for path in stage.rglob('*'):
            if path.is_file():archive.write(path,Path(stage.name)/path.relative_to(stage))
    with zipfile.ZipFile(out) as archive:
        if archive.testzip():raise RuntimeError('Windows ZIP failed validation')
    out.with_suffix('.zip.sha256').write_text(hashlib.sha256(out.read_bytes()).hexdigest()+'  '+out.name+'\n')
    local_exe=ROOT/'releases'/f'OrbOfManyFaces-Flasher-{version}.exe'
    shutil.copyfile(executable,local_exe)
    local_exe.with_suffix('.exe.sha256').write_text(hashlib.sha256(local_exe.read_bytes()).hexdigest()+'  '+local_exe.name+'\n')
    print('Windows flasher:',out,'bytes:',out.stat().st_size)
    print('Executable:',executable)


if __name__=='__main__':main()
