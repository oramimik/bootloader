#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#include "init/boot.h"
#include "lib/setup.h"

//EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
//EFI_GRAPHICS_OUTPUT_BLT_PIXEL blt;

static void EFIAPI __free(struct settings_new_environment *new_setup,
			  struct uefi_state_struct *uefi_state);

void EFIAPI init_services(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
	gST = SystemTable;
	gBS = SystemTable->BootServices;
}

//void EFIAPI __create_painting(void)
//{
//	EFI_STATUS status;
//
//	EFI_GRAPHICS_OUTPUT_BLT_PIXEL blt;
//
//	status = gBS->LocateProtocol(&gEfiGraphicsOutputProtocolGuid, NULL,
//			    (void **)&gop);
//	if (EFI_ERROR(status)) {
//		Print(L"failed to locate protocol for graphics output protocol "
//		      L"= %r\r\n",
//		      status);
//	}
//
//	
//}
//
//void EFIAPI __init_blt_pixel(void)
//{
//	
//}

static void EFIAPI __free(struct settings_new_environment *new_setup,
			  struct uefi_state_struct *uefi_state)
{
	gBS->FreePages(new_setup->ap_entry_page, 1);
	new_setup->ap_entry_page = 0;

	gBS->FreePages(new_setup->enter_vmm_addr, 1);
	new_setup->enter_vmm_addr = 0;

	gBS->FreePool((void *)uefi_state);
	uefi_state = NULL;

	Print(L"\r\nclear dummy memory\r\n");
}

EFI_STATUS EFIAPI UefiMain(IN EFI_HANDLE ImageHandle,
			   IN EFI_SYSTEM_TABLE *SystemTable)
{
	EFI_FILE_PROTOCOL *vmm_bin;
	struct settings_new_environment *new_setup;
	struct uefi_state_struct *uefi_state;

	DEBUG((DEBUG_INFO, "this is vmm bootloader entry\n"));

	init_services(ImageHandle, SystemTable);

	uefi_state = create_uefi_state();
	new_setup = create_new_enviroment();

	uefi_state->start(uefi_state);
	gST->ConOut->SetAttribute(gST->ConOut,
				  EFI_GREEN | EFI_BACKGROUND_BLACK);
	Print(L"context addr = %lx\r\n", (UINT64 *)new_setup); 

	new_setup->init(new_setup);

	vmm_bin = new_setup->open_vmm(ImageHandle);
	new_setup->read_vmm(new_setup, vmm_bin);

	new_setup->start(new_setup, uefi_state); /*only vmm size is 6mb, total = 8mb*/

	vmm_bin->Close(vmm_bin);

	Print(L"now enter to asm\r\n");
	enter_vmm(new_setup, uefi_state);
	Print(L"now out from asm\r\n");


	__free(new_setup, uefi_state);
	// actually need free memory for clean
	exit_vmm();

	gBS->Exit(ImageHandle, 1, 0, NULL);

	return EFI_SUCCESS;
}
