	for (int i = 0; i < cactusCount; i++)
	{
		int x = xo + random->nextInt(16) + 8;
		int y = random->nextInt(Level::genDepth);
		int z = zo + random->nextInt(16) + 8;
		cactusFeature->place(level, random, x, y, z);
	}