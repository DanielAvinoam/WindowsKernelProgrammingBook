#pragma once
#include "pch.h"

enum class ItemType : short {
	None,
	ProcessCreate,
	ProcessExit,
	ThreadCreate,
	ThreadExit,
	ImageLoad
};

struct ItemHeader {
	ItemType Type;
	USHORT Size;
	LARGE_INTEGER Time;
};

struct ProcessExitInfo : ItemHeader {
	ULONG ProcessId;
};

struct ProcessCreateInfo : ItemHeader {
	ULONG ProcessId;
	ULONG ParentProcessId;
	USHORT CommandLineLength;
	USHORT CommandLineOffset;
	USHORT ImageFileLength;
	USHORT ImageFileOffset;
};

struct ThreadCreateExitInfo : ItemHeader {
	ULONG ThreadId;
	ULONG ProcessId;
};

struct ImageLoad: ItemHeader {
	ULONG ProcessId;
	PVOID ImageBase;
	USHORT FullImageLength;
	USHORT FullImageOffset;
};