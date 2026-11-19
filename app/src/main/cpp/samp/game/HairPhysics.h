#pragma once
// =====================================================================================
//  HairPhysics - fisica de tecido do CABELO LONGO (hair6) do Maximo RP.
//
//  Modulo PURO: nao inclui nada do jogo/RenderWare (compila no PC para teste unitario).
//  Uma folha de cols x rows particulas (cols = mechas ao redor da cabeca; rows = ao longo do comprimento).
//  A linha 0 (raiz) fica presa ao couro cabeludo e SEGUE a cabeca (osso 8) a cada quadro; o resto e Verlet:
//    - passo FIXO (kStepDt), no maximo kMaxSubsteps por quadro, resto descartado (sem espiral em FPS baixo);
//    - restricoes: comprimento (mecha), laterais (folha), flexao minima (nao dobra sobre si), limite de estiramento;
//    - linhas 1 e 2 "macias" (puxadas para o repouso) -> volume junto ao couro cabeludo;
//    - vento (direcao + intensidade, rajadas, ondas que viajam raiz->ponta) por pressao na normal + arrasto tangencial,
//      sobre a velocidade RELATIVA (amortece sozinho); inercia do movimento da cabeca vem do proprio Verlet;
//    - colisao: esfera da cabeca e cilindro eliptico do tronco/ombros (so empurra para fora; o tronco desliga dentro de veiculo);
//    - guarda: NaN / explosao / teleporte da ancora -> volta ao repouso e conta em stats.resets.
//  Toda posicao e em coordenadas de MUNDO. O quadro do ped (right/fwd/up) vem do jogo (CPlaceable::GetRight/Forward/Up).
// =====================================================================================
#include <cstdint>

namespace MxHair
{
	struct V3 { float x = 0, y = 0, z = 0; };
	inline V3    operator+(V3 a, V3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
	inline V3    operator-(V3 a, V3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
	inline V3    operator*(V3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }
	inline float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
	inline V3    Cross(V3 a, V3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }

	constexpr float kStepDt      = 1.0f / 60.0f;
	constexpr int   kMaxSubsteps = 3;
	constexpr float kMaxFrameDt  = 0.10f;
	constexpr int   kMaxCols     = 36;
	constexpr int   kMaxRows     = 12;
	constexpr int   kMaxP        = kMaxCols * kMaxRows;

	// Ajuste GLOBAL (vale para todos os cabelos; o comando /cabelo6 mexe nisto ao vivo, sem recompilar).
	struct Tune
	{
		float skullUp     = 0.075f;   // centro do cranio acima do osso 8 (m, ao longo do "up" do ped)
		float skullR      = 0.105f;   // raio do cranio (m)
		float rootElevDeg = 22.0f;    // elevacao das raizes na esfera do cranio (0 = altura das orelhas)
		float rootRadius  = 0.112f;   // raio onde ficam as raizes (um pouco acima da casca do cabelo, que o DFF desenha)
		float skin        = 0.012f;   // espessura extra (cabelo nao encosta no cranio/tronco)
		float bodyA       = 0.200f;   // semi-eixo do tronco: lateral (m)
		float bodyB       = 0.135f;   // semi-eixo do tronco: frente-tras (m)
		float bodyTop     = 0.150f;   // inicio do tronco abaixo do osso 8 (m)
		float bodyBottom  = 0.720f;   // fim do tronco abaixo do osso 8 (m)
		float bodyBack    = 0.010f;   // deslocamento do eixo do tronco para tras do osso 8 (m)
		float gravity     = 9.81f;
		float damping     = 0.972f;   // fator de velocidade por passo
		float baseSpeed   = 6.0f;     // m/s com intensidade 1
		float gust        = 0.30f;    // amplitude relativa das rajadas
		float flutter     = 0.45f;    // turbulencia lateral relativa
		float pressureK   = 2.0f;     // arrasto de pressao (normal)
		float tangentK    = 0.10f;    // arrasto tangencial
		float stretchMax  = 1.05f;    // limite de estiramento da mecha
		float softPin1    = 0.30f;    // puxao ao repouso da linha 1 (por passo)
		float softPin2    = 0.10f;    // idem linha 2
		int   iterations  = 5;
	};
	Tune& GlobalTune();

	struct Shape
	{
		int   cols     = 18;
		int   rows     = 10;
		float length   = 0.50f;      // comprimento medio da mecha (m)
		float arcDeg   = 250.0f;     // quanto da cabeca as raizes cobrem (centrado atras)
		float ragged   = 0.18f;      // variacao de comprimento entre mechas (0..0.3): pontas irregulares
		int   spikePeriod = 0;       // >=2: pontas em DENTE (zigue-zague): colunas por dente (0 = sem dentes)
		float spikeDepth  = 0.45f;   // quanto o vale e mais curto que a ponta do dente (0..0.8, fracao do comprimento)
	};

	struct Frame
	{
		V3   head;                   // posicao do osso 8 (mundo)
		V3   right, fwd, up;         // eixos do ped (ortonormais; up ~ +Z)
		bool body = true;            // false (veiculo): sem colisao com o tronco
	};

	struct Stats { int lastSubsteps = 0; uint32_t resets = 0; float maxStretchSeen = 1.0f; };

	class Sim
	{
	public:
		bool Init(const Shape& s, uint32_t seed);
		bool Ready() const { return m_cols > 0; }
		void SetFrame(const Frame& f);              // todo quadro, antes de Update. A 1a chamada posiciona no repouso.
		void SetWind(float dirRad, float intensity); // dirRad = PARA ONDE sopra (0 = +X, anti-horario)
		void Reset();                               // volta ao repouso (precisa de SetFrame antes)
		void Update(float dtReal);
		void Step();                                // 1 passo fixo (testes)

		int  Cols() const { return m_cols; }
		int  Rows() const { return m_rows; }
		int  Count() const { return m_cols * m_rows; }
		const V3* Positions() const { return m_p; }
		const V3* Rest() const { return m_t; }      // alvo de repouso (mundo) com o quadro atual
		void GetRenderPositions(V3* out) const;     // extrapola o resto do tempo entre passos (sem tremor)
		void ComputeNormals(const V3* pos, V3* out) const;
		float SimTime() const { return m_time; }

		Stats stats;

	private:
		void layout();                              // alvo de repouso no mundo a partir do quadro
		void integrate();
		void solve();
		void collide();
		bool sane() const;
		V3   windAt(int c, int r) const;

		int   m_cols = 0, m_rows = 0;
		float m_seg[kMaxCols] = {};                 // comprimento de cada segmento da mecha c
		float m_lat[kMaxRows][kMaxCols] = {};       // repouso lateral (c,c+1) na linha r
		float m_ph[kMaxP] = {};
		V3    m_p[kMaxP], m_prev[kMaxP], m_t[kMaxP];
		float m_ang[kMaxCols] = {};                 // angulo da raiz em volta da cabeca (rad; 0 = atras, + = lado direito do ped)
		bool  m_latDone = false;
		Frame m_f{};
		bool  m_haveFrame = false;
		V3    m_lastHead{};
		float m_windDir = 0, m_windInt = 0.3f;
		float m_time = 0, m_acc = 0;
		Shape m_shape;
	};
}
