#include "Platform.h"
#include <iostream>
#include <set>
#include <typeinfo>
#include <thread>

using namespace original_decoration;
namespace d=original_decoration::detail;
namespace reference {
using namespace original_decoration::detail;
using Mth=original_decoration::detail::Mth;
class Level;
#include "ReferenceDeclarations.inc"
// Independent dense raw-feature world, not the adapter's overlay implementation.
class Level {
public:
    static const int maxBuildHeight=256,genDepth=128;
    std::vector<uint8_t> tiles,data;
    d::Writes writes;
    Level():tiles(64*256*64),data(tiles.size()) {
        for(int x=-32;x<32;++x) for(int z=-32;z<32;++z) for(int y=0;y<64;++y)
            tiles[index(x,y,z)]=y==63?2:3;
    }
    static size_t index(int x,int y,int z) { return ((x+32)*64+(z+32))*256+y; }
    bool inside(int x,int y,int z) { return x>=-32 && x<32 && z>=-32 && z<32 && y>=0 && y<256; }
    int getTile(int x,int y,int z) { return inside(x,y,z)?tiles[index(x,y,z)]:0; }
    bool isEmptyTile(int x,int y,int z) { return getTile(x,y,z)==0; }
    Material* getMaterial(int x,int y,int z) { return Tile::tiles[getTile(x,y,z)]->material; }
    bool setTileAndData(int x,int y,int z,int t,int meta,int) {
        if(!inside(x,y,z)) return false;
        tiles[index(x,y,z)]=t; data[index(x,y,z)]=meta;
        writes[std::make_tuple(x,y,z)]={x,y,z,uint8_t(t),uint8_t(meta)}; return true;
    }
};
#include "ReferenceBodies.inc"
}
namespace {
uint8_t flat(int,int y,int) { return y<0 || y>=64?0:y==63?2:3; }
int top(int,int) { return 64; }
Options treesOnly(size_t cache=128) { Options o;o.plants=false;o.cachePlans=cache;return o; }
std::unique_ptr<Generator> generator(int64_t seed,int biome,Options options=treesOnly(),int size=512) {
    return std::unique_ptr<Generator>(new Generator(seed,size,[biome](int,int){return biome;},top,flat,options));
}
bool equal(const std::vector<Block>& a,const std::vector<Block>& b) {
    if(a.size()!=b.size()) return false;
    for(size_t i=0;i<a.size();++i) {
        auto x=a[i],y=b[i];
        if(std::tie(x.x,x.y,x.z,x.tile,x.data)!=std::tie(y.x,y.y,y.z,y.tile,y.data)) return false;
    }
    return true;
}
std::vector<Block> vectorOf(const d::Writes& w) { std::vector<Block> v;for(auto& e:w)v.push_back(e.second);return v; }
uint64_t hash(const std::vector<Block>& blocks) {
    uint64_t h=1469598103934665603ull;
    for(auto b:blocks) for(int v:{b.x,b.y,b.z,int(b.tile),int(b.data)}) {
        for(int i=0;i<4;++i) { h^=uint8_t(uint32_t(v)>>(i*8));h*=1099511628211ull; }
    }
    return h;
}
void rawFeature() {
    size_t tested=0;
    for(int kind=0;kind<10;++kind) for(int seed=0;seed<12;++seed) {
        d::Level adapter(seed,512,top,flat); reference::Level raw;
        Random a(seed),b(seed);
        std::unique_ptr<d::Feature> fa;
        std::unique_ptr<reference::Feature> fb;
        switch(kind) {
        case 0:fa.reset(new d::TreeFeature(false));fb.reset(new reference::TreeFeature(false));break;
        case 1:fa.reset(new d::BirchFeature(false));fb.reset(new reference::BirchFeature(false));break;
        case 2:fa.reset(new d::PineFeature);fb.reset(new reference::PineFeature);break;
        case 3:fa.reset(new d::SpruceFeature(false));fb.reset(new reference::SpruceFeature(false));break;
        case 4:fa.reset(new d::MegaTreeFeature(false,24,3,3));fb.reset(new reference::MegaTreeFeature(false,24,3,3));break;
        case 5:fa.reset(new d::SwampTreeFeature);fb.reset(new reference::SwampTreeFeature);break;
        case 6:fa.reset(new d::GroundBushFeature(3,0));fb.reset(new reference::GroundBushFeature(3,0));break;
        case 7:fa.reset(new d::BasicTree(false));fb.reset(new reference::BasicTree(false));break;
        case 8:fa.reset(new d::TreeFeature(false,9,3,3,true));fb.reset(new reference::TreeFeature(false,9,3,3,true));break;
        case 9:fa.reset(new d::TreeFeature(false));fb.reset(new reference::TreeFeature(false));
            adapter.setTileAndData(0,65,0,1,0,0);raw.setTileAndData(0,65,0,1,0,0);break;
        }
        fa->init(1,1,1); fb->init(1,1,1);
        bool ok=fa->place(&adapter,&a,0,64,0),expected=fb->place(&raw,&b,0,64,0);
        assert(ok==expected); assert(ok || kind==9);
        assert(equal(vectorOf(adapter.writes),vectorOf(raw.writes)));
        assert(a.nextInt()==b.nextInt()); ++tested;
    }
    std::cout<<"raw dense Level vs callback adapter: "<<tested<<" feature/seed cases\n";
}
void metadata() {
    for(int kind=0;kind<8;++kind) {
        d::Level l(42,512,top,flat);Random r(42);std::unique_ptr<d::Feature> f;
        switch(kind) {
        case 0:f.reset(new d::TreeFeature(false));break;
        case 1:f.reset(new d::BirchFeature(false));break;
        case 2:f.reset(new d::PineFeature);break;
        case 3:f.reset(new d::SpruceFeature(false));break;
        case 4:f.reset(new d::MegaTreeFeature(false,24,3,3));break;
        case 5:f.reset(new d::TreeFeature(false,10,3,3,true));break;
        case 6:f.reset(new d::SwampTreeFeature);break;
        case 7:f.reset(new d::GroundBushFeature(3,0));break;
        }
        assert(f->place(&l,&r,0,64,0));int logs=0,leaves=0;
        int species=kind==1?2:kind==2||kind==3?1:kind==4||kind==5||kind==7?3:0;
        for(auto e:l.writes) {
            if(e.second.tile==17) { assert((e.second.data&3)==species);++logs; }
            if(e.second.tile==18) { assert((e.second.data&3)==(kind==7?0:species));++leaves; }
            assert(e.second.data<16);
        }
        assert(logs>0 && leaves>0);
    }
    bool branch=false;
    for(int seed=0;seed<32;++seed) {
        d::Level l(seed,512,top,flat); Random r(seed);d::BasicTree fancy(false);
        fancy.init(1,1,1);assert(fancy.place(&l,&r,0,64,0));
        for(auto e:l.writes) if(e.second.tile==17 && (e.second.data==4 || e.second.data==8)) branch=true;
    }
    assert(branch);
}
void schedule() {
    int expected[23]={0,-999,-999,0,10,10,2,0,0,0,0,0,0,0,-100,-100,-999,-999,10,10,0,50,50};
    for(int id=0;id<=22;++id) assert(d::counts(id).treeCount==expected[id]);
    assert(d::counts(1).flowerCount==4 && d::counts(1).grassCount==10);
    assert(d::counts(2).reedsCount==50 && d::counts(17).cactusCount==10);
    assert(d::counts(21).grassCount==25 && d::counts(6).flowerCount==-999);
    for(int64_t seed:{int64_t(0),int64_t(1),int64_t(-1),INT64_MIN,INT64_MAX}) {
        d::Level l(seed,512,top,flat); Random actual(seed),expectedR(seed);
        d::seedChunk(&l,&actual,-4,7);
        int64_t xs=expectedR.nextLong()/2*2+1,zs=expectedR.nextLong()/2*2+1;
        expectedR.setSeed(((-4*xs)+(7*zs))^seed);
        assert(actual.nextInt()==expectedR.nextInt());
    }
    std::set<std::string> types;
    for(int id:{4,5,6,21}) for(int seed=0;seed<300;++seed) {
        Random actual(seed),control(seed);std::unique_ptr<d::Feature> f(d::biomeFor(id).getTreeFeature(&actual));
        if(id==4) { if(control.nextInt(5)!=0)control.nextInt(10); }
        if(id==5)control.nextInt(3);
        if(id==21 && control.nextInt(10)!=0 && control.nextInt(2)!=0) {
            if(control.nextInt(3)==0)control.nextInt(20);else control.nextInt(7);
        }
        assert(actual.nextInt()==control.nextInt());types.insert(typeid(*f).name());
    }
    assert(types.size()==8); // jungle variants share the original TreeFeature class
}
void determinism() {
    for(int id:{4,5,6,21}) {
        auto a=generator(12345,id),b=generator(12345,id),c=generator(42,id);
        auto first=a->generateForChunk(0,0);
        assert(!first.empty() && equal(first,a->generateForChunk(0,0)) && equal(first,b->generateForChunk(0,0)));
        assert(hash(first)!=hash(c->generateForChunk(0,0)));
    }
}
void seams() {
    for(int id:{4,21}) {
        auto forward=generator(-12345,id,treesOnly(2)),reverse=generator(-12345,id,treesOnly(1));
        std::map<std::pair<int,int>,std::vector<Block>> expected;
        for(int x=-2;x<=2;++x) for(int z=-2;z<=2;++z) expected[{x,z}]=forward->generateForChunk(x,z);
        for(int x=2;x>=-2;--x) for(int z=2;z>=-2;--z) assert(equal(expected[{x,z}],reverse->generateForChunk(x,z)));
        bool spills=false;
        for(auto b:d::plan(-12345,512,0,0,id,top,flat,false)) if(b.x>=16 || b.z>=16)spills=true;
        assert(spills);
        for(auto e:expected) {
            d::Writes manual;int cx=e.first.first,cz=e.first.second;
            for(int ox=cx-1;ox<=cx+1;++ox)for(int oz=cz-1;oz<=cz+1;++oz)
                for(auto b:d::plan(-12345,512,ox,oz,id,top,flat,false)) {
                    assert(b.x>=ox*16-16 && b.x<ox*16+32 && b.z>=oz*16-16 && b.z<oz*16+32);
                    if(b.x<cx*16 || b.x>=cx*16+16 || b.z<cz*16 || b.z>=cz*16+16)continue;
                    auto p=std::make_tuple(b.x,b.y,b.z);auto old=manual.find(p);
                    if(old!=manual.end() && old->second.tile==17 && b.tile!=17)continue;
                    if(old!=manual.end() && old->second.tile==18 && b.tile!=17 && b.tile!=18)continue;
                    manual[p]=b;
                }
            assert(equal(e.second,vectorOf(manual)));
        }
    }
}
void desert() {
    for(int id:{1,2,14,15,16,17,8,9}) {
        auto g=generator(12345,id);
        for(int x=-2;x<=2;++x)for(int z=-2;z<=2;++z) assert(g->generateForChunk(x,z).empty());
    }
}
void bounds() {
    auto raw=[](int x,int y,int z)->uint8_t { assert(x>=-24 && x<24 && z>=-24 && z<24 && y>=0 && y<256);return flat(x,y,z); };
    Generator g(42,48,[](int x,int z){assert(x>=-24 && x<24 && z>=-24 && z<24);return 21;},top,raw);
    for(int x=-2;x<=1;++x)for(int z=-2;z<=1;++z)for(auto b:g.generateForChunk(x,z)) {
        assert(b.x>=-24 && b.x<24 && b.z>=-24 && b.z<24 && b.y>=0 && b.y<256);
        assert(b.x>=x*16 && b.x<x*16+16 && b.z>=z*16 && b.z<z*16+16);
    }
    assert(g.generateForChunk(INT_MAX,INT_MIN).empty());
    bool threw=false;try {Generator invalid(0,10,[](int,int){return 4;},top,flat);}catch(const std::invalid_argument&){threw=true;}assert(threw);
    d::Level l(42,512,top,flat);Random r(42);d::PineFeature pine;assert(!pine.place(&l,&r,0,124,0));
    auto high=[](int,int y,int)->uint8_t{return y==199?2:0;};
    d::Level highLevel(42,512,[](int,int){return 200;},high);Random r2(42);d::SpruceFeature spruce(false);assert(spruce.place(&highLevel,&r2,0,200,0));
    for(auto e:highLevel.writes)assert(e.second.y<256);
}
void container() {
    d::Level l(42,512,top,flat);
    assert(l.getHeightmap(0,0)==64 && l.getTopSolidBlock(0,0)==64);
    l.setTileAndData(0,64,0,9,0,2);assert(l.getHeightmap(0,0)==65 && l.getTopSolidBlock(0,0)==64);
    assert(l.getMaterial(0,64,0)==d::Material::water);
    l.setTileAndData(0,65,0,18,2,2);assert(l.getHeightmap(0,0)==66 && l.getTopSolidBlock(0,0)==64);
    l.setTileAndData(0,66,0,17,3,2);assert(l.getHeightmap(0,0)==67 && l.getTopSolidBlock(0,0)==67);
    l.setTileAndData(0,66,0,0,0,2);assert(l.getHeightmap(0,0)==66);
    assert(d::Material::air->isSolid()==false && d::Material::stone->isSolid());
    // PRE-DECORATION immutable house/stone overlay prevents trees in volume.
    auto house=[](int,int y,int)->uint8_t {return y<80?1:0;};
    Generator blocked(42,512,[](int,int){return 4;},[](int,int){return 80;},house);
    assert(blocked.generateForChunk(0,0).empty());
}
void plants() {
    size_t grass=0,flower=0,cactus=0,reeds=0;
    for(int seed=0;seed<32;++seed) {
        auto p=d::plan(seed,512,0,0,1,top,flat,true);
        for(auto b:p) {grass+=b.tile==31;flower+=b.tile==37||b.tile==38;}
        auto sand=[](int,int y,int)->uint8_t {return y<64?12:0;};
        for(auto b:d::plan(seed,512,0,0,2,top,sand,true))cactus+=b.tile==81;
        auto bank=[](int x,int y,int)->uint8_t {return y<63?3:y==63?(x%2?9:12):0;};
        for(auto b:d::plan(seed,512,0,0,2,top,bank,true))reeds+=b.tile==83;
    }
    assert(grass && flower && cactus && reeds);
    std::cout<<"plants: grass="<<grass<<" flowers="<<flower<<" cactus="<<cactus<<" reeds="<<reeds<<'\n';
}
void threaded() {
    auto g=generator(12345,21);std::vector<Block> a,b;
    std::thread t1([&]{a=g->generateForChunk(0,0);});
    std::thread t2([&]{b=g->generateForChunk(0,0);});t1.join();t2.join();assert(equal(a,b));
}
}
int main(int argc,char** argv) {
    std::string name=argc>1?argv[1]:"all";
    for(auto test:{std::make_pair("rawfeature",rawFeature),{"metadata",metadata},{"schedule",schedule},
                  {"determinism",determinism},{"seams",seams},{"desert",desert},{"bounds",bounds},
                  {"container",container},{"plants",plants},{"threaded",threaded}})
        if(name==test.first || name=="all") {test.second();std::cout<<test.first<<" passed\n";}
}
