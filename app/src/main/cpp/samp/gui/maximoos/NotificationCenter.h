#pragma once
// Maximo OS - NotificationCenter: fila de notificacoes do tablet (sem dados reais na etapa 1; os apps/servidor chamam post()).
#include <cstdint>
#include <string>
#include <vector>

namespace MaximoOS
{
	struct Notification
	{
		enum Level { Info, Warn, Alert } level = Info;
		std::string title, text, appId;   // appId: app aberto ao tocar (vazio = so fecha)
		int64_t     time = 0;             // ms (relogio do OS)
		bool        read = false;
		int         id = 0;
	};

	class NotificationCenter
	{
	public:
		static constexpr size_t kMax = 50;
		static constexpr int64_t kToastMs = 3500;

		int post(const std::string& title, const std::string& text, const std::string& appId, int64_t now, Notification::Level lv = Notification::Info)
		{
			Notification n; n.title = title; n.text = text; n.appId = appId; n.time = now; n.level = lv; n.id = ++m_next;
			m_list.insert(m_list.begin(), n);
			if (m_list.size() > kMax) m_list.pop_back();
			m_toastId = n.id; m_toastUntil = now + kToastMs;
			return n.id;
		}
		const std::vector<Notification>& list() const { return m_list; }
		int unread() const { int c = 0; for (auto& n : m_list) if (!n.read) ++c; return c; }
		int unreadFor(const std::string& appId) const { int c = 0; for (auto& n : m_list) if (!n.read && n.appId == appId) ++c; return c; }
		void markAllRead() { for (auto& n : m_list) n.read = true; }
		void markRead(int id) { for (auto& n : m_list) if (n.id == id) n.read = true; }
		void dismiss(int id) { for (size_t i = 0; i < m_list.size(); ++i) if (m_list[i].id == id) { m_list.erase(m_list.begin() + i); break; } if (m_toastId == id) m_toastId = 0; }
		void clear() { m_list.clear(); m_toastId = 0; }
		// notificacao em destaque (toast) ainda valida em 'now', ou nullptr
		const Notification* toast(int64_t now) const
		{
			if (!m_toastId || now >= m_toastUntil) return nullptr;
			for (auto& n : m_list) if (n.id == m_toastId) return &n;
			return nullptr;
		}
		void dismissToast() { m_toastId = 0; }
	private:
		std::vector<Notification> m_list;
		int m_next = 0, m_toastId = 0; int64_t m_toastUntil = 0;
	};
}
