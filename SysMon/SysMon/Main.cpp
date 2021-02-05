#include "pch.h"
#include "SysMon.h"
#include "SysMonCommon.h"
#include "AutoLock.h"

Globals g_Globals;
DRIVER_UNLOAD SysMonUnload;
DRIVER_DISPATCH SysMonCreateClose, SysMonRead;
void PushItem(LIST_ENTRY* entry);
void OnProcessNotify(PEPROCESS Process, HANDLE ProcessId, PPS_CREATE_NOTIFY_INFO CreateInfo);
void OnThreadNotify(HANDLE ProcessId, HANDLE ThreadId, BOOLEAN Create);
void OnImageLoadNotify(PUNICODE_STRING FullImageName, HANDLE ProcessId, PIMAGE_INFO ImageInfo);

extern "C" NTSTATUS
DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING) {
	auto status = STATUS_SUCCESS;

	InitializeListHead(&g_Globals.ItemsHead);
	g_Globals.Mutex.Init();

	PDEVICE_OBJECT DeviceObject = nullptr;
	UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\sysmon");
	bool symLinkCreated = false;

	do 
	{
		UNICODE_STRING devName = RTL_CONSTANT_STRING(L"\\Device\\sysmon");
		status = IoCreateDevice(DriverObject, 0, &devName,
			FILE_DEVICE_UNKNOWN, 0, TRUE, &DeviceObject);
		if (!NT_SUCCESS(status)) {
			KdPrint((DRIVER_PREFIX "failed to create device (0x%08X)\n",
				status));
			break;
		}
		DeviceObject->Flags |= DO_DIRECT_IO;

		status = IoCreateSymbolicLink(&symLink, &devName);
		if (!NT_SUCCESS(status)) {
			KdPrint((DRIVER_PREFIX "failed to create sym link (0x%08X)\n",
				status));
			break;
		}
		symLinkCreated = true;

		// register for process notifications
		status = PsSetCreateProcessNotifyRoutineEx(OnProcessNotify, FALSE);
		if (!NT_SUCCESS(status)) {
			KdPrint((DRIVER_PREFIX "failed to register process callback (0x%08X)\n", status));
			break;
		}

		// register for thread notifications
		status = PsSetCreateThreadNotifyRoutine(OnThreadNotify);
		if (!NT_SUCCESS(status)) {
			KdPrint((DRIVER_PREFIX "failed to set thread callbacks (status=%08X)\n", status));
			break;
		}

		// register for image load notifications
		status = PsSetLoadImageNotifyRoutine(OnImageLoadNotify);
		if (!NT_SUCCESS(status)) {
			KdPrint((DRIVER_PREFIX "failed to set image load callbacks (status=%08X)\n", status));
			break;
		}

	} while (false);

	if (!NT_SUCCESS(status)) {
		if (symLinkCreated)
			IoDeleteSymbolicLink(&symLink);
		if (DeviceObject)
			IoDeleteDevice(DeviceObject);
	}
	DriverObject->DriverUnload = SysMonUnload;
	DriverObject->MajorFunction[IRP_MJ_CREATE] = DriverObject->MajorFunction[IRP_MJ_CLOSE] = SysMonCreateClose;
	DriverObject->MajorFunction[IRP_MJ_READ] = SysMonRead;
	return status;
}

NTSTATUS SysMonCreateClose(PDEVICE_OBJECT, PIRP Irp) {
	Irp->IoStatus.Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = 0;
	IoCompleteRequest(Irp, 0);
	return STATUS_SUCCESS;
}

NTSTATUS SysMonRead(PDEVICE_OBJECT, PIRP Irp) {
	auto stack = IoGetCurrentIrpStackLocation(Irp);
	auto len = stack->Parameters.Read.Length;
	auto status = STATUS_SUCCESS;
	auto count = 0;
	NT_ASSERT(Irp->MdlAddress); // we're using Direct I/O
	auto buffer = (UCHAR*)MmGetSystemAddressForMdlSafe(Irp->MdlAddress,
		NormalPagePriority);
	if (!buffer) {
		status = STATUS_INSUFFICIENT_RESOURCES;
	}
	else {
		//AutoLock<FastMutex> lock(g_Globals.Mutex);
		AutoLock lock(g_Globals.Mutex); // C++ 17

		while (true) {
			if (IsListEmpty(&g_Globals.ItemsHead)) // can also check g_Globals.ItemCount
				break;
			auto entry = RemoveHeadList(&g_Globals.ItemsHead);
			auto info = CONTAINING_RECORD(entry, FullItem<ItemHeader>, Entry);
			auto size = info->Data.Size;
			if (len < size) {
				// user's buffer is full, insert item back
				InsertHeadList(&g_Globals.ItemsHead, entry);
				break;
			}
			g_Globals.ItemCount--;
			::memcpy(buffer, &info->Data, size);
			len -= size;
			buffer += size;
			count += size;
			// free data after copy
			ExFreePool(info);
		}
	}
	Irp->IoStatus.Status = status;
	Irp->IoStatus.Information = count;
	IoCompleteRequest(Irp, 0);
	return status;
}

void SysMonUnload(PDRIVER_OBJECT DriverObject) {
	// unregister all notifications
	PsSetCreateProcessNotifyRoutineEx(OnProcessNotify, TRUE);
	PsRemoveCreateThreadNotifyRoutine(OnThreadNotify);
	PsRemoveLoadImageNotifyRoutine(OnImageLoadNotify);

	UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\sysmon");
	IoDeleteSymbolicLink(&symLink);
	IoDeleteDevice(DriverObject->DeviceObject);

	// free remaining items
	while (!IsListEmpty(&g_Globals.ItemsHead)) {
		auto entry = RemoveHeadList(&g_Globals.ItemsHead);
		ExFreePool(CONTAINING_RECORD(entry, FullItem<ItemHeader>, Entry));
		//ExFreePool(RemoveHeadList(&g_Globals.ItemsHead)); // ???
	}
}

void OnProcessNotify(PEPROCESS Process, HANDLE ProcessId, PPS_CREATE_NOTIFY_INFO CreateInfo) {
	UNREFERENCED_PARAMETER(Process);
	if (CreateInfo){
		// process create
		USHORT allocSize = sizeof(FullItem<ProcessCreateInfo>);
		USHORT commandLineSize = 0;
		USHORT imageFileSize = 0;
		if (CreateInfo->CommandLine) {
			commandLineSize = CreateInfo->CommandLine->Length;
			allocSize += commandLineSize;
		}
		if (CreateInfo->ImageFileName) {
			imageFileSize = CreateInfo->ImageFileName->Length;
			allocSize += imageFileSize;
		}
		auto info = (FullItem<ProcessCreateInfo>*)ExAllocatePoolWithTag(PagedPool,
			allocSize, DRIVER_TAG);
		if (info == nullptr) {
			KdPrint((DRIVER_PREFIX "failed allocation\n"));
			return;
		}
		auto& item = info->Data;
		KeQuerySystemTimePrecise(&item.Time);
		item.Type = ItemType::ProcessCreate;
		item.Size = sizeof(ProcessCreateInfo) + commandLineSize + imageFileSize;
		item.ProcessId = HandleToULong(ProcessId);
		item.ParentProcessId = HandleToULong(CreateInfo->ParentProcessId);

		if (commandLineSize > 0) {
			::memcpy((UCHAR*)&item + sizeof(item), CreateInfo->CommandLine->Buffer,
				commandLineSize);
			item.CommandLineLength = commandLineSize / sizeof(WCHAR); // length in WCHARs
			item.CommandLineOffset = sizeof(item);
		}
		else item.CommandLineLength = 0;
		if (imageFileSize > 0) {
			::memcpy((UCHAR*)&item + sizeof(item) + item.CommandLineLength, CreateInfo->ImageFileName->Buffer, 
				imageFileSize);
			item.ImageFileLength = imageFileSize / sizeof(WCHAR); // length in WCHARs
			item.ImageFileOffset = sizeof(item) + item.CommandLineLength;
		}
		else item.ImageFileLength = 0;

		PushItem(&info->Entry);
	}
	else {
		// process exit
		auto info = (FullItem<ProcessExitInfo>*)ExAllocatePoolWithTag(PagedPool,
			sizeof(FullItem<ProcessExitInfo>), DRIVER_TAG);
		if (info == nullptr) {
			KdPrint((DRIVER_PREFIX "failed allocation\n"));
			return;
		}
		auto& item = info->Data;
		KeQuerySystemTimePrecise(&item.Time);
		item.Type = ItemType::ProcessExit;
		item.ProcessId = HandleToULong(ProcessId);
		item.Size = sizeof(ProcessExitInfo);

		PushItem(&info->Entry);
	}
}

void OnThreadNotify(HANDLE ProcessId, HANDLE ThreadId, BOOLEAN Create) {
	auto size = sizeof(FullItem<ThreadCreateExitInfo>);
	auto info = (FullItem<ThreadCreateExitInfo>*)ExAllocatePoolWithTag(PagedPool,
		size, DRIVER_TAG);
	if (info == nullptr) {
		KdPrint((DRIVER_PREFIX "Failed to allocate memory\n"));
		return;
	}
	auto& item = info->Data;
	KeQuerySystemTimePrecise(&item.Time);
	item.Size = sizeof(item);
	item.Type = Create ? ItemType::ThreadCreate : ItemType::ThreadExit;
	item.ProcessId = HandleToULong(ProcessId);
	item.ThreadId = HandleToULong(ThreadId);
	PushItem(&info->Entry);
}

void OnImageLoadNotify(PUNICODE_STRING FullImageName, HANDLE ProcessId, PIMAGE_INFO ImageInfo) {
	auto processId = HandleToULong(ProcessId);
	if (processId == 0)
		// Kernel Load
		return;
	USHORT allocSize = sizeof(FullItem<ImageLoad>);
	USHORT imageNameSize = 0;
	if (FullImageName) {
		imageNameSize = FullImageName->Length;
		allocSize += imageNameSize;
	}
	auto info = (FullItem<ImageLoad>*)ExAllocatePoolWithTag(PagedPool,
		allocSize, DRIVER_TAG);
	if (info == nullptr) {
		KdPrint((DRIVER_PREFIX "failed allocation\n"));
		return;
	}
	auto& item = info->Data;
	KeQuerySystemTimePrecise(&item.Time);
	item.Size = sizeof(ImageLoad) + imageNameSize;
	item.Type = ItemType::ImageLoad;
	item.ProcessId = processId;
	item.ImageBase = ImageInfo->ImageBase;
	if (imageNameSize > 0) {
		::memcpy((UCHAR*)&item + sizeof(item), FullImageName->Buffer,
			imageNameSize);
		item.FullImageLength = imageNameSize / sizeof(WCHAR);
		item.FullImageOffset = sizeof(item);
	}
	else item.FullImageOffset = 0;
	PushItem(&info->Entry);
}

void PushItem(LIST_ENTRY* entry) {
	AutoLock<FastMutex> lock(g_Globals.Mutex);
	if (g_Globals.ItemCount > 1024) {
		// too many items, remove oldest one
		auto head = RemoveHeadList(&g_Globals.ItemsHead);
		g_Globals.ItemCount--;
		auto item = CONTAINING_RECORD(head, FullItem<ItemHeader>, Entry);
		ExFreePool(item);
	}
	InsertTailList(&g_Globals.ItemsHead, entry);
	g_Globals.ItemCount++;
}