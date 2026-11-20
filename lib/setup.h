#ifndef _SETUP_H
#define _SETUP_H

#define PAGE_4KB 0x00001000
#define PAGE_MASK (~(PAGE_4KB - 1))

#define PAGE_2MB 0x00200000

#define BASIC_FLAGS_MASK 0x3
#define PDE_FLAGS_MASK 0x83

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
typedef struct {
	UINT16 Limit;
	UINT64 Offset;
} DESCRIPTOR_REGISTER;

typedef struct {
	UINT16 IsrLow;
	UINT16 KernelCs;
	UINT8 Ist;
	UINT8 Attributes;
	UINT16 IsrMid;
	UINT32 IsrHigh;
	UINT32 Reserved;
} IDT_ENTRY;
#pragma pack(pop)

typedef struct {
	UINT32 Reserved0;
	UINT16 Rsp0;
	UINT64 Rsp1;
	UINT64 Rsp2;
	UINT64 Reserved1;
	UINT64 Ist[7];
	UINT64 Reserved2;
	UINT16 Reserved3;
	UINT16 IopbBase;
} TSS_DESCRIPTOR;

#pragma pack(push, 4)
typedef struct {
	UINT32 PrevTaskLink;
	UINT32 Esp0;
	UINT32 Ss0;
	UINT32 Esp1;
	UINT32 Ss1;
	UINT32 Esp2;
	UINT32 Ss2;
	UINT32 Cr3;
	UINT32 Eip;
	UINT32 Eflags;
	UINT32 Eax;
	UINT32 Ecx;
	UINT32 Edx;
	UINT32 Ebx;
	UINT32 Esp;
	UINT32 Ebp;
	UINT32 Esi;
	UINT32 Edi;
	UINT32 Es;
	UINT32 Cs;
	UINT32 Ss;
	UINT32 Ds;
	UINT32 Fs;
	UINT32 Gs;
	UINT32 LdtSegmentSelector;
	UINT32 IoMapBaseAddr;
	UINT32 Ssp;
} TASK_STATE_SEGMENT32;
#pragma pack(pop)

#pragma pack(push, 8)
typedef struct {
	UINT16 Selector;
	UINT16 Pad0;
	UINT32 Pad1;
	UINT64 Limit;
	UINT64 AccessRights;
	UINT64 Base;
} SEGMENT_STATE;

/*
 * In long mode, the TSS does not store information on a task's execution state,
 * instead it is used to store the IST (Interrupt Stack Table).
 */
typedef struct {
	UINT64 Reserved0;
	UINT64 Rsp0;
	UINT64 Rsp1;
	UINT64 Rsp2;
	UINT64 Reserved1;
	UINT64 Ist[7];
	UINT64 Reserved2;
	UINT64 Reserved3;
	UINT32 IoMapBaseAddr;
} TASK_STATE_SEGMENT64;

typedef struct {
	UINT64 ProcessorId;
	UINT64 Package;
	UINT64 CoreAddr;
	UINT64 ThreadAddr;
} PROCESSOR_STRUCT;
#pragma pack(pop)

// Forward declarations
typedef struct _SETTINGS_NEW_ENVIRONMENT SETTINGS_NEW_ENVIRONMENT;
typedef struct _UEFI_STATE_STRUCT UEFI_STATE_STRUCT;
typedef struct _INIT_VMM_PARAM INIT_VMM_PARAM;

#pragma pack(push, 8)
struct _SETTINGS_NEW_ENVIRONMENT {
	EFI_PHYSICAL_ADDRESS Vmm;
	EFI_PHYSICAL_ADDRESS VmmStack;
//	EFI_PHYSICAL_ADDRESS ReservedAlign;
	EFI_PHYSICAL_ADDRESS EnterVmmAddr;
	EFI_PHYSICAL_ADDRESS ApEntryPage;
	UINT64 VmmStackSize;

	UINT64 *Gdt;
	DESCRIPTOR_REGISTER *Gdtr;

	UINT64 *Pml4;
	UINT64 *Pdpte;
	UINT64 *Pde;
	UINT64 *Pte;
	TASK_STATE_SEGMENT32 Tss32;
	TASK_STATE_SEGMENT64 Tss64;

	UINTN Rflags;

	EFI_MEMORY_DESCRIPTOR *MemoryDesc;
	UINTN MapSize;
	UINTN DescSize;

	UINT64 Bsp;
	UINT64 ProcessorNum;
	UINT64 EnabledProcessorNum;

	UINT64 VmmPages;
	UINT64 VmmBinSize;
	UINT64 Capacity;

	UINT64 UsedFreePages;
	UINT64 CurrentFreePage;

	VOID(EFIAPI *Init)(SETTINGS_NEW_ENVIRONMENT *NewSetup);
	UINT64(EFIAPI *Start)(SETTINGS_NEW_ENVIRONMENT *NewSetup,
			    UEFI_STATE_STRUCT *UefiState);

	UINT64(EFIAPI *SetGdt)(SETTINGS_NEW_ENVIRONMENT *NewSetup,
			       UINT64 FreePage);
//	VOID(EFIAPI *SetTss)(SETTINGS_NEW_ENVIRONMENT *NewSetup);

	UINT64(EFIAPI *GetMemoryMap)(SETTINGS_NEW_ENVIRONMENT *NewSetup);
	UINT64(EFIAPI *SetPageTable)(SETTINGS_NEW_ENVIRONMENT *NewSetup,
				     UINT64 FreePage);
	UINT64(EFIAPI *StartAps)(SETTINGS_NEW_ENVIRONMENT *NewSetup,
				 UINT64 FreePage);
	UINT64(EFIAPI *Done)(SETTINGS_NEW_ENVIRONMENT *NewSetup,
			     INIT_VMM_PARAM *VmmParameter);

	EFI_FILE_PROTOCOL *(EFIAPI *OpenVmm)(EFI_HANDLE ImageHandle);
	UINTN(EFIAPI *VmmSize)(EFI_FILE_PROTOCOL *VmmImg);
	EFI_STATUS(EFIAPI *ReadVmm)(SETTINGS_NEW_ENVIRONMENT *NewSetup,
				    EFI_FILE_PROTOCOL *VmmImg);
};
#pragma pack(pop)

#pragma pack(push, 1)
struct _UEFI_STATE_STRUCT {
	UINT64 Rax;
	UINT64 Rbx;
	UINT64 Rcx;
	UINT64 Rdx;
	UINT64 R8;
	UINT64 R9;
	UINT64 R10;
	UINT64 R11;
	UINT64 R12;
	UINT64 R13;
	UINT64 R14;
	UINT64 R15;
	UINT64 Rsi;
	UINT64 Rdi;
	UINT64 Rbp;
	UINT64 Rsp;
	UINT64 Rip;
	UINT64 RFlags;

	UINT64 Cr0;
	UINT64 Cr2;
	UINT64 Cr3;
	UINT64 Cr4;

	SEGMENT_STATE Cs;
	SEGMENT_STATE Ss;
	SEGMENT_STATE Ds;
	SEGMENT_STATE Es;
	SEGMENT_STATE Fs;
	SEGMENT_STATE Gs;
	SEGMENT_STATE Tr;

	UINT64 Dr7;
	UINT64 Msr;

	UINT64 CoreNum;
	UINT64 EntryPageAp;

	DESCRIPTOR_REGISTER Gdtr;
	DESCRIPTOR_REGISTER Idtr;

	UINT64 GdtLimit;
	UINT64 GdtOffset;

	UINT64 IdtLimit;
	UINT64 IdtOffset;

	VOID(EFIAPI *Start)(UEFI_STATE_STRUCT *UefiState);
	VOID(EFIAPI *SetRegisters)(UEFI_STATE_STRUCT *UefiState);
};

typedef struct {
	CHAR8 Signature[4];
	UINT32 Len;
	CHAR8 Revision;
	CHAR8 Checksum;
	CHAR8 Oemid[6];
	CHAR8 OemTableId[8];
	UINT32 OemRevision;
	UINT32 CreatorId;
	UINT32 CreatorRevision;
} EXTENDED_SYSTEM_DESC_TABLE;
#pragma pack(pop)

#pragma pack(push, 8)
struct _INIT_VMM_PARAM {
	EFI_PHYSICAL_ADDRESS UefiStateAddress;
	EFI_PHYSICAL_ADDRESS VmmEntry;
	EFI_PHYSICAL_ADDRESS VmmStack;
	UINT64 *Pml4;
	EFI_PHYSICAL_ADDRESS ExtraMemoryPa;
	EFI_PHYSICAL_ADDRESS ApEntryPage;
	UINT64 VmmStackVa;
	UINT64 ExtraMemoryEntryVa;
	UINT64 SubStackEntryVa;
	UINT64 SubStackPages;
	UINT64 Bsp;
	UINT64 *ApicId;
	UINT64 CoreNum;
	UINT64 Reserved0;
	UINT64 Reserved1;
	UINT64 Reserved2;
};
#pragma pack(pop)

// VMM Context Helper Functions
SETTINGS_NEW_ENVIRONMENT *CreateNewEnvironment(VOID);
UEFI_STATE_STRUCT *CreateUefiState(VOID);

#endif