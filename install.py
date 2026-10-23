"""Install the packaged clock firmware, backing up the recipient's Orb first."""
from pathlib import Path
import argparse
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
import subprocess
import sys

ROOT=Path(__file__).resolve().parent

def validate():
    manifest=json.loads((ROOT/'manifest.json').read_text(encoding='utf-8'))
    if manifest.get('flash_bytes')!=0x1000000 or manifest.get('chip')!='esp32s3' or manifest.get('contains_nvs') is not False:
        raise ValueError('Unexpected device layout')
    allowed={'bootloader.bin':(0,0x8000),'partitions.bin':(0x8000,0x9000),'firmware.bin':(0x10000,0x650000)}
    if len(manifest['images'])!=3 or {i['file'] for i in manifest['images']}!=set(allowed):
        raise ValueError('Unexpected firmware image set')
    regions=[]
    for image in manifest['images']:
        path=ROOT/image['file']
        if path.parent!=ROOT or not path.is_file():raise ValueError('Missing image: '+image['file'])
        data=path.read_bytes()
        if len(data)!=image['bytes'] or hashlib.sha256(data).hexdigest()!=image['sha256']:
            raise ValueError('Image verification failed: '+image['file'])
        start=int(image['address'],0);end=start+len(data)
        if (start,image['limit'])!=allowed[image['file']]:raise ValueError('Unexpected image region')
        # Flash erase operates in 4096-byte sectors. Include the full erase
        # range when checking that recipient NVS/settings remain untouched.
        erased_start=start&~4095;erased_end=(end+4095)&~4095
        if erased_start<0xE000 and erased_end>0x9000:raise ValueError('Image would overlap NVS: '+image['file'])
        if end>image['limit']:raise ValueError('Image exceeds its permitted region: '+image['file'])
        regions.append((erased_start,erased_end))
    for (_,end),(start,_) in zip(sorted(regions),sorted(regions)[1:]):
        if end>start:raise ValueError('Overlapping image erase regions')
    packs=manifest.get('sd_packs',[])
    if len(packs)!=4 or sum(i.get('face_count',0) for i in packs)!=34 or len({i['file'] for i in packs})!=4:
        raise ValueError('Unexpected SD library set')
    for item in packs:
        path=ROOT/item['file']
        if not path.resolve().is_relative_to(ROOT.resolve()) or not path.is_file():
            raise ValueError('Invalid or missing SD pack')
        if path.stat().st_size!=item['bytes'] or hashlib.sha256(path.read_bytes()).hexdigest()!=item['sha256']:
            raise ValueError('SD pack verification failed: '+item['file'])
    return manifest

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',help='The recipient Orb serial port, e.g. COM7')
    parser.add_argument('--check',action='store_true',help='Only verify packaged images; do not access hardware')
    parser.add_argument('--dry-run',action='store_true',help='Print commands; do not access hardware')
    mode=parser.add_mutually_exclusive_group()
    mode.add_argument('--sd-only',action='store_true',help='Repair/install SD packs without flashing or changing settings')
    mode.add_argument('--skip-sd',action='store_true',help='Install firmware only')
    args=parser.parse_args()
    manifest=validate()
    print('Verified:',manifest['release'])
    print('Hardware:',manifest['board'])
    for image in manifest['images']:print(' ',image['address'],image['file'],image['bytes'],'bytes')
    if args.check:return
    if not args.port:parser.error('--port is required unless using --check')
    if not args.dry_run and not args.skip_sd and importlib.util.find_spec('serial') is None:
        parser.error('Install dependencies first: python -m pip install -r requirements-install.txt')
    if args.sd_only:
        if args.dry_run:
            print('DRY RUN: would verify/upload the four SD packs; no hardware access.');return
        from tools.upload_groups import install_packs
        results=install_packs(args.port,manifest['sd_packs'],ROOT)
        if len(results)!=4 or any(r['status']=='failed' for r in results):
            raise RuntimeError('Some optional SD packs remain unavailable; see retry instructions above.')
        return
    common=[sys.executable,'-m','esptool','--chip','esp32s3','--port',args.port,'--baud','921600','--before','default_reset','--after','hard_reset']
    stamp='dry-run' if args.dry_run else datetime.now(timezone.utc).strftime('%Y%m%d-%H%M%S-%f')
    backup=Path.cwd()/'orb-backups'/stamp/'orb-before-install.bin'
    read=common+['read_flash','0x0',hex(manifest['flash_bytes']),str(backup)]
    write=common+['write_flash']
    for image in manifest['images']:write.extend([image['address'],str(ROOT/image['file'])])
    if args.dry_run:
        print('DRY RUN: no hardware access and no backup file created.')
        for cmd in (read,write):print(subprocess.list2cmdline(cmd))
        if not args.skip_sd:print('Then wait for reboot and verify/upload all four SD packs.')
        return
    if importlib.util.find_spec('esptool') is None:
        parser.error('Install the flasher first: python -m pip install esptool==4.8.1 pyserial==3.5')
    backup.parent.mkdir(parents=True,exist_ok=False)
    print('Saving the recipient device backup:',backup,flush=True)
    subprocess.run(read,check=True)
    if not backup.is_file() or backup.stat().st_size!=manifest['flash_bytes']:
        raise RuntimeError('Backup is incomplete; firmware has not been written.')
    digest=hashlib.sha256(backup.read_bytes()).hexdigest()
    backup.with_suffix('.sha256').write_text(digest+'  '+backup.name+'\n',encoding='utf-8')
    print('Backup complete. Writing clock firmware; NVS/settings are preserved.',flush=True)
    subprocess.run(write,check=True)
    print('Firmware installed successfully. Backup:',backup,flush=True)
    if args.skip_sd:
        print('8 built-in faces available. SD installation skipped.')
    else:
        try:
            from tools.upload_groups import install_packs
            install_packs(args.port,manifest['sd_packs'],ROOT)
        except (OSError,RuntimeError,TimeoutError,ImportError) as error:
            print('Firmware installation succeeded; optional SD upload failed:',error)
            print('8 built-in faces available; SD face count not verified.')
            print(f'Retry: python install.py --port {args.port} --sd-only')
    print('Swipe for faces; turn for volume; hold 1.5 seconds, turn and press for libraries.')

if __name__=='__main__':
    try:main()
    except (OSError,ValueError,RuntimeError,subprocess.CalledProcessError) as error:
        sys.exit(str(error))
