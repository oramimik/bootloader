#ifndef _BOOT_H
#define _BOOT_H

struct vmm_context;
struct uefi_state_struct;

UINT64 EFIAPI get_gdtr_offset();
UINT64 EFIAPI get_pg_table_offset();

void EFIAPI init_services(EFI_HANDLE image_handle,
			  EFI_SYSTEM_TABLE *system_table);

EFI_STATUS EFIAPI UefiMain(IN EFI_HANDLE ImageHandle,
			   IN EFI_SYSTEM_TABLE *SystemTable);

extern void enter_vmm(struct vmm_context *context,
		      struct uefi_state_struct *uefi_state);

#endif

// page table 재수정 및 gdt 재수정
// page table를 지역 변수로 설정하고 PML4를 vmm에 전달
// 0~16MB지점까지 가상 주소를 매핑하고 총 6MB인 물리 주소를 매핑 (4KB)