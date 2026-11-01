# Renderiza os dumps do MockCanvas (aproximacao: sem ImGui; fonte DejaVu) para checar layout. Uso: python3 render.py 2400 1080 home.txt out.png
import sys
from PIL import Image, ImageDraw, ImageFont
W,H=int(sys.argv[1]),int(sys.argv[2]); im=Image.new("RGBA",(W,H),(40,60,40,255))
def font(sz):
    for p in ("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf","/usr/share/fonts/dejavu/DejaVuSans.ttf"):
        try: return ImageFont.truetype(p,max(6,int(sz*0.8)))
        except Exception: pass
    return ImageFont.load_default()
for ln in open(sys.argv[3],encoding="utf-8"):
    head,_,s=ln.rstrip("\n").partition("|"); f=head.split(); k=f[0]
    x,y,w,h=map(float,f[1:5]); r,g,b,a=map(int,f[5:9]); A,B=float(f[9]),float(f[10]); cx,cy,cw,ch=map(float,f[15:19])
    layer=Image.new("RGBA",(W,H),(0,0,0,0)); d=ImageDraw.Draw(layer); col=(r,g,b,a)
    if k=='R':
        if A>0.5: d.rounded_rectangle([x,y,x+w,y+h],radius=min(A,min(w,h)/2),fill=col)
        else: d.rectangle([x,y,x+w,y+h],fill=col)
    elif k=='S': d.rounded_rectangle([x,y,x+w,y+h],radius=min(A,min(w,h)/2),outline=col,width=max(1,int(B)))
    elif k=='G': d.rectangle([x,y,x+w,y+h],fill=col)
    elif k=='C': d.ellipse([x,y,x+w,y+h],fill=col)
    elif k=='O': d.ellipse([x,y,x+w,y+h],outline=col,width=max(1,int(B)))
    elif k=='L': d.line([x,y,x+w,y+h],fill=col,width=max(1,int(A)))
    elif k=='T': d.polygon([(x,y),(float(f[11]),float(f[12])),(float(f[13]),float(f[14]))],fill=col)
    elif k=='X': d.text((x,y),s,font=font(A),fill=col)
    m=Image.new("L",(W,H),0); ImageDraw.Draw(m).rectangle([max(cx,0),max(cy,0),min(cx+cw,W),min(cy+ch,H)],fill=255)
    layer.putalpha(Image.composite(layer.getchannel("A"),Image.new("L",(W,H),0),m)); im=Image.alpha_composite(im,layer)
im.convert("RGB").save(sys.argv[4])
