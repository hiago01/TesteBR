#pragma once
#include <cstdint>
typedef uint32_t RwUInt32; typedef int RwBool;
struct RwRaster{}; struct RwImage{}; struct RwStream{}; struct RwMemory{}; struct RpGeometry{}; struct RpAtomic{};
struct RwTexture { int refCount=1; void* raster=nullptr; char name[32]={}; };
struct RpMaterial { RwTexture* texture=nullptr; struct { uint8_t red=255,green=255,blue=255,alpha=255; } color; };
struct RpClump { int dummy=0; };
typedef RpAtomic* (*RpAtomicCallBack)(RpAtomic*, void*);
inline RpClump* RpClumpForAllAtomics(RpClump* c, RpAtomicCallBack, void*) { return c; }
inline RwBool RpClumpDestroy(RpClump*) { return 1; }
