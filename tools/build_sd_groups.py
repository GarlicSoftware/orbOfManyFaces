"""Build bounded, clock-only SD groups. Blue Meridian is supplied by firmware.
Usage: python tools/build_sd_groups.py [slug ...]
With no slugs, pack the archived faces not already in the built-in rotation.
Copy output .ocg files to /clock-groups/ on a FAT32 SD card, or use upload_groups.py.
"""
from pathlib import Path
import hashlib,json,struct,sys,unicodedata,zlib
from PIL import Image
from bake_assets import ROOT,SOURCE,KINDS,IMPORTED,rgb565,ink,glow_font
from font_bin import load_font

OUT=ROOT/'sd-groups'
LIMIT=6*1024*1024
def ascii_text(value):
    return unicodedata.normalize('NFKD',value).encode('ascii','ignore').decode().replace('"',"'").replace('\\','/').strip() or 'Clock'
def string(value):
    data=ascii_text(value).encode();return struct.pack('<H',len(data))+data+b'\0'
def image_data(im,alpha=False):
    data=bytearray()
    for p in im.convert('RGBA' if alpha else 'RGB').getdata():
        c=rgb565(*p[:3]);data.extend((c&255,c>>8))
        if alpha:data.append(p[3])
    return bytes(data)
def blob(raw):
    compressed=zlib.compress(raw,9) if raw else b''
    return struct.pack('<II',len(compressed),len(raw))+compressed
def sprite(folder,name,meta):
    path=folder/name
    if not path.exists() or not meta.get('show'):
        return blob(b'')+struct.pack('<HHhhhhB',0,0,0,0,233,233,0),0
    im=Image.open(path).convert('RGBA');box=im.getchannel('A').getbbox()
    if not box:return blob(b'')+struct.pack('<HHhhhhB',0,0,0,0,233,233,0),0
    im=im.crop(box);raw=image_data(im,True)
    return blob(raw)+struct.pack('<HHhhhhB',*im.size,meta.get('pivotX',0)-box[0],meta.get('pivotY',0)-box[1],meta.get('centerX',233),meta.get('centerY',233),meta.get('blend',0)),len(raw)
def font(content):
    if content is None:return b'\0'
    glyphs,pixels,baseline,height=content
    assert len(glyphs)==95
    return b'\1'+struct.pack('<bBI',baseline,height,len(pixels))+b''.join(struct.pack('<IBBBbb',*g) for g in glyphs)+bytes(pixels)
def face(slug):
    folder=ROOT/'imported-faces'/slug
    style=json.loads((folder/'clock_style.json').read_text(encoding='utf-8'))
    manifest=json.loads((folder/'theme.json').read_text(encoding='utf-8'))
    name=ascii_text(manifest['name'])[:96];out=bytearray(string(name))
    bg=style.get('bg',0);im=Image.new('RGBA',(466,466),(bg>>16,(bg>>8)&255,bg&255,255));im.alpha_composite(Image.open(folder/'clock_plate.png').convert('RGBA'))
    raw=image_data(im);out.extend(blob(raw));raw_size=len(raw)
    hands=style.get('hands',{});shadow=hands.get('shadow',{})
    for kind in KINDS:
        data,size=sprite(folder,f'clock_hand_{kind}.png' if kind in KINDS[:3] else f'clock_{kind}.png',hands.get(kind,{}));out.extend(data);raw_size+=size
    for kind in KINDS[:3]:
        meta=dict(hands.get(kind,{}));meta['show']=meta.get('show',False) and shadow.get('on',False)
        meta['centerX']=meta.get('centerX',233)+shadow.get('dx',0);meta['centerY']=meta.get('centerY',233)+shadow.get('dy',0);meta['blend']=0
        data,size=sprite(folder,f'clock_shadow_{kind}.png',meta);out.extend(data);raw_size+=size
    data,size=sprite(folder,'clock_overlay.png',{'show':True,'centerX':0,'centerY':0});out.extend(data);raw_size+=size
    out.extend(bytes(hands.get('order',[3,4,0,1,2])))
    for slot in (1,2):
        m=style.get('text'+str(slot),{})
        if not m.get('show'):out.extend(b'\0');continue
        content=load_font(folder/f'font_clock{slot}.bin');out.extend(font(content));out.extend(string(m.get('fmt','')))
        out.extend(struct.pack('<hhHBBBhhB',m.get('x',233),m.get('y',233),ink(m.get('color',0xffffff)),m.get('opa',255),bool(m.get('upper')),bool(m.get('curved')),m.get('curveR',150),round(m.get('arcDeg',0)*10),m.get('align',1)))
        glow=glow_font(content,m['glow']) if m.get('glow') and not m.get('curved') else None
        out.extend(font(glow));out.extend(struct.pack('<HHBB',ink(m.get('glowColor',m.get('color',0xffffff))),ink(m.get('bg',0)),m.get('bgOpa',0),m.get('radius',4)))
    out.extend(bytes((bool(style.get('textOverHands')),style.get('plateFollow',0))))
    if raw_size>4000000:raise ValueError(f'{name}: decoded image size exceeds face limit: {raw_size}')
    return bytes(out),raw_size,{'slug':slug,'name':name,'author':manifest.get('author',''),'license':manifest.get('license','Not specified; personal device copy')}
def peak(items):return sum(sorted([700000,700000]+[i[1] for i in items],reverse=True)[:2])
def write_group(index,items):
    name=f'Library {index:02d}'
    body=string(name)+b''.join(item[0] for item in items);raw=max(item[1] for item in items)
    data=b'ORBGRP01'+struct.pack('<IIII',len(items),len(body),zlib.crc32(body),raw)+body
    assert len(data)<=3*1024*1024 and len(data)+peak(items)+262144<=LIMIT
    filename=f'library-{index:02d}-{hashlib.sha256(data).hexdigest()[:8]}.ocg';(OUT/filename).write_bytes(data)
    result={'file':filename,'name':name,'bytes':len(data),'max_decoded':raw,'ram_required_after_framebuffers':len(data)+peak(items)+262144,'faces':[item[2] for item in items],'blue_meridian_shared':True}
    print(f'{name}: {len(items)+1} faces incl. blue Meridian; {len(data):,} pack bytes; {result["ram_required_after_framebuffers"]:,} RAM budget')
    return result
def main():
    OUT.mkdir(exist_ok=True)
    slugs=sys.argv[1:] or [s for s in json.loads((ROOT/'face-library.json').read_text()) if s not in IMPORTED]
    groups=[];batch=[];size=128;raw=700000
    for slug in slugs:
        item=face(slug);next_raw=max(raw,item[1]);next_size=size+len(item[0])
        if batch and (next_size>3*1024*1024 or next_size+peak(batch+[item])+262144>LIMIT or len(batch)>=12):
            groups.append(write_group(len(groups)+1,batch));batch=[];size=128;raw=700000
        batch.append(item);size+=len(item[0]);raw=max(raw,item[1])
    if batch:groups.append(write_group(len(groups)+1,batch))
    (OUT/'index.json').write_text(json.dumps(groups,indent=2),encoding='utf-8')
    print(f'{len(slugs)} additional faces in {len(groups)} groups; built-in {len(IMPORTED)+2} retained.')
if __name__=='__main__':main()
