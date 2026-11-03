	m_XZSize = level->getLevelData()->getXZSize();
#ifdef _LARGE_WORLDS
	level->getLevelData()->getMoatFlags(&m_classicEdgeMoat, &m_smallEdgeMoat, &m_mediumEdgeMoat);
#endif
	this->level = level;

	random = new Random(seed);
	pprandom = new Random(seed);	// 4J - added, so that we can have a separate random for doing post-processing in parallel with creation
	lperlinNoise1 = new PerlinNoise(random, 16);
	lperlinNoise2 = new PerlinNoise(random, 16);
	perlinNoise1 = new PerlinNoise(random, 8);
	perlinNoise3 = new PerlinNoise(random, 4);

	scaleNoise = new PerlinNoise(random, 10);
	depthNoise = new PerlinNoise(random, 16);

	if (FLOATING_ISLANDS)
	{
		floatingIslandScale = new PerlinNoise(random, 10);
		floatingIslandNoise = new PerlinNoise(random, 16);
	}
	else
	{
		floatingIslandScale = nullptr;
		floatingIslandNoise = nullptr;
	}

	forestNoise = new PerlinNoise(random, 8);
