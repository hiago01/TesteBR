// FarWorld - permite construcoes (IPL estatico) alem de +-3000 (ex.: cidade do MOBJ em x ~ -8300).
//
// Problema original (libGTASA 2.10 arm64): CEntity::Add(const CRect&) e CEntity::Remove() prendem o retangulo a [-3000, 2999]
// so de UM lado (max(esq,-3000) / min(dir,2999)). Um predio inteiro fora do mapa vira um intervalo vazio -> nao entra em
// nenhum setor -> nunca e desenhado nem colide. Aqui os DOIS lados sao presos, entao o predio cai no setor da BORDA.
// Como o renderer so varre os setores que o frustum cobre (e para setores fora de 0..119 a lista principal e ignorada), o
// ScanSectorList e envolvido: para um setor fora da grade, a borda correspondente tambem e varrida (1x por quadro).
//
// Desligar sem recompilar: crie o arquivo vazio SAMP/farworld.off  (a patch nao e aplicada).
// Tudo que e registrado em log tem o prefixo [FARWORLD].
#pragma once

namespace FarWorld
{
    void Install();
}
