#pragma once
// Maximo OS - Navigation: pilha de telas usada pelo SISTEMA (historico de apps) e por cada APP (suas telas internas).
// Um unico mecanismo para todos: push/pop/clear. Logica pura (testada no PC).
#include <algorithm>
#include <vector>

namespace MaximoOS
{
	template <class T>
	class NavStack
	{
	public:
		void push(const T& v) { m_s.push_back(v); }
		bool pop() { if (m_s.empty()) return false; m_s.pop_back(); return true; }
		void clear() { m_s.clear(); }
		bool empty() const { return m_s.empty(); }
		int  depth() const { return (int)m_s.size(); }
		const T& top() const { return m_s.back(); }
		T& top() { return m_s.back(); }
		// pop ate o fundo (deixa 'keep' itens)
		void popTo(int keep) { while ((int)m_s.size() > keep) m_s.pop_back(); }
		const std::vector<T>& items() const { return m_s; }
	private:
		std::vector<T> m_s;
	};

	// Transicao leve entre duas telas (alpha + pequeno deslocamento). Sem alocacao, 160 ms.
	struct Transition
	{
		float t = 1.0f;                // 0..1 (1 = terminada)
		int   dir = 1;                 // +1 entrando (vem da direita), -1 voltando (vem da esquerda), 0 = so fade
		static constexpr float kDurationMs = 160.0f;
		void start(int d) { t = 0.0f; dir = d; }
		void update(float dtMs) { if (t < 1.0f) t = std::min(1.0f, t + dtMs / kDurationMs); }
		bool running() const { return t < 1.0f; }
		static float ease(float x) { return 1.0f - (1.0f - x) * (1.0f - x); }     // ease-out quadratico
		float alpha() const { return ease(t); }
		float offsetFrac() const { return (1.0f - ease(t)) * 0.06f * dir; }       // 6% da largura, no maximo
	};
}
