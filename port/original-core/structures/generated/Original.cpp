// Generated from port/upstream; see source-manifest.json. Do not hand edit.
#include "Original.h"
namespace original_structures { namespace detail {
BoundingBox::BoundingBox()
{
	// 4J added initialisers
	x0 = 0;
	y0 = 0;
	z0 = 0;
	x1 = 0;
	y1 = 0;
	z1 = 0;
}



BoundingBox *BoundingBox::getUnknownBox()
{
	return new BoundingBox(INT_MAX, INT_MAX, INT_MAX, INT_MIN, INT_MIN, INT_MIN );
}

BoundingBox *BoundingBox::orientBox(int footX, int footY, int footZ, int offX, int offY, int offZ, int width, int height, int depth, int orientation)
{
	switch (orientation)
	{
	default:
		return new BoundingBox(footX + offX, footY + offY, footZ + offZ, footX + width - 1 + offX, footY + height - 1 + offY, footZ + depth - 1 + offZ);
	case Direction::NORTH:
		// foot is at x0, y0, z1
		return new BoundingBox(footX + offX, footY + offY, footZ - depth + 1 + offZ, footX + width - 1 + offX, footY + height - 1 + offY, footZ + offZ);
	case Direction::SOUTH:
		// foot is at x0, y0, z0
		return new BoundingBox(footX + offX, footY + offY, footZ + offZ, footX + width - 1 + offX, footY + height - 1 + offY, footZ + depth - 1 + offZ);
	case Direction::WEST:
		// foot is at x1, y0, z0, but width and depth are flipped
		return new BoundingBox(footX - depth + 1 + offZ, footY + offY, footZ + offX, footX + offZ, footY + height - 1 + offY, footZ + width - 1 + offX);
	case Direction::EAST:
		// foot is at x0, y0, z0, but width and depth are flipped
		return new BoundingBox(footX + offZ, footY + offY, footZ + offX, footX + depth - 1 + offZ, footY + height - 1 + offY, footZ + width - 1 + offX);
	}
}

BoundingBox::BoundingBox(BoundingBox *other)
{
	x0 = other->x0;
	y0 = other->y0;
	z0 = other->z0;
	x1 = other->x1;
	y1 = other->y1;
	z1 = other->z1;
}

BoundingBox::BoundingBox(int x0, int y0, int z0, int x1, int y1, int z1)
{
	this->x0 = x0;
	this->y0 = y0;
	this->z0 = z0;
	this->x1 = x1;
	this->y1 = y1;
	this->z1 = z1;
}

BoundingBox::BoundingBox(int x0, int z0, int x1, int z1)
{
	this->x0 = x0;
	this->z0 = z0;
	this->x1 = x1;
	this->z1 = z1;

	// the bounding box for this constructor is limited to world size,
	// excluding bedrock level
	y0 = 1;
	y1 = 512;
}

bool BoundingBox::intersects(BoundingBox *other)
{
	return !(this->x1 < other->x0 || this->x0 > other->x1 || this->z1 < other->z0 || this->z0 > other->z1 || this->y1 < other->y0 || this->y0 > other->y1);
}

bool BoundingBox::intersects(int x0, int y0, int z0, int x1, int y1, int z1)
{
	return !(this->x1 < x0 || this->x0 > x1 || this->z1 < z0 || this->z0 > z1 || this->y1 < y0 || this->y0 > y1);
}

bool BoundingBox::intersects(int x0, int z0, int x1, int z1)
{
	return !(this->x1 < x0 || this->x0 > x1 || this->z1 < z0 || this->z0 > z1);
}

void BoundingBox::expand(BoundingBox *other)
{
	x0 = Math::_min(x0, other->x0);
	y0 = Math::_min(y0, other->y0);
	z0 = Math::_min(z0, other->z0);
	x1 = Math::_max(x1, other->x1);
	y1 = Math::_max(y1, other->y1);
	z1 = Math::_max(z1, other->z1);
}

BoundingBox *BoundingBox::getIntersection(BoundingBox *other)
{
	if (!intersects(other))
	{
		return nullptr;
	}
	BoundingBox *result = new BoundingBox();
	result->x0 = Math::_max(x0, other->x0);
	result->y0 = Math::_max(y0, other->y0);
	result->z0 = Math::_max(z0, other->z0);
	result->x1 = Math::_min(x1, other->x1);
	result->y1 = Math::_min(y1, other->y1);
	result->z1 = Math::_min(z1, other->z1);

	return result;
}

void BoundingBox::move(int dx, int dy, int dz)
{
	x0 += dx;
	y0 += dy;
	z0 += dz;
	x1 += dx;
	y1 += dy;
	z1 += dz;
}

bool BoundingBox::isInside(int x, int y, int z)
{
	return (x >= x0 && x <= x1 && z >= z0 && z <= z1 && y >= y0 && y <= y1);
}

int BoundingBox::getXSpan()
{
	return x1 - x0 + 1;
}

int BoundingBox::getYSpan()
{
	return y1 - y0 + 1;
}

int BoundingBox::getZSpan()
{
	return z1 - z0 + 1;
}

int BoundingBox::getXCenter()
{
	return x0 + (x1 - x0 + 1) / 2;
}

int BoundingBox::getYCenter()
{
	return y0 + (y1 - y0 + 1) / 2;
}

int BoundingBox::getZCenter()
{
	return z0 + (z1 - z0 + 1) / 2;
}
StructurePiece::StructurePiece()
{
	boundingBox = nullptr;
	orientation = 0;
	genDepth = 0;
	// for reflection
}

StructurePiece::StructurePiece( int genDepth )
{
	boundingBox = nullptr;
	this->genDepth = genDepth;
	orientation = Direction::UNDEFINED;
}

StructurePiece::~StructurePiece()
{
	if(boundingBox != nullptr) delete boundingBox;
}

void StructurePiece::addChildren( StructurePiece* startPiece, list< StructurePiece* > *pieces, Random* random )
{
}

BoundingBox* StructurePiece::getBoundingBox()
{
	return boundingBox;
}

int StructurePiece::getGenDepth()
{
	return genDepth;
}

StructurePiece* StructurePiece::findCollisionPiece( list< StructurePiece* > *pieces, BoundingBox* box )
{
	for (auto& piece : *pieces)
	{
		if ( piece && piece->getBoundingBox() && piece->getBoundingBox()->intersects( box ) )
		{
			return piece;
		}
	}
	return nullptr;
}

int StructurePiece::getWorldX( int x, int z )
{
	switch ( orientation )
	{
	case Direction::NORTH:
	case Direction::SOUTH:
		return boundingBox->x0 + x;
	case Direction::WEST:
		return boundingBox->x1 - z;
	case Direction::EAST:
		return boundingBox->x0 + z;
	default:
		return x;
	}
}

int StructurePiece::getWorldY( int y )
{
	if ( orientation == Direction::UNDEFINED )
	{
		return y;
	}
	return y + boundingBox->y0;
}

int StructurePiece::getWorldZ( int x, int z )
{
	switch ( orientation )
	{
	case Direction::NORTH:
		return boundingBox->z1 - z;
	case Direction::SOUTH:
		return boundingBox->z0 + z;
	case Direction::WEST:
	case Direction::EAST:
		return boundingBox->z0 + x;
	default:
		return z;
	}
}

int StructurePiece::getOrientationData( int tile, int data )
{
	if ( tile == Tile::rail->id )
	{
		if ( orientation == Direction::WEST || orientation == Direction::EAST )
		{
			if ( data == BaseRailTile::DIR_FLAT_X )
			{
				return BaseRailTile::DIR_FLAT_Z;
			}
			else
			{
				return BaseRailTile::DIR_FLAT_X;
			}
		}
	}
	else if ( tile == Tile::door_wood_Id || tile == Tile::door_iron_Id )
	{
		if ( orientation == Direction::SOUTH )
		{
			if ( data == 0 )
			{
				return 2;
			}
			if ( data == 2 )
			{
				return 0;
			}
		}
		else if ( orientation == Direction::WEST )
		{
			// 0 = 1
			// 1 = 2
			// 2 = 3
			// 3 = 0
			return ( data + 1 ) & 3;
		}
		else if ( orientation == Direction::EAST )
		{
			// 0 = 3
			// 1 = 0
			// 2 = 1
			// 3 = 2
			return ( data + 3 ) & 3;
		}
	}
	else if ( tile == Tile::stairs_stone_Id || tile == Tile::stairs_wood_Id || tile == Tile::stairs_netherBricks_Id || tile == Tile::stairs_stoneBrick_Id || tile == Tile::stairs_sandstone_Id)
	{
		if ( orientation == Direction::SOUTH )
		{
			if ( data == 2 )
			{
				return 3;
			}
			if ( data == 3 )
			{
				return 2;
			}
		}
		else if ( orientation == Direction::WEST )
		{
			if ( data == 0 )
			{
				return 2;
			}
			if ( data == 1 )
			{
				return 3;
			}
			if ( data == 2 )
			{
				return 0;
			}
			if ( data == 3 )
			{
				return 1;
			}
		}
		else if ( orientation == Direction::EAST )
		{
			if ( data == 0 )
			{
				return 2;
			}
			if ( data == 1 )
			{
				return 3;
			}
			if ( data == 2 )
			{
				return 1;
			}
			if ( data == 3 )
			{
				return 0;
			}
		}
	}
	else if ( tile == Tile::ladder->id )
	{
		if ( orientation == Direction::SOUTH )
		{
			if ( data == Facing::NORTH )
			{
				return Facing::SOUTH;
			}
			if ( data == Facing::SOUTH )
			{
				return Facing::NORTH;
			}
		}
		else if ( orientation == Direction::WEST )
		{
			if ( data == Facing::NORTH )
			{
				return Facing::WEST;
			}
			if ( data == Facing::SOUTH )
			{
				return Facing::EAST;
			}
			if ( data == Facing::WEST )
			{
				return Facing::NORTH;
			}
			if ( data == Facing::EAST )
			{
				return Facing::SOUTH;
			}
		}
		else if ( orientation == Direction::EAST )
		{
			if ( data == Facing::NORTH )
			{
				return Facing::EAST;
			}
			if ( data == Facing::SOUTH )
			{
				return Facing::WEST;
			}
			if ( data == Facing::WEST )
			{
				return Facing::NORTH;
			}
			if ( data == Facing::EAST )
			{
				return Facing::SOUTH;
			}
		}

	}
	else if ( tile == Tile::button->id )
	{
		if ( orientation == Direction::SOUTH )
		{
			if ( data == 3 )
			{
				return 4;
			}
			if ( data == 4 )
			{
				return 3;
			}
		}
		else if ( orientation == Direction::WEST )
		{
			if ( data == 3 )
			{
				return 1;
			}
			if ( data == 4 )
			{
				return 2;
			}
			if ( data == 2 )
			{
				return 3;
			}
			if ( data == 1 )
			{
				return 4;
			}
		}
		else if ( orientation == Direction::EAST )
		{
			if ( data == 3 )
			{
				return 2;
			}
			if ( data == 4 )
			{
				return 1;
			}
			if ( data == 2 )
			{
				return 3;
			}
			if ( data == 1 )
			{
				return 4;
			}
		}
	}
	else if (tile == Tile::tripWireSource_Id || (Tile::tiles[tile] != nullptr && dynamic_cast<DirectionalTile *>(Tile::tiles[tile])))
	{
		if (orientation == Direction::SOUTH)
		{
			if (data == Direction::SOUTH || data == Direction::NORTH)
			{
				return Direction::DIRECTION_OPPOSITE[data];
			}
		}
		else if (orientation == Direction::WEST)
		{
			if (data == Direction::NORTH)
			{
				return Direction::WEST;
			}
			if (data == Direction::SOUTH)
			{
				return Direction::EAST;
			}
			if (data == Direction::WEST)
			{
				return Direction::NORTH;
			}
			if (data == Direction::EAST)
			{
				return Direction::SOUTH;
			}
		}
		else if (orientation == Direction::EAST)
		{
			if (data == Direction::NORTH)
			{
				return Direction::EAST;
			}
			if (data == Direction::SOUTH)
			{
				return Direction::WEST;
			}
			if (data == Direction::WEST)
			{
				return Direction::NORTH;
			}
			if (data == Direction::EAST)
			{
				return Direction::SOUTH;
			}
		}
	}
	else if (tile == Tile::pistonBase_Id || tile == Tile::pistonStickyBase_Id || tile == Tile::lever_Id || tile == Tile::dispenser_Id)
	{
		if (orientation == Direction::SOUTH)
		{
			if (data == Facing::NORTH || data == Facing::SOUTH)
			{
				return Facing::OPPOSITE_FACING[data];
			}
		}
		else if (orientation == Direction::WEST)
		{
			if (data == Facing::NORTH)
			{
				return Facing::WEST;
			}
			if (data == Facing::SOUTH)
			{
				return Facing::EAST;
			}
			if (data == Facing::WEST)
			{
				return Facing::NORTH;
			}
			if (data == Facing::EAST)
			{
				return Facing::SOUTH;
			}
		} else if (orientation == Direction::EAST)
		{
			if (data == Facing::NORTH)
			{
				return Facing::EAST;
			}
			if (data == Facing::SOUTH)
			{
				return Facing::WEST;
			}
			if (data == Facing::WEST)
			{
				return Facing::NORTH;
			}
			if (data == Facing::EAST)
			{
				return Facing::SOUTH;
			}
		}
	}
	return data;

}

void StructurePiece::placeBlock( Level* level, int block, int data, int x, int y, int z, BoundingBox* chunkBB )
{
	int worldX = getWorldX( x, z );
	int worldY = getWorldY( y );
	int worldZ = getWorldZ( x, z );

	if ( !chunkBB->isInside( worldX, worldY, worldZ ) )
	{
		return;
	}

	// 4J Stu - We shouldn't be removing bedrock when generating things (eg in SuperFlat)
	if(worldY == 0) return;

	level->setTileAndData( worldX, worldY, worldZ, block, data, Tile::UPDATE_CLIENTS);
}

int StructurePiece::getBlock( Level* level, int x, int y, int z, BoundingBox* chunkBB )
{
	int worldX = getWorldX( x, z );
	int worldY = getWorldY( y );
	int worldZ = getWorldZ( x, z );

	if ( !chunkBB->isInside( worldX, worldY, worldZ ) )
	{
		return 0;
	}

	return level->getTile( worldX, worldY, worldZ );
}

void StructurePiece::generateAirBox(Level *level, BoundingBox *chunkBB, int x0, int y0, int z0, int x1, int y1, int z1)
{
	for (int y = y0; y <= y1; y++)
	{
		for (int x = x0; x <= x1; x++)
		{
			for (int z = z0; z <= z1; z++)
			{
				placeBlock(level, 0, 0, x, y, z, chunkBB);
			}
		}
	}
}

void StructurePiece::generateBox( Level* level, BoundingBox* chunkBB, int x0, int y0, int z0, int x1, int y1, int z1,
								 int edgeTile, int fillTile, bool skipAir )
{
	for ( int y = y0; y <= y1; y++ )
	{
		for ( int x = x0; x <= x1; x++ )
		{
			for ( int z = z0; z <= z1; z++ )
			{

				if ( skipAir && getBlock( level, x, y, z, chunkBB ) == 0 )
				{
					continue;
				}
				if ( y == y0 || y == y1 || x == x0 || x == x1 || z == z0 || z == z1 )
				{
					placeBlock( level, edgeTile, 0, x, y, z, chunkBB );
				}
				else
				{
					placeBlock( level, fillTile, 0, x, y, z, chunkBB );
				}

			}
		}
	}
}

void StructurePiece::generateBox(Level *level, BoundingBox *chunkBB, int x0, int y0, int z0, int x1, int y1, int z1, int edgeTile, int edgeData, int fillTile, int fillData, bool skipAir)
{
	for (int y = y0; y <= y1; y++)
	{
		for (int x = x0; x <= x1; x++)
		{
			for (int z = z0; z <= z1; z++)
			{

				if (skipAir && getBlock(level, x, y, z, chunkBB) == 0)
				{
					continue;
				}
				if (y == y0 || y == y1 || x == x0 || x == x1 || z == z0 || z == z1)
				{
					placeBlock(level, edgeTile, edgeData, x, y, z, chunkBB);
				}
				else
				{
					placeBlock(level, fillTile, fillData, x, y, z, chunkBB);
				}

			}
		}
	}
}

void StructurePiece::generateBox( Level* level, BoundingBox* chunkBB, BoundingBox* boxBB, int edgeTile, int fillTile,
								 bool skipAir )
{
	generateBox( level, chunkBB, boxBB->x0, boxBB->y0, boxBB->z0, boxBB->x1, boxBB->y1, boxBB->z1, edgeTile, fillTile,
		skipAir );
}

void StructurePiece::generateBox( Level* level, BoundingBox* chunkBB, int x0, int y0, int z0, int x1, int y1, int z1,
								 bool skipAir, Random* random, StructurePiece::BlockSelector* selector )
{
	for ( int y = y0; y <= y1; y++ )
	{
		for ( int x = x0; x <= x1; x++ )
		{
			for ( int z = z0; z <= z1; z++ )
			{

				if ( skipAir && getBlock( level, x, y, z, chunkBB ) == 0 )
				{
					continue;
				}
				selector->next( random, x, y, z, y == y0 || y == y1 || x == x0 || x == x1 || z == z0 || z == z1 );
				placeBlock( level, selector->getNextId(), selector->getNextData(), x, y, z, chunkBB );

			}
		}
	}
}

void StructurePiece::generateBox( Level* level, BoundingBox* chunkBB, BoundingBox* boxBB, bool skipAir, Random* random,
								 StructurePiece::BlockSelector* selector )
{
	generateBox( level, chunkBB, boxBB->x0, boxBB->y0, boxBB->z0, boxBB->x1, boxBB->y1, boxBB->z1, skipAir, random,
		selector );
}

void StructurePiece::generateMaybeBox( Level* level, BoundingBox* chunkBB, Random *random, float probability, int x0,
									  int y0, int z0, int x1, int y1, int z1, int edgeTile, int fillTile,
									  bool skipAir )
{
	for ( int y = y0; y <= y1; y++ )
	{
		for ( int x = x0; x <= x1; x++ )
		{
			for ( int z = z0; z <= z1; z++ )
			{

				if ( random->nextFloat() > probability )
				{
					continue;
				}
				if ( skipAir && getBlock( level, x, y, z, chunkBB ) == 0 )
				{
					continue;
				}
				if ( y == y0 || y == y1 || x == x0 || x == x1 || z == z0 || z == z1 )
				{
					placeBlock( level, edgeTile, 0, x, y, z, chunkBB );
				}
				else
				{
					placeBlock( level, fillTile, 0, x, y, z, chunkBB );
				}

			}
		}
	}
}

void StructurePiece::maybeGenerateBlock( Level* level, BoundingBox* chunkBB, Random *random, float probability, int x,
										int y, int z, int tile, int data )
{
	if ( random->nextFloat() < probability )
	{
		placeBlock( level, tile, data, x, y, z, chunkBB );
	}
}

void StructurePiece::generateUpperHalfSphere( Level* level, BoundingBox* chunkBB, int x0, int y0, int z0, int x1,
											 int y1, int z1, int fillTile, bool skipAir )
{
	float	diagX = static_cast<float>(x1 - x0 + 1);
	float	diagY = static_cast<float>(y1 - y0 + 1);
	float	diagZ = static_cast<float>(z1 - z0 + 1);
	float	cx = x0 + diagX / 2;
	float	cz = z0 + diagZ / 2;

	for ( int y = y0; y <= y1; y++ )
	{
		float normalizedYDistance = static_cast<float>(y - y0) / diagY;

		for ( int x = x0; x <= x1; x++ )
		{
			float normalizedXDistance = ( float )( x - cx ) / ( diagX * 0.5f );

			for ( int z = z0; z <= z1; z++ )
			{
				float normalizedZDistance = ( float )( z - cz ) / ( diagZ * 0.5f );

				if ( skipAir && getBlock( level, x, y, z, chunkBB ) == 0 )
				{
					continue;
				}

				float dist = ( normalizedXDistance * normalizedXDistance ) + ( normalizedYDistance *
					normalizedYDistance ) + ( normalizedZDistance * normalizedZDistance );

				if ( dist <= 1.05f )
				{
					placeBlock( level, fillTile, 0, x, y, z, chunkBB );
				}

			}
		}
	}

}

void StructurePiece::generateAirColumnUp( Level* level, int x, int startY, int z, BoundingBox* chunkBB )
{
	int worldX = getWorldX( x, z );
	int worldY = getWorldY( startY );
	int worldZ = getWorldZ( x, z );

	if ( !chunkBB->isInside( worldX, worldY, worldZ ) )
	{
		return;
	}

	while ( !level->isEmptyTile( worldX, worldY, worldZ ) && worldY < Level::maxBuildHeight - 1 )
	{
		level->setTileAndData( worldX, worldY, worldZ, 0, 0, Tile::UPDATE_CLIENTS);
		worldY++;
	}
}

void StructurePiece::fillColumnDown( Level* level, int tile, int tileData, int x, int startY, int z, BoundingBox* chunkBB )
{
	int worldX = getWorldX( x, z );
	int worldY = getWorldY( startY );
	int worldZ = getWorldZ( x, z );

	if ( !chunkBB->isInside( worldX, worldY, worldZ ) )
	{
		return;
	}

	while ( ( level->isEmptyTile( worldX, worldY, worldZ ) || level->getMaterial( worldX, worldY, worldZ )->isLiquid() ) && worldY > 1 )
	{
		level->setTileAndData( worldX, worldY, worldZ, tile, tileData, Tile::UPDATE_CLIENTS );
		worldY--;
	}
}

void StructurePiece::createDoor( Level* level, BoundingBox* chunkBB, Random* random, int x, int y, int z,
								int orientation )
{
	int worldX = getWorldX( x, z );
	int worldY = getWorldY( y );
	int worldZ = getWorldZ( x, z );

	if ( chunkBB->isInside( worldX, worldY, worldZ ) )
	{
		DoorItem::place( level, worldX, worldY, worldZ, orientation, Tile::door_wood );
	}
}
VillagePieces::PieceWeight::PieceWeight(VillagePieces::EPieceClass pieceClass, int weight, int maxPlaceCount) : weight(weight)
{
	this->placeCount = 0;	// 4J added initialiser
	this->pieceClass = pieceClass;
	this->maxPlaceCount = maxPlaceCount;
}

bool VillagePieces::PieceWeight::doPlace(int depth)
{
	return maxPlaceCount == 0 || placeCount < maxPlaceCount;
}

bool VillagePieces::PieceWeight::isValid()
{
	return maxPlaceCount == 0 || placeCount < maxPlaceCount;
}

list<VillagePieces::PieceWeight *> *VillagePieces::createPieceSet(Random *random, int villageSize)
{
	list<PieceWeight *> *newPieces = new list<PieceWeight *>;

	newPieces->push_back(new PieceWeight(VillagePieces::EPieceClass_SimpleHouse, 4, Mth::nextInt(random, 2 + villageSize, 4 + villageSize * 2)));
	newPieces->push_back(new PieceWeight(VillagePieces::EPieceClass_SmallTemple, 20, Mth::nextInt(random, 0 + villageSize, 1 + villageSize)));
	newPieces->push_back(new PieceWeight(VillagePieces::EPieceClass_BookHouse, 20, Mth::nextInt(random, 0 + villageSize, 2 + villageSize)));
	newPieces->push_back(new PieceWeight(VillagePieces::EPieceClass_SmallHut, 3, Mth::nextInt(random, 2 + villageSize, 5 + villageSize * 3)));
	newPieces->push_back(new PieceWeight(VillagePieces::EPieceClass_PigHouse, 15, Mth::nextInt(random, 0 + villageSize, 2 + villageSize)));
	newPieces->push_back(new PieceWeight(VillagePieces::EPieceClass_DoubleFarmland, 3, Mth::nextInt(random, 1 + villageSize, 4 + villageSize)));
	newPieces->push_back(new PieceWeight(VillagePieces::EPieceClass_Farmland, 3, Mth::nextInt(random, 2 + villageSize, 4 + villageSize * 2)));
	newPieces->push_back(new PieceWeight(VillagePieces::EPieceClass_Smithy, 15, Mth::nextInt(random, 0, 1 + villageSize)));
	newPieces->push_back(new PieceWeight(VillagePieces::EPieceClass_TwoRoomHouse, 8, Mth::nextInt(random, 0 + villageSize, 3 + villageSize * 2)));

	// silly way of filtering "infinite" buildings
    auto it = newPieces->begin();
    while( it != newPieces->end() )
	{
		if( (*it)->maxPlaceCount == 0 )
		{
			delete (*it);
			it = newPieces->erase(it);
		}
		else
		{
			it++;
		}
	}

	return newPieces;
}

int VillagePieces::updatePieceWeight(list<PieceWeight *> *currentPieces)
{
	bool hasAnyPieces = false;
	int totalWeight = 0;
	for(auto& piece : *currentPieces)
	{
		if (piece->maxPlaceCount > 0 && piece->placeCount < piece->maxPlaceCount)
		{
			hasAnyPieces = true;
		}
		totalWeight += piece->weight;
	}
	return (hasAnyPieces ? totalWeight : -1);
}

VillagePieces::VillagePiece *VillagePieces::findAndCreatePieceFactory(StartPiece *startPiece, VillagePieces::PieceWeight *piece, list<StructurePiece *> *pieces, Random *random, int footX, int footY, int footZ, int direction, int depth)
{
	VillagePieces::EPieceClass pieceClass = piece->pieceClass;
	VillagePiece *villagePiece = nullptr;

	if (pieceClass == VillagePieces::EPieceClass_SimpleHouse)
	{
		villagePiece = SimpleHouse::createPiece(startPiece, pieces, random, footX, footY, footZ, direction, depth);
	}
	else if (pieceClass == VillagePieces::EPieceClass_SmallTemple)
	{
		villagePiece = SmallTemple::createPiece(startPiece, pieces, random, footX, footY, footZ, direction, depth);
	}
	else if (pieceClass == VillagePieces::EPieceClass_BookHouse)
	{
		villagePiece = BookHouse::createPiece(startPiece, pieces, random, footX, footY, footZ, direction, depth);
	}
	else if (pieceClass == VillagePieces::EPieceClass_SmallHut)
	{
		villagePiece = SmallHut::createPiece(startPiece, pieces, random, footX, footY, footZ, direction, depth);
	}
	else if (pieceClass == VillagePieces::EPieceClass_PigHouse)
	{
		villagePiece = PigHouse::createPiece(startPiece, pieces, random, footX, footY, footZ, direction, depth);
	}
	else if (pieceClass == VillagePieces::EPieceClass_DoubleFarmland)
	{
		villagePiece = DoubleFarmland::createPiece(startPiece, pieces, random, footX, footY, footZ, direction, depth);
	}
	else if (pieceClass == VillagePieces::EPieceClass_Farmland)
	{
		villagePiece = Farmland::createPiece(startPiece, pieces, random, footX, footY, footZ, direction, depth);
	}
	else if (pieceClass == VillagePieces::EPieceClass_Smithy)
	{
		villagePiece = Smithy::createPiece(startPiece, pieces, random, footX, footY, footZ, direction, depth);
	}
	else if (pieceClass == VillagePieces::EPieceClass_TwoRoomHouse)
	{
		villagePiece = TwoRoomHouse::createPiece(startPiece, pieces, random, footX, footY, footZ, direction, depth);
	}

	return villagePiece;
}

VillagePieces::VillagePiece *VillagePieces::generatePieceFromSmallDoor(StartPiece *startPiece, list<StructurePiece *> *pieces, Random *random, int footX, int footY, int footZ, int direction, int depth)
{
	int totalWeight = updatePieceWeight(startPiece->pieceSet);
	if (totalWeight <= 0)
	{
		return nullptr;
	}

	int numAttempts = 0;
	while (numAttempts < 5)
	{
		numAttempts++;

		int weightSelection = random->nextInt(totalWeight);
        for ( PieceWeight *piece : *startPiece->pieceSet )
        {
			weightSelection -= piece->weight;
			if (weightSelection < 0)
			{
				if (!piece->doPlace(depth) || (piece == startPiece->previousPiece && startPiece->pieceSet->size() > 1))
				{
					break;
				}

				VillagePiece *villagePiece = findAndCreatePieceFactory(startPiece, piece, pieces, random, footX, footY, footZ, direction, depth);
				if (villagePiece != nullptr)
				{
					piece->placeCount++;
					startPiece->previousPiece = piece;

					if (!piece->isValid())
					{
						startPiece->pieceSet->remove(piece);
					}
					return villagePiece;
				}
			}
		}
	}

	// attempt to place a light post instead
	{
		BoundingBox *box = LightPost::findPieceBox(startPiece, pieces, random, footX, footY, footZ, direction);
		if (box != nullptr)
		{
			return new LightPost(startPiece, depth, random, box, direction);
		}
		delete box;
	}

	return nullptr;
}

StructurePiece *VillagePieces::generateAndAddPiece(StartPiece *startPiece, list<StructurePiece *> *pieces, Random *random, int footX, int footY, int footZ, int direction, int depth)
{
	if (depth > MAX_DEPTH)
	{
		return nullptr;
	}
	if (abs(footX - startPiece->getBoundingBox()->x0) > 7 * 16 || abs(footZ - startPiece->getBoundingBox()->z0) > 7 * 16)
	{
		return nullptr;
	}

	StructurePiece *newPiece = generatePieceFromSmallDoor(startPiece, pieces, random, footX, footY, footZ, direction, depth + 1);
	if (newPiece != nullptr)
	{
		int x = (newPiece->boundingBox->x0 + newPiece->boundingBox->x1) / 2;
		int z = (newPiece->boundingBox->z0 + newPiece->boundingBox->z1) / 2;
		int xs = newPiece->boundingBox->x1 - newPiece->boundingBox->x0;
		int zs = newPiece->boundingBox->z1 - newPiece->boundingBox->z0;
		int r = xs > zs ? xs : zs;
		if (startPiece->getBiomeSource()->containsOnly(x, z, r / 2 + 4, VillageFeature::allowedBiomes))
		{
			pieces->push_back(newPiece);
			startPiece->pendingHouses.push_back(newPiece);
			return newPiece;
		}
		delete newPiece;
	}
	return nullptr;
}

StructurePiece *VillagePieces::generateAndAddRoadPiece(StartPiece *startPiece, list<StructurePiece *> *pieces, Random *random, int footX, int footY, int footZ, int direction, int depth)
{
	if (depth > BASE_ROAD_DEPTH + startPiece->villageSize)
	{
		return nullptr;
	}
	if (abs(footX - startPiece->getBoundingBox()->x0) > 7 * 16 || abs(footZ - startPiece->getBoundingBox()->z0) > 7 * 16)
	{
		return nullptr;
	}

	BoundingBox *box = StraightRoad::findPieceBox(startPiece, pieces, random, footX, footY, footZ, direction);
	if (box != nullptr && box->y0 > LOWEST_Y_POSITION)
	{
		StructurePiece *newPiece = new StraightRoad(startPiece, depth, random, box, direction);
		int x = (newPiece->boundingBox->x0 + newPiece->boundingBox->x1) / 2;
		int z = (newPiece->boundingBox->z0 + newPiece->boundingBox->z1) / 2;
		int xs = newPiece->boundingBox->x1 - newPiece->boundingBox->x0;
		int zs = newPiece->boundingBox->z1 - newPiece->boundingBox->z0;
		int r = xs > zs ? xs : zs;
		if (startPiece->getBiomeSource()->containsOnly(x, z, r / 2 + 4, VillageFeature::allowedBiomes))
		{
			pieces->push_back(newPiece);
			startPiece->pendingRoads.push_back(newPiece);
			return newPiece;
		}
		// 4J Stu - The dtor for newPiece will destroy box
		delete newPiece;
	}
	else if(box != nullptr)
	{
		delete box;
	}

	return nullptr;
}

VillagePieces::VillagePiece::VillagePiece()
{
	heightPosition = -1;
	spawnedVillagerCount = 0;
	isDesertVillage = false;
	startPiece = nullptr;
	// for reflection
}

VillagePieces::VillagePiece::VillagePiece(StartPiece *startPiece, int genDepth) : StructurePiece(genDepth)
{
	heightPosition = -1;
	isDesertVillage = false;
	spawnedVillagerCount = 0;
	this->startPiece = startPiece;
	if (startPiece != nullptr)
	{
		this->isDesertVillage = startPiece->isDesertVillage;
	}
}

StructurePiece *VillagePieces::VillagePiece::generateHouseNorthernLeft(StartPiece *startPiece, list<StructurePiece *> *pieces, Random *random, int yOff, int zOff)
{
	switch (orientation)
	{
	case Direction::NORTH:
		return generateAndAddPiece(startPiece, pieces, random, boundingBox->x0 - 1, boundingBox->y0 + yOff, boundingBox->z0 + zOff, Direction::WEST, getGenDepth());
	case Direction::SOUTH:
		return generateAndAddPiece(startPiece, pieces, random, boundingBox->x0 - 1, boundingBox->y0 + yOff, boundingBox->z0 + zOff, Direction::WEST, getGenDepth());
	case Direction::WEST:
		return generateAndAddPiece(startPiece, pieces, random, boundingBox->x0 + zOff, boundingBox->y0 + yOff, boundingBox->z0 - 1, Direction::NORTH, getGenDepth());
	case Direction::EAST:
		return generateAndAddPiece(startPiece, pieces, random, boundingBox->x0 + zOff, boundingBox->y0 + yOff, boundingBox->z0 - 1, Direction::NORTH, getGenDepth());
	}
	return nullptr;
}

StructurePiece *VillagePieces::VillagePiece::generateHouseNorthernRight(StartPiece *startPiece, list<StructurePiece *> *pieces, Random *random, int yOff, int zOff)
{
	switch (orientation)
	{
	case Direction::NORTH:
		return generateAndAddPiece(startPiece, pieces, random, boundingBox->x1 + 1, boundingBox->y0 + yOff, boundingBox->z0 + zOff, Direction::EAST, getGenDepth());
	case Direction::SOUTH:
		return generateAndAddPiece(startPiece, pieces, random, boundingBox->x1 + 1, boundingBox->y0 + yOff, boundingBox->z0 + zOff, Direction::EAST, getGenDepth());
	case Direction::WEST:
		return generateAndAddPiece(startPiece, pieces, random, boundingBox->x0 + zOff, boundingBox->y0 + yOff, boundingBox->z1 + 1, Direction::SOUTH, getGenDepth());
	case Direction::EAST:
		return generateAndAddPiece(startPiece, pieces, random, boundingBox->x0 + zOff, boundingBox->y0 + yOff, boundingBox->z1 + 1, Direction::SOUTH, getGenDepth());
	}
	return nullptr;
}

int VillagePieces::VillagePiece::getAverageGroundHeight(Level *level, BoundingBox *chunkBB)
{
	int total = 0;
	int count = 0;
	for (int z = boundingBox->z0; z <= boundingBox->z1; z++)
	{
		for (int x = boundingBox->x0; x <= boundingBox->x1; x++)
		{
			if (chunkBB->isInside(x, 64, z))
			{
				total += Math::_max(level->getTopSolidBlock(x, z), level->dimension->getSpawnYPosition());
				count++;
			}
		}
	}

	if (count == 0)
	{
		return -1;
	}
	return total / count;
}

bool VillagePieces::VillagePiece::isOkBox(BoundingBox *box, StartPiece *startRoom)
{
	bool bIsOk = false;

	if(box != nullptr)
	{
		if( box->y0 > LOWEST_Y_POSITION ) bIsOk = true;

		int xzSize = startRoom->m_level->getLevelData()->getXZSize();
		int blockMin = -( (xzSize << 4) / 2) + 1;
		int blockMax = ( (xzSize << 4) / 2 ) - 1;

		if(box->x0 <= blockMin) bIsOk = false;
		if(box->z0 <= blockMin) bIsOk = false;
		if(box->x1 >= blockMax) bIsOk = false;
		if(box->z1 >= blockMax) bIsOk = false;
	}

	return bIsOk;
}

int VillagePieces::VillagePiece::biomeBlock(int tile, int data)
{
	if (isDesertVillage)
	{
		if (tile == Tile::treeTrunk_Id)
		{
			return Tile::sandStone_Id;
		}
		else if (tile == Tile::cobblestone_Id)
		{
			return Tile::sandStone_Id;
		}
		else if (tile == Tile::wood_Id)
		{
			return Tile::sandStone_Id;
		}
		else if (tile == Tile::stairs_wood_Id)
		{
			return Tile::stairs_sandstone_Id;
		}
		else if (tile == Tile::stairs_stone_Id)
		{
			return Tile::stairs_sandstone_Id;
		}
		else if (tile == Tile::gravel_Id)
		{
			return Tile::sandStone_Id;
		}
	}
	return tile;
}

int VillagePieces::VillagePiece::biomeData(int tile, int data)
{
	if (isDesertVillage)
	{
		if (tile == Tile::treeTrunk_Id)
		{
			return 0;
		}
		else if (tile == Tile::cobblestone_Id)
		{
			return SandStoneTile::TYPE_DEFAULT;
		}
		else if (tile == Tile::wood_Id)
		{
			return SandStoneTile::TYPE_SMOOTHSIDE;
		}
	}
	return data;
}

void VillagePieces::VillagePiece::placeBlock(Level *level, int block, int data, int x, int y, int z, BoundingBox *chunkBB)
{
	int bblock = biomeBlock(block, data);
	int bdata = biomeData(block, data);
	StructurePiece::placeBlock(level, bblock, bdata, x, y, z, chunkBB);
}

void VillagePieces::VillagePiece::generateBox(Level *level, BoundingBox *chunkBB, int x0, int y0, int z0, int x1, int y1, int z1, int edgeTile, int fillTile, bool skipAir)
{
	int bEdge = biomeBlock(edgeTile, 0);
	int bEdgeData = biomeData(edgeTile, 0);
	int bFill = biomeBlock(fillTile, 0);
	int bFillData = biomeData(fillTile, 0);
	StructurePiece::generateBox(level, chunkBB, x0, y0, z0, x1, y1, z1, bEdge, bEdgeData, bFill, bFillData, skipAir);
}

void VillagePieces::VillagePiece::fillColumnDown(Level *level, int block, int data, int x, int startY, int z, BoundingBox *chunkBB)
{
	int bblock = biomeBlock(block, data);
	int bdata = biomeData(block, data);
	StructurePiece::fillColumnDown(level, bblock, bdata, x, startY, z, chunkBB);
}

VillagePieces::Well::Well()
{
	// for reflection
}

VillagePieces::Well::Well(StartPiece *startPiece, int genDepth, Random *random, int west, int north) : VillagePiece(startPiece, genDepth)
{
	orientation = random->nextInt(4);

	switch (orientation)
	{
	case Direction::NORTH:
	case Direction::SOUTH:
		boundingBox = new BoundingBox(west, 64, north, west + width - 1, 64 + height - 1, north + depth - 1);
		break;
	default:
		boundingBox = new BoundingBox(west, 64, north, west + depth - 1, 64 + height - 1, north + width - 1);
		break;
	}
}

VillagePieces::Well::Well(StartPiece *startPiece, int genDepth, Random *random, BoundingBox *stairsBox, int direction) : VillagePiece(startPiece, genDepth)
{
	orientation = direction;
	boundingBox = stairsBox;
}

void VillagePieces::Well::addChildren(StructurePiece *startPiece, list<StructurePiece *> *pieces, Random *random)
{
	generateAndAddRoadPiece(static_cast<StartPiece *>(startPiece), pieces, random, boundingBox->x0 - 1, boundingBox->y1 - 4, boundingBox->z0 + 1, Direction::WEST, getGenDepth());
	generateAndAddRoadPiece(static_cast<StartPiece *>(startPiece), pieces, random, boundingBox->x1 + 1, boundingBox->y1 - 4, boundingBox->z0 + 1, Direction::EAST, getGenDepth());
	generateAndAddRoadPiece(static_cast<StartPiece *>(startPiece), pieces, random, boundingBox->x0 + 1, boundingBox->y1 - 4, boundingBox->z0 - 1, Direction::NORTH, getGenDepth());
	generateAndAddRoadPiece(static_cast<StartPiece *>(startPiece), pieces, random, boundingBox->x0 + 1, boundingBox->y1 - 4, boundingBox->z1 + 1, Direction::SOUTH, getGenDepth());
}

bool VillagePieces::Well::postProcess(Level *level, Random *random, BoundingBox *chunkBB)
{
	if (heightPosition < 0)
	{
		heightPosition = getAverageGroundHeight(level, chunkBB);
		if (heightPosition < 0)
		{
			return true;
		}
		boundingBox->move(0, heightPosition - boundingBox->y1 + 3, 0);
	}

	generateBox(level, chunkBB, 1, 0, 1, 4, height - 3, 4, Tile::cobblestone_Id, Tile::water_Id, false);
	placeBlock(level, 0, 0, 2, height - 3, 2, chunkBB);
	placeBlock(level, 0, 0, 3, height - 3, 2, chunkBB);
	placeBlock(level, 0, 0, 2, height - 3, 3, chunkBB);
	placeBlock(level, 0, 0, 3, height - 3, 3, chunkBB);

	placeBlock(level, Tile::fence_Id, 0, 1, height - 2, 1, chunkBB);
	placeBlock(level, Tile::fence_Id, 0, 1, height - 1, 1, chunkBB);
	placeBlock(level, Tile::fence_Id, 0, 4, height - 2, 1, chunkBB);
	placeBlock(level, Tile::fence_Id, 0, 4, height - 1, 1, chunkBB);
	placeBlock(level, Tile::fence_Id, 0, 1, height - 2, 4, chunkBB);
	placeBlock(level, Tile::fence_Id, 0, 1, height - 1, 4, chunkBB);
	placeBlock(level, Tile::fence_Id, 0, 4, height - 2, 4, chunkBB);
	placeBlock(level, Tile::fence_Id, 0, 4, height - 1, 4, chunkBB);
	generateBox(level, chunkBB, 1, height, 1, 4, height, 4, Tile::cobblestone_Id, Tile::cobblestone_Id, false);

	for (int z = 0; z <= 5; z++)
	{
		for (int x = 0; x <= 5; x++)
		{
			// only do the frame
			if (x != 0 && x != 5 && z != 0 && z != 5)
			{
				continue;
			}
			placeBlock(level, Tile::gravel_Id, 0, x, height - 4, z, chunkBB);
			generateAirColumnUp(level, x, height - 3, z, chunkBB);
		}
	}

	return true;

}

VillagePieces::StartPiece::StartPiece()
{
	// for reflection
}

VillagePieces::StartPiece::StartPiece(BiomeSource *biomeSource, int genDepth, Random *random, int west, int north, list<PieceWeight *> *pieceSet, int villageSize, Level *level) : Well(nullptr, 0, random, west, north)
{
	isLibraryAdded = false;		// 4J - added initialiser
	previousPiece = nullptr;		// 4J - added initialiser
	this->biomeSource = biomeSource;
	this->pieceSet = pieceSet;
        ownedPieceWeights.assign(pieceSet->begin(), pieceSet->end());
	this->villageSize = villageSize;
	m_level = level;

	Biome *biome = biomeSource->getBiome(west, north);
	isDesertVillage = biome == Biome::desert || biome == Biome::desertHills;
}

VillagePieces::StartPiece::~StartPiece()
{
	for(auto& it : ownedPieceWeights)
	{
		delete it;
	}
	delete pieceSet;
}

BiomeSource *VillagePieces::StartPiece::getBiomeSource()
{
	return biomeSource;
}

VillagePieces::StraightRoad::StraightRoad()
{
	// for reflection
}

VillagePieces::StraightRoad::StraightRoad(StartPiece *startPiece, int genDepth, Random *random, BoundingBox *stairsBox, int direction) : VillageRoadPiece(startPiece, genDepth)
{
	orientation = direction;
	boundingBox = stairsBox;
	length = Math::_max(stairsBox->getXSpan(), stairsBox->getZSpan());
}

void VillagePieces::StraightRoad::addChildren(StructurePiece *startPiece, list<StructurePiece *> *pieces, Random *random)
{
	bool hasHouses = false;

	// place left houses
	int depth = random->nextInt(5);
	while (depth < length - 8)
	{
		StructurePiece *piece = generateHouseNorthernLeft(static_cast<StartPiece *>(startPiece), pieces, random, 0, depth);
		if (piece != nullptr)
		{
			depth += Math::_max(piece->boundingBox->getXSpan(), piece->boundingBox->getZSpan());
			hasHouses = true;
		}
		depth += 2 + random->nextInt(5);
	}

	// place right houses
	depth = random->nextInt(5);
	while (depth < length - 8)
	{
		StructurePiece *piece = generateHouseNorthernRight(static_cast<StartPiece *>(startPiece), pieces, random, 0, depth);
		if (piece != nullptr)
		{
			depth += Math::_max(piece->boundingBox->getXSpan(), piece->boundingBox->getZSpan());
			hasHouses = true;
		}
		depth += 2 + random->nextInt(5);
	}

	if (hasHouses && random->nextInt(3) > 0)
	{
		switch (orientation)
		{
		case Direction::NORTH:
			generateAndAddRoadPiece(static_cast<StartPiece *>(startPiece), pieces, random, boundingBox->x0 - 1, boundingBox->y0, boundingBox->z0, Direction::WEST, getGenDepth());
			break;
		case Direction::SOUTH:
			generateAndAddRoadPiece(static_cast<StartPiece *>(startPiece), pieces, random, boundingBox->x0 - 1, boundingBox->y0, boundingBox->z1 - 2, Direction::WEST, getGenDepth());
			break;
		case Direction::EAST:
			generateAndAddRoadPiece(static_cast<StartPiece *>(startPiece), pieces, random, boundingBox->x1 - 2, boundingBox->y0, boundingBox->z0 - 1, Direction::NORTH, getGenDepth());
			break;
		case Direction::WEST:
			generateAndAddRoadPiece(static_cast<StartPiece *>(startPiece), pieces, random, boundingBox->x0, boundingBox->y0, boundingBox->z0 - 1, Direction::NORTH, getGenDepth());
			break;
		}
	}
	if (hasHouses && random->nextInt(3) > 0)
	{
		switch (orientation)
		{
		case Direction::NORTH:
			generateAndAddRoadPiece(static_cast<StartPiece *>(startPiece), pieces, random, boundingBox->x1 + 1, boundingBox->y0, boundingBox->z0, Direction::EAST, getGenDepth());
			break;
		case Direction::SOUTH:
			generateAndAddRoadPiece(static_cast<StartPiece *>(startPiece), pieces, random, boundingBox->x1 + 1, boundingBox->y0, boundingBox->z1 - 2, Direction::EAST, getGenDepth());
			break;
		case Direction::EAST:
			generateAndAddRoadPiece(static_cast<StartPiece *>(startPiece), pieces, random, boundingBox->x1 - 2, boundingBox->y0, boundingBox->z1 + 1, Direction::SOUTH, getGenDepth());
			break;
		case Direction::WEST:
			generateAndAddRoadPiece(static_cast<StartPiece *>(startPiece), pieces, random, boundingBox->x0, boundingBox->y0, boundingBox->z1 + 1, Direction::SOUTH, getGenDepth());
			break;
		}
	}
}

BoundingBox *VillagePieces::StraightRoad::findPieceBox(StartPiece *startPiece, list<StructurePiece *> *pieces, Random *random, int footX, int footY, int footZ, int direction)
{
	int length = 7 * (Mth::nextInt(random, 3, 5));

	while (length >= 7)
	{
		BoundingBox *box = BoundingBox::orientBox(footX, footY, footZ, 0, 0, 0, width, 3, length, direction);

		if (isOkBox(box, startPiece) && StructurePiece::findCollisionPiece(pieces, box) == nullptr)
		{
			return box;
		}
		delete box;
		length -= 7;
	}

	return nullptr;
}

bool VillagePieces::StraightRoad::postProcess(Level *level, Random *random, BoundingBox *chunkBB)
{
	int tile = biomeBlock(Tile::gravel_Id, 0);
	for (int x = boundingBox->x0; x <= boundingBox->x1; x++)
	{
		for (int z = boundingBox->z0; z <= boundingBox->z1; z++)
		{
			if (chunkBB->isInside(x, 64, z))
			{
				int y = level->getTopSolidBlock(x, z) - 1;
				level->setTileAndData(x, y, z,tile, 0, Tile::UPDATE_CLIENTS);
			}
		}
	}

	return true;
}

VillagePieces::SimpleHouse::SimpleHouse()
{
	hasTerrace = false;
	// for reflection
}

VillagePieces::SimpleHouse::SimpleHouse(StartPiece *startPiece, int genDepth, Random *random, BoundingBox *stairsBox, int direction) : VillagePiece(startPiece, genDepth), hasTerrace(random->nextBoolean())
{
	orientation = direction;
	boundingBox = stairsBox;
}

VillagePieces::SimpleHouse *VillagePieces::SimpleHouse::createPiece(StartPiece *startPiece, list<StructurePiece *> *pieces, Random *random, int footX, int footY, int footZ, int direction, int genDepth)
{
	BoundingBox *box = BoundingBox::orientBox(footX, footY, footZ, 0, 0, 0, width, height, depth, direction);

	if (!isOkBox(box, startPiece) || StructurePiece::findCollisionPiece(pieces, box) != nullptr)
	{
		delete box;
		return nullptr;
	}

	return new SimpleHouse(startPiece, genDepth, random, box, direction);
}

bool VillagePieces::SimpleHouse::postProcess(Level *level, Random *random, BoundingBox *chunkBB)
{
	if (heightPosition < 0)
	{
		heightPosition = getAverageGroundHeight(level, chunkBB);
		if (heightPosition < 0)
		{
			return true;
		}
		boundingBox->move(0, heightPosition - boundingBox->y1 + height - 1, 0);
	}

	// floor
	generateBox(level, chunkBB, 0, 0, 0, 4, 0, 4, Tile::cobblestone_Id, Tile::cobblestone_Id, false);
	// roof
	generateBox(level, chunkBB, 0, 4, 0, 4, 4, 4, Tile::treeTrunk_Id, Tile::treeTrunk_Id, false);
	generateBox(level, chunkBB, 1, 4, 1, 3, 4, 3, Tile::wood_Id, Tile::wood_Id, false);

	// window walls
	placeBlock(level, Tile::cobblestone_Id, 0, 0, 1, 0, chunkBB);
	placeBlock(level, Tile::cobblestone_Id, 0, 0, 2, 0, chunkBB);
	placeBlock(level, Tile::cobblestone_Id, 0, 0, 3, 0, chunkBB);
	placeBlock(level, Tile::cobblestone_Id, 0, 4, 1, 0, chunkBB);
	placeBlock(level, Tile::cobblestone_Id, 0, 4, 2, 0, chunkBB);
	placeBlock(level, Tile::cobblestone_Id, 0, 4, 3, 0, chunkBB);
	placeBlock(level, Tile::cobblestone_Id, 0, 0, 1, 4, chunkBB);
	placeBlock(level, Tile::cobblestone_Id, 0, 0, 2, 4, chunkBB);
	placeBlock(level, Tile::cobblestone_Id, 0, 0, 3, 4, chunkBB);
	placeBlock(level, Tile::cobblestone_Id, 0, 4, 1, 4, chunkBB);
	placeBlock(level, Tile::cobblestone_Id, 0, 4, 2, 4, chunkBB);
	placeBlock(level, Tile::cobblestone_Id, 0, 4, 3, 4, chunkBB);
	generateBox(level, chunkBB, 0, 1, 1, 0, 3, 3, Tile::wood_Id, Tile::wood_Id, false);
	generateBox(level, chunkBB, 4, 1, 1, 4, 3, 3, Tile::wood_Id, Tile::wood_Id, false);
	generateBox(level, chunkBB, 1, 1, 4, 3, 3, 4, Tile::wood_Id, Tile::wood_Id, false);
	placeBlock(level, Tile::thinGlass_Id, 0, 0, 2, 2, chunkBB);
	placeBlock(level, Tile::thinGlass_Id, 0, 2, 2, 4, chunkBB);
	placeBlock(level, Tile::thinGlass_Id, 0, 4, 2, 2, chunkBB);

	// door wall
	placeBlock(level, Tile::wood_Id, 0, 1, 1, 0, chunkBB);
	placeBlock(level, Tile::wood_Id, 0, 1, 2, 0, chunkBB);
	placeBlock(level, Tile::wood_Id, 0, 1, 3, 0, chunkBB);
	placeBlock(level, Tile::wood_Id, 0, 2, 3, 0, chunkBB);
	placeBlock(level, Tile::wood_Id, 0, 3, 3, 0, chunkBB);
	placeBlock(level, Tile::wood_Id, 0, 3, 2, 0, chunkBB);
	placeBlock(level, Tile::wood_Id, 0, 3, 1, 0, chunkBB);
	if (getBlock(level, 2, 0, -1, chunkBB) == 0 && getBlock(level, 2, -1, -1, chunkBB) != 0)
	{
		placeBlock(level, Tile::stairs_stone_Id, getOrientationData(Tile::stairs_stone_Id, 3), 2, 0, -1, chunkBB);
	}

	// fill room with air
	generateBox(level, chunkBB, 1, 1, 1, 3, 3, 3, 0, 0, false);

	// roof fence
	if (hasTerrace) {
		placeBlock(level, Tile::fence_Id, 0, 0, 5, 0, chunkBB);
		placeBlock(level, Tile::fence_Id, 0, 1, 5, 0, chunkBB);
		placeBlock(level, Tile::fence_Id, 0, 2, 5, 0, chunkBB);
		placeBlock(level, Tile::fence_Id, 0, 3, 5, 0, chunkBB);
		placeBlock(level, Tile::fence_Id, 0, 4, 5, 0, chunkBB);
		placeBlock(level, Tile::fence_Id, 0, 0, 5, 4, chunkBB);
		placeBlock(level, Tile::fence_Id, 0, 1, 5, 4, chunkBB);
		placeBlock(level, Tile::fence_Id, 0, 2, 5, 4, chunkBB);
		placeBlock(level, Tile::fence_Id, 0, 3, 5, 4, chunkBB);
		placeBlock(level, Tile::fence_Id, 0, 4, 5, 4, chunkBB);
		placeBlock(level, Tile::fence_Id, 0, 4, 5, 1, chunkBB);
		placeBlock(level, Tile::fence_Id, 0, 4, 5, 2, chunkBB);
		placeBlock(level, Tile::fence_Id, 0, 4, 5, 3, chunkBB);
		placeBlock(level, Tile::fence_Id, 0, 0, 5, 1, chunkBB);
		placeBlock(level, Tile::fence_Id, 0, 0, 5, 2, chunkBB);
		placeBlock(level, Tile::fence_Id, 0, 0, 5, 3, chunkBB);
	}

	// ladder
	if (hasTerrace)
	{
		int orientationData = getOrientationData(Tile::ladder_Id, 3);
		placeBlock(level, Tile::ladder_Id, orientationData, 3, 1, 3, chunkBB);
		placeBlock(level, Tile::ladder_Id, orientationData, 3, 2, 3, chunkBB);
		placeBlock(level, Tile::ladder_Id, orientationData, 3, 3, 3, chunkBB);
		placeBlock(level, Tile::ladder_Id, orientationData, 3, 4, 3, chunkBB);
	}

	// torch
	placeBlock(level, Tile::torch_Id, 0, 2, 3, 1, chunkBB);

	for (int z = 0; z < depth; z++)
	{
		for (int x = 0; x < width; x++)
		{
			generateAirColumnUp(level, x, height, z, chunkBB);
			fillColumnDown(level, Tile::cobblestone_Id, 0, x, -1, z, chunkBB);
		}
	}


	return true;

}

VillagePieces::SmallTemple::SmallTemple()
{
	// for reflection
}

VillagePieces::SmallTemple::SmallTemple(StartPiece *startPiece, int genDepth, Random *random, BoundingBox *stairsBox, int direction) : VillagePiece(startPiece, genDepth)
{
	heightPosition = -1; // 4J added initialiser
	orientation = direction;
	boundingBox = stairsBox;
}

VillagePieces::SmallTemple *VillagePieces::SmallTemple::createPiece(StartPiece *startPiece, list<StructurePiece *> *pieces, Random *random, int footX, int footY, int footZ, int direction, int genDepth)
{
	BoundingBox *box = BoundingBox::orientBox(footX, footY, footZ, 0, 0, 0, width, height, depth, direction);

	if (!isOkBox(box, startPiece) || StructurePiece::findCollisionPiece(pieces, box) != nullptr)
	{
		delete box;
		return nullptr;
	}

	return new SmallTemple(startPiece, genDepth, random, box, direction);
}

bool VillagePieces::SmallTemple::postProcess(Level *level, Random *random, BoundingBox *chunkBB)
{
	if (heightPosition < 0)
	{
		heightPosition = getAverageGroundHeight(level, chunkBB);
		if (heightPosition < 0)
		{
			return true;
		}
		boundingBox->move(0, heightPosition - boundingBox->y1 + height - 1, 0);
	}

	// fill inside with air
	generateBox(level, chunkBB, 1, 1, 1, 3, 3, 7, 0, 0, false);
	generateBox(level, chunkBB, 1, 5, 1, 3, 9, 3, 0, 0, false);

	// floor
	generateBox(level, chunkBB, 1, 0, 0, 3, 0, 8, Tile::cobblestone_Id, Tile::cobblestone_Id, false);

	// front wall
	generateBox(level, chunkBB, 1, 1, 0, 3, 10, 0, Tile::cobblestone_Id, Tile::cobblestone_Id, false);
	// left tall wall
	generateBox(level, chunkBB, 0, 1, 1, 0, 10, 3, Tile::cobblestone_Id, Tile::cobblestone_Id, false);
	// right tall wall
	generateBox(level, chunkBB, 4, 1, 1, 4, 10, 3, Tile::cobblestone_Id, Tile::cobblestone_Id, false);
	// left low wall
	generateBox(level, chunkBB, 0, 0, 4, 0, 4, 7, Tile::cobblestone_Id, Tile::cobblestone_Id, false);
	// right low wall
	generateBox(level, chunkBB, 4, 0, 4, 4, 4, 7, Tile::cobblestone_Id, Tile::cobblestone_Id, false);
	// far low wall
	generateBox(level, chunkBB, 1, 1, 8, 3, 4, 8, Tile::cobblestone_Id, Tile::cobblestone_Id, false);
	// far upper wall
	generateBox(level, chunkBB, 1, 5, 4, 3, 10, 4, Tile::cobblestone_Id, Tile::cobblestone_Id, false);

	// low roof
	generateBox(level, chunkBB, 1, 5, 5, 3, 5, 7, Tile::cobblestone_Id, Tile::cobblestone_Id, false);
	// high roof
	generateBox(level, chunkBB, 0, 9, 0, 4, 9, 4, Tile::cobblestone_Id, Tile::cobblestone_Id, false);
	// middle floor / roof
	generateBox(level, chunkBB, 0, 4, 0, 4, 4, 4, Tile::cobblestone_Id, Tile::cobblestone_Id, false);
	placeBlock(level, Tile::cobblestone_Id, 0, 0, 11, 2, chunkBB);
	placeBlock(level, Tile::cobblestone_Id, 0, 4, 11, 2, chunkBB);
	placeBlock(level, Tile::cobblestone_Id, 0, 2, 11, 0, chunkBB);
	placeBlock(level, Tile::cobblestone_Id, 0, 2, 11, 4, chunkBB);

	// altar pieces
	placeBlock(level, Tile::cobblestone_Id, 0, 1, 1, 6, chunkBB);
	placeBlock(level, Tile::cobblestone_Id, 0, 1, 1, 7, chunkBB);
	placeBlock(level, Tile::cobblestone_Id, 0, 2, 1, 7, chunkBB);
	placeBlock(level, Tile::cobblestone_Id, 0, 3, 1, 6, chunkBB);
	placeBlock(level, Tile::cobblestone_Id, 0, 3, 1, 7, chunkBB);
	placeBlock(level, Tile::stairs_stone_Id, getOrientationData(Tile::stairs_stone_Id, 3), 1, 1, 5, chunkBB);
	placeBlock(level, Tile::stairs_stone_Id, getOrientationData(Tile::stairs_stone_Id, 3), 2, 1, 6, chunkBB);
	placeBlock(level, Tile::stairs_stone_Id, getOrientationData(Tile::stairs_stone_Id, 3), 3, 1, 5, chunkBB);
	placeBlock(level, Tile::stairs_stone_Id, getOrientationData(Tile::stairs_stone_Id, 1), 1, 2, 7, chunkBB);
	placeBlock(level, Tile::stairs_stone_Id, getOrientationData(Tile::stairs_stone_Id, 0), 3, 2, 7, chunkBB);

	// windows
	placeBlock(level, Tile::thinGlass_Id, 0, 0, 2, 2, chunkBB);
	placeBlock(level, Tile::thinGlass_Id, 0, 0, 3, 2, chunkBB);
	placeBlock(level, Tile::thinGlass_Id, 0, 4, 2, 2, chunkBB);
	placeBlock(level, Tile::thinGlass_Id, 0, 4, 3, 2, chunkBB);
	placeBlock(level, Tile::thinGlass_Id, 0, 0, 6, 2, chunkBB);
	placeBlock(level, Tile::thinGlass_Id, 0, 0, 7, 2, chunkBB);
	placeBlock(level, Tile::thinGlass_Id, 0, 4, 6, 2, chunkBB);
	placeBlock(level, Tile::thinGlass_Id, 0, 4, 7, 2, chunkBB);
	placeBlock(level, Tile::thinGlass_Id, 0, 2, 6, 0, chunkBB);
	placeBlock(level, Tile::thinGlass_Id, 0, 2, 7, 0, chunkBB);
	placeBlock(level, Tile::thinGlass_Id, 0, 2, 6, 4, chunkBB);
	placeBlock(level, Tile::thinGlass_Id, 0, 2, 7, 4, chunkBB);
	placeBlock(level, Tile::thinGlass_Id, 0, 0, 3, 6, chunkBB);
	placeBlock(level, Tile::thinGlass_Id, 0, 4, 3, 6, chunkBB);
	placeBlock(level, Tile::thinGlass_Id, 0, 2, 3, 8, chunkBB);

	// torches
	placeBlock(level, Tile::torch_Id, 0, 2, 4, 7, chunkBB);
	placeBlock(level, Tile::torch_Id, 0, 1, 4, 6, chunkBB);
	placeBlock(level, Tile::torch_Id, 0, 3, 4, 6, chunkBB);
	placeBlock(level, Tile::torch_Id, 0, 2, 4, 5, chunkBB);

	// ladder
	int orientationData = getOrientationData(Tile::ladder_Id, 4);
	for (int y = 1; y <= 9; y++)
	{
		placeBlock(level, Tile::ladder_Id, orientationData, 3, y, 3, chunkBB);
	}

	// entrance
	placeBlock(level, 0, 0, 2, 1, 0, chunkBB);
	placeBlock(level, 0, 0, 2, 2, 0, chunkBB);
	createDoor(level, chunkBB, random, 2, 1, 0, getOrientationData(Tile::door_wood_Id, 1));
	if (getBlock(level, 2, 0, -1, chunkBB) == 0 && getBlock(level, 2, -1, -1, chunkBB) != 0)
	{
		placeBlock(level, Tile::stairs_stone_Id, getOrientationData(Tile::stairs_stone_Id, 3), 2, 0, -1, chunkBB);
	}


	for (int z = 0; z < depth; z++)
	{
		for (int x = 0; x < width; x++)
		{
			generateAirColumnUp(level, x, height, z, chunkBB);
			fillColumnDown(level, Tile::cobblestone_Id, 0, x, -1, z, chunkBB);
		}
	}


	return true;

}

VillagePieces::BookHouse::BookHouse()
{
	// for reflection
}

VillagePieces::BookHouse::BookHouse(StartPiece *startPiece, int genDepth, Random *random, BoundingBox *stairsBox, int direction) : VillagePiece(startPiece, genDepth)
{
	heightPosition = -1; // 4J added initialiser
	orientation = direction;
	boundingBox = stairsBox;
}

VillagePieces::BookHouse *VillagePieces::BookHouse::createPiece(StartPiece *startPiece, list<StructurePiece *> *pieces, Random *random, int footX, int footY, int footZ, int direction, int genDepth)
{
	BoundingBox *box = BoundingBox::orientBox(footX, footY, footZ, 0, 0, 0, width, height, depth, direction);

	if (!isOkBox(box, startPiece) || StructurePiece::findCollisionPiece(pieces, box) != nullptr)
	{
		delete box;
		return nullptr;
	}

	return new BookHouse(startPiece, genDepth, random, box, direction);
}

bool VillagePieces::BookHouse::postProcess(Level *level, Random *random, BoundingBox *chunkBB)
{
	if (heightPosition < 0)
	{
		heightPosition = getAverageGroundHeight(level, chunkBB);
		if (heightPosition < 0)
		{
			return true;
		}
		boundingBox->move(0, heightPosition - boundingBox->y1 + height - 1, 0);
	}

	// fill inside with air
	generateBox(level, chunkBB, 1, 1, 1, 7, 5, 4, 0, 0, false);

	// floor
	generateBox(level, chunkBB, 0, 0, 0, 8, 0, 5, Tile::cobblestone_Id, Tile::cobblestone_Id, false);
	// roof
	generateBox(level, chunkBB, 0, 5, 0, 8, 5, 5, Tile::cobblestone_Id, Tile::cobblestone_Id, false);
	generateBox(level, chunkBB, 0, 6, 1, 8, 6, 4, Tile::cobblestone_Id, Tile::cobblestone_Id, false);
	generateBox(level, chunkBB, 0, 7, 2, 8, 7, 3, Tile::cobblestone_Id, Tile::cobblestone_Id, false);
	int southStairs = getOrientationData(Tile::stairs_wood_Id, 3);
	int northStairs = getOrientationData(Tile::stairs_wood_Id, 2);
	for (int d = -1; d <= 2; d++) {
		for (int w = 0; w <= 8; w++) {
			placeBlock(level, Tile::stairs_wood_Id, southStairs, w, 6 + d, d, chunkBB);
			placeBlock(level, Tile::stairs_wood_Id, northStairs, w, 6 + d, 5 - d, chunkBB);
		}
	}

	// rock supports
	generateBox(level, chunkBB, 0, 1, 0, 0, 1, 5, Tile::cobblestone_Id, Tile::cobblestone_Id, false);
	generateBox(level, chunkBB, 1, 1, 5, 8, 1, 5, Tile::cobblestone_Id, Tile::cobblestone_Id, false);
	generateBox(level, chunkBB, 8, 1, 0, 8, 1, 4, Tile::cobblestone_Id, Tile::cobblestone_Id, false);
	generateBox(level, chunkBB, 2, 1, 0, 7, 1, 0, Tile::cobblestone_Id, Tile::cobblestone_Id, false);
	generateBox(level, chunkBB, 0, 2, 0, 0, 4, 0, Tile::cobblestone_Id, Tile::cobblestone_Id, false);
	generateBox(level, chunkBB, 0, 2, 5, 0, 4, 5, Tile::cobblestone_Id, Tile::cobblestone_Id, false);
	generateBox(level, chunkBB, 8, 2, 5, 8, 4, 5, Tile::cobblestone_Id, Tile::cobblestone_Id, false);
	generateBox(level, chunkBB, 8, 2, 0, 8, 4, 0, Tile::cobblestone_Id, Tile::cobblestone_Id, false);

	// wooden walls
	generateBox(level, chunkBB, 0, 2, 1, 0, 4, 4, Tile::wood_Id, Tile::wood_Id, false);
	generateBox(level, chunkBB, 1, 2, 5, 7, 4, 5, Tile::wood_Id, Tile::wood_Id, false);
	generateBox(level, chunkBB, 8, 2, 1, 8, 4, 4, Tile::wood_Id, Tile::wood_Id, false);
	generateBox(level, chunkBB, 1, 2, 0, 7, 4, 0, Tile::wood_Id, Tile::wood_Id, false);

	// windows
	placeBlock(level, Tile::thinGlass_Id, 0, 4, 2, 0, chunkBB);
	placeBlock(level, Tile::thinGlass_Id, 0, 5, 2, 0, chunkBB);
	placeBlock(level, Tile::thinGlass_Id, 0, 6, 2, 0, chunkBB);
	placeBlock(level, Tile::thinGlass_Id, 0, 4, 3, 0, chunkBB);
	placeBlock(level, Tile::thinGlass_Id, 0, 5, 3, 0, chunkBB);
	placeBlock(level, Tile::thinGlass_Id, 0, 6, 3, 0, chunkBB);
	placeBlock(level, Tile::thinGlass_Id, 0, 0, 2, 2, chunkBB);
	placeBlock(level, Tile::thinGlass_Id, 0, 0, 2, 3, chunkBB);
	placeBlock(level, Tile::thinGlass_Id, 0, 0, 3, 2, chunkBB);
	placeBlock(level, Tile::thinGlass_Id, 0, 0, 3, 3, chunkBB);
	placeBlock(level, Tile::thinGlass_Id, 0, 8, 2, 2, chunkBB);
	placeBlock(level, Tile::thinGlass_Id, 0, 8, 2, 3, chunkBB);
	placeBlock(level, Tile::thinGlass_Id, 0, 8, 3, 2, chunkBB);
	placeBlock(level, Tile::thinGlass_Id, 0, 8, 3, 3, chunkBB);
	placeBlock(level, Tile::thinGlass_Id, 0, 2, 2, 5, chunkBB);
	placeBlock(level, Tile::thinGlass_Id, 0, 3, 2, 5, chunkBB);
	placeBlock(level, Tile::thinGlass_Id, 0, 5, 2, 5, chunkBB);
	placeBlock(level, Tile::thinGlass_Id, 0, 6, 2, 5, chunkBB);

	// roof inside and bookshelf
	generateBox(level, chunkBB, 1, 4, 1, 7, 4, 1, Tile::wood_Id, Tile::wood_Id, false);
	generateBox(level, chunkBB, 1, 4, 4, 7, 4, 4, Tile::wood_Id, Tile::wood_Id, false);
	generateBox(level, chunkBB, 1, 3, 4, 7, 3, 4, Tile::bookshelf_Id, Tile::bookshelf_Id, false);

	// couch
	placeBlock(level, Tile::wood_Id, 0, 7, 1, 4, chunkBB);
	placeBlock(level, Tile::stairs_wood_Id, getOrientationData(Tile::stairs_wood_Id, 0), 7, 1, 3, chunkBB);
	int orientationData = getOrientationData(Tile::stairs_wood_Id, 3);
	placeBlock(level, Tile::stairs_wood_Id, orientationData, 6, 1, 4, chunkBB);
	placeBlock(level, Tile::stairs_wood_Id, orientationData, 5, 1, 4, chunkBB);
	placeBlock(level, Tile::stairs_wood_Id, orientationData, 4, 1, 4, chunkBB);
	placeBlock(level, Tile::stairs_wood_Id, orientationData, 3, 1, 4, chunkBB);

	// tables
	placeBlock(level, Tile::fence_Id, 0, 6, 1, 3, chunkBB);
	placeBlock(level, Tile::pressurePlate_wood_Id, 0, 6, 2, 3, chunkBB);
	placeBlock(level, Tile::fence_Id, 0, 4, 1, 3, chunkBB);
	placeBlock(level, Tile::pressurePlate_wood_Id, 0, 4, 2, 3, chunkBB);
	placeBlock(level, Tile::workBench_Id, 0, 7, 1, 1, chunkBB);

	// entrance
	placeBlock(level, 0, 0, 1, 1, 0, chunkBB);
	placeBlock(level, 0, 0, 1, 2, 0, chunkBB);
	createDoor(level, chunkBB, random, 1, 1, 0, getOrientationData(Tile::door_wood_Id, 1));
	if (getBlock(level, 1, 0, -1, chunkBB) == 0 && getBlock(level, 1, -1, -1, chunkBB) != 0)
	{
		placeBlock(level, Tile::stairs_stone_Id, getOrientationData(Tile::stairs_stone_Id, 3), 1, 0, -1, chunkBB);
	}

	for (int z = 0; z < depth; z++)
	{
		for (int x = 0; x < width; x++)
		{
			generateAirColumnUp(level, x, height, z, chunkBB);
			fillColumnDown(level, Tile::cobblestone_Id, 0, x, -1, z, chunkBB);
		}
	}


	return true;

}

VillagePieces::SmallHut::SmallHut()
{
	// for reflection
}

VillagePieces::SmallHut::SmallHut(StartPiece *startPiece, int genDepth, Random *random, BoundingBox *stairsBox, int direction) : VillagePiece(startPiece, genDepth), lowCeiling(random->nextBoolean()), tablePlacement(random->nextInt(3))
{
	heightPosition = -1; // 4J added initialiser

	orientation = direction;
	boundingBox = stairsBox;
}

VillagePieces::SmallHut *VillagePieces::SmallHut::createPiece(StartPiece *startPiece, list<StructurePiece *> *pieces, Random *random, int footX, int footY, int footZ, int direction, int genDepth)
{
	BoundingBox *box = BoundingBox::orientBox(footX, footY, footZ, 0, 0, 0, width, height, depth, direction);

	if (!isOkBox(box, startPiece) || StructurePiece::findCollisionPiece(pieces, box) != nullptr)
	{
		delete box;
		return nullptr;
	}

	return new SmallHut(startPiece, genDepth, random, box, direction);
}

bool VillagePieces::SmallHut::postProcess(Level *level, Random *random, BoundingBox *chunkBB)
{
	if (heightPosition < 0)
	{
		heightPosition = getAverageGroundHeight(level, chunkBB);
		if (heightPosition < 0)
		{
			return true;
		}
		boundingBox->move(0, heightPosition - boundingBox->y1 + height - 1, 0);
	}

	// fill inside with air
	generateBox(level, chunkBB, 1, 1, 1, 3, 5, 4, 0, 0, false);

	// floor
	generateBox(level, chunkBB, 0, 0, 0, 3, 0, 4, Tile::cobblestone_Id, Tile::cobblestone_Id, false);
	generateBox(level, chunkBB, 1, 0, 1, 2, 0, 3, Tile::dirt_Id, Tile::dirt_Id, false);
	// roof
	if (lowCeiling) {
		generateBox(level, chunkBB, 1, 4, 1, 2, 4, 3, Tile::treeTrunk_Id, Tile::treeTrunk_Id, false);
	} else {
		generateBox(level, chunkBB, 1, 5, 1, 2, 5, 3, Tile::treeTrunk_Id, Tile::treeTrunk_Id, false);
	}
	placeBlock(level, Tile::treeTrunk_Id, 0, 1, 4, 0, chunkBB);
	placeBlock(level, Tile::treeTrunk_Id, 0, 2, 4, 0, chunkBB);
	placeBlock(level, Tile::treeTrunk_Id, 0, 1, 4, 4, chunkBB);
	placeBlock(level, Tile::treeTrunk_Id, 0, 2, 4, 4, chunkBB);
	placeBlock(level, Tile::treeTrunk_Id, 0, 0, 4, 1, chunkBB);
	placeBlock(level, Tile::treeTrunk_Id, 0, 0, 4, 2, chunkBB);
	placeBlock(level, Tile::treeTrunk_Id, 0, 0, 4, 3, chunkBB);
	placeBlock(level, Tile::treeTrunk_Id, 0, 3, 4, 1, chunkBB);
	placeBlock(level, Tile::treeTrunk_Id, 0, 3, 4, 2, chunkBB);
	placeBlock(level, Tile::treeTrunk_Id, 0, 3, 4, 3, chunkBB);

	// corners
	generateBox(level, chunkBB, 0, 1, 0, 0, 3, 0, Tile::treeTrunk_Id, Tile::treeTrunk_Id, false);
	generateBox(level, chunkBB, 3, 1, 0, 3, 3, 0, Tile::treeTrunk_Id, Tile::treeTrunk_Id, false);
	generateBox(level, chunkBB, 0, 1, 4, 0, 3, 4, Tile::treeTrunk_Id, Tile::treeTrunk_Id, false);
	generateBox(level, chunkBB, 3, 1, 4, 3, 3, 4, Tile::treeTrunk_Id, Tile::treeTrunk_Id, false);

	// wooden walls
	generateBox(level, chunkBB, 0, 1, 1, 0, 3, 3, Tile::wood_Id, Tile::wood_Id, false);
	generateBox(level, chunkBB, 3, 1, 1, 3, 3, 3, Tile::wood_Id, Tile::wood_Id, false);
	generateBox(level, chunkBB, 1, 1, 0, 2, 3, 0, Tile::wood_Id, Tile::wood_Id, false);
	generateBox(level, chunkBB, 1, 1, 4, 2, 3, 4, Tile::wood_Id, Tile::wood_Id, false);

	// windows
	placeBlock(level, Tile::thinGlass_Id, 0, 0, 2, 2, chunkBB);
	placeBlock(level, Tile::thinGlass_Id, 0, 3, 2, 2, chunkBB);

	// table
	if (tablePlacement > 0) {
		placeBlock(level, Tile::fence_Id, 0, tablePlacement, 1, 3, chunkBB);
		placeBlock(level, Tile::pressurePlate_wood_Id, 0, tablePlacement, 2, 3, chunkBB);
	}

	// entrance
	placeBlock(level, 0, 0, 1, 1, 0, chunkBB);
	placeBlock(level, 0, 0, 1, 2, 0, chunkBB);
	createDoor(level, chunkBB, random, 1, 1, 0, getOrientationData(Tile::door_wood_Id, 1));
	if (getBlock(level, 1, 0, -1, chunkBB) == 0 && getBlock(level, 1, -1, -1, chunkBB) != 0)
	{
		placeBlock(level, Tile::stairs_stone_Id, getOrientationData(Tile::stairs_stone_Id, 3), 1, 0, -1, chunkBB);
	}

	for (int z = 0; z < depth; z++)
	{
		for (int x = 0; x < width; x++)
		{
			generateAirColumnUp(level, x, height, z, chunkBB);
			fillColumnDown(level, Tile::cobblestone_Id, 0, x, -1, z, chunkBB);
		}
	}


	return true;

}

VillagePieces::PigHouse::PigHouse()
{
	// for reflection
}

VillagePieces::PigHouse::PigHouse(StartPiece *startPiece, int genDepth, Random *random, BoundingBox *stairsBox, int direction) : VillagePiece(startPiece, genDepth)
{
	orientation = direction;
	boundingBox = stairsBox;
}

VillagePieces::PigHouse *VillagePieces::PigHouse::createPiece(StartPiece *startPiece, list<StructurePiece *> *pieces, Random *random, int footX, int footY, int footZ, int direction, int genDepth)
{

	BoundingBox *box = BoundingBox::orientBox(footX, footY, footZ, 0, 0, 0, width, height, depth, direction);

	if (!isOkBox(box, startPiece) || StructurePiece::findCollisionPiece(pieces, box) != nullptr)
	{
		delete box;
		return nullptr;
	}

	return new PigHouse(startPiece, genDepth, random, box, direction);
}

bool VillagePieces::PigHouse::postProcess(Level *level, Random *random, BoundingBox *chunkBB)
{
	if (heightPosition < 0)
	{
		heightPosition = getAverageGroundHeight(level, chunkBB);
		if (heightPosition < 0)
		{
			return true;
		}
		boundingBox->move(0, heightPosition - boundingBox->y1 + height - 1, 0);
	}

	// fill inside with air
	generateBox(level, chunkBB, 1, 1, 1, 7, 4, 4, 0, 0, false);
	generateBox(level, chunkBB, 2, 1, 6, 8, 4, 10, 0, 0, false);

	// pig floor
	generateBox(level, chunkBB, 2, 0, 6, 8, 0, 10, Tile::dirt_Id, Tile::dirt_Id, false);
	placeBlock(level, Tile::cobblestone_Id, 0, 6, 0, 6, chunkBB);
	// pig fence
	generateBox(level, chunkBB, 2, 1, 6, 2, 1, 10, Tile::fence_Id, Tile::fence_Id, false);
	generateBox(level, chunkBB, 8, 1, 6, 8, 1, 10, Tile::fence_Id, Tile::fence_Id, false);
	generateBox(level, chunkBB, 3, 1, 10, 7, 1, 10, Tile::fence_Id, Tile::fence_Id, false);

	// floor
	generateBox(level, chunkBB, 1, 0, 1, 7, 0, 4, Tile::wood_Id, Tile::wood_Id, false);
	generateBox(level, chunkBB, 0, 0, 0, 0, 3, 5, Tile::cobblestone_Id, Tile::cobblestone_Id, false);
	generateBox(level, chunkBB, 8, 0, 0, 8, 3, 5, Tile::cobblestone_Id, Tile::cobblestone_Id, false);
	generateBox(level, chunkBB, 1, 0, 0, 7, 1, 0, Tile::cobblestone_Id, Tile::cobblestone_Id, false);
	generateBox(level, chunkBB, 1, 0, 5, 7, 1, 5, Tile::cobblestone_Id, Tile::cobblestone_Id, false);

	// roof
	generateBox(level, chunkBB, 1, 2, 0, 7, 3, 0, Tile::wood_Id, Tile::wood_Id, false);
	generateBox(level, chunkBB, 1, 2, 5, 7, 3, 5, Tile::wood_Id, Tile::wood_Id, false);
	generateBox(level, chunkBB, 0, 4, 1, 8, 4, 1, Tile::wood_Id, Tile::wood_Id, false);
	generateBox(level, chunkBB, 0, 4, 4, 8, 4, 4, Tile::wood_Id, Tile::wood_Id, false);
	generateBox(level, chunkBB, 0, 5, 2, 8, 5, 3, Tile::wood_Id, Tile::wood_Id, false);
	placeBlock(level, Tile::wood_Id, 0, 0, 4, 2, chunkBB);
	placeBlock(level, Tile::wood_Id, 0, 0, 4, 3, chunkBB);
	placeBlock(level, Tile::wood_Id, 0, 8, 4, 2, chunkBB);
	placeBlock(level, Tile::wood_Id, 0, 8, 4, 3, chunkBB);

	int southStairs = getOrientationData(Tile::stairs_wood_Id, 3);
	int northStairs = getOrientationData(Tile::stairs_wood_Id, 2);
	for (int d = -1; d <= 2; d++)
	{
		for (int w = 0; w <= 8; w++)
		{
			placeBlock(level, Tile::stairs_wood_Id, southStairs, w, 4 + d, d, chunkBB);
			placeBlock(level, Tile::stairs_wood_Id, northStairs, w, 4 + d, 5 - d, chunkBB);
		}
	}

	// windows etc
	placeBlock(level, Tile::treeTrunk_Id, 0, 0, 2, 1, chunkBB);
	placeBlock(level, Tile::treeTrunk_Id, 0, 0, 2, 4, chunkBB);
	placeBlock(level, Tile::treeTrunk_Id, 0, 8, 2, 1, chunkBB);
	placeBlock(level, Tile::treeTrunk_Id, 0, 8, 2, 4, chunkBB);
	placeBlock(level, Tile::thinGlass_Id, 0, 0, 2, 2, chunkBB);
	placeBlock(level, Tile::thinGlass_Id, 0, 0, 2, 3, chunkBB);
	placeBlock(level, Tile::thinGlass_Id, 0, 8, 2, 2, chunkBB);
	placeBlock(level, Tile::thinGlass_Id, 0, 8, 2, 3, chunkBB);
	placeBlock(level, Tile::thinGlass_Id, 0, 2, 2, 5, chunkBB);
	placeBlock(level, Tile::thinGlass_Id, 0, 3, 2, 5, chunkBB);
	placeBlock(level, Tile::thinGlass_Id, 0, 5, 2, 0, chunkBB);
	placeBlock(level, Tile::thinGlass_Id, 0, 6, 2, 5, chunkBB);

	// table
	placeBlock(level, Tile::fence_Id, 0, 2, 1, 3, chunkBB);
	placeBlock(level, Tile::pressurePlate_wood_Id, 0, 2, 2, 3, chunkBB);
	placeBlock(level, Tile::wood_Id, 0, 1, 1, 4, chunkBB);
	placeBlock(level, Tile::stairs_wood_Id, getOrientationData(Tile::stairs_wood_Id, 3), 2, 1, 4, chunkBB);
	placeBlock(level, Tile::stairs_wood_Id, getOrientationData(Tile::stairs_wood_Id, 1), 1, 1, 3, chunkBB);

	// butcher table
	generateBox(level, chunkBB, 5, 0, 1, 7, 0, 3, Tile::stoneSlab_Id, Tile::stoneSlab_Id, false);
	placeBlock(level, Tile::stoneSlab_Id, 0, 6, 1, 1, chunkBB);
	placeBlock(level, Tile::stoneSlab_Id, 0, 6, 1, 2, chunkBB);

	// entrance
	placeBlock(level, 0, 0, 2, 1, 0, chunkBB);
	placeBlock(level, 0, 0, 2, 2, 0, chunkBB);
	placeBlock(level, Tile::torch_Id, 0, 2, 3, 1, chunkBB);
	createDoor(level, chunkBB, random, 2, 1, 0, getOrientationData(Tile::door_wood_Id, 1));
	if (getBlock(level, 2, 0, -1, chunkBB) == 0 && getBlock(level, 2, -1, -1, chunkBB) != 0)
	{
		placeBlock(level, Tile::stairs_stone_Id, getOrientationData(Tile::stairs_stone_Id, 3), 2, 0, -1, chunkBB);
	}

	// pig entrance
	placeBlock(level, 0, 0, 6, 1, 5, chunkBB);
	placeBlock(level, 0, 0, 6, 2, 5, chunkBB);
	placeBlock(level, Tile::torch_Id, 0, 6, 3, 4, chunkBB);
	createDoor(level, chunkBB, random, 6, 1, 5, getOrientationData(Tile::door_wood_Id, 1));

	for (int z = 0; z < 5; z++)
	{
		for (int x = 0; x < width; x++)
		{
			generateAirColumnUp(level, x, height, z, chunkBB);
			fillColumnDown(level, Tile::cobblestone_Id, 0, x, -1, z, chunkBB);
		}
	}


	return true;

}

VillagePieces::TwoRoomHouse::TwoRoomHouse()
{
	// for reflection
}

VillagePieces::TwoRoomHouse::TwoRoomHouse(StartPiece *startPiece, int genDepth, Random *random, BoundingBox *stairsBox, int direction) : VillagePiece(startPiece, genDepth)
{
	heightPosition = -1;	// 4J added initialiser

	orientation = direction;
	boundingBox = stairsBox;
}

VillagePieces::TwoRoomHouse *VillagePieces::TwoRoomHouse::createPiece(StartPiece *startPiece, list<StructurePiece *> *pieces, Random *random, int footX, int footY, int footZ, int direction, int genDepth)
{
	BoundingBox *box = BoundingBox::orientBox(footX, footY, footZ, 0, 0, 0, width, height, depth, direction);

	if (!isOkBox(box, startPiece) || StructurePiece::findCollisionPiece(pieces, box) != nullptr)
	{
		delete box;
		return nullptr;
	}

	return new TwoRoomHouse(startPiece, genDepth, random, box, direction);
}

bool VillagePieces::TwoRoomHouse::postProcess(Level *level, Random *random, BoundingBox *chunkBB)
{
	if (heightPosition < 0)
	{
		heightPosition = getAverageGroundHeight(level, chunkBB);
		if (heightPosition < 0)
		{
			return true;
		}
		boundingBox->move(0, heightPosition - boundingBox->y1 + height - 1, 0);
	}

	// fill inside with air
	generateBox(level, chunkBB, 1, 1, 1, 7, 4, 4, 0, 0, false);
	generateBox(level, chunkBB, 2, 1, 6, 8, 4, 10, 0, 0, false);

	// floor
	generateBox(level, chunkBB, 2, 0, 5, 8, 0, 10, Tile::wood_Id, Tile::wood_Id, false);
	generateBox(level, chunkBB, 1, 0, 1, 7, 0, 4, Tile::wood_Id, Tile::wood_Id, false);
	generateBox(level, chunkBB, 0, 0, 0, 0, 3, 5, Tile::cobblestone_Id, Tile::cobblestone_Id, false);
	generateBox(level, chunkBB, 8, 0, 0, 8, 3, 10, Tile::cobblestone_Id, Tile::cobblestone_Id, false);
	generateBox(level, chunkBB, 1, 0, 0, 7, 2, 0, Tile::cobblestone_Id, Tile::cobblestone_Id, false);
	generateBox(level, chunkBB, 1, 0, 5, 2, 1, 5, Tile::cobblestone_Id, Tile::cobblestone_Id, false);
	generateBox(level, chunkBB, 2, 0, 6, 2, 3, 10, Tile::cobblestone_Id, Tile::cobblestone_Id, false);
	generateBox(level, chunkBB, 3, 0, 10, 7, 3, 10, Tile::cobblestone_Id, Tile::cobblestone_Id, false);

	// room 1 roof
	generateBox(level, chunkBB, 1, 2, 0, 7, 3, 0, Tile::wood_Id, Tile::wood_Id, false);
	generateBox(level, chunkBB, 1, 2, 5, 2, 3, 5, Tile::wood_Id, Tile::wood_Id, false);
	generateBox(level, chunkBB, 0, 4, 1, 8, 4, 1, Tile::wood_Id, Tile::wood_Id, false);
	generateBox(level, chunkBB, 0, 4, 4, 3, 4, 4, Tile::wood_Id, Tile::wood_Id, false);
	generateBox(level, chunkBB, 0, 5, 2, 8, 5, 3, Tile::wood_Id, Tile::wood_Id, false);
	placeBlock(level, Tile::wood_Id, 0, 0, 4, 2, chunkBB);
	placeBlock(level, Tile::wood_Id, 0, 0, 4, 3, chunkBB);
	placeBlock(level, Tile::wood_Id, 0, 8, 4, 2, chunkBB);
	placeBlock(level, Tile::wood_Id, 0, 8, 4, 3, chunkBB);
	placeBlock(level, Tile::wood_Id, 0, 8, 4, 4, chunkBB);

	int southStairs = getOrientationData(Tile::stairs_wood_Id, 3);
	int northStairs = getOrientationData(Tile::stairs_wood_Id, 2);
	for (int d = -1; d <= 2; d++)
	{
		for (int w = 0; w <= 8; w++)
		{
			placeBlock(level, Tile::stairs_wood_Id, southStairs, w, 4 + d, d, chunkBB);
			if ((d > -1 || w <= 1) && (d > 0 || w <= 3) && (d > 1 || w <= 4 || w >= 6)) {
				placeBlock(level, Tile::stairs_wood_Id, northStairs, w, 4 + d, 5 - d, chunkBB);
			}
		}
	}

	// room 2 roof
	generateBox(level, chunkBB, 3, 4, 5, 3, 4, 10, Tile::wood_Id, Tile::wood_Id, false);
	generateBox(level, chunkBB, 7, 4, 2, 7, 4, 10, Tile::wood_Id, Tile::wood_Id, false);
	generateBox(level, chunkBB, 4, 5, 4, 4, 5, 10, Tile::wood_Id, Tile::wood_Id, false);
	generateBox(level, chunkBB, 6, 5, 4, 6, 5, 10, Tile::wood_Id, Tile::wood_Id, false);
	generateBox(level, chunkBB, 5, 6, 3, 5, 6, 10, Tile::wood_Id, Tile::wood_Id, false);
	int westStairs = getOrientationData(Tile::stairs_wood_Id, 0);
	for (int w = 4; w >= 1; w--)
	{
		placeBlock(level, Tile::wood_Id, 0, w, 2 + w, 7 - w, chunkBB);
		for (int d = 8 - w; d <= 10; d++)
		{
			placeBlock(level, Tile::stairs_wood_Id, westStairs, w, 2 + w, d, chunkBB);
		}
	}
	int eastStairs = getOrientationData(Tile::stairs_wood_Id, 1);
	placeBlock(level, Tile::wood_Id, 0, 6, 6, 3, chunkBB);
	placeBlock(level, Tile::wood_Id, 0, 7, 5, 4, chunkBB);
	placeBlock(level, Tile::stairs_wood_Id, eastStairs, 6, 6, 4, chunkBB);
	for (int w = 6; w <= 8; w++)
	{
		for (int d = 5; d <= 10; d++)
		{
			placeBlock(level, Tile::stairs_wood_Id, eastStairs, w, 12 - w, d, chunkBB);
		}
	}

	// windows etc
	placeBlock(level, Tile::treeTrunk_Id, 0, 0, 2, 1, chunkBB);
	placeBlock(level, Tile::treeTrunk_Id, 0, 0, 2, 4, chunkBB);
	placeBlock(level, Tile::thinGlass_Id, 0, 0, 2, 2, chunkBB);
	placeBlock(level, Tile::thinGlass_Id, 0, 0, 2, 3, chunkBB);

	placeBlock(level, Tile::treeTrunk_Id, 0, 4, 2, 0, chunkBB);
	placeBlock(level, Tile::thinGlass_Id, 0, 5, 2, 0, chunkBB);
	placeBlock(level, Tile::treeTrunk_Id, 0, 6, 2, 0, chunkBB);

	placeBlock(level, Tile::treeTrunk_Id, 0, 8, 2, 1, chunkBB);
	placeBlock(level, Tile::thinGlass_Id, 0, 8, 2, 2, chunkBB);
	placeBlock(level, Tile::thinGlass_Id, 0, 8, 2, 3, chunkBB);
	placeBlock(level, Tile::treeTrunk_Id, 0, 8, 2, 4, chunkBB);
	placeBlock(level, Tile::wood_Id, 0, 8, 2, 5, chunkBB);
	placeBlock(level, Tile::treeTrunk_Id, 0, 8, 2, 6, chunkBB);
	placeBlock(level, Tile::thinGlass_Id, 0, 8, 2, 7, chunkBB);
	placeBlock(level, Tile::thinGlass_Id, 0, 8, 2, 8, chunkBB);
	placeBlock(level, Tile::treeTrunk_Id, 0, 8, 2, 9, chunkBB);
	placeBlock(level, Tile::treeTrunk_Id, 0, 2, 2, 6, chunkBB);
	placeBlock(level, Tile::thinGlass_Id, 0, 2, 2, 7, chunkBB);
	placeBlock(level, Tile::thinGlass_Id, 0, 2, 2, 8, chunkBB);
	placeBlock(level, Tile::treeTrunk_Id, 0, 2, 2, 9, chunkBB);

	placeBlock(level, Tile::treeTrunk_Id, 0, 4, 4, 10, chunkBB);
	placeBlock(level, Tile::thinGlass_Id, 0, 5, 4, 10, chunkBB);
	placeBlock(level, Tile::treeTrunk_Id, 0, 6, 4, 10, chunkBB);
	placeBlock(level, Tile::wood_Id, 0, 5, 5, 10, chunkBB);

	// entrance
	placeBlock(level, 0, 0, 2, 1, 0, chunkBB);
	placeBlock(level, 0, 0, 2, 2, 0, chunkBB);
	placeBlock(level, Tile::torch_Id, 0, 2, 3, 1, chunkBB);
	createDoor(level, chunkBB, random, 2, 1, 0, getOrientationData(Tile::door_wood_Id, 1));
	generateBox(level, chunkBB, 1, 0, -1, 3, 2, -1, 0, 0, false);
	if (getBlock(level, 2, 0, -1, chunkBB) == 0 && getBlock(level, 2, -1, -1, chunkBB) != 0) {
		placeBlock(level, Tile::stairs_stone_Id, getOrientationData(Tile::stairs_stone_Id, 3), 2, 0, -1, chunkBB);
	}

	for (int z = 0; z < 5; z++)
	{
		for (int x = 0; x < width; x++)
		{
			generateAirColumnUp(level, x, height, z, chunkBB);
			fillColumnDown(level, Tile::cobblestone_Id, 0, x, -1, z, chunkBB);
		}
	}
	for (int z = 5; z < depth - 1; z++)
	{
		for (int x = 2; x < width; x++)
		{
			generateAirColumnUp(level, x, height, z, chunkBB);
			fillColumnDown(level, Tile::cobblestone_Id, 0, x, -1, z, chunkBB);
		}
	}


	return true;

}

VillagePieces::Smithy::Smithy()
{
	// for reflection
}

VillagePieces::Smithy::Smithy(StartPiece *startPiece, int genDepth, Random *random, BoundingBox *stairsBox, int direction) : VillagePiece(startPiece, genDepth)
{
	hasPlacedChest = false;

	orientation = direction;
	boundingBox = stairsBox;
}

VillagePieces::Smithy *VillagePieces::Smithy::createPiece(StartPiece *startPiece, list<StructurePiece *> *pieces, Random *random, int footX, int footY, int footZ, int direction, int genDepth)
{
	BoundingBox *box = BoundingBox::orientBox(footX, footY, footZ, 0, 0, 0, width, height, depth, direction);

	if (!isOkBox(box, startPiece) || StructurePiece::findCollisionPiece(pieces, box) != nullptr)
	{
		delete box;
		return nullptr;
	}

	return new Smithy(startPiece, genDepth, random, box, direction);
}

bool VillagePieces::Smithy::postProcess(Level *level, Random *random, BoundingBox *chunkBB)
{
	if (heightPosition < 0)
	{
		heightPosition = getAverageGroundHeight(level, chunkBB);
		if (heightPosition < 0)
		{
			return true;
		}
		boundingBox->move(0, heightPosition - boundingBox->y1 + height - 1, 0);
	}

	// fill inside with air
	generateBox(level, chunkBB, 0, 1, 0, 9, 4, 6, 0, 0, false);

	// floor
	generateBox(level, chunkBB, 0, 0, 0, 9, 0, 6, Tile::cobblestone_Id, Tile::cobblestone_Id, false);

	// roof
	generateBox(level, chunkBB, 0, 4, 0, 9, 4, 6, Tile::cobblestone_Id, Tile::cobblestone_Id, false);
	generateBox(level, chunkBB, 0, 5, 0, 9, 5, 6, Tile::stoneSlabHalf_Id, Tile::stoneSlabHalf_Id, false);
	generateBox(level, chunkBB, 1, 5, 1, 8, 5, 5, 0, 0, false);

	// room walls
	generateBox(level, chunkBB, 1, 1, 0, 2, 3, 0, Tile::wood_Id, Tile::wood_Id, false);
	generateBox(level, chunkBB, 0, 1, 0, 0, 4, 0, Tile::treeTrunk_Id, Tile::treeTrunk_Id, false);
	generateBox(level, chunkBB, 3, 1, 0, 3, 4, 0, Tile::treeTrunk_Id, Tile::treeTrunk_Id, false);
	generateBox(level, chunkBB, 0, 1, 6, 0, 4, 6, Tile::treeTrunk_Id, Tile::treeTrunk_Id, false);
	placeBlock(level, Tile::wood_Id, 0, 3, 3, 1, chunkBB);
	generateBox(level, chunkBB, 3, 1, 2, 3, 3, 2, Tile::wood_Id, Tile::wood_Id, false);
	generateBox(level, chunkBB, 4, 1, 3, 5, 3, 3, Tile::wood_Id, Tile::wood_Id, false);
	generateBox(level, chunkBB, 0, 1, 1, 0, 3, 5, Tile::wood_Id, Tile::wood_Id, false);
	generateBox(level, chunkBB, 1, 1, 6, 5, 3, 6, Tile::wood_Id, Tile::wood_Id, false);

	// pillars
	generateBox(level, chunkBB, 5, 1, 0, 5, 3, 0, Tile::fence_Id, Tile::fence_Id, false);
	generateBox(level, chunkBB, 9, 1, 0, 9, 3, 0, Tile::fence_Id, Tile::fence_Id, false);

	// furnace
	generateBox(level, chunkBB, 6, 1, 4, 9, 4, 6, Tile::cobblestone_Id, Tile::cobblestone_Id, false);
	placeBlock(level, Tile::lava_Id, 0, 7, 1, 5, chunkBB);
	placeBlock(level, Tile::lava_Id, 0, 8, 1, 5, chunkBB);
	placeBlock(level, Tile::ironFence_Id, 0, 9, 2, 5, chunkBB);
	placeBlock(level, Tile::ironFence_Id, 0, 9, 2, 4, chunkBB);
	generateBox(level, chunkBB, 7, 2, 4, 8, 2, 5, 0, 0, false);
	placeBlock(level, Tile::cobblestone_Id, 0, 6, 1, 3, chunkBB);
	placeBlock(level, Tile::furnace_Id, 0, 6, 2, 3, chunkBB);
	placeBlock(level, Tile::furnace_Id, 0, 6, 3, 3, chunkBB);
	placeBlock(level, Tile::stoneSlab_Id, 0, 8, 1, 1, chunkBB);

	// windows etc
	placeBlock(level, Tile::thinGlass_Id, 0, 0, 2, 2, chunkBB);
	placeBlock(level, Tile::thinGlass_Id, 0, 0, 2, 4, chunkBB);
	placeBlock(level, Tile::thinGlass_Id, 0, 2, 2, 6, chunkBB);
	placeBlock(level, Tile::thinGlass_Id, 0, 4, 2, 6, chunkBB);

	// table
	placeBlock(level, Tile::fence_Id, 0, 2, 1, 4, chunkBB);
	placeBlock(level, Tile::pressurePlate_wood_Id, 0, 2, 2, 4, chunkBB);
	placeBlock(level, Tile::wood_Id, 0, 1, 1, 5, chunkBB);
	placeBlock(level, Tile::stairs_wood_Id, getOrientationData(Tile::stairs_wood_Id, 3), 2, 1, 5, chunkBB);
	placeBlock(level, Tile::stairs_wood_Id, getOrientationData(Tile::stairs_wood_Id, 1), 1, 1, 4, chunkBB);

	if (!hasPlacedChest)
	{
		int y = getWorldY(1);
		int x = getWorldX(5, 5), z = getWorldZ(5, 5);
		if (chunkBB->isInside(x, y, z))
		{
			hasPlacedChest = true;
			random->nextInt(6); // original roll count draw; loot omitted
			placeBlock(level, Tile::chest_Id, 0, 5, 1, 5, chunkBB);
		}
	}

	// entrance
	for (int x = 6; x <= 8; x++)
	{
		if (getBlock(level, x, 0, -1, chunkBB) == 0 && getBlock(level, x, -1, -1, chunkBB) != 0 )
		{
			placeBlock(level, Tile::stairs_stone_Id, getOrientationData(Tile::stairs_stone_Id, 3), x, 0, -1, chunkBB);
		}
	}

	for (int z = 0; z < depth; z++)
	{
		for (int x = 0; x < width; x++)
		{
			generateAirColumnUp(level, x, height, z, chunkBB);
			fillColumnDown(level, Tile::cobblestone_Id, 0, x, -1, z, chunkBB);
		}
	}


	return true;

}

VillagePieces::Farmland::Farmland()
{
	cropsA = 0;
	cropsB = 0;
	// for reflection
}

VillagePieces::Farmland::Farmland(StartPiece *startPiece, int genDepth, Random *random, BoundingBox *stairsBox, int direction) : VillagePiece(startPiece, genDepth)
{
	orientation = direction;
	boundingBox = stairsBox;

	cropsA = selectCrops(random);
	cropsB = selectCrops(random);
}

int VillagePieces::Farmland::selectCrops(Random *random)
{
	switch (random->nextInt(5))
	{
	default:
		return Tile::wheat_Id;
	case 0:
		return Tile::carrots_Id;
	case 1:
		return Tile::potatoes_Id;
	}
}

VillagePieces::Farmland *VillagePieces::Farmland::createPiece(StartPiece *startPiece, list<StructurePiece *> *pieces, Random *random, int footX, int footY, int footZ, int direction, int genDepth)
{
	BoundingBox *box = BoundingBox::orientBox(footX, footY, footZ, 0, 0, 0, width, height, depth, direction);

	if (!isOkBox(box, startPiece) || StructurePiece::findCollisionPiece(pieces, box) != nullptr)
	{
		delete box;
		return nullptr;
	}

	return new Farmland(startPiece, genDepth, random, box, direction);
}

bool VillagePieces::Farmland::postProcess(Level *level, Random *random, BoundingBox *chunkBB)
{
	if (heightPosition < 0)
	{
		heightPosition = getAverageGroundHeight(level, chunkBB);
		if (heightPosition < 0)
		{
			return true;
		}
		boundingBox->move(0, heightPosition - boundingBox->y1 + height - 1, 0);
	}

	// fill inside with air
	generateBox(level, chunkBB, 0, 1, 0, 6, 4, 8, 0, 0, false);

	// farmlands
	generateBox(level, chunkBB, 1, 0, 1, 2, 0, 7, Tile::farmland_Id, Tile::farmland_Id, false);
	generateBox(level, chunkBB, 4, 0, 1, 5, 0, 7, Tile::farmland_Id, Tile::farmland_Id, false);
	// walkpaths
	generateBox(level, chunkBB, 0, 0, 0, 0, 0, 8, Tile::treeTrunk_Id, Tile::treeTrunk_Id, false);
	generateBox(level, chunkBB, 6, 0, 0, 6, 0, 8, Tile::treeTrunk_Id, Tile::treeTrunk_Id, false);
	generateBox(level, chunkBB, 1, 0, 0, 5, 0, 0, Tile::treeTrunk_Id, Tile::treeTrunk_Id, false);
	generateBox(level, chunkBB, 1, 0, 8, 5, 0, 8, Tile::treeTrunk_Id, Tile::treeTrunk_Id, false);
	// water
	generateBox(level, chunkBB, 3, 0, 1, 3, 0, 7, Tile::water_Id, Tile::water_Id, false);
	// crops
	for (int d = 1; d <= 7; d++)
	{
		placeBlock(level, cropsA, Mth::nextInt(random, 2, 7), 1, 1, d, chunkBB);
		placeBlock(level, cropsA, Mth::nextInt(random, 2, 7), 2, 1, d, chunkBB);
		placeBlock(level, cropsB, Mth::nextInt(random, 2, 7), 4, 1, d, chunkBB);
		placeBlock(level, cropsB, Mth::nextInt(random, 2, 7), 5, 1, d, chunkBB);
	}

	for (int z = 0; z < depth; z++)
	{
		for (int x = 0; x < width; x++)
		{
			generateAirColumnUp(level, x, height, z, chunkBB);
			fillColumnDown(level, Tile::dirt_Id, 0, x, -1, z, chunkBB);
		}
	}

	return true;

}

VillagePieces::DoubleFarmland::DoubleFarmland()
{
	cropsA = 0;
	cropsB = 0;
	cropsC = 0;
	cropsD = 0;
	// for reflection
}

VillagePieces::DoubleFarmland::DoubleFarmland(StartPiece *startPiece, int genDepth, Random *random, BoundingBox *stairsBox, int direction) : VillagePiece(startPiece, genDepth)
{
	heightPosition = -1;	// 4J added initialiser
	orientation = direction;
	boundingBox = stairsBox;

	cropsA = selectCrops(random);
	cropsB = selectCrops(random);
	cropsC = selectCrops(random);
	cropsD = selectCrops(random);
}

int VillagePieces::DoubleFarmland::selectCrops(Random *random)
{
	switch (random->nextInt(5))
	{
	default:
		return Tile::wheat_Id;
	case 0:
		return Tile::carrots_Id;
	case 1:
		return Tile::potatoes_Id;
	}
}

VillagePieces::DoubleFarmland *VillagePieces::DoubleFarmland::createPiece(StartPiece *startPiece, list<StructurePiece *> *pieces, Random *random, int footX, int footY, int footZ, int direction, int genDepth)
{
	BoundingBox *box = BoundingBox::orientBox(footX, footY, footZ, 0, 0, 0, width, height, depth, direction);

	if (!isOkBox(box, startPiece) || StructurePiece::findCollisionPiece(pieces, box) != nullptr)
	{
		delete box;
		return nullptr;
	}

	return new DoubleFarmland(startPiece, genDepth, random, box, direction);
}

bool VillagePieces::DoubleFarmland::postProcess(Level *level, Random *random, BoundingBox *chunkBB)
{
	if (heightPosition < 0)
	{
		heightPosition = getAverageGroundHeight(level, chunkBB);
		if (heightPosition < 0)
		{
			return true;
		}
		boundingBox->move(0, heightPosition - boundingBox->y1 + height - 1, 0);
	}

	// fill inside with air
	generateBox(level, chunkBB, 0, 1, 0, 12, 4, 8, 0, 0, false);

	// farmlands
	generateBox(level, chunkBB, 1, 0, 1, 2, 0, 7, Tile::farmland_Id, Tile::farmland_Id, false);
	generateBox(level, chunkBB, 4, 0, 1, 5, 0, 7, Tile::farmland_Id, Tile::farmland_Id, false);
	generateBox(level, chunkBB, 7, 0, 1, 8, 0, 7, Tile::farmland_Id, Tile::farmland_Id, false);
	generateBox(level, chunkBB, 10, 0, 1, 11, 0, 7, Tile::farmland_Id, Tile::farmland_Id, false);
	// walkpaths
	generateBox(level, chunkBB, 0, 0, 0, 0, 0, 8, Tile::treeTrunk_Id, Tile::treeTrunk_Id, false);
	generateBox(level, chunkBB, 6, 0, 0, 6, 0, 8, Tile::treeTrunk_Id, Tile::treeTrunk_Id, false);
	generateBox(level, chunkBB, 12, 0, 0, 12, 0, 8, Tile::treeTrunk_Id, Tile::treeTrunk_Id, false);
	generateBox(level, chunkBB, 1, 0, 0, 11, 0, 0, Tile::treeTrunk_Id, Tile::treeTrunk_Id, false);
	generateBox(level, chunkBB, 1, 0, 8, 11, 0, 8, Tile::treeTrunk_Id, Tile::treeTrunk_Id, false);
	// water
	generateBox(level, chunkBB, 3, 0, 1, 3, 0, 7, Tile::water_Id, Tile::water_Id, false);
	generateBox(level, chunkBB, 9, 0, 1, 9, 0, 7, Tile::water_Id, Tile::water_Id, false);
	// crops
	for (int d = 1; d <= 7; d++)
	{
		placeBlock(level, cropsA, Mth::nextInt(random, 2, 7), 1, 1, d, chunkBB);
		placeBlock(level, cropsA, Mth::nextInt(random, 2, 7), 2, 1, d, chunkBB);
		placeBlock(level, cropsB, Mth::nextInt(random, 2, 7), 4, 1, d, chunkBB);
		placeBlock(level, cropsB, Mth::nextInt(random, 2, 7), 5, 1, d, chunkBB);
		placeBlock(level, cropsC, Mth::nextInt(random, 2, 7), 7, 1, d, chunkBB);
		placeBlock(level, cropsC, Mth::nextInt(random, 2, 7), 8, 1, d, chunkBB);
		placeBlock(level, cropsD, Mth::nextInt(random, 2, 7), 10, 1, d, chunkBB);
		placeBlock(level, cropsD, Mth::nextInt(random, 2, 7), 11, 1, d, chunkBB);
	}

	for (int z = 0; z < depth; z++)
	{
		for (int x = 0; x < width; x++)
		{
			generateAirColumnUp(level, x, height, z, chunkBB);
			fillColumnDown(level, Tile::dirt_Id, 0, x, -1, z, chunkBB);
		}
	}


	return true;

}

VillagePieces::LightPost::LightPost()
{
	// for reflection
}

VillagePieces::LightPost::LightPost(StartPiece *startPiece, int genDepth, Random *random, BoundingBox *box, int direction) : VillagePiece(startPiece, genDepth)
{
	heightPosition = -1;	// 4J - added initialiser
	orientation = direction;
	boundingBox = box;
}

BoundingBox *VillagePieces::LightPost::findPieceBox(StartPiece *startPiece, list<StructurePiece *> *pieces, Random *random, int footX, int footY, int footZ, int direction)
{
	BoundingBox *box = BoundingBox::orientBox(footX, footY, footZ, 0, 0, 0, width, height, depth, direction);

	if (!isOkBox(box, startPiece) || StructurePiece::findCollisionPiece(pieces, box) != nullptr)
	{
		delete box;
		return nullptr;
	}

	return box;
}

bool VillagePieces::LightPost::postProcess(Level *level, Random *random, BoundingBox *chunkBB)
{
	if (heightPosition < 0)
	{
		heightPosition = getAverageGroundHeight(level, chunkBB);
		if (heightPosition < 0)
		{
			return true;
		}
		boundingBox->move(0, heightPosition - boundingBox->y1 + height - 1, 0);
	}

	// fill with air
	generateBox(level, chunkBB, 0, 0, 0, 2, 3, 1, 0, 0, false);

	// pillar
	placeBlock(level, Tile::fence_Id, 0, 1, 0, 0, chunkBB);
	placeBlock(level, Tile::fence_Id, 0, 1, 1, 0, chunkBB);
	placeBlock(level, Tile::fence_Id, 0, 1, 2, 0, chunkBB);

	// head
	placeBlock(level, Tile::wool_Id, DyePowderItem::WHITE, 1, 3, 0, chunkBB);

	// torches
	placeBlock(level, Tile::torch_Id, 0, 0, 3, 0, chunkBB);
	placeBlock(level, Tile::torch_Id, 0, 1, 3, 1, chunkBB);
	placeBlock(level, Tile::torch_Id, 0, 2, 3, 0, chunkBB);
	placeBlock(level, Tile::torch_Id, 0, 1, 3, -1, chunkBB);

	return true;
}
std::vector<Biome*> VillageFeature::allowedBiomes;
void VillageFeature::staticCtor()
{
	allowedBiomes.push_back( Biome::plains );
	allowedBiomes.push_back( Biome::desert );
}

void VillageFeature::_init(int iXZSize)
{
	villageSizeModifier = 0;
	townSpacing = 32;
	minTownSeparation = 8;

	m_iXZSize=iXZSize;
}

VillageFeature::VillageFeature(int iXZSize)
{
	_init(iXZSize);
}



bool VillageFeature::isFeatureChunk(int x, int z,bool bIsSuperflat)
{
	int townSpacing = this->townSpacing;

	if(!bIsSuperflat
#ifdef _LARGE_WORLDS
		&& level->dimension->getXZSize() < 128
#endif
		)
		{
			townSpacing= 16;// 4J change 32;
		}

		int xx = x;
		int zz = z;
		if (x < 0) x -= townSpacing - 1;
		if (z < 0) z -= townSpacing - 1;

		int xCenterTownChunk = x / townSpacing;
		int zCenterTownChunk = z / townSpacing;
		Random *r = level->getRandomFor(xCenterTownChunk, zCenterTownChunk, 10387312);
		xCenterTownChunk *= townSpacing;
		zCenterTownChunk *= townSpacing;
		xCenterTownChunk += r->nextInt(townSpacing - minTownSeparation);
		zCenterTownChunk += r->nextInt(townSpacing - minTownSeparation);
		x = xx;
		z = zz;

		bool forcePlacement = false;


		if (forcePlacement || (x == xCenterTownChunk && z == zCenterTownChunk) )
		{
			bool biomeOk = level->getBiomeSource()->containsOnly(x * 16 + 8, z * 16 + 8, 0, allowedBiomes);
			if (biomeOk)
			{
				//app.DebugPrintf("Biome ok for Village at %d, %d\n",(x * 16 + 8),(z * 16 + 8));
				return true;
			}
		}

		return false;
}

StructureStart *VillageFeature::createStructureStart(int x, int z)
{
	// 4J added


	return new VillageStart(level, random, x, z, villageSizeModifier, m_iXZSize);
}



VillageFeature::VillageStart::VillageStart(Level *level, Random *random, int chunkX, int chunkZ, int villageSizeModifier, int iXZSize)
{
	valid = false;	// 4J added initialiser
	m_iXZSize=iXZSize;

	list<VillagePieces::PieceWeight *> *pieceSet = VillagePieces::createPieceSet(random, villageSizeModifier);

	VillagePieces::StartPiece *startRoom = new VillagePieces::StartPiece(level->getBiomeSource(), 0, random, (chunkX * 16) + 2, (chunkZ * 16) + 2, pieceSet, villageSizeModifier, level);
	pieces.push_back(startRoom);
	startRoom->addChildren(startRoom, &pieces, random);

	vector<StructurePiece *> *pendingRoads = &startRoom->pendingRoads;
	vector<StructurePiece *> *pendingHouses = &startRoom->pendingHouses;
	while (!pendingRoads->empty() || !pendingHouses->empty())
	{

		// prioritize roads
		if (pendingRoads->empty())
		{
			int pos = random->nextInt(static_cast<int>(pendingHouses->size()));
            auto it = pendingHouses->begin() + pos;
            StructurePiece *structurePiece = *it;
			pendingHouses->erase(it);
			structurePiece->addChildren(startRoom, &pieces, random);
		}
		else
		{
			int pos = random->nextInt(static_cast<int>(pendingRoads->size()));
            auto it = pendingRoads->begin() + pos;
            StructurePiece *structurePiece = *it;
			pendingRoads->erase(it);
			structurePiece->addChildren(startRoom, &pieces, random);
		}
	}

	calculateBoundingBox();

	size_t count = 0;
	for(auto& piece : pieces)
	{
		if ( piece && dynamic_cast<VillagePieces::VillageRoadPiece *>(piece) )
		{
			++count;
		}
	}
	valid = count > 2;
}

bool VillageFeature::VillageStart::isValid()
{
	// 4J-PB - Adding a bounds check to ensure a village isn't over the edge of our world - we end up with half houses in that case
	if((boundingBox->x0<(-m_iXZSize/2)) || (boundingBox->x1>(m_iXZSize/2)) || (boundingBox->z0<(-m_iXZSize/2)) || (boundingBox->z1>(m_iXZSize/2)))
	{
		valid=false;
	}
	return valid;
}
StructureStart::StructureStart()
{
	chunkX = chunkZ = 0;
	boundingBox = nullptr;		// 4J added initialiser
}



StructureStart::~StructureStart()
{
	for(auto& piece : pieces)
	{
		delete piece;
	}
	delete boundingBox;
}

BoundingBox *StructureStart::getBoundingBox()
{
	return boundingBox;
}

list<StructurePiece *> *StructureStart::getPieces()
{
	return &pieces;
}

void StructureStart::calculateBoundingBox()
{
	boundingBox = BoundingBox::getUnknownBox();

	for(auto& piece : pieces)
	{
		boundingBox->expand(piece->getBoundingBox());
	}
}
void DoorItem::place(Level *level, int x, int y, int z, int dir, Tile *tile)
{

	int xra = 0;
	int zra = 0;
	if (dir == 0) zra = +1;
	if (dir == 1) xra = -1;
	if (dir == 2) zra = -1;
	if (dir == 3) xra = +1;


	int solidLeft = (level->isSolidBlockingTile(x - xra, y, z - zra) ? 1 : 0) + (level->isSolidBlockingTile(x - xra, y + 1, z - zra) ? 1 : 0);
	int solidRight = (level->isSolidBlockingTile(x + xra, y, z + zra) ? 1 : 0) + (level->isSolidBlockingTile(x + xra, y + 1, z + zra) ? 1 : 0);

	bool doorLeft = (level->getTile(x - xra, y, z - zra) == tile->id) || (level->getTile(x - xra, y + 1, z - zra) == tile->id);
	bool doorRight = (level->getTile(x + xra, y, z + zra) == tile->id) || (level->getTile(x + xra, y + 1, z + zra) == tile->id);

	bool flip = false;
	if (doorLeft && !doorRight) flip = true;
	else if (solidRight > solidLeft) flip = true;

	level->setTileAndData(x, y, z, tile->id, dir, Tile::UPDATE_CLIENTS);
	level->setTileAndData(x, y + 1, z, tile->id, 8 | (flip ? 1 : 0), Tile::UPDATE_CLIENTS);
	level->updateNeighborsAt(x, y, z, tile->id);
	level->updateNeighborsAt(x, y + 1, z, tile->id);
}
int Mth::nextInt(Random *random, int minInclusive, int maxInclusive)
{
	if (minInclusive >= maxInclusive)
	{
		return minInclusive;
	}
	return random->nextInt(maxInclusive - minInclusive + 1) + minInclusive;
}
Random *Level::getRandomFor(int x, int z, int blend)
{
	int64_t seed = (x * 341873128712l + z * 132897987541l) + getLevelData()->getSeed() + blend;
	random->setSeed(seed);
	return random;
}

}}
