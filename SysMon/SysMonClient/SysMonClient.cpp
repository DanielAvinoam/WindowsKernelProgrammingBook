#include "pch.h"
#include "inttypes.h"
#include "..//SysMon/SysMonCommon.h"

void DisplayTime(const LARGE_INTEGER& time) {
	SYSTEMTIME st;
	::FileTimeToSystemTime((FILETIME*)&time, &st);
	printf("%02d:%02d:%02d.%03d: ",
		st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
}

void DisplayInfo(BYTE* buffer, DWORD size) {
	auto count = size;
	while (count > 0) {
		auto header = (ItemHeader*)buffer;
		DisplayTime(header->Time);
		switch (header->Type) {
		case ItemType::ProcessExit:
		{
			auto info = (ProcessExitInfo*)buffer;
			printf("Process %d Exited\n", info->ProcessId);
			break;
		}
		case ItemType::ProcessCreate:
		{
			auto info = (ProcessCreateInfo*)buffer;
			std::wstring commandline((WCHAR*)(buffer + info->CommandLineOffset), info->CommandLineLength);
			std::wstring imageFile((WCHAR*)(buffer + info->ImageFileOffset), info->ImageFileLength);
			printf("Process %d Created. Command line: %ws. Image File: %ws\n", info->ProcessId,
				commandline.c_str(),
				imageFile .c_str());
			break;
		}
		case ItemType::ThreadCreate:
		{
			auto info = (ThreadCreateExitInfo*)buffer;
			printf("Thread %d Created in process %d\n",
				info->ThreadId, info->ProcessId);
			break;
		}
		case ItemType::ThreadExit:
		{
			auto info = (ThreadCreateExitInfo*)buffer;
			printf("Thread %d Exited from process %d\n",
				info->ThreadId, info->ProcessId);
			break;
		}
		case ItemType::ImageLoad: 
		{
			auto info = (ImageLoad*)buffer;
			std::wstring fullImageName((WCHAR*)(buffer + info->FullImageOffset), info->FullImageLength);
			printf("Process %d loaded an image at Image Base: 0x%" PRIx64 ". Image path: %ws\n", info->ProcessId,
				info->ImageBase,
				fullImageName.c_str());
		}
		default:
			break;
		}
		buffer += header->Size;
		count -= header->Size;
	}
}

int main() {
	auto hFile = ::CreateFile(L"\\\\.\\SysMon", GENERIC_READ, 0,
		nullptr, OPEN_EXISTING, 0, nullptr);
	if (hFile == INVALID_HANDLE_VALUE)
		return 0;
	BYTE buffer[1 << 16]; // 64KB buffer
	while (true) {
		DWORD bytes;
		if (!::ReadFile(hFile, buffer, sizeof(buffer), &bytes, nullptr))
			return 0;
		if (bytes != 0)
			DisplayInfo(buffer, bytes);
		::Sleep(500);
	}
	return 1;
}
