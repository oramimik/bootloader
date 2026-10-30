#ifndef _BOOT_H
#define _BOOT_H

struct settings_new_environment;
struct uefi_state_struct;
struct init_vmm_param;

void EFIAPI init_services(EFI_HANDLE image_handle,
			  EFI_SYSTEM_TABLE *system_table);

EFI_STATUS EFIAPI UefiMain(IN EFI_HANDLE ImageHandle,
			   IN EFI_SYSTEM_TABLE *SystemTable);

extern void enter_vmm(struct settings_new_environment *new_setup,
		      struct uefi_state_struct *uefi_state);

extern void exit_vmm(void);

#endif
