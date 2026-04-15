#ifndef _SETUP_H
#define _SETUP_H

#define PAGE_SIZE 4096
#define PAGE_MASK (~(PAGE_SIZE - 1))

#define PRESENT_MASK 0x1
#define READ_WRITE_MASK 0x2

#define BASIC_FLAGS_MASK 0x3
#define PDE_FALGS_MASK 0x83

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

struct vmm_context *create_vmm_context();

void EFIAPI init_vmm_context(struct vmm_context *context);
void EFIAPI start_setup(struct vmm_context *context, UINTN vmm_binary_size);

UINT64 EFIAPI setup_gdt(struct vmm_context *context, UINT64 free_page);
void EFIAPI setup_tss_x86_x64(struct vmm_context *context);
static void EFIAPI setup_tss_2_gdt(struct vmm_context *context);

UINT64 EFIAPI setup_idt(struct vmm_context *context, UINT64 free_page);

UINT64 EFIAPI setup_page_table(struct vmm_context *context, UINT64 free_page,
			       UINT64 mapping_2mb_addr);
static void EFIAPI __left_2mb_mapping(struct vmm_context *context,
					     UINT64 free_page);

UINT64 EFIAPI start_ap_wake_up(struct vmm_context *context, UINT64 free_page);

EFI_FILE_PROTOCOL *open_vmm_binary(EFI_HANDLE image_handle);
UINTN EFIAPI get_vmm_size(EFI_FILE_PROTOCOL *vmm_img);
EFI_STATUS EFIAPI read_vmm_binary(struct vmm_context *context,
				  EFI_FILE_PROTOCOL *vmm_img);

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
