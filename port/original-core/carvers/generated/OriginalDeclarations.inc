// Generated exact original declarations; do not edit.
#pragma once
class Level;

class Feature
{
private:
	bool doUpdate;
public:
	Feature();
	Feature(bool doUpdate);
	virtual ~Feature() {};

	virtual bool place(Level *level, Random *random, int x, int y, int z) = 0;
	virtual bool placeWithIndex(Level *level, Random *random, int x, int y, int z,int iIndex, int iRadius) { return false;}
	virtual void init(double V1, double V2, double V3) {};
	virtual void applyFeature(Level *level, Random *random, int xChunk, int zChunk);

protected:
	virtual void placeBlock(Level *level, int x, int y, int z, int tile);
	virtual void placeBlock(Level *level, int x, int y, int z, int tile, int data);
};

class Level;

class LargeFeature
{
public:
	static const wstring STRONGHOLD;
protected:
    int radius;
    Random *random;
	Level *level;

public:
	LargeFeature();
	~LargeFeature();

    virtual void apply(ChunkSource *ChunkSource, Level *level, int xOffs, int zOffs, byteArray blocks);

protected:
	virtual void addFeature(Level *level, int x, int z, int xOffs, int zOffs, byteArray blocks) {}
};
class LargeCaveFeature : public LargeFeature
{
protected:
    void addRoom(int64_t seed, int xOffs, int zOffs, byteArray blocks, double xRoom, double yRoom, double zRoom);
    void addTunnel(int64_t seed, int xOffs, int zOffs, byteArray blocks, double xCave, double yCave, double zCave, float thickness, float yRot, float xRot, int step, int dist, double yScale);
    virtual void addFeature(Level *level, int x, int z, int xOffs, int zOffs, byteArray blocks);
};

class Level;

class CanyonFeature : public LargeFeature
{
private:
	float rs[1024];

protected:
    void addTunnel(int64_t seed, int xOffs, int zOffs, byteArray blocks, double xCave, double yCave, double zCave, float thickness, float yRot, float xRot, int step, int dist, double yScale);
    virtual void addFeature(Level *level, int x, int z, int xOffs, int zOffs, byteArray blocks);
};

class Level;

class OreFeature : public Feature
{
private:
	int tile;
	int count;
	int targetTile;

	void _init(int tile, int count, int targetTile);
public:
	OreFeature (int tile, int count);
	OreFeature(int tile, int count, int targetTile);

	virtual bool place(Level *level, Random *random, int x, int y, int z);
};