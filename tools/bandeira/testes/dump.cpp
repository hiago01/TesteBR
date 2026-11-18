#include <cstdlib>
#include "FlagPhysics.h"
#include "FlagModel.h"
#include <cstdio>
#include <fstream>
#include <iterator>
using namespace MxFlag;
int main(int argc,char**argv){
 float wind=argc>1?atof(argv[1]):1.0f;
 std::ifstream in(getenv("FLAG_DFF") ? getenv("FLAG_DFF") : "SAMP/models/flag_maximo.dff",std::ios::binary); std::vector<uint8_t> b((std::istreambuf_iterator<char>(in)),{});
 FlagModelData m; std::string e; if(!LoadFlagModel(b.data(),b.size(),m,e)){puts(e.c_str());return 1;}
 std::vector<Vec3> rest(m.cols*m.rows); for(size_t i=0;i<rest.size();i++) rest[i]={m.simRest[i*3],0,m.simRest[i*3+2]};
 CFlagPhysics f; f.Init(rest.data(),m.cols,m.rows,99); f.SetTransform({0,0,0},0); f.SetWind(0,wind);
 FILE*o=fopen(argv[2],"w");
 const auto&cl=m.parts[2]; int N=m.clothVerts; fprintf(o,"%d %zu\n",N,cl.tris.size()/2);
 for(int k=0;k<5;k++){ for(int i=0;i<60*(k?1:8);i++) f.Update(1/60.f);
   std::vector<Vec3> rp(f.Count()); f.GetRenderPositions(rp.data());
   for(int v=0;v<N;v++){ Vec3 p{}; for(int q=0;q<4;q++){ const Vec3&s=rp[m.skin[v].idx[q]]; float w=m.skin[v].w[q]; p.x+=s.x*w;p.y+=s.y*w;p.z+=s.z*w;} fprintf(o,"%f %f %f %f %f\n",p.x,p.y,p.z,cl.uv[v*2],cl.uv[v*2+1]); }
 }
 for(size_t t=0;t<cl.tris.size()/2;t++) fprintf(o,"%d %d %d\n",cl.tris[t].a,cl.tris[t].b,cl.tris[t].c);
 fclose(o);}
