#ifndef _SETUP_H
#define _SETUP_H

#define PAGE_4KB 0x00001000
#define PAGE_MASK (~(PAGE_4KB - 1))

#define PAGE_2MB 0x00200000

#define BASIC_FLAGS_MASK 0x3
#define PDE_FLAGS_MASK 0x83

//#define PHY_ADDRESS_MASK 0x000FFFFFFFFFF000ULL
//#define PHY_ADDRESS_MASK2 0x000FFFFFFFF00000ULL

#define X86_NULL_DESCRIPTOR 0x00000000000000
#define X86_KERNEL_CODE_SEGMENT 0x00CF9A000FFFFF
#define X86_KERNEL_DATA_SEGMENT 0x00CF92000FFFFF
#define X86_USER_CODE_SEGMENT 0x00CFFA000FFFFF
#define X86_USER_DATA_SEGMENT 0x00CFF2000FFFFF

#define X64_NULL_DESCRIPTOR 0x00000000000000
#define X64_KERNEL_CODE_SEGMENT 0x00AF9A000FFFFF
#define X64_KERNEL_DATA_SEGMENT 0x00CF92000FFFFF
#define X64_USER_CODE_SEGMENT 0x00AFFA000FFFFF
#define X64_USER_DATA_SEGMENT 0x00CFF2000FFFFF

#pragma pack(push, 2)
struct descriptor_register {
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
struct segment_state {
	UINT16 selector;
	UINT16 pad0;
	UINT32 pad1;
	UINT64 limit;
	UINT64 access_rights;
	UINT64 base;
};

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
#pragma pack(pop)

#pragma pack(push, 8)
struct vmm_context {
	EFI_PHYSICAL_ADDRESS vmm; // 0x0
	EFI_PHYSICAL_ADDRESS vmm_stack; // 8
	EFI_PHYSICAL_ADDRESS vmcs; // 10
	EFI_PHYSICAL_ADDRESS enter_vmm_addr; // 18
	EFI_PHYSICAL_ADDRESS ap_entry_page; // 20
	UINT64 vmm_stack_size;

	UINT64 *gdt; // 30
	//struct idt_entry idt[256];

	struct descriptor_register *gdtr; // 38
	//struct descriptor_register *idtr;

	UINT64 *pml4; // 40
	UINT64 *pdpte;
	UINT64 *pde;
	UINT64 *pte;
	struct task_state_segment32 tss32;
	struct task_state_segment64 tss64;

	UINTN rflags;

	EFI_MEMORY_DESCRIPTOR *memory_desc;
	UINTN map_size;
	UINTN desc_size;

	UINT64 bsp;
	UINT64 processor_num;
	UINT64 enabled_processor_num;

	UINT64 vmm_bin_size;
	UINT64 capacity;

	UINT64 current_free_page;

	void(EFIAPI *init)(struct vmm_context *context);
	void(EFIAPI *start)(struct vmm_context *context,
			    struct uefi_state_struct *uefi_state);

	UINT64(EFIAPI *set_gdt)(struct vmm_context *context, UINT64 free_page);
	//UINT64(EFIAPI *set_idt)(struct vmm_context *context, UINT64 free_page);
	void(EFIAPI *set_tss)(struct vmm_context *context);

	void(EFIAPI *get_memory_map)(struct vmm_context *context);
	
	UINT64(EFIAPI *set_page_table)(struct vmm_context *context,
				   UINT64 free_page);

	UINT64(EFIAPI *start_aps)(struct vmm_context *context,
				  UINT64 free_page);

	void(EFIAPI *done)(struct vmm_context *context,
			   struct vmm_parameters *vmm_parameter);

	EFI_FILE_PROTOCOL *(EFIAPI *open_vmm)(EFI_HANDLE image_handle);
	UINTN(EFIAPI *vmm_size)(EFI_FILE_PROTOCOL *vmm_img);
	EFI_STATUS(EFIAPI *read_vmm)(struct vmm_context *context,
				     EFI_FILE_PROTOCOL *vmm_img);
};
#pragma pack(pop)

#pragma pack(push, 1)
struct uefi_state_struct {
	UINT64 rax;
	UINT64 rbx;
	UINT64 rcx;
	UINT64 rdx;
	UINT64 r8;
	UINT64 r9;
	UINT64 r10;
	UINT64 r11;
	UINT64 r12;
	UINT64 r13;
	UINT64 r14;
	UINT64 r15;
	UINT64 rsi;
	UINT64 rdi;
	UINT64 rbp;
	UINT64 rsp;
	UINT64 rip;
	UINT64 rflags;

	UINT64 cr0;
	UINT64 cr2;
	UINT64 cr3;
	UINT64 cr4;

	struct segment_state cs;
	struct segment_state ss;
	struct segment_state ds;
	struct segment_state es;
	struct segment_state fs;
	struct segment_state gs;
	struct segment_state tr;

	UINT64 dr7;

	UINT64 msr;
	
	UINT64 core_num;
	UINT64 entry_page_ap;

	struct descriptor_register gdtr;
	struct descriptor_register idtr;

	void(EFIAPI *start)(struct uefi_state_struct *uefi_state);
	void(EFIAPI *set_registers)(struct uefi_state_struct *uefi_state);
};
#pragma pack(pop)

#pragma pack(push, 8)
struct vmm_parameters {
	EFI_PHYSICAL_ADDRESS uefi_state_address;
	EFI_PHYSICAL_ADDRESS vmm_entry;
	EFI_PHYSICAL_ADDRESS vmm_stack;
	EFI_PHYSICAL_ADDRESS vmcs;
	EFI_PHYSICAL_ADDRESS pml4;
	EFI_PHYSICAL_ADDRESS extra_memory;
	UINT64 extra_memory_size;
	EFI_PHYSICAL_ADDRESS ap_entry_page;
	//EFI_PHYSICAL_ADDRESS memory_map; // actually not need it
	//UINT64 map_size;
};
#pragma pack(pop)


// vmm context -------------------------------------------------
struct vmm_context *create_vmm_context(void);
struct uefi_state_struct *create_uefi_state(void);
struct vmm_parameters *create_vmm_parameters(void);

#endif
