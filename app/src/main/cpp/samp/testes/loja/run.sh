#!/bin/sh
# Teste de PC da loja do cliente (nao compila o cliente Android): compila shoppanel.cpp com um renderer falso e roda os testes;
# se o Pillow existir, gera os PNG de pre-visualizacao em $SH_OUT (padrao /tmp/sh_out).
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"; SAMP="$HERE/../.."
T=$(mktemp -d); mkdir -p "$T/gui/samp_widgets" "$T/vendor/raknet"
cp "$SAMP/gui/samp_widgets/shoppanel.cpp" "$SAMP/gui/samp_widgets/shoppanel.h" "$T/gui/samp_widgets/"
cp "$SAMP/gui/imguirenderer.h" "$T/gui/"
ln -s "$SAMP/vendor/imgui" "$T/vendor/imgui"
cat > "$T/main.h" <<'EOS'
#pragma once
#include <cstdint>
void FLog(const char* fmt, ...);
EOS
cat > "$T/vendor/raknet/BitStream.h" <<'EOS'
#pragma once
#include <cstdint>
#include <cstring>
namespace RakNet {
class BitStream {
public:
	BitStream(const uint8_t* d, int n) : m_d(d), m_n(n), m_p(0) {}
	template <class T> bool Read(T& v) { if (m_p + (int)sizeof(T) > m_n) return false; memcpy(&v, m_d + m_p, sizeof(T)); m_p += (int)sizeof(T); return true; }
	bool Read(char* p, unsigned n) { if (m_p + (int)n > m_n) return false; memcpy(p, m_d + m_p, n); m_p += (int)n; return true; }
private:
	const uint8_t* m_d; int m_n, m_p;
};
}
EOS
cat > "$T/gui/samp_widgets/notafiscal.h" <<'EOS'
#pragma once
#include "../imguirenderer.h"
// stub de teste: no jogo vem de notafiscal.cpp (tile azul + glifo branco)
namespace InvoicePanel { inline void DrawCompanyIcon(ImGuiRenderer* r, int icon, float x, float y, float size, int alpha) { r->drawRoundedRect(ImVec2(x, y), ImVec2(x + size, y + size), ImColor(37, 109, 255, alpha), 7.0f, true); } }
EOS
mkdir -p "${SH_OUT:=/tmp/sh_out}"; export SH_OUT
g++ -std=gnu++17 -Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations -I"$T" -o "$T/sh" -x c++ "$HERE/shop_main.test" -x none "$T/gui/samp_widgets/shoppanel.cpp"
"$T/sh"
if python3 -c "import PIL" 2>/dev/null; then python3 "$HERE/render.py" "$SH_OUT"; fi
rm -rf "$T"
