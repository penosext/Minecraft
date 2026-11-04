	static const int UPDATE_NEIGHBORS = (1 << 0);
	// clients: send tile update over network connections
	static const int UPDATE_CLIENTS = (1 << 1);
	// invisible: this update is invisible, so don't rebuild graphics
	static const int UPDATE_INVISIBLE = (1 << 2);
	// clients: send tile update over network connections
	static const int UPDATE_INVISIBLE_NO_LIGHT = (1 << 3) | UPDATE_INVISIBLE;

	static const int UPDATE_NONE = UPDATE_INVISIBLE;
	static const int UPDATE_ALL = UPDATE_NEIGHBORS | UPDATE_CLIENTS;

