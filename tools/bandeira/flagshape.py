"""Forma da bandeira (feather flag) - UMA fonte de verdade para o DFF e para a textura.
Eixos do modelo: +X do mastro para a ponta livre, +Z para cima, folha do tecido no plano XZ (frente = normal -Y)."""
import numpy as np
W=1.20           # largura util do tecido (m)  -> textura 1024 px  (853 px/m)
H=2.40           # altura do tecido (m)        -> textura 2048 px
ZB=0.55          # z da borda de baixo do tecido
X0=0.035         # folga do tecido ate o eixo do mastro
MAST_R=0.022; MAST_TOP=3.12
T_ARC=0.80       # acima disso o tecido se solta do mastro (arco superior)
MINW=0.07        # largura minima de uma linha (fracao de W)
def xr(t):
    t=np.asarray(t,float)
    out=np.where(t<0.30, np.sin(np.pi/2*np.clip(t/0.30,0,1))**1.25, 1.0)           # diagonal curva embaixo
    top=np.clip((t-0.90)/0.10,0,1); out=out*(1-0.14*top**2)                          # canto superior direito arredondado
    return np.maximum(out,MINW)*W
def xl(t):
    t=np.asarray(t,float); a=np.clip((t-T_ARC)/(1-T_ARC),0,1)
    out=W*(1-np.sqrt(np.clip(1-a**2,0,1)))                                          # arco da curva superior
    return np.minimum(out,xr(t)*(1-MINW*1.3))
def inside(u,v):
    """u,v em 0..1 da textura (v=0 topo). True dentro do tecido"""
    t=1-np.asarray(v,float); x=np.asarray(u,float)*W
    return (x>=xl(t)-1e-9)&(x<=xr(t)+1e-9)&(t>=0)&(t<=1)
