#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Gera flag_maximo.txd (RenderWare TXD, plataforma D3D9, DXT1 com mips - o mesmo formato do mx_loja.txd/pref.txd do projeto)
e acrescenta as texturas ao banco texdb/mapasmaximo (ETC1 com mips, como as texturas do jogo; bytes antigos intactos)."""
import os,sys,struct,zlib,re
sys.path.insert(0,'/home/claude/out/mc/tools')
import numpy as np, skin_texdb as T
from PIL import Image
def levels(img):
    out=[img]; w,h=img.size
    while w>1 or h>1:
        w=max(1,w//2); h=max(1,h//2); out.append(img.resize((w,h),Image.BOX if False else Image.LANCZOS))
    return out
def pad4(a):
    h,w=a.shape[:2]; H=(h+3)//4*4; W=(w+3)//4*4
    if (H,W)==(h,w): return a
    return np.pad(a,((0,H-h),(0,W-w),(0,0)),mode='edge')
def enc(a,kind):
    rgb=pad4(a[...,:3])
    return T.encode_dxt1(rgb) if kind=='dxt' else T.encode_etc1(rgb)
def txd_native(name,img):
    lv=levels(img); w,h=img.size
    data=b''.join(struct.pack('<I',len(enc(np.asarray(l.convert('RGB')),'dxt')))+enc(np.asarray(l.convert('RGB')),'dxt') for l in lv)
    nm=name.encode().ljust(32,b'\0'); mk=b'\0'*32
    st=struct.pack('<II',9,0x1106)+nm+mk+struct.pack('<II',0x8200,0x31545844)+struct.pack('<HHBBBB',w,h,16,len(lv),4,8)+data
    ch=lambda t,d: struct.pack('<III',t,len(d),0x1803ffff)+d
    return ch(0x15,ch(1,st)+ch(3,b''))
def write_txd(path,items):
    ch=lambda t,d: struct.pack('<III',t,len(d),0x1803ffff)+d
    body=ch(1,struct.pack('<HH',len(items),0))+b''.join(txd_native(n,Image.open(p).convert('RGB')) for n,p in items)+ch(3,b'')
    open(path,'wb').write(ch(0x16,body))
def etc_record(name,img):
    lv=levels(img); data=b''.join(enc(np.asarray(l.convert('RGB')),'etc') for l in lv)
    w,h=img.size
    import texdb_format as F
    assert len(data)==F.total_size(T.GL_ETC1,w,h,True),(len(data),F.total_size(T.GL_ETC1,w,h,True))
    return struct.pack('<HHHHII',T.name_hash(name)&0xFFFF,T.GL_ETC1,w,h|0x8000,len(data)+4,0)+data, lv
def append_bank(bank,name,items):
    txtp=os.path.join(bank,name+'.txt'); lines=open(txtp,encoding='latin-1').read().split('\n')
    if lines[-1]=='': lines.pop()
    old=[re.match(r'"([^"]+)"',l).group(1) for l in lines[1:]]
    h16={T.name_hash(n)&0xFFFF:n for n in old}
    dat=bytearray(open(os.path.join(bank,name+'.etc.dat'),'rb').read()); toc=open(os.path.join(bank,name+'.etc.toc'),'rb').read()
    offs=list(struct.unpack('<%dI'%(len(toc)//4-1),toc[4:])); assert struct.unpack('<I',toc[:4])[0]==len(dat) and len(offs)==len(old)
    tmb=bytearray(open(os.path.join(bank,name+'.etc.tmb'),'rb').read()); unc=bytearray(open(os.path.join(bank,name+'.unc.tmb'),'rb').read())
    for nm,p in items:
        assert nm not in old and (T.name_hash(nm)&0xFFFF) not in h16,'colisao '+nm
        h16[T.name_hash(nm)&0xFFFF]=nm
        img=Image.open(p).convert('RGB'); rec,lv=etc_record(nm,img)
        offs.append(len(dat)); dat+=rec
        th=np.asarray(img.resize((8,8),Image.BOX)); th4=np.dstack([th,np.full((8,8),255,np.uint8)])
        f2,d2=T.encode_tex(th4,False,'etc'); tmb+=T.record(nm,f2,8,8,d2)
        f3,d3=T.encode_tex(th4,False,'unc'); unc+=T.record(nm,f3,8,8,d3)
        raw=open(p,'rb').read(); w,h=img.size
        lines.append('"%s" width=%d height=%d png=%08x img=%08x'%(nm,w,h,zlib.crc32(raw)&0xFFFFFFFF,zlib.crc32(np.asarray(img).tobytes())&0xFFFFFFFF))
        print('  +',nm,w,h,'mips=%d'%len(lv),len(rec),'bytes')
    open(os.path.join(bank,name+'.etc.dat'),'wb').write(bytes(dat)); open(os.path.join(bank,name+'.etc.toc'),'wb').write(struct.pack('<I',len(dat))+struct.pack('<%dI'%len(offs),*offs))
    open(os.path.join(bank,name+'.etc.tmb'),'wb').write(bytes(tmb)); open(os.path.join(bank,name+'.unc.tmb'),'wb').write(bytes(unc))
    open(txtp,'w',newline='\n',encoding='latin-1').write('\n'.join(lines)+'\n')
    return T.verify(bank)
if __name__=='__main__':
    sys.path.insert(0,'/tmp/wm/atm3/out/tools'); import texdb_format as F
    items=[('flag_maximo','/tmp/fl/flag_maximo.png'),('flag_maximo_verso','/tmp/fl/flag_maximo_verso.png')]
    import time; t=time.time()
    write_txd('/tmp/fl/flag_maximo.txd',items); print('txd',os.path.getsize('/tmp/fl/flag_maximo.txd'),'%.1fs'%(time.time()-t))
