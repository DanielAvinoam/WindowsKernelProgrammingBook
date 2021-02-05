#pragma once

#define PRIORITY_BOOSTER_DEVICE 0x8000

// Define driver's control codes
// DDDD DDDD DDDD DDDD AAFF FFFF FFFF FFMM
#define IOCTL_PRIORITY_BOOSTER_SET_PRIORITY CTL_CODE(PRIORITY_BOOSTER_DEVICE, \
0x800, METHOD_NEITHER, FILE_ANY_ACCESS)

// Define driver's input data
struct ThreadData {
	ULONG ThreadId;
	int Priority;
};
