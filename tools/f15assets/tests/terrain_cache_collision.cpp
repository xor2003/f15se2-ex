#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <vector>
#include "inttype.h"
#include "r3d_replacement.h"
struct TerrainEntry { int x,y,z,shape; };
int32 g_ViewX=0,g_ViewY=0;
int16 g_viewZ=0;
int g_lodGridDim[5]={0,0,0,16,0};
int matrix3dt[5][32]={};
TerrainEntry *matrix3dt_2[5][32]={};
const char *regnStr="test-campaign";
R3DReplacementMesh *testMesh=nullptr;
R3DReplacementMesh *r3d_replacementMesh(const char *,int){return testMesh;}
int16 process3dg(int16,int16,int16){return 0;}
#include "replacement_terrain_collision.h"
int main(int argc,char **argv){
    assert(argc>1);
    for(int file=1;file<argc;++file){
        std::ifstream in(argv[file],std::ios::binary);assert(in);
        in.seekg(40);
        uint32 count=0;in.read((char*)&count,4);assert(count<1000);
        std::vector<R3DReplacementPrim> prims(count);
        std::vector<std::vector<float>> storage(count);
        for(uint32 i=0;i<count;++i){
            uint32 header[6];float rgba[4];
            in.read((char*)header,sizeof(header));in.read((char*)rgba,sizeof(rgba));
            assert(header[1]<1000000);
            storage[i].resize(header[1]*3);
            in.read((char*)storage[i].data(),storage[i].size()*sizeof(float));assert(in);
            prims[i].mode=header[0];prims[i].nVerts=header[1];
            prims[i].sourceFlags=header[5];prims[i].xyz=storage[i].data();
        }
        R3DReplacementMesh mesh={};mesh.nPrims=count;mesh.prims=prims.data();testMesh=&mesh;
        TerrainEntry entry={0,0,0,17};matrix3dt[3][0]=1;matrix3dt_2[3][0]=&entry;
        int checked=0;
        for(auto &p:prims){
            if(p.mode!=4 || !(p.sourceFlags&R3D_SURFACE_LAND))continue;
            for(int i=0;i<p.nVerts;i+=3){
                const float *a=p.xyz+i*3,*b=a+3,*c=a+6;
                double nx=(b[1]-a[1])*(c[2]-a[2])-(b[2]-a[2])*(c[1]-a[1]);
                double ny=(b[2]-a[2])*(c[0]-a[0])-(b[0]-a[0])*(c[2]-a[2]);
                double nz=(b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]);
                if(std::abs(nz)<1e-6)continue;
                g_ViewX=std::lround((2048+(a[0]+b[0]+c[0])/3.0)*16);
                g_ViewY=std::lround((2048+(a[1]+b[1]+c[1])/3.0)*16);
                double x=g_ViewX/16.0-2048,y=g_ViewY/16.0-2048;
                double z=a[2]-(nx*(x-a[0])+ny*(y-a[1]))/nz;
                if(z<2)continue;
                g_viewZ=(int)std::floor(z*16)-2;
                assert(aircraftInsideReplacementTerrain());
                g_viewZ=(int)std::ceil(z*16)+2;
                assert(!aircraftInsideReplacementTerrain());
                ++checked;
            }
        }
        assert(checked>0);std::printf("PASS %s: %d below/above pairs\n",argv[file],checked);
    }
}
