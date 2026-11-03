	for (int i = 0; i < flowerCount; i++)
	{
		int x = xo + random->nextInt(16) + 8;
		int y = random->nextInt(Level::genDepth);
		int z = zo + random->nextInt(16) + 8;
		yellowFlowerFeature->place(level, random, x, y, z);

		if (random->nextInt(4) == 0)
		{
			x = xo + random->nextInt(16) + 8;
			y = random->nextInt(Level::genDepth);
			z = zo + random->nextInt(16) + 8;
			roseFlowerFeature->place(level, random, x, y, z);
		}
	}

	for (int i = 0; i < grassCount; i++)
	{
		//int grassType = TallGrass::TALL_GRASS;

		int x = xo + random->nextInt(16) + 8;
		int y = random->nextInt(Level::genDepth);
		int z = zo + random->nextInt(16) + 8;
		MemSect(50);
		Feature *grassFeature = biome->getGrassFeature(random);
		MemSect(0);
		grassFeature->place(level, random, x, y, z);
		delete grassFeature;
	}
