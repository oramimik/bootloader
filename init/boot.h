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

#pragma pack(push, 4)
struct task_state_segment32 {
	UINT32 prev_task_link; // 15 ~ 31 reserved, segment selector for tss
	UINT32 esp0; // 15 ~ 31 reserved
	UINT32 ss0; // 15 ~ 31 reserved, segment selector 0
	UINT32 esp1;
	UINT32 ss1; // 15 ~ 31 reserved, segment selector 1
	UINT32 esp2;
	UINT32 ss2; // 15 ~ 31 reserved, segment selector 2
	UINT32 cr3;
	UINT32 eip;
	UINT32 eflags;
	UINT32 eax;
	UINT32 ecx;
	UINT32 edx;
	UINT32 ebx;
	UINT32 esp;
	UINT32 ebp;
	UINT32 esi;
	UINT32 edi;
	UINT32 es;
	UINT32 cs;
	UINT32 ss;
	UINT32 ds;
	UINT32 fs;
	UINT32 gs;
	UINT32 ldt_segment_selector;
	UINT32 io_map_base_addr; // tss to the i/o permission bit map
	UINT32 ssp; // shadow stack pointer
};
#pragma pack(pop)

#pragma pack(push, 8)
/*
in long mode, the tss does not store information on a task's excution state,
instead it is used to store the ist(interrupt stack table).
*/
struct task_state_segment64 {
	UINT64 reserved0;
	UINT64 rsp0;
	UINT64 rsp1;
	UINT64 rsp2;
	UINT64 reserved1;
	UINT64 ist[7];
	UINT64 reserved2;
	UINT64 reserved3;
	UINT32 io_map_base_addr;
};

struct processor_struct {
	UINT64 processor_id;
	UINT64 package;
	UINT64 core_addr;
	UINT64 thread_addr;
};

struct vmm_context {
	EFI_PHYSICAL_ADDRESS vmm;
	EFI_PHYSICAL_ADDRESS vmm_stack;
	UINT64 vmm_stack_size;

	UINT64 *gdt;
	struct idt_entry idt[256];

	struct gdtr_struct *gdtr;
	struct task_state_segment32 tss32;
	struct task_state_segment64 tss64;
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
	void(EFIAPI *set_tss)(struct vmm_context *context);
	
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