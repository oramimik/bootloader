#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>

#include "init/boot.h"
#include "lib/setup.h"

void EFIAPI init_services(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
	gST = SystemTable;
	gBS = SystemTable->BootServices;
}

EFI_STATUS EFIAPI UefiMain(IN EFI_HANDLE ImageHandle,
			   IN EFI_SYSTEM_TABLE *SystemTable)
{
	EFI_STATUS status;
	EFI_FILE_PROTOCOL *vmm_bin;
	struct vmm_context *context;
	// UINTN vmm_binary_size;
	UINTN map_key;

	init_services(ImageHandle, SystemTable);

	context = create_vmm_context();
	Print(L"context addr = %lx\r\n", (UINT64 *)context);

	context->init(context);

	vmm_bin = context->open_vmm(ImageHandle);
	context->read_vmm(context, vmm_bin);
	//vmm_binary_size = context->vmm_size(
	//	vmm_bin); // save vmm size for identity mapping

	context->start(context, 1536 * 4096); /* only vmm 6mb, actaully 8mb */

	vmm_bin->Close(vmm_bin);

	gBS->AllocatePool(EfiRuntimeServicesData,
			  sizeof(EFI_MEMORY_DESCRIPTOR) * 8,
			  (void **)&context->memory_desc);

	do {
		status = gBS->GetMemoryMap(&context->map_size,
					   context->memory_desc, &map_key,
					   &context->desc_size, NULL);
		gBS->FreePool((void *)context->memory_desc);
		gBS->AllocatePool(EfiRuntimeServicesData, context->map_size,
				  (void **)&context->memory_desc);
		if (EFI_ERROR(status)) {
			Print(L"failed get memory map = %r\r\n", status);
			context->map_size += context->desc_size * 8;
		}
	} while (status == EFI_BUFFER_TOO_SMALL);

	gBS->ExitBootServices(ImageHandle, map_key);

	// enter vmm

	return status;
}