#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#include <Protocol/LoadedImage.h>
#include <Protocol/MpService.h>
#include <Protocol/SimpleFileSystem.h>

#include <Guid/Acpi.h>
#include <Guid/FileInfo.h>

#include <Include/X64/ProcessorBind.h>

#include "init/boot.h"
#include "setup.h"

static void EFIAPI __set_control_registers(struct uefi_state_struct *state);
static void EFIAPI __set_segment_registers(struct uefi_state_struct *state);
static void EFIAPI __get_gdtr_idtr(struct uefi_state_struct *state);
// static void EFIAPI __set_msr_etc(struct uefi_state_struct *state);

// vmm context -------------------------------------------------
static UINT64 EFIAPI __vmm_mapping(struct settings_new_environment *new_setup,
				   UINT64 free_page);
static void EFIAPI __extra_mapping(struct settings_new_environment *new_setup,
				   EFI_PHYSICAL_ADDRESS extra_memory_pa);

static void EFIAPI __print_2mb(struct settings_new_environment *new_setup);
static void EFIAPI __print_4kb(struct settings_new_environment *new_setup);

// -----------------
static void EFIAPI
__init_vmm_context(struct settings_new_environment *new_setup);
static void EFIAPI __start_setup(struct settings_new_environment *new_setup,
				 struct uefi_state_struct *uefi_state);

static UINT64 EFIAPI __setup_gdt(struct settings_new_environment *new_setup,
				 UINT64 free_page);
static void EFIAPI
__setup_tss_x86_x64(struct settings_new_environment *new_setup);
static void EFIAPI
__setup_tss_2_gdt(struct settings_new_environment *new_setup);

// static UINT64 EFIAPI setup_idt(struct vmm_context *context, UINT64
// free_page);

static UINT64 EFIAPI __setup_page_table(
	struct settings_new_environment *new_setup, UINT64 free_page);
static UINT64 EFIAPI __setup_apic_page_table(
	struct settings_new_environment *context, UINT32 loca_apic_address);

static void EFIAPI
__setup_memory_map(struct settings_new_environment *new_setup);

static UINT64 EFIAPI __start_ap_wake_up(
	struct settings_new_environment *new_setup, UINT64 free_page);

static UINT64 EFIAPI
__setup_vmm_parameters(struct settings_new_environment *new_setup,
		       struct init_vmm_param *vmm_paramter);

static EFI_ACPI_6_6_MULTIPLE_APIC_DESCRIPTION_TABLE_HEADER *
__get_apic(struct settings_new_environment *new_setup);
static EFI_ACPI_6_6_MULTIPLE_APIC_DESCRIPTION_TABLE_HEADER *
__get_madt(struct settings_new_environment *new_setup,
	   EFI_ACPI_DESCRIPTION_HEADER *madt_address);

static void EFIAPI __print_vmm_parameters(struct init_vmm_param *parameters);

static EFI_FILE_PROTOCOL *__open_vmm_binary(EFI_HANDLE image_handle);
static UINTN EFIAPI __get_vmm_size(EFI_FILE_PROTOCOL *vmm_img);
static EFI_STATUS EFIAPI __read_vmm_binary(
	struct settings_new_environment *new_setup, EFI_FILE_PROTOCOL *vmm_img);
// static void EFIAPI create_graphics_protocol(struct vmm_context *context);
//  vmm context -------------------------------------------------

// uefi state ------------------------------------------------------
static void EFIAPI start_uefi_setup(struct uefi_state_struct *uefi_state);

struct settings_new_environment *create_new_enviroment(void)
{
	struct settings_new_environment *context;
	UINTN size;

	size = sizeof(struct settings_new_environment);
	gBS->AllocatePool(EfiRuntimeServicesData, size, (void **)&context);
	ZeroMem((void *)context, size);

	context->init = __init_vmm_context;
	context->start = __start_setup;
	context->set_gdt = __setup_gdt;
	// context->set_idt = setup_idt;
	context->set_tss = __setup_tss_x86_x64;
	context->set_page_table = __setup_page_table;

	context->done = __setup_vmm_parameters;

	context->open_vmm = __open_vmm_binary;
	context->vmm_size = __get_vmm_size;
	context->read_vmm = __read_vmm_binary;

	context->get_memory_map = __setup_memory_map;

	context->start_aps = __start_ap_wake_up;

	return context;
}

static void EFIAPI __init_vmm_context(struct settings_new_environment *context)
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

static EFI_STATUS EFIAPI __read_vmm_binary(
	struct settings_new_environment *new_setup, EFI_FILE_PROTOCOL *vmm_img)
{
	EFI_STATUS status;
	UINTN total_size;

	vmm_img->SetPosition(vmm_img, 0);
	new_setup->vmm_bin_size = (UINT64)__get_vmm_size(vmm_img);
	total_size = (UINTN)new_setup->vmm_bin_size + 0x00200000;

	Print(L"TOTAL SIZE = %llx\r\n", total_size);
	new_setup->capacity = EFI_SIZE_TO_PAGES(total_size);
	Print(L"TOTAL PAGES = %llx\r\n", new_setup->capacity);

	Print(L"TOTAL PAGES FOR VMM = 0x%llx\r\n", new_setup->capacity);
	status = gBS->AllocatePages(AllocateAnyPages, EfiRuntimeServicesCode,
				    new_setup->capacity, &new_setup->vmm);
	if (EFI_ERROR(status)) {
		Print(L"failed to allocate pages for vmm = %r\r\n", status);
		return status;
	}
	ZeroMem((void *)new_setup->vmm, new_setup->capacity);
	Print(L"address of vmm = 0x%llx\r\n", new_setup->vmm);

	status = vmm_img->Read(vmm_img, &total_size, (void *)new_setup->vmm);
	if (EFI_ERROR(status)) {
		Print(L"failed to read for vmm binary = %r\r\n", status);
		gBS->FreePages(new_setup->vmm, new_setup->capacity);
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
		gBS->AllocatePool(EfiLoaderData, bin_size, (void **)&FileInfo);
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

static void EFIAPI __start_setup(struct settings_new_environment *new_setup,
				 struct uefi_state_struct *uefi_state)
{
	UINT64 free_page;
	struct init_vmm_param *parameters =
		(struct init_vmm_param *)(new_setup->vmm + 0x10);

	free_page = ((new_setup->vmm + new_setup->vmm_bin_size) &
		     0xffffffffffff0000ULL) +
		    PAGE_4KB;
	new_setup->vmm_stack = (EFI_PHYSICAL_ADDRESS)free_page;
	new_setup->vmm_stack_size = 8 * PAGE_4KB;

	free_page = ((new_setup->vmm + (UINT64)new_setup->vmm_bin_size +
		      new_setup->vmm_stack_size) &
		     PAGE_MASK) +
		    PAGE_4KB;

	Print(L"\r\n\r\n\r\n");

	Print(L"free page address = 0x%lx\r\n", free_page);
	new_setup->vmcs = free_page;
	free_page += PAGE_4KB;
	Print(L"free page address = 0x%lx\r\n", free_page);

	Print(L"\r\n\r\n\r\n");

	new_setup->set_tss(new_setup);
	free_page = new_setup->set_gdt(new_setup, free_page);

	// free_page = context->set_idt(context, free_page);

	free_page = new_setup->set_page_table(new_setup, free_page);

	free_page = new_setup->start_aps(new_setup, free_page);

	new_setup->current_free_page = free_page;
	Print(L"how many used free page = %llx\r\n",
	      (new_setup->current_free_page - new_setup->vmm) >> 12);

	new_setup->get_memory_map(new_setup);

	parameters->uefi_state_address = (EFI_PHYSICAL_ADDRESS)uefi_state;
	__setup_vmm_parameters(new_setup, parameters);
}

static UINT64 EFIAPI __setup_gdt(struct settings_new_environment *new_setup,
				 UINT64 free_page)
{
	UINT64 gdt_addr_va, gdtr_addr;
	UINT64 gdt_offset_va;

	gdt_addr_va = free_page;
	new_setup->gdt = (UINT64 *)gdt_addr_va;
	Print(L"gdt addr = 0x%lx\r\n", new_setup->gdt);
	new_setup->gdt[0] = X86_NULL_DESCRIPTOR;
	new_setup->gdt[1] = X86_KERNEL_CODE_SEGMENT;
	new_setup->gdt[2] = X86_KERNEL_DATA_SEGMENT;
	new_setup->gdt[3] = X86_USER_CODE_SEGMENT;
	new_setup->gdt[4] = X86_USER_DATA_SEGMENT;
	new_setup->gdt[5] = 0;
	new_setup->gdt[6] = X64_NULL_DESCRIPTOR;
	new_setup->gdt[7] = X64_KERNEL_CODE_SEGMENT;
	new_setup->gdt[8] = X64_KERNEL_DATA_SEGMENT;
	new_setup->gdt[9] = X64_USER_CODE_SEGMENT;
	new_setup->gdt[10] = X64_USER_DATA_SEGMENT;
	new_setup->gdt[11] = 0;
	__setup_tss_2_gdt(new_setup);

	for (UINT64 i = 0; i < 12; ++i) {
		Print(L"GDT[%llu] = 0x%lx, addr = 0x%lx\r\n", i,
		      new_setup->gdt[i], &new_setup->gdt[i]);
	}

	gdt_offset_va = (UINT64)0x00400000 + (gdt_addr_va - new_setup->vmm);
	gdtr_addr = gdt_addr_va + 112;

	new_setup->gdtr = (struct descriptor_register *)gdtr_addr;

	Print(L"new_gdt_offset = %llx\r\n", gdt_offset_va);
	Print(L"gdt_address = %llx\r\n", gdt_addr_va);
	Print(L"gdtr addr = 0x%lx\r\n", (UINT64)new_setup->gdtr);

	CopyMem((VOID *)gdt_offset_va, (void *)gdt_addr_va,
		sizeof(UINT64) * 14);

	new_setup->gdtr->offset = gdt_offset_va;
	new_setup->gdtr->limit = (UINT16)(sizeof(UINT64) * 14) - 1;

	Print(L"gdtr->offset = 0x%lx\r\n", new_setup->gdtr->offset);
	Print(L"gdtr->limit = 0x%lx\r\n", new_setup->gdtr->limit);

	Print(L"\r\n\r\n\r\n");

	free_page += PAGE_4KB;
	return free_page;
}

static void EFIAPI __setup_tss_x86_x64(struct settings_new_environment *context)
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

static void EFIAPI __setup_tss_2_gdt(struct settings_new_environment *new_setup)
{
	UINT64 tss_addr;

	tss_addr = (UINT64)&new_setup->tss32;
	new_setup->gdt[5] = (sizeof(struct task_state_segment32) - 1) & 0xffff;
	new_setup->gdt[5] |= (UINT64)(tss_addr & 0xffffff) << 16;
	new_setup->gdt[5] |= (0x89ULL << 40);
	new_setup->gdt[5] |= (UINT64)(tss_addr & 0xff000000) << 32;

	tss_addr = (UINT64)&new_setup->tss64;
	Print(L"tss address = 0x%lx\r\n", tss_addr);
	new_setup->gdt[11] = (sizeof(struct task_state_segment64) - 1) & 0xffff;
	new_setup->gdt[11] |= (UINT64)(tss_addr & 0xffffff) << 16;
	new_setup->gdt[11] |= (0x89ULL << 40);
	new_setup->gdt[11] |= (UINT64)(tss_addr & 0xff000000) << 32;
	new_setup->gdt[12] = 0;
}

// static UINT64 EFIAPI setup_idt(struct vmm_context *context, UINT64 free_page)
//{
//	free_page += PAGE_4KB;
//
//	return free_page;
// }

static UINT64 EFIAPI
__setup_page_table(struct settings_new_environment *new_setup, UINT64 free_page)
{
	new_setup->pml4 = (UINT64 *)free_page;
	free_page += PAGE_4KB;
	new_setup->pdpte = (UINT64 *)free_page;
	free_page += PAGE_4KB;
	new_setup->pde = (UINT64 *)free_page;
	free_page += PAGE_4KB;

	ZeroMem((void *)new_setup->pml4, PAGE_4KB);
	ZeroMem((void *)new_setup->pdpte, PAGE_4KB);
	ZeroMem((void *)new_setup->pde, PAGE_4KB);

	for (UINT64 i = 0; i < 2; ++i) {
		new_setup->pde[i] = (i * PAGE_2MB); /* 0 ~ 4mb*/
		new_setup->pde[i] |= PDE_FLAGS_MASK;
	}

	free_page = __vmm_mapping(new_setup, free_page);

	new_setup->pdpte[0] = (UINT64)new_setup->pde | BASIC_FLAGS_MASK;
	new_setup->pml4[0] = (UINT64)new_setup->pdpte | BASIC_FLAGS_MASK;
	Print(L"\r\n\r\n");

	Print(L"\r\n\r\n");
	__print_2mb(new_setup);
	__print_4kb(new_setup);

#ifdef _DEBUG
	Print(L"pdpte = 0x%llx\r\n", new_setup->pdpte);
	Print(L"pdpte[0] = 0x%llx\r\n", new_setup->pdpte[0]);
	Print(L"pml4[0] = 0x%llx\r\n", new_setup->pml4[0]);
	Print(L"pml4 = 0x%llx\r\n", new_setup->pml4);
#endif

	gST->ConOut->ClearScreen(gST->ConOut);

	Print(L"\n\n");

	Print(L"   _____     /\\      ____    _____        _____     /\\      "
	      L"_____    |      _____ \r\n");
	Print(L"  |  _ \\\\   /  \\    / ___|  | ____|      |_   _|   /  \\    "
	      L"|  _ \\\\   |     | ____|\r\n");
	Print(L"  | |_) |  / /\\ \\  | |  _   |  _|          | |    / /\\ \\   "
	      L"| |_) |   |     |  _|  \r\n");
	Print(L"  |  __/  / ____ \\ | |_| |  | |___         | |   / ____ \\  | "
	      L" _ <    |___  | |___ \r\n");
	Print(L"  |_|    /_/    \\_\\\\____|  |_____|        |_|  /_/    \\_\\ "
	      L"|_| \\_\\   |_____| |_____|\r\n");

	gBS->Stall(3000);

	return free_page;
}

static UINT64 EFIAPI __setup_apic_page_table(
	struct settings_new_environment *new_setup, UINT32 local_apic_address)
{
	// identity mapping for local apic address
	UINT64 *pde, i, *pte;

	pde = (UINT64 *)new_setup->current_free_page;
	ZeroMem((void *)pde, PAGE_4KB);
	new_setup->current_free_page += PAGE_4KB;

	i = (local_apic_address >> 21) & 0x1ff;
	pte = (UINT64 *)new_setup->current_free_page;
	pte[0] = (UINT64)local_apic_address |
		 0x13; // present, rw = 1 (ON), cache = 0 (OFF)
	new_setup->pde[i] = ((UINT64)pte) | BASIC_FLAGS_MASK;
	new_setup->pdpte[3] = (UINT64)pde | BASIC_FLAGS_MASK;

	// #ifdef _DEBUG
	Print(L"\r\n\r\n");
	Print(L"pdpte[0] = 0x%llx\r\n", new_setup->pdpte[0]);
	Print(L"pdpte[3] = 0x%llx\r\n", new_setup->pdpte[3]);
	for (i = 0; i < 70; ++i) {
		Print(L"===================pde[%llx] = "
		      L"0x%llx==================\r\n",
		      i, new_setup->pde[i]);
	}
	Print(L"\r\n\r\n");
	// #endif

	new_setup->current_free_page += PAGE_4KB;

	return 0;
}

static UINT64 EFIAPI __vmm_mapping(struct settings_new_environment *new_setup,
				   UINT64 free_page)
{
	UINT64 *pte, vector, j, i;

	vector = EFI_SIZE_TO_PAGES(new_setup->vmm_bin_size);
	Print(L"vmm image size to pages = %llx\r\n", vector);

	vector = (vector + (512 - 1)) >> 9;
	++vector;
	Print(L"Pages = %llx\r\n", vector);
	
	for (i = 0; i < vector; ++i) {
		Print(L"Mapping %llx\r\n", i);
		pte = (UINT64 *)free_page;
		free_page += PAGE_4KB;
		ZeroMem((void *)pte, PAGE_4KB);
		for (j = 0; j < 512; ++j) {
			pte[j] = (new_setup->vmm + (vector * PAGE_2MB) +
				  (j * PAGE_4KB)) |
				 BASIC_FLAGS_MASK;
		}
		new_setup->pde[vector + 2] = ((UINT64)pte) | BASIC_FLAGS_MASK;
	}

	gST->ConOut->ClearScreen(gST->ConOut);

	Print(L"\n\n");

	Print(L"  \\   /  |  \\/  |  \\/  | |  \\/  |  / \\  |  _ \\|  _ \\| | "
	      L"| |/ ___|\r\n");
	Print(L"   \\ / /  | |\\/| | |\\/| | | |\\/| | / _ \\ | |_) | |_) | | "
	      L"| | |  _ \r\n");
	Print(L"    V /   |_|  |_|_|  |_| |_|  |_|/_/ \\_\\|  __/|  __/| |_| | "
	      L"|_| |\r\n");
	Print(L"     V                                   |_|   |_|    \\___/ "
	      L"\\____|\r\n");

	gBS->Stall(3000);

	free_page += PAGE_4KB;
	return free_page;
}

static void EFIAPI __extra_mapping(struct settings_new_environment *new_setup,
				   EFI_PHYSICAL_ADDRESS extra_memory_pa)
{
	UINT64 i, j, *pte, vector;

	vector = EFI_SIZE_TO_PAGES(new_setup->vmm_bin_size);
	Print(L"vmm image size to pages = %llx\r\n", vector);

	vector = (vector + (512 - 1)) >> 9;
	++vector;
	Print(L"Pages = %llx\r\n", vector);

	j = 0;
	// i = 6 == start from 6mb, 4096 x 512 x 64 = 128mb
	for (i = 6; i < 70; ++i) {
		pte = (UINT64 *)new_setup->current_free_page;
		new_setup->current_free_page += PAGE_4KB;
		Print(L"CURRENT FREE PAGES = %llx\r\n",
		      new_setup->current_free_page);
		ZeroMem((void *)pte, PAGE_4KB);
		for (j = 0; j < 512; ++j) {
			pte[j] = (UINT64)((extra_memory_pa) | BASIC_FLAGS_MASK);
			extra_memory_pa += PAGE_4KB;
			// #ifdef _DEBUG
			if (j > 510) {
				Print(L"##################PTE[%llx] = "
				      L"0x%llx#####################\r\n",
				      j, pte[j]);
			}

			// #endif
		}
		new_setup->pde[i] = (UINT64)pte | BASIC_FLAGS_MASK;
		Print(L"===============PDE[%llx] = "
		      L"0x%llx===================\r\n",
		      i, new_setup->pde[i]);
	}
}

static void EFIAPI __print_2mb(struct settings_new_environment *new_setup)
{
	if (new_setup->pde[5] != 0) {
		for (UINT64 i = 0; i < 69; ++i) {
			Print(L"pde[%llu] = 0x%llx ", i, new_setup->pde[i]);
		}
	} else {
		for (UINT64 i = 0; i < 5; ++i) {
			Print(L"pde[%llu] = 0x%llx\r\n", i, new_setup->pde[i]);
		}
	}
	Print(L"\r\n\r\n");
}

static void EFIAPI __print_4kb(struct settings_new_environment *new_setup)
{
}

void EFIAPI __setup_memory_map(struct settings_new_environment *new_setup)
{
	EFI_STATUS status;
	UINT64 size;
	UINTN map_key;

	gBS->AllocatePool(EfiRuntimeServicesData,
			  sizeof(EFI_MEMORY_DESCRIPTOR) * 8,
			  (void **)&new_setup->memory_desc);

	do {
		status = gBS->GetMemoryMap(&new_setup->map_size,
					   new_setup->memory_desc, &map_key,
					   &new_setup->desc_size, NULL);
		gBS->FreePool((void *)new_setup->memory_desc);
		gBS->AllocatePool(EfiRuntimeServicesData, new_setup->map_size,
				  (void **)&new_setup->memory_desc);
		if (EFI_ERROR(status)) {
			Print(L"failed get memory map = %r\r\n", status);
			new_setup->map_size += new_setup->desc_size * 8;
		}
	} while (status == EFI_BUFFER_TOO_SMALL);

	size = new_setup->map_size;
}

static UINT64 EFIAPI
__start_ap_wake_up(struct settings_new_environment *new_setup, UINT64 free_page)
{
	/*
	 * actually i can wake up every ap cores in here
	 * but i wanna be prove myself then i'm gonna be wake up all ap cores in
	 * vmm code
	 */
	EFI_MP_SERVICES_PROTOCOL *mp;
	UINT64 free, end_addr;
	UINTN num;

	gBS->LocateProtocol(&gEfiMpServiceProtocolGuid, NULL, (void **)&mp);

	free = free_page;

	mp->GetNumberOfProcessors(mp, &new_setup->processor_num,
				  &new_setup->enabled_processor_num);

	num = new_setup->processor_num;
	free = sizeof(struct processor_struct) * num;

#ifdef _DEBUG
	Print(L"processor number = %llu\r\n", new_setup->processor_num);
	Print(L"enabled processor number = %llu\r\n",
	      new_setup->enabled_processor_num);
#endif

	mp->WhoAmI(mp, &new_setup->bsp);
	Print(L"BSP = %llx\r\n", new_setup->bsp);

	/* now we should calc free page pages for next alloc to ap entry page
	 * !!!!! */
	end_addr = free_page + free;
	Print(L"jump free page = %lx\r\n",
	      ((end_addr + (PAGE_4KB - 1)) & PAGE_MASK));
	free_page = ((end_addr + (PAGE_4KB - 1)) & PAGE_MASK);

	free_page += PAGE_4KB;

	return free_page;
}

static UINT64 EFIAPI
__setup_vmm_parameters(struct settings_new_environment *new_setup,
		       struct init_vmm_param *parameters)
{
	UINT64 j;
	volatile UINT32 *apic_address_saver;
	UINT8 *madt_entry, *madt_end;
	EFI_ACPI_6_6_MULTIPLE_APIC_DESCRIPTION_TABLE_HEADER *madt_addr;

	parameters->vmm_stack =
		(UINT64)0x00400000 + (new_setup->vmm_stack - new_setup->vmm);
	parameters->ap_entry_page = new_setup->ap_entry_page;
	Print(L"enabled processor num = %llx\r\n",
	      new_setup->enabled_processor_num);
	Print(L"processor num = %llx\r\n", new_setup->processor_num);
	parameters->core_num = new_setup->enabled_processor_num;
	madt_addr = __get_apic(new_setup); // should be complete

	new_setup->current_free_page += PAGE_4KB;

#ifdef _DEBUG
	Print(L"\n\MADT ADDRESS IN UEFI = %llx\r\n", madt_addr);
	Print(L"MADT SIGNATURE = %lx\r\n", madt_addr->Header.Signature);
	Print(L"MADT LENGTH = %lx\r\n", madt_addr->Header.Length);
	Print(L"APIC ADDRESS = %lx\r\n", madt_addr->LocalApicAddress);
	Print(L"FLAGS = %lx\r\n\n", madt_addr->Flags);
#endif

	madt_entry = (UINT8*)madt_addr +
			  sizeof(EFI_ACPI_6_6_MULTIPLE_APIC_DESCRIPTION_TABLE_HEADER);
	madt_end = (UINT8*)((UINT8*)madt_addr + madt_addr->Header.Length);

	apic_address_saver = (volatile UINT32 *)((
		UINT64)(madt_addr->LocalApicAddress + 0x20));

	j = AsmReadMsr64(0x1b);

	Print(L"APIC BASE MSR = %lx\r\n", j);
	Print(L"Is BSP? = %s\r\n", (j & (1 << 8)) ? L"YES" : L"NO");
	
	Print(L"APIC Global Enable? = %s\r\n",
	      (j & (1 << 11)) ? L"YES" : L"NO");

	j = 0;
	j |= (1 << 11); // x2apic enable

	AsmWriteMsr64(0x1b, j);
	Print(L"APIC BASE MSR (After) = %lx\r\n", AsmReadMsr64(0x1b));
	
	j = 0;
	while (madt_entry < madt_end) {
		EFI_ACPI_6_6_PROCESSOR_LOCAL_APIC_STRUCTURE *local_apic =
			(EFI_ACPI_6_6_PROCESSOR_LOCAL_APIC_STRUCTURE *)
				madt_entry;

		if (local_apic->Length == 0) {
			Print(L"MADT LENGTH = %lx\r\n", local_apic->Length);
			break;
		} else {
			Print(L"MADT ADDRESS %lx\r\n", madt_entry);
			Print(L"MADT LENGTH = %lx\r\n", local_apic->Length);
		}

		if (local_apic->Type == 0 && local_apic->Flags & 1) {
			Print(L"Legacy APIC ID (type0) = %lx\r\n",
			      local_apic->ApicId);
			parameters->apic_id[j] = (UINT64)local_apic->ApicId;
			++j;
		}
		if (local_apic->Type == 9) {
			EFI_ACPI_6_6_PROCESSOR_LOCAL_X2APIC_STRUCTURE
				*local_x2apic =
				(EFI_ACPI_6_6_PROCESSOR_LOCAL_X2APIC_STRUCTURE
					 *)madt_entry;
			if (local_x2apic->Flags & 1) {
				Print(L"APIC ID = %lx\r\n",
				      local_x2apic->X2ApicId);
				parameters->apic_id[j] =
					(UINT64)local_x2apic->X2ApicId;
				++j;
			}
		}
		madt_entry += local_apic->Length;
		parameters->core_num = j;
	}

	Print(L"\r\nPROCESSOR NUM = %llx\r\n", parameters->core_num);

	__setup_apic_page_table(
		new_setup,
		madt_addr->LocalApicAddress); // complete identity

	/*CopyMem((void *)parameters->gop, (void *)context->efi_gop,
		sizeof(EFI_GRAPHICS_OUTPUT_PROTOCOL));*/

	gBS->AllocatePages(AllocateAnyPages, EfiRuntimeServicesCode, 0x8000,
			   &parameters->extra_memory_pa);

	SetMem((void *)parameters->extra_memory_pa, 0x8000, 0);

	//////////////////////////////////////////////////////////////////////////////
	///identity mapping for extra memory address!
	__extra_mapping(new_setup, parameters->extra_memory_pa);
	///////////////////////////
	/////////////////////////////////////////////////////

	parameters->pml4 =
		(UINT64)0x00400000 +
		((EFI_PHYSICAL_ADDRESS)new_setup->pml4 - new_setup->vmm);

	__print_vmm_parameters(parameters);

	return 1;
}

static EFI_ACPI_6_6_MULTIPLE_APIC_DESCRIPTION_TABLE_HEADER *
__get_apic(struct settings_new_environment *new_setup)
{
	EFI_ACPI_6_6_MULTIPLE_APIC_DESCRIPTION_TABLE_HEADER *madt_address;
	EFI_CONFIGURATION_TABLE *table;
	EFI_ACPI_6_6_ROOT_SYSTEM_DESCRIPTION_POINTER *rsdp;
	EFI_ACPI_DESCRIPTION_HEADER *xsdt;

	table = gST->ConfigurationTable;
	for (UINT64 i = 0; i < gST->NumberOfTableEntries; ++i) {
		if (CompareGuid(&table[i].VendorGuid, &gEfiAcpi20TableGuid)) {
			rsdp = (EFI_ACPI_6_6_ROOT_SYSTEM_DESCRIPTION_POINTER *)
				       table[i]
					       .VendorTable;
			xsdt = (EFI_ACPI_DESCRIPTION_HEADER *)rsdp->XsdtAddress;
		}
	}

	if (rsdp == NULL || xsdt == NULL) {
		return NULL;
	}

	madt_address = __get_madt(new_setup, xsdt);
	if (madt_address == NULL) {
		return NULL;
	}

	return madt_address;
}

static EFI_ACPI_6_6_MULTIPLE_APIC_DESCRIPTION_TABLE_HEADER *
__get_madt(struct settings_new_environment *new_setup,
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
			return (EFI_ACPI_6_6_MULTIPLE_APIC_DESCRIPTION_TABLE_HEADER
					*)h;
		}
	}

	return NULL;
}

static void EFIAPI __print_vmm_parameters(struct init_vmm_param *parameters)
{

	Print(L"vmm stack = 0x%llx\r\n", parameters->vmm_stack);
	Print(L"extra memory entry = 0x%llx\r\n", parameters->extra_memory_pa);
	Print(L"ap entry page = 0x%llx\r\n", parameters->ap_entry_page);
	Print(L"pml4 = 0x%llx\r\n", parameters->pml4);
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

	state->gdt_limit = desc.limit;
	state->gdt_offset = desc.offset;

	ZeroMem((void *)&desc, sizeof(struct descriptor_register));
	AsmReadIdtr((IA32_DESCRIPTOR *)&desc);
	state->idtr.limit = desc.limit;
	state->idtr.offset = desc.offset;

	state->idt_limit = desc.limit;
	state->idt_offset = desc.offset;
}

// static void EFIAPI __set_msr_etc(struct uefi_state_struct *state)
// {

// }
