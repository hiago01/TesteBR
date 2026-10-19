import random, math, io, re, os, sys
BLANK=os.environ.get('BLANK')=='1'
OUTD=os.environ.get('OUTD','out')
from PIL import Image, ImageDraw, ImageFont, ImageFilter
F='/home/claude/proj/TesteBR-free/app/src/main/assets/Fonts/'
def font(n,s): return ImageFont.truetype(F+n,s)
BOLD='HELVETICANEUELTCOM-MD.TTF'; COND='HELVETICANEUELTCOM-MDCN.TTF'; ROM='HELVETICANEUELTW1G-ROMAN.OTF'
# logo branco do HUD (atlas embutido)
h=open('proj/TesteBR-free/app/src/main/cpp/samp/gui/samp_widgets/hudatlas.h').read()
hexs=re.findall(r'0x([0-9a-f]{2})',h.split('kPng[]')[1]); png=bytes(int(x,16) for x in hexs)
atlas=Image.open(io.BytesIO(png)).convert('RGBA'); logo=atlas.crop((0,0,1024,349))
bb=logo.getbbox(); logo=logo.crop(bb)
def paste_logo(img,x,y,w,color=None,alpha=255):
    l=logo.resize((w,round(logo.height*w/logo.width)),Image.LANCZOS)
    if color:
        a=l.split()[3].point(lambda v:v*alpha//255); l=Image.merge('RGBA',(*[Image.new('L',l.size,c) for c in color],a))
    img.alpha_composite(l,(x,y))
def rr(d,box,r,**k): d.rounded_rectangle(box,radius=r,**k)
def grad(w,h,c0,c1):
    g=Image.new('RGB',(w,h)); px=g.load()
    for y in range(h):
        for x in range(w):
            t=(x/w*0.6+y/h*0.4); px[x,y]=tuple(int(c0[i]+(c1[i]-c0[i])*t) for i in range(3))
    return g
def silhouette(d,box,bg=(200,205,210),fg=(120,125,132)):
    x0,y0,x1,y1=box; rr(d,box,10,fill=bg); w=x1-x0; h=y1-y0
    cx=x0+w/2; d.ellipse([cx-w*.2,y0+h*.18,cx+w*.2,y0+h*.18+w*.4],fill=fg)
    d.pieslice([cx-w*.38,y0+h*.58,cx+w*.38,y0+h*1.3],180,360,fill=fg)
def qr(d,x,y,s,seed=7):
    random.seed(seed); n=25; c=s/n
    d.rectangle([x-6,y-6,x+s+6,y+s+6],fill=(255,255,255))
    for i in range(n):
        for j in range(n):
            fin=(i<7 and j<7) or (i<7 and j>=n-7) or (i>=n-7 and j<7)
            if fin: continue
            if random.random()<.5: d.rectangle([x+j*c,y+i*c,x+(j+1)*c,y+(i+1)*c],fill=(15,15,15))
    for (a,b) in [(0,0),(0,n-7),(n-7,0)]:
        X=x+b*c;Y=y+a*c
        d.rectangle([X,Y,X+7*c,Y+7*c],fill=(15,15,15)); d.rectangle([X+c,Y+c,X+6*c,Y+6*c],fill=(255,255,255)); d.rectangle([X+2*c,Y+2*c,X+5*c,Y+5*c],fill=(15,15,15))
def field(d,x,y,label,val,lw=300,col=(20,60,25)):
    d.text((x,y),label,font=font(COND,17),fill=col); d.text((x,y+20),'' if BLANK else val,font=font(BOLD,28),fill=(15,15,15))
def sig(d,x,y,w,txt):
    d.line([x,y,x+w,y],fill=(60,60,60),width=2); d.text((x,y+4),txt,font=font(COND,15),fill=(70,70,70))
    p=[(x+10,y-8),(x+40,y-34),(x+70,y-6),(x+100,y-30),(x+130,y-8),(x+170,y-26)]; d.line(p,fill=(30,40,110),width=3,joint='curve')

# ================= 1) CNH =================
W,H=1000,620
img=Image.new('RGBA',(W,H),(0,0,0,0)); base=grad(W,H,(34,110,52),(150,205,130)).convert('RGBA')
m=Image.new('L',(W,H),0); ImageDraw.Draw(m).rounded_rectangle([0,0,W-1,H-1],radius=36,fill=255); img.paste(base,(0,0),m)
d=ImageDraw.Draw(img)
paste_logo(img,60,200,260,(255,255,255),0)              # marca d'agua
d=ImageDraw.Draw(img)
rr(d,(14,14,W-15,H-15),28,outline=(235,245,230,255),width=3)
d.rectangle((14,28,W-15,112),fill=(18,70,32,255)); 
d.text((40,36),'DEPARTAMENTO DE TRÂNSITO',font=font(BOLD,34),fill=(255,255,255)); d.text((40,76),'CARTEIRA NACIONAL DE HABILITAÇÃO  •  ESTADO DE SAN ANDREAS',font=font(COND,19),fill=(190,235,185))
paste_logo(img,W-250,40,190,(255,255,255)); d=ImageDraw.Draw(img)
rr(d,(40,140,300,470),14,fill=(255,255,255,235)); silhouette(d,(52,152,288,458))
rr(d,(320,140,W-40,520),14,fill=(255,255,255,170))
paste_logo(img,440,250,380,(30,90,45),38); d=ImageDraw.Draw(img)
field(d,345,152,'NOME','Player'); field(d,345,224,'ID DO JOGADOR','ID 27'); field(d,345,296,'NACIONALIDADE','Brasileiro')
field(d,345,368,'Nº DE REGISTRO','000000000'); field(d,640,152,'CATEGORIA','AB'); field(d,640,224,'1ª HABILITAÇÃO','00/00/0000'); field(d,640,296,'VALIDADE','00/00/0000')
field(d,640,368,'OBSERVAÇÕES','—')
qr(d,W-200,470-0,120) if False else None
qr(d,W-215,392,110,11)
sig(d,345,486,260,'ASSINATURA DO CONDUTOR')
d.rectangle((14,H-70,W-15,H-15),fill=(18,70,32,255)); d.text((40,H-55),'MÁXIMO RP',font=font(BOLD,24),fill=(255,255,255)); d.text((260,H-52),'ITEM FICTÍCIO  •  USO NO JOGO',font=font(COND,20),fill=(190,235,185))
os.makedirs(OUTD,exist_ok=True); img.save(OUTD+'/doc_cnh_transito.png')

# ================= 2) Identidade funcional (carteira) =================
W,H=1500,640
img=Image.new('RGBA',(W,H),(0,0,0,0)); d=ImageDraw.Draw(img)
rr(d,(0,0,W-1,H-1),30,fill=(24,24,28,255)); rr(d,(10,10,W-11,H-11),24,outline=(70,70,78,255),width=3)
d.line([W//2,10,W//2,H-10],fill=(10,10,12),width=6)
# cartao branco (esquerda)
rr(d,(40,40,W//2-30,H-40),18,fill=(244,244,240,255))
d.rectangle((40,58,W//2-30,150),fill=(255,255,255,255)); d.rectangle((40,150,W//2-30,158),fill=(30,70,170,255))
d.text((70,64),'ESTADO DE SAN ANDREAS',font=font(BOLD,28),fill=(20,30,70)); d.text((70,102),'IDENTIDADE FUNCIONAL',font=font(COND,24),fill=(60,70,110))
paste_logo(img,W//2-210,66,150,(20,30,70)); d=ImageDraw.Draw(img)
silhouette(d,(70,185,260,415),bg=(205,215,235),fg=(90,105,140))
cx=290
for i,(l,v) in enumerate([('NOME','Player'),('ID DO JOGADOR','ID 27'),('Nº FUNCIONAL','MX-0000'),('ORGANIZAÇÃO','POLÍCIA MÁXIMO RP'),('NATURALIDADE','LOS SANTOS'),('VALIDADE','00/00/0000')]):
    col=i%2; row=i//2; x=cx+col*215; y=180+row*80
    d.text((x,y),l,font=font(COND,16),fill=(40,60,130)); d.text((x,y+20),'' if BLANK else v,font=font(BOLD,22 if len(v)<12 else 17),fill=(15,15,20))
sig(d,70,470,300,'ASSINATURA DO TITULAR'); sig(d,400,470,260,'AUTORIDADE EMISSORA')
d.rectangle((40,H-110,W//2-30,H-58),fill=(30,70,170,255)); d.text((70,H-100),'MÁXIMO RP  •  ITEM FICTÍCIO  •  USO NO JOGO',font=font(COND,20),fill=(255,255,255))
# couro (direita) + costura + distintivo generico
rr(d,(W//2+30,40,W-40,H-40),18,fill=(34,34,38,255))
for t in range(0,1):
    pass
for x in range(W//2+50,W-50,22): d.line([x,52,x+11,52],fill=(95,95,105),width=3); d.line([x,H-52,x+11,H-52],fill=(95,95,105),width=3)
cx,cy=int(W*0.75),int(H*0.46); gold=(212,170,60,255)
# louros (arco de folhas de cada lado do escudo)
for side in (-1,1):
    for k in range(11):
        t=k/10; ang=math.radians(-60+t*150)           # de baixo para cima
        px=cx+side*(125*math.cos(ang)); py=cy+10-125*math.sin(ang)*-1*-1
        py=cy+20-130*math.sin(ang)
        leaf=Image.new('RGBA',(60,60),(0,0,0,0)); ld=ImageDraw.Draw(leaf); ld.ellipse([20,4,40,56],fill=(190,150,48,255))
        leaf=leaf.rotate(-side*(60-t*150)+0*side,resample=Image.BICUBIC)
        img.alpha_composite(leaf,(int(px-30),int(py-30)))
d=ImageDraw.Draw(img)
# escudo
sh=[(cx-90,cy-110),(cx+90,cy-110),(cx+90,cy+10),(cx,cy+125),(cx-90,cy+10)]
d.polygon(sh,fill=(18,18,22,255),outline=gold); d.line(sh+[sh[0]],fill=gold,width=7)
st=[(cx+math.cos(math.radians(-90+i*36))*(60 if i%2==0 else 26),cy-30+math.sin(math.radians(-90+i*36))*(60 if i%2==0 else 26)) for i in range(10)]
d.polygon(st,fill=gold)
paste_logo(img,cx-70,cy+40,140,(212,170,60)); d=ImageDraw.Draw(img)
d.text((cx-130,H-100),'POLÍCIA  •  MÁXIMO RP',font=font(BOLD,26),fill=gold)
img.save(OUTD+'/doc_identidade_funcional.png')
