#include "World.hpp"
#include "OriginalWorld.hpp"
#include "ChunkPump.hpp"
#include "Inventory.hpp"
#include "InventoryRenderer.hpp"
#include "ItemCatalog.hpp"
#include "ItemIcons.hpp"
#include "WorldRenderer.hpp"
#include "LegacyCatalog.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <unistd.h>

static void require(bool ok, const char* message) { if(!ok) throw std::runtime_error(message); }
static uint16_t block(const char* key) {
    for(int i=0;i<legacy::kBlockCount;++i)if(!std::strcmp(legacy::kBlockSpecs[i].key,key))return uint16_t(i+1);
    throw std::runtime_error("missing block");
}
int main() {
    try {
        char root[]="/tmp/minecraft-ranges-icons.XXXXXX";
        require(mkdtemp(root)!=nullptr,"mkdtemp");
        mcworld::World world; world.beginStreaming(1); world.create(12345,root,true,864,false,false);
        const auto initial=world.streamingInfo();
        world.setChunkRanges(1,2);
        const auto info=world.streamingInfo();
        require(info.radius==1 && info.preloadRadius==2 && info.windowSize==48 && info.target==25 && info.displayTarget==9,
                "independent ranges");
        require(info.totalGenerated==initial.totalGenerated,"settings generated terrain");
        world.renderVolume();
        mcworld::ChunkPump pump;
        int frames=0;
        while(world.streamingInfo().queued) {
            require(pump.start(world,1),"pump start");
            while(pump.poll()==mcworld::ChunkPump::State::Running) {
                world.update(.01f,0,0,false,0); world.renderVolume(); ++frames; usleep(1000);
            }
            require(pump.poll()==mcworld::ChunkPump::State::Complete,"pump failed");
        }
        require(world.streamingInfo().loaded==25 && world.streamingInfo().displayLoaded==9,"load counts");
        std::printf("ranges PASS display=1 preload=2 loaded=25 displayLoaded=9 liveUpdates=%d\n",frames);
        mcinventory::Inventory inv; inv.open(world);
        std::vector<uint32_t> empty(960*266,0xff203040), image;
        mcinventory::renderInventoryOverlay(empty.data(),960,266,inv,false);
        const auto layout=mcinventory::inventoryLayout(960,266);
        int rendered=0;
        for(const auto& item:sourceitems::kItems) {
            if(!item.creative || item.renderIndex!=-1)continue;
            require(itemicons::findIcon(item.key)!=nullptr,"missing nonblock item icon");
            require(inv.selectHotbarItem(0,sourceitems::itemIndexForKey(item.key),1),"item select");
            image.assign(960*266,0xff203040);
            mcinventory::renderInventoryOverlay(image.data(),960,266,inv,false);
            const auto box=layout.hotbarIcons[0]; int changed=0;
            for(int y=box.y;y<box.y+box.h;++y)for(int x=box.x;x<box.x+box.w;++x)
                changed+=image[size_t(y)*960+x]!=empty[size_t(y)*960+x];
            require(changed>0,"blank native inventory icon"); ++rendered;
        }
        require(rendered==50,"nonblock item count");
        require(inv.selectHotbarItem(0,sourceitems::itemIndexForIdAux(276,0),1),"diamond sword select");
        require(inv.selectHotbarItem(1,sourceitems::itemIndexForIdAux(264,0),64),"diamond select");
        inv.save(world); mcinventory::Inventory reopened; reopened.open(world);
        require(reopened.slots()[0].key=="item:276:0" && reopened.slots()[1].key=="item:264:0","item persistence");
        image.assign(960*266,0xff203040); mcinventory::renderInventoryOverlay(image.data(),960,266,reopened,true);
        const auto path=std::string(root)+"/inventory.ppm";
        FILE* file=std::fopen(path.c_str(),"wb"); require(file!=nullptr,"open image");
        std::fprintf(file,"P6\n960 266\n255\n");
        for(auto pixel:image){ unsigned char rgb[]={static_cast<unsigned char>(pixel>>16),static_cast<unsigned char>(pixel>>8),static_cast<unsigned char>(pixel)}; require(std::fwrite(rgb,1,3,file)==3,"image write"); }
        require(std::fclose(file)==0,"image close");
        std::printf("icons PASS visible=50 sword=item:276:0 diamond=item:264:0 image=%s\n",path.c_str());
        std::vector<uint16_t> cells(32*32*32,0); std::vector<uint8_t> mask(32*32,1);
        for(int z=0;z<32;++z)for(int x=0;x<32;++x){
            if(x>=24) mask[z*32+x]=0;
            else cells[(20*32+z)*32+x]=block("stone");
        }
        mcworld::RenderVolume v{cells.data(),0,0,0,32,32,32,mask.data()};
        worldrender::LightingBuffer lighting; require(lighting.update(v,"probe",1,0),"initial light");
        require(lighting.sample(16,10,16).sky==0 && lighting.sample(28,32,16).sky==0,"unknown or roof light");
        cells[(20*32+16)*32+16]=0;
        require(lighting.refreshAround(v,16,20,16,16),"immediate local light");
        require(lighting.sample(16,10,16).sky>=13 && lighting.sample(28,10,16).sky==0,"immediate light sample");
        int steps=0; while(!lighting.update(v,"probe",2,32)) require(++steps<10000,"light convergence");
        worldrender::LightingBuffer reference; require(reference.update(v,"probe",2,0),"reference light");
        require(lighting.sky==reference.sky && lighting.block==reference.block,"eventual light parity");
        std::printf("lighting PASS edit immediate, unknown dark, eventual reference equality steps=%d\n",steps);
        std::printf("DEVICE RANGES/ICONS/LIGHTING PASS root=%s\n",root);
        return 0;
    } catch(const std::exception& error) { std::fprintf(stderr,"FAIL: %s\n",error.what()); return 1; }
}
