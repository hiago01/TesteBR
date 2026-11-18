import numpy as np, sys
from PIL import Image
from scipy import ndimage as ndi
sys.path.insert(0,'/tmp/fl'); import flagshape as S
TW,TH=1024,2048
u=(np.arange(TW)+.5)/TW; v=(np.arange(TH)+.5)/TH
U,V=np.meshgrid(u,v); MASK=S.inside(U,V)
yy,xx=np.mgrid[0:TH,0:TW]
LOGO=Image.open('/mnt/user-data/uploads/1795ae88-image.png').convert('RGB')
def art(mirror,seed):
    rng=np.random.default_rng(seed)
    mask=MASK
    weave=(np.sin(xx*2*np.pi/3.0)*np.sin(yy*2*np.pi/3.0))*2.2+rng.normal(0,1.2,(TH,TW))
    img=np.clip(np.stack([np.full((TH,TW),7.0),np.full((TH,TW),7.0),np.full((TH,TW),9.0)],-1)+weave[...,None],0,255)
    dist=ndi.distance_transform_edt(np.pad(mask,1,constant_values=False))[1:-1,1:-1]
    sh=np.clip(0.5+0.5*np.sin((xx*0.9+yy*0.35)*2*np.pi/420.0),0,1)
    G=np.array([176,128,24.])+(np.array([255,214,84.])-np.array([176,128,24.]))*sh[...,None]
    for m,a in (((dist>=10)&(dist<46)&mask,1.0),((dist>=62)&(dist<68)&mask,0.9)):
        img=np.where(m[...,None],img*(1-a)+G*a,img)
    e1=((U-1.18)/0.80)**2+((V-1.06)/0.46)**2; e2=((U-1.18)/0.745)**2+((V-1.06)/0.425)**2
    sw=(e1<1)&mask&(dist>70); sw2=(e1<1)&(e2>=1)&mask&(dist>70)
    img=np.where(sw[...,None],np.array([226,176,30.])*(0.92+0.08*sh[...,None]),img)
    img=np.where(sw2[...,None],np.array([255,226,110.])*(0.9+0.1*sh[...,None]),img)
    img=np.where(mask[...,None],img,np.array([6.,6.,8.]))
    if mirror: img=img[:,::-1].copy()          # a malha do verso usa u=1-u: o contorno espelha; o logo (abaixo) NAO
    LW=860; L=np.clip(np.asarray(LOGO.resize((LW,LW),Image.LANCZOS),float)-np.array([1.0,1.0,12.0]),0,255)
    cx,cy=TW//2,int(TH*0.40); x0,y0=cx-LW//2,cy-LW//2
    lm=np.ones((LW,LW)); f=int(LW*0.14)
    for k in range(f):
        a=(k+1)/f; a=a*a*(3-2*a)
        lm[k,:]=np.minimum(lm[k,:],a); lm[LW-1-k,:]=np.minimum(lm[LW-1-k,:],a); lm[:,k]=np.minimum(lm[:,k],a); lm[:,LW-1-k]=np.minimum(lm[:,LW-1-k],a)
    reg=img[y0:y0+LW,x0:x0+LW]
    img[y0:y0+LW,x0:x0+LW]=reg*(1-lm[...,None])+L*lm[...,None]
    return np.clip(img,0,255).astype(np.uint8)
for nm,mir,sd in (('flag_maximo',False,7),('flag_maximo_verso',True,8)):
    o=art(mir,sd); Image.fromarray(o).save('/tmp/fl/%s.png'%nm); print(nm,o.shape,o.reshape(-1,3).mean(0).round(1))
