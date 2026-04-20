#ifndef _SETUP_H
#define _SETUP_H

#define PAGE_4KB 0x1000
#define PAGE_MASK (~(PAGE_4KB - 1))

#define MIN_PAGE_NUM 0xf
#define PAGE_2MB 0x200000

#define PRESENT_MASK 0x1
#define READ_WRITE_MASK 0x2

#define BASIC_FLAGS_MASK 0x3
#define PDE_FALGS_MASK 0x87

#define PHY_ADDRESS_MASK 0x000FFFFFFFFFF000ULL

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

#define offset_of(type, member) ((size_t)&(((type *)0)->member))

#define container_of(ptr, type, member)                                        \
	({                                                                     \
		void *__mptr = (void *)ptr(type *)((char *)__mptr -            \
						   offset_of(type, member));   \
	})

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
	EFI_PHYSICAL_ADDRESS vmm;
	EFI_PHYSICAL_ADDRESS vmm_stack;
	EFI_PHYSICAL_ADDRESS vmcs;
	EFI_PHYSICAL_ADDRESS trampoline_address;
	EFI_PHYSICAL_ADDRESS ap_entry_page;
	UINT64 vmm_stack_size;

	UINT64 *gdt;
	//struct idt_entry idt[256];

	struct descriptor_register *gdtr;
	//struct descriptor_register *idtr;

	struct task_state_segment32 tss32;
	struct task_state_segment64 tss64;
	UINT64 *pml4;
	//UINT64 *pdpte;
	//UINT64 *pde;
	//UINT64 *pte;

	UINTN rflags;

	EFI_MEMORY_DESCRIPTOR *memory_desc;
	UINTN map_size;
	UINTN desc_size;

	UINT64 bsp;
	UINT64 processor_num;
	UINT64 enabled_processor_num;

	UINT64 vmm_bin_size;
	UINT64 capacity;

	void(EFIAPI *init)(struct vmm_context *context);
	void(EFIAPI *start)(struct vmm_context *context);

	UINT64(EFIAPI *set_gdt)(struct vmm_context *context, UINT64 free_page);
	UINT64(EFIAPI *set_idt)(struct vmm_context *context, UINT64 free_page);
	void(EFIAPI *set_tss)(struct vmm_context *context);

	void(EFIAPI *get_memory_map)(struct vmm_context *context);
	
	UINT64(EFIAPI *set_page_table)(struct vmm_context *context,
				   UINT64 free_page, UINT64 mapping_2mb_addr);

	UINT64(EFIAPI *start_aps)(struct vmm_context *context,
				  UINT64 free_page);

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

	UINT64 msr_efer;
	UINT64 msr_lme;
	
	UINT64 core_num;

	UINT64 fs_base;
	UINT64 gs_base;
	UINT64 entry_page_ap;
	UINT64 uncached;

	struct descriptor_register gdtr;
	struct descriptor_register idtr;

	void(EFIAPI *start)(struct uefi_state_struct *uefi_state);
	void(EFIAPI *set_registers)(struct uefi_state_struct *uefi_state);
};
#pragma pack(pop)

struct vmm_context *create_vmm_context();

void EFIAPI init_vmm_context(struct vmm_context *context);
void EFIAPI start_setup(struct vmm_context *context);

UINT64 EFIAPI setup_gdt(struct vmm_context *context, UINT64 free_page);
void EFIAPI setup_tss_x86_x64(struct vmm_context *context);
static void EFIAPI setup_tss_2_gdt(struct vmm_context *context);

UINT64 EFIAPI setup_idt(struct vmm_context *context, UINT64 free_page);

UINT64 EFIAPI setup_page_table(struct vmm_context *context, UINT64 free_page,
			       UINT64 mapping_2mb_addr);
static void EFIAPI __vmm_mapping(struct vmm_context *context, UINT64 *pde,
				 UINT64 *free_page);
static void EFIAPI __print_2mb(struct vmm_context *context,
				      UINT64 *pdpte, UINT64 *pde);
static void EFIAPI __print_4kb(UINT64 *pte0, UINT64 *pte1, UINT64 *pte2);

void EFIAPI setup_memory_map(struct vmm_context *context);

UINT64 EFIAPI start_ap_wake_up(struct vmm_context *context, UINT64 free_page);

EFI_FILE_PROTOCOL *open_vmm_binary(EFI_HANDLE image_handle);
UINTN EFIAPI get_vmm_size(EFI_FILE_PROTOCOL *vmm_img);
EFI_STATUS EFIAPI read_vmm_binary(struct vmm_context *context,
				  EFI_FILE_PROTOCOL *vmm_img);

struct uefi_state_struct *create_uefi_state(void);

void EFIAPI start_uefi_setup(struct uefi_state_struct *uefi_state);
static void EFIAPI __set_control_registers(struct uefi_state_struct *uefi_state);
static void EFIAPI __set_segment_registers(struct uefi_state_struct *uefi_state);
static void EFIAPI __set_gdtr_idtr(struct uefi_state_struct *uefi_state);
static void EFIAPI __set_msr_etc(struct uefi_state_struct *uefi_state);


/*

%macro isr_err_stub 1
isr_stub_%+%1:
	call exception_handler
	iret
%endmacro

%macro isr_no_err_stub 1
isr_stub_%+%1:
	call exception_handler
	iret
%endmacro

isr_no_err_stub 0
isr_no_err_stub 1
isr_no_err_stub 2
isr_no_err_stub 3
isr_no_err_stub 4
isr_no_err_stub 5
isr_no_err_stub 6
isr_no_err_stub 7
isr_err_stub    8
isr_no_err_stub 9
isr_err_stub    10
isr_err_stub    11
isr_err_stub    12
isr_err_stub    13
isr_err_stub    14
isr_no_err_stub 15
isr_no_err_stub 16
isr_err_stub    17
isr_no_err_stub 18
isr_no_err_stub 19
isr_no_err_stub 20
isr_no_err_stub 21
isr_no_err_stub 22
isr_no_err_stub 23
isr_no_err_stub 24
isr_no_err_stub 25
isr_no_err_stub 26
isr_no_err_stub 27
isr_no_err_stub 28
isr_no_err_stub 29
isr_err_stub    30
isr_no_err_stub 31

global exception_handler:
exception_handler:
	cli
	hlt
*/

#endif
