#ifndef _BOOT_H
#define _BOOT_H

// Forward declarations
typedef struct _SETTINGS_NEW_ENVIRONMENT SETTINGS_NEW_ENVIRONMENT;
typedef struct _UEFI_STATE_STRUCT UEFI_STATE_STRUCT;
typedef struct _INIT_VMM_PARAM INIT_VMM_PARAM;

VOID EFIAPI InitServices(IN EFI_HANDLE ImageHandle,
			 IN EFI_SYSTEM_TABLE *SystemTable);

EFI_STATUS EFIAPI UefiMain(IN EFI_HANDLE ImageHandle,
			   IN EFI_SYSTEM_TABLE *SystemTable);

extern VOID EnterVmm(SETTINGS_NEW_ENVIRONMENT *NewSetup,
		     UEFI_STATE_STRUCT *UefiState);

extern VOID ExitVmm(VOID);

#endif // _BOOT_H
