# gera os .col pequenos usados por sim_v3.cpp em /tmp/fx (usa os 2 .col originais enviados)
import struct,os
def ents(path):
    d=open(path,'rb').read(); o=0; out=[]
    while o+8<=len(d) and d[o:o+3]==b'COL':
        sz=struct.unpack_from('<I',d,o+4)[0]; out.append((d[o+8:o+30].split(b'\0')[0].decode(),d[o:o+8+sz])); o+=8+sz
    return out
new=dict(ents('/mnt/user-data/uploads/countrye_10.col')); old=dict(ents('/mnt/user-data/uploads/countrye_10_original_.col'))
os.makedirs('/tmp/fx',exist_ok=True)
open('/tmp/fx/brewery_only.col','wb').write(new['CE_brewery']); open('/tmp/fx/brewery_orig.col','wb').write(old['CE_brewery'])
open('/tmp/fx/two.col','wb').write(new['cunte_roads11']+new['cunte_roads12'])
b=bytearray(new['CE_brewery']); oF=struct.unpack_from('<I',b,4+96)[0]; b[4+oF]=0xFF; b[4+oF+1]=0xFF; open('/tmp/fx/bad.col','wb').write(bytes(b))
