	caveFeature->apply(this, level, xOffs, zOffs, blocks);
	// 4J Stu Design Change - 1.8 gen goes stronghold, mineshaft, village, canyon
	// this changed in 1.2 to canyon, mineshaft, village, stronghold
	// This change makes sense as it stops canyons running through other structures
	canyonFeature->apply(this, level, xOffs, zOffs, blocks);
