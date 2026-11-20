#include <Uefi.h>
#include <PiDxe.h>
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

static VOID EFIAPI SetControlRegisters(UEFI_STATE_STRUCT *State);
static VOID EFIAPI SetSegmentRegisters(UEFI_STATE_STRUCT *State);
static VOID EFIAPI GetGdtrIdtr(UEFI_STATE_STRUCT *State);

static UINT64 EFIAPI VmmMapping(SETTINGS_NEW_ENVIRONMENT *NewSetup,
				UINT64 FreePage);
static VOID EFIAPI ExtraMapping(SETTINGS_NEW_ENVIRONMENT *NewSetup,
				EFI_PHYSICAL_ADDRESS ExtraMemoryPa);

static VOID EFIAPI Print2Mb(SETTINGS_NEW_ENVIRONMENT *NewSetup);
static VOID EFIAPI Print4Kb(SETTINGS_NEW_ENVIRONMENT *NewSetup);

// -----------------
static VOID EFIAPI InitVmmEnvironment(SETTINGS_NEW_ENVIRONMENT *NewSetup);
static UINT64 EFIAPI StartSetup(SETTINGS_NEW_ENVIRONMENT *NewSetup,
			      UEFI_STATE_STRUCT *UefiState);

static UINT64 EFIAPI SetupGdt(SETTINGS_NEW_ENVIRONMENT *NewSetup,
			      UINT64 FreePage);
//static VOID EFIAPI SetupTssX86X64(SETTINGS_NEW_ENVIRONMENT *NewSetup);
//static VOID EFIAPI SetupTss2Gdt(SETTINGS_NEW_ENVIRONMENT *NewSetup);

static UINT64 EFIAPI SetupPageTable(SETTINGS_NEW_ENVIRONMENT *NewSetup,
				    UINT64 FreePage);

static UINT64 EFIAPI SetupMemoryMap(SETTINGS_NEW_ENVIRONMENT *NewSetup);

static UINT64 EFIAPI StartApWakeUp(SETTINGS_NEW_ENVIRONMENT *NewSetup,
				   UINT64 FreePage);

static UINT64 EFIAPI InitVmmParameters(SETTINGS_NEW_ENVIRONMENT *NewSetup,
					INIT_VMM_PARAM *VmmParameters);

static EFI_ACPI_6_6_MULTIPLE_APIC_DESCRIPTION_TABLE_HEADER *
GetApic(SETTINGS_NEW_ENVIRONMENT *NewSetup);
static EFI_ACPI_6_6_MULTIPLE_APIC_DESCRIPTION_TABLE_HEADER *
GetMadt(SETTINGS_NEW_ENVIRONMENT *NewSetup,
	EFI_ACPI_DESCRIPTION_HEADER *MadtAddress);

static VOID EFIAPI PrintVmmParameters(SETTINGS_NEW_ENVIRONMENT *NewSetup,
				      INIT_VMM_PARAM *Parameters);

static EFI_FILE_PROTOCOL EFIAPI *OpenVmmBinary(EFI_HANDLE ImageHandle);
static UINTN EFIAPI GetVmmSize(EFI_FILE_PROTOCOL *VmmImg);
static EFI_STATUS EFIAPI ReadVmmBinary(SETTINGS_NEW_ENVIRONMENT *NewSetup,
				       EFI_FILE_PROTOCOL *VmmImg);

static VOID EFIAPI StartUefiSetup(UEFI_STATE_STRUCT *UefiState);

SETTINGS_NEW_ENVIRONMENT *CreateNewEnvironment(VOID)
{
	SETTINGS_NEW_ENVIRONMENT *NewSetup;
	UINTN Size;
	EFI_STATUS Status;

	Size = sizeof(SETTINGS_NEW_ENVIRONMENT);
	Status = gBS->AllocatePool(EfiRuntimeServicesData, Size, (VOID **)&NewSetup);
	if (EFI_ERROR(Status)) {
		Print(L"Failed AllocatePool for NewSetup = %r\r\n", Status);
		return NULL;
	}

	ZeroMem((VOID *)NewSetup, Size);

	NewSetup->Init = InitVmmEnvironment;
	NewSetup->Start = StartSetup;
	NewSetup->SetGdt = SetupGdt;
//	NewSetup->SetTss = SetupTssX86X64;
	NewSetup->SetPageTable = SetupPageTable;

	NewSetup->Done = InitVmmParameters;

	NewSetup->OpenVmm = OpenVmmBinary;
	NewSetup->VmmSize = GetVmmSize;
	NewSetup->ReadVmm = ReadVmmBinary;

	NewSetup->GetMemoryMap = SetupMemoryMap;

	NewSetup->StartAps = StartApWakeUp;

	return NewSetup;
}

static VOID EFIAPI InitVmmEnvironment(SETTINGS_NEW_ENVIRONMENT *NewSetup)
{
	EFI_STATUS Status;
	UINT64 EnterVmmAddress;

	NewSetup->EnterVmmAddr = 0x3fffff;
	Status = gBS->AllocatePages(AllocateMaxAddress, EfiRuntimeServicesCode,
				    1, &NewSetup->EnterVmmAddr);
	if (EFI_ERROR(Status)) {
		Print(L"failed allocate for identity mapping = %r\r\n", Status);
	}
	EnterVmmAddress = (UINT64)EnterVmm;

	CopyMem((VOID *)NewSetup->EnterVmmAddr, (VOID *)EnterVmmAddress,
		PAGE_4KB);

	NewSetup->ApEntryPage = 0xfffff;
	Status = gBS->AllocatePages(AllocateMaxAddress, EfiRuntimeServicesData,
				    1, &NewSetup->ApEntryPage);
	if (EFI_ERROR(Status)) {
		Print(L"failed allocate page for ap entry = %r\r\n", Status);
	} else {
		ZeroMem((VOID *)NewSetup->ApEntryPage, PAGE_4KB);
		Print(L"ap entry page address = %lx\r\n",
		      NewSetup->ApEntryPage);
	}
}

static EFI_FILE_PROTOCOL EFIAPI *OpenVmmBinary(EFI_HANDLE ImageHandle)
{
	EFI_STATUS Status;
	EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Simple;
	EFI_FILE_PROTOCOL *Root, *VmmBin;
	EFI_LOADED_IMAGE_PROTOCOL *LoadedImg;

	Status = gBS->HandleProtocol(ImageHandle, &gEfiLoadedImageProtocolGuid,
				     (VOID **)&LoadedImg);
	if (EFI_ERROR(Status)) {
		Print(L"Failed to HandleProtocol for Simple File System = "
		      L"%r\r\n",
		      Status);
		return NULL;
	}
	Print(L"ImageBase Address = %llx\r\n", LoadedImg->ImageBase);

	Status = gBS->HandleProtocol(LoadedImg->DeviceHandle,
				     &gEfiSimpleFileSystemProtocolGuid,
				     (VOID **)&Simple);
	if (EFI_ERROR(Status)) {
		Print(L"Failed to SimpleFileSystem = %r\r\n", Status);
		return NULL;
	}

	Status = Simple->OpenVolume(Simple, &Root);
	if (EFI_ERROR(Status)) {
		Print(L"Failed to OpenVolume = %r\r\n", Status);
		return NULL;
	}

	Status = Root->Open(Root, &VmmBin, L"vmm.bin",
			    EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0);
	if (EFI_ERROR(Status)) {
		Print(L"Failed to Open vmm.bin = %r\r\n", Status);
		VmmBin = NULL;
	}
	Print(L"successed for open vmm binary\r\n");

	return VmmBin;
}

static EFI_STATUS EFIAPI ReadVmmBinary(SETTINGS_NEW_ENVIRONMENT *NewSetup,
				       EFI_FILE_PROTOCOL *VmmImg)
{
	EFI_STATUS Status;
	UINTN TotalPages;

	VmmImg->SetPosition(VmmImg, 0);
	NewSetup->VmmBinSize = (UINT64)GetVmmSize(VmmImg);
	if (NewSetup->VmmBinSize == 0) {
		Status = EFI_NOT_READY;
		return Status;
	}

	TotalPages = EFI_SIZE_TO_PAGES(NewSetup->VmmBinSize);
	Print(L"How many need pages = %llx\r\n", TotalPages);
	NewSetup->VmmPages = TotalPages;
	TotalPages = ((((TotalPages + 511) >> 9) * 512) + 512) >> 1;
	NewSetup->Capacity = TotalPages * PAGE_4KB;

	if (NewSetup->Capacity - (NewSetup->VmmPages * PAGE_4KB) < 0x1a000) {
		TotalPages *= 2;
	}

	Print(L"TOTAL SIZE = 0x%llx\r\n", TotalPages);
	Print(L"TOTAL ALLOCATE SIZE = 0x%llx\r\n", NewSetup->Capacity);

	Status = gBS->AllocatePages(AllocateAnyPages, EfiRuntimeServicesCode,
				    TotalPages, &NewSetup->Vmm);
	if (EFI_ERROR(Status)) {
		Print(L"failed to allocate pages for vmm = %r\r\n", Status);
		return Status;
	}
	ZeroMem((VOID *)NewSetup->Vmm, NewSetup->Capacity);
	Print(L"address of vmm = 0x%llx\r\n", NewSetup->Vmm);

	Status = VmmImg->Read(VmmImg, &NewSetup->VmmBinSize,
			      (VOID *)NewSetup->Vmm);
	Print(L"RESIZE VMM BIN SIZE = %llx\r\n", NewSetup->VmmBinSize);
	if (NewSetup->VmmBinSize & 0xfff) {
		NewSetup->VmmBinSize = (NewSetup->VmmBinSize + 0xfff) & ~0xfff;
		Print(L"ALIGN VMM BIN SIZE = %llx\r\n", NewSetup->VmmBinSize);
	}
	if (EFI_ERROR(Status)) {
		Print(L"failed to read for vmm binary = %r\r\n", Status);
		gBS->FreePages(NewSetup->Vmm, TotalPages);
		return Status;
	}

	return Status;
}

static UINTN EFIAPI GetVmmSize(EFI_FILE_PROTOCOL *VmmImg)
{
	EFI_STATUS Status;
	EFI_FILE_INFO *FileInfo;
	UINTN BinSize = 0;

	Status = VmmImg->GetInfo(VmmImg, &gEfiFileInfoGuid, &BinSize, NULL);
	if (Status == EFI_BUFFER_TOO_SMALL) {
		Status = gBS->AllocatePool(EfiLoaderData, BinSize, (VOID **)&FileInfo);
		if (EFI_ERROR(Status)) {
			return 0;
		}
	}

	Status = VmmImg->GetInfo(VmmImg, &gEfiFileInfoGuid, &BinSize,
				 (VOID *)FileInfo);
	if (EFI_ERROR(Status)) {
		gBS->FreePool(FileInfo);
		return 0;
	}

	Print(L"VMM BINARY NAME = %s\r\n", FileInfo->FileName);
	Print(L"VMM BINARY SIZE = 0x%llx\r\n", FileInfo->FileSize);

	BinSize = FileInfo->FileSize;
	gBS->FreePool(FileInfo);
	return BinSize;
}

static UINT64 EFIAPI StartSetup(SETTINGS_NEW_ENVIRONMENT *NewSetup,
			      UEFI_STATE_STRUCT *UefiState)
{
	UINT64 FreePage;
	INIT_VMM_PARAM *Parameters = (INIT_VMM_PARAM *)(NewSetup->Vmm + 0x10);

	FreePage = ((NewSetup->Vmm + (NewSetup->VmmPages * PAGE_4KB)) &
		    ~0xfffULL);
	NewSetup->VmmStack = FreePage;
	NewSetup->VmmStackSize = 16 * PAGE_4KB;

	FreePage += NewSetup->VmmStackSize;

	Print(L"\r\n\r\n\r\n");

	Print(L"free page address = 0x%lx\r\n", FreePage);

	Print(L"\r\n\r\n\r\n");

//	NewSetup->SetTss(NewSetup);
	FreePage = NewSetup->SetGdt(NewSetup, FreePage);

	FreePage = NewSetup->SetPageTable(NewSetup, FreePage);
	if (FreePage == 0) {
		return 0;
	}

	FreePage = NewSetup->StartAps(NewSetup, FreePage);

	NewSetup->CurrentFreePage = FreePage;
	Print(L"NewSetup->VMM = 0x%llx\r\n", NewSetup->Vmm);
	Print(L"how many used free page = %llx\r\n",
	      (NewSetup->CurrentFreePage - NewSetup->Vmm) >> 12);
	NewSetup->UsedFreePages = (NewSetup->CurrentFreePage - NewSetup->Vmm) >> 12;
	Print(L"USED FREE PAGES = 0x%llx\r\n", NewSetup->UsedFreePages);

	if (NewSetup->GetMemoryMap(NewSetup) == 0) {
		return 0;
	}

	Parameters->UefiStateAddress = (EFI_PHYSICAL_ADDRESS)UefiState;
	InitVmmParameters(NewSetup, Parameters);

	return 1;
}

static UINT64 EFIAPI SetupGdt(SETTINGS_NEW_ENVIRONMENT *NewSetup,
			      UINT64 FreePage)
{
	UINT64 GdtAddrVa, GdtrAddr;
	UINT64 GdtOffsetVa;

	GdtAddrVa = FreePage;
	NewSetup->Gdt = (UINT64 *)GdtAddrVa;
	Print(L"gdt addr = 0x%lx\r\n", NewSetup->Gdt);
	NewSetup->Gdt[0] = X86_NULL_DESCRIPTOR;
	NewSetup->Gdt[1] = X86_KERNEL_CODE_SEGMENT;
	NewSetup->Gdt[2] = X86_KERNEL_DATA_SEGMENT;
	NewSetup->Gdt[3] = X86_USER_CODE_SEGMENT;
	NewSetup->Gdt[4] = X86_USER_DATA_SEGMENT;
	NewSetup->Gdt[5] = 0;
	NewSetup->Gdt[6] = X64_NULL_DESCRIPTOR;
	NewSetup->Gdt[7] = X64_KERNEL_CODE_SEGMENT;
	NewSetup->Gdt[8] = X64_KERNEL_DATA_SEGMENT;
	NewSetup->Gdt[9] = X64_USER_CODE_SEGMENT;
	NewSetup->Gdt[10] = X64_USER_DATA_SEGMENT;
	NewSetup->Gdt[11] = 0;
//	SetupTss2Gdt(NewSetup);

	for (UINT64 I = 0; I < 12; ++I) {
		Print(L"GDT[%llu] = 0x%lx, addr = 0x%lx\r\n", I,
		      NewSetup->Gdt[I], &NewSetup->Gdt[I]);
	}

	GdtOffsetVa = (UINT64)0x00400000 + (GdtAddrVa - NewSetup->Vmm);
	GdtrAddr = GdtAddrVa + 112;

	NewSetup->Gdtr = (DESCRIPTOR_REGISTER *)GdtrAddr;

	Print(L"new_gdt_offset = %llx\r\n", GdtOffsetVa);
	Print(L"gdt_address = %llx\r\n", GdtAddrVa);
	Print(L"gdtr addr = 0x%lx\r\n", (UINT64)NewSetup->Gdtr);

	CopyMem((VOID *)GdtOffsetVa, (VOID *)GdtAddrVa, sizeof(UINT64) * 14);

	NewSetup->Gdtr->Offset = GdtOffsetVa;
	NewSetup->Gdtr->Limit = (UINT16)(sizeof(UINT64) * 14) - 1;

	Print(L"gdtr->offset = 0x%lx\r\n", NewSetup->Gdtr->Offset);
	Print(L"gdtr->limit = 0x%lx\r\n", NewSetup->Gdtr->Limit);

	Print(L"\r\n\r\n\r\n");

	FreePage += PAGE_4KB;
	return FreePage;
}

/*
static VOID EFIAPI SetupTssX86X64(SETTINGS_NEW_ENVIRONMENT *NewSetup)
{
	ZeroMem((VOID *)&NewSetup->Tss32, sizeof(TASK_STATE_SEGMENT32));
	ZeroMem((VOID *)&NewSetup->Tss64, sizeof(TASK_STATE_SEGMENT64));

	NewSetup->Tss32.Cr3 = (UINT64)NewSetup->Pml4 >> 32;
	NewSetup->Tss32.Eflags = 0x2;
	NewSetup->Tss32.IoMapBaseAddr = sizeof(TASK_STATE_SEGMENT32);

	NewSetup->Tss64.Rsp0 =
		(UINT64)NewSetup->VmmStack + NewSetup->VmmStackSize;
	NewSetup->Tss64.IoMapBaseAddr = sizeof(TASK_STATE_SEGMENT64);
}

static VOID EFIAPI SetupTss2Gdt(SETTINGS_NEW_ENVIRONMENT *NewSetup)
{
	UINT64 TssAddr;

	TssAddr = (UINT64)&NewSetup->Tss32;
	NewSetup->Gdt[5] = (sizeof(TASK_STATE_SEGMENT32) - 1) & 0xffff;
	NewSetup->Gdt[5] |= (UINT64)(TssAddr & 0xffffff) << 16;
	NewSetup->Gdt[5] |= (0x89ULL << 40);
	NewSetup->Gdt[5] |= (UINT64)(TssAddr & 0xff000000) << 32;

	TssAddr = (UINT64)&NewSetup->Tss64;
	Print(L"tss address = 0x%lx\r\n", TssAddr);
	NewSetup->Gdt[11] = (sizeof(TASK_STATE_SEGMENT64) - 1) & 0xffff;
	NewSetup->Gdt[11] |= (UINT64)(TssAddr & 0xffffff) << 16;
	NewSetup->Gdt[11] |= (0x89ULL << 40);
	NewSetup->Gdt[11] |= (UINT64)(TssAddr & 0xff000000) << 32;
	NewSetup->Gdt[12] = 0;
}
*/

static UINT64 EFIAPI SetupPageTable(SETTINGS_NEW_ENVIRONMENT *NewSetup,
				    UINT64 FreePage)
{
	NewSetup->Pml4 = (UINT64 *)FreePage;
	FreePage += PAGE_4KB;
	NewSetup->Pdpte = (UINT64 *)FreePage;
	FreePage += PAGE_4KB;
	NewSetup->Pde = (UINT64 *)FreePage;
	FreePage += PAGE_4KB;

	ZeroMem((VOID *)NewSetup->Pml4, PAGE_4KB);
	ZeroMem((VOID *)NewSetup->Pdpte, PAGE_4KB);
	ZeroMem((VOID *)NewSetup->Pde, PAGE_4KB);

	/* 0 ~ 4mb */
	for (UINT64 i = 0; i < 2; ++i) {
		NewSetup->Pde[i] = (i * PAGE_2MB);
		NewSetup->Pde[i] |= PDE_FLAGS_MASK;
	}

	FreePage = VmmMapping(NewSetup, FreePage);
	if (FreePage == 0) {
		return 0;
	}

	NewSetup->Pdpte[0] = (UINT64)NewSetup->Pde | BASIC_FLAGS_MASK;
	NewSetup->Pml4[0] = (UINT64)NewSetup->Pdpte | BASIC_FLAGS_MASK;
	Print(L"\r\n\r\n");

	Print(L"\r\n\r\n");
	Print4Kb(NewSetup);

#ifdef _DEBUG
	Print(L"pdpte = 0x%llx\r\n", NewSetup->Pdpte);
	Print(L"pdpte[0] = 0x%llx\r\n", NewSetup->Pdpte[0]);
	Print(L"pml4[0] = 0x%llx\r\n", NewSetup->Pml4[0]);
	Print(L"pml4 = 0x%llx\r\n", NewSetup->Pml4);
#endif

	gST->ConOut->ClearScreen(gST->ConOut);
	Print(L"\n\n");

	gBS->Stall(30000);

	return FreePage;
}


static UINT64 EFIAPI VmmMapping(SETTINGS_NEW_ENVIRONMENT *NewSetup,
				UINT64 FreePage)
{
	UINT64 *Pte, Vector, j, i;

	Vector = EFI_SIZE_TO_PAGES(NewSetup->Capacity);
	Print(L"vmm image size to pages = %llx\r\n", Vector);

	Vector = (Vector + (512 - 1)) >> 9;
	Print(L"Pages = %llx\r\n", Vector);

	if ((NewSetup->Capacity & 0xfff) == 0) {
		for (i = 0; i < Vector; ++i) {
			Print(L"Mapping %llx\r\n", i);
			Pte = (UINT64 *)FreePage;
			FreePage += PAGE_4KB;
			for (j = 0; j < 512; ++j) {
				Pte[j] = (NewSetup->Vmm + (i * PAGE_2MB) +
					  (j * PAGE_4KB)) |
					 BASIC_FLAGS_MASK;
			}
			NewSetup->Pde[i + 2] = (UINT64)Pte | BASIC_FLAGS_MASK;
			Print(L"PDE[%llx] = 0x%llx\r\n", (i + 2),
			      NewSetup->Pde[i + 2]);
		}
	} else {
		return 0;
	}

	gST->ConOut->ClearScreen(gST->ConOut);
	Print(L"\n\n");

	gBS->Stall(3000);

	return FreePage;
}

static VOID EFIAPI ExtraMapping(SETTINGS_NEW_ENVIRONMENT *NewSetup,
				EFI_PHYSICAL_ADDRESS ExtraMemoryPa)
{
	UINT64 i, j, *Pte, Vector;

	j = EFI_SIZE_TO_PAGES(NewSetup->Capacity);
	Print(L"vmm image size to pages = %llx\r\n", j);
	Vector = (UINT64)0x8000 >> 9;

	j = (j + (512 - 1)) >> 9;
	j += 2;
	Print(L"Pages = %llx\r\n", j);

	Vector += j;
	Print(L"Vector = %llx\r\n", Vector);
	for (i = j; i < Vector; ++i) {
		Pte = (UINT64 *)NewSetup->CurrentFreePage;
		NewSetup->CurrentFreePage += PAGE_4KB;
		Print(L"CURRENT FREE PAGES = %llx\r\n",
		      NewSetup->CurrentFreePage);
		for (j = 0; j < 512; ++j) {
			Pte[j] = (UINT64)((ExtraMemoryPa) | BASIC_FLAGS_MASK);
			ExtraMemoryPa += PAGE_4KB;
			if (j > 510) {
			}
		}
		NewSetup->Pde[i] = (UINT64)Pte | BASIC_FLAGS_MASK;
	}

	Print2Mb(NewSetup);
}

static VOID EFIAPI Print2Mb(SETTINGS_NEW_ENVIRONMENT *NewSetup)
{
	UINT64 i;

	for (i = 0; i < 68; ++i) {
		Print(L"PDE[%llx] = 0x%llx\r\n", i, NewSetup->Pde[i]);
	}

	Print(L"\r\n\r\n");
}

static VOID EFIAPI Print4Kb(SETTINGS_NEW_ENVIRONMENT *NewSetup)
{
}

UINT64 EFIAPI SetupMemoryMap(SETTINGS_NEW_ENVIRONMENT *NewSetup)
{
	EFI_STATUS Status;
	UINTN MapKey;

	Status = gBS->AllocatePool(EfiRuntimeServicesData,
			  sizeof(EFI_MEMORY_DESCRIPTOR) * 8,
			  (VOID **)&NewSetup->MemoryDesc);
	if (EFI_ERROR(Status)) {
		Print(L"Failed to AllocatePool for MemoryMap = %r\r\n", Status);
		return 0;
	}

	do {
		Status = gBS->GetMemoryMap(&NewSetup->MapSize,
					   NewSetup->MemoryDesc, &MapKey,
					   &NewSetup->DescSize, NULL);
		if (Status == EFI_BUFFER_TOO_SMALL) {
			gBS->FreePool((VOID *)NewSetup->MemoryDesc);
			gBS->AllocatePool(EfiRuntimeServicesData, NewSetup->MapSize,
					  (VOID **)&NewSetup->MemoryDesc);
			if (EFI_ERROR(Status)) {
				Print(L"failed get memory map = %r\r\n", Status);
				NewSetup->MapSize += NewSetup->DescSize * 8;
			}
		}
	} while (Status == EFI_BUFFER_TOO_SMALL);

	return 1;
}

static UINT64 EFIAPI StartApWakeUp(SETTINGS_NEW_ENVIRONMENT *NewSetup,
				   UINT64 FreePage)
{
	EFI_MP_SERVICES_PROTOCOL *Mp;
	UINT64 Free, EndAddr;
	UINTN Num;

	gBS->LocateProtocol(&gEfiMpServiceProtocolGuid, NULL, (VOID **)&Mp);

	Free = FreePage;

	Mp->GetNumberOfProcessors(Mp, &NewSetup->ProcessorNum,
				  &NewSetup->EnabledProcessorNum);

	Num = NewSetup->ProcessorNum;
	Free = sizeof(PROCESSOR_STRUCT) * Num;

#ifdef _DEBUG
	Print(L"processor number = %llu\r\n", NewSetup->ProcessorNum);
	Print(L"enabled processor number = %llu\r\n",
	      NewSetup->EnabledProcessorNum);
#endif

	Mp->WhoAmI(Mp, &NewSetup->Bsp);
	Print(L"BSP = %llx\r\n", NewSetup->Bsp);

	EndAddr = FreePage + Free;
	Print(L"jump free page = %lx\r\n",
	      ((EndAddr + (PAGE_4KB - 1)) & PAGE_MASK));
	FreePage = ((EndAddr + (PAGE_4KB - 1)) & PAGE_MASK);

	FreePage += PAGE_4KB;

	return FreePage;
}

static UINT64 EFIAPI InitVmmParameters(SETTINGS_NEW_ENVIRONMENT *NewSetup,
					INIT_VMM_PARAM *Parameters)
{
	EFI_STATUS Status;
	UINT64 i;
	UINT8 *MadtEntry, *MadtEnd;
	EFI_ACPI_6_6_MULTIPLE_APIC_DESCRIPTION_TABLE_HEADER *MadtAddr;

	Parameters->VmmEntry = NewSetup->Vmm;
	Parameters->VmmStack = NewSetup->VmmStack; // phy address
	Parameters->VmmStackVa =
		(UINT64)0x00400000 +
		(NewSetup->VmmStack - NewSetup->Vmm); // virtual address

	Parameters->ExtraMemoryEntryVa =
		((UINT64)0x00400000 + NewSetup->Capacity);

	Parameters->SubStackEntryVa += (UINT64)0x00400000 + NewSetup->VmmStackSize;

	i = Parameters->SubStackEntryVa & 0xfffff;

	Parameters->SubStackPages = i >> 12;
	Parameters->Bsp = NewSetup->Bsp;

	Parameters->ApEntryPage = NewSetup->ApEntryPage;

	Print(L"enabled processor num = %llx\r\n",
	      NewSetup->EnabledProcessorNum);
	Print(L"processor num = %llx\r\n", NewSetup->ProcessorNum);
	Parameters->CoreNum = NewSetup->EnabledProcessorNum;

	Status = gBS->AllocatePool(EfiRuntimeServicesData,
				   Parameters->CoreNum * sizeof(UINT64),
				   (VOID **)&Parameters->ApicId);
	if (EFI_ERROR(Status)) {
		Print(L"APIC ID ALLOCATE FAILED = %r\r\n", Status);
		return 0;
	}
	MadtAddr = GetApic(NewSetup);

	NewSetup->CurrentFreePage += PAGE_4KB;

#ifdef _DEBUG
	Print(L"\nMADT ADDRESS IN UEFI = %llx\r\n", MadtAddr);
	Print(L"MADT SIGNATURE = %lx\r\n", MadtAddr->Header.Signature);
	Print(L"MADT LENGTH = %lx\r\n", MadtAddr->Header.Length);
	Print(L"APIC ADDRESS = %lx\r\n", MadtAddr->LocalApicAddress);
	Print(L"FLAGS = %lx\r\n\n", MadtAddr->Flags);
#endif

	MadtEntry = (UINT8 *)MadtAddr +
		    sizeof(EFI_ACPI_6_6_MULTIPLE_APIC_DESCRIPTION_TABLE_HEADER);
	MadtEnd = (UINT8 *)((UINT8 *)MadtAddr + MadtAddr->Header.Length);

	i = AsmReadMsr64(0x1b);

	i |= (1 << 10);
	AsmWriteMsr64(0x1b, i);
	Print(L"APIC BASE MSR (After) = %lx\r\n", AsmReadMsr64(0x1b));

	i = 0;
	while (MadtEntry < MadtEnd) {
		EFI_ACPI_6_6_PROCESSOR_LOCAL_APIC_STRUCTURE *LocalApic =
			(EFI_ACPI_6_6_PROCESSOR_LOCAL_APIC_STRUCTURE *)
				MadtEntry;

		if (LocalApic->Length == 0) {
			Print(L"MADT LENGTH = %lx\r\n", LocalApic->Length);
			break;
		} else {
			Print(L"MADT ADDRESS %lx\r\n", MadtEntry);
			Print(L"MADT LENGTH = %lx\r\n", LocalApic->Length);
		}

		if (LocalApic->Type == 0 && LocalApic->Flags & 1) {
			Print(L"Legacy APIC ID (type0) = %lx\r\n",
			      LocalApic->ApicId);
			Parameters->ApicId[i] = (UINT64)LocalApic->ApicId;
			++i;
		}
		if (LocalApic->Type == 9) {
			EFI_ACPI_6_6_PROCESSOR_LOCAL_X2APIC_STRUCTURE
			*LocalX2Apic =
				(EFI_ACPI_6_6_PROCESSOR_LOCAL_X2APIC_STRUCTURE
					 *)MadtEntry;
			if (LocalX2Apic->Flags & 1) {
				Print(L"APIC ID = %lx\r\n",
				      LocalX2Apic->X2ApicId);
				Parameters->ApicId[i] =
					(UINT64)LocalX2Apic->X2ApicId;
				++i;
			}
		}
		MadtEntry += LocalApic->Length;
	}

	//SetupApicPageTable(NewSetup, MadtAddr->LocalApicAddress);

	Status = gBS->AllocatePages(AllocateAnyPages, EfiRuntimeServicesCode, 0x8000,
			   &Parameters->ExtraMemoryPa);

	if (EFI_ERROR(Status)) {
		Print(L"Failed AllocatePages for ExtraMemory !!!!!! = %r\r\n", Status);
		return 0;
	}
	SetMem((VOID *)Parameters->ExtraMemoryPa, 0x8000 * PAGE_4KB, 0);

	ExtraMapping(NewSetup, Parameters->ExtraMemoryPa);

	Parameters->Pml4 =
		(UINT64 *)((UINT64)0x00400000 + ((EFI_PHYSICAL_ADDRESS)NewSetup->Pml4 - NewSetup->Vmm));

	Parameters->Reserved0 = 0;
	Parameters->Reserved1 = 0;
	Parameters->Reserved2 = 0;
	PrintVmmParameters(NewSetup, Parameters);

	return 1;
}

static EFI_ACPI_6_6_MULTIPLE_APIC_DESCRIPTION_TABLE_HEADER *
GetApic(SETTINGS_NEW_ENVIRONMENT *NewSetup)
{
	EFI_ACPI_6_6_MULTIPLE_APIC_DESCRIPTION_TABLE_HEADER *MadtAddress;
	EFI_CONFIGURATION_TABLE *Table;
	EFI_ACPI_6_6_ROOT_SYSTEM_DESCRIPTION_POINTER *Rsdp = NULL;
	EFI_ACPI_DESCRIPTION_HEADER *Xsdt = NULL;

	Table = gST->ConfigurationTable;
	for (UINT64 I = 0; I < gST->NumberOfTableEntries; ++I) {
		if (CompareGuid(&Table[I].VendorGuid, &gEfiAcpi20TableGuid)) {
			Rsdp = (EFI_ACPI_6_6_ROOT_SYSTEM_DESCRIPTION_POINTER *)
				       Table[I]
					       .VendorTable;
			Xsdt = (EFI_ACPI_DESCRIPTION_HEADER *)Rsdp->XsdtAddress;
		}
	}

	if (Rsdp == NULL || Xsdt == NULL) {
		return NULL;
	}

	MadtAddress = GetMadt(NewSetup, Xsdt);
	if (MadtAddress == NULL) {
		return NULL;
	}

	return MadtAddress;
}

static EFI_ACPI_6_6_MULTIPLE_APIC_DESCRIPTION_TABLE_HEADER *
GetMadt(SETTINGS_NEW_ENVIRONMENT *NewSetup,
	EFI_ACPI_DESCRIPTION_HEADER *MadtAddress)
{
	UINT64 Entries, Len;
	UINT64 *Entry;

	Len = (UINT32)(*(UINT32 *)((UINT8 *)MadtAddress + 4));
	Print(L"LEN = %llx\r\n", Len);
	Entries = Len - sizeof(EFI_ACPI_DESCRIPTION_HEADER);
	Print(L"ENTRIES = %llx\r\n", Entries);

	Entry = (UINT64 *)(MadtAddress + 1);
	for (UINT64 I = 0; I < Entries; ++I) {
		EFI_ACPI_DESCRIPTION_HEADER *H =
			(EFI_ACPI_DESCRIPTION_HEADER *)(UINT64)Entry[I];

		if (H->Signature ==
		    EFI_ACPI_2_0_MULTIPLE_APIC_DESCRIPTION_TABLE_SIGNATURE) {
			return (EFI_ACPI_6_6_MULTIPLE_APIC_DESCRIPTION_TABLE_HEADER
					*)H;
		}
	}

	return NULL;
}

static VOID EFIAPI PrintVmmParameters(SETTINGS_NEW_ENVIRONMENT *NewSetup,
				      INIT_VMM_PARAM *Parameters)
{
	UINT64 i;
	Print(L"\r\n\r\n\r\n\r\n\r\n");
	Print(L"############################################################"
	      L"\r\n");
	Print(L"PHYSICAL ADDRESS PRINTING\r\n");
	Print(L"vmm stack = 0x%llx\r\n", Parameters->VmmStack);
	Print(L"extra memory entry = 0x%llx\r\n", Parameters->ExtraMemoryPa);
	Print(L"ap entry page = 0x%llx\r\n", Parameters->ApEntryPage);
	Print(L"pml4 = 0x%llx\r\n", Parameters->Pml4);
	Print(L"PHYSICAL ADDRESS PRINTING\r\n");
	Print(L"BSP = %llx\r\n", Parameters->Bsp);
	for (i = 0; i < Parameters->CoreNum; ++i) {
		Print(L"APIC ID[%llx] = %llx\r\n", i, Parameters->ApicId[i]);
	}
	Print(L"Enabled Processor = %llx\r\n", Parameters->CoreNum);
	Print(L"############################################################"
	      L"\r\n");
	Print(L"\r\n\r\n\r\n\r\n\r\n");

	Print(L"\r\n\r\n\r\n");
	Print(L"############################################################"
	      L"\r\n");
	Print(L"VIRTUAL ADDRESS PRINTING\r\n");
	Print(L"vmm stack address = %llx\r\n", Parameters->VmmStackVa);
	Print(L"extra memory entry = 0x%llx\r\n",
	      Parameters->ExtraMemoryEntryVa);
	Print(L"sub stack entry = 0x%llx\r\n", Parameters->SubStackEntryVa);
	Print(L"sub stack pages = %llx\r\n", Parameters->SubStackPages);
	Print(L"pml4 = 0x%llx\r\n", Parameters->Pml4);
	Print(L"BSP = %llx\r\n", Parameters->Bsp);
	for (i = 0; i < Parameters->CoreNum; ++i) {
		Print(L"APIC ID[%llx] = %llx\r\n",i, Parameters->ApicId[i]);
	}
	Print(L"Enabled Processor = %llx\r\n", Parameters->CoreNum);
	Print(L"PHYSICAL ADDRESS PRINTING\r\n");
	Print(L"############################################################"
	      L"\r\n");
	Print(L"\r\n\r\n\r\n\r\n\r\n");
}

UEFI_STATE_STRUCT *CreateUefiState(VOID)
{
	UEFI_STATE_STRUCT *UefiState;

	gBS->AllocatePool(EfiBootServicesData, sizeof(UEFI_STATE_STRUCT),
			  (VOID **)&UefiState);
	ZeroMem((VOID *)UefiState, sizeof(UEFI_STATE_STRUCT));

	UefiState->Start = StartUefiSetup;

	return UefiState;
}

static VOID EFIAPI StartUefiSetup(UEFI_STATE_STRUCT *State)
{
	EFI_MP_SERVICES_PROTOCOL *Mp;

	gBS->LocateProtocol(&gEfiMpServiceProtocolGuid, NULL, (VOID **)&Mp);

	Mp->GetNumberOfProcessors(Mp, &State->CoreNum, NULL);

	SetControlRegisters(State);
	SetSegmentRegisters(State);
	GetGdtrIdtr(State);

	State->RFlags = AsmReadEflags();
	State->Msr = AsmReadMsr64(0xC0000080);
	State->Rip = (UINT64)ExitVmm;
}

static VOID EFIAPI SetControlRegisters(UEFI_STATE_STRUCT *State)
{
	State->Cr0 = (UINT64)AsmReadCr0();
	State->Cr2 = (UINT64)AsmReadCr2();
	State->Cr3 = (UINT64)AsmReadCr3();
	State->Cr4 = (UINT64)AsmReadCr4();
	State->Dr7 = (UINT64)AsmReadDr7();
}

static VOID EFIAPI SetSegmentRegisters(UEFI_STATE_STRUCT *State)
{
	State->Cs.Selector = AsmReadCs();
	State->Ss.Selector = AsmReadSs();
	State->Ds.Selector = AsmReadDs();
	State->Es.Selector = AsmReadEs();
	State->Fs.Selector = AsmReadFs();
	State->Gs.Selector = AsmReadGs();
	State->Tr.Selector = AsmReadTr();
}

static VOID EFIAPI GetGdtrIdtr(UEFI_STATE_STRUCT *State)
{
	DESCRIPTOR_REGISTER Desc;

	ZeroMem((VOID *)&Desc, sizeof(DESCRIPTOR_REGISTER));
	AsmReadGdtr((IA32_DESCRIPTOR *)&Desc);
	State->Gdtr.Limit = Desc.Limit;
	State->Gdtr.Offset = Desc.Offset;

	State->GdtLimit = Desc.Limit;
	State->GdtOffset = Desc.Offset;

	ZeroMem((VOID *)&Desc, sizeof(DESCRIPTOR_REGISTER));
	AsmReadIdtr((IA32_DESCRIPTOR *)&Desc);
	State->Idtr.Limit = Desc.Limit;
	State->Idtr.Offset = Desc.Offset;

	State->IdtLimit = Desc.Limit;
	State->IdtOffset = Desc.Offset;
}