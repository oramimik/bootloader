#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Uefi.h>

#include "init/boot.h"
#include "lib/setup.h"

UINT64 EFIAPI get_gdtr_offset()
{
	return offset_of(struct vmm_context, gdtr);
}

UINT64 EFIAPI get_pg_table_offset()
{
	return offset_of(struct vmm_context, pml4);
}

void EFIAPI init_services(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
	gST = SystemTable;
	gBS = SystemTable->BootServices;
}

EFI_STATUS EFIAPI UefiMain(IN EFI_HANDLE ImageHandle,
			   IN EFI_SYSTEM_TABLE *SystemTable)
{
	EFI_FILE_PROTOCOL *vmm_bin;
	struct vmm_context *context;
	struct uefi_state_struct *uefi_state;

	DEBUG((DEBUG_INFO, "this is vmm bootloader entry\n"));

	init_services(ImageHandle, SystemTable);

	uefi_state = create_uefi_state();
	uefi_state->start(uefi_state);
	context = create_vmm_context();
	Print(L"context addr = %lx\r\n", (UINT64 *)context);

	context->init(context);

	vmm_bin = context->open_vmm(ImageHandle);
	context->read_vmm(context, vmm_bin);

	context->start(context); /* only vmm size is 6mb, total = 8mb */

	vmm_bin->Close(vmm_bin);

	Print(L"now enter to asm\r\n");
	enter_vmm(context, uefi_state);

	// i had mistake used to "ExitBootServices"
	gBS->Exit(ImageHandle, 1, 0, NULL);

	return EFI_SUCCESS;
}
