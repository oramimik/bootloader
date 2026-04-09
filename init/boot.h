#ifndef _BOOT_H
#define _BOOT_H

#pragma pack(push, 2)
struct gdtr_struct {
	UINT16 limit;
	UINT64 offset;
};

struct idt_entry {
	UINT16 isr_low;
	UINT16 kernel_cs;
	UINT8 ist;
	UINT8 attributes;
	UINT16 isr_mid;
	UINT32 isr_high;
	UINT32 reserved;
};

struct idtr_struct {
	UINT16 limit;
	UINT64 offset;
};
#pragma pack(pop)

struct tss_descriptor {
	UINT32 reserved0;
	UINT16 rsp0;
	UINT64 rsp1;
	UINT64 rsp2;
	UINT64 reserved1;
	UINT64 ist[7];
	UINT64 reserved2;
	UINT16 reserved3;
	UINT16 iopb_base;
};

#pragma pack(push, 8)
struct processor_struct {
	UINT64 processor_id;
	UINT64 package;
	UINT64 core_addr;
	UINT64 thread_addr;
};

struct vmm_context {
	EFI_PHYSICAL_ADDRESS vmm;
	EFI_PHYSICAL_ADDRESS vmm_stack;

	UINT64 *gdt;
	struct idt_entry idt[256];

	struct gdtr_struct *gdtr;
	struct idtr_struct *idtr;
	UINT64 *pml4;
	UINT64 *pdpte;
	UINT64 *pde;
	UINT64 *pte;

	UINTN rflags;

	EFI_PHYSICAL_ADDRESS ap_entry_page;

	EFI_MEMORY_DESCRIPTOR *memory_desc;
	UINTN map_size;
	UINTN desc_size;

	UINT64 bsp;
	UINT64 processor_num;
	UINT64 enabled_processor_num;
	struct processor_struct *processor;

	UINT64 max_vmm_size;
	UINT64 current_free_address;

	void(EFIAPI *init)(struct vmm_context *context);
	void(EFIAPI *start)(struct vmm_context *context, UINTN vmm_binary_size);

	UINT64(EFIAPI *set_gdt)(struct vmm_context *context, UINT64 free_page);
	UINT64(EFIAPI *set_idt)(struct vmm_context *context, UINT64 free_page);
	
	UINT64(EFIAPI *set_page_table)(struct vmm_context *context,
				   UINT64 free_page);

	UINT64(EFIAPI *start_aps)(struct vmm_context *context,
				  UINT64 free_page);

	EFI_FILE_PROTOCOL *(EFIAPI *open_vmm)(EFI_HANDLE image_handle);
	UINTN(EFIAPI *vmm_size)(EFI_FILE_PROTOCOL *vmm_img);
	EFI_STATUS(EFIAPI *read_vmm)(struct vmm_context *context,
				     EFI_FILE_PROTOCOL *vmm_img);
};
#pragma pack(pop)

void EFIAPI init_services(EFI_HANDLE image_handle,
			  EFI_SYSTEM_TABLE *system_table);

EFI_STATUS EFIAPI UefiMain(IN EFI_HANDLE ImageHandle,
			   IN EFI_SYSTEM_TABLE *SystemTable);

#endif