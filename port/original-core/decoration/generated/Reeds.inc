	for (int i = 0; i < reedsCount; i++)
	{
		int x = xo + random->nextInt(16) + 8;
		int z = zo + random->nextInt(16) + 8;
		int y = random->nextInt(Level::genDepth);
		reedsFeature->place(level, random, x, y, z);
	}

	for (int i = 0; i < 10; i++)
	{
		int x = xo + random->nextInt(16) + 8;
		int y = random->nextInt(Level::genDepth);
		int z = zo + random->nextInt(16) + 8;
		reedsFeature->place(level, random, x, y, z);
	}

