#pragma once
// =====================================================================================
//  FlagMesh - monta os vertices/indices da bandeira para desenho imediato (Im3D) e os divide em lotes.
//  Modulo PURO (sem RenderWare): o Flag.cpp entrega um ISink que faz RwIm3DTransform/RenderIndexedPrimitive.
//
//  - Mastro e base: geometria estatica do DFF, so transladada; cor = cor do material * sombreado fixo.
//  - Tecido: a malha de pena e DEFORMADA pela "pele" (bilinear sobre a grade de simulacao, ver FlagModel.h).
//    Cada triangulo e desenhado UMA vez, pelo lado que esta voltado para a camera: frente (flag_maximo) ou verso
//    (flag_maximo_verso). Sem sobreposicao frente/verso -> sem z-fighting e sem depender da convencao de culling.
//  - Sombreado "baked" (o Im3D do jogo nao e iluminado): ambiente + luz fixa, dupla face, multiplicado por 'brightness'.
//  - Lotes pequenos (kBatchVerts) para ficar bem abaixo de qualquer limite do buffer Im3D (a mangueira usa 102 vertices).
// =====================================================================================

#include "FlagModel.h"
#include "FlagPhysics.h"

#include <cstdint>
#include <vector>

namespace MxFlag
{
	// mesmo layout de RxObjSpace3DVertex (0x24 bytes): conferido por static_assert em Flag.cpp
	struct DrawVertex { float x, y, z; float nx, ny, nz; uint32_t color; float u, v; };
	static_assert(sizeof(DrawVertex) == 0x24, "DrawVertex deve ter o layout de RxObjSpace3DVertex");

	enum DrawPass : int { PassStatic = 0, PassFront = 1, PassBack = 2 };

	struct ISink
	{
		virtual ~ISink() = default;
		// um lote: nv vertices e ni indices (3 por triangulo, indices locais ao lote)
		virtual void Submit(int pass, const DrawVertex* verts, int nv, const uint16_t* idx, int ni) = 0;
	};

	constexpr int kBatchVerts = 240;

	class CFlagMesh
	{
	public:
		bool Init(const FlagModelData* model);                           // o modelo deve sobreviver a este objeto
		bool Ready() const { return m_model != nullptr; }

		void SetStatic(const Vec3& basePos, float brightness);           // mastro + base (chamar ao criar/mover)
		void UpdateCloth(const Vec3* sim, const Vec3* simNrm, const Vec3& cam, float brightness);   // 1x por quadro
		void Draw(ISink& sink) const;                                    // 3 passes: estatico, frente, verso

		int frontTris() const { return (int)m_front.size() / 3; }
		int backTris()  const { return (int)m_back.size() / 3; }

	private:
		const FlagModelData*     m_model = nullptr;
		std::vector<DrawVertex>  m_static;        // mastro + base
		std::vector<uint32_t>    m_staticTris;    // 3 indices por triangulo
		std::vector<DrawVertex>  m_cloth;         // 2N: [0,N) = frente, [N,2N) = verso (mesma posicao, UV do verso)
		std::vector<uint32_t>    m_front, m_back; // indices (em m_cloth) dos triangulos escolhidos
		mutable std::vector<int> m_remap;         // trabalho do lote
	};
}
