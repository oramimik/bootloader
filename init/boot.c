#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#include "init/boot.h"
#include "lib/setup.h"

// EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
// EFI_GRAPHICS_OUTPUT_BLT_PIXEL blt;

static VOID EFIAPI FreeResources(SETTINGS_NEW_ENVIRONMENT *NewSetup,
				 UEFI_STATE_STRUCT *UefiState);

VOID EFIAPI InitServices(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
	gST = SystemTable;
	gBS = SystemTable->BootServices;
}

// VOID EFIAPI CreatePainting(VOID)
//{
//     EFI_STATUS Status;
//     EFI_GRAPHICS_OUTPUT_BLT_PIXEL Blt;
//
//     Status = gBS->LocateProtocol(&gEfiGraphicsOutputProtocolGuid, NULL,
//                 (VOID **)&gop);
//     if (EFI_ERROR(Status)) {
//         Print(L"failed to locate protocol for graphics output protocol "
//               L"= %r\r\n",
//               Status);
//     }
// }
//
// VOID EFIAPI InitBltPixel(VOID)
//{
// }

static VOID EFIAPI FreeResources(SETTINGS_NEW_ENVIRONMENT *NewSetup,
				 UEFI_STATE_STRUCT *UefiState)
{
	gBS->FreePages(NewSetup->ApEntryPage, 1);
	NewSetup->ApEntryPage = 0;

	gBS->FreePages(NewSetup->EnterVmmAddr, 1);
	NewSetup->EnterVmmAddr = 0;

	gBS->FreePool((VOID *)UefiState);
	UefiState = NULL;

	Print(L"\r\nclear dummy memory\r\n");
}

EFI_STATUS EFIAPI UefiMain(IN EFI_HANDLE ImageHandle,
			   IN EFI_SYSTEM_TABLE *SystemTable)
{
	EFI_FILE_PROTOCOL *VmmBin;
	SETTINGS_NEW_ENVIRONMENT *NewSetup;
	UEFI_STATE_STRUCT *UefiState;
	UINT64 Status;

	DEBUG((DEBUG_INFO, "this is vmm bootloader entry\n"));

	InitServices(ImageHandle, SystemTable);

	UefiState = CreateUefiState();
	NewSetup = CreateNewEnvironment();

	UefiState->Start(UefiState);
	gST->ConOut->SetAttribute(gST->ConOut,
				  EFI_GREEN | EFI_BACKGROUND_BLACK);
	Print(L"context addr = %lx\r\n", (UINT64 *)NewSetup);

	NewSetup->Init(NewSetup);

	VmmBin = NewSetup->OpenVmm(ImageHandle);
	if (EFI_NOT_READY == NewSetup->ReadVmm(NewSetup, VmmBin)) {
		Print(L"Failed read vmm\r\n");
		return EFI_LOAD_ERROR;
	}

	Status = NewSetup->Start(NewSetup,
			UefiState); /* only vmm size is 6mb, total = 8mb */
	VmmBin->Close(VmmBin);
	if (Status == 0) {
		return EFI_LOAD_ERROR;
	}

	Print(L"now enter to asm\r\n");
	EnterVmm(NewSetup, UefiState);
	Print(L"now out from asm\r\n");

	FreeResources(NewSetup, UefiState);
	// actually need free memory for clean
	ExitVmm();

	gBS->Exit(ImageHandle, 1, 0, NULL);

	return EFI_SUCCESS;
}
