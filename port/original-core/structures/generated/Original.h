// Generated from port/upstream; see source-manifest.json. Do not hand edit.
#pragma once
#include "Platform.h"
namespace original_structures { namespace detail {
struct Tile {
 virtual ~Tile() {}
 int id; explicit Tile(int n):id(n) {}
 static const int UPDATE_CLIENTS=2;
static const int stone_Id = 1;
static const int grass_Id = 2;
static const int dirt_Id = 3;
static const int cobblestone_Id = 4;
static const int wood_Id = 5;
static const int sapling_Id = 6;
static const int unbreakable_Id = 7;
static const int water_Id = 8;
static const int calmWater_Id = 9;
static const int lava_Id = 10;
static const int calmLava_Id = 11;
static const int sand_Id = 12;
static const int gravel_Id = 13;
static const int goldOre_Id = 14;
static const int ironOre_Id = 15;
static const int coalOre_Id = 16;
static const int treeTrunk_Id = 17;
static const int leaves_Id = 18;
static const int sponge_Id = 19;
static const int glass_Id = 20;
static const int lapisOre_Id = 21;
static const int lapisBlock_Id = 22;
static const int dispenser_Id = 23;
static const int sandStone_Id = 24;
static const int noteblock_Id = 25;
static const int bed_Id = 26;
static const int goldenRail_Id = 27;
static const int detectorRail_Id = 28;
static const int pistonStickyBase_Id = 29;
static const int web_Id = 30;
static const int tallgrass_Id = 31;
static const int deadBush_Id = 32;
static const int pistonBase_Id = 33;
static const int pistonExtensionPiece_Id = 34;
static const int wool_Id = 35;
static const int pistonMovingPiece_Id = 36;
static const int flower_Id = 37;
static const int rose_Id = 38;
static const int mushroom_brown_Id = 39;
static const int mushroom_red_Id = 40;
static const int goldBlock_Id = 41;
static const int ironBlock_Id = 42;
static const int stoneSlab_Id = 43;
static const int stoneSlabHalf_Id = 44;
static const int redBrick_Id = 45;
static const int tnt_Id = 46;
static const int bookshelf_Id = 47;
static const int mossyCobblestone_Id = 48;
static const int obsidian_Id = 49;
static const int torch_Id = 50;
static const int fire_Id = 51;
static const int mobSpawner_Id = 52;
static const int stairs_wood_Id = 53;
static const int chest_Id = 54;
static const int redStoneDust_Id = 55;
static const int diamondOre_Id = 56;
static const int diamondBlock_Id = 57;
static const int workBench_Id = 58;
static const int wheat_Id = 59;
static const int farmland_Id = 60;
static const int furnace_Id = 61;
static const int furnace_lit_Id = 62;
static const int sign_Id = 63;
static const int door_wood_Id = 64;
static const int ladder_Id = 65;
static const int rail_Id = 66;
static const int stairs_stone_Id = 67;
static const int wallSign_Id = 68;
static const int lever_Id = 69;
static const int pressurePlate_stone_Id = 70;
static const int door_iron_Id = 71;
static const int pressurePlate_wood_Id = 72;
static const int redStoneOre_Id = 73;
static const int redStoneOre_lit_Id = 74;
static const int redstoneTorch_off_Id = 75;
static const int redstoneTorch_on_Id = 76;
static const int button_stone_Id = 77;
static const int topSnow_Id = 78;
static const int ice_Id = 79;
static const int snow_Id = 80;
static const int cactus_Id = 81;
static const int clay_Id = 82;
static const int reeds_Id = 83;
static const int jukebox_Id = 84;
static const int fence_Id = 85;
static const int pumpkin_Id = 86;
static const int netherRack_Id = 87;
static const int soulsand_Id = 88;
static const int glowstone_Id = 89;
static const int portalTile_Id = 90;
static const int litPumpkin_Id = 91;
static const int cake_Id = 92;
static const int diode_off_Id = 93;
static const int diode_on_Id = 94;
static const int stained_glass_Id = 95;
static const int trapdoor_Id = 96;
static const int monsterStoneEgg_Id = 97;
static const int stoneBrick_Id = 98;
static const int hugeMushroom_brown_Id = 99;
static const int hugeMushroom_red_Id = 100;
static const int ironFence_Id = 101;
static const int thinGlass_Id = 102;
static const int melon_Id = 103;
static const int pumpkinStem_Id = 104;
static const int melonStem_Id = 105;
static const int vine_Id = 106;
static const int fenceGate_Id = 107;
static const int stairs_bricks_Id = 108;
static const int stairs_stoneBrick_Id = 109;
static const int mycel_Id = 110;
static const int waterLily_Id = 111;
static const int netherBrick_Id = 112;
static const int netherFence_Id = 113;
static const int stairs_netherBricks_Id = 114;
static const int netherStalk_Id = 115;
static const int enchantTable_Id = 116;
static const int brewingStand_Id = 117;
static const int cauldron_Id = 118;
static const int endPortalTile_Id = 119;
static const int endPortalFrameTile_Id = 120;
static const int endStone_Id = 121;
static const int dragonEgg_Id = 122;
static const int redstoneLight_Id = 123;
static const int redstoneLight_lit_Id = 124;
static const int woodSlab_Id = 125;
static const int woodSlabHalf_Id = 126;
static const int cocoa_Id = 127;
static const int stairs_sandstone_Id = 128;
static const int emeraldOre_Id = 129;
static const int enderChest_Id = 130;
static const int tripWireSource_Id = 131;
static const int tripWire_Id = 132;
static const int emeraldBlock_Id = 133;
static const int stairs_sprucewood_Id = 134;
static const int stairs_birchwood_Id = 135;
static const int stairs_junglewood_Id = 136;
static const int commandBlock_Id = 137;
static const int beacon_Id = 138;
static const int cobbleWall_Id = 139;
static const int flowerPot_Id = 140;
static const int carrots_Id = 141;
static const int potatoes_Id = 142;
static const int button_wood_Id = 143;
static const int skull_Id = 144;
static const int anvil_Id = 145;
static const int chest_trap_Id = 146;
static const int weightedPlate_light_Id = 147;
static const int weightedPlate_heavy_Id = 148;
static const int comparator_off_Id = 149;
static const int comparator_on_Id = 150;
static const int daylightDetector_Id = 151;
static const int redstoneBlock_Id = 152;
static const int netherQuartz_Id = 153;
static const int hopper_Id = 154;
static const int quartzBlock_Id = 155;
static const int stairs_quartz_Id = 156;
static const int activatorRail_Id = 157;
static const int dropper_Id = 158;
static const int clayHardened_colored_Id = 159;
static const int stained_glass_pane_Id = 160;
static const int hayBlock_Id = 170;
static const int woolCarpet_Id = 171;
static const int clayHardened_Id = 172;
static const int coalBlock_Id = 173;
static Tile *rail,*ladder,*button,*door_wood;
static Tile* tiles[256];
};
struct DirectionalTile: Tile { explicit DirectionalTile(int n):Tile(n) {} };


class BoundingBox
{
public:
	int x0, y0, z0, x1, y1, z1;

	BoundingBox();
	static BoundingBox *getUnknownBox();
	static BoundingBox *orientBox(int footX, int footY, int footZ, int offX, int offY, int offZ, int width, int height, int depth, int orientation);
	BoundingBox(BoundingBox *other);
	BoundingBox(int x0, int y0, int z0, int x1, int y1, int z1);
	BoundingBox(int x0, int z0, int x1, int z1);
	bool intersects(BoundingBox *other);
	bool intersects(int x0, int y0, int z0, int x1, int y1, int z1);

	bool intersects(int x0, int z0, int x1, int z1);
	void expand(BoundingBox *other);
	BoundingBox *getIntersection(BoundingBox *other);
	void move(int dx, int dy, int dz);
	bool isInside(int x, int y, int z);

	int getXSpan();
	int getYSpan();
	int getZSpan();
	int getXCenter();
	int getYCenter();
	int getZCenter();

	wstring toString();
};
class Level;
class ChunkPos;
class BlockSelector;
class ChestTileEntity;
class TilePos;

/**
* 
* A structure piece is a construction or room, located somewhere in the world
* with a given orientatino (out of Direction.java). Structure pieces have a
* bounding box that says where the piece is located and its bounds, and the
* orientation is used to translate local coordinates into world coordinates.
* <p>
* The default orientation is Direction.UNDEFINED, in which case no translation
* will occur. If the orientation is Direction.NORTH, coordinate (0, 0, 0) will
* be at (boundingBox.x0, boundingBox.y0, boundingBox.z1). In other words, (1,
* 1, 1) will be translated to (boundingBox.x0 + 1, boundingBox.y0 + 1,
* boundingBox.z1 - 1).
* <p>
* When using Direction.SOUTH, the x coordinate will be the same, and the z
* coordinate will be flipped. In other words, the bounding box is NOT rotated!
* It is only flipped along the z axis. Also note that the bounding box is in
* world coordinates, so the local drawing must never reach outside of this.
* <p>
* When using east and west coordinates, the local z coordinate will be swapped
* with the local x coordinate. For example, (0, 0, 0) is (boundingBox.z1,
* boundingBox.y0, boundingBox.z0), and (1, 1, 1) becomes (boundingBox.x1 - 1,
* boundingBox.y0 + 1, boundingBox.z0 + 1) when using Direction.WEST.
* <p>
* When-ever a structure piece is placing blocks, it is VERY IMPORTANT to always
* make sure that all getTile and setTile calls are within the chunk's bounding
* box. Failing to check this will cause the level generator to create new
* chunks, leading to infinite loops and other errors.
*/
class StructurePiece
{
public:

public:
	class BlockSelector
	{

	protected:
		int nextId;
		int nextData;

	public:
		virtual void next(Random *random, int worldX, int worldY, int worldZ, bool isEdge) {}

		virtual int getNextId() { return nextId; }
		virtual int getNextData() { return nextData; }
	};

public:		// 4J is protected in java, but accessed from VillagePieces, not sure how
	BoundingBox *boundingBox;
protected:
	int orientation;
	int genDepth;

public:
	StructurePiece();

protected:
	StructurePiece(int genDepth);

public:
	virtual ~StructurePiece();


protected:

public:

protected:

public:
	virtual void addChildren(StructurePiece *startPiece, list<StructurePiece *> *pieces, Random *random);
	virtual bool postProcess(Level *level, Random *random, BoundingBox *chunkBB) = 0;

	virtual BoundingBox *getBoundingBox();

	int getGenDepth();

public:
	static StructurePiece *findCollisionPiece(list<StructurePiece *> *pieces, BoundingBox *box);
protected:
public:
	// 4J Stu - Made these public to use in game rules
	int getWorldX(int x, int z);
	int getWorldY(int y);
	int getWorldZ(int x, int z);
	int getOrientationData(int tile, int data);
	virtual void placeBlock(Level *level, int block, int data, int x, int y, int z, BoundingBox *chunkBB);

	/**
	* The purpose of this method is to wrap the getTile call on Level, in order
	* to prevent the level from generating chunks that shouldn't be loaded yet.
	* Returns 0 if the call is out of bounds.
	* 
	* @param level
	* @param x
	* @param y
	* @param z
	* @param chunkPosition
	* @return
	*/
	virtual int getBlock(Level *level, int x, int y, int z, BoundingBox *chunkBB);
	virtual void generateAirBox(Level *level, BoundingBox *chunkBB, int x0, int y0, int z0, int x1, int y1, int z1);
	virtual void generateBox(Level *level, BoundingBox *chunkBB, int x0, int y0, int z0, int x1, int y1, int z1, int edgeTile, int fillTile, bool skipAir);
	virtual void generateBox(Level *level, BoundingBox *chunkBB, int x0, int y0, int z0, int x1, int y1, int z1, int edgeTile, int edgeData, int fillTile, int fillData, bool skipAir);
	virtual void generateBox(Level *level, BoundingBox *chunkBB, BoundingBox *boxBB, int edgeTile, int fillTile, bool skipAir);
	virtual void generateBox(Level *level, BoundingBox *chunkBB, int x0, int y0, int z0, int x1, int y1, int z1, bool skipAir, Random *random, BlockSelector *selector);
	virtual void generateBox(Level *level, BoundingBox *chunkBB, BoundingBox *boxBB, bool skipAir, Random *random, BlockSelector *selector);
	virtual void generateMaybeBox(Level *level, BoundingBox *chunkBB, Random *random, float probability, int x0, int y0, int z0, int x1, int y1, int z1, int edgeTile, int fillTile, bool skipAir);
	virtual void maybeGenerateBlock(Level *level, BoundingBox *chunkBB, Random *random, float probability, int x, int y, int z, int tile, int data);
	virtual void generateUpperHalfSphere(Level *level, BoundingBox *chunkBB, int x0, int y0, int z0, int x1, int y1, int z1, int fillTile, bool skipAir);
	virtual void generateAirColumnUp(Level *level, int x, int startY, int z, BoundingBox *chunkBB);
	virtual void fillColumnDown(Level *level, int tile, int tileData, int x, int startY, int z, BoundingBox *chunkBB);

protected:
	void createDoor(Level *level, BoundingBox *chunkBB, Random *random, int x, int y, int z, int orientation);
};

class BiomeSource;

class VillagePieces
{

private:
	static const int MAX_DEPTH = 50;
	static const int BASE_ROAD_DEPTH = 3;
	// the dungeon starts at 64 and traverses downwards to this point
	static const int LOWEST_Y_POSITION = 10;

public:
	static const int SIZE_SMALL = 0;
	static const int SIZE_BIG = 1;
	static const int SIZE_BIGGEST = 2;

	// 4J - added to replace use of Class<? extends VillagePiece> within this class
	enum EPieceClass
	{
		EPieceClass_SimpleHouse,
		EPieceClass_SmallTemple,
		EPieceClass_BookHouse,
		EPieceClass_SmallHut,
		EPieceClass_PigHouse,
		EPieceClass_DoubleFarmland,
		EPieceClass_Farmland,
		EPieceClass_Smithy,
		EPieceClass_TwoRoomHouse
	};


	class PieceWeight
	{
	public:
		EPieceClass pieceClass;		// 4J - EPieceClass was Class<? extends VillagePiece>
		const int weight;
		int placeCount;
		int maxPlaceCount;

		PieceWeight(EPieceClass pieceClass, int weight, int maxPlaceCount);	// 4J - EPieceClass was Class<? extends VillagePiece>
		bool doPlace(int depth);
		bool isValid();
	};

	static list<PieceWeight *> *createPieceSet(Random *random, int villageSize);		// 4J - was ArrayList

	class StartPiece;
private:
	class VillagePiece;
	static int updatePieceWeight(list<PieceWeight *> *currentPieces);		// 4J = was array list
	static VillagePiece *findAndCreatePieceFactory(StartPiece *startPiece, PieceWeight *piece, list<StructurePiece *> *pieces, Random *random, int footX, int footY, int footZ, int direction, int depth);
	static VillagePiece *generatePieceFromSmallDoor(StartPiece *startPiece, list<StructurePiece *> *pieces, Random *random, int footX, int footY, int footZ, int direction, int depth);
	static StructurePiece *generateAndAddPiece(StartPiece *startPiece, list<StructurePiece *> *pieces, Random *random, int footX, int footY, int footZ, int direction, int depth);
	static StructurePiece *generateAndAddRoadPiece(StartPiece *startPiece, list<StructurePiece *> *pieces, Random *random, int footX, int footY, int footZ, int direction, int depth);


	/**
	* 
	*
	*/
private:
	class VillagePiece : public StructurePiece
	{
	protected:
		int heightPosition;
	private:
		int spawnedVillagerCount;
		bool isDesertVillage;
	protected:
		StartPiece *startPiece;

		VillagePiece();
		VillagePiece(StartPiece *startPiece, int genDepth);
		StructurePiece *generateHouseNorthernLeft(StartPiece *startPiece, list<StructurePiece *> *pieces, Random *random, int yOff, int zOff);
		StructurePiece *generateHouseNorthernRight(StartPiece *startPiece, list<StructurePiece *> *pieces, Random *random, int yOff, int zOff);
		int getAverageGroundHeight(Level *level, BoundingBox *chunkBB);
		static bool isOkBox(BoundingBox *box, StartPiece *startRoom); // 4J added startRoom param
		virtual int biomeBlock(int tile, int data);
		virtual int biomeData(int tile, int data);
		virtual void placeBlock(Level *level, int block, int data, int x, int y, int z, BoundingBox *chunkBB);
		virtual void generateBox(Level *level, BoundingBox *chunkBB, int x0, int y0, int z0, int x1, int y1, int z1, int edgeTile, int fillTile, bool skipAir);
		virtual void fillColumnDown(Level *level, int block, int data, int x, int startY, int z, BoundingBox *chunkBB);
	};

	/**
	* 
	*
	*/
public:
	class Well : public VillagePiece
	{
	public:

	private:
		static const int width = 6;
		static const int height = 15;
		static const int depth = 6;

	public:
		Well();
		Well(StartPiece *startPiece, int genDepth, Random *random, int west, int north);
		Well(StartPiece *startPiece, int genDepth, Random *random, BoundingBox *stairsBox, int direction);
		virtual void addChildren(StructurePiece *startPiece, list<StructurePiece *> *pieces, Random *random);
		virtual bool postProcess(Level *level, Random *random, BoundingBox *chunkBB);
	};

public:
	class StartPiece : public Well
	{
	public:

	public:
		// these fields are only used in generation step and aren't serialized :{
		BiomeSource *biomeSource;
		bool isDesertVillage;

		int villageSize;
		bool isLibraryAdded;
		PieceWeight *previousPiece;
		list<PieceWeight *> *pieceSet;
        vector<PieceWeight*> ownedPieceWeights; // adapter ownership of exhausted weights
		Level *m_level;

		// these queues are used so that the addChildren calls are called in a random order
		vector<StructurePiece *> pendingHouses;
		vector<StructurePiece *> pendingRoads;

		StartPiece();
		StartPiece(BiomeSource *biomeSource, int genDepth, Random *random, int west, int north, list<PieceWeight *> *pieceSet, int villageSize, Level *level); // 4J Added level param
		virtual ~StartPiece();

		BiomeSource *getBiomeSource();

	};

public:
	class VillageRoadPiece : public VillagePiece
	{
	protected:
		VillageRoadPiece() {}
		VillageRoadPiece(StartPiece *startPiece, int genDepth) : VillagePiece(startPiece, genDepth) {}
	};

	/**
	* 
	*
	*/
public:
	class StraightRoad : public VillageRoadPiece
	{
	public:

	private:
		static const int width = 3;
		int length;
	public:
		StraightRoad();
		StraightRoad(StartPiece *startPiece, int genDepth, Random *random, BoundingBox *stairsBox, int direction);

	protected:

	public:
		virtual void addChildren(StructurePiece *startPiece, list<StructurePiece *> *pieces, Random *random);
		static BoundingBox *findPieceBox(StartPiece *startPiece, list<StructurePiece *> *pieces, Random *random, int footX, int footY, int footZ, int direction);
		virtual bool postProcess(Level *level, Random *random, BoundingBox *chunkBB);
	};

	/**
	* 
	*
	*/
public:
	class SimpleHouse : public VillagePiece
	{
	public:

	private:
		static const int width = 5;
		static const int height = 6;
		static const int depth = 5;

	private:
		bool hasTerrace;

	public:
		SimpleHouse();
		SimpleHouse(StartPiece *startPiece, int genDepth, Random *random, BoundingBox *stairsBox, int direction);

	protected:

	public:
		static SimpleHouse *createPiece(StartPiece *startPiece, list<StructurePiece *> *pieces, Random *random, int footX, int footY, int footZ, int direction, int genDepth);
		virtual bool postProcess(Level *level, Random *random, BoundingBox *chunkBB);
	};

public:
	class SmallTemple : public VillagePiece
	{
	public:

	private:
		static const int width = 5;
		static const int height = 12;
		static const int depth = 9;

		int heightPosition;

	public:
		SmallTemple();
		SmallTemple(StartPiece *startPiece, int genDepth, Random *random, BoundingBox *stairsBox, int direction);

		static SmallTemple *createPiece(StartPiece *startPiece, list<StructurePiece *> *pieces, Random *random, int footX, int footY, int footZ, int direction, int genDepth);
		virtual bool postProcess(Level *level, Random *random, BoundingBox *chunkBB);
	};

public:
	class BookHouse : public VillagePiece
	{
	public:

	private:
		static const int width = 9;
		static const int height = 9;
		static const int depth = 6;

		int heightPosition;

	public:
		BookHouse();
		BookHouse(StartPiece *startPiece, int genDepth, Random *random, BoundingBox *stairsBox, int direction);

		static BookHouse *createPiece(StartPiece *startPiece, list<StructurePiece *> *pieces, Random *random, int footX, int footY, int footZ, int direction, int genDepth);
		virtual bool postProcess(Level *level, Random *random, BoundingBox *chunkBB);
	};

public:
	class SmallHut : public VillagePiece
	{
	public:


	private:
		static const int width = 4;
		static const int height = 6;
		static const int depth = 5;

		bool lowCeiling;
		int tablePlacement;

	public:
		SmallHut();
		SmallHut(StartPiece *startPiece, int genDepth, Random *random, BoundingBox *stairsBox, int direction);

	protected:

	public:
		static SmallHut *createPiece(StartPiece *startPiece, list<StructurePiece *> *pieces, Random *random, int footX, int footY, int footZ, int direction, int genDepth);
		virtual bool postProcess(Level *level, Random *random, BoundingBox *chunkBB);
	};

public:
	class PigHouse : public VillagePiece
	{
	public:


	private:
		static const int width = 9;
		static const int height = 7;
		static const int depth = 11;

	public:
		PigHouse();
		PigHouse(StartPiece *startPiece, int genDepth, Random *random, BoundingBox *stairsBox, int direction);
		static PigHouse *createPiece(StartPiece *startPiece, list<StructurePiece *> *pieces, Random *random, int footX, int footY, int footZ, int direction, int genDepth);
		virtual bool postProcess(Level *level, Random *random, BoundingBox *chunkBB);
	};

public:
	class TwoRoomHouse : public VillagePiece
	{
	public:

	private:
		static const int width = 9;
		static const int height = 7;
		static const int depth = 12;

		int heightPosition;

	public:
		TwoRoomHouse();
		TwoRoomHouse(StartPiece *startPiece, int genDepth, Random *random, BoundingBox *stairsBox, int direction);
		static TwoRoomHouse *createPiece(StartPiece *startPiece, list<StructurePiece *> *pieces, Random *random, int footX, int footY, int footZ, int direction, int genDepth);
		virtual  bool postProcess(Level *level, Random *random, BoundingBox *chunkBB);
	};

public:
	class Smithy : public VillagePiece
	{
	public:


	private:
		static const int width = 10;
		static const int height = 6;
		static const int depth = 7;

		bool hasPlacedChest;


	public:

		Smithy();
		Smithy(StartPiece *startPiece, int genDepth, Random *random, BoundingBox *stairsBox, int direction);
		static Smithy *createPiece(StartPiece *startPiece, list<StructurePiece *> *pieces, Random *random, int footX, int footY, int footZ, int direction, int genDepth);

	protected:

	public:
		virtual bool postProcess(Level *level, Random *random, BoundingBox *chunkBB);
	};

public:
	class Farmland : public VillagePiece
	{
	public:


	private:
		static const int width = 7;
		static const int height = 4;
		static const int depth = 9;

		int cropsA;
		int cropsB;

		int selectCrops(Random *random);

	public:
		Farmland();
		Farmland(StartPiece *startPiece, int genDepth, Random *random, BoundingBox *stairsBox, int direction);

	protected:

	public:
		static Farmland *createPiece(StartPiece *startPiece, list<StructurePiece *> *pieces, Random *random, int footX, int footY, int footZ, int direction, int genDepth);
		virtual bool postProcess(Level *level, Random *random, BoundingBox *chunkBB);
	};

public:
	class DoubleFarmland : public VillagePiece
	{
	public:

	private:
		static const int width = 13;
		static const int height = 4;
		static const int depth = 9;

		int heightPosition;

		int cropsA;
		int cropsB;
		int cropsC;
		int cropsD;

		int selectCrops(Random *random);

	public:
		DoubleFarmland();
		DoubleFarmland(StartPiece *startPiece, int genDepth, Random *random, BoundingBox *stairsBox, int direction);

	protected:

	public:
		static DoubleFarmland *createPiece(StartPiece *startPiece, list<StructurePiece *> *pieces, Random *random, int footX, int footY, int footZ, int direction, int genDepth);
		virtual bool postProcess(Level *level, Random *random, BoundingBox *chunkBB);
	};

public:
	class LightPost : public VillagePiece
	{
	public:

	private:
		static const int width = 3;
		static const int height = 4;
		static const int depth = 2;

		int heightPosition;

	public:
		LightPost();
		LightPost(StartPiece *startPiece, int genDepth, Random *random, BoundingBox *box, int direction);
		static BoundingBox *findPieceBox(StartPiece *startPiece, list<StructurePiece *> *pieces, Random *random, int footX, int footY, int footZ, int direction);
		virtual bool postProcess(Level *level, Random *random, BoundingBox *chunkBB);
	};
};

class StructureStart {
public:
    std::list<StructurePiece*> pieces;
    StructureStart(); ~StructureStart();
    BoundingBox* getBoundingBox(); std::list<StructurePiece*>* getPieces();
    void calculateBoundingBox();
protected:
    BoundingBox* boundingBox; int chunkX, chunkZ;
};
class VillageFeature {
public:
    Level* level; Random* random;
    int villageSizeModifier, townSpacing, minTownSeparation, m_iXZSize;
    static std::vector<Biome*> allowedBiomes;
    static void staticCtor(); void _init(int); VillageFeature(int);
    bool isFeatureChunk(int,int,bool=false);
    class VillageStart: public StructureStart {
        bool valid; int m_iXZSize;
    public:
        VillageStart(Level*,Random*,int,int,int,int); bool isValid();
    };
    StructureStart* createStructureStart(int,int);
};

}}
