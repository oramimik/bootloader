#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

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
	EFI_FILE_PROTOCOL *vmm_bin;
	struct vmm_context *context;
	struct uefi_state_struct *uefi_state;
	struct vmm_parameters *vmm_parameter;

	DEBUG((DEBUG_INFO, "this is vmm bootloader entry\n"));

	init_services(ImageHandle, SystemTable);

	uefi_state = create_uefi_state();
	context = create_vmm_context();
	vmm_parameter = create_vmm_parameters();

	uefi_state->start(uefi_state);
	Print(L"context addr = %lx\r\n", (UINT64 *)context);

	context->init(context);

	vmm_bin = context->open_vmm(ImageHandle);
	context->read_vmm(context, vmm_bin);

	context->start(context); /*only vmm size is 6mb, total = 8mb*/
	vmm_parameter->uefi_state_address = (EFI_PHYSICAL_ADDRESS)uefi_state;
	context->done(context, vmm_parameter);

	vmm_bin->Close(vmm_bin);

	Print(L"now enter to asm\r\n");
	enter_vmm(context, uefi_state, vmm_parameter);
	Print(L"now out from asm\r\n");

	exit_vmm();

	// i had mistake used to "ExitBootServices"
	gBS->Exit(ImageHandle, 1, 0, NULL);

	return EFI_SUCCESS;
}
