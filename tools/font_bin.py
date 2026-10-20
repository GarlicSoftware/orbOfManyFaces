"""Build-time conversion of plain LVGL binary glyphs (LVGL MIT file layout)."""
import struct

class Bits:
    def __init__(self,data,offset=0):self.data=data;self.pos=offset*8
    def read(self,n,signed=False):
        value=0
        for _ in range(n):
            value=(value<<1)|((self.data[self.pos//8]>>(7-self.pos%8))&1);self.pos+=1
        if signed and n and value&(1<<(n-1)):value-=1<<n
        return value

def load_font(path):
    data=path.read_bytes();tables={};offset=0
    while offset<len(data):
        length,label=struct.unpack_from('<I4s',data,offset)
        assert length>=8 and offset+length<=len(data),(path,label,length)
        tables[label]=offset;offset+=length
    assert data[4:8]==b'head' and data[41]==0,'Expected uncompressed exported glyphs'
    ascent,descent=struct.unpack_from('<Hh',data,16)
    default=struct.unpack_from('<H',data,30)[0]
    loc_format,_,adv_format,bpp,xy,wh,adv_bits,compression=struct.unpack_from('<8B',data,34)
    assert bpp in (1,2,4,8) and not compression
    cmap=tables[b'cmap'];mapping={}
    for i in range(struct.unpack_from('<I',data,cmap+8)[0]):
        rel,start,length,first,count,kind,_=struct.unpack_from('<IIHHHBB',data,cmap+12+i*16);ptr=cmap+rel
        if kind in (0,2):
            for c in range(length):mapping[start+c]=first+(data[ptr+c] if kind==0 else c)
        else:
            assert kind in (1,3)
            points=struct.unpack_from('<'+'H'*count,data,ptr)
            for j,c in enumerate(points):mapping[start+c]=first+(struct.unpack_from('<H',data,ptr+2*count+2*j)[0] if kind==1 else j)
    loca=tables[b'loca'];count=struct.unpack_from('<I',data,loca+8)[0]
    offsets=struct.unpack_from('<'+('H' if loc_format==0 else 'I')*count,data,loca+12)
    glyphs=[];pixels=[]
    for code in range(32,127):
        gid=mapping.get(code,0)
        if not gid:glyphs.append((len(pixels),0,0,0,0,0));continue
        bits=Bits(data,tables[b'glyf']+offsets[gid]);advance=bits.read(adv_bits) if adv_bits else default
        advance=round(advance/16) if adv_format else advance
        x,y=bits.read(xy,True),bits.read(xy,True);w,h=bits.read(wh),bits.read(wh)
        glyphs.append((len(pixels),w,h,advance,x,-y-h))
        pixels.extend(round(bits.read(bpp)*255/((1<<bpp)-1)) for _ in range(w*h))
    return glyphs,pixels,round((ascent+descent)/2),ascent-descent
