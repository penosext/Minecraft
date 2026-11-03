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

class TreeFeature : public Feature
{
private:
	const int baseHeight;
	const bool addJungleFeatures;
	const int trunkType;
	const int leafType;

public:
	TreeFeature(bool doUpdate);
	TreeFeature(bool doUpdate, int baseHeight, int trunkType, int leafType, bool addJungleFeatures);

	virtual bool place(Level *level, Random *random, int x, int y, int z);

private:
	void addVine(Level *level, int xx, int yy, int zz, int dir);
};

class Level;

class BirchFeature : public Feature
{
public:
	BirchFeature(bool doUpdate);
    virtual bool place(Level *level, Random *random, int x, int y, int z);
};

class PineFeature : public Feature
{
public:
    virtual bool place(Level *level, Random *random, int x, int y, int z);
};

class SpruceFeature : public Feature
{
public:
	SpruceFeature(bool doUpdate);
    virtual bool place(Level *level, Random *random, int x, int y, int z);
};

class MegaTreeFeature : public Feature
{
private:
	const int baseHeight;
	const int trunkType;
	const int leafType;

public:
	MegaTreeFeature(bool doUpdate, int baseHeight, int trunkType, int leafType);

	bool place(Level *level, Random *random, int x, int y, int z);

private:
	void placeLeaves(Level *level, int x, int z, int topPosition, int baseRadius, Random *random);
};
class SwampTreeFeature : public Feature
{
public:
	virtual bool place(Level *level, Random *random, int x, int y, int z);
private:
	void addVine(Level *level, int xx, int yy, int zz, int dir);
};
class GroundBushFeature : public Feature
{
private:
	int leafTileType;
	int trunkTileType;

public:
	GroundBushFeature(int trunkType, int leafType);

	virtual bool place(Level *level, Random *random, int x, int y, int z);
};
class Level;

class BasicTree : public Feature
{
private:
    // The axisConversionArray, when given a primary index, allows easy
    // access to the indices of the other two axies.  Access the data at the
    // primary index location to get the horizontal secondary axis.
    // Access the data at the primary location plus three to get the
    // remaining, tertiary, axis.
    // All directions are specified by an index, 0, 1, or 2 which
    // correspond to x, y, and z.
    // The axisConversionArray is used in several places
    // notably the crossection and taperedLimb methods.
    // Example:
    // If the primary axis is z, then the primary index is 2.
    // The secondary index is axisConversionArray[2] which is 0,
    // the index for the x axis.
    // The remaining axis is axisConversionArray[2 + 3] which is 1,
    // the index for the y axis.
    // Using this method, the secondary axis will always be horizontal (x or z),
    // and the tertiary always vertical (y), if possible.
    static byte axisConversionArray[];

    // Set up the pseudorandom number generator
    Random *rnd;

    // Make fields to hold the level data and the random seed
    Level *thisLevel;

    // Field to hold the tree origin, x y and z.
    int origin[3];
    // Field to hold the tree height.
    int height;
    // Other important tree information.
    int trunkHeight;
    double trunkHeightScale;
    double branchDensity;
    double branchSlope;
    double widthScale;
    double foliageDensity;
    int trunkWidth;
    int heightVariance;
    int foliageHeight;
    // The foliage coordinates are a list of [x,y,z,y of branch base] values for each cluster
    int **foliageCoords;
	int foliageCoordsLength;
	void prepare();
	void crossection(int x, int y, int z, float radius, byte direction, int material);
	float treeShape(int y);
	float foliageShape(int y);
	void foliageCluster(int x, int y, int z);
	void limb(int *start, int *end, int material);
	void makeFoliage();
	bool trimBranches(int localY);
	void makeTrunk();
	void makeBranches();
	int checkLine(int *start, int *end);
	bool checkLocation();

public:
	BasicTree(bool doUpdate);
	virtual ~BasicTree();

	virtual void init(double heightInit, double widthInit, double foliageDensityInit);
	virtual bool place(Level *level, Random *random, int x, int y, int z);
};

class Level;

class FlowerFeature : public Feature
{
private:
	int tile;

public:
	FlowerFeature (int tile);
	bool place(Level *level, Random *random, int x, int y, int z);
};
class TallGrassFeature : public Feature
{
private:
	int tile;
	int type;

public:
	TallGrassFeature (int tile, int type);

	virtual bool place(Level *level, Random *random, int x, int y, int z);
};
class ReedsFeature : public Feature
{
public:
    virtual bool place(Level *level, Random *random, int x, int y, int z);
};
class CactusFeature : public Feature
{
public:
    virtual bool place(Level *level, Random *random, int x, int y, int z);
};