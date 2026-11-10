#pragma once
// Celular - REDE: protocolo com o servidor (RPC 240 cliente->servidor, RPC 241 servidor->cliente; ver maximo_celular.inc).
// Logica pura (testada no PC). O adaptador (samp_widgets/phone.cpp) so entrega os pacotes recebidos e envia os pedidos.
//  - Thread do JOGO: onServerPacket() so interpreta e enfileira (fila com mutex, limitada).
//  - Thread de DESENHO: pump() aplica os eventos (mensagens recebidas, estado da ligacao) e expira pedidos sem resposta.
// Nada e simulado: sem resposta do servidor em 6 s o pedido FALHA com aviso; sem servico (nenhum pacote recebido) o envio/chamada
// retorna "indisponivel". O numero do remetente NUNCA e enviado: o servidor usa o da conta.
#include "PhoneData.h"
#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>

namespace Phone
{
	namespace Net
	{
		enum Op { OpNumber = 1, OpSend = 2, OpDial = 3, OpAnswer = 4, OpHangup = 5, OpGram = 6, OpPose = 7, OpChipOff = 8, OpDevice = 9, OpOpen = 10, OpMessage = 20, OpCall = 21, OpGramToken = 30 };
		enum Status
		{
			S_Ok = 0, S_NotLogged, S_Busy, S_NoLine, S_NoTarget, S_Empty, S_Long, S_Self, S_Rate, S_Full, S_BadArg, S_BadText,
			S_OutOfArea, S_TargetBusy, S_InCall, S_NoCall, S_Down, S_NoSpace,
			S_Timeout = 100       // so local: o servidor nao respondeu
		};
		constexpr int kTimeoutMs = 6000, kMaxQueue = 128, kMaxPending = 12;

		inline std::string StatusText(int s)
		{
			switch (s)
			{
			case S_NotLogged:  return "Faça login para usar o celular.";
			case S_Busy:       return "Servidor ocupado. Tente de novo.";
			case S_NoLine:     return "Sem chip: instale um chip (item do inventário) no celular.";
			case S_NoSpace:    return "Inventário cheio: libere espaço para receber o chip.";
			case S_NoTarget:   return "Esse número não existe.";
			case S_Empty:      return "Mensagem vazia.";
			case S_Long:       return "Mensagem muito longa (máx. 240 caracteres).";
			case S_Self:       return "Esse é o seu próprio número.";
			case S_Rate:       return "Muitas mensagens seguidas. Aguarde um pouco.";
			case S_Full:       return "A caixa do destinatário está cheia.";
			case S_BadArg:     return "Pedido inválido.";
			case S_BadText:    return "A mensagem tem caracteres inválidos.";
			case S_OutOfArea:  return "Fora de área ou desligado.";
			case S_TargetBusy: return "Ocupado.";
			case S_InCall:     return "Você já está em uma ligação.";
			case S_NoCall:     return "Nenhuma ligação em andamento.";
			case S_Down:       return "Serviço do celular fora do ar.";
			case S_Timeout:    return "Sem resposta do servidor.";
			default:           return "Erro do celular.";
			}
		}

		// Pedido (cliente -> servidor). O adaptador transforma em RPC 240.
		struct Request { int op = 0; uint16_t req = 0; std::string number, text; int arg = 0; };

		// Evento (servidor -> cliente), ja interpretado.
		struct Event
		{
			int op = 0, req = 0, status = 0;
			std::string number, text;       // OpNumber/OpMessage: numero (de quem enviou) e texto; OpCall: numero
			int callState = 0, reason = 0, secs = 0;
			bool hasPhone = false;          // OpDevice: o jogador tem um celular em uso (item do inventario); number = numero do CHIP (vazio = sem chip)
		};

		template <class BS> inline bool ReadBytes(BS& bs, size_t n, std::string& out)
		{
			out.assign(n, '\0');
			if (n && !bs.Read(&out[0], (unsigned)n)) return false;
			return true;
		}
		// Rejeita pacote truncado/absurdo. Nao altera nada se falhar.
		template <class BS> inline bool ParsePacket(BS& bs, Event& out)
		{
			Event e; uint8_t op = 0, st = 0; uint16_t req = 0;
			if (!bs.Read(op) || !bs.Read(req) || !bs.Read(st)) return false;
			e.op = op; e.req = req; e.status = st;
			uint8_t nl = 0;
			if ((op == OpNumber || op == OpGramToken) && st != S_Ok) { out = e; return true; }          // resposta de erro: so o status
			if (op == OpGramToken)
			{
				// token do Gram: [len u8][token][porta u16]. Token = so letras/numeros/ponto (vai na URL)
				uint8_t tl = 0; uint16_t port = 0;
				if (!bs.Read(tl) || tl < 20 || tl > 64 || !ReadBytes(bs, tl, e.text) || !bs.Read(port) || port < 1024) return false;
				for (char c : e.text) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || c == '.')) return false;
				e.secs = port; out = e; return true;
			}
			if (op == OpOpen) { out = e; return true; }                          // servidor pede para abrir/fechar o celular (usou o item)
			if (op == OpDevice)
			{
				uint8_t hp = 0;
				if (st != S_Ok) { out = e; return true; }
				if (!bs.Read(hp) || !bs.Read(nl) || (nl != 0 && nl != 7) || !ReadBytes(bs, nl, e.number)) return false;
				for (char c : e.number) if (c < '0' || c > '9') return false;
				e.hasPhone = hp != 0; out = e; return true;
			}
			if (op == OpNumber || op == OpMessage || op == OpCall)
			{
				if (op == OpCall)
				{
					uint8_t cs = 0, rs = 0;
					if (!bs.Read(cs) || !bs.Read(rs) || cs > 3 || rs > 5) return false;
					e.callState = cs; e.reason = rs;
				}
				if (!bs.Read(nl) || nl > 16 || !ReadBytes(bs, nl, e.number)) return false;
				for (char c : e.number) if (c < '0' || c > '9') return false;
				if (op == OpMessage)
				{
					uint16_t tl = 0;
					if (!bs.Read(tl) || tl == 0 || tl > Data::kMsgStore || !ReadBytes(bs, tl, e.text)) return false;
					if (!Text::ValidUtf8(e.text)) return false;
				}
				if (op == OpCall) { uint16_t secs = 0; if (!bs.Read(secs)) return false; e.secs = secs; }
				if (op != OpCall && e.number.size() != 7) return false;
			}
			else if ((op < OpNumber || op > OpHangup) && op != OpChipOff) return false;
			out = e;
			return true;
		}
	}

	class PhoneNet
	{
	public:
		// --- ligacoes com o resto do sistema (preenchidas pelo adaptador / testes) ---
		std::function<bool(const Net::Request&)> send;                                            // envia o pedido (thread de desenho)
		std::function<void(const std::string& title, const std::string& text, const std::string& appId, int level)> notify;
		std::function<void(const std::string& number)> incomingCall;                             // abre o celular na tela de ligacao
		std::function<void()> openRequest;                                                        // o item celular foi usado (inventario/slot rapido): alternar a tela
		std::function<void(bool hasPhone)> deviceChanged;                                         // o aparelho em uso mudou / trocou de chip (resetar Gram e Loja)
		std::function<void(int status, const std::string& token, int port)> gramToken;          // resposta do pedido de token do Gram (thread de desenho)

		void attach(PhoneContext* c)
		{
			m_c = c;
			c->dial = [this](const std::string& n) { return dial(n); };
			c->sendMessage = [this](const std::string& n, const std::string& t) { return sendMessage(n, t); };
			c->answer = [this]() { answer(); };
			c->hangup = [this]() { hangup(); };
		}

		// ---------------- thread do JOGO ----------------
		void onServerPacket(const Net::Event& e)
		{
			std::lock_guard<std::mutex> l(m_mx);
			if (m_q.size() < (size_t)Net::kMaxQueue) m_q.push_back(e);
		}
		template <class BS> void onServerPacketBS(BS& bs) { Net::Event e; if (Net::ParsePacket(bs, e)) onServerPacket(e); }
		void connectionReset() { m_reset.store(true); }

		// ---------------- thread de DESENHO ----------------
		void pump(int64_t now)
		{
			if (!m_c) return;
			if (m_reset.exchange(false)) doReset();
			std::deque<Net::Event> q;
			{ std::lock_guard<std::mutex> l(m_mx); q.swap(m_q); }
			for (auto& e : q) apply(e, now);
			// pedidos sem resposta
			for (size_t i = 0; i < m_pend.size();)
			{
				if (now - m_pend[i].sentMs >= Net::kTimeoutMs) { Pending p = m_pend[i]; m_pend.erase(m_pend.begin() + i); fail(p, Net::S_Timeout, now); }
				else ++i;
			}
			// microfone da ligacao: aberto sozinho (viva-voz) ou enquanto segura o botao; nunca fora de uma ligacao com voz
			{
				const PhoneCall& pc = m_c->call;
				const bool want = pc.state == 3 && pc.voice && ((m_c->settings.handsFree && !pc.muted) || (!m_c->settings.handsFree && pc.held));
				if (want != m_talkSent) { m_talkSent = want; if (m_c->talk) m_c->talk(want); }
			}
			// tela "encerrada" some sozinha
			PhoneCall& c = m_c->call;
			if (c.state == 0 && !c.endText.empty() && now >= c.endAtMs) c.endText.clear();
		}
		// pede o numero (ao abrir o celular); no maximo 1 a cada 8 s
		void requestNumber(int64_t now)
		{
			if (!m_c || !m_c->myNumber.empty() || !send) return;
			if (m_lastHello && now - m_lastHello < 8000) return;
			m_lastHello = now;
			Net::Request r; r.op = Net::OpNumber; r.req = nextReq();
			send(r);
		}

		// pede o token do Gram (rede social); no maximo 1 a cada 6 s. false = nao enviou (sem rede / muito cedo)
		bool requestGramToken(int64_t now)
		{
			if (!m_c || !send || !m_c->networked) return false;           // Gram/Loja funcionam SEM chip (so precisam do celular; o servidor recusa se nao houver)
			if (m_lastGram && now - m_lastGram < 6000) return false;
			m_lastGram = now;
			Net::Request r; r.op = Net::OpGram; r.req = nextReq();
			return send(r);
		}

		// tira o chip do celular em uso (volta ao inventario). "" = pedido enviado (a resposta aparece em ctx.chipMsg); senao o erro
		std::string removeChip()
		{
			if (!m_c->networked) return "Serviço do celular indisponível.";
			if (m_c->myNumber.empty()) return "O celular não tem chip.";
			if (m_c->call.state != 0) return "Encerre a ligação antes.";
			Net::Request r; r.op = Net::OpChipOff; r.req = nextReq();
			if (!send || !send(r)) return "Sem conexão com o servidor.";
			addPending(r, m_c->nowMs);
			return std::string();
		}

		// pose do corpo (0 = nenhuma, 1 = celular na mao, 2 = foto (camera traseira), 3 = selfie). O servidor sincroniza com quem esta perto.
		bool sendPose(int mode)
		{
			if (!send) return false;
			Net::Request r; r.op = Net::OpPose; r.req = nextReq(); r.arg = mode;
			return send(r);
		}

		// ---------------- acoes dos apps ----------------
		std::string dial(const std::string& number)
		{
			if (!m_c->networked) return "Serviço de chamadas indisponível neste servidor.";
			if (m_c->myNumber.empty()) return "Sem chip: instale um chip no celular.";
			if (m_c->settings.airplane) return "Modo avião ativado.";
			if (m_c->call.state != 0) return "Você já está em uma ligação.";
			const std::string n = Data::CleanNumber(number);
			if (!validNumber(n)) return "Número inválido (7 dígitos).";
			if (n == m_c->myNumber) return "Esse é o seu próprio número.";
			Net::Request r; r.op = Net::OpDial; r.req = nextReq(); r.number = n;
			if (!send || !send(r)) return "Sem conexão com o servidor.";
			addPending(r, m_c->nowMs);
			PhoneCall& c = m_c->call;
			c = PhoneCall(); c.state = 1; c.number = n;
			return std::string();
		}
		std::string sendMessage(const std::string& number, const std::string& text)
		{
			if (!m_c->networked) return "Serviço de mensagens indisponível neste servidor.";
			if (m_c->myNumber.empty()) return "Sem chip: instale um chip no celular.";
			if (m_c->settings.airplane) return "Modo avião ativado.";
			const std::string n = Data::CleanNumber(number);
			if (!validNumber(n)) return "Número inválido (7 dígitos).";
			if (n == m_c->myNumber) return "Esse é o seu próprio número.";
			if (text.empty() || text.size() > Data::kMsgMax) return "Mensagem muito longa.";
			if (m_pend.size() >= (size_t)Net::kMaxPending) return "Aguarde: há mensagens sendo enviadas.";
			if (m_lastSend && m_c->nowMs - m_lastSend < 400) return "Aguarde um instante.";
			Net::Request r; r.op = Net::OpSend; r.req = nextReq(); r.number = n; r.text = text;
			if (!send || !send(r)) return "Sem conexão com o servidor.";
			m_lastSend = m_c->nowMs;
			addPending(r, m_c->nowMs);
			return std::string();
		}
		void answer()
		{
			if (m_c->call.state != 2) return;
			Net::Request r; r.op = Net::OpAnswer; r.req = nextReq();
			if (send && send(r)) addPending(r, m_c->nowMs);
		}
		void hangup()
		{
			if (m_c->call.state == 0) return;
			Net::Request r; r.op = Net::OpHangup; r.req = nextReq();
			if (send) send(r);
			if (m_c->call.state == 1 && !m_c->call.confirmed) endCall(m_c->nowMs, "Chamada cancelada", 0);   // cancelou antes do servidor responder
		}

		size_t pendingCount() const { return m_pend.size(); }

	private:
		struct Pending { uint16_t req; int op; int64_t sentMs; std::string number, text; };
		PhoneContext* m_c = nullptr;
		std::mutex m_mx; std::deque<Net::Event> m_q; std::atomic<bool> m_reset{ false };
		std::vector<Pending> m_pend;
		uint16_t m_req = 0; int64_t m_lastHello = 0, m_lastSend = 0, m_lastGram = 0; bool m_talkSent = false;

		static bool validNumber(const std::string& n)
		{
			if (n.size() != 7) return false;
			for (char c : n) if (c < '0' || c > '9') return false;
			return true;
		}
		uint16_t nextReq() { if (++m_req == 0) ++m_req; return m_req; }
		void addPending(const Net::Request& r, int64_t now)
		{
			if (m_pend.size() >= 32) m_pend.erase(m_pend.begin());
			m_pend.push_back({ r.req, r.op, now, r.number, r.text });
		}
		void doReset()
		{
			m_pend.clear(); m_c->networked = false; m_c->myNumber.clear(); m_c->call = PhoneCall(); m_lastHello = 0; m_lastGram = 0;
			{ std::lock_guard<std::mutex> l(m_mx); m_q.clear(); }
		}
		std::string label(const std::string& number) const
		{
			const std::string nm = Data::NameFor(Data::LoadContacts(*m_c), number);
			return nm.empty() ? number : nm;
		}
		void fail(const Pending& p, int status, int64_t now)
		{
			if (p.op == Net::OpSend)
			{
				Data::MarkOutgoing(*m_c, p.number, p.text, 2); m_c->changed();
				if (notify) notify("Mensagem não enviada", Net::StatusText(status), "mensagens", 1);
			}
			else if (p.op == Net::OpDial)
			{
				Data::AddCall(*m_c, p.number, 0); m_c->changed();
				endCall(now, Net::StatusText(status), 0);
			}
			else if (p.op == Net::OpAnswer && status != Net::S_Ok) endCall(now, Net::StatusText(status), 0);
			else if (p.op == Net::OpChipOff) { m_c->chipMsg = Net::StatusText(status); m_c->chipMsgAt = now; }
		}
		void endCall(int64_t now, const std::string& text, int)
		{
			PhoneCall& c = m_c->call;
			c = PhoneCall(); c.endText = text; c.endAtMs = now + 1800;
		}
		void apply(const Net::Event& e, int64_t now)
		{
			using namespace Net;
			switch (e.op)
			{
			case OpNumber: m_c->networked = true; if (e.status == S_Ok) m_c->myNumber = e.number; break;
			case OpOpen: m_c->networked = true; if (openRequest) openRequest(); break;
			case OpDevice:
				m_c->networked = true;
				if (e.status == S_Ok) { m_c->myNumber = e.number; if (deviceChanged) deviceChanged(e.hasPhone); }
				break;
			case OpMessage:
			{
				m_c->networked = true;
				Data::ReceiveMessage(*m_c, e.number, e.text); m_c->changed();
				if (notify) notify(label(e.number), e.text, "mensagens", 0);
				break;
			}
			case OpCall: m_c->networked = true; applyCall(e, now); break;
			case OpGramToken: m_c->networked = true; if (gramToken) gramToken(e.status, e.text, e.secs); break;
			default: applyReply(e, now); break;
			}
		}
		void applyReply(const Net::Event& e, int64_t now)
		{
			using namespace Net;
			m_c->networked = true;
			Pending p; bool found = false;
			for (size_t i = 0; i < m_pend.size(); ++i) if (m_pend[i].req == e.req && m_pend[i].op == e.op) { p = m_pend[i]; m_pend.erase(m_pend.begin() + i); found = true; break; }
			if (e.op == OpNumber) { /* erro "tente de novo": o HELLO sera repetido ao reabrir */ return; }
			if (!found) return;
			if (e.op == OpSend)
			{
				if (e.status == S_Ok) { Data::MarkOutgoing(*m_c, p.number, p.text, 0); m_c->changed(); }
				else fail(p, e.status, now);
			}
			else if (e.op == OpDial)
			{
				if (e.status == S_Ok) { if (m_c->call.state == 1) m_c->call.confirmed = true; Data::AddCall(*m_c, p.number, 0); m_c->changed(); }
				else fail(p, e.status, now);
			}
			else if (e.op == OpAnswer && e.status != S_Ok) endCall(now, StatusText(e.status), 0);
			else if (e.op == OpChipOff) { m_c->chipMsg = e.status == S_Ok ? "Chip removido: está no inventário." : StatusText(e.status); m_c->chipMsgAt = now; }
			// OpHangup: o estado final vem pelo evento de ligacao
		}
		void applyCall(const Net::Event& e, int64_t now)
		{
			PhoneCall& c = m_c->call;
			switch (e.callState)
			{
			case 1: if (c.state == 1) { c.state = 1; c.number = e.number; c.confirmed = true; c.incoming = false; c.endText.clear(); } break;
			case 2:
			{
				if (m_c->settings.airplane) { Net::Request r; r.op = Net::OpHangup; r.req = nextReq(); if (send) send(r); break; }   // modo aviao: recusa sem tocar
				c = PhoneCall(); c.state = 2; c.number = e.number; c.incoming = true; c.confirmed = true;
				if (incomingCall) incomingCall(e.number);
				break;
			}
			case 3: if (c.state != 0) { c.state = 3; c.startMs = now; c.number = e.number; c.confirmed = true; c.voice = (e.reason == 1); } break;
			default:    // 0 = encerrada
			{
				const int prev = c.state; const bool incoming = c.incoming; const std::string num = c.number.empty() ? e.number : c.number;
				static const char* T[6] = { "Chamada encerrada", "Chamada encerrada", "Chamada recusada", "Sem resposta", "Chamada encerrada", "O outro jogador saiu" };
				if (prev == 0 && c.endText.size()) break;                       // ja encerrada localmente
				if (incoming && prev == 2)                                      // tocou e nao foi atendida: perdida
				{
					Data::AddCall(*m_c, num, 2); m_c->changed();
					if (e.reason != 4 && notify) notify("Chamada perdida", label(num), "telefone", 1);
				}
				else if (incoming && prev == 3) { Data::AddCall(*m_c, num, 1); m_c->changed(); }
				if (prev != 0 || e.reason != 0) endCall(now, T[e.reason % 6], e.reason);
				break;
			}
			}
		}
	};
}
