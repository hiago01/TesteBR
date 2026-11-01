#pragma once
// Maximo OS - InputManager: ponte ENTRE a thread do jogo (AND_TouchEvent_hook) E a thread de desenho, mais a captura de toque.
//
//  - Enquanto o tablet esta aberto, HandleRawTouch() devolve true para TODO evento: o GTA/SA-MP nao recebe nada.
//  - O dedo que comecou com o tablet aberto fica "reivindicado" ate o UP/CANCEL, mesmo que o tablet feche no meio do gesto
//    (nenhum botao do jogo fica preso, nenhum dedo "vaza" para o personagem/camera).
//  - Ao abrir, a primeira chamada de ConsumeFlushRequest() devolve true UMA vez: o hook solta os dedos que o jogo ja considerava
//    pressionados (AND_TouchEvent(1,i,0,0)) - mesmo esquema do MaximoMenu/Provador/Inventario.
//  - Os eventos vao para uma fila (mutex curto, maximo 256) e a thread de desenho os consome em Drain().
// Logica pura (std::mutex/atomic): testada no PC.
#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

namespace MaximoOS
{
	struct RawTouch { int type = 0, num = 0; float x = 0, y = 0; bool cancel = false; };

	class InputManager
	{
	public:
		void setOpen(bool open)
		{
			if (open)
			{
				{ std::lock_guard<std::mutex> l(m_mx); m_queue.clear(); }
				m_flush.store(true);
			}
			else
			{
				std::lock_guard<std::mutex> l(m_mx); m_queue.clear();    // dedos ja reivindicados continuam engolidos ate o UP
			}
			m_open.store(open);
		}
		bool isOpen() const { return m_open.load(); }

		// thread do jogo. type: 2 = push, 3 = move, 1 = pop. true = consumido (NAO repassar ao jogo).
		bool handleRaw(int type, int num, int x, int y, bool cancel)
		{
			const int n = num & 3;
			const bool open = m_open.load();
			if (!open && !m_claimed[n].load()) return false;           // fluxo normal do jogo
			if (open)
			{
				m_claimed[n].store(true);
				std::lock_guard<std::mutex> l(m_mx);
				if (m_queue.size() < 256) m_queue.push_back({ type, n, (float)x, (float)y, cancel });
			}
			if (type == 1) m_claimed[n].store(false);                  // pop/cancel: dedo solto
			return true;
		}

		bool consumeFlush() { return m_flush.exchange(false); }

		// thread de desenho
		void drain(std::vector<RawTouch>& out)
		{
			std::lock_guard<std::mutex> l(m_mx);
			out.insert(out.end(), m_queue.begin(), m_queue.end());
			m_queue.clear();
		}

		bool anyClaimed() const { for (auto& c : m_claimed) if (c.load()) return true; return false; }

	private:
		std::atomic<bool> m_open{ false }, m_flush{ false };
		std::atomic<bool> m_claimed[4] = { {false}, {false}, {false}, {false} };
		std::mutex m_mx;
		std::vector<RawTouch> m_queue;
	};
}
