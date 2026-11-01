# SPDX-License-Identifier: GPL-2.0-or-later
"""Packaged flasher engine: reuse the protected CLI installer and verified SD uploader."""
from pathlib import Path
import contextlib
import io
import os
import re
import sys

import install
from tools import upload_groups


def payload_root():
    return Path(sys._MEIPASS)/'payload' if getattr(sys,'frozen',False) else Path(__file__).resolve().parents[1]


def user_data_root():
    base=Path(os.environ['LOCALAPPDATA']) if os.environ.get('LOCALAPPDATA') else Path.home()
    return base/'OrbOfManyFaces'


def ports():
    from serial.tools import list_ports
    items=[]
    for port in list_ports.comports():
        items.append({'port':port.device,'label':f'{port.device} — {port.description}',
                      'recommended':port.vid==0x303A,'serial':port.serial_number})
    return sorted(items,key=lambda p:(not p['recommended'],p['port']))


def run_esptool(command,check=True):
    # Frozen Python cannot spawn `sys.executable -m esptool`: that would launch
    # the GUI executable again. Call the pinned, bundled module in this worker.
    if len(command)<4 or command[1:3]!=['-m','esptool']:
        raise ValueError('Unexpected flashing command')
    import esptool
    if esptool.__version__!='4.8.1':
        raise RuntimeError('This flasher requires its bundled esptool 4.8.1.')
    try:
        esptool.main(command[3:])
    except SystemExit as error:
        if error.code not in (None,0):raise RuntimeError(f'Flashing tool exited with code {error.code}') from error


class Tee(io.TextIOBase):
    def __init__(self,*streams):self.streams=streams
    def write(self,text):
        for stream in self.streams:stream.write(text)
        return len(text)
    def flush(self):
        for stream in self.streams:stream.flush()


def probe(port,runner=run_esptool):
    output=io.StringIO()
    with contextlib.redirect_stdout(Tee(sys.stdout,output)),contextlib.redirect_stderr(Tee(sys.stderr,output)):
        runner([sys.executable,'-m','esptool','--chip','esp32s3','--port',port,
                '--baud','921600','--before','default_reset','--after','hard_reset','flash_id'],check=True)
    text=output.getvalue()
    if not re.search(r'Detected flash size:\s*16\s*MB\b',text,re.IGNORECASE):
        raise RuntimeError('A 16 MB ESP32-S3 Orb was not detected. No firmware was written.')
    return {'chip':'esp32s3','flash_bytes':0x1000000}


def summary(results):
    verified={r['file']:r for r in results if r.get('status') in ('installed','verified existing')}
    return {'packs':results,'available_faces':8+sum(r['faces'] for r in verified.values()),
            'sd_complete':len(verified)==4}


def execute(operation,port,root,data_dir,phase=lambda name:None,runner=run_esptool,uploader=None,connector=None):
    root=Path(root).resolve();install.ROOT=root
    manifest=install.validate()  # Local hashes/layout are checked before USB use.
    result={'ok':True,'release':manifest['release'],'firmware_installed':False,
            'available_faces':8,'sd_complete':False,'packs':[]}
    if operation=='verify':return dict(result,verified=True)
    if operation=='probe':
        phase('connect');return dict(result,device=probe(port,runner))
    if operation=='setup':
        phase('setup');clock=(connector or upload_groups.connect)(port)
        try:
            try:response=clock.command('setup start')
            except RuntimeError as error:
                if 'unknown command' in str(error):raise RuntimeError('Install the bundled firmware first, then open Wi-Fi & time setup.') from error
                raise
        finally:clock.serial.close()
        return dict(result,setup_started=response.get('active',False))
    if operation not in ('install','repair','firmware-only'):raise ValueError('Unknown flasher operation')
    if operation in ('install','firmware-only'):
        phase('connect');probe(port,runner)
        result.update(install.main(['--port',port,'--skip-sd'],runner=runner,
                                   backup_root=data_dir,on_phase=phase))
        # Persist an intermediate receipt so a later optional upload failure
        # cannot make a successful firmware flash look unsuccessful.
        phase('firmware-ready')
    if operation!='firmware-only':
        phase('sd')
        try:
            results=(uploader or upload_groups.install_packs)(port,manifest['sd_packs'],root)
            result.update(summary(results))
        except (OSError,ValueError,RuntimeError,TimeoutError,ImportError) as error:
            result['sd_error']=str(error)
    phase('complete')
    return result
