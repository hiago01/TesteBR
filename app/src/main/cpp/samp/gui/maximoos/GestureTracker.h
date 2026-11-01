#pragma once
// Maximo OS - reconhecimento de gestos (um dedo): toque, segurar, arrastar (com velocidade) e soltar.
// Logica pura (sem ImGui/jogo), testada no PC. Entrada: eventos crus no formato do hook do jogo (1 = pop, 2 = push, 3 = move).
#include "Types.h"
#include <cstdint>
#include <vector>

namespace MaximoOS
{
	struct Touch
	{
		enum Kind { Down, Move, Up, Cancel, Tap, LongPress, DragBegin, Drag, DragEnd } kind = Down;
		Vec2 pos;          // posicao atual
		Vec2 start;        // onde o dedo encostou
		Vec2 delta;        // deslocamento desde o evento anterior (Drag)
		Vec2 velocity;     // px/s, so em DragEnd
		int64_t t = 0;
	};

	class GestureTracker
	{
	public:
		float slop = 12.0f;             // px de tolerancia antes de virar arrasto (Metrics::px(...) ajusta)
		int   longPressMs = 450;

		// type: 2 = push, 3 = move, 1 = pop; cancel = veio de ACTION_CANCEL. Apenas o PRIMEIRO dedo e rastreado.
		void feed(int type, int num, float x, float y, bool cancel, int64_t now, std::vector<Touch>& out)
		{
			if (type == 2)
			{
				if (m_finger >= 0) return;                       // ja ha um dedo: os outros sao ignorados pela UI
				m_finger = num; m_start = m_pos = m_last = { x, y }; m_t0 = m_lastT = now;
				m_drag = false; m_lp = false; m_vel = {};
				push(out, Touch::Down, now, 0);
			}
			else if (type == 3)
			{
				if (num != m_finger) return;
				m_pos = { x, y };
				const float dx = m_pos.x - m_start.x, dy = m_pos.y - m_start.y;
				if (!m_drag && dx * dx + dy * dy > slop * slop)
				{
					m_drag = true; m_last = m_start;
					push(out, Touch::DragBegin, now, 0);
				}
				if (m_drag)
				{
					Vec2 d{ m_pos.x - m_last.x, m_pos.y - m_last.y };
					const float dt = std::max(1.0f, (float)(now - m_lastT)) / 1000.0f;
					const float a = 0.35f;                          // filtro simples da velocidade
					m_vel = { m_vel.x * (1 - a) + (d.x / dt) * a, m_vel.y * (1 - a) + (d.y / dt) * a };
					m_last = m_pos; m_lastT = now;
					Touch t = make(Touch::Drag, now); t.delta = d; out.push_back(t);
				}
				push(out, Touch::Move, now, 0);
			}
			else if (type == 1)
			{
				if (num != m_finger) return;
				m_pos = { x, y };
				if (cancel) { push(out, Touch::Cancel, now, 0); }
				else if (m_drag)
				{
					Touch t = make(Touch::DragEnd, now); t.velocity = m_vel; out.push_back(t);
					push(out, Touch::Up, now, 0);
				}
				else
				{
					push(out, Touch::Up, now, 0);
					if (!m_lp) push(out, Touch::Tap, now, 0);       // segurar longo nao vira toque
				}
				m_finger = -1; m_drag = false; m_lp = false;
			}
		}

		// chamar todo quadro: dispara o "segurar" quando o dedo parado passa de longPressMs
		void tick(int64_t now, std::vector<Touch>& out)
		{
			if (m_finger >= 0 && !m_drag && !m_lp && now - m_t0 >= longPressMs) { m_lp = true; push(out, Touch::LongPress, now, 0); }
		}

		bool active() const { return m_finger >= 0; }
		bool dragging() const { return m_drag; }
		void reset() { m_finger = -1; m_drag = false; m_lp = false; }

	private:
		Touch make(Touch::Kind k, int64_t now) const { Touch t; t.kind = k; t.pos = m_pos; t.start = m_start; t.t = now; return t; }
		void push(std::vector<Touch>& out, Touch::Kind k, int64_t now, int) const { out.push_back(make(k, now)); }
		int m_finger = -1; Vec2 m_start, m_pos, m_last, m_vel; int64_t m_t0 = 0, m_lastT = 0; bool m_drag = false, m_lp = false;
	};
}
