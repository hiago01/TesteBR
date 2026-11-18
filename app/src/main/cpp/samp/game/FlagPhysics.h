#pragma once
// =====================================================================================
//  FlagPhysics - fisica de tecido (Verlet + molas/restricoes) da bandeira 3D do Maximo RP.
//
//  Modulo PURO: nao inclui nada do jogo/RenderWare (compila no PC para teste unitario).
//  A malha de simulacao e UMA folha de CxR particulas (a mesma grade do tecido do flag_maximo.dff);
//  a frente e o verso sao desenhados a partir dela (ver Flag.cpp).
//
//  Simulacao em coordenadas de MUNDO. O quadro local da bandeira tem o mastro no eixo Z (x=y=0)
//  e o tecido em +X (plano XZ). O quadro gira em torno do mastro (yaw) como um cata-vento.
//
//  - Integracao Verlet com passo FIXO (kStepDt) e no maximo kMaxSubsteps por quadro: o tempo que
//    sobra e descartado (nada de espiral da morte em FPS baixo) e o resultado nao depende do FPS.
//  - Restricoes de distancia: horizontais, verticais e diagonais (cisalhamento), Gauss-Seidel,
//    mais passe final de limite de estiramento (maxStretch).
//  - Pinos por mascara: coluna 0 fixa no mastro, colunas 1 e 2 "macias" (puxadas para a posicao
//    de repouso), para o tecido nao dobrar de forma feia junto ao mastro.
//  - Vento: direcao + intensidade; velocidade com rajadas senoidais e fase que viaja pelo tecido
//    (fase diferente por particula), componente lateral/vertical de turbulencia. Forca
//    aerodinamica por triangulo sobre a velocidade RELATIVA (arrasto de pressao na normal +
//    arrasto tangencial leve) -> amortece sozinha.
//  - Colisao: cilindro do mastro e piso da base (so empurra para fora).
//  - Guarda anti-NaN / anti-explosao: se algo sair do limite, volta ao repouso e conta em stats.
// =====================================================================================

#include <cstdint>
#include <vector>

namespace MxFlag
{
	struct Vec3 { float x = 0, y = 0, z = 0; };

	constexpr float kStepDt       = 1.0f / 60.0f;   // passo fixo da simulacao
	constexpr int   kMaxSubsteps  = 4;              // maximo de passos por quadro (limita custo)
	constexpr float kMaxFrameDt   = 0.10f;          // dt real acima disso e cortado (pausa/loading)

	struct PhysicsParams
	{
		float gravity      = 9.81f;
		float damping      = 0.985f;   // fator de velocidade por passo (Verlet)
		int   iterations   = 8;        // iteracoes de restricao por passo (a distancia reduz: ver Flag.cpp)
		bool  bending      = true;     // restricoes de flexao (mais caras; desligadas nos LOD distantes)
		float maxStretch   = 1.06f;    // limite de estiramento (1.06 = 6% sobre o comprimento de repouso)
		float pressureK    = 2.0f;     // 0.5*rho/sigma efetivo (arrasto de pressao na normal)
		float tangentK     = 0.12f;    // arrasto tangencial
		float baseSpeed    = 6.0f;     // m/s com intensidade 1.0
		float gustAmp      = 0.28f;    // amplitude relativa das rajadas (0..1)
		float flutterAmp   = 0.55f;    // amplitude da turbulencia transversal (relativa a velocidade)
		float yawStiffness = 2.2f;     // mola do cata-vento (rad/s^2 por rad)
		float yawDamping   = 2.4f;     // amortecimento do cata-vento
		float mastRadius   = 0.034f;   // raio de colisao do mastro (raio real 0.022 + folga)
		float floorZ       = 0.20f;    // piso (altura da base) acima do pe do mastro
	};

	struct Stats
	{
		int      lastSubsteps = 0;
		uint32_t resets       = 0;       // quantas vezes a guarda anti-NaN/explosao disparou
		float    maxStretchSeen = 1.0f;  // maior (comprimento / repouso) entre as arestas, no ultimo passo
	};

	class CFlagPhysics
	{
	public:
		// rest = posicoes de repouso no quadro local (mastro em x=y=0), cols*rows particulas, linha a linha.
		bool Init(const Vec3* rest, int cols, int rows, uint32_t seed);
		bool Ready() const { return m_cols > 0; }

		void SetTransform(const Vec3& basePos, float yaw);      // pe do mastro (mundo) + giro inicial (rad)
		void SetWind(float dirRad, float intensity);            // dirRad = para ONDE o vento sopra (rad, 0 = +X, anti-horario)
		void SetEnabled(bool on) { m_enabled = on; }
		bool Enabled() const { return m_enabled; }
		void Reset();                                           // volta ao repouso (pendurada, sem velocidade)

		void Update(float dtReal);                              // chamado por quadro; faz 0..kMaxSubsteps passos fixos
		void Step();                                            // 1 passo fixo (exposto para os testes)

		int          Count() const    { return m_cols * m_rows; }
		int          Cols() const     { return m_cols; }
		int          Rows() const     { return m_rows; }
		const Vec3*  Positions() const { return m_p.data(); }
		float        Yaw() const      { return m_yaw; }
		float        SimTime() const  { return m_time; }
		float        PinWeight(int i) const { return m_pin[i]; }
		const std::vector<uint32_t>& Tris() const { return m_tris; }   // 3 indices por triangulo (so a folha de simulacao)

		// posicoes para DESENHO (extrapola o resto do tempo entre passos fixos: sem tremor em FPS alto); out tem Count()
		void GetRenderPositions(Vec3* out) const;
		// normais por particula (media das faces, pesada pela area) para o desenho; out tem Count() elementos
		void ComputeNormals(const Vec3* pos, Vec3* out) const;

		PhysicsParams params;
		Stats         stats;

	private:
		struct Edge { uint16_t a, b; float rest; uint8_t kind; };

		void   buildEdges();
		void   target(int i, Vec3& out) const;                  // posicao de repouso da particula i no mundo, com yaw atual
		void   sampleWind(Vec3* out) const;
		void   integrate(const Vec3* wind);
		void   solve();
		bool   sane() const;

		int    m_cols = 0, m_rows = 0;
		bool   m_enabled = true;
		std::vector<Vec3>     m_rest;   // local
		std::vector<Vec3>     m_p, m_prev;
		std::vector<float>    m_pin;    // 1 = fixa, 0 = livre, entre = macia
		std::vector<float>    m_ph;     // fase por particula (travelling wave)
		std::vector<Edge>     m_edges;
		int                   m_nonBend = 0;   // as primeiras m_nonBend arestas sao estruturais/cisalhamento
		std::vector<uint32_t> m_tris;
		std::vector<Vec3>     m_wind;   // buffer de trabalho
		Vec3   m_base{};
		float  m_yaw = 0, m_yawVel = 0;
		float  m_windDir = 0, m_windInt = 1.0f;
		float  m_time = 0;
		float  m_acc = 0;
		float  m_seedPh[4] = {0, 0, 0, 0};
	};
}
