"""Package only the explicit public project allowlist and its matching build."""
from pathlib import Path
import hashlib,json,re,shutil,tempfile,zipfile
ROOT=Path(__file__).resolve().parents[1]
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def main():
    version=re.search(r'Clock ([0-9.]+)',(ROOT/'src/main.cpp').read_text()).group(1)
    build=ROOT/'.pio/build/orb-clock';out=ROOT/'releases';out.mkdir(exist_ok=True)
    credits=json.loads((ROOT/'ARTWORK_CREDITS.json').read_text(encoding='utf-8'))
    assert len(credits)==42 and all(c['license'] in ('MIT','CC0 1.0') for c in credits)
    with tempfile.TemporaryDirectory(prefix='orb-public-release-') as temp:
        folder=Path(temp)/f'orbOfManyFaces-{version}';folder.mkdir()
        for name in ('README.md','ROADMAP.md','CHANGELOG.md','requirements-install.txt','NOTICE.md','LICENSE','requirements.txt','platformio.ini','partitions.csv','install.py','ARTWORK_CREDITS.json','face-credits.json','face-library.json'):
            shutil.copyfile(ROOT/name,folder/name)
        for directory in ('src','include','clock-assets','imported-faces','licenses','docs','sd-groups','tests'):
            shutil.copytree(ROOT/directory,folder/directory,ignore=shutil.ignore_patterns('__pycache__'))
        (folder/'tools').mkdir()
        for name in ('__init__.py','device.py','upload_groups.py','build_sd_groups.py','font_bin.py','bake_assets.py','package_release.py','test_logic.py','test_favorites.py','test_renderer.py'):
            shutil.copyfile(ROOT/'tools'/name,folder/'tools'/name)
        images=[]
        for name,address,limit in [('bootloader.bin',0,0x8000),('partitions.bin',0x8000,0x9000),('firmware.bin',0x10000,0x650000)]:
            source=build/name;assert source.is_file() and address+source.stat().st_size<=limit
            shutil.copyfile(source,folder/name);images.append({'file':name,'address':hex(address),'limit':limit,'bytes':source.stat().st_size,'sha256':sha(source)})
        packs=[]
        for group in json.loads((folder/'sd-groups/index.json').read_text()):
            path=folder/'sd-groups'/group['file'];packs.append({'file':'sd-groups/'+group['file'],'bytes':path.stat().st_size,'sha256':sha(path),'face_count':len(group['faces'])})
        manifest={'release':'Orb of Many Faces '+version,'board':'Waveshare ESP32-S3-Touch-AMOLED-1.75 (16 MB flash, 8 MB PSRAM)','chip':'esp32s3','flash_bytes':0x1000000,'face_count':42,'built_in_face_count':8,'sd_group_count':len(packs),'hourly_chime':True,'images':images,'sd_packs':packs,'contains_nvs':False}
        (folder/'manifest.json').write_text(json.dumps(manifest,indent=2))
        archive=out/f'orbOfManyFaces-{version}.zip'
        with zipfile.ZipFile(archive,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=9) as z:
            for p in sorted(folder.rglob('*')):
                if p.is_file():z.write(p,p.relative_to(folder.parent))
        with zipfile.ZipFile(archive) as z:
            assert z.testzip() is None
            for item in images+packs:assert hashlib.sha256(z.read(folder.name+'/'+item['file'])).hexdigest()==item['sha256']
    archive.with_suffix('.zip.sha256').write_text(sha(archive)+'  '+archive.name+'\n')
    print(archive,archive.stat().st_size,'bytes; 42 public faces, image/pack hashes verified')
if __name__=='__main__':main()
