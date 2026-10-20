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
    regions=[]
    for image in manifest['images']:
        path=ROOT/image['file']
        if path.parent!=ROOT or not path.is_file():raise ValueError('Missing image: '+image['file'])
        data=path.read_bytes()
        if len(data)!=image['bytes'] or hashlib.sha256(data).hexdigest()!=image['sha256']:
            raise ValueError('Image verification failed: '+image['file'])
        start=int(image['address'],0);end=start+len(data)
        # Flash erase operates in 4096-byte sectors. Include the full erase
        # range when checking that recipient NVS/settings remain untouched.
        erased_start=start&~4095;erased_end=(end+4095)&~4095
        if erased_start<0xE000 and erased_end>0x9000:raise ValueError('Image would overlap NVS: '+image['file'])
        if end>image['limit']:raise ValueError('Image exceeds its permitted region: '+image['file'])
        regions.append((erased_start,erased_end))
    for (_,end),(start,_) in zip(sorted(regions),sorted(regions)[1:]):
        if end>start:raise ValueError('Overlapping image erase regions')
    for item in manifest.get('sd_packs',[]):
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
    args=parser.parse_args()
    manifest=validate()
    print('Verified:',manifest['release'])
    print('Hardware:',manifest['board'])
    for image in manifest['images']:print(' ',image['address'],image['file'],image['bytes'],'bytes')
    if args.check:return
    if not args.port:parser.error('--port is required unless using --check')
    common=[sys.executable,'-m','esptool','--chip','esp32s3','--port',args.port,'--baud','921600','--before','default_reset','--after','hard_reset']
    stamp='dry-run' if args.dry_run else datetime.now(timezone.utc).strftime('%Y%m%d-%H%M%S-%f')
    backup=Path.cwd()/'orb-backups'/stamp/'orb-before-install.bin'
    read=common+['read_flash','0x0',hex(manifest['flash_bytes']),str(backup)]
    write=common+['write_flash']
    for image in manifest['images']:write.extend([image['address'],str(ROOT/image['file'])])
    if args.dry_run:
        print('DRY RUN: no hardware access and no backup file created.')
        for cmd in (read,write):print(subprocess.list2cmdline(cmd))
        return
    if importlib.util.find_spec('esptool') is None:
        parser.error('Install the flasher first: python -m pip install esptool==4.8.6')
    backup.parent.mkdir(parents=True,exist_ok=False)
    print('Saving the recipient device backup:',backup,flush=True)
    subprocess.run(read,check=True)
    if not backup.is_file() or backup.stat().st_size!=manifest['flash_bytes']:
        raise RuntimeError('Backup is incomplete; firmware has not been written.')
    digest=hashlib.sha256(backup.read_bytes()).hexdigest()
    backup.with_suffix('.sha256').write_text(digest+'  '+backup.name+'\n',encoding='utf-8')
    print('Backup complete. Writing clock firmware; NVS/settings are preserved.',flush=True)
    subprocess.run(write,check=True)
    print('Installed. Swipe for faces; turn for volume; hold for groups. Copy SD packs separately; see README.')

if __name__=='__main__':
    try:main()
    except (ValueError,RuntimeError,subprocess.CalledProcessError) as error:
        sys.exit(str(error))
