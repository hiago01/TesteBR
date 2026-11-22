#!/usr/bin/env python3
"""Pre-visualizacao da nota fiscal: transforma a gravacao do renderer falso (*.dump) em PNG (so para olhar o layout no PC)."""
import glob, os, sys
from PIL import Image, ImageDraw, ImageFont

SS = 2
FONTS = ['/usr/share/fonts/truetype/google-fonts/RobotoCondensed-Regular.ttf', '/usr/share/fonts/truetype/dejavu/DejaVuSansCondensed.ttf', '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf']
FONT = next((f for f in FONTS if os.path.exists(f)), None)
_cache = {}
def font(sz):
    sz = max(6, int(round(sz)))
    if sz not in _cache:
        _cache[sz] = ImageFont.truetype(FONT, sz) if FONT else ImageFont.load_default()
    return _cache[sz]

def scene(w, h):
    im = Image.new('RGB', (w, h), (70, 78, 90))
    d = ImageDraw.Draw(im)
    for y in range(h):
        t = y / h
        d.line([(0, y), (w, y)], fill=(int(120 - 50 * t), int(135 - 45 * t), int(150 - 40 * t)))
    d.rounded_rectangle([w * 0.05, h * 0.20, w * 0.62, h * 0.75], radius=h * 0.08, fill=(235, 238, 242))      # "carro" branco
    d.rectangle([0, h * 0.78, w, h], fill=(90, 92, 96))
    d.ellipse([w * 0.80, h * 0.55, w * 0.98, h * 1.05], fill=(20, 22, 30))
    return im

def render(path):
    lines = open(path, encoding='latin-1').read().split('\n')
    w, h = [int(float(x)) for x in lines[0].split()[1:3]]
    im = scene(w * SS, h * SS)
    d = ImageDraw.Draw(im, 'RGBA')
    S = lambda v: float(v) * SS
    for ln in lines[1:]:
        if not ln: continue
        k, _, rest = ln.partition(' ')
        a = rest.split(' ')
        if k == 'L':
            x0, y0, x1, y1, th = map(float, a[:5]); r, g, b, al = map(int, a[5:9])
            d.line([(S(x0), S(y0)), (S(x1), S(y1))], fill=(r, g, b, al), width=max(1, int(round(th * SS))))
        elif k == 'R':
            x0, y0, x1, y1 = map(float, a[:4]); r, g, b, al, fill = map(int, a[4:9]); th = float(a[9])
            box = [S(x0), S(y0), S(x1), S(y1)]
            if fill: d.rectangle(box, fill=(r, g, b, al))
            else: d.rectangle(box, outline=(r, g, b, al), width=max(1, int(th * SS)))
        elif k == 'RR':
            x0, y0, x1, y1 = map(float, a[:4]); r, g, b, al = map(int, a[4:8]); rd = float(a[8]); fill = int(a[9]); th = float(a[10])
            box = [S(x0), S(y0), S(x1), S(y1)]
            rad = max(0, min(S(rd), (box[2] - box[0]) / 2, (box[3] - box[1]) / 2))
            if fill: d.rounded_rectangle(box, radius=rad, fill=(r, g, b, al))
            else: d.rounded_rectangle(box, radius=rad, outline=(r, g, b, al), width=max(1, int(th * SS)))
        elif k == 'M':
            x0, y0, x1, y1 = map(float, a[:4]); v = list(map(int, a[4:20]))
            top, bot = v[0:4], v[8:12]
            Y0, Y1 = int(S(y0)), int(S(y1))
            for yy in range(Y0, Y1):
                t = (yy - Y0) / max(1, Y1 - Y0 - 1)
                d.line([(S(x0), yy), (S(x1), yy)], fill=tuple(int(top[i] + (bot[i] - top[i]) * t) for i in range(4)))
        elif k == 'P':
            n = int(a[0]); r, g, b, al = map(int, a[1:5]); pts = [(S(a[5 + 2 * i]), S(a[6 + 2 * i])) for i in range(n)]
            d.polygon(pts, fill=(r, g, b, al))
        elif k == 'PL':
            n = int(a[0]); closed = int(a[1]); th = float(a[2]); r, g, b, al = map(int, a[3:7]); pts = [(S(a[7 + 2 * i]), S(a[8 + 2 * i])) for i in range(n)]
            if closed: pts.append(pts[0])
            d.line(pts, fill=(r, g, b, al), width=max(1, int(round(th * SS))), joint='curve')
        elif k == 'T':
            x, y, fs = float(a[0]), float(a[1]), float(a[2]); r, g, b, al = map(int, a[3:7]); text = ' '.join(a[7:])
            d.text((S(x), S(y)), text, font=font(fs * SS * 0.82), fill=(r, g, b, al))
    im = im.resize((w, h), Image.LANCZOS)
    out = path.replace('.dump', '.png')
    im.save(out)
    return out

if __name__ == '__main__':
    d = sys.argv[1] if len(sys.argv) > 1 else '.'
    for p in sorted(glob.glob(os.path.join(d, '*.dump'))):
        print('png:', render(p))
