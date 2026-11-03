// Standalone host test: link World/OriginalWorld and original_decoration,
// original_structures, original_terrain, original_biomes, original_core, pthread.
#define main legacyWorldRegressionMain
#include "world-test.cpp"
#undef main
#include "OriginalWorld.hpp"
#include <array>
#include <map>

uint32_t get32(const Bytes& bytes, size_t at) {
    assert(at + 4 <= bytes.size());
    uint32_t result = 0;
    for (int i = 0; i < 4; ++i) result |= uint32_t(bytes[at + i]) << (8 * i);
    return result;
}
void append32(Bytes& bytes, uint32_t value) {
    const auto at = bytes.size(); bytes.resize(at + 4); u32(bytes, at, value);
}
struct Edit { int x, y, z; uint16_t value; };
void originalFixture(const std::string& path, std::vector<Edit> edits, uint32_t version = 2) {
    const auto keys = palette(text(path + "/level.json"));
    mcworld::OriginalWorld base(12345, 864, false, false);
    std::map<int, std::map<uint32_t, uint16_t>> chunks;
    for (const auto& edit : edits) {
        const int key = (edit.z / 16) * 54 + edit.x / 16;
        chunks[key][uint32_t((size_t(edit.y) * 16 + edit.z % 16) * 16 + edit.x % 16)] = edit.value;
    }
    for (const auto& chunk : chunks) {
        const int cx = chunk.first % 54, cz = chunk.first / 54;
        Bytes bytes {'M','C','O','C','H','N','K',0};
        append32(bytes, version); append32(bytes, 12345); append32(bytes, hash(read(path + "/level.json")));
        append32(bytes, uint32_t(chunk.first)); append32(bytes, uint32_t(chunk.second.size()));
        for (int z = 0; z < 16; ++z) for (int x = 0; x < 16; ++x) bytes.push_back(base.biomeAt(cx * 16 + x, cz * 16 + z));
        for (const auto& edit : chunk.second) {
            append32(bytes, edit.first);
            uint32_t saved = 0;
            if (edit.second) {
                const auto it = std::find(keys.begin(), keys.end(), legacy::kBlockSpecs[edit.second - 1].key);
                assert(it != keys.end()); saved = uint32_t(it - keys.begin()) + 1;
            }
            append32(bytes, saved);
        }
        append32(bytes, hash(bytes));
        write(path + "/chunk-" + std::to_string(chunk.first) + ".bin", bytes);
    }
}
std::vector<std::string> chunkFiles(const std::string& path) {
    std::vector<std::string> files;
    for (const auto& entry : fs::directory_iterator(path))
        if (entry.path().filename().string().find("chunk-") == 0) files.push_back(entry.path());
    return files;
}
void remapOriginalPalette(const std::string& path) {
    auto level = text(path + "/level.json"); auto keys = palette(level);
    const size_t count = keys.size(); std::reverse(keys.begin(), keys.end()); palette(level, keys);
    Bytes joined;
    for (const auto& key : keys) { joined.insert(joined.end(), key.begin(), key.end()); joined.push_back(0); }
    const auto ph = hash(joined); field(level, "paletteHash", ph); writeText(path + "/level.json", level);
    const auto mh = hash(read(path + "/level.json"));
    for (const auto& file : chunkFiles(path)) {
        auto bytes = read(file); u32(bytes, 16, mh);
        for (size_t at = 284; at + 8 <= bytes.size() - 4; at += 8) {
            const auto value = get32(bytes, at + 4);
            if (value) u32(bytes, at + 4, uint32_t(count + 1 - value));
        }
        u32(bytes, bytes.size() - 4, hash(bytes, bytes.size() - 4)); write(file, bytes);
    }
    auto state = read(path + "/state.bin"); u32(state, 16, mh); u32(state, 20, ph); u32(state, 40, hash(state, 40));
    write(path + "/state.bin", state);
}
void pickFacesAndReach() {
    Temp root;
    World fixture; fixture.create(12345, root.path, true, 864, false, false);
    const auto stone = blockId("stone"), water = blockId("calmWater");
    // Full-cell DDA fixture 100 blocks above generation, no renderer dependency.
    originalFixture(fixture.path(), {{100,200,102,stone}, {100,200,98,stone},
        {102,200,100,stone}, {98,200,100,stone}, {100,203,100,stone}, {100,198,100,stone}});
    player(fixture.path(), 100.5f, 199, 100.5f);
    World world; world.open(fixture.path(), root.path);
    for (const auto& test : std::vector<std::array<float, 3>>{{0,0,2}, {180,0,3}, {90,0,4}, {-90,0,5}, {0,90,0}, {0,-90,1}}) {
        const auto hit = world.pick(test[0], test[1]);
        assert(hit.hit && hit.face == int(test[2]) && hit.block == stone && hit.distance > 0);
    }
    const int faceOffsets[6][3]={{0,-1,0},{0,1,0},{0,0,-1},{0,0,1},{-1,0,0},{1,0,0}};
    for (const auto& test : std::vector<std::array<float,3>>{{0,0,2},{180,0,3},{90,0,4},{-90,0,5},{0,90,0},{0,-90,1}}) {
        World side; side.open(fixture.path(),root.path);
        const auto hit=side.pick(test[0],test[1]); const auto* offset=faceOffsets[hit.face];
        const int px=hit.x+offset[0], py=hit.y+offset[1], pz=hit.z+offset[2];
        if (hit.face==1) { assert(!side.place(test[0],test[1],int(stone)-1)); continue; } // player's feet cell
        assert(side.place(test[0],test[1],int(blockId("planks_birch"))-1));
        assert(side.blockAt(px,py,pz)==blockId("planks_birch"));
        assert(side.mine(test[0],test[1]) && side.lastMinedBlock()==int(blockId("planks_birch"))-1);
    }
    near(world.pick(0, 0).distance, 1.5f);
    assert(!world.pick(0, 0, 1.49f).hit && world.pick(0, 0, 1.5f).hit);
    rejects([&] { world.pick(0, 0, 0); });
    rejects([&] { world.pick(std::numeric_limits<float>::infinity(), 0); });
    rejects([&] { world.place(0, std::numeric_limits<float>::quiet_NaN(), int(stone) - 1); });
    assert(!world.place(0, 0, -1) && !world.place(0, 0, legacy::kBlockCount));
    assert(!world.place(0, 0, int(water) - 1));
    const auto revision = world.revision();
    assert(world.place(0, 0, int(blockId("planks_birch")) - 1));
    assert(world.blockAt(100,200,101) == blockId("planks_birch") && world.revision() != revision);
    const auto nextRevision = world.revision();
    // Nearest cube now places into the player's current eye/body cell: reject.
    assert(!world.place(0, 0, int(stone) - 1) && world.revision() == nextRevision);
    assert(world.mine(0, 0) && world.lastMinedBlock() == int(blockId("planks_birch")) - 1);
    assert(world.blockAt(100,200,101) == 0);
    assert(!world.mine(0, 0, .1f) && world.lastMinedBlock() == -1);
    world.save(); World reopened; reopened.open(world.path(), root.path);
    assert(reopened.blockAt(100,200,101) == 0 && reopened.blockAt(100,200,102) == stone);
    // Farther than creative 5 is never picked, even with huge requested reach.
    World far; far.create(12345, root.path, true, 864, false, false);
    originalFixture(far.path(), {{100,200,106,stone}}); player(far.path(), 100.5f,199,100.5f);
    far.open(far.path(), root.path); assert(!far.pick(0,0,1000).hit && !far.mine(0,0,1000));
    player(far.path(), 100.5f,199,101); far.open(far.path(),root.path);
    assert(far.pick(0,0,1000).hit); near(far.pick(0,0).distance,5);
}
void placementPersistenceAndDictionary() {
    Temp root;
    const auto stone = blockId("stone"), gold = blockId("goldOre");
    World w; w.create(12345, root.path, true, 864, false, false);
    originalFixture(w.path(), {{100,200,102,stone}}); player(w.path(),100.5f,199,100.5f);
    w.open(w.path(), root.path);
    const auto metadata = read(w.path() + "/level.json");
    assert(w.place(0,0,int(gold)-1));
    const auto beforeState=read(w.path()+"/state.bin");
    auto files=chunkFiles(w.path()); assert(files.size()==1); const auto beforeChunk=read(files[0]);
    struct rlimit previous {}; assert(::getrlimit(RLIMIT_FSIZE,&previous)==0);
    auto limited=previous; limited.rlim_cur=16; const auto signal=std::signal(SIGXFSZ,SIG_IGN);
    assert(::setrlimit(RLIMIT_FSIZE,&limited)==0); rejects([&] { w.save(); });
    assert(::setrlimit(RLIMIT_FSIZE,&previous)==0); std::signal(SIGXFSZ,signal);
    assert(read(w.path()+"/state.bin")==beforeState && read(files[0])==beforeChunk);
    w.save();
    assert(get32(read(files[0]),8) == 2 && read(w.path()+"/level.json") == metadata);
    remapOriginalPalette(w.path());
    World mapped; mapped.open(w.path(),root.path);
    assert(mapped.blockAt(100,200,101) == gold && mapped.blockAt(100,200,102) == stone);
    assert(mapped.mine(0,0) && mapped.lastMinedBlock() == int(gold)-1);
    assert(mapped.place(0,0,int(blockId("planks_jungle"))-1)); mapped.save();
    World again; again.open(mapped.path(),root.path);
    assert(again.blockAt(100,200,101) == blockId("planks_jungle"));
    // v1 chunk reader remains removal-only even with a valid checksum/palette.
    const auto good = read(files[0]); auto bad = good; u32(bad,8,1); u32(bad,bad.size()-4,hash(bad,bad.size()-4));
    write(files[0],bad); rejects([&] { again.open(mapped.path(),root.path); });
    write(files[0],good);
    // Hostile IDs, duplicate/out-of-order records, baseline identity and headers.
    bad=good; u32(bad,292,get32(bad,284)); u32(bad,bad.size()-4,hash(bad,bad.size()-4)); write(files[0],bad);
    rejects([&] { again.open(mapped.path(),root.path); }); write(files[0],good);
    bad=good; u32(bad,284,0); u32(bad,bad.size()-4,hash(bad,bad.size()-4)); write(files[0],bad);
    rejects([&] { again.open(mapped.path(),root.path); }); write(files[0],good);
    bad=good; u32(bad,288,65535); u32(bad,bad.size()-4,hash(bad,bad.size()-4)); write(files[0],bad);
    rejects([&] { again.open(mapped.path(),root.path); }); write(files[0],good);
    bad=good; u32(bad,16,get32(bad,16)^1); u32(bad,bad.size()-4,hash(bad,bad.size()-4)); write(files[0],bad);
    rejects([&] { again.open(mapped.path(),root.path); }); write(files[0],good);
    fs::remove(files[0]); rejects([&] { mapped.save(); }); write(files[0],good);
    fs::rename(files[0],files[0]+".good"); fs::create_symlink(files[0]+".good",files[0]);
    rejects([&] { mapped.save(); }); rejects([&] { again.open(mapped.path(),root.path); });
    fs::remove(files[0]); fs::rename(files[0]+".good",files[0]);
}
void oldFormatsAndNetEdits() {
    Temp root;
    World legacy; legacy.create(7,root.path,false);
    const auto chunks=read(legacy.path()+"/chunks.bin"), metadata=read(legacy.path()+"/level.json");
    rejects([&] { legacy.place(0,-90,int(blockId("stone"))-1); });
    assert(read(legacy.path()+"/chunks.bin")==chunks && !fs::exists(legacy.path()+"/edits.bin"));
    assert(legacy.mine(0,-90)); legacy.save();
    World opened; opened.open(legacy.path(),root.path); assert(!opened.originalGeneration());
    assert(read(legacy.path()+"/chunks.bin")==chunks && read(legacy.path()+"/level.json")==metadata);
    // Real v1 removal bytes remain readable, and later placements use v2 without
    // ever changing the generation version or immutable saved dictionary.
    World old; old.create(12345,root.path,true,864,false,false);
    const auto level=read(old.path()+"/level.json");
    const int x=int(old.playerX()), y=int(old.playerY())-1, z=int(old.playerZ());
    assert(old.mine(0,-90)); old.save(); const auto file=chunkFiles(old.path()).at(0);
    auto bytes=read(file); u32(bytes,8,1); u32(bytes,bytes.size()-4,hash(bytes,bytes.size()-4)); write(file,bytes);
    opened.open(old.path(),root.path); assert(!opened.decorationsEnabled() && opened.blockAt(x,y,z)==0);
    opened.save(); assert(read(file)==bytes && read(old.path()+"/level.json")==level);
    // Refill the mined BASE surface with its exact block. Net overlay becomes
    // empty (v2 count=0), remains tracked atomically and is accepted on reopen.
    mcworld::OriginalWorld base(12345,864,false,false); const auto original=base.blockAt(x,y,z);
    assert(opened.place(0,-90,int(original)-1)); opened.save();
    assert(get32(read(file),8)==2 && get32(read(file),24)==0 && read(file).size()==288);
    World refilled; refilled.open(opened.path(),root.path); assert(refilled.blockAt(x,y,z)==original);
    assert(read(old.path()+"/level.json")==level);
    World fresh; fresh.create(12345,root.path,true,864); // default v3 + structures
    const int vx=int(fresh.playerX()), vy=int(fresh.playerY())-1, vz=int(fresh.playerZ());
    const auto v3Level=read(fresh.path()+"/level.json");
    assert(fresh.decorationsEnabled() && fresh.structuresEnabled() && fresh.mine(0,-90));
    assert(fresh.place(0,-90,int(blockId("goldOre"))-1)); fresh.save();
    World v3; v3.open(fresh.path(),root.path);
    assert(v3.decorationsEnabled() && v3.structuresEnabled() && v3.blockAt(vx,vy,vz)==blockId("goldOre"));
    assert(v3.mine(0,-90) && v3.lastMinedBlock()==int(blockId("goldOre"))-1); v3.save();
    assert(read(v3.path()+"/level.json")==v3Level);
}
void boundariesAndLiquidReplacement() {
    Temp root;
    const auto stone=blockId("stone"), water=blockId("calmWater"), bedrock=blockId("unbreakable");
    World fixture; fixture.create(12345,root.path,true,864,false,false);
    originalFixture(fixture.path(),{{0,200,102,stone},{100,255,100,stone},{110,200,112,bedrock}});
    player(fixture.path(),.25f,199,100.5f); World w; w.open(fixture.path(),root.path);
    assert(w.pick(0,0).face==2 && w.place(0,0,int(stone)-1)); w.save();
    assert(w.blockAt(-1,200,101)==0);
    player(fixture.path(),100.5f,252,100.5f); w.open(fixture.path(),root.path);
    assert(w.pick(0,90).hit && w.pick(0,90).face==0);
    assert(w.place(0,90,int(stone)-1) && w.blockAt(100,254,100)==stone);
    assert(!w.place(0,90,int(stone)-1)); // next cell intersects player's head
    player(fixture.path(),110.5f,199,110.5f); w.open(fixture.path(),root.path);
    assert(w.pick(0,0).block==bedrock && !w.mine(0,0) && w.lastMinedBlock()==-1);
    // Baseline liquid is replaceable (not solid), but selection of liquid is
    // deliberately unsupported. Locate one finite-world water column.
    mcworld::OriginalWorld base(12345,864,false,false);
    bool tested=false;
    for(int z=16;z<848&&!tested;z+=8) for(int x=16;x<848&&!tested;x+=8) {
        const int y=62; // original seaLevel=63 fills cells below it, not cell 63
        if(base.blockAt(x,y,z)!=water || base.blockAt(x,y-1,z)!=water ||
           base.blockAt(x,y,z-1)!=water || base.blockAt(x,y-1,z-1)!=water || base.blockAt(x,y+1,z-1)) continue;
        World pool; pool.create(12345,root.path,true,864,false,false);
        originalFixture(pool.path(),{{x,y,z+1,stone}}); player(pool.path(),x+.5f,float(y-1),z-.5f);
        pool.open(pool.path(),root.path); assert(pool.pick(0,0).block==stone);
        assert(pool.place(0,0,int(stone)-1) && pool.blockAt(x,y,z)==stone); pool.save();
        assert(pool.mine(0,0) && pool.blockAt(x,y,z)==0); pool.save();
        World dry; dry.open(pool.path(),root.path); assert(dry.blockAt(x,y,z)==0);
        tested=true;
    }
    assert(tested);
}
#ifndef MCWORLD_INTERACTION_FIXTURES_ONLY
int main() {
    pickFacesAndReach(); placementPersistenceAndDictionary(); oldFormatsAndNetEdits(); boundariesAndLiquidReplacement();
    std::cout << "world-interaction-test: creative reach, six faces, AABB rejection, placement/drop IDs, liquid rules, v1/v2 chunks, dictionary remap, immutable legacy and safe I/O passed\n";
}
#endif
