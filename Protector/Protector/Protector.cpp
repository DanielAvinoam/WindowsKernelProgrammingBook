#include "pch.h"
#include "Protector.h"

Globals g_Globals;
DRIVER_UNLOAD ProtectorUnload;
DRIVER_DISPATCH ProtectorCreateClose, ProtectorWrite;
void OnProcessNotify(PEPROCESS Process, HANDLE ProcessId, PPS_CREATE_NOTIFY_INFO CreateInfo);
void PushItem(LIST_ENTRY* entry);

extern "C" NTSTATUS
DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING) {
	auto status = STATUS_SUCCESS;

	// Init Globals
	g_Globals.Mutex.Init();
	InitializeListHead(&g_Globals.ItemsHead);

	PDEVICE_OBJECT DeviceObject = nullptr;
	UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\protector");
	bool symLinkCreated = false;
	do
	{
		UNICODE_STRING devName = RTL_CONSTANT_STRING(L"\\Device\\protector");
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

		// Register for process creation notifications
		status = PsSetCreateProcessNotifyRoutineEx(OnProcessNotify, FALSE);
		if (!NT_SUCCESS(status)) {
			KdPrint((DRIVER_PREFIX "failed to register process callback (0x%08X)\n", status));
			break;
		}
		symLinkCreated = true;
	} while (false);

	if (!NT_SUCCESS(status)) {
		if (symLinkCreated)
			IoDeleteSymbolicLink(&symLink);
		if (DeviceObject)
			IoDeleteDevice(DeviceObject);
	}
	DriverObject->DriverUnload = ProtectorUnload;
	DriverObject->MajorFunction[IRP_MJ_CREATE] = DriverObject->MajorFunction[IRP_MJ_CLOSE] = ProtectorCreateClose;
	DriverObject->MajorFunction[IRP_MJ_WRITE] = ProtectorWrite;
	return status;
}

void ProtectorUnload(PDRIVER_OBJECT DriverObject) {
	PsSetCreateProcessNotifyRoutineEx(OnProcessNotify, TRUE);
	UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\protector");
	IoDeleteSymbolicLink(&symLink);
	IoDeleteDevice(DriverObject->DeviceObject);

	// free remaining items
	while (!IsListEmpty(&g_Globals.ItemsHead)) {
		auto entry = RemoveHeadList(&g_Globals.ItemsHead);
		ExFreePool(CONTAINING_RECORD(entry, FullItem, Entry));
		//ExFreePool(RemoveHeadList(&g_Globals.ItemsHead)); // ???
	}
}

NTSTATUS ProtectorCreateClose(PDEVICE_OBJECT, PIRP Irp) {
	Irp->IoStatus.Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = 0;
	IoCompleteRequest(Irp, 0);
	return STATUS_SUCCESS;
}

NTSTATUS ProtectorWrite(PDEVICE_OBJECT, PIRP Irp) {
	auto status = STATUS_SUCCESS;

	NT_ASSERT(Irp->MdlAddress); // we're using Direct I/O
	auto buffer = (BlockedExecutable*)::MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority);
	if (!buffer) status = STATUS_INSUFFICIENT_RESOURCES;
	else {
		USHORT pathSize = buffer->PathSize;
		USHORT allocSize = sizeof(BlockedExecutable) + pathSize;
		auto info = (BlockedExecutable*)ExAllocatePoolWithTag(PagedPool, allocSize, DRIVER_TAG);
		if (info == nullptr) status = STATUS_INSUFFICIENT_RESOURCES;
		else {
			::memset(info, 0, allocSize);
			info->PathSize = buffer->PathSize;
			info->PathOffset = buffer->PathOffset;
			::memcpy((UCHAR*)&info + info->PathOffset, buffer + buffer->PathOffset, pathSize);
			::memset(info, 0, 1);
		}

		/*auto allocSize = sizeof(FullItem) + buffer->PathSize;
		auto info = (FullItem*)::ExAllocatePoolWithTag(PagedPool, allocSize, DRIVER_TAG);
		if (info == nullptr) status = STATUS_INSUFFICIENT_RESOURCES;
		else {
			::memset(info, 0, allocSize);
			BlockedExecutable item = info->blockedExecutable;	
			item.PathSize = buffer->PathSize;
			item.PathOffset = buffer->PathOffset;
			::memmove((UCHAR*)&item + item.PathOffset, buffer + buffer->PathOffset, buffer->PathSize);
			PushItem(&info->Entry);
		}*/
	}
	Irp->IoStatus.Status = status;
	Irp->IoStatus.Information = 0;
	IoCompleteRequest(Irp, 0);
	return status;
}

void OnProcessNotify(PEPROCESS Process, HANDLE ProcessId, PPS_CREATE_NOTIFY_INFO CreateInfo) {
	UNREFERENCED_PARAMETER(Process);
	UNREFERENCED_PARAMETER(ProcessId);

	if (g_Globals.ItemCount == 0) return;

	if (!CreateInfo) return;

	if (!CreateInfo->FileOpenNameAvailable) return;

	AutoLock lock(g_Globals.Mutex);

	PLIST_ENTRY pEntry = g_Globals.ItemsHead.Flink;
	while(pEntry != &g_Globals.ItemsHead) {
		auto info = CONTAINING_RECORD(pEntry, FullItem, Entry);
		auto& item = info->blockedExecutable;
		auto result = ::wcsstr((WCHAR*)CreateInfo->ImageFileName->Buffer, (WCHAR*)&item + item.PathOffset);

		// Match found, block image load
		if (result != NULL) CreateInfo->CreationStatus = STATUS_ACCESS_DENIED;

		KdPrint((DRIVER_PREFIX "%wZ was blocked from executing.\n", CreateInfo->ImageFileName));
		pEntry = pEntry->Flink;
	}
}

void PushItem(LIST_ENTRY* entry) {
	AutoLock lock(g_Globals.Mutex);
	if (g_Globals.ItemCount > 1024) {
		// too many items, remove oldest one
		auto head = RemoveHeadList(&g_Globals.ItemsHead);
		g_Globals.ItemCount--;
		auto item = CONTAINING_RECORD(head, FullItem, Entry);
		ExFreePool(item);
	}
	InsertTailList(&g_Globals.ItemsHead, entry);
	g_Globals.ItemCount++;
}