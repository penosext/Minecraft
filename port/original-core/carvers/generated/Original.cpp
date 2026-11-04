#include "Platform.h"
namespace original_carvers { namespace detail {


Feature::Feature()
{
	this->doUpdate = false;
}

Feature::Feature(bool doUpdate)
{
	this->doUpdate = doUpdate;
}

void Feature::applyFeature(Level *level, Random *random, int xChunk, int zChunk)
{
}

void Feature::placeBlock(Level *level, int x, int y, int z, int tile)
{
	placeBlock(level, x, y, z, tile, 0);
}

void Feature::placeBlock(Level *level, int x, int y, int z, int tile, int data)
{
	if (doUpdate)
	{
		level->setTileAndData(x, y, z, tile, data, Tile::UPDATE_ALL);
	}
	else
	{
		level->setTileAndData(x, y, z, tile, data, Tile::UPDATE_CLIENTS);
	}
}

const wstring LargeFeature::STRONGHOLD = L"StrongHold";

LargeFeature::LargeFeature()
{
	radius = 8;
	random = new Random();
}

LargeFeature::~LargeFeature()
{
	delete random;
}

void LargeFeature::apply(ChunkSource *ChunkSource, Level *level, int xOffs, int zOffs, byteArray blocks)
{
    int r = radius;
	this->level = level;

    random->setSeed(level->getSeed());
    int64_t xScale = random->nextLong();
    int64_t zScale = random->nextLong();

    for (int x = xOffs - r; x <= xOffs + r; x++)
	{
        for (int z = zOffs - r; z <= zOffs + r; z++)
		{
            int64_t xx = x * xScale;
            int64_t zz = z * zScale;
            random->setSeed(xx ^ zz ^ level->getSeed());
            addFeature(level, x, z, xOffs, zOffs, blocks);
        }
    }
}

void LargeCaveFeature::addRoom(int64_t seed, int xOffs, int zOffs, byteArray blocks, double xRoom, double yRoom, double zRoom)
{
	addTunnel(seed, xOffs, zOffs, blocks, xRoom, yRoom, zRoom, 1 + random->nextFloat() * 6, 0, 0, -1, -1, 0.5);
}

void LargeCaveFeature::addTunnel(int64_t seed, int xOffs, int zOffs, byteArray blocks, double xCave, double yCave, double zCave, float thickness, float yRot, float xRot, int step, int dist, double yScale)
{
	double xMid = xOffs * 16 + 8;
	double zMid = zOffs * 16 + 8;

	float yRota = 0;
	float xRota = 0;
	Random random = Random(seed);

	if (dist <= 0)
	{
		int max = radius * 16 - 16;
		dist = max - random.nextInt(max / 4);
	}
	bool singleStep = false;

	if (step == -1)
	{
		step = dist / 2;
		singleStep = true;
	}


	int splitPoint = random.nextInt(dist / 2) + dist / 4;
	bool steep = random.nextInt(6) == 0;

	for (; step < dist; step++)
	{
		double rad = 1.5 + (Mth::sin(step * PI / dist) * thickness) * 1;
		double yRad = rad * yScale;

		float xc = Mth::cos(xRot);
		float xs = Mth::sin(xRot);
		xCave += Mth::cos(yRot) * xc;
		yCave += xs;
		zCave += Mth::sin(yRot) * xc;

		if (steep)
		{
			xRot *= 0.92f;
		}
		else
		{
			xRot *= 0.7f;
		}
		xRot += xRota * 0.1f;
		yRot += yRota * 0.1f;

		xRota *= 0.90f;
		yRota *= 0.75f;
		xRota += (random.nextFloat() - random.nextFloat()) * random.nextFloat() * 2;
		yRota += (random.nextFloat() - random.nextFloat()) * random.nextFloat() * 4;


		if (!singleStep && step == splitPoint && thickness > 1 && dist > 0)
		{
			addTunnel(random.nextLong(), xOffs, zOffs, blocks, xCave, yCave, zCave, random.nextFloat() * 0.5f + 0.5f, yRot - PI / 2, xRot / 3, step, dist, 1.0);
			addTunnel(random.nextLong(), xOffs, zOffs, blocks, xCave, yCave, zCave, random.nextFloat() * 0.5f + 0.5f, yRot + PI / 2, xRot / 3, step, dist, 1.0);
			return;
		}
		if (!singleStep && random.nextInt(4) == 0) continue;
		{
			double xd = xCave - xMid;
			double zd = zCave - zMid;
			double remaining = dist - step;
			double rr = (thickness + 2) + 16;
			if (xd * xd + zd * zd - (remaining * remaining) > rr * rr)
			{
				return;
			}
		}

		if (xCave < xMid - 16 - rad * 2 || zCave < zMid - 16 - rad * 2 || xCave > xMid + 16 + rad * 2 || zCave > zMid + 16 + rad * 2) continue;

		int x0 = Mth::floor(xCave - rad) - xOffs * 16 - 1;
		int x1 = Mth::floor(xCave + rad) - xOffs * 16 + 1;

		int y0 = Mth::floor(yCave - yRad) - 1;
		int y1 = Mth::floor(yCave + yRad) + 1;

		int z0 = Mth::floor(zCave - rad) - zOffs * 16 - 1;
		int z1 = Mth::floor(zCave + rad) - zOffs * 16 + 1;

		if (x0 < 0) x0 = 0;
		if (x1 > 16) x1 = 16;

		if (y0 < 1) y0 = 1;
		if (y1 > Level::genDepth - 8) y1 = Level::genDepth - 8;

		if (z0 < 0) z0 = 0;
		if (z1 > 16) z1 = 16;

		bool detectedWater = false;
		for (int xx = x0; !detectedWater && xx < x1; xx++)
		{
			for (int zz = z0; !detectedWater && zz < z1; zz++)
			{
				for (int yy = y1 + 1; !detectedWater && yy >= y0 - 1; yy--)
				{
					int p = (xx * 16 + zz) * Level::genDepth + yy;
					if (yy < 0 || yy >= Level::genDepth) continue;
					if (blocks[p] == Tile::water_Id || blocks[p] == Tile::calmWater_Id)
					{
						detectedWater = true;
					}
					if (yy != y0 - 1 && xx != x0 && xx != x1 - 1 && zz != z0 && zz != z1 - 1)
					{
						yy = y0;
					}
				}
			}
		}
		if (detectedWater) continue;

		for (int xx = x0; xx < x1; xx++)
		{
			double xd = ((xx + xOffs * 16 + 0.5) - xCave) / rad;
			for (int zz = z0; zz < z1; zz++)
			{
				double zd = ((zz + zOffs * 16 + 0.5) - zCave) / rad;
				int p = (xx * 16 + zz) * Level::genDepth + y1;
				bool hasGrass = false;
				if (xd * xd + zd * zd < 1)
				{
					for (int yy = y1 - 1; yy >= y0; yy--)
					{
						double yd = (yy + 0.5 - yCave) / yRad;
						if (yd > -0.7 && xd * xd + yd * yd + zd * zd < 1)
						{
							int block = blocks[p];
							if (block == Tile::grass_Id) hasGrass = true;
							if (block == Tile::stone_Id || block == Tile::dirt_Id || block == Tile::grass_Id)
							{
								if (yy < 10)
								{
									blocks[p] = static_cast<byte>(Tile::lava_Id);
								}
								else
								{
									blocks[p] = static_cast<byte>(0);
									if (hasGrass && blocks[p - 1] == Tile::dirt_Id) blocks[p - 1] = (byte) level->getBiome(xx + xOffs * 16, zz + zOffs * 16)->topMaterial;
								}
							}
						}
						p--;
					}
				}
			}
		}
		if (singleStep) break;
	}

}

void LargeCaveFeature::addFeature(Level *level, int x, int z, int xOffs, int zOffs, byteArray blocks)
{
	int caves = random->nextInt(random->nextInt(random->nextInt(40) + 1) + 1);
	if (random->nextInt(15) != 0) caves = 0;

	for (int cave = 0; cave < caves; cave++)
	{
		double xCave = x * 16 + random->nextInt(16);
		double yCave = random->nextInt(random->nextInt(Level::genDepth - 8) + 8);
		double zCave = z * 16 + random->nextInt(16);

		int tunnels = 1;
		if (random->nextInt(4) == 0)
		{
			addRoom(random->nextLong(), xOffs, zOffs, blocks, xCave, yCave, zCave);
			tunnels += random->nextInt(4);
		}

		for (int i = 0; i < tunnels; i++)
		{

			float yRot = random->nextFloat() * PI * 2;
			float xRot = ((random->nextFloat() - 0.5f) * 2) / 8;
			float thickness = random->nextFloat() * 2 + random->nextFloat();
			if (random->nextInt(10) == 0) thickness *= random->nextFloat() * random->nextFloat() * 3 + 1;

			addTunnel(random->nextLong(), xOffs, zOffs, blocks, xCave, yCave, zCave, thickness, yRot, xRot, 0, 0, 1.0);
		}
	}

}


void CanyonFeature::addTunnel(int64_t seed, int xOffs, int zOffs, byteArray blocks, double xCave, double yCave, double zCave, float thickness, float yRot, float xRot, int step, int dist, double yScale)
{
	MemSect(49);
	Random *random = new Random(seed);
	MemSect(0);
	double xMid = xOffs * 16 + 8;
	double zMid = zOffs * 16 + 8;

	float yRota = 0;
	float xRota = 0;
	//        int dist = CAVE_RADIUS * 16 - 16;
	//        if (step>0) dist = step*2;

	if (dist <= 0)
	{
		int max = radius * 16 - 16;
		dist = max - random->nextInt(max / 4);
	}
	bool singleStep = false;

	if (step == -1)
	{
		step = dist / 2;
		singleStep = true;
	}

	float f = 1;
	for (int i = 0; i < Level::genDepth; i++)
	{
		if (i == 0 || random->nextInt(3) == 0)
		{
			f = 1 + (random->nextFloat() * random->nextFloat()) * 1.0f;
		}
		rs[i] = f * f;
	}

	for (; step < dist; step++)
	{
		double rad = 1.5 + (Mth::sin(step * PI / dist) * thickness) * 1;
		double yRad = rad * yScale;

		rad *= (random->nextFloat() * 0.25 + 0.75);
		yRad *= (random->nextFloat() * 0.25 + 0.75);

		float xc = Mth::cos(xRot);
		float xs = Mth::sin(xRot);
		xCave += Mth::cos(yRot) * xc;
		yCave += xs;
		zCave += Mth::sin(yRot) * xc;

		xRot *= 0.7f;

		xRot += xRota * 0.05f;
		yRot += yRota * 0.05f;

		xRota *= 0.80f;
		yRota *= 0.50f;
		xRota += (random->nextFloat() - random->nextFloat()) * random->nextFloat() * 2;
		yRota += (random->nextFloat() - random->nextFloat()) * random->nextFloat() * 4;

		if (!singleStep && random->nextInt(4) == 0) continue;

		{
			double xd = xCave - xMid;
			double zd = zCave - zMid;
			double remaining = dist - step;
			double rr = (thickness + 2) + 16;
			if (xd * xd + zd * zd - (remaining * remaining) > rr * rr)
			{
				delete random;
				return;
			}
		}

		if (xCave < xMid - 16 - rad * 2 || zCave < zMid - 16 - rad * 2 || xCave > xMid + 16 + rad * 2 || zCave > zMid + 16 + rad * 2) continue;

		int x0 = Mth::floor(xCave - rad) - xOffs * 16 - 1;
		int x1 = Mth::floor(xCave + rad) - xOffs * 16 + 1;

		int y0 = Mth::floor(yCave - yRad) - 1;
		int y1 = Mth::floor(yCave + yRad) + 1;

		int z0 = Mth::floor(zCave - rad) - zOffs * 16 - 1;
		int z1 = Mth::floor(zCave + rad) - zOffs * 16 + 1;

		if (x0 < 0) x0 = 0;
		if (x1 > 16) x1 = 16;

		if (y0 < 1) y0 = 1;
		if (y1 > Level::genDepth - 8) y1 = Level::genDepth - 8;

		if (z0 < 0) z0 = 0;
		if (z1 > 16) z1 = 16;

		bool detectedWater = false;
		for (int xx = x0; !detectedWater && xx < x1; xx++)
		{
			for (int zz = z0; !detectedWater && zz < z1; zz++)
			{
				for (int yy = y1 + 1; !detectedWater && yy >= y0 - 1; yy--)
				{
					int p = (xx * 16 + zz) * Level::genDepth + yy;
					if (yy < 0 || yy >= Level::genDepth) continue;
					if (blocks[p] == Tile::water_Id || blocks[p] == Tile::calmWater_Id)
					{
						detectedWater = true;
					}
					if (yy != y0 - 1 && xx != x0 && xx != x1 - 1 && zz != z0 && zz != z1 - 1)
					{
						yy = y0;
					}
				}
			}
		}
		if (detectedWater) continue;

		for (int xx = x0; xx < x1; xx++)
		{
			double xd = ((xx + xOffs * 16 + 0.5) - xCave) / rad;
			for (int zz = z0; zz < z1; zz++)
			{
				double zd = ((zz + zOffs * 16 + 0.5) - zCave) / rad;
				int p = (xx * 16 + zz) * Level::genDepth + y1;
				bool hasGrass = false;
				if (xd * xd + zd * zd < 1)
				{
					for (int yy = y1 - 1; yy >= y0; yy--)
					{
						double yd = (yy + 0.5 - yCave) / yRad;
						if ((xd * xd + zd * zd) * rs[yy] + (yd * yd / 6) < 1)
						{
							int block = blocks[p];
							if (block == Tile::grass_Id) hasGrass = true;
							if (block == Tile::stone_Id || block == Tile::dirt_Id || block == Tile::grass_Id)
							{
								if (yy < 10)
								{
									blocks[p] = static_cast<byte>(Tile::lava_Id);
								}
								else
								{
									blocks[p] = static_cast<byte>(0);
									if (hasGrass && blocks[p - 1] == Tile::dirt_Id) blocks[p - 1] = (byte) level->getBiome(xx + xOffs * 16, zz + zOffs * 16)->topMaterial;
								}
							}
						}
						p--;
					}
				}
			}
		}
		if (singleStep) break;
	}
	delete random;
}

void CanyonFeature::addFeature(Level *level, int x, int z, int xOffs, int zOffs, byteArray blocks)
{
	if (random->nextInt(50) != 0) return;

	double xCave = x * 16 + random->nextInt(16);
	double yCave = random->nextInt(random->nextInt(40) + 8) + 20;
	double zCave = z * 16 + random->nextInt(16);

	int tunnels = 1;

	for (int i = 0; i < tunnels; i++)
	{
		float yRot = random->nextFloat() * PI * 2;
		float xRot = ((random->nextFloat() - 0.5f) * 2) / 8;
		float thickness = (random->nextFloat() * 2 + random->nextFloat()) * 2;

		addTunnel(random->nextLong(), xOffs, zOffs, blocks, xCave, yCave, zCave, thickness, yRot, xRot, 0, 0, 3.0);

		// 4J Add to feature list
		app.AddTerrainFeaturePosition(eTerrainFeature_Ravine,static_cast<int>(xCave / 16.0),static_cast<int>(yCave / 16.0));
	}

}

void OreFeature::_init(int tile, int count, int targetTile)
{
	this->tile = tile;
	this->count = count;
	this->targetTile = targetTile;
}

OreFeature::OreFeature(int tile, int count)
{
	_init(tile, count, Tile::stone_Id);
}

OreFeature::OreFeature(int tile, int count, int targetTile)
{
	_init(tile, count, targetTile);
}

bool OreFeature::place(Level *level, Random *random, int x, int y, int z)
{
	PIXBeginNamedEvent(0,"Place Ore Feature");
	float dir = random->nextFloat() * PI;

	double x0 = x + 8 + Mth::sin(dir) * count / 8;
	double x1 = x + 8 - Mth::sin(dir) * count / 8;
	double z0 = z + 8 + Mth::cos(dir) * count / 8;
	double z1 = z + 8 - Mth::cos(dir) * count / 8;

	double y0 = y + random->nextInt(3) - 2;
	double y1 = y + random->nextInt(3) - 2;

	bool collisionsExpected = false;

	LevelGenerationOptions *levelGenOptions = nullptr;
	if( app.getLevelGenerationOptions() != nullptr )
	{
		levelGenOptions = app.getLevelGenerationOptions();

		// 4J Stu - Optimise schematic intersection checks by first checking the max possible bounding box of this place call
		int minX = x0 - 1;
		int minY = y0 - 1;
		int minZ = z0 - 1;

		double maxss = count / 16;
		double maxr = (Mth::sin(PI) + 1) * maxss + 1;
		double maxhr = (Mth::sin(PI) + 1) * maxss + 1;
		int maxX = Mth::floor(x1 + maxr / 2);
		int maxY = Mth::floor(y1 + maxhr / 2);
		int maxZ = Mth::floor(z1 + maxr / 2);

		collisionsExpected = levelGenOptions->checkIntersects(minX, minY, minZ, maxX, maxY, maxZ);
	}

	bool doEarlyRejectTest = false;
	if( y0 > level->getSeaLevel() )
	{
		doEarlyRejectTest = true;
	}

	for (int d = 0; d <= count; d++)
	{
		double xx = x0 + (x1 - x0) * d / count;
		double yy = y0 + (y1 - y0) * d / count;
		double zz = z0 + (z1 - z0) * d / count;

        double ss = random->nextDouble() * count / 16;
        double r = (Mth::sin(d * PI / count) + 1) * ss + 1;
		double hr = r; //(Mth::sin(d * PI / count) + 1) * ss + 1;

		double halfR = r/2;
		double halfHR = halfR; //hr/2;

		int xt0 = Mth::floor(xx - halfR);
		int yt0 = Mth::floor(yy - halfHR);
		int zt0 = Mth::floor(zz - halfR);

		int xt1 = Mth::floor(xx + halfR);
		int yt1 = Mth::floor(yy + halfHR);
		int zt1 = Mth::floor(zz + halfR);

		// 4J Stu Added to stop ore features generating areas previously place by game rule generation
		if(collisionsExpected && levelGenOptions != nullptr)
		{
			bool intersects = levelGenOptions->checkIntersects(xt0, yt0, zt0, xt1, yt1, zt1);
			if(intersects)
			{
				//app.DebugPrintf("Skipping ore feature generation as it overlaps a game rule structure\n");
				continue;
			}
		}

		// A large % of ore placement is entirely into the air. Attempt to identify some of these early, by check the corners
		// of the area we are placing in to see if we are going to (very probably) be entirely above the height stored in the heightmap

		if( doEarlyRejectTest )
		{
			bool earlyReject = true;
			if     ( level->getHeightmap(xt0, zt0) >= yt0 ) earlyReject = false;
			else if( level->getHeightmap(xt1, zt0) >= yt0 ) earlyReject = false;
			else if( level->getHeightmap(xt0, zt1) >= yt0 ) earlyReject = false;
			else if( level->getHeightmap(xt1, zt1) >= yt0 ) earlyReject = false;

			if( earlyReject ) continue;
		}

		double xdxd,ydyd;

		double xd0 = ((xt0 + 0.5) - xx);
		double yd0 = ((yt0 + 0.5) - yy);
		double zd0 = ((zt0 + 0.5) - zz);

		double halfRSq = halfR * halfR;

		double xd = xd0;
        for (int x2 = xt0; x2 <= xt1; x2++, xd++)
		{
			xdxd = xd * xd;
            if (xdxd < halfRSq)
			{
				double yd = yd0;
                for (int y2 = yt0; y2 <= yt1; y2++, yd++)
				{
					ydyd = yd * yd;
                    if (xdxd + ydyd < halfRSq)
					{
						double zd = zd0;
                        for (int z2 = zt0; z2 <= zt1; z2++, zd++)
						{
                           	if (xdxd + ydyd + zd * zd < halfRSq)
							{
                                if ( level->getTile(x2, y2, z2) == targetTile)
								{									
									level->setTileAndData(x2, y2, z2, tile, 0, Tile::UPDATE_INVISIBLE_NO_LIGHT);
								}
                            }
                        }
					}
                }
            }
        }
    }

	PIXEndNamedEvent();
    return true;
}

float *Mth::_sin = nullptr;

const float Mth::sinScale = 65536.0f / (float) (PI * 2);

void Mth::init()
{
	_sin = new float[65536];
	for (int i = 0; i < 65536; i++)
	{
		_sin[i] = (float) ::sin(i * PI * 2 / 65536.0f);
	}
}

float Mth::sin(float i)
{
	if(_sin == nullptr) init();		// 4J - added
	return _sin[static_cast<int>(i * sinScale) & 65535];
}

float Mth::cos(float i)
{
	if(_sin == nullptr) init();		// 4J - added
	return _sin[static_cast<int>(i * sinScale + 65536 / 4) & 65535];
}

int Mth::floor(float v)
{
	int i = static_cast<int>(v);
	return v < i ? i - 1 : i;
}

int Mth::floor(double v)
{
	int i = static_cast<int>(v);
	return v < i ? i - 1 : i;
}

void BiomeDecorator::decorateDepthSpan(int count, Feature *feature, int y0, int y1)
{
	for (int i = 0; i < count; i++)
	{
		int x = xo + random->nextInt(16);
		int y = random->nextInt(y1 - y0) + y0;
		int z = zo + random->nextInt(16);
		feature->place(level, random, x, y, z);
	}
}

void BiomeDecorator::decorateDepthAverage(int count, Feature *feature, int yMid, int ySpan)
{
	for (int i = 0; i < count; i++)
	{
		int x = xo + random->nextInt(16);
		int y = random->nextInt(ySpan) + random->nextInt(ySpan) + (yMid - ySpan);
		int z = zo + random->nextInt(16);
		feature->place(level, random, x, y, z);
	}
}

void BiomeDecorator::decorateOres()
{
	level->setInstaTick(true);		// 4J - optimisation
	decorateDepthSpan(20, dirtOreFeature, 0, Level::genDepth);
	decorateDepthSpan(10, gravelOreFeature, 0, Level::genDepth);
	decorateDepthSpan(20, coalOreFeature, 0, Level::genDepth);
	decorateDepthSpan(20, ironOreFeature, 0, Level::genDepth / 2);
	decorateDepthSpan(2, goldOreFeature, 0, Level::genDepth / 4);
	decorateDepthSpan(8, redStoneOreFeature, 0, Level::genDepth / 8);
	decorateDepthSpan(1, diamondOreFeature, 0, Level::genDepth / 8);
	decorateDepthAverage(1, lapisOreFeature, Level::genDepth / 8, Level::genDepth / 8);
	level->setInstaTick(false);
}

} }
