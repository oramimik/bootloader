#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>


#include <Protocol/LoadedImage.h>
#include <Protocol/MpService.h>
#include <Protocol/SimpleFileSystem.h>

#include <Guid/FileInfo.h>

#include "init/boot.h"
#include "setup.h"

struct vmm_context *create_vmm_context()
{
	struct vmm_context *context;
	UINTN size;

	size = sizeof(struct vmm_context);
	gBS->AllocatePool(EfiRuntimeServicesData, size, (void **)&context);
	ZeroMem((void *)context, size);

	context->init = init_vmm_context;
	context->start = start_setup;
	context->set_gdt = setup_gdt;
	context->set_idt = setup_idt;
	context->set_tss = setup_tss_x86_x64;
	context->set_page_table = setup_page_table;

	context->open_vmm = open_vmm_binary;
	context->vmm_size = get_vmm_size;
	context->read_vmm = read_vmm_binary;

	context->start_aps = start_ap_wake_up;

	return context;
}

void EFIAPI init_vmm_context(struct vmm_context *context)
{
	EFI_STATUS status;
	UINT64 enter_vmm_address;

	status = gBS->AllocatePages(AllocateAnyPages, EfiRuntimeServicesCode,
				    2048, &context->vmm);
	if (EFI_ERROR(status)) {
		Print(L"failed allocate pages for vmm = %r!!!!!!!!!\r\n",
		      status);
	}
	ZeroMem((void *)context->vmm, 2048 * PAGE_SIZE);
	Print(L"vmm address = %lx\r\n", (UINT64 *)context->vmm);

	context->max_vmm_size = 2048 * PAGE_SIZE;

	context->bridge_address = 0x400000;
	status = gBS->AllocatePages(AllocateMaxAddress, EfiRuntimeServicesCode,
				    1, &context->bridge_address);
	if(EFI_ERROR(status)) {
		Print(L"failed allocate for identity mapping = %r\r\n", status);
	}
	enter_vmm_address = (UINT64)enter_vmm;
	CopyMem((void*)context->bridge_address, (void*)enter_vmm_address, 4095);

	context->ap_entry_page = 0xFFFFF;
	status = gBS->AllocatePages(AllocateMaxAddress, EfiRuntimeServicesData,
				    1, &context->ap_entry_page);
	if (EFI_ERROR(status)) {
		Print(L"failed allocate page for ap entry = %r\r\n", status);
	} else {
		ZeroMem((void *)context->ap_entry_page, PAGE_SIZE);
		Print(L"ap entry page address = %lx\r\n",
		      context->ap_entry_page);
	}
}

EFI_FILE_PROTOCOL *open_vmm_binary(EFI_HANDLE image_handle)
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

EFI_STATUS EFIAPI read_vmm_binary(struct vmm_context *context,
				  EFI_FILE_PROTOCOL *vmm_img)
{
	EFI_STATUS status;
	UINTN size;

	size = context->vmm_size(vmm_img);
	vmm_img->SetPosition(vmm_img, 0);

	status = vmm_img->Read(vmm_img, &size, (void *)context->vmm);
	if (EFI_ERROR(status)) {
		Print(L"failed to read for vmm binary = %r\r\n", status);
	}

	Print(L"we got read VMM Binary\r\n");
	if (EFI_ERROR(status)) {
		Print(L"failed to close for vmm binary = %r\r\n", status);
	}

	return status;
}

UINTN EFIAPI get_vmm_size(EFI_FILE_PROTOCOL *vmm_img)
{
	UINTN bin_size;

	vmm_img->GetInfo(vmm_img, &gEfiFileInfoGuid, &bin_size, NULL);
	Print(L"vmm binary size = 0x%lx\r\n", bin_size);

	return bin_size;
}

void EFIAPI start_setup(struct vmm_context *context, UINTN vmm_binary_size)
{
	UINT64 free_page, mapping_2mb;

	free_page = ((context->vmm + vmm_binary_size) & PAGE_MASK) + PAGE_SIZE;

	Print(L"\r\n\r\n\r\n");

	Print(L"free page address = 0x%lx\r\n", free_page);
	context->vmm_stack = (EFI_PHYSICAL_ADDRESS)free_page;
	context->vmm_stack_size = 32 * PAGE_SIZE;
	mapping_2mb = free_page;
	free_page += context->vmm_stack_size;
	Print(L"free page address = 0x%lx\r\n", free_page);

	Print(L"\r\n\r\n\r\n");

	context->set_tss(context);
	free_page = context->set_gdt(context, free_page);

	/* in enter vmm, i have set cli. */
	//free_page = context->set_idt(context, free_page);

	free_page = context->set_page_table(context, free_page, mapping_2mb);

	free_page = context->start_aps(context, free_page);

	context->current_free_address = free_page;
}

UINT64 EFIAPI setup_gdt(struct vmm_context *context, UINT64 free_page)
{
	UINT64 gdt_addr, gdtr_addr;

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

	for (UINT64 i = 0; i < 13; ++i) {
		Print(L"GDT[%llu] = 0x%lx, addr = 0x%lx\r\n", i,
		      context->gdt[i], &context->gdt[i]);
	}

	gdtr_addr = (UINT64)((UINT64 *)gdt_addr + 6);

	context->gdtr = (struct gdtr_struct *)gdtr_addr;
	Print(L"gdtr addr = 0x%lx\r\n", (UINT64)context->gdtr);

	context->gdtr->offset = (UINT64)context->gdt;
	context->gdtr->limit = 0xb;

	Print(L"gdtr->offset = 0x%lx\r\n", context->gdtr->offset);
	Print(L"gdtr->limit = 0x%lx\r\n", context->gdtr->limit);

	Print(L"\r\n\r\n\r\n");

	free_page += PAGE_SIZE;
	return free_page;
}

void EFIAPI setup_tss_x86_x64(struct vmm_context *context)
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

UINT64 EFIAPI setup_idt(struct vmm_context *context, UINT64 free_page)
{
	free_page += PAGE_SIZE;

	return free_page;
}

UINT64 EFIAPI setup_page_table(struct vmm_context *context, UINT64 free_page,
			       UINT64 mapping_2mb_addr)
{
	context->pml4 = (UINT64 *)free_page;
	free_page += PAGE_SIZE;

	context->pdpte = (UINT64 *)free_page;
	free_page += PAGE_SIZE;

	context->pde = (UINT64 *)free_page;
	free_page += PAGE_SIZE;

	context->pte = (UINT64 *)free_page;
	free_page += PAGE_SIZE;

	context->pml4[0] |= (UINT64)BASIC_FLAGS_MASK;
	context->pdpte[0] |= (UINT64)BASIC_FLAGS_MASK;

	for (UINT64 i = 0; i < 3; ++i) {
		context->pde[i] = ((UINT64)context->vmm + (i * 0x200000)) &
				  PHY_ADDRESS_MASK;
		context->pde[i] |= PDE_FALGS_MASK; /* 0, 2MB, 4MB, 6MB */
	}

	Print(L"\r\n\r\n");
	Print(L"PML4[0] Entry: 0x%016lx\r\n", context->pml4[0]);
	Print(L"PDPT[0] Entry: 0x%016lx\r\n", context->pdpte[0]);
	for (UINT64 i = 0; i < 3; ++i) {
		Print(L"PDE[%llu] = 0x%lx\r\n", i, context->pde[i]);
		Print(L"PDE[%llu] = 0x%lx\r\n", i, context->pde[i]);
	}
	Print(L"\r\n");

	__left_2mb_mapping(context, mapping_2mb_addr);

	free_page += PAGE_SIZE;
	return free_page;
}

static void EFIAPI __left_2mb_mapping(struct vmm_context *context,
				      UINT64 mapping_2mb_addr)
{
	context->pde[3] |= PRESENT_MASK;
	context->pde[3] |= READ_WRITE_MASK;
	context->pde[3] |= (UINT64)context->pte;

	for (UINT64 i = 0; i < 512; ++i) {
		context->pte[i] = mapping_2mb_addr + (4096 * i);
		context->pte[i] |= PRESENT_MASK;
		context->pte[i] |= READ_WRITE_MASK;
	}

	Print(L"\r\n\r\n");
	Print(L"PML4[0] Entry: 0x%016lx\r\n", context->pml4[0]);
	Print(L"PDPT[0] Entry: 0x%016lx\r\n", context->pdpte[0]);
	for (UINT64 i = 0; i < 3; ++i) {
		Print(L"PDE[%llu] = 0x%lx\r\n", i, context->pde[i]);
		Print(L"PDE[%llu] = 0x%lx\r\n", i, context->pde[i]);
	}
	Print(L"\r\n\r\n");

	Print(L"\r\n");
}

UINT64 EFIAPI start_ap_wake_up(struct vmm_context *context, UINT64 free_page)
{
	EFI_MP_SERVICES_PROTOCOL *mp;
	UINT64 free, end_addr;
	UINTN num;
	EFI_PROCESSOR_INFORMATION processor_info;

	gBS->LocateProtocol(&gEfiMpServiceProtocolGuid, NULL, (void **)&mp);

	free = free_page;
	context->processor = (struct processor_struct *)free;

	mp->GetNumberOfProcessors(mp, &context->processor_num,
				  &context->enabled_processor_num);

	num = context->processor_num;
	free = sizeof(struct processor_struct) * num;

	Print(L"processor number = %llu\r\n", context->processor_num);
	Print(L"enabled processor number = %llu\r\n",
	      context->enabled_processor_num);

	mp->WhoAmI(mp, &context->bsp);

	for (UINT64 i = 0; i < num; ++i) {
		mp->GetProcessorInfo(mp, i, &processor_info);
		context->processor[i].processor_id = processor_info.ProcessorId;
		context->processor[i].package =
			(UINT64)processor_info.Location.Package;
		context->processor[i].core_addr =
			(UINT64)processor_info.Location.Core;
		context->processor[i].thread_addr =
			(UINT64)processor_info.Location.Thread;
	}

	/* now we should calc free page pages for next alloc to ap entry page
	 * !!!!! */
	end_addr = free_page + free;
	Print(L"jump free page = %lx\r\n",
	      ((end_addr + (PAGE_SIZE - 1)) & PAGE_MASK));
	free_page = ((end_addr + (PAGE_SIZE - 1)) & PAGE_MASK);

	return free_page;
}