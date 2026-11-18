#include <cstdlib>
#include "FlagModel.h"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <random>
using namespace MxFlag;
int main(){ std::ifstream in(getenv("FLAG_DFF") ? getenv("FLAG_DFF") : "SAMP/models/flag_maximo.dff",std::ios::binary); std::vector<uint8_t> b((std::istreambuf_iterator<char>(in)),{});
 std::mt19937 rng(42); int ok=0,bad=0;
 for(int it=0;it<4000;it++){ auto m=b; int n=1+rng()%8; for(int k=0;k<n;k++){ size_t p = (rng()%3==0)? rng()%400 : rng()%m.size(); m[p]=rng(); }
   if(rng()%4==0) m.resize(rng()%m.size());
   FlagModelData d; std::string e; if(LoadFlagModel(m.data(),m.size(),d,e)) ok++; else bad++; }
 printf("fuzz: %d aceitos, %d rejeitados, sem crash\n",ok,bad); }
