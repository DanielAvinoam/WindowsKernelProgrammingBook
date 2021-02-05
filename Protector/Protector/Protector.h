#pragma once
#include "pch.h"
#include "FastMutex.h"
#include "ProtectorCommon.h"
#include "AutoLock.h"

#define DRIVER_PREFIX "Protector: "
#define DRIVER_TAG 'ptcr'

struct Globals {
	LIST_ENTRY ItemsHead;
	int ItemCount;
	FastMutex Mutex;
};

struct FullItem {
	LIST_ENTRY Entry;
	BlockedExecutable blockedExecutable;
};