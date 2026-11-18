#pragma once
// =====================================================================================
//  FlagModel - leitor minimo do flag_maximo.dff (RenderWare 3.7 / GTA SA, versao 0x1803ffff).
//  Modulo PURO (sem RenderWare/jogo): le Clump -> GeometryList -> Geometry e entrega, para cada
//  atomico, posicoes/normais/UV/triangulos/materiais. A geometria NAO e enviada ao RenderWare
//  como RpGeometry: o desenho e feito em RwIm3D (ver Flag.cpp), entao este leitor so precisa
//  entender o arquivo gerado por tools/bandeira/flaggen_dff.py (e DFFs equivalentes).
//  Toda leitura e checada contra o tamanho do buffer; arquivo invalido -> false + motivo.
// =====================================================================================

#include <cstdint>
#include <string>
#include <vector>

namespace MxFlag
{
	struct MeshMaterial { std::string texture; uint8_t r = 255, g = 255, b = 255, a = 255; };
	struct MeshTri      { uint16_t a, b, c, mat; };

	struct MeshPart
	{
		std::vector<float>        pos;      // 3 por vertice
		std::vector<float>        nrm;      // 3 por vertice (vazio se o DFF nao tem normais)
		std::vector<float>        uv;       // 2 por vertice (vazio se nao ha UV)
		std::vector<MeshTri>      tris;
		std::vector<MeshMaterial> mats;
		int vertexCount() const { return (int)(pos.size() / 3); }
	};

	// Pele do tecido: cada vertice da frente e interpolado (bilinear) a partir de 4 particulas da grade de SIMULACAO.
	struct SkinVert { uint16_t idx[4]; float w[4]; };

	struct FlagModelData
	{
		std::vector<MeshPart> parts;        // ordem dos atomicos do DFF: 0 = mastro, 1 = base, 2 = tecido (frente+verso)
		int cols = 0, rows = 0;             // grade do tecido (colunas x linhas de particulas) - derivada da malha
		int clothVerts = 0;                 // vertices da folha (= vertices do tecido / 2)
		// A simulacao roda numa grade RETANGULAR regular (cols x rows) que cobre a caixa do tecido; a forma de pena
		// (celulas muito achatadas nas pontas) e so a malha desenhada, ligada a grade por 'skin'. Evita restricoes
		// mal condicionadas (arestas de 7 mm ao lado de 60 mm).
		std::vector<float>    simRest;      // 3 por particula (cols*rows), quadro local da bandeira
		std::vector<SkinVert> skin;         // clothVerts entradas (so a frente; o verso usa os mesmos pesos)
	};

	// Le o DFF da memoria. Em erro devolve false e preenche err.
	bool LoadFlagModel(const uint8_t* data, size_t size, FlagModelData& out, std::string& err);
}
