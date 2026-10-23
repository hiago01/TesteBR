#pragma once
struct CColModel { int m_nColSlot=0; bool m_bHasCollisionVolumes=true; unsigned tris=0; unsigned GetTriCount() const { return tris; } };
struct CBaseModelInfo { CColModel* m_pColModel=nullptr; };
