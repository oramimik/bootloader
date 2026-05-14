#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#include "init/boot.h"
#include "lib/setup.h"

static void EFIAPI __free(struct vmm_context *context,
			  struct uefi_state_struct *uefi_state);

void EFIAPI init_services(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
	gST = SystemTable;
	gBS = SystemTable->BootServices;
}

static void EFIAPI __free(struct vmm_context *context,
			  struct uefi_state_struct *uefi_state)
{
	gBS->FreePages(context->ap_entry_page, 1);
	context->ap_entry_page = 0;

	gBS->FreePages(context->enter_vmm_addr, 1);
	context->enter_vmm_addr = 0;

	gBS->FreePool((void *)uefi_state);
	uefi_state = NULL;

	Print(L"\r\nclear dummy memory\r\n");
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
	context = create_vmm_context();

	uefi_state->start(uefi_state);
	Print(L"context addr = %lx\r\n", (UINT64 *)context);

	context->init(context);

	vmm_bin = context->open_vmm(ImageHandle);
	context->read_vmm(context, vmm_bin);

	context->start(context, uefi_state); /*only vmm size is 6mb, total = 8mb*/

	vmm_bin->Close(vmm_bin);

	Print(L"now enter to asm\r\n");
	enter_vmm(context, uefi_state);
	Print(L"now out from asm\r\n");

	// actually need free memory for clean
	exit_vmm();

	gBS->Exit(ImageHandle, 1, 0, NULL);

	return EFI_SUCCESS;
}
