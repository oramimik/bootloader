#ifndef _SETUP_H
#define _SETUP_H

#define PAGE_SIZE 4096
#define PAGE_MASK (~(PAGE_SIZE - 1))

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
UINT64 EFIAPI setup_idt(struct vmm_context *context, UINT64 free_page);

UINT64 EFIAPI setup_page_table(struct vmm_context *context, UINT64 free_page);
static inline void EFIAPI __left_2mb_mapping(struct vmm_context *context,
					 UINT64 free_page);

UINT64 EFIAPI start_ap_wake_up(struct vmm_context *context, UINT64 free_page);

EFI_FILE_PROTOCOL *open_vmm_binary(EFI_HANDLE image_handle);
UINTN EFIAPI get_vmm_size(EFI_FILE_PROTOCOL *vmm_img);
EFI_STATUS EFIAPI read_vmm_binary(struct vmm_context *context,
				  EFI_FILE_PROTOCOL *vmm_img);

#endif
