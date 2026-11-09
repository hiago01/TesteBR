#pragma once
// Celular - PhoneInput: ponte entre a thread do jogo (AND_TouchEvent_hook) e a thread de desenho, com captura de toque POR REGIAO.
//  - Um dedo que COMECA (push) dentro da moldura do aparelho fica reivindicado ate o UP/CANCEL: o GTA/SA-MP nao recebe push, move nem pop dele.
//  - Um dedo que comeca FORA do aparelho segue para o jogo normalmente (joystick e camera continuam funcionando com o celular aberto).
//  - Se o celular fechar no meio do gesto, o dedo reivindicado continua engolido ate o UP (nenhum botao do jogo fica preso).
//  - Eventos vao para uma fila (mutex curto, maximo 256); a thread de desenho consome em drain().
#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

namespace Phone
{
	struct RawTouch { int type = 0, num = 0; float x = 0, y = 0; bool cancel = false; };

	class PhoneInput
	{
	public:
		void setOpen(bool open)
		{
			if (!open) { std::lock_guard<std::mutex> l(m_mx); m_queue.clear(); }
			m_open.store(open);
		}
		bool isOpen() const { return m_open.load(); }
		// retangulo (pixels) que captura toques novos; atualizado pela thread de desenho a cada quadro
		void setCaptureRect(float x, float y, float w, float h) { m_x.store(x); m_y.store(y); m_w.store(w); m_h.store(h); }

		// thread do jogo. type: 2 = push, 3 = move, 1 = pop. true = consumido (NAO repassar ao jogo).
		bool handleRaw(int type, int num, int x, int y, bool cancel)
		{
			const int n = num & 3;
			if (type == 2)
			{
				if (!m_open.load() || !insideCapture((float)x, (float)y)) return false;
				m_claimed[n].store(true);
			}
			else if (!m_claimed[n].load()) return false;

			if (m_open.load())
			{
				std::lock_guard<std::mutex> l(m_mx);
				if (m_queue.size() < 256) m_queue.push_back({ type, n, (float)x, (float)y, cancel });
			}
			if (type == 1) m_claimed[n].store(false);                  // pop/cancel: dedo solto
			return true;
		}
		void drain(std::vector<RawTouch>& out)
		{
			std::lock_guard<std::mutex> l(m_mx);
			out.insert(out.end(), m_queue.begin(), m_queue.end());
			m_queue.clear();
		}
		bool anyClaimed() const { for (auto& c : m_claimed) if (c.load()) return true; return false; }
	private:
		bool insideCapture(float x, float y) const
		{
			const float cx = m_x.load(), cy = m_y.load(), cw = m_w.load(), ch = m_h.load();
			return cw > 0 && ch > 0 && x >= cx && x < cx + cw && y >= cy && y < cy + ch;
		}
		std::atomic<bool> m_open{ false };
		std::atomic<float> m_x{ 0 }, m_y{ 0 }, m_w{ 0 }, m_h{ 0 };
		std::atomic<bool> m_claimed[4] = { {false}, {false}, {false}, {false} };
		std::mutex m_mx;
		std::vector<RawTouch> m_queue;
	};
}
