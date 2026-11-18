#pragma once
#include <cstdint>
struct CVector { float x=0,y=0,z=0; };
struct CMatrixM { CVector m_forward{0,1,0}; };
struct CCamera { CVector pos; CMatrixM mat; CVector GetPosition() { return pos; } CMatrixM& GetMatrix() { return mat; } };
struct CGame { uint8_t interior = 0; uint8_t GetActiveInterior() { return interior; } };
