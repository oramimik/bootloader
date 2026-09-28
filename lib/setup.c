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
#include <Guid/Acpi.h>

#include <Include/X64/ProcessorBind.h>

#include "init/boot.h"
#include "setup.h"

static void EFIAPI __set_control_registers(struct uefi_state_struct *state);
static void EFIAPI __set_segment_registers(struct uefi_state_struct *state);
static void EFIAPI __get_gdtr_idtr(struct uefi_state_struct *state);
// static void EFIAPI __set_msr_etc(struct uefi_state_struct *state);


// vmm context -------------------------------------------------
static UINT64 EFIAPI __vmm_mapping(struct vmm_context *context,
				   UINT64 free_page);
static void EFIAPI __extra_mapping(struct vmm_context *context,
				      EFI_PHYSICAL_ADDRESS extra_memory);

static void EFIAPI __print_2mb(struct vmm_context *context);
static void EFIAPI __print_4kb(struct vmm_context *context);

// -----------------
static void EFIAPI __init_vmm_context(struct vmm_context *context);
static void EFIAPI __start_setup(struct vmm_context *context,
				 struct uefi_state_struct *uefi_state);

static UINT64 EFIAPI __setup_gdt(struct vmm_context *context, UINT64 free_page);
static void EFIAPI __setup_tss_x86_x64(struct vmm_context *context);
static void EFIAPI __setup_tss_2_gdt(struct vmm_context *context);

// static UINT64 EFIAPI setup_idt(struct vmm_context *context, UINT64
// free_page);

static UINT64 EFIAPI __setup_basic_page_table(struct vmm_context *context,
				      UINT64 free_page);
static UINT64 EFIAPI __setup_apic_page_table(
	struct vmm_context *context,
	UINT32 loca_apic_address);

static void EFIAPI __setup_memory_map(struct vmm_context *context);

static UINT64 EFIAPI __start_ap_wake_up(struct vmm_context *context,
				      UINT64 free_page);

static UINT64 EFIAPI __setup_vmm_parameters(struct vmm_context *context,
					struct vmm_parameters *vmm_paramter);

static EFI_ACPI_2_0_MULTIPLE_APIC_DESCRIPTION_TABLE_HEADER *
__get_apic(struct vmm_context *context);
static EFI_ACPI_2_0_MULTIPLE_APIC_DESCRIPTION_TABLE_HEADER *
__get_madt(struct vmm_context *context,
			  EFI_ACPI_DESCRIPTION_HEADER *madt_address);

static void EFIAPI
__print_vmm_parameters(struct vmm_parameters *parameters);

static EFI_FILE_PROTOCOL *__open_vmm_binary(EFI_HANDLE image_handle);
static UINTN EFIAPI __get_vmm_size(EFI_FILE_PROTOCOL *vmm_img);
static EFI_STATUS EFIAPI __read_vmm_binary(struct vmm_context *context,
					 EFI_FILE_PROTOCOL *vmm_img);
//static void EFIAPI create_graphics_protocol(struct vmm_context *context);
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

	context->init = __init_vmm_context;
	context->start = __start_setup;
	context->set_gdt = __setup_gdt;
	//context->set_idt = setup_idt;
	context->set_tss = __setup_tss_x86_x64;
	context->set_page_table = __setup_basic_page_table;

	context->done = __setup_vmm_parameters;

	context->open_vmm = __open_vmm_binary;
	context->vmm_size = __get_vmm_size;
	context->read_vmm = __read_vmm_binary;

	context->get_memory_map = __setup_memory_map;

	context->start_aps = __start_ap_wake_up;

	return context;
}

static void EFIAPI __init_vmm_context(struct vmm_context *context)
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

static EFI_FILE_PROTOCOL *__open_vmm_binary(EFI_HANDLE image_handle)
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

static EFI_STATUS EFIAPI __read_vmm_binary(struct vmm_context *context,
				  EFI_FILE_PROTOCOL *vmm_img)
{
	EFI_STATUS status;
	UINTN total_size;

	vmm_img->SetPosition(vmm_img, 0);
	context->vmm_bin_size = (UINT64)__get_vmm_size(vmm_img);
	total_size = (UINTN)context->vmm_bin_size + 0x00200000;

	Print(L"TOTAL SIZE = %llx\r\n", total_size);
	context->capacity = EFI_SIZE_TO_PAGES(total_size);
	Print(L"TOTAL PAGES = %llx\r\n", context->capacity);

	Print(L"TOTAL PAGES FOR VMM = 0x%llx\r\n", context->capacity);
	status = gBS->AllocatePages(AllocateAnyPages, EfiRuntimeServicesCode,
				    context->capacity, &context->vmm);
	if (EFI_ERROR(status)) {
		Print(L"failed to allocate pages for vmm = %r\r\n", status);
		return status;
	}
	ZeroMem((void *)context->vmm, context->capacity);
	Print(L"address of vmm = 0x%llx\r\n", context->vmm);

	
	status = vmm_img->Read(vmm_img, &total_size,
			       (void *)context->vmm);
	if (EFI_ERROR(status)) {
		Print(L"failed to read for vmm binary = %r\r\n", status);
		gBS->FreePages(context->vmm, context->capacity);
		return status;
	}

	return status;
}

static UINTN EFIAPI __get_vmm_size(EFI_FILE_PROTOCOL *vmm_img)
{
	EFI_STATUS status;
	EFI_FILE_INFO *FileInfo;
	UINTN bin_size = 0;

	status = vmm_img->GetInfo(vmm_img, &gEfiFileInfoGuid, &bin_size, NULL);
	if (status == EFI_BUFFER_TOO_SMALL) {
		gBS->AllocatePool(EfiLoaderData, bin_size,
				  (void **)&FileInfo);
	}

	status = vmm_img->GetInfo(vmm_img, &gEfiFileInfoGuid, &bin_size,
			 (void *)FileInfo);
	if (EFI_ERROR(status)) {
		gBS->FreePool(FileInfo);
		return 0;
	}

	Print(L"VMM BINARY NAME = %s\r\n", FileInfo->FileName);
	Print(L"VMM BINARY SIZE = 0x%llx\r\n", FileInfo->FileSize);

	bin_size = FileInfo->FileSize;
	gBS->FreePool(FileInfo);
	return bin_size;
}

static void EFIAPI __start_setup(struct vmm_context *context,
				 struct uefi_state_struct *uefi_state)
{
	UINT64 free_page;
	struct vmm_parameters *parameters =
		(struct vmm_parameters *)(context->vmm + 0x10);

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
	 
	context->current_free_page = free_page;

	context->get_memory_map(context);

	parameters->uefi_state_address = (EFI_PHYSICAL_ADDRESS)uefi_state;
	__setup_vmm_parameters(context, parameters);
}

static UINT64 EFIAPI __setup_gdt(struct vmm_context *context, UINT64 free_page)
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
	context->gdt[5] = 0;
	context->gdt[6] = X64_NULL_DESCRIPTOR;
	context->gdt[7] = X64_KERNEL_CODE_SEGMENT;
	context->gdt[8] = X64_KERNEL_DATA_SEGMENT;
	context->gdt[9] = X64_USER_CODE_SEGMENT;
	context->gdt[10] = X64_USER_DATA_SEGMENT;
	context->gdt[11] = 0;
	__setup_tss_2_gdt(context);

	for (UINT64 i = 0; i < 12; ++i) {
		Print(L"GDT[%llu] = 0x%lx, addr = 0x%lx\r\n", i,
		      context->gdt[i], &context->gdt[i]);
	}

	new_gdt_offset = (UINT64)0x00400000 + (gdt_addr - context->vmm);
	gdtr_addr = gdt_addr + 112;

	context->gdtr = (struct descriptor_register *)gdtr_addr;

	Print(L"new_gdt_offset = %llx\r\n", new_gdt_offset);
	Print(L"gdt_address = %llx\r\n", gdt_addr);
	Print(L"gdtr addr = 0x%lx\r\n", (UINT64)context->gdtr);

	CopyMem((VOID *)new_gdt_offset, (void *)gdt_addr, sizeof(UINT64) * 13);

	context->gdtr->offset = new_gdt_offset;
	context->gdtr->limit = (UINT16)(sizeof(UINT64) * 14) - 1;

	Print(L"gdtr->offset = 0x%lx\r\n", context->gdtr->offset);
	Print(L"gdtr->limit = 0x%lx\r\n", context->gdtr->limit);

	Print(L"\r\n\r\n\r\n");

	free_page += PAGE_4KB;
	return free_page;
}

static void EFIAPI __setup_tss_x86_x64(struct vmm_context *context)
{
	ZeroMem((void *)&context->tss32, sizeof(struct task_state_segment32));
	ZeroMem((void *)&context->tss64, sizeof(struct task_state_segment64));

	/*context->tss32.esp0 =
		((UINT64)context->vmm_stack + context->vmm_stack_size) >> 32;*/
	context->tss32.cr3 = (UINT64)context->pml4 >> 32;
	context->tss32.eflags = 0x2;
	context->tss32.io_map_base_addr = sizeof(struct task_state_segment32);

	context->tss64.rsp0 =
		(UINT64)context->vmm_stack + context->vmm_stack_size;
	context->tss64.io_map_base_addr = sizeof(struct task_state_segment64);
}

static void EFIAPI __setup_tss_2_gdt(struct vmm_context *context)
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

static UINT64 EFIAPI __setup_basic_page_table(struct vmm_context *context, UINT64 free_page)
{
	context->pml4 = (UINT64 *)free_page;
	free_page += PAGE_4KB;
	context->pdpte = (UINT64 *)free_page;
	free_page += PAGE_4KB;
	context->pde = (UINT64 *)free_page;
	free_page += PAGE_4KB;

	ZeroMem((void *)context->pml4, PAGE_4KB);
	ZeroMem((void *)context->pdpte, PAGE_4KB);
	ZeroMem((void *)context->pde, PAGE_4KB);

	for (UINT64 i = 0; i < 2; ++i) {
		context->pde[i] = (i * PAGE_2MB); /* 0 ~ 4mb*/
		context->pde[i] |= PDE_FLAGS_MASK;
	}

	free_page = __vmm_mapping(context, free_page);

	context->pdpte[0] = (UINT64)context->pde | BASIC_FLAGS_MASK;
	context->pml4[0] = (UINT64)context->pdpte | BASIC_FLAGS_MASK;
	Print(L"\r\n\r\n");

	Print(L"\r\n\r\n");
	// 4mb + 6mb = 0 - 2 - 4 - 6 - 8 - 10
	__print_2mb(context);
	//__print_4kb(&pde[2], &pde[3], &pde[4]);

	#ifdef _DEBUG
	Print(L"pdpte = 0x%llx\r\n", context->pdpte);
	Print(L"pdpte[0] = 0x%llx\r\n", context->pdpte[0]);
	Print(L"pml4[0] = 0x%llx\r\n", context->pml4[0]);
	Print(L"pml4 = 0x%llx\r\n", context->pml4);
	#endif

	return free_page;
}

static UINT64 EFIAPI __setup_apic_page_table(
	struct vmm_context *context,
	UINT32 local_apic_address)
{
	// identity mapping for local apic address
	UINT64 *pde, i, *pte;

	pde = (UINT64*)context->current_free_page;
	ZeroMem((void *)pde, PAGE_4KB);
	context->current_free_page += PAGE_4KB;

	i = (local_apic_address >> 21) & 0x1ff;
	pte = (UINT64*)context->current_free_page;
	pte[0] = (UINT64)local_apic_address | 0x13; // present, rw = 1 (ON), cache = 0 (OFF)
	context->pde[i] = ((UINT64)pte) | BASIC_FLAGS_MASK;
	context->pdpte[3] = (UINT64)pde | BASIC_FLAGS_MASK;

	//#ifdef _DEBUG
	Print(L"\r\n\r\n");
	Print(L"pdpte[0] = 0x%llx\r\n", context->pdpte[0]);
	Print(L"pdpte[3] = 0x%llx\r\n", context->pdpte[3]);
	Print(L"pde[%llx] = 0x%llx\r\n", i, context->pde[i]);
	Print(L"pte[0] = 0x%llx\r\n", pte[0]);
	Print(L"\r\n\r\n");
	//#endif

	context->current_free_page += PAGE_4KB;

	return 0;
}

static UINT64 EFIAPI __vmm_mapping(struct vmm_context *context,
				   UINT64 free_page)
{
	UINT64 *pte, i, j;

	for (i = 0; i < 4; ++i) {
		pte = (UINT64 *)free_page;
		free_page += PAGE_4KB;
		ZeroMem((void *)pte, PAGE_4KB);
		for (j = 0; j < 512; ++j) {
			pte[j] = (context->vmm + (i * 0x00200000) +
				  (j * PAGE_4KB)) |
				 BASIC_FLAGS_MASK;
		}
		context->pde[i + 2] = ((UINT64)pte) | BASIC_FLAGS_MASK;
	}

	free_page += PAGE_4KB;
	return free_page;
}

static void EFIAPI __extra_mapping(struct vmm_context *context,
				     EFI_PHYSICAL_ADDRESS extra_memory)
{
	UINT64 i, j, *pte;

	j = 0;
	for (i = 6; i < 70; ++i) {
		pte = (UINT64*)context->current_free_page;
		context->current_free_page += PAGE_4KB;
		ZeroMem((void *)pte, PAGE_4KB);
		for (j = 0; j < 512; ++j) {
			pte[j] = (UINT64)((extra_memory) | BASIC_FLAGS_MASK);
			extra_memory += PAGE_4KB;
			#ifdef _DEBUG
			Print(L"PTE[%llx] = 0x%llx\r\n", j, pte[j]);
			#endif
		}
		context->pde[i] = (UINT64)pte | BASIC_FLAGS_MASK;
		Print(L"PDE[%llx] = 0x%llx\r\n", i, context->pde[i]);
	}
}

static void EFIAPI __print_2mb(struct vmm_context *context)
{
	if (context->pde[5] != 0) {
		for (UINT64 i = 0; i < 69; ++i) {
			Print(L"pde[%llu] = 0x%llx ", i, context->pde[i]);
		}
	} else {
		for (UINT64 i = 0; i < 5; ++i) {
			Print(L"pde[%llu] = 0x%llx\r\n", i, context->pde[i]);
		}
	}
	Print(L"\r\n\r\n");
}

static void EFIAPI __print_4kb(struct vmm_context *context)
{
	
}

void EFIAPI __setup_memory_map(struct vmm_context *context)
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

static UINT64 EFIAPI __start_ap_wake_up(struct vmm_context *context, UINT64 free_page)
{
	/*
	* actually i can wake up every ap cores in here
	* but i wanna be prove myself then i'm gonna be wake up all ap cores in vmm code
	*/
	EFI_MP_SERVICES_PROTOCOL *mp;
	UINT64 free, end_addr;
	UINTN num;

	gBS->LocateProtocol(&gEfiMpServiceProtocolGuid, NULL, (void **)&mp);

	free = free_page;

	mp->GetNumberOfProcessors(mp, &context->processor_num,
				  &context->enabled_processor_num);

	num = context->processor_num;
	free = sizeof(struct processor_struct) * num;

	#ifdef _DEBUG
	Print(L"processor number = %llu\r\n", context->processor_num);
	Print(L"enabled processor number = %llu\r\n",
	      context->enabled_processor_num);
	#endif

	mp->WhoAmI(mp, &context->bsp);
	Print(L"BSP = %llx\r\n", context->bsp);

	/* now we should calc free page pages for next alloc to ap entry page
	 * !!!!! */
	end_addr = free_page + free;
	Print(L"jump free page = %lx\r\n",
	      ((end_addr + (PAGE_4KB - 1)) & PAGE_MASK));
	free_page = ((end_addr + (PAGE_4KB - 1)) & PAGE_MASK);

	free_page += PAGE_4KB;

	return free_page;
}

static UINT64 EFIAPI __setup_vmm_parameters(struct vmm_context *context,
					    struct vmm_parameters *parameters)
{
	UINT64 j;
	volatile UINT32 *apic_address_saver;
	UINT8 *madt_entry, *madt_end;
	EFI_ACPI_2_0_MULTIPLE_APIC_DESCRIPTION_TABLE_HEADER *madt_addr;
	EFI_ACPI_2_0_PROCESSOR_LOCAL_APIC_STRUCTURE *local_apic;

	parameters->vmm_entry = context->vmm;
	parameters->vmm_stack = (UINT64)0x00400000 + (UINT64)0x8000 +
				(context->vmm_stack - context->vmm);
	parameters->vmcs = (UINT64)0x00400000 + (context->vmcs - context->vmm);
	parameters->extra_memory_size = 0x8000000;
	parameters->ap_entry_page = context->ap_entry_page;
	Print(L"enabled processor num = %llx\r\n",
	      context->enabled_processor_num);
	Print(L"processor num = %llx\r\n", context->processor_num);
	parameters->core_num = context->enabled_processor_num;
	madt_addr = __get_apic(context); // should be complete

	context->current_free_page += PAGE_4KB;

	#ifdef _DEBUG
		Print(L"\n\nAPIC ADDRESS IN UEFI = %llx\r\n", madt_addr);
		Print(L"MADT SIGNATURE = %lx\r\n", madt_addr->Header.Signature);
		Print(L"MADT LENGTH = %lx\r\n", madt_addr->Header.Length);
		Print(L"APIC ADDRESS = %lx\r\n", madt_addr->LocalApicAddress);
		Print(L"FLAGS = %lx\r\n\n", madt_addr->Flags);
	#endif

		madt_entry =
			(UINT8 *)
				((UINT8*)madt_addr +
			sizeof(EFI_ACPI_2_0_MULTIPLE_APIC_DESCRIPTION_TABLE_HEADER));
		madt_end = (UINT8 *)madt_addr + madt_addr->Header.Length;

		apic_address_saver =
			(volatile UINT32 *)((UINT64)(madt_addr->LocalApicAddress + 0x20));

		j = AsmReadMsr64(0x1b);

		Print(L"APIC BASE MSR = %lx\r\n", j);
		Print(L"Is BSP? = %s\r\n", (j & (1 << 8)) ? L"YES" : L"NO");
		if (j & (1 << 8)) {
			if (j & (1 << 11)) {
				parameters->bsp =
					(UINT64)(((*apic_address_saver) >> 24) & 0xff);
				Print(L"BSP = %llx\r\n", parameters->bsp);
			}
		}
		Print(L"APIC Global Enable? = %s\r\n",
		      (j & (1 << 11)) ? L"YES" : L"NO");

		j = 0;
		while (madt_entry < madt_end) {
			local_apic =
				(EFI_ACPI_2_0_PROCESSOR_LOCAL_APIC_STRUCTURE *)madt_entry;
			if (local_apic->Type == 0 && local_apic->Flags & 1) {
				Print(L"APIC ID = %lx\r\n", local_apic->ApicId);
				parameters->ipi_address[j] =
					(UINT64)(local_apic->ApicId & 0xff);
				Print(L"IPI ADDRESS = %llx\r\n",
				      parameters->ipi_address[j]); // CORRECT GET IPI APIC ADDRESS !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
				++j;
			}
			if (local_apic->Length == 0) {
				return 0;
			}
			madt_entry += local_apic->Length;
		}
		Print(L"CURRENT APIC COUNT = %llx\r\n", j); // complete get apic address 32 core

		__setup_apic_page_table(
			context,
			madt_addr->LocalApicAddress); // complete identity
						      // mapping for local apic
						      // address
	
	/*CopyMem((void *)parameters->gop, (void *)context->efi_gop,
		sizeof(EFI_GRAPHICS_OUTPUT_PROTOCOL));*/

	gBS->AllocatePages(AllocateAnyPages, EfiRuntimeServicesCode, 0x8000,
			   &parameters->extra_memory);

	////////////////////////////////////////////////////////////////////////////// identity mapping for extra memory address!
	__extra_mapping(context, parameters->extra_memory);
	/////////////////////////// //////////////////////////////////////////////////

	parameters->pml4 = (UINT64)0x00400000 +
			      ((EFI_PHYSICAL_ADDRESS)context->pml4 -
			      context->vmm);

	__print_vmm_parameters(parameters);

	return 1;
}

static EFI_ACPI_2_0_MULTIPLE_APIC_DESCRIPTION_TABLE_HEADER *
__get_apic(struct vmm_context *context)
{
	EFI_ACPI_2_0_MULTIPLE_APIC_DESCRIPTION_TABLE_HEADER *madt_address;
	EFI_CONFIGURATION_TABLE *table;
	EFI_ACPI_2_0_ROOT_SYSTEM_DESCRIPTION_POINTER *rsdp;
	EFI_ACPI_DESCRIPTION_HEADER *xsdt;
	
	table = gST->ConfigurationTable;
	for (UINT64 i = 0; i < gST->NumberOfTableEntries; ++i) {
		if (CompareGuid(&table[i].VendorGuid, &gEfiAcpi20TableGuid)) {
			rsdp = (EFI_ACPI_2_0_ROOT_SYSTEM_DESCRIPTION_POINTER *)
				       table[i]
					       .VendorTable;
			xsdt = (EFI_ACPI_DESCRIPTION_HEADER *)rsdp->XsdtAddress;
		}
	}

	madt_address = __get_madt(context, xsdt);
	if (madt_address == NULL) {
		return NULL;
	}

	return madt_address;
}

static EFI_ACPI_2_0_MULTIPLE_APIC_DESCRIPTION_TABLE_HEADER *
__get_madt(struct vmm_context *context,
			  EFI_ACPI_DESCRIPTION_HEADER *madt_address)
{
	UINT64 entries, len;
	UINT64 *entry;

	len = (UINT32)(*(UINT32 *)((UINT8 *)madt_address + 4));
	Print(L"LEN = %llx\r\n", len);
	entries = len - sizeof(EFI_ACPI_DESCRIPTION_HEADER);
	Print(L"ENTRIES = %llx\r\n", entries);

	entry = (UINT64 *)(madt_address + 1);
	for (UINT64 i = 0; i < entries; ++i) {
		EFI_ACPI_DESCRIPTION_HEADER *h =
			(EFI_ACPI_DESCRIPTION_HEADER *)(UINT64)entry[i];

		if (h->Signature ==
		    EFI_ACPI_2_0_MULTIPLE_APIC_DESCRIPTION_TABLE_SIGNATURE) {
			return (EFI_ACPI_2_0_MULTIPLE_APIC_DESCRIPTION_TABLE_HEADER*)h;
		}
	}

	return NULL;
}

static void EFIAPI __print_vmm_parameters(struct vmm_parameters *parameters)
{
	UINT64 i;

	Print(L"vmm entry = 0x%llx\r\n", parameters->vmm_entry);
	Print(L"vmm stack = 0x%llx\r\n", parameters->vmm_stack);
	Print(L"vmcs = 0x%llx\r\n", parameters->vmcs);
	Print(L"extra memory entry = 0x%llx\r\n", parameters->extra_memory);
	Print(L"extra memory size  = 0x%llx\r\n", parameters->extra_memory_size);
	Print(L"ap entry page = 0x%llx\r\n", parameters->ap_entry_page);
	Print(L"pml4 = 0x%llx\r\n", parameters->pml4);

	for (i = 0; i < 64; ++i) {
		Print(L"ipi address = %llx\r\n", parameters->ipi_address[i]);
	}
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
	state->es.selector = AsmReadEs();
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
