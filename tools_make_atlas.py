# Gera o atlas da HUD (logo Maximo RP + icones brancos) e o header C++ embutido.
# Atlas 1024x512 RGBA:  logo em cima (1024x349) | linha de icones 128x128 embaixo.
import math, sys
from PIL import Image, ImageDraw

LOGO = '/mnt/user-data/uploads/maximo_rp_versao_inicial_sem_fundo.png'
OUT_PNG = 'hud_atlas.png'
OUT_H   = 'hud_atlas.h'

AW, AH = 1024, 512
SS = 4  # supersample dos icones

atlas = Image.new('RGBA', (AW, AH), (0, 0, 0, 0))

# ---------------- logo ----------------
logo = Image.open(LOGO).convert('RGBA')
# recorte manual (a bbox do alpha pega pontinhos soltos do PNG)
crop = logo.crop((255, 200, 1650, 675))
lw = AW
lh = round(crop.height * lw / crop.width)
logo_r = crop.resize((lw, lh), Image.LANCZOS)
# deixa o logo 100% branco (mantem o alpha)
r, g, b, a = logo_r.split()
white = Image.new('L', logo_r.size, 255)
logo_r = Image.merge('RGBA', (white, white, white, a))
atlas.paste(logo_r, (0, 0))
LOGO_RECT = (0, 0, lw, lh)

# ---------------- icones ----------------
ISZ = 128
ICON_Y = 360
icons = {}

def new_canvas():
    return Image.new('L', (ISZ * SS, ISZ * SS), 0)

def finish(mask):
    return mask.resize((ISZ, ISZ), Image.LANCZOS)

def P(x, y):  # coordenadas 0..128 -> supersample
    return (x * SS, y * SS)

def heart():
    m = new_canvas(); d = ImageDraw.Draw(m)
    pts = []
    for i in range(361):
        t = math.radians(i)
        x = 16 * math.sin(t) ** 3
        y = 13 * math.cos(t) - 5 * math.cos(2*t) - 2 * math.cos(3*t) - math.cos(4*t)
        pts.append(P(64 + x * 3.0, 62 - y * 3.0))
    d.polygon(pts, fill=255)
    return finish(m)

def shield():
    m = new_canvas(); d = ImageDraw.Draw(m)
    outer = [P(64, 18), P(98, 30), P(98, 64), P(92, 82), P(64, 108), P(36, 82), P(30, 64), P(30, 30)]
    d.polygon(outer, fill=255)
    # recorte interno: sobra uma moldura + metade preenchida (estilo emblema)
    inner = [P(64, 30), P(88, 39), P(88, 63), P(83, 76), P(64, 94), P(45, 76), P(40, 63), P(40, 39)]
    d.polygon(inner, fill=0)
    half = [P(64, 30), P(64, 94), P(45, 76), P(40, 63), P(40, 39)]
    d.polygon(half, fill=255)
    return finish(m)

def burger():
    m = new_canvas(); d = ImageDraw.Draw(m)
    # pao de cima
    d.pieslice([P(26, 28), P(102, 84)], 180, 360, fill=255)
    # alface (onda)
    pts = []
    for i in range(0, 101):
        x = 24 + i * 0.8
        y = 74 + math.sin(i / 100 * math.pi * 6) * 3.2
        pts.append(P(x, y))
    for i in range(100, -1, -1):
        x = 24 + i * 0.8
        y = 74 + math.sin(i / 100 * math.pi * 6) * 3.2 + 6
        pts.append(P(x, y))
    d.polygon(pts, fill=255)
    # carne
    d.rounded_rectangle([P(26, 86), P(102, 96)], radius=5 * SS, fill=255)
    # pao de baixo
    d.rounded_rectangle([P(28, 100), P(100, 110)], radius=6 * SS, fill=255)
    # sementes (furos)
    for sx, sy in [(48, 46), (64, 40), (80, 46), (56, 58), (72, 58)]:
        d.ellipse([P(sx - 2.4, sy - 1.6), P(sx + 2.4, sy + 1.6)], fill=0)
    return finish(m)

def bolt():
    m = new_canvas(); d = ImageDraw.Draw(m)
    pts = [P(72, 14), P(38, 68), P(60, 68), P(52, 114), P(92, 54), P(68, 54), P(72, 14)]
    d.polygon(pts, fill=255)
    return finish(m)

def drop():
    m = new_canvas(); d = ImageDraw.Draw(m)
    cx, cy, r = 64, 76, 26
    d.ellipse([P(cx - r, cy - r), P(cx + r, cy + r)], fill=255)
    # ponta (tangentes a partir do topo)
    top = (64, 14)
    dist = cy - top[1]
    ang = math.asin(r / dist)
    tl = math.cos(ang) * dist
    # pontos de tangencia
    dx = math.sqrt(dist * dist - r * r)
    k = r / dist
    px = r * math.sqrt(1 - k * k)
    py = cy - r * k * k  # y do ponto de tangencia
    pts = [P(top[0], top[1]), P(64 + px, py), P(64 - px, py)]
    d.polygon(pts, fill=255)
    # brilho (recorte)
    d.ellipse([P(50, 70), P(58, 84)], fill=0)
    return finish(m)

def person():
    m = new_canvas(); d = ImageDraw.Draw(m)
    d.ellipse([P(46, 22), P(82, 58)], fill=255)
    d.pieslice([P(26, 66), P(102, 142)], 180, 360, fill=255)
    return finish(m)

def burst():
    m = new_canvas(); d = ImageDraw.Draw(m)
    pts = []
    n = 8
    for i in range(n * 2):
        ang = math.pi / n * i - math.pi / 2
        rr = 46 if i % 2 == 0 else 20
        pts.append(P(64 + math.cos(ang) * rr, 64 + math.sin(ang) * rr))
    d.polygon(pts, fill=255)
    return finish(m)

def coin():
    m = new_canvas(); d = ImageDraw.Draw(m)
    d.ellipse([P(22, 22), P(106, 106)], fill=255)
    d.ellipse([P(34, 34), P(94, 94)], fill=0)
    d.rectangle([P(60, 40), P(68, 88)], fill=255)
    return finish(m)

ORDER = [('heart', heart), ('shield', shield), ('burger', burger), ('bolt', bolt),
         ('drop', drop), ('person', person), ('burst', burst), ('coin', coin)]
rects = {}
for i, (name, fn) in enumerate(ORDER):
    mask = fn()
    icon = Image.new('RGBA', (ISZ, ISZ), (255, 255, 255, 0))
    icon.putalpha(mask)
    x = i * ISZ
    atlas.paste(icon, (x, ICON_Y), icon)
    rects[name] = (x, ICON_Y, ISZ, ISZ)

atlas.save(OUT_PNG, optimize=True)
print('atlas png bytes:', len(open(OUT_PNG, 'rb').read()))

# ---------------- header C++ ----------------
data = open(OUT_PNG, 'rb').read()
with open(OUT_H, 'w') as f:
    f.write('// GERADO por make_atlas.py - nao edite a mao.\n')
    f.write('// Atlas da HUD: logo Maximo RP (branco) + icones brancos (tintados em runtime).\n')
    f.write('#pragma once\n#include <cstddef>\n\n')
    f.write('namespace HudAtlas\n{\n')
    f.write('    constexpr int kWidth  = %d;\n    constexpr int kHeight = %d;\n\n' % (AW, AH))
    f.write('    struct Rect { int x, y, w, h; };\n')
    f.write('    constexpr Rect kLogo = { %d, %d, %d, %d };\n' % (LOGO_RECT[0], LOGO_RECT[1], LOGO_RECT[2], LOGO_RECT[3]))
    for name, r in rects.items():
        f.write('    constexpr Rect kIcon_%s = { %d, %d, %d, %d };\n' % ((name,) + r))
    f.write('\n    static const unsigned char kPng[] = {\n')
    for i in range(0, len(data), 24):
        f.write('        ' + ','.join('0x%02x' % b for b in data[i:i+24]) + ',\n')
    f.write('    };\n    constexpr size_t kPngSize = %d;\n}\n' % len(data))
print('logo rect', LOGO_RECT, 'header ok')
