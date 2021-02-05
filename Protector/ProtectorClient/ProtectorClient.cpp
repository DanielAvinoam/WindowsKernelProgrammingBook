#include "pch.h"
#include "..//Protector/ProtectorCommon.h"

using namespace std;

HANDLE protectorHandle;

bool SetBlockedPath(wstring filePath) {
	USHORT pathSize = filePath.length() * sizeof(WCHAR); // size in bytes
	USHORT allocSize = sizeof(BlockedExecutable) + pathSize;

	auto blockedExecutable = (BlockedExecutable*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, allocSize);
	if (!blockedExecutable) return false;

	blockedExecutable->PathSize = pathSize;
	blockedExecutable->PathOffset = (USHORT)sizeof(BlockedExecutable);

	if (!::memcpy((BYTE*)blockedExecutable + blockedExecutable->PathOffset, filePath.c_str(), pathSize)) return false;

	ULONG bytesWritten;
	if (!::WriteFile(protectorHandle, blockedExecutable, allocSize, &bytesWritten, NULL)) return false;
	
	// Test
	STARTUPINFO info = { sizeof(info) };
	PROCESS_INFORMATION processInfo;
	return ::CreateProcess(filePath.c_str(), NULL, NULL, NULL, TRUE, 0, NULL, NULL, &info, &processInfo) == ERROR_ACCESS_DENIED;
}

int wmain(int argc, wchar_t* argv[]) {
	if (argc != 2) {
		printf("Usage: ProtectorClient.exe [Blocked Image Path]");
		return 0;
	}

	ifstream file(argv[1]);
	if (!file.good()) { 
		printf("Invalid file path."); 
		return 0;
	}

	protectorHandle = ::CreateFile(L"\\\\.\\protector", GENERIC_WRITE, 0,
		nullptr, OPEN_EXISTING, 0, nullptr);
	if (protectorHandle == INVALID_HANDLE_VALUE)
		return 0;

	wstring filePath(argv[1]);
	bool success = SetBlockedPath(filePath);
	if (success) printf("File blocked successfuly.");
	else printf("Error blocking file.");
	return success;
}