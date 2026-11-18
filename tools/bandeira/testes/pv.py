import numpy as np, sys
from PIL import Image
import matplotlib; matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.collections import PolyCollection
tex=np.asarray(Image.open('png/flag_maximo.png').convert('RGB')).astype(float)/255
def load(fn):
    L=open(fn).read().split('\n'); N,T=map(int,L[0].split()); fr=[]
    for k in range(5): fr.append(np.array([list(map(float,l.split())) for l in L[1+k*N:1+(k+1)*N]]))
    tris=np.array([list(map(int,l.split())) for l in L[1+5*N:1+5*N+T] if l.strip()])
    return fr,tris
def draw(ax,P,tris,view):
    xyz=P[:,:3]; uv=P[:,3:]
    if view=='side': pts=np.c_[xyz[:,0]*np.cos(.5)+xyz[:,1]*np.sin(.5)*0.9, xyz[:,2]+ -xyz[:,0]*0.0]  # pseudo-oblique
    pts=np.c_[xyz[:,0]+xyz[:,1]*0.9, xyz[:,2]+xyz[:,1]*0.25]
    polys=[];cols=[]
    sun=np.array([0.4,-0.7,0.6]);sun/=np.linalg.norm(sun)
    order=np.argsort([-(xyz[t,1].mean()) for t in tris])   # far (y+) first
    for ti in order:
        t=tris[ti]; a,b,c=xyz[t]; n=np.cross(b-a,c-a); n/=max(np.linalg.norm(n),1e-9)
        sh=0.55+0.45*abs(n@sun)
        u,v=uv[t].mean(0); px=int(np.clip(u,0,.999)*tex.shape[1]); py=int(np.clip(v,0,.999)*tex.shape[0])
        cols.append(tex[py,px]*sh); polys.append(pts[t])
    ax.add_collection(PolyCollection(polys,facecolors=cols,edgecolors='none'))
    ax.plot([0,0],[0,3.1],color='#444',lw=3); ax.set_xlim(-.5,1.8);ax.set_ylim(-0.1,3.3);ax.set_aspect('equal');ax.axis('off');ax.set_facecolor('#8fb8e8')
for name,fn in (('10','d10.txt'),('04','d04.txt')):
    fr,tris=load(fn); fig,axs=plt.subplots(1,5,figsize=(15,5.2),facecolor='#8fb8e8')
    for k,ax in enumerate(axs): draw(ax,fr[k],tris,'x')
    fig.tight_layout(); fig.savefig(f'sim_{name}.png',dpi=70); plt.close(fig)
