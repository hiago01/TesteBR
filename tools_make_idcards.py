# Gera as texturas dos cartoes de identidade 3D (Maximo RP) e o header C++ embutido.
# Uso: python3 tools_make_idcards.py <pasta_Cards> <saida_dir>
#  - recorta os 5 banners da prancha (Civil/Policial/Delegado/Medico/Administrador)
#  - recorta o cartao Militar do Exercito e APAGA os dados ficticios (Player / ID 27 / N/A)
#  - canto arredondado (alpha), largura 512 px, proporcao original mantida, PNG paletizado
import sys, os, io
from PIL import Image, ImageDraw
SRC = sys.argv[1] if len(sys.argv) > 1 else 'cards/Cards'
OUT = sys.argv[2] if len(sys.argv) > 2 else 'out'
DOCS = sys.argv[3] if len(sys.argv) > 3 else 'tools_assets'   # docs de mundo (6=CNH, 7=Identidade funcional)
os.makedirs(OUT, exist_ok=True)
W = 512
S = 1620 / 1402.0                      # coordenadas medidas na previa 1402 px -> arquivo 1620 px

def box(x0, y0, x1, y1): return tuple(int(round(v * S)) for v in (x0, y0, x1, y1))

sheet = Image.open(f'{SRC}/a_clean_high_detail_graphic_design_sheet_of_five.png').convert('RGB')
mil   = Image.open(f'{SRC}/wide_horizontal_game_id_card_banner_in_a_green_mi.png').convert('RGB')

cards = [  # id, nome, imagem, caixa, cor de destaque (r,g,b)
 (0, 'civil',         sheet, box(4,   2,   696, 270), (150, 200, 40)),
 (1, 'policial',      sheet, box(705, 2,  1398, 270), (40, 130, 255)),
 (2, 'delegado',      sheet, box(4,  282,  696, 554), (235, 50, 40)),
 (3, 'medico',        sheet, box(705,282, 1398, 554), (40, 215, 215)),
 (4, 'administrador', sheet, box(348,565, 1052, 824), (170, 70, 235)),
 (5, 'militar',       mil,   box(8,   50, 1394, 786), (90, 200, 40)),
]
for _cid, _name, _file, _acc in [(6, 'cnh', 'doc_cnh_transito.png', (80, 180, 90)),
                                 (7, 'funcional', 'doc_identidade_funcional.png', (212, 170, 60))]:
    _im = Image.open(f'{DOCS}/{_file}').convert('RGBA')
    cards.append((_cid, _name, _im, (0, 0, _im.width, _im.height), _acc))

def clean_militar(im):
    # apaga os valores ficticios (mantem os rotulos e a FUNCAO "MILITAR DO EXERCITO")
    d = ImageDraw.Draw(im)
    paper = im.getpixel((int(560*S), int(268*S)))
    # coordenadas na previa 1402x840 relativas ao cartao inteiro -> convertidas para o recorte depois
    return paper

def rounded(im, r):
    m = Image.new('L', im.size, 0)
    ImageDraw.Draw(m).rounded_rectangle([0, 0, im.width - 1, im.height - 1], radius=r, fill=255)
    out = im.convert('RGBA'); out.putalpha(m); return out

hdr = []
png_bytes = []
for cid, name, img, bx, acc in cards:
    work = img.copy()
    if name == 'militar':
        d = ImageDraw.Draw(work)
        # campos de valor (previa 1402): Player / ID 27 / N/A  -> papel
        for (x0, y0, x1, y1) in [(396,283,612,327), (396,403,612,447), (396,622,612,664)]:
            sx0, sy0, sx1, sy1 = [int(round(v*S)) for v in (x0,y0,x1,y1)]
            paper = work.getpixel((sx1 + 6, (sy0 + sy1)//2))
            d.rectangle([sx0, sy0, sx1, sy1], fill=paper)
    c = work.crop(bx)
    h = round(c.height * W / c.width)
    c = c.resize((W, h), Image.LANCZOS)
    c = rounded(c, int(h * 0.06))
    c.save(f'{OUT}/card_{name}.png')
    q = c.quantize(colors=200, method=Image.FASTOCTREE, dither=Image.NONE)
    b = io.BytesIO(); q.save(b, 'PNG', optimize=True); data = b.getvalue()
    png_bytes.append((cid, name, c.width, c.height, acc, data))
    print(name, c.size, len(data), 'bytes')

with open(f'{OUT}/idcard_assets.h', 'w') as f:
    f.write('// GERADO por tools_make_idcards.py - nao edite a mao.\n')
    f.write('// Texturas dos cartoes de identidade 3D (Maximo RP): 5 banners + Militar (dados ficticios apagados).\n')
    f.write('#pragma once\n#include <cstddef>\n#include <cstdint>\n\nnamespace IdCardAssets\n{\n')
    f.write('    struct Card { const char* name; int w, h; uint8_t accentR, accentG, accentB; const unsigned char* png; size_t size; };\n\n')
    for cid, name, w, h, acc, data in png_bytes:
        f.write(f'    static const unsigned char kPng_{name}[] = {{\n')
        for i in range(0, len(data), 24):
            f.write('        ' + ','.join(f'0x{x:02x}' for x in data[i:i+24]) + ',\n')
        f.write('    };\n')
    f.write('\n    static const Card kCards[] = {\n')
    for cid, name, w, h, acc, data in png_bytes:
        f.write(f'        {{ "{name}", {w}, {h}, {acc[0]}, {acc[1]}, {acc[2]}, kPng_{name}, sizeof(kPng_{name}) }},\n')
    f.write('    };\n    constexpr int kCount = %d;\n}\n' % len(png_bytes))
