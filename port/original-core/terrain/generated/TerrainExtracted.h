#pragma once
#define LEVEL_WIDTH_CLASSIC 54
#define LEVEL_WIDTH_SMALL 64
#define LEVEL_WIDTH_MEDIUM (3*64)

namespace original_terrain_detail {
class Mth : public ::Mth {
public:
static float sqrt(float x);
static float sqrt(double x);
};
class Tile {
public:
static const int stone_Id = 1;
static const int grass_Id = 2;
static const int dirt_Id = 3;
static const int unbreakable_Id = 7;
static const int calmWater_Id = 9;
static const int sand_Id = 12;
static const int sandStone_Id = 24;
static const int ice_Id = 79;
};
} // namespace original_terrain_detail
