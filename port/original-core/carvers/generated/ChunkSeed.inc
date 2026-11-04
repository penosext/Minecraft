	pprandom->setSeed(level->getSeed());
	int64_t xScale = pprandom->nextLong() / 2 * 2 + 1;
	int64_t zScale = pprandom->nextLong() / 2 * 2 + 1;
	pprandom->setSeed(((xt * xScale) + (zt * zScale)) ^ level->getSeed());
