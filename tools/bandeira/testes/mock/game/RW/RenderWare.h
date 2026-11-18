#pragma once
#include <cstdint>
typedef uint32_t RwUInt32; typedef int RwBool;
#define RWRSTATE(a) (reinterpret_cast<void *>(a))
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif
enum { rwRENDERSTATETEXTURERASTER, rwRENDERSTATEVERTEXALPHAENABLE, rwRENDERSTATEZTESTENABLE, rwRENDERSTATEZWRITEENABLE, rwRENDERSTATECULLMODE, rwRENDERSTATEFOGENABLE, rwRENDERSTATETEXTUREFILTER, rwRENDERSTATETEXTUREADDRESS };
enum { rwCULLMODECULLNONE = 1, rwFILTERLINEAR = 2, rwTEXTUREADDRESSCLAMP = 3, rwPRIMTYPETRILIST = 3, rwIM3D_VERTEXUV = 1 };
struct RwRaster { int id; };
struct RwTexture { RwRaster* raster; };
struct RxObjSpace3DVertex { struct { float x,y,z; } objVertex; struct { float x,y,z; } objNormal; uint32_t color; float u, v; };
static_assert(sizeof(RxObjSpace3DVertex) == 0x24);
typedef RxObjSpace3DVertex RwIm3DVertex; typedef uint32_t RwImVertexIndex;
RwBool RwRenderStateSet(int, void*);
void* RwIm3DTransform(RwIm3DVertex*, RwUInt32, void*, RwUInt32);
RwBool RwIm3DRenderIndexedPrimitive(int, RwImVertexIndex*, int);
RwBool RwIm3DEnd();
