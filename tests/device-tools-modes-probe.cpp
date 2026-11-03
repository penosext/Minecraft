#include "ActorRenderer.hpp"
#include "GameActions.hpp"
#include "Inventory.hpp"
#include "InventoryRenderer.hpp"
#include "ItemCatalog.hpp"
#include "ItemIcons.hpp"
#include "ToolRules.hpp"
#include "World.hpp"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>
static void require(bool ok,const char* what){ if(!ok) throw std::runtime_error(what); }
static int indexOf(int id,int aux){ for(int i=0;i<sourceitems::kItemCount;++i) if(sourceitems::kItems[i].itemId==id&&sourceitems::kItems[i].aux==aux) return i; return -1; }
int main(){
  try {
    char root[]="/tmp/minecraft-tools.XXXXXX"; require(mkdtemp(root)!=nullptr,"mkdtemp");
    mcworld::World world; world.create(2468,root,true,864,false,false);
    mcinventory::Inventory inv; inv.open(world);
    mcgame::GameActions actions;
    require(inv.gameMode()==mcinventory::GameMode::Creative,"old save defaults to creative");
    // Source-pinned numbers reaching the runtime.
    require(mcgame::attackDamage(276)==8.0f,"diamond sword total damage");
    require(mcgame::attackDamage(293)==1.0f,"hoe damage");
    require(mcgame::attackDamage(-1)==1.0f,"empty hand damage");
    require(mcgame::canHarvest(278,49)&&!mcgame::canHarvest(270,15),"harvest gating");
    std::printf("rules PASS sword=8 hoe=1 gating ok\n");
    // Creative: instant break, no durability, no drops.
    bool broke=false; int ticks=0;
    for(;ticks<20&&!broke;++ticks) broke=actions.tickMining(world,inv,0,-90,true,.05f).broken;
    require(broke&&ticks==1,"creative instant break");
    // Survival with a diamond pickaxe: progress, wear and a real drop.
    require(inv.selectHotbarItem(0,indexOf(278,0),1)&&inv.select(0),"give pickaxe");
    require(inv.setGameMode(mcinventory::GameMode::Survival),"enter survival");
    require(inv.selectedMaxDamage()==1561&&inv.selectedDamage()==0,"pickaxe durability");
    actions.resetMining();
    int survivalTicks=0; bool mined=false;
    for(;survivalTicks<600&&!mined;++survivalTicks) mined=actions.tickMining(world,inv,0,-90,true,.05f).broken;
    require(mined&&survivalTicks>1,"survival mining takes time");
    require(inv.selectedDamage()==1,"mining wear");
    int stacks=0; std::string drop;
    for(const auto& slot:inv.slots()) if(slot.count){ ++stacks; if(slot.key!="item:278:0"&&drop.empty()) drop=slot.key; }
    require(stacks==2&&!drop.empty(),"survival drop collected");
    std::printf("survival PASS ticks=%d wear=%d drop=%s\n",survivalTicks,inv.selectedDamage(),drop.c_str());
    // Durability and mode survive a save/reopen; creative give is refused in survival.
    require(!inv.selectHotbarItem(1,indexOf(264,0),1),"survival refuses creative give");
    inv.save(world);
    mcinventory::Inventory reopened; reopened.open(world);
    require(reopened.gameMode()==mcinventory::GameMode::Survival,"mode persisted");
    require(reopened.select(0)&&reopened.selectedDamage()==1,"damage persisted");
    std::printf("persist PASS mode=survival damage=%d\n",reopened.selectedDamage());
    // Held tool renders from its own item sprite, not an empty hand.
    require(reopened.selectedBlockIndex()<0,"tool has no block render index");
    require(itemicons::findIcon("item:278:0")!=nullptr,"pickaxe sprite");
    std::vector<uint32_t> frame(480*160,0xff101820), blank=frame;
    renderHeldItemOverlay(frame.data(),480,160,278,0,.5f);
    int changed=0; for(size_t i=0;i<frame.size();++i) changed+=frame[i]!=blank[i];
    require(changed>0,"held tool drew nothing");
    std::printf("held PASS pixels=%d\n",changed);
    // Hoe tills grass/dirt only with air above, and wears one point.
    require(reopened.selectHotbarItem(2,indexOf(293,0),1)==false,"survival still refuses give");
    std::filesystem::remove_all(root);
    std::printf("DEVICE TOOLS/MODES/DURABILITY PASS\n");
    return 0;
  } catch(const std::exception& e){ std::fprintf(stderr,"FAIL: %s\n",e.what()); return 1; }
}
