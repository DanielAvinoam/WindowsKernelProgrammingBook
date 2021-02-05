#include <ntifs.h>
#include "PriorityBoosterCommon.h"
#include <ntddk.h>

// Prototypes
NTSTATUS PriorityBoosterCreateClose(_In_ PDEVICE_OBJECT, _In_ PIRP Irp);
NTSTATUS PriorityBoosterDeviceControl(_In_ PDEVICE_OBJECT, _In_ PIRP Irp);
void PriorityBoosterUnload(_In_ PDRIVER_OBJECT DriverObject);

// DriverEntry
extern "C" NTSTATUS
DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath) {
	UNREFERENCED_PARAMETER(RegistryPath);

	// Link major functions 
	DriverObject->DriverUnload = PriorityBoosterUnload;
	DriverObject->MajorFunction[IRP_MJ_CREATE] = PriorityBoosterCreateClose;
	DriverObject->MajorFunction[IRP_MJ_CLOSE] = PriorityBoosterCreateClose;
	DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = PriorityBoosterDeviceControl;

	// Create device object
	UNICODE_STRING devName = RTL_CONSTANT_STRING(L"\\Device\\PriorityBooster");
	PDEVICE_OBJECT DeviceObject;
	NTSTATUS status = IoCreateDevice(
		DriverObject,			// our driver object,
		0,		                // no need for extra bytes,
		&devName,				// the device name,
		FILE_DEVICE_UNKNOWN,    // device type,
		0,						// characteristics flags,
		FALSE,					// not exclusive,
		&DeviceObject			// the resulting pointer
	);
	if (!NT_SUCCESS(status)) {
		KdPrint(("Failed to create device object (0x%08X)\n", status));
		return status;
	}

	// Create symbolic link
	UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\PriorityBooster");
	status = IoCreateSymbolicLink(&symLink, &devName);
	if (!NT_SUCCESS(status)) {
		KdPrint(("Failed to create symbolic link (0x%08X)\n", status));
		IoDeleteDevice(DeviceObject);
		return status;
	}

	return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS PriorityBoosterCreateClose(_In_ PDEVICE_OBJECT, _In_ PIRP Irp) {
	Irp->IoStatus.Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = 0;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS PriorityBoosterDeviceControl(_In_ PDEVICE_OBJECT, _In_ PIRP Irp) {
	// Get our IO_STACK_LOCATION
	auto stack = IoGetCurrentIrpStackLocation(Irp); // IO_STACK_LOCATION*
	auto status = STATUS_SUCCESS;

	// ControlCode switch
	switch (stack->Parameters.DeviceIoControl.IoControlCode) {
	case IOCTL_PRIORITY_BOOSTER_SET_PRIORITY: {
		// Check input validity
		auto len = stack->Parameters.DeviceIoControl.InputBufferLength;
		if (len < sizeof(ThreadData)) {
			status = STATUS_BUFFER_TOO_SMALL;
			break;
		}
		auto data = (ThreadData*)stack->Parameters.DeviceIoControl.Type3InputBuffer;
		if (data == nullptr) {
			status = STATUS_INVALID_PARAMETER;
			break;
		}
		__try {
			if (data->Priority < 1 || data->Priority > 31) {
				status = STATUS_INVALID_PARAMETER;
				break;
			}
			// Convert threadID to KTHREAD and set new priority
			PETHREAD Thread;
			status = PsLookupThreadByThreadId(ULongToHandle(data->ThreadId), &Thread);
			if (!NT_SUCCESS(status))
				break;
			KeSetPriorityThread((PKTHREAD)Thread, data->Priority);
			ObDereferenceObject(Thread);
			KdPrint(("Thread Priority change for %d to %d succeeded!\n",
				data->ThreadId, data->Priority));
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			// something wrong with the buffer
			status = STATUS_ACCESS_VIOLATION;
		}
		break;
	}
	default:
		status = STATUS_INVALID_DEVICE_REQUEST;
		break;
	}
	Irp->IoStatus.Status = status;
	Irp->IoStatus.Information = 0;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return status;
}

void PriorityBoosterUnload(_In_ PDRIVER_OBJECT DriverObject) {
	UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\PriorityBooster");
	// delete symbolic link
	IoDeleteSymbolicLink(&symLink);
	// delete device object
	IoDeleteDevice(DriverObject->DeviceObject);
}