#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#include <Protocol/LoadedImage.h>
#include <Protocol/MpService.h>
#include <Protocol/SimpleFileSystem.h>

#include <Guid/FileInfo.h>

#include "init/boot.h"
#include "setup.h"

static void EFIAPI __set_control_registers(struct uefi_state_struct *state);
static void EFIAPI __set_segment_registers(struct uefi_state_struct *state);
static void EFIAPI __get_gdtr_idtr(struct uefi_state_struct *state);
// static void EFIAPI __set_msr_etc(struct uefi_state_struct *state);


// vmm context -------------------------------------------------
static UINT64 EFIAPI __vmm_mapping(struct vmm_context *context, UINT64 *pde,
				 UINT64 free_page);
static void EFIAPI __print_2mb(struct vmm_context *context, UINT64 *pdpte,
			       UINT64 *pde);
static void EFIAPI __print_4kb(UINT64 *pte1, UINT64 *pte2, UINT64 *pte3);

// -----------------
static void EFIAPI init_vmm_context(struct vmm_context *context);
static void EFIAPI start_setup(struct vmm_context *context);

static UINT64 EFIAPI setup_gdt(struct vmm_context *context, UINT64 free_page);
static void EFIAPI setup_tss_x86_x64(struct vmm_context *context);
static void EFIAPI setup_tss_2_gdt(struct vmm_context *context);

// static UINT64 EFIAPI setup_idt(struct vmm_context *context, UINT64
// free_page);

static UINT64 EFIAPI setup_page_table(struct vmm_context *context,
				      UINT64 free_page);

static void EFIAPI setup_memory_map(struct vmm_context *context);

static UINT64 EFIAPI start_ap_wake_up(struct vmm_context *context,
				      UINT64 free_page);

static void EFIAPI setup_vmm_parameters(struct vmm_context *context,
					struct vmm_parameters *vmm_paramter);

static EFI_FILE_PROTOCOL *open_vmm_binary(EFI_HANDLE image_handle);
static UINTN EFIAPI get_vmm_size(EFI_FILE_PROTOCOL *vmm_img);
static EFI_STATUS EFIAPI read_vmm_binary(struct vmm_context *context,
					 EFI_FILE_PROTOCOL *vmm_img);
// vmm context -------------------------------------------------

// uefi state ------------------------------------------------------
static void EFIAPI start_uefi_setup(struct uefi_state_struct *uefi_state);

struct vmm_context *create_vmm_context(void)
{
	struct vmm_context *context;
	UINTN size;

	size = sizeof(struct vmm_context);
	gBS->AllocatePool(EfiRuntimeServicesData, size, (void **)&context);
	ZeroMem((void *)context, size);

	context->init = init_vmm_context;
	context->start = start_setup;
	context->set_gdt = setup_gdt;
	//context->set_idt = setup_idt;
	context->set_tss = setup_tss_x86_x64;
	context->set_page_table = setup_page_table;

	context->done = setup_vmm_parameters;

	context->open_vmm = open_vmm_binary;
	context->vmm_size = get_vmm_size;
	context->read_vmm = read_vmm_binary;

	context->get_memory_map = setup_memory_map;

	context->start_aps = start_ap_wake_up;

	return context;
}

static void EFIAPI init_vmm_context(struct vmm_context *context)
{
	EFI_STATUS status;
	UINT64 enter_vmm_address;

	context->enter_vmm_addr = 0x3fffff;
	status = gBS->AllocatePages(AllocateMaxAddress, EfiRuntimeServicesCode,
				    1, &context->enter_vmm_addr);
	if (EFI_ERROR(status)) {
		Print(L"failed allocate for identity mapping = %r\r\n", status);
	}
	enter_vmm_address = (UINT64)enter_vmm;
	
	CopyMem((void *)context->enter_vmm_addr, (void *)enter_vmm_address,
		PAGE_4KB);

	context->ap_entry_page = 0xfffff;
	status = gBS->AllocatePages(AllocateMaxAddress, EfiRuntimeServicesData,
				    1, &context->ap_entry_page);
	if (EFI_ERROR(status)) {
		Print(L"failed allocate page for ap entry = %r\r\n", status);
	} else {
		ZeroMem((void *)context->ap_entry_page, PAGE_4KB);
		Print(L"ap entry page address = %lx\r\n",
		      context->ap_entry_page);
	}
}

static EFI_FILE_PROTOCOL *open_vmm_binary(EFI_HANDLE image_handle)
{
	EFI_STATUS status;
	EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *simple;
	EFI_FILE_PROTOCOL *root, *vmm_bin;
	EFI_LOADED_IMAGE_PROTOCOL *loaded_img;

	status = gBS->HandleProtocol(image_handle, &gEfiLoadedImageProtocolGuid,
				     (void **)&loaded_img);
	if (EFI_ERROR(status)) {
		Print(L"Failed to HandleProtocol for Simple File System = "
		      L"%r\r\n",
		      status);
		return NULL;
	}

	status = gBS->HandleProtocol(loaded_img->DeviceHandle,
				     &gEfiSimpleFileSystemProtocolGuid,
				     (void **)&simple);
	if (EFI_ERROR(status)) {
		Print(L"Failed to SimpleFileSystem = %r\r\n", status);
		return NULL;
	}

	status = simple->OpenVolume(simple, &root);
	if (EFI_ERROR(status)) {
		Print(L"Failed to OpenVolume = %r\r\n", status);
		return NULL;
	}

	status = root->Open(root, &vmm_bin, L"vmm.bin",
			    EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0);
	if (EFI_ERROR(status)) {
		Print(L"Failed to Open vmm.bin = %r\r\n", status);
		vmm_bin = NULL;
	}
	Print(L"successed for open vmm binary\r\n");

	return vmm_bin;
}

static EFI_STATUS EFIAPI read_vmm_binary(struct vmm_context *context,
				  EFI_FILE_PROTOCOL *vmm_img)
{
	EFI_STATUS status;
	UINT64 size;

	// context->vmm_bin_size = (UINT64)context->vmm_size(vmm_img);
	// size = ((context->vmm_bin_size / PAGE_4KB) + 1) + MIN_PAGE_NUM;

	// status = gBS->AllocatePages(AllocateAnyPages, EfiRuntimeServicesCode,
	// 			    size, &context->vmm); // vmmsize + 4kb * 0xf
	// if (EFI_ERROR(status)) {
	// 	Print(L"failed allocate pages for vmm = %r!!!!!!!!!\r\n",
	// 	      status);
	// }
	// context->capacity = (UINT64)size * PAGE_4KB;
	// ZeroMem((void *)context->vmm, context->capacity);
	// Print(L"vmm address = %lx\r\n", (UINT64 *)context->vmm);

	//

	size = 0x600;
	context->capacity = size;
	status = gBS->AllocatePages(AllocateAnyPages, EfiRuntimeServicesCode,
				    size, &context->vmm);
	if (EFI_ERROR(status)) {
		Print(L"failed to allocate pages for vmm = %r\r\n", status);
	}
	ZeroMem((void *)context->vmm, context->capacity);
	Print(L"address of vmm = 0x%llx\r\n", context->vmm);

	vmm_img->SetPosition(vmm_img, 0);
	context->vmm_bin_size = 1024 * PAGE_4KB;
	status = vmm_img->Read(vmm_img, &context->vmm_bin_size,
			       (void *)context->vmm);
	if (EFI_ERROR(status)) {
		Print(L"failed to read for vmm binary = %r\r\n", status);
	}

	Print(L"we got read vmm binary\r\n");
	if (EFI_ERROR(status)) {
		Print(L"failed to close for vmm binary = %r\r\n", status);
	}

	return status;
}

static UINTN EFIAPI get_vmm_size(EFI_FILE_PROTOCOL *vmm_img)
{
	UINTN bin_size;

	vmm_img->GetInfo(vmm_img, &gEfiFileInfoGuid, &bin_size, NULL);
	Print(L"vmm binary size = 0x%lx\r\n", bin_size);

	return bin_size;
}

static void EFIAPI start_setup(struct vmm_context *context)
{
	UINT64 free_page;

	free_page = context->vmm + context->vmm_bin_size;
	context->vmm_stack = (EFI_PHYSICAL_ADDRESS)free_page;
	context->vmm_stack_size = 8 * PAGE_4KB;

	free_page = ((context->vmm + (UINT64)context->vmm_bin_size +
		      context->vmm_stack_size) &
		     PAGE_MASK) +
		    PAGE_4KB;

	Print(L"\r\n\r\n\r\n");

	Print(L"free page address = 0x%lx\r\n", free_page);
	context->vmcs = free_page;
	free_page += PAGE_4KB;
	Print(L"free page address = 0x%lx\r\n", free_page);

	Print(L"\r\n\r\n\r\n");

	context->set_tss(context);
	free_page = context->set_gdt(context, free_page);

	/* in enter vmm, i have set cli. */
	// free_page = context->set_idt(context, free_page);

	free_page = context->set_page_table(context, free_page);

	free_page = context->start_aps(context, free_page);

	context->get_memory_map(context);
}

static UINT64 EFIAPI setup_gdt(struct vmm_context *context, UINT64 free_page)
{
	UINT64 gdt_addr, gdtr_addr;
	UINT64 new_gdt_offset;

	gdt_addr = free_page;
	context->gdt = (UINT64 *)gdt_addr;
	Print(L"gdt addr = 0x%lx\r\n", context->gdt);
	context->gdt[0] = X86_NULL_DESCRIPTOR;
	context->gdt[1] = X86_KERNEL_CODE_SEGMENT;
	context->gdt[2] = X86_KERNEL_DATA_SEGMENT;
	context->gdt[3] = X86_USER_CODE_SEGMENT;
	context->gdt[4] = X86_USER_DATA_SEGMENT;
	context->gdt[6] = X64_NULL_DESCRIPTOR;
	context->gdt[7] = X64_KERNEL_CODE_SEGMENT;
	context->gdt[8] = X64_KERNEL_DATA_SEGMENT;
	context->gdt[9] = X64_USER_CODE_SEGMENT;
	context->gdt[10] = X64_USER_DATA_SEGMENT;
	setup_tss_2_gdt(context);

	for (UINT64 i = 0; i < 12; ++i) {
		Print(L"GDT[%llu] = 0x%lx, addr = 0x%lx\r\n", i,
		      context->gdt[i], &context->gdt[i]);
	}

	new_gdt_offset = (UINT64)0x00400000 + (gdt_addr - context->vmm);
	gdtr_addr = gdt_addr + 112;

	context->gdtr = (struct descriptor_register *)gdtr_addr;
	Print(L"gdtr addr = 0x%lx\r\n", (UINT64)context->gdtr);

	// CopyMem((VOID *)new_gdt_offset, (void*)gdt_addr, sizeof(UINT64) *
	// 13);

	context->gdtr->offset = new_gdt_offset;
	context->gdtr->limit = (UINT16)(sizeof(UINT64) * 13) - 1;

	Print(L"gdtr->offset = 0x%lx\r\n", context->gdtr->offset);
	Print(L"gdtr->limit = 0x%lx\r\n", context->gdtr->limit);

	Print(L"\r\n\r\n\r\n");

	free_page += PAGE_4KB;
	return free_page;
}

static void EFIAPI setup_tss_x86_x64(struct vmm_context *context)
{
	ZeroMem((void *)&context->tss32, sizeof(struct task_state_segment32));
	ZeroMem((void *)&context->tss64, sizeof(struct task_state_segment64));

	context->tss32.esp0 =
		((UINT64)context->vmm_stack + context->vmm_stack_size) >> 32;
	context->tss32.cr3 = (UINT64)context->pml4 >> 32;
	context->tss32.eflags = 0x2;
	context->tss32.io_map_base_addr = sizeof(struct task_state_segment32);

	context->tss64.rsp0 =
		(UINT64)context->vmm_stack + context->vmm_stack_size;
	context->tss64.io_map_base_addr = sizeof(struct task_state_segment64);
}

static void EFIAPI setup_tss_2_gdt(struct vmm_context *context)
{
	UINT64 tss_addr;

	tss_addr = (UINT64)&context->tss32;
	context->gdt[5] = (sizeof(struct task_state_segment32) - 1) & 0xffff;
	context->gdt[5] |= (UINT64)(tss_addr & 0xffffff) << 16;
	context->gdt[5] |= (0x89ULL << 40);
	context->gdt[5] |= (UINT64)(tss_addr & 0xff000000) << 32;

	tss_addr = (UINT64)&context->tss64;
	Print(L"tss address = 0x%lx\r\n", tss_addr);
	context->gdt[11] = (sizeof(struct task_state_segment64) - 1) & 0xffff;
	context->gdt[11] |= (UINT64)(tss_addr & 0xffffff) << 16;
	context->gdt[11] |= (0x89ULL << 40);
	context->gdt[11] |= (UINT64)(tss_addr & 0xff000000) << 32;
	context->gdt[12] = 0;
}

//static UINT64 EFIAPI setup_idt(struct vmm_context *context, UINT64 free_page)
//{
//	free_page += PAGE_4KB;
//
//	return free_page;
//}

static UINT64 EFIAPI setup_page_table(struct vmm_context *context, UINT64 free_page)
{
	UINT64 *pdpte, *pde;

	context->pml4 = (UINT64 *)free_page;
	free_page += PAGE_4KB;
	pdpte = (UINT64 *)free_page;
	free_page += PAGE_4KB;
	pde = (UINT64 *)free_page;
	free_page += PAGE_4KB;

	ZeroMem((void *)context->pml4, PAGE_4KB);
	ZeroMem((void *)pdpte, PAGE_4KB);
	ZeroMem((void *)pde, PAGE_4KB);

	for (UINT64 i = 0; i < 2; ++i) {
		pde[i] = (i * PAGE_2MB); /* 0 ~ 4mb*/
		pde[i] |= PDE_FLAGS_MASK;
	}

	free_page = __vmm_mapping(context, pde, free_page);

	pdpte[0] = (UINT64)pde | BASIC_FLAGS_MASK;
	context->pml4[0] = (UINT64)pdpte | BASIC_FLAGS_MASK;
	Print(L"\r\n\r\n");

	Print(L"\r\n\r\n");
	// 4mb + 6mb = 0 - 2 - 4 - 6 - 8 - 10
	__print_2mb(context, pdpte, pde);
	//__print_4kb(&pde[2], &pde[3], &pde[4]);

	Print(L"pdpte = 0x%llx\r\n", pdpte);
	Print(L"pdpte[0] = 0x%llx\r\n", pdpte[0]);
	Print(L"pml4[0] = 0x%llx\r\n", context->pml4[0]);
	Print(L"pml4 = 0x%llx\r\n", context->pml4);

	return free_page;
}

static UINT64 EFIAPI __vmm_mapping(struct vmm_context *context, UINT64 *pde,
				 UINT64 free_page)
{
	UINT64 *pte1, *pte2, *pte3;

	pte1 = (UINT64 *)free_page;
	free_page += PAGE_4KB;
	pte2 = (UINT64 *)free_page;
	free_page += PAGE_4KB;
	pte3 = (UINT64 *)free_page;
	free_page += PAGE_4KB;

	ZeroMem((void *)pte1, PAGE_4KB);
	ZeroMem((void *)pte2, PAGE_4KB);
	ZeroMem((void *)pte3, PAGE_4KB);

	Print(L"vmm address = 0x%llx\r\n", context->vmm);
	for (UINT64 i = 0; i < 512; ++i) {
		pte1[i] = (context->vmm + (PAGE_4KB * i)) | BASIC_FLAGS_MASK;
	}

	for (UINT64 i = 0; i < 512; ++i) {
		pte2[i] = ((context->vmm + 0x00200000) + (PAGE_4KB * i)) |
			  BASIC_FLAGS_MASK;
	}
	for (UINT64 i = 0; i < 512; ++i) {
		pte3[i] = ((context->vmm + 0x00400000) + (PAGE_4KB * i)) |
			  BASIC_FLAGS_MASK;
	}

	pde[2] = ((UINT64)pte1) | BASIC_FLAGS_MASK;
	pde[3] = ((UINT64)pte2) | BASIC_FLAGS_MASK;
	pde[4] = ((UINT64)pte3) | BASIC_FLAGS_MASK;

	return free_page;
}

static void EFIAPI __print_2mb(struct vmm_context *context, UINT64 *pdpte,
			       UINT64 *pde)
{
	for (UINT64 i = 0; i < 5; ++i) {
		Print(L"pde[%llu] = 0x%llx ", i, pde[i]);
	}
	Print(L"\r\n\r\n");
}

static void EFIAPI __print_4kb(UINT64 *pte1, UINT64 *pte2, UINT64 *pte3)
{
	Print(L"pte1 = 0x%llx\r\n", pte1[0]);
	Print(L"pte2 = 0x%llx\r\n", pte2[0]);
	Print(L"pte3 = 0x%llx\r\n", pte3[0]);

	for (UINT64 i = 0; i < 512; ++i) {
		Print(L"pte1[%llu] = 0x%llx\r\n", i, *(UINT64*)&pte1[i]);
	}

	for (UINT64 i = 0; i < 512; ++i) {
		Print(L"pte2[%llu] = 0x%llx\r\n", i, pte2[i]);
	}

	for (UINT64 i = 0; i < 512; ++i) {
		Print(L"pte3[%llu] = 0x%llx\r\n", i, pte3[i]);
	}
	
	Print(L"\r\n\r\n");
}

void EFIAPI setup_memory_map(struct vmm_context *context)
{
	EFI_STATUS status;
	UINT64 size;
	UINTN map_key;

	gBS->AllocatePool(EfiRuntimeServicesData,
			  sizeof(EFI_MEMORY_DESCRIPTOR) * 8,
			  (void **)&context->memory_desc);

	do {
		status = gBS->GetMemoryMap(&context->map_size,
					   context->memory_desc, &map_key,
					   &context->desc_size, NULL);
		gBS->FreePool((void *)context->memory_desc);
		gBS->AllocatePool(EfiRuntimeServicesData, context->map_size,
				  (void **)&context->memory_desc);
		if (EFI_ERROR(status)) {
			Print(L"failed get memory map = %r\r\n", status);
			context->map_size += context->desc_size * 8;
		}
	} while (status == EFI_BUFFER_TOO_SMALL);

	size = context->map_size;
}

static UINT64 EFIAPI start_ap_wake_up(struct vmm_context *context, UINT64 free_page)
{
	EFI_MP_SERVICES_PROTOCOL *mp;
	UINT64 free, end_addr;
	UINTN num;

	gBS->LocateProtocol(&gEfiMpServiceProtocolGuid, NULL, (void **)&mp);

	free = free_page;

	mp->GetNumberOfProcessors(mp, &context->processor_num,
				  &context->enabled_processor_num);

	num = context->processor_num;
	free = sizeof(struct processor_struct) * num;

	Print(L"processor number = %llu\r\n", context->processor_num);
	Print(L"enabled processor number = %llu\r\n",
	      context->enabled_processor_num);

	mp->WhoAmI(mp, &context->bsp);

	/* now we should calc free page pages for next alloc to ap entry page
	 * !!!!! */
	end_addr = free_page + free;
	Print(L"jump free page = %lx\r\n",
	      ((end_addr + (PAGE_4KB - 1)) & PAGE_MASK));
	free_page = ((end_addr + (PAGE_4KB - 1)) & PAGE_MASK);

	free_page += 4096;

	return free_page;
}

static void EFIAPI setup_vmm_parameters(struct vmm_context *context,
				 struct vmm_parameters *vmm_parameter)
{
	vmm_parameter->vmm_entry = context->vmm;
	vmm_parameter->vmm_stack = context->vmm_stack;
	vmm_parameter->vmcs = context->vmcs;
	vmm_parameter->pml4 = (EFI_PHYSICAL_ADDRESS)context->pml4;
	vmm_parameter->extra_memory_size = 0x8000;
	vmm_parameter->ap_entry_page = context->ap_entry_page;
	vmm_parameter->memory_map = (EFI_PHYSICAL_ADDRESS)context->memory_desc;
	vmm_parameter->map_size = context->map_size;

	gBS->AllocatePages(AllocateAnyPages, EfiRuntimeServicesCode, 0x8000,
			   &vmm_parameter->extra_memory);
}

struct uefi_state_struct *create_uefi_state(void)
{
	struct uefi_state_struct *uefi_state;

	gBS->AllocatePool(EfiBootServicesData, sizeof(struct uefi_state_struct),
			  (void **)&uefi_state);
	ZeroMem((void *)uefi_state, sizeof(struct uefi_state_struct));

	uefi_state->start = start_uefi_setup;

	return uefi_state;
}

static void EFIAPI start_uefi_setup(struct uefi_state_struct *state)
{
	EFI_MP_SERVICES_PROTOCOL *mp;

	gBS->LocateProtocol(&gEfiMpServiceProtocolGuid, NULL, (void **)&mp);

	mp->GetNumberOfProcessors(mp, &state->core_num, NULL);

	__set_control_registers(state);
	__set_segment_registers(state);
	__get_gdtr_idtr(state);

	state->rflags = AsmReadEflags();
	state->msr = AsmReadMsr64(0xC0000080);
	state->rip = (UINT64)exit_vmm;
}

static void EFIAPI __set_control_registers(struct uefi_state_struct *state)
{
	state->cr0 = (UINT64)AsmReadCr0();
	state->cr2 = (UINT64)AsmReadCr2();
	state->cr3 = (UINT64)AsmReadCr3();
	state->cr4 = (UINT64)AsmReadCr4();
	state->dr7 = (UINT64)AsmReadDr7();
}

static void EFIAPI __set_segment_registers(struct uefi_state_struct *state)
{
	state->cs.selector = AsmReadCs();
	state->ss.selector = AsmReadSs();
	state->ds.selector = AsmReadDs();
	state->es.selector = AsmReadGs();
	state->fs.selector = AsmReadFs();
	state->gs.selector = AsmReadGs();
	state->tr.selector = AsmReadTr();
}

static void EFIAPI __get_gdtr_idtr(struct uefi_state_struct *state)
{
	struct descriptor_register desc;

	ZeroMem((void *)&desc, sizeof(struct descriptor_register));
	AsmReadGdtr((IA32_DESCRIPTOR *)&desc);
	state->gdtr.limit = desc.limit;
	state->gdtr.offset = desc.offset;

	ZeroMem((void *)&desc, sizeof(struct descriptor_register));
	AsmReadIdtr((IA32_DESCRIPTOR *)&desc);
	state->idtr.limit = desc.limit;
	state->idtr.offset = desc.offset;
}

// static void EFIAPI __set_msr_etc(struct uefi_state_struct *state)
// {

// }

struct vmm_parameters *create_vmm_parameters(void)
{
	struct vmm_parameters *vmm_paramter;

	gBS->AllocatePool(EfiRuntimeServicesData, sizeof(struct vmm_parameters),
			  (void **)&vmm_paramter);

	ZeroMem((void *)vmm_paramter, sizeof(struct vmm_parameters));

	return vmm_paramter;
}
