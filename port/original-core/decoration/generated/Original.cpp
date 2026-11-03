#include "Platform.h"
namespace original_decoration { namespace detail {


MaterialColor **MaterialColor::colors;
		   
MaterialColor *MaterialColor::none = nullptr;
MaterialColor *MaterialColor::grass = nullptr;
MaterialColor *MaterialColor::sand = nullptr;
MaterialColor *MaterialColor::cloth = nullptr;
MaterialColor *MaterialColor::fire = nullptr;
MaterialColor *MaterialColor::ice = nullptr;
MaterialColor *MaterialColor::metal = nullptr;
MaterialColor *MaterialColor::plant = nullptr;
MaterialColor *MaterialColor::snow = nullptr;
MaterialColor *MaterialColor::clay = nullptr;
MaterialColor *MaterialColor::dirt = nullptr;
MaterialColor *MaterialColor::stone = nullptr;
MaterialColor *MaterialColor::water = nullptr;
MaterialColor *MaterialColor::wood = nullptr;

void MaterialColor::staticCtor()
{
	MaterialColor::colors = new MaterialColor *[16];

	MaterialColor::none = new MaterialColor(0, eMinecraftColour_Material_None);
	MaterialColor::grass = new MaterialColor(1, eMinecraftColour_Material_Grass);
	MaterialColor::sand = new MaterialColor(2, eMinecraftColour_Material_Sand);
	MaterialColor::cloth = new MaterialColor(3, eMinecraftColour_Material_Cloth);
	MaterialColor::fire = new MaterialColor(4, eMinecraftColour_Material_Fire);
	MaterialColor::ice = new MaterialColor(5, eMinecraftColour_Material_Ice);
	MaterialColor::metal = new MaterialColor(6, eMinecraftColour_Material_Metal);
	MaterialColor::plant = new MaterialColor(7, eMinecraftColour_Material_Plant);
	MaterialColor::snow = new MaterialColor(8, eMinecraftColour_Material_Snow);
	MaterialColor::clay = new MaterialColor(9, eMinecraftColour_Material_Clay);
	MaterialColor::dirt = new MaterialColor(10, eMinecraftColour_Material_Dirt);
	MaterialColor::stone = new MaterialColor(11, eMinecraftColour_Material_Stone);
	MaterialColor::water = new MaterialColor(12, eMinecraftColour_Material_Water);
	MaterialColor::wood = new MaterialColor(13, eMinecraftColour_Material_Wood);
}

MaterialColor::MaterialColor(int id, eMinecraftColour col)
{
	this->id = id;
	this->col = col;
	colors[id] = this;
}

Material *Material::air = nullptr;
Material *Material::grass = nullptr;
Material *Material::dirt = nullptr;
Material *Material::wood = nullptr;
Material *Material::stone = nullptr;
Material *Material::metal = nullptr;
Material *Material::heavyMetal = nullptr;
Material *Material::water = nullptr;
Material *Material::lava = nullptr;
Material *Material::leaves = nullptr;
Material *Material::plant = nullptr;
Material *Material::replaceable_plant = nullptr;
Material *Material::sponge = nullptr;
Material *Material::cloth = nullptr;
Material *Material::fire = nullptr;
Material *Material::sand = nullptr;
Material *Material::decoration = nullptr;
Material *Material::clothDecoration = nullptr;
Material *Material::glass = nullptr;
Material *Material::buildable_glass = nullptr;
Material *Material::explosive = nullptr;
Material *Material::coral = nullptr;
Material *Material::ice = nullptr;
Material *Material::topSnow = nullptr;
Material *Material::snow = nullptr;
Material *Material::cactus = nullptr;
Material *Material::clay = nullptr;
Material *Material::vegetable = nullptr;
Material *Material::egg = nullptr;
Material *Material::portal = nullptr;
Material *Material::cake = nullptr;
Material *Material::piston = nullptr;
Material *Material::web = nullptr;

void Material::staticCtor()
{
	Material::air = new GasMaterial(MaterialColor::none);
	Material::grass = new Material(MaterialColor::grass);
	Material::dirt = new Material(MaterialColor::dirt);
	Material::wood = (new Material(MaterialColor::wood))->flammable();
	Material::stone = (new Material(MaterialColor::stone))->notAlwaysDestroyable();
	Material::metal = (new Material(MaterialColor::metal))->notAlwaysDestroyable();
	Material::heavyMetal = (new Material(MaterialColor::metal))->notAlwaysDestroyable()->notPushable();
	Material::water = (new LiquidMaterial(MaterialColor::water))->destroyOnPush();
	Material::lava = (new LiquidMaterial(MaterialColor::fire))->destroyOnPush();
	Material::leaves = (new Material(MaterialColor::plant))->flammable()->neverBuildable()->destroyOnPush();
	Material::plant = (new DecorationMaterial(MaterialColor::plant))->destroyOnPush();
	Material::replaceable_plant = (new DecorationMaterial(MaterialColor::plant))->flammable()->destroyOnPush()->replaceable();
	Material::sponge = new Material(MaterialColor::cloth);
	Material::cloth = (new Material(MaterialColor::cloth))->flammable();
	Material::fire = (new GasMaterial(MaterialColor::none))->destroyOnPush();
	Material::sand = new Material(MaterialColor::sand);
	Material::decoration = (new DecorationMaterial(MaterialColor::none))->destroyOnPush();
	Material::clothDecoration = (new DecorationMaterial(MaterialColor::cloth))->flammable();
	Material::glass = (new Material(MaterialColor::none))->neverBuildable()->makeDestroyedByHand();
	Material::buildable_glass = (new Material(MaterialColor::none))->makeDestroyedByHand();
	Material::explosive = (new Material(MaterialColor::fire))->flammable()->neverBuildable();
	Material::coral = (new Material(MaterialColor::plant))->destroyOnPush();
	Material::ice = (new Material(MaterialColor::ice))->neverBuildable()->makeDestroyedByHand();
	Material::topSnow = (new DecorationMaterial(MaterialColor::snow))->replaceable()->neverBuildable()->notAlwaysDestroyable()->destroyOnPush();
	Material::snow = (new Material(MaterialColor::snow))->notAlwaysDestroyable();
	Material::cactus = (new Material(MaterialColor::plant))->neverBuildable()->destroyOnPush();
	Material::clay = (new Material(MaterialColor::clay));
	Material::vegetable = (new Material(MaterialColor::plant))->destroyOnPush();
	Material::egg = ( new Material(MaterialColor::plant))->destroyOnPush();
	Material::portal = (new PortalMaterial(MaterialColor::none))->notPushable();
	Material::cake = (new Material(MaterialColor::none))->destroyOnPush();
	// 4J added WebMaterial, Java version just does a local alteration when instantiating the Material for webs to get the same thing
	Material::web = (new WebMaterial(MaterialColor::cloth))->notAlwaysDestroyable()->destroyOnPush();
	Material::piston  = (new Material(MaterialColor::stone))->notPushable();
}

Material::Material(MaterialColor *color)
{
	this->color = color;

	// 4J Stu - Default inits
	_flammable = false;
	_replaceable = false;
	_neverBuildable = false;
	_isAlwaysDestroyable = true;
	pushReaction = 0;
	destroyedByHand = false;
}

bool Material::isLiquid()
{
    return false;
}

bool Material::letsWaterThrough()
{
    return (!isLiquid() && !isSolid());
}

bool Material::isSolid()
{
    return true;
}

bool Material::blocksLight()
{
    return true;
}

bool Material::blocksMotion()
{
    return true;
}

Material *Material::neverBuildable()
{
    this->_neverBuildable = true;
    return this;
}

Material *Material::notAlwaysDestroyable()
{
	this->_isAlwaysDestroyable = false;
	return this;
}

Material *Material::flammable()
{
    this->_flammable = true;
    return this;
}

bool Material::isFlammable()
{
    return _flammable;
}

Material *Material::replaceable()
{
    this->_replaceable = true;
    return this;
}

bool Material::isReplaceable()
{
    return _replaceable;
}

bool Material::isSolidBlocking()
{
    if (_neverBuildable) return false;
    return blocksMotion();
}

bool Material::isAlwaysDestroyable()
{
    // these materials will always drop resources when destroyed, regardless
	// of player's equipment
    return _isAlwaysDestroyable;
}

int Material::getPushReaction()
{
	return pushReaction;
}

Material *Material::makeDestroyedByHand()
{
	this->destroyedByHand = true;
	return this;
}

bool Material::isDestroyedByHand()
{
	return destroyedByHand;
}

Material *Material::destroyOnPush()
{
	pushReaction = PUSH_DESTROY;
	return this;
}

Material *Material::notPushable()
{
	pushReaction = PUSH_BLOCK;
	return this;
}

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

TreeFeature::TreeFeature(bool doUpdate) : Feature(doUpdate), baseHeight(4), trunkType(0), leafType(0), addJungleFeatures(false)
{
}

TreeFeature::TreeFeature(bool doUpdate, int baseHeight, int trunkType, int leafType, bool addJungleFeatures) : Feature(doUpdate), baseHeight(baseHeight), trunkType(trunkType), leafType(leafType), addJungleFeatures(addJungleFeatures)
{
}

bool TreeFeature::place(Level *level, Random *random, int x, int y, int z)
{	
	int treeHeight = random->nextInt(3) + baseHeight;

	bool free = true;
	if (y < 1 || y + treeHeight + 1 > Level::maxBuildHeight) return false;

	// 4J Stu Added to stop tree features generating areas previously place by game rule generation
	if(app.getLevelGenerationOptions() != nullptr)
	{
		PIXBeginNamedEvent(0,"TreeFeature checking intersects");
		LevelGenerationOptions *levelGenOptions = app.getLevelGenerationOptions();
		bool intersects = levelGenOptions->checkIntersects(x - 2, y - 1, z - 2, x + 2, y + treeHeight, z + 2);
		PIXEndNamedEvent();
		if(intersects)
		{
			//app.DebugPrintf("Skipping reeds feature generation as it overlaps a game rule structure\n");
			return false;
		}
	}

	for (int yy = y; yy <= y + 1 + treeHeight; yy++)
	{
		int r = 1;
		if (yy == y) r = 0;
		if (yy >= y + 1 + treeHeight - 2) r = 2;
		for (int xx = x - r; xx <= x + r && free; xx++)
		{
			for (int zz = z - r; zz <= z + r && free; zz++)
			{
				if (yy >= 0 && yy < Level::maxBuildHeight)
				{
					int tt = level->getTile(xx, yy, zz);
					if (tt != 0 && tt != Tile::leaves_Id && tt != Tile::grass_Id && tt != Tile::dirt_Id && tt != Tile::treeTrunk_Id) free = false;
				}
				else
				{
					free = false;
				}
			}
		}
	}

	if (!free) return false;

	int belowTile = level->getTile(x, y - 1, z);
	if ((belowTile != Tile::grass_Id && belowTile != Tile::dirt_Id) || y >= Level::maxBuildHeight - treeHeight - 1) return false;

	placeBlock(level, x, y - 1, z, Tile::dirt_Id, 0);
	
	PIXBeginNamedEvent(0,"Placing TreeFeature leaves");
	int grassHeight = 3;
	int extraWidth = 0;
	// 4J Stu - Generate leaves from the top down to stop having to recalc heightmaps
	for (int yy = y + treeHeight; yy >= y - grassHeight + treeHeight; yy--)
	{
		int yo = yy - (y + treeHeight);
		int offs = extraWidth + 1 - yo / 2;
		for (int xx = x - offs; xx <= x + offs; xx++)
		{
			int xo = xx - (x);
			for (int zz = z - offs; zz <= z + offs; zz++)
			{
				int zo = zz - (z);
				if (abs(xo) == offs && abs(zo) == offs && (random->nextInt(2) == 0 || yo == 0)) continue;
				int t = level->getTile(xx, yy, zz);
				if (t == 0 || t == Tile::leaves_Id) placeBlock(level, xx, yy, zz, Tile::leaves_Id, leafType);
			}
		}
	}
	PIXEndNamedEvent();
	PIXBeginNamedEvent(0,"Placing TreeFeature trunks");
	for (int hh = 0; hh < treeHeight; hh++)
	{
		int t = level->getTile(x, y + hh, z);
		if (t == 0 || t == Tile::leaves_Id)
		{
			placeBlock(level, x, y + hh, z, Tile::treeTrunk_Id, trunkType);
			if (addJungleFeatures && hh > 0)
			{
				if (random->nextInt(3) > 0 && level->isEmptyTile(x - 1, y + hh, z))
				{
					placeBlock(level, x - 1, y + hh, z, Tile::vine_Id, VineTile::VINE_EAST);
				}
				if (random->nextInt(3) > 0 && level->isEmptyTile(x + 1, y + hh, z))
				{
					placeBlock(level, x + 1, y + hh, z, Tile::vine_Id, VineTile::VINE_WEST);
				}
				if (random->nextInt(3) > 0 && level->isEmptyTile(x, y + hh, z - 1))
				{
					placeBlock(level, x, y + hh, z - 1, Tile::vine_Id, VineTile::VINE_SOUTH);
				}
				if (random->nextInt(3) > 0 && level->isEmptyTile(x, y + hh, z + 1))
				{
					placeBlock(level, x, y + hh, z + 1, Tile::vine_Id, VineTile::VINE_NORTH);
				}
			}
		}
	}
	PIXEndNamedEvent();
	if (addJungleFeatures)
	{
		PIXBeginNamedEvent(0,"TreeFeature adding vines");
		for (int yy = y - 3 + treeHeight; yy <= y + treeHeight; yy++)
		{
			int yo = yy - (y + treeHeight);
			int offs = 2 - yo / 2;
			for (int xx = x - offs; xx <= x + offs; xx++)
			{
				for (int zz = z - offs; zz <= z + offs; zz++)
				{
					if (level->getTile(xx, yy, zz) == Tile::leaves_Id)
					{
						if (random->nextInt(4) == 0 && level->getTile(xx - 1, yy, zz) == 0)
						{
							addVine(level, xx - 1, yy, zz, VineTile::VINE_EAST);
						}
						if (random->nextInt(4) == 0 && level->getTile(xx + 1, yy, zz) == 0)
						{
							addVine(level, xx + 1, yy, zz, VineTile::VINE_WEST);
						}
						if (random->nextInt(4) == 0 && level->getTile(xx, yy, zz - 1) == 0)
						{
							addVine(level, xx, yy, zz - 1, VineTile::VINE_SOUTH);
						}
						if (random->nextInt(4) == 0 && level->getTile(xx, yy, zz + 1) == 0)
						{
							addVine(level, xx, yy, zz + 1, VineTile::VINE_NORTH);
						}
					}
				}
			}
		}
		PIXEndNamedEvent();
		// also chance for cocoa plants around stem
		if (random->nextInt(5) == 0 && treeHeight > 5)
		{
		PIXBeginNamedEvent(0,"TreeFeature adding cocoa");
			for (int rows = 0; rows < 2; rows++)
			{
				for (int dir = 0; dir < 4; dir++)
				{
					if (random->nextInt(4 - rows) == 0)
					{
						int age = random->nextInt(3);
						placeBlock(level, x + Direction::STEP_X[Direction::DIRECTION_OPPOSITE[dir]], y + treeHeight - 5 + rows, z + Direction::STEP_Z[Direction::DIRECTION_OPPOSITE[dir]],
							Tile::cocoa_Id, (age << 2) | dir);
					}
				}
			}
			PIXEndNamedEvent();
		}
	}

	return true;
}

void TreeFeature::addVine(Level *level, int xx, int yy, int zz, int dir)
{
	placeBlock(level, xx, yy, zz, Tile::vine_Id, dir);
	int maxDir = 4;
	while (level->getTile(xx, --yy, zz) == 0 && maxDir > 0)
	{
		placeBlock(level, xx, yy, zz, Tile::vine_Id, dir);
		maxDir--;
	}
}

BirchFeature::BirchFeature(bool doUpdate) : Feature(doUpdate)
{
}

bool BirchFeature::place(Level *level, Random *random, int x, int y, int z)
{
	int treeHeight = random->nextInt(3) + 5;

	bool free = true;
	if (y < 1 || y + treeHeight + 1 > Level::maxBuildHeight) return false;

	for (int yy = y; yy <= y + 1 + treeHeight; yy++)
	{
		int r = 1;
		if (yy == y) r = 0;
		if (yy >= y + 1 + treeHeight - 2) r = 2;
		for (int xx = x - r; xx <= x + r && free; xx++)
		{
			for (int zz = z - r; zz <= z + r && free; zz++)
			{
				if (yy >= 0 && yy < Level::maxBuildHeight)
				{
					int tt = level->getTile(xx, yy, zz);
					if (tt != 0 && tt != Tile::leaves_Id) free = false;
				}
				else
				{
					free = false;
				}
			}
		}
	}

	if (!free) return false;

	int belowTile = level->getTile(x, y - 1, z);
	if ((belowTile != Tile::grass_Id && belowTile != Tile::dirt_Id) || y >= Level::maxBuildHeight - treeHeight - 1) return false;

	// 4J Stu Added to stop tree features generating areas previously place by game rule generation
	if(app.getLevelGenerationOptions() != nullptr)
	{
		LevelGenerationOptions *levelGenOptions = app.getLevelGenerationOptions();
		int radius = 3;
		bool intersects = levelGenOptions->checkIntersects(x - radius, y - 1, z - radius, x + radius, y + treeHeight, z + radius);
		if(intersects)
		{
			//app.DebugPrintf("Skipping reeds feature generation as it overlaps a game rule structure\n");
			return false;
		}
	}

	placeBlock(level, x, y - 1, z, Tile::dirt_Id);

	for (int yy = y - 3 + treeHeight; yy <= y + treeHeight; yy++)
	{
		int yo = yy - (y + treeHeight);
		int offs = 1 - yo / 2;
		for (int xx = x - offs; xx <= x + offs; xx++)
		{
			int xo = xx - (x);
			for (int zz = z - offs; zz <= z + offs; zz++)
			{
				int zo = zz - (z);
				if (abs(xo) == offs && abs(zo) == offs && (random->nextInt(2) == 0 || yo == 0)) continue;
				int t = level->getTile(xx, yy, zz);
				if (t == 0 || t == Tile::leaves_Id) placeBlock(level, xx, yy, zz, Tile::leaves_Id, LeafTile::BIRCH_LEAF);
			}
		}
	}
	for (int hh = 0; hh < treeHeight; hh++)
	{
		int t = level->getTile(x, y + hh, z);
		if (t == 0 || t == Tile::leaves_Id) placeBlock(level, x, y + hh, z, Tile::treeTrunk_Id, TreeTile::BIRCH_TRUNK);
	}
	return true;

}

bool PineFeature::place(Level *level, Random *random, int x, int y, int z)
{
    // pines can be quite tall
    int treeHeight = random->nextInt(5) + 7;
    int trunkHeight = treeHeight - random->nextInt(2) - 3;
    int topHeight = treeHeight - trunkHeight;
    int topRadius = 1 + random->nextInt(topHeight + 1);

    bool free = true;
    // may not be outside of y boundaries
    if (y < 1 || y + treeHeight + 1 > Level::genDepth)
	{
        return false;
    }

	// 4J Stu Added to stop tree features generating areas previously place by game rule generation
	if(app.getLevelGenerationOptions() != nullptr)
	{
		LevelGenerationOptions *levelGenOptions = app.getLevelGenerationOptions();
		bool intersects = levelGenOptions->checkIntersects(x - topRadius, y - 1, z - topRadius, x + topRadius, y + treeHeight, z + topRadius);
		if(intersects)
		{
			//app.DebugPrintf("Skipping reeds feature generation as it overlaps a game rule structure\n");
			return false;
		}
	}

    // make sure there is enough space
    for (int yy = y; yy <= y + 1 + treeHeight && free; yy++)
	{

        int r = 1;
        if ((yy - y) < trunkHeight)
		{
            r = 0;
        }
		else
		{
            r = topRadius;
        }
        for (int xx = x - r; xx <= x + r && free; xx++)
		{
            for (int zz = z - r; zz <= z + r && free; zz++)
			{
                if (yy >= 0 && yy < Level::genDepth)
				{
                    int tt = level->getTile(xx, yy, zz);
                    if (tt != 0 && tt != Tile::leaves_Id) free = false;
                }
				else
				{
                    free = false;
                }
            }
        }
    }

    if (!free) return false;

    // must stand on ground
    int belowTile = level->getTile(x, y - 1, z);
    if ((belowTile != Tile::grass_Id && belowTile != Tile::dirt_Id) || y >= Level::genDepth - treeHeight - 1) return false;

    placeBlock(level, x, y - 1, z, Tile::dirt_Id);

    // place leaf top
    int currentRadius = 0;
    for (int yy = y + treeHeight; yy >= y + trunkHeight; yy--)
	{
        for (int xx = x - currentRadius; xx <= x + currentRadius; xx++)
		{
            int xo = xx - (x);
            for (int zz = z - currentRadius; zz <= z + currentRadius; zz++)
			{
                int zo = zz - (z);
                if (abs(xo) == currentRadius && abs(zo) == currentRadius && currentRadius > 0) continue;
                if (!Tile::solid[level->getTile(xx, yy, zz)]) placeBlock(level, xx, yy, zz, Tile::leaves_Id, LeafTile::EVERGREEN_LEAF);
            }
        }

        if (currentRadius >= 1 && yy == (y + trunkHeight + 1))
		{
            currentRadius -= 1;
        }
		else if (currentRadius < topRadius)
		{
            currentRadius += 1;
        }
    }
    for (int hh = 0; hh < treeHeight - 1; hh++)
	{
        int t = level->getTile(x, y + hh, z);
        if (t == 0 || t == Tile::leaves_Id) placeBlock(level, x, y + hh, z, Tile::treeTrunk_Id, TreeTile::DARK_TRUNK);
    }

    return true;
}

SpruceFeature::SpruceFeature(bool doUpdate) : Feature(doUpdate)
{
}

bool SpruceFeature::place(Level *level, Random *random, int x, int y, int z)
{
    // pines can be quite tall
    int treeHeight = random->nextInt(4) + 6;
    int trunkHeight = 1 + random->nextInt(2);
    int topHeight = treeHeight - trunkHeight;
    int leafRadius = 2 + random->nextInt(2);

    bool free = true;
    // may not be outside of y boundaries
	if (y < 1 || y + treeHeight + 1 > Level::maxBuildHeight)
	{
        return false;
    }

	// 4J Stu Added to stop tree features generating areas previously place by game rule generation
	if(app.getLevelGenerationOptions() != nullptr)
	{
		LevelGenerationOptions *levelGenOptions = app.getLevelGenerationOptions();
		bool intersects = levelGenOptions->checkIntersects(x - leafRadius, y - 1, z - leafRadius, x + leafRadius, y + treeHeight, z + leafRadius);
		if(intersects)
		{
			//app.DebugPrintf("Skipping reeds feature generation as it overlaps a game rule structure\n");
			return false;
		}
	}

    // make sure there is enough space
    for (int yy = y; yy <= y + 1 + treeHeight && free; yy++)
	{
        int r = 1;
        if ((yy - y) < trunkHeight)
		{
            r = 0;
        }
		else
		{
            r = leafRadius;
        }
        for (int xx = x - r; xx <= x + r && free; xx++)
		{
            for (int zz = z - r; zz <= z + r && free; zz++)
			{
                if (yy >= 0 && yy < Level::maxBuildHeight)
				{
                    int tt = level->getTile(xx, yy, zz);
                    if (tt != 0 && tt != Tile::leaves_Id) free = false;
                }
				else
				{
                    free = false;
                }
            }
        }
    }

    if (!free) return false;

    // must stand on ground
    int belowTile = level->getTile(x, y - 1, z);
    if ((belowTile != Tile::grass_Id && belowTile != Tile::dirt_Id) || y >= Level::maxBuildHeight - treeHeight - 1) return false;

    placeBlock(level, x, y - 1, z, Tile::dirt_Id);

    // place leaf top
    int currentRadius = random->nextInt(2);
    int maxRadius = 1;
    int minRadius = 0;
    for (int heightPos = 0; heightPos <= topHeight; heightPos++)
	{

        const int yy = y + treeHeight - heightPos;

        for (int xx = x - currentRadius; xx <= x + currentRadius; xx++)
		{
            int xo = xx - (x);
            for (int zz = z - currentRadius; zz <= z + currentRadius; zz++)
			{
                int zo = zz - (z);
                if (abs(xo) == currentRadius && abs(zo) == currentRadius && currentRadius > 0) continue;
                if (!Tile::solid[level->getTile(xx, yy, zz)]) placeBlock(level, xx, yy, zz, Tile::leaves_Id, LeafTile::EVERGREEN_LEAF);
            }
        }

        if (currentRadius >= maxRadius)
		{
            currentRadius = minRadius;
            minRadius = 1;
            maxRadius += 1;
            if (maxRadius > leafRadius)
			{
                maxRadius = leafRadius;
            }
        }
		else
		{
            currentRadius = currentRadius + 1;
        }
    }
    int topOffset = random->nextInt(3);
    for (int hh = 0; hh < treeHeight - topOffset; hh++)
	{
        int t = level->getTile(x, y + hh, z);
        if (t == 0 || t == Tile::leaves_Id) placeBlock(level, x, y + hh, z, Tile::treeTrunk_Id, TreeTile::DARK_TRUNK);
    }
    return true;
}


MegaTreeFeature::MegaTreeFeature(bool doUpdate, int baseHeight, int trunkType, int leafType) : Feature(doUpdate), baseHeight(baseHeight), trunkType(trunkType), leafType(leafType)
{
}

bool MegaTreeFeature::place(Level *level, Random *random, int x, int y, int z)
{
	int treeHeight = random->nextInt(3) + baseHeight;

	bool free = true;
	if (y < 1 || y + treeHeight + 1 > Level::maxBuildHeight) return false;

	// 4J Stu Added to stop tree features generating areas previously place by game rule generation
	if(app.getLevelGenerationOptions() != nullptr)
	{
		PIXBeginNamedEvent(0, "MegaTreeFeature Checking intersects");
		LevelGenerationOptions *levelGenOptions = app.getLevelGenerationOptions();
		bool intersects = levelGenOptions->checkIntersects(x - 2, y - 1, z - 2, x + 2, y + treeHeight, z + 2);
		PIXEndNamedEvent();
		if(intersects)
		{
			//app.DebugPrintf("Skipping reeds feature generation as it overlaps a game rule structure\n");
			return false;
		}
	}

	for (int yy = y; yy <= y + 1 + treeHeight; yy++)
	{
		int r = 2;
		if (yy == y) r = 1;
		if (yy >= y + 1 + treeHeight - 2) r = 2;
		for (int xx = x - r; xx <= x + r && free; xx++)
		{
			for (int zz = z - r; zz <= z + r && free; zz++)
			{
				if (yy >= 0 && yy < Level::maxBuildHeight)
				{
					int tt = level->getTile(xx, yy, zz);
					if (tt != 0 && tt != Tile::leaves_Id && tt != Tile::grass_Id && tt != Tile::dirt_Id && tt != Tile::treeTrunk_Id && tt != Tile::sapling_Id) free = false;
				}
				else
				{
					free = false;
				}
			}
		}
	}

	if (!free) return false;

	int belowTile = level->getTile(x, y - 1, z);
	if ((belowTile != Tile::grass_Id && belowTile != Tile::dirt_Id) || y >= Level::maxBuildHeight - treeHeight - 1) return false;

	level->setTileAndData(x, y - 1, z, Tile::dirt_Id, 0, Tile::UPDATE_CLIENTS);
	level->setTileAndData(x + 1, y - 1, z, Tile::dirt_Id, 0, Tile::UPDATE_CLIENTS);
	level->setTileAndData(x, y - 1, z + 1, Tile::dirt_Id, 0, Tile::UPDATE_CLIENTS);
	level->setTileAndData(x + 1, y - 1, z + 1, Tile::dirt_Id, 0, Tile::UPDATE_CLIENTS);

	PIXBeginNamedEvent(0,"MegaTree placing leaves, %d, %d, %d", x, z, y+treeHeight);
	placeLeaves(level, x, z, y + treeHeight, 2, random);
	PIXEndNamedEvent();

	PIXBeginNamedEvent(0,"MegaTree placing branches");
	int branchHeight = y + treeHeight - 2 - random->nextInt(4);
	while (branchHeight > y + treeHeight / 2)
	{
		float angle = random->nextFloat() * PI * 2.0f;
		int bx = x + static_cast<int>(0.5f + Mth::cos(angle) * 4.0f);
		int bz = z + static_cast<int>(0.5f + Mth::sin(angle) * 4.0f);
		placeLeaves(level, bx, bz, branchHeight, 0, random);

		for (int b = 0; b < 5; b++)
		{
			bx = x + static_cast<int>(1.5f + Mth::cos(angle) * b);
			bz = z + static_cast<int>(1.5f + Mth::sin(angle) * b);
			placeBlock(level, bx, branchHeight - 3 + b / 2, bz, Tile::treeTrunk_Id, trunkType);
		}

		branchHeight -= 2 + random->nextInt(4);
	}
	PIXEndNamedEvent();

	PIXBeginNamedEvent(0, "MegaTree placing vines");
	for (int hh = 0; hh < treeHeight; hh++)
	{
		int t = level->getTile(x, y + hh, z);
		if (t == 0 || t == Tile::leaves_Id)
		{
			placeBlock(level, x, y + hh, z, Tile::treeTrunk_Id, trunkType);
			if (hh > 0)
			{
				if (random->nextInt(3) > 0 && level->isEmptyTile(x - 1, y + hh, z))
				{
					placeBlock(level, x - 1, y + hh, z, Tile::vine_Id, VineTile::VINE_EAST);
				}
				if (random->nextInt(3) > 0 && level->isEmptyTile(x, y + hh, z - 1))
				{
					placeBlock(level, x, y + hh, z - 1, Tile::vine_Id, VineTile::VINE_SOUTH);
				}
			}
		}
		if (hh < (treeHeight - 1))
		{
			t = level->getTile(x + 1, y + hh, z);
			if (t == 0 || t == Tile::leaves_Id)
			{
				placeBlock(level, x + 1, y + hh, z, Tile::treeTrunk_Id, trunkType);
				if (hh > 0)
				{
					if (random->nextInt(3) > 0 && level->isEmptyTile(x + 2, y + hh, z))
					{
						placeBlock(level, x + 2, y + hh, z, Tile::vine_Id, VineTile::VINE_WEST);
					}
					if (random->nextInt(3) > 0 && level->isEmptyTile(x + 1, y + hh, z - 1))
					{
						placeBlock(level, x + 1, y + hh, z - 1, Tile::vine_Id, VineTile::VINE_SOUTH);
					}
				}
			}
			t = level->getTile(x + 1, y + hh, z + 1);
			if (t == 0 || t == Tile::leaves_Id)
			{
				placeBlock(level, x + 1, y + hh, z + 1, Tile::treeTrunk_Id, trunkType);
				if (hh > 0)
				{
					if (random->nextInt(3) > 0 && level->isEmptyTile(x + 2, y + hh, z + 1))
					{
						placeBlock(level, x + 2, y + hh, z + 1, Tile::vine_Id, VineTile::VINE_WEST);
					}
					if (random->nextInt(3) > 0 && level->isEmptyTile(x + 1, y + hh, z + 2))
					{
						placeBlock(level, x + 1, y + hh, z + 2, Tile::vine_Id, VineTile::VINE_NORTH);
					}
				}
			}
			t = level->getTile(x, y + hh, z + 1);
			if (t == 0 || t == Tile::leaves_Id)
			{
				placeBlock(level, x, y + hh, z + 1, Tile::treeTrunk_Id, trunkType);
				if (hh > 0)
				{
					if (random->nextInt(3) > 0 && level->isEmptyTile(x - 1, y + hh, z + 1))
					{
						placeBlock(level, x - 1, y + hh, z + 1, Tile::vine_Id, VineTile::VINE_EAST);
					}
					if (random->nextInt(3) > 0 && level->isEmptyTile(x, y + hh, z + 2))
					{
						placeBlock(level, x, y + hh, z + 2, Tile::vine_Id, VineTile::VINE_NORTH);
					}
				}
			}
		}
	}
	PIXEndNamedEvent();

	return true;
}

void MegaTreeFeature::placeLeaves(Level *level, int x, int z, int topPosition, int baseRadius, Random *random)
{
	int grassHeight = 2;
	// 4J Stu - Generate from top down so that we don't have to keep adjusting the heightmaps
	for (int yy = topPosition; yy >= topPosition - grassHeight; yy--)
	{
		int yo = yy - (topPosition);
		int radius = baseRadius + 1 - yo;
		for (int xx = x - radius; xx <= x + radius + 1; xx++)
		{
			int xo = xx - (x);
			for (int zz = z - radius; zz <= z + radius + 1; zz++)
			{
				int zo = zz - (z);
				if ((xo < 0 && zo < 0) && (xo * xo + zo * zo) > (radius * radius))
				{
					continue;
				}
				if ((xo > 0 || zo > 0) && (xo * xo + zo * zo) > ((radius + 1) * (radius + 1)))
				{
					continue;
				}
				if (random->nextInt(4) == 0 && (xo * xo + zo * zo) > ((radius - 1) * (radius - 1)))
				{
					continue;
				}
				PIXBeginNamedEvent(0,"Getting tile");
				int t = level->getTile(xx, yy, zz);
				PIXEndNamedEvent();
				if (t == 0 || t == Tile::leaves_Id)
				{
					PIXBeginNamedEvent(0,"Placing block");
					placeBlock(level, xx, yy, zz, Tile::leaves_Id, leafType);
					PIXEndNamedEvent();
				}
			}
		}
	}
}

bool SwampTreeFeature::place(Level *level, Random *random, int x, int y, int z)
{
	int treeHeight = random->nextInt(4) + 5;
	while (level->getMaterial(x, y - 1, z) == Material::water)
		y--;

	bool free = true;
	if (y < 1 || y + treeHeight + 1 > Level::genDepth) return false;

	// 4J Stu Added to stop tree features generating areas previously place by game rule generation
	if(app.getLevelGenerationOptions() != nullptr)
	{
		LevelGenerationOptions *levelGenOptions = app.getLevelGenerationOptions();
		bool intersects = levelGenOptions->checkIntersects(x - 3, y - 1, z - 3, x + 3, y + treeHeight, z + 3);
		if(intersects)
		{
			//app.DebugPrintf("Skipping reeds feature generation as it overlaps a game rule structure\n");
			return false;
		}
	}

	for (int yy = y; yy <= y + 1 + treeHeight; yy++) 
	{
		int r = 1;
		if (yy == y) r = 0;
		if (yy >= y + 1 + treeHeight - 2) r = 3;
		for (int xx = x - r; xx <= x + r && free; xx++)
		{
			for (int zz = z - r; zz <= z + r && free; zz++)
			{
				if (yy >= 0 && yy < Level::genDepth)
				{
					int tt = level->getTile(xx, yy, zz);
					if (tt != 0 && tt != Tile::leaves_Id)
					{
						if (tt == Tile::calmWater_Id || tt == Tile::water_Id)
						{
							if (yy > y) free = false;
						}
						else
						{
							free = false;
						}
					}
				}
				else
				{
					free = false;
				}
			}
		}
	}

	if (!free) return false;

	int belowTile = level->getTile(x, y - 1, z);
	if ((belowTile != Tile::grass_Id && belowTile != Tile::dirt_Id) || y >= Level::genDepth - treeHeight - 1) return false;

	placeBlock(level, x, y - 1, z, Tile::dirt_Id);

	for (int yy = y - 3 + treeHeight; yy <= y + treeHeight; yy++)
	{
		int yo = yy - (y + treeHeight);
		int offs = 2 - yo / 2;
		for (int xx = x - offs; xx <= x + offs; xx++)
		{
			int xo = xx - (x);
			for (int zz = z - offs; zz <= z + offs; zz++)
			{
				int zo = zz - (z);
				if (abs(xo) == offs && abs(zo) == offs && (random->nextInt(2) == 0 || yo == 0)) continue;
				if (!Tile::solid[level->getTile(xx, yy, zz)]) placeBlock(level, xx, yy, zz, Tile::leaves_Id);
			}
		}
	}

	for (int hh = 0; hh < treeHeight; hh++)
	{
		int t = level->getTile(x, y + hh, z);
		if (t == 0 || t == Tile::leaves_Id || t == Tile::water_Id || t == Tile::calmWater_Id) placeBlock(level, x, y + hh, z, Tile::treeTrunk_Id);
	}

	for (int yy = y - 3 + treeHeight; yy <= y + treeHeight; yy++)
	{
		int yo = yy - (y + treeHeight);
		int offs = 2 - yo / 2;
		for (int xx = x - offs; xx <= x + offs; xx++)
		{
			for (int zz = z - offs; zz <= z + offs; zz++)
			{
				if (level->getTile(xx, yy, zz) == Tile::leaves_Id)
				{
					if (random->nextInt(4) == 0 && level->getTile(xx - 1, yy, zz) == 0)
					{
						addVine(level, xx - 1, yy, zz, VineTile::VINE_EAST);
					}
					if (random->nextInt(4) == 0 && level->getTile(xx + 1, yy, zz) == 0)
					{
						addVine(level, xx + 1, yy, zz, VineTile::VINE_WEST);
					}
					if (random->nextInt(4) == 0 && level->getTile(xx, yy, zz - 1) == 0)
					{
						addVine(level, xx, yy, zz - 1, VineTile::VINE_SOUTH);
					}
					if (random->nextInt(4) == 0 && level->getTile(xx, yy, zz + 1) == 0)
					{
						addVine(level, xx, yy, zz + 1, VineTile::VINE_NORTH);
					}
				}
			}
		}
	}
	return true;

}

void SwampTreeFeature::addVine(Level *level, int xx, int yy, int zz, int dir)
{
    placeBlock(level, xx, yy, zz, Tile::vine_Id, dir);
    int maxDir = 4;
    while (level->getTile(xx, --yy, zz) == 0 && maxDir > 0)
	{
        placeBlock(level, xx, yy, zz, Tile::vine_Id, dir);
        maxDir--;
    }
}

GroundBushFeature::GroundBushFeature(int trunkType, int leafType)
{
	trunkTileType = trunkType;
	leafTileType = leafType;
}

bool GroundBushFeature::place(Level *level, Random *random, int x, int y, int z)
{
	PIXBeginNamedEvent(0,"Placing GroundBushFeature");
	int t = 0;
	while (((t = level->getTile(x, y, z)) == 0 || t == Tile::leaves_Id) && y > 0)
		y--;

	int tile = level->getTile(x, y, z);
	if (tile == Tile::dirt_Id || tile == Tile::grass_Id)
	{
		y++;
		placeBlock(level, x, y, z, Tile::treeTrunk_Id, trunkTileType);

		for (int yy = y; yy <= y + 2; yy++)
		{
			int yo = yy - y;
			int offs = 2 - yo;
			for (int xx = x - offs; xx <= x + offs; xx++)
			{
				int xo = xx - (x);
				for (int zz = z - offs; zz <= z + offs; zz++)
				{
					int zo = zz - (z);
					if (abs(xo) == offs && abs(zo) == offs && random->nextInt(2) == 0) continue;
					if (!Tile::solid[level->getTile(xx, yy, zz)]) placeBlock(level, xx, yy, zz, Tile::leaves_Id, leafTileType);
				}
			}
		}
	}
	PIXEndNamedEvent();
	return true;
}

byte BasicTree::axisConversionArray[] = { 2, 0, 0, 1, 2, 1 };

BasicTree::~BasicTree()
{
	delete rnd;

	for( int i = 0; i < foliageCoordsLength; i++ )
	{
		delete [] foliageCoords[i];
	}
	delete [] foliageCoords;
}

BasicTree::BasicTree(bool doUpdate) : Feature(doUpdate)
{
	rnd = new Random();
    origin[0] = 0;
	origin[1] = 0;
	origin[2] = 0;
    // Field to hold the tree height.
    height = 0;
    // Other important tree information.
    trunkHeight = 0;
    trunkHeightScale = 0.618;
    branchDensity = 1.0;
    branchSlope = 0.381;
    widthScale = 1.0;
    foliageDensity = 1.0;
    trunkWidth = 1;
    heightVariance = 12;
    foliageHeight = 4;
	foliageCoords = nullptr;
	foliageCoordsLength = 0;
}

void BasicTree::prepare()
{
    // Initialize the instance variables.
    // Populate the list of foliage cluster locations.
    // Designed to be overridden in child classes to change basic
    // tree properties (trunk width, branch angle, foliage density, etc..).
    trunkHeight = static_cast<int>(height * trunkHeightScale);
    if (trunkHeight >= height) trunkHeight = height - 1;
    int clustersPerY = static_cast<int>(1.382 + pow(foliageDensity * height / 13.0, 2));
    if (clustersPerY < 1) clustersPerY = 1;
    // The foliage coordinates are a list of [x,y,z,y of branch base] values for each cluster
	int **tempFoliageCoords = new int *[clustersPerY * height];
	for( int i = 0; i < clustersPerY * height; i++ )
	{
		tempFoliageCoords[i] = new int[4];
	}
    int y = origin[1] + height - foliageHeight;
    int clusterCount = 1;
    int trunkTop = origin[1] + trunkHeight;
    int relativeY = y - origin[1];

    tempFoliageCoords[0][0] = origin[0];
    tempFoliageCoords[0][1] = y;
    tempFoliageCoords[0][2] = origin[2];
    tempFoliageCoords[0][3] = trunkTop;
    y--;

    while (relativeY >= 0)
	{
        int num = 0;

        float shapefac = treeShape(relativeY);
        if (shapefac < 0)
		{
            y--;
            relativeY--;
            continue;
        }

        // The originOffset is to put the value in the middle of the block.
        double originOffset = 0.5;
        while (num < clustersPerY)
		{
            double radius = widthScale * (shapefac * (rnd->nextFloat() + 0.328));
            double angle = rnd->nextFloat() * 2.0 * 3.14159;
            int x = Mth::floor(radius * sin(angle) + origin[0] + originOffset);
            int z = Mth::floor(radius * cos(angle) + origin[2] + originOffset);
            int checkStart[] = { x, y, z };
            int checkEnd[] = { x, y + foliageHeight, z };
            // check the center column of the cluster for obstructions.
            if (checkLine(checkStart, checkEnd) == -1) {
                // If the cluster can be created, check the branch path
                // for obstructions.
                int checkBranchBase[] = { origin[0], origin[1], origin[2] };
                double distance = sqrt(pow(abs(origin[0] - checkStart[0]), 2.0) + pow(abs(origin[2] - checkStart[2]), 2.0));
                double branchHeight = distance * branchSlope;
                if ((checkStart[1] - branchHeight) > trunkTop)
				{
                    checkBranchBase[1] = trunkTop;
                }
				else
				{
                    checkBranchBase[1] = static_cast<int>(checkStart[1] - branchHeight);
                }
                // Now check the branch path
                if (checkLine(checkBranchBase, checkStart) == -1)
				{
                    // If the branch path is clear, add the position to the list
                    // of foliage positions
                    tempFoliageCoords[clusterCount][0] = x;
                    tempFoliageCoords[clusterCount][1] = y;
                    tempFoliageCoords[clusterCount][2] = z;
                    tempFoliageCoords[clusterCount][3] = checkBranchBase[1];
                    clusterCount++;
                }
            }
            num++;
        }
        y--;
        relativeY--;
    }
	// 4J Stu - Rather than copying the array, we are storing the number of valid elements in the array
	foliageCoordsLength = clusterCount;
	foliageCoords = tempFoliageCoords;
	// Delete the rest of the array whilst we still know how big it was
	for( int i = clusterCount; i < clustersPerY * height; i++ )
	{
		delete [] tempFoliageCoords[i];
		tempFoliageCoords[i] = nullptr;
	}
	// 4J - original code for above is the following, it isn't obvious to me why it is doing a copy of the array, so let's not for now
//    foliageCoords = new int[clusterCount][4];
//    System.arraycopy(tempFoliageCoords, 0, foliageCoords, 0, clusterCount);

}

void BasicTree::crossection(int x, int y, int z, float radius, byte direction, int material)
{
	PIXBeginNamedEvent(0, "BasicTree crossection");
    // Create a circular cross section.
    //
    // Used to nearly everything in the foliage, branches, and trunk.
    // This is a good target for performance optimization.

    // Passed values:
    // x,y,z is the center location of the cross section
    // radius is the radius of the section from the center
    // direction is the direction the cross section is pointed, 0 for x, 1 for y, 2 for z
    // material is the index number for the material to use
    int rad = static_cast<int>(radius + 0.618);
    byte secidx1 = axisConversionArray[direction];
    byte secidx2 = axisConversionArray[direction + 3];
    int center[] = { x, y, z };
    int position[] = { 0, 0, 0 };
    int offset1 = -rad;
    int offset2 = -rad;
    int thismat;
    position[direction] = center[direction];
    while (offset1 <= rad)
	{
        position[secidx1] = center[secidx1] + offset1;
        offset2 = -rad;
        while (offset2 <= rad)
		{
            double thisdistance = pow(abs(offset1) + 0.5, 2) + pow(abs(offset2) + 0.5, 2);
            if (thisdistance > radius * radius)
			{
                offset2++;
                continue;
            }
            position[secidx2] = center[secidx2] + offset2;
			PIXBeginNamedEvent(0,"BasicTree getting tile");
            thismat = thisLevel->getTile(position[0], position[1], position[2]);
			PIXEndNamedEvent();
            if (!((thismat == 0) || (thismat == Tile::leaves_Id)))
			{
                // If the material of the checked block is anything other than
                // air or foliage, skip this tile.
                offset2++;
                continue;
            }
			PIXBeginNamedEvent(0,"BasicTree placing block");
            placeBlock(thisLevel, position[0], position[1], position[2], material, 0);
			PIXEndNamedEvent();
            offset2++;
        }
        offset1++;
    }
	PIXEndNamedEvent();
}

float BasicTree::treeShape(int y)
{
    // Take the y position relative to the base of the tree.
    // Return the distance the foliage should be from the trunk axis.
    // Return a negative number if foliage should not be created at this height.
    // This method is intended for overriding in child classes, allowing
    // different shaped trees.
    // This method should return a consistent value for each y (don't randomize).
    if (y < (static_cast<float>(height) * 0.3)) return (float) -1.618;
    float radius = static_cast<float>(height) / static_cast<float>(2.0);
    float adjacent = (static_cast<float>(height) / static_cast<float>(2.0)) - y;
    float distance;
    if (adjacent == 0) distance = radius;
    else if (abs(adjacent) >= radius) distance = static_cast<float>(0.0);
    else distance = static_cast<float>(sqrt(pow(abs(radius), 2) - pow(abs(adjacent), 2)));
    // Alter this factor to change the overall width of the tree.
    distance *= static_cast<float>(0.5);
    return distance;
}

float BasicTree::foliageShape(int y)
{
    // Take the y position relative to the base of the foliage cluster.
    // Return the radius of the cluster at this y
    // Return a negative number if no foliage should be created at this level
    // this method is intended for overriding in child classes, allowing
    // foliage of different sizes and shapes.
    if ((y < 0) || (y >= foliageHeight)) return static_cast<float>(-1);
    else if ((y == 0) || (y == (foliageHeight - 1))) return static_cast<float>(2);
    else return static_cast<float>(3);
}

void BasicTree::foliageCluster(int x, int y, int z)
{
	PIXBeginNamedEvent(0,"BasicTree foliageCluster");
    // Generate a cluster of foliage, with the base at x, y, z.
    // The shape of the cluster is derived from  foliageShape
    // crossection is called to make each level.
    int topy = y + foliageHeight;
    int cury = topy - 1;
    float radius;
	// 4J Stu - Generate foliage from the top down so that we don't keep recalculating heightmaps
    while (cury >= y)
	{
        radius = foliageShape(cury - y);
        crossection(x, cury, z, radius, (byte) 1, Tile::leaves_Id);
        cury--;
    }
	PIXEndNamedEvent();
}

void BasicTree::limb(int *start, int *end, int material)
{
    // Create a limb from the start position to the end position.
    // Used for creating the branches and trunk.

    // Populate delta, the difference between start and end for all three axies.
    // Set primidx to the index with the largest overall distance traveled.
    int delta[] = { 0, 0, 0 };
    byte idx = 0;
    byte primidx = 0;
    while (idx < 3)
	{
        delta[idx] = end[idx] - start[idx];
        if (abs(delta[idx]) > abs(delta[primidx]))
		{
            primidx = idx;
        }
        idx++;
    }
    // If the largest distance is zero, don't bother to do anything else.
    if (delta[primidx] == 0) return;
    // set up the other two axis indices.
    byte secidx1 = axisConversionArray[primidx];
    byte secidx2 = axisConversionArray[primidx + 3];
    // primsign is digit 1 or -1 depending on whether the limb is headed
    // along the positive or negative primidx axis.
    char primsign;
    if (delta[primidx] > 0) primsign = 1;
    else primsign = -1;
    // Initilize the per-step movement for the non-primary axies.
    double secfac1 = static_cast<double>(delta[secidx1]) / static_cast<double>(delta[primidx]);
    double secfac2 = static_cast<double>(delta[secidx2]) / static_cast<double>(delta[primidx]);
    // Initialize the coordinates.
    int coordinate[] = { 0, 0, 0 };
    // Loop through each crossection along the primary axis, from start to end
    int primoffset = 0;
    int endoffset = delta[primidx] + primsign;
    while (primoffset != endoffset)
	{
        coordinate[primidx] = Mth::floor(start[primidx] + primoffset + 0.5);
        coordinate[secidx1] = Mth::floor(start[secidx1] + (primoffset * secfac1) + 0.5);
        coordinate[secidx2] = Mth::floor(start[secidx2] + (primoffset * secfac2) + 0.5);

		int dir = TreeTile::FACING_Y;
		int xdiff = abs(coordinate[0] - start[0]);
		int zdiff = abs(coordinate[2] - start[2]);
		int maxdiff = max(xdiff, zdiff);

		if (maxdiff > 0)
		{
			if (xdiff == maxdiff)
			{
				dir = TreeTile::FACING_X;
			}
			else if (zdiff == maxdiff)
			{
				dir = TreeTile::FACING_Z;
			}
		}
        placeBlock(thisLevel, coordinate[0], coordinate[1], coordinate[2], material, dir);
        primoffset += primsign;
    }
}

void BasicTree::makeFoliage()
{
    // Create the tree foliage.
    // Call foliageCluster at the correct locations
    int idx = 0;
    int finish = foliageCoordsLength;
    while (idx < finish)
	{
        int x = foliageCoords[idx][0];
        int y = foliageCoords[idx][1];
        int z = foliageCoords[idx][2];
        foliageCluster(x, y, z);
        idx++;
    }
}

bool BasicTree::trimBranches(int localY)
{
    // For larger trees, randomly "prune" the branches so there
    // aren't too many.
    // Return true if the branch should be created.
    // This method is intended for overriding in child classes, allowing
    // decent amounts of branches on very large trees.
    // Can also be used to disable branches on some tree types, or
    // make branches more sparse.
    if (localY < (height * 0.2)) return false;
    else return true;
}

void BasicTree::makeTrunk()
{
    // Create the trunk of the tree.
    int x = origin[0];
    int startY = origin[1];
    int topY = origin[1] + trunkHeight;
    int z = origin[2];
    int startCoord[] = { x, startY, z };
    int endCoord[] = { x, topY, z };
    limb(startCoord, endCoord, Tile::treeTrunk_Id);
    if (trunkWidth == 2)
	{
        startCoord[0] += 1;
        endCoord[0] += 1;
        limb(startCoord, endCoord, Tile::treeTrunk_Id);
        startCoord[2] += 1;
        endCoord[2] += 1;
        limb(startCoord, endCoord, Tile::treeTrunk_Id);
        startCoord[0] += -1;
        endCoord[0] += -1;
        limb(startCoord, endCoord, Tile::treeTrunk_Id);
    }
}

void BasicTree::makeBranches()
{
    // Create the tree branches.
    // Call trimBranches for each branch to see if you should create it.
    // Call taperedLimb to the correct locations
    int idx = 0;
    int finish = foliageCoordsLength;
    int baseCoord[] = { origin[0], origin[1], origin[2] };
    while (idx < finish)
	{
        int *coordValues = foliageCoords[idx];
        int endCoord[] = { coordValues[0], coordValues[1], coordValues[2] };
        baseCoord[1] = coordValues[3];
        int localY = baseCoord[1] - origin[1];
        if (trimBranches(localY))
		{
            limb(baseCoord, endCoord, Tile::treeTrunk_Id);
        }
        idx++;
    }
}

int BasicTree::checkLine(int *start, int *end)
{
    // Check from coordinates start to end (both inclusive) for blocks other than air and foliage
    // If a block other than air and foliage is found, return the number of steps taken.
    // If no block other than air and foliage is found, return -1.
    // Examples:
    // If the third block searched is stone, return 2
    // If the first block searched is lava, return 0

    int delta[] = { 0, 0, 0 };
    byte idx = 0;
    byte primidx = 0;
    while (idx < 3)
	{
        delta[idx] = end[idx] - start[idx];
        if (abs(delta[idx]) > abs(delta[primidx]))
		{
            primidx = idx;
        }
        idx++;
    }
    // If the largest distance is zero, don't bother to do anything else.
    if (delta[primidx] == 0) return -1;
    // set up the other two axis indices.
    byte secidx1 = axisConversionArray[primidx];
    byte secidx2 = axisConversionArray[primidx + 3];
    // primsign is digit 1 or -1 depending on whether the limb is headed
    // along the positive or negative primidx axis.
    char primsign; // 4J Stu - Was byte, but we use in a sum below and byte=unsigned char so we were setting endoffset incorrectly
    if (delta[primidx] > 0) primsign = 1;
    else primsign = -1;
    // Initilize the per-step movement for the non-primary axies.
    double secfac1 = static_cast<double>(delta[secidx1]) / static_cast<double>(delta[primidx]);
    double secfac2 = static_cast<double>(delta[secidx2]) / static_cast<double>(delta[primidx]);
    // Initialize the coordinates.
    int coordinate[] = { 0, 0, 0 };
    // Loop through each crossection along the primary axis, from start to end
    int primoffset = 0;
    int endoffset = delta[primidx] + primsign;
    int thismat;
    while (primoffset != endoffset)
	{
        coordinate[primidx] = start[primidx] + primoffset;
        coordinate[secidx1] = Mth::floor(start[secidx1] + (primoffset * secfac1));
        coordinate[secidx2] = Mth::floor(start[secidx2] + (primoffset * secfac2));
        thismat = thisLevel->getTile(coordinate[0], coordinate[1], coordinate[2]);
        if (!((thismat == 0) || (thismat == Tile::leaves_Id)))
		{
            // If the material of the checked block is anything other than
            // air or foliage, stop looking.
            break;
        }
        primoffset += primsign;
    }
    // If you reached the end without finding anything, return -1.
    if (primoffset == endoffset)
	{
        return -1;
    }
    // Otherwise, return the number of steps you took.
    else
	{
        return abs(primoffset);
    }
}

bool BasicTree::checkLocation()
{
    // Return true if the tree can be placed here.
    // Return false if the tree can not be placed here.

    // Examine the square under the trunk.  Is it grass or dirt?
    // If not, return false
    // Examine center column for how tall the tree can be.
    // If the checked height is shorter than height, but taller
    // than 4, set the tree to the maximum height allowed.
    // If the space is too short, return false.
    int startPosition[] = { origin[0], origin[1], origin[2] };
    int endPosition[] = { origin[0], origin[1] + height - 1, origin[2] };

	// 4J Stu Added to stop tree features generating areas previously place by game rule generation
	if(app.getLevelGenerationOptions() != nullptr)
	{
		LevelGenerationOptions *levelGenOptions = app.getLevelGenerationOptions();
		bool intersects = levelGenOptions->checkIntersects(startPosition[0], startPosition[1], startPosition[2], endPosition[0], endPosition[1], endPosition[2]);
		if(intersects)
		{
			//app.DebugPrintf("Skipping reeds feature generation as it overlaps a game rule structure\n");
			return false;
		}
	}

    // Check the location it is resting on
    int baseMaterial = thisLevel->getTile(origin[0], origin[1] - 1, origin[2]);
    if (!((baseMaterial == 2) || (baseMaterial == 3)))
	{
        return false;
    }
    int allowedHeight = checkLine(startPosition, endPosition);
    // If the set height is good, go with that
    if (allowedHeight == -1)
	{
        return true;
    }
    // If the space is too short, tell the build to abort
    else if (allowedHeight < 6)
	{
        return false;
    }
    // If the space is shorter than the set height, but not too short
    // shorten the height, and tell the build to continue
    else
	{
        height = allowedHeight;
        //System.out.println("Shortened the tree");
        return true;
    }
}

void BasicTree::init(double heightInit, double widthInit, double foliageDensityInit)
{
    // all of the parameters should be from 0.0 to 1.0
    // heightInit scales the maximum overall height of the tree (still randomizes height within the possible range)
    // widthInit scales the maximum overall width of the tree (keep this above 0.3 or so)
    // foliageDensityInit scales how many foliage clusters are created.
    //
    // Note, you can call "place" without calling "init".
    // This is the same as calling init(1.0,1.0,1.0) and then calling place.
    heightVariance = static_cast<int>(heightInit * 12);
    if (heightInit > 0.5) foliageHeight = 5;
    widthScale = widthInit;
    foliageDensity = foliageDensityInit;
}

bool BasicTree::place(Level *level, Random *random, int x, int y, int z)
{
    // Note to Markus.
    // currently the following fields are set randomly.  If you like, make them
    // parameters passed into "place".
    //
    // height: so the map generator can intelligently set the height of the tree,
    // and make forests with large trees in the middle and smaller ones on the edges.

    // Initialize the instance fields for the level and the seed.
    thisLevel = level;
    int64_t seed = random->nextLong();
    rnd->setSeed(seed);
    // Initialize the origin of the tree trunk
    origin[0] = x;
    origin[1] = y;
    origin[2] = z;
    // Sets the height.  Take out this line if height is passed as a parameter
    if (height == 0)
	{
        height = 5 + rnd->nextInt(heightVariance);
    }
    if (!(checkLocation()))
	{
        //System.out.println("Tree location failed");
        return false;
    }
	PIXBeginNamedEvent(0, "Placing BasicTree");
    //System.out.println("The height is");
    //System.out.println(height);
    //System.out.println("Trunk Height check done");
	PIXBeginNamedEvent(0, "Preparing tree");
    prepare();
	PIXEndNamedEvent();
    //System.out.println("Prepare done");
	PIXBeginNamedEvent(0, "Making foliage");
    makeFoliage();
	PIXEndNamedEvent();
    //System.out.println("Foliage done");
	PIXBeginNamedEvent(0, "Making trunk");
    makeTrunk();
	PIXEndNamedEvent();
    //System.out.println("Trunk done");
	PIXBeginNamedEvent(0, "Making branches");
    makeBranches();
	PIXEndNamedEvent();
    //System.out.println("Branches done");
	PIXEndNamedEvent();
    return true;
}

FlowerFeature::FlowerFeature(int tile)
{
	this->tile = tile;
}

bool FlowerFeature::place(Level *level, Random *random, int x, int y, int z)
{
	// 4J Stu Added to stop tree features generating areas previously place by game rule generation
	if(app.getLevelGenerationOptions() != nullptr)
	{
		LevelGenerationOptions *levelGenOptions = app.getLevelGenerationOptions();
		bool intersects = levelGenOptions->checkIntersects(x - 8, y - 4, z - 8, x + 8, y + 4, z + 8);
		if(intersects)
		{
			//app.DebugPrintf("Skipping reeds feature generation as it overlaps a game rule structure\n");
			return false;
		}
	}

	for (int i = 0; i < 64; i++)
	{
		int x2 = x + random->nextInt(8) - random->nextInt(8);
		int y2 = y + random->nextInt(4) - random->nextInt(4);
		int z2 = z + random->nextInt(8) - random->nextInt(8);
		if (level->isEmptyTile(x2, y2, z2) && (!level->dimension->hasCeiling || y2 < Level::genDepthMinusOne))
		{
			if (Tile::tiles[tile]->canSurvive(level, x2, y2, z2))
			{
				level->setTileAndData(x2, y2, z2, tile, 0, Tile::UPDATE_CLIENTS);
			}
		}
	}

	return true;
}

TallGrassFeature::TallGrassFeature(int tile, int type)
{
	this->tile = tile;
	this->type = type;
}

bool TallGrassFeature::place(Level *level, Random *random, int x, int y, int z)
{
	int t = 0;
	while (((t = level->getTile(x, y, z)) == 0 || t == Tile::leaves_Id) && y > 0)
		y--;

	for (int i = 0; i < 128; i++)
	{
		int x2 = x + random->nextInt(8) - random->nextInt(8);
		int y2 = y + random->nextInt(4) - random->nextInt(4);
		int z2 = z + random->nextInt(8) - random->nextInt(8);
		if (level->isEmptyTile(x2, y2, z2))
		{
			if (Tile::tiles[tile]->canSurvive(level, x2, y2, z2))
			{
				level->setTileAndData(x2, y2, z2, tile, type, Tile::UPDATE_CLIENTS);
			}
		}
	}

	return true;
}

bool ReedsFeature::place(Level *level, Random *random, int x, int y, int z)
{
	for (int i = 0; i < 20; i++)
	{
		int x2 = x + random->nextInt(4) - random->nextInt(4);
		int y2 = y;
		int z2 = z + random->nextInt(4) - random->nextInt(4);

		// 4J Stu Added to stop reed features generating areas previously place by game rule generation
		if(app.getLevelGenerationOptions() != nullptr)
		{
			LevelGenerationOptions *levelGenOptions = app.getLevelGenerationOptions();
			bool intersects = levelGenOptions->checkIntersects(x2, y2, z2, x2, y2, z2);
			if(intersects)
			{
				//app.DebugPrintf("Skipping reeds feature generation as it overlaps a game rule structure\n");
				continue;
			}
		}
		if (level->isEmptyTile(x2, y2, z2)) 
		{
			if (level->getMaterial(x2-1, y2-1, z2) == Material::water || 
				level->getMaterial(x2+1, y2-1, z2) == Material::water || 
				level->getMaterial(x2, y2-1, z2-1) == Material::water || 
				level->getMaterial(x2, y2-1, z2+1) == Material::water)
			{

				int h = 2 + random->nextInt(random->nextInt(3) + 1);
				for (int yy = 0; yy < h; yy++)
				{
					if ( Tile::reeds->canSurvive(level, x2, y2 + yy, z2) )
					{
						level->setTileAndData(x2, y2 + yy, z2, Tile::reeds_Id, 0, Tile::UPDATE_CLIENTS);
					}
				}
			}
		}
	}

	return true;
}

bool CactusFeature::place(Level *level, Random *random, int x, int y, int z)
{
	for (int i = 0; i < 10; i++) {
		int x2 = x + random->nextInt(8) - random->nextInt(8);
		int y2 = y + random->nextInt(4) - random->nextInt(4);
		int z2 = z + random->nextInt(8) - random->nextInt(8);
		if (level->isEmptyTile(x2, y2, z2))
		{
			int h = 1 + random->nextInt(random->nextInt(3) + 1);
			for (int yy = 0; yy < h; yy++)
			{
				if (Tile::cactus->canSurvive(level, x2, y2+yy, z2))
				{
					level->setTileAndData(x2, y2+yy, z2, Tile::cactus_Id, 0, Tile::UPDATE_CLIENTS);
				}
			}
		}
	}

	return true;
}



const float Mth::sinScale = 65536.0f / (float) (PI * 2);
float *Mth::_sin = nullptr;

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

bool Bush::mayPlaceOn(int tile)
{
	return tile == Tile::grass_Id || tile == Tile::dirt_Id || tile == Tile::farmland_Id;
}

bool Bush::canSurvive(Level *level, int x, int y, int z)
{
	return ( level->getDaytimeRawBrightness(x, y, z) >= 8 || (level->canSeeSky(x, y, z))) && mayPlaceOn(level->getTile(x, y - 1, z));
}

bool ReedTile::mayPlace(Level *level, int x, int y, int z) 
{
	int below = level->getTile(x, y - 1, z);
	if (below == id) return true;
	if (below != Tile::grass_Id && below != Tile::dirt_Id && below != Tile::sand_Id) return false;
	if (level->getMaterial(x - 1, y - 1, z) == Material::water) return true;
	if (level->getMaterial(x + 1, y - 1, z) == Material::water) return true;
	if (level->getMaterial(x, y - 1, z - 1) == Material::water) return true;
	if (level->getMaterial(x, y - 1, z + 1) == Material::water) return true;
	//printf("no water\n");
	return false;
}

bool ReedTile::canSurvive(Level *level, int x, int y, int z)
{
	return mayPlace(level, x, y, z);
}

bool CactusTile::canSurvive(Level *level, int x, int y, int z)
{
	if (level->getMaterial(x - 1, y, z)->isSolid()) return false;
	if (level->getMaterial(x + 1, y, z)->isSolid()) return false;
	if (level->getMaterial(x, y, z - 1)->isSolid()) return false;
	if (level->getMaterial(x, y, z + 1)->isSolid()) return false;
	int below = level->getTile(x, y - 1, z);
	return below == Tile::cactus_Id || below == Tile::sand_Id;
}

Feature *Biome::getTreeFeature(Random *random)
{
	if (random->nextInt(10) == 0)
	{
		return new BasicTree(false); // 4J used to return member fancyTree, now returning newly created object so that caller can be consistently resposible for cleanup
	}
	return new TreeFeature(false); // 4J used to return member normalTree, now returning newly created object so that caller can be consistently resposible for cleanup
}

Feature *ForestBiome::getTreeFeature(Random *random)
{
    if (random->nextInt(5) == 0)
	{
        return new BirchFeature(false); // 4J used to return member birchTree, now returning newly created object so that caller can be consistently resposible for cleanup
    }
    if (random->nextInt(10) == 0)
	{
        return new BasicTree(false); // 4J used to return member fancyTree, now returning newly created object so that caller can be consistently resposible for cleanup
    }
    return new TreeFeature(false); // 4J used to return member normalTree, now returning newly created object so that caller can be consistently resposible for cleanup
}

Feature *TaigaBiome::getTreeFeature(Random *random)
{
	if (random->nextInt(3) == 0)
	{
		return new PineFeature();
	}
	return new SpruceFeature(false);
}

Feature *JungleBiome::getTreeFeature(Random *random)
{
	if (random->nextInt(10) == 0)
	{
		return new BasicTree(false); // 4J used to return member fancyTree, now returning newly created object so that caller can be consistently resposible for cleanup
	}
	if (random->nextInt(2) == 0)
	{
		return new GroundBushFeature(TreeTile::JUNGLE_TRUNK, LeafTile::NORMAL_LEAF);
	}
	if (random->nextInt(3) == 0)
	{
		return new MegaTreeFeature(false, 10 + random->nextInt(20), TreeTile::JUNGLE_TRUNK, LeafTile::JUNGLE_LEAF);
	}
	return new TreeFeature(false, 4 + random->nextInt(7), TreeTile::JUNGLE_TRUNK, LeafTile::JUNGLE_LEAF, true);
}

Feature *SwampBiome::getTreeFeature(Random *random)
{
	return new SwampTreeFeature(); // 4J used to return member swampTree, now returning newly created object so that caller can be consistently resposible for cleanup
}

Feature *RainforestBiome::getTreeFeature(Random *random)
{
    if (random->nextInt(3) == 0)
	{
        return new BasicTree(false);
    }
    return new TreeFeature(false);
}

Feature *Biome::getGrassFeature(Random *random)
{
	return new TallGrassFeature(Tile::tallgrass_Id, TallGrass::TALL_GRASS);
}

Feature *JungleBiome::getGrassFeature(Random *random)
{
	if (random->nextInt(4) == 0)
	{
		return new TallGrassFeature(Tile::tallgrass_Id, TallGrass::FERN);
	}
	return new TallGrassFeature(Tile::tallgrass_Id, TallGrass::TALL_GRASS);
}

} }
