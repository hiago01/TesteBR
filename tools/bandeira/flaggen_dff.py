#!/usr/bin/env python3
"""Gera SAMP/models/flag_maximo.dff (RenderWare 3.7 / GTA SA, ver 0x1803ffff): 3 atomics = mastro, base, tecido (frente+verso)."""
import struct, json, numpy as np, sys
sys.path.insert(0,'/tmp/fl'); import flagshape as S
C=12; R=40                       # colunas x linhas do tecido (13 x 41 = 533 pontos por folha)
VER=0x1803ffff
def chunk(t,data,ver=VER): return struct.pack('<III',t,len(data),ver)+data
def pad4(s): s=s.encode()+b'\0'; return s+b'\0'*((-len(s))%4)
# ---------------- tecido ----------------
def cloth():
    P=[];UV=[]
    for j in range(R+1):
        t=j/R; a=float(S.xl(t)); b=float(S.xr(t))
        for i in range(C+1):
            x=S.X0+a+(b-a)*i/C; P.append((x,0.0,S.ZB+S.H*t)); UV.append(((x-S.X0)/S.W,1.0-t))
    P=np.array(P); UV=np.array(UV); N=len(P)
    tri=[]
    for j in range(R):
        for i in range(C):
            v00=j*(C+1)+i; v10=v00+1; v01=v00+(C+1); v11=v01+1
            tri.append((v00,v10,v01)); tri.append((v10,v11,v01))       # CCW visto da frente (-Y)
    tri=np.array(tri)
    front=P.copy(); front[:,1]=-0.002; back=P.copy(); back[:,1]=+0.002
    UVb=UV.copy(); UVb[:,0]=1.0-UVb[:,0]
    V=np.vstack([front,back]); U=np.vstack([UV,UVb])
    T=np.vstack([tri,tri[:,[0,2,1]]+N])                              # verso: ordem invertida
    Nr=np.vstack([np.tile([0,-1,0.],(N,1)),np.tile([0,1,0.],(N,1))])
    M=np.concatenate([np.zeros(len(tri),int),np.ones(len(tri),int)])
    return V,Nr,U,T,M,N
# ---------------- malhas por revolucao ----------------
def lathe(profile,segs,cx=0.0,cy=0.0,mat=0):
    """profile: lista de (r,z) de baixo para cima, normal para fora (sentido horario visto de cima = CCW de fora)"""
    V=[];T=[]
    for (r,z) in profile:
        for s in range(segs):
            a=2*np.pi*s/segs; V.append((cx+r*np.cos(a),cy+r*np.sin(a),z))
    n=len(profile)
    for k in range(n-1):
        for s in range(segs):
            a=k*segs+s; b=k*segs+(s+1)%segs; c=(k+1)*segs+s; d=(k+1)*segs+(s+1)%segs
            T.append((a,c,b,mat)); T.append((b,c,d,mat))
    return np.array(V),T
def smooth_normals(V,T):
    N=np.zeros_like(V)
    for t in T:
        a,b,c=t[0],t[1],t[2]; n=np.cross(V[b]-V[a],V[c]-V[a]); N[a]+=n;N[b]+=n;N[c]+=n
    l=np.linalg.norm(N,axis=1); l[l==0]=1; return N/l[:,None]
def mast():
    pr=[(0.0,0.10),(0.040,0.10),(0.040,0.18),(0.026,0.20),(S.MAST_R,0.22),(S.MAST_R,3.04),(0.030,3.06),(0.030,3.09),(0.0,3.09)]
    V1,T1=lathe(pr,10,mat=0)
    # ponteira dourada (esfera achatada)
    cap=[(0.0,3.075),(0.032,3.085),(0.042,3.112),(0.030,3.140),(0.012,3.155),(0.0,3.158)]
    V2,T2=lathe(cap,10,mat=1)
    V=np.vstack([V1,V2]); T=T1+[(a+len(V1),b+len(V1),c+len(V1),m) for a,b,c,m in T2]
    # normais ficam no sentido de fora: confere pelo sinal em relacao ao eixo
    N=smooth_normals(V,T); return V,N,np.zeros((len(V),2)),np.array([t[:3] for t in T]),np.array([t[3] for t in T])
def base():
    pr=[(0.0,0.0),(0.30,0.0),(0.345,0.022),(0.345,0.075),(0.31,0.104),(0.17,0.118),(0.075,0.135),(0.05,0.18),(0.0,0.18)]
    V,T=lathe(pr,28)
    N=smooth_normals(V,T); return V,N,np.zeros((len(V),2)),np.array([t[:3] for t in T]),np.array([t[3] for t in T])
# ---------------- RW ----------------
def material(name,rgb):
    st=chunk(1,struct.pack('<IBBBBIIfff',0,rgb[0],rgb[1],rgb[2],255,0,1 if name else 0,1.0,0.0,1.0))
    tx=b''
    if name: tx=chunk(6,chunk(1,bytes.fromhex('06010100'))+chunk(2,pad4(name))+chunk(2,b'\0\0\0\0')+chunk(3,b''))
    return chunk(7,st+tx+chunk(3,b''))
def geometry(V,Nr,UV,T,M,mats):
    nv=len(V); nt=len(T); flags=0x1003e
    pre=np.full((nv,4),255,np.uint8)
    tris=b''.join(struct.pack('<HHHH',int(t[1]),int(t[0]),int(m),int(t[2])) for t,m in zip(T,M))
    mn=V.min(0); mx=V.max(0); cen=(mn+mx)/2; rad=float(np.linalg.norm(V-cen,axis=1).max())
    st=struct.pack('<IIII',flags,nt,nv,1)+pre.tobytes()+UV.astype('<f4').tobytes()+tris+struct.pack('<4f',*cen,rad)+struct.pack('<II',1,1)+V.astype('<f4').tobytes()+Nr.astype('<f4').tobytes()
    nmat=len(mats)
    matlist=chunk(8,chunk(1,struct.pack('<I',nmat)+b'\xff\xff\xff\xff'*nmat)+b''.join(material(*m) for m in mats))
    groups=[]
    for mi in range(nmat):
        idx=[(int(t[0]),int(t[1]),int(t[2])) for t,m in zip(T,M) if m==mi]
        groups.append(struct.pack('<II',len(idx)*3,mi)+np.array(idx,'<u4').tobytes())
    binm=struct.pack('<III',0,nmat,nt*3)+b''.join(groups)
    night=struct.pack('<I',0x37505050)+pre.tobytes()
    ext=chunk(0x50E,binm)+chunk(0x253F2FD,struct.pack('<I',0))+chunk(0x253F2F9,night)
    return chunk(15,chunk(1,st)+matlist+chunk(3,ext))
def build(out):
    Vc,Nc,Uc,Tc,Mc,N=cloth(); Vm,Nm,Um,Tm,Mm=mast(); Vb,Nb,Ub,Tb,Mb=base()
    geoms=[geometry(Vm,Nm,Um,Tm,Mm,[(None,(52,55,62)),(None,(214,172,44))]),
           geometry(Vb,Nb,Ub,Tb,Mb,[(None,(26,26,30))]),
           geometry(Vc,Nc,Uc,Tc,Mc,[('flag_maximo',(255,255,255)),('flag_maximo_verso',(255,255,255))])]
    names=['flag_maximo','flag_mast','flag_base','flag_cloth']
    fr=struct.pack('<I',4)
    for k in range(4):
        fr+=struct.pack('<9f3fiI',1,0,0,0,1,0,0,0,1,0,0,0,-1 if k==0 else 0,0x00020003)
    frames=chunk(14,chunk(1,fr)+b''.join(chunk(3,chunk(0x253F2FE,nm.encode())) for nm in names))
    geomlist=chunk(26,chunk(1,struct.pack('<I',3))+b''.join(geoms))
    atomics=b''.join(chunk(20,chunk(1,struct.pack('<IIII',k+1,k,5,0))+chunk(3,b'')) for k in range(3))
    clump=chunk(16,chunk(1,struct.pack('<III',3,0,0))+frames+geomlist+atomics+chunk(3,b''))
    open(out,'wb').write(clump)
    info=dict(C=C,R=R,N=N,W=S.W,H=S.H,ZB=S.ZB,X0=S.X0,bytes=len(clump),verts=dict(mast=len(Vm),base=len(Vb),cloth=len(Vc)),tris=dict(mast=len(Tm),base=len(Tb),cloth=len(Tc)))
    json.dump(info,open(out+'.json','w'),indent=1); print(info)
    return clump
if __name__=='__main__': build(sys.argv[1] if len(sys.argv)>1 else '/tmp/fl/flag_maximo.dff')
