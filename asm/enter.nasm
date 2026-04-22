bits 64
align 16

default rel

struc uefi_state
    .rax:              resq 1
    .rbx:              resq 1
    .rcx:              resq 1
    .rdx:              resq 1
    .r8:               resq 1
    .r9:               resq 1
    .r10:              resq 1
    .r11:              resq 1
    .r12:              resq 1
    .r13:              resq 1
    .r14:              resq 1
    .r15:              resq 1
    .rsi:              resq 1
    .rdi:              resq 1
    .rbp:              resq 1
    .rsp:              resq 1
    .rip:              resq 1
    .rflags:           resq 1

    .cr0:              resq 1
    .cr2:              resq 1
    .cr3:              resq 1
    .cr4:              resq 1
    .cr8:              resq 1

    .cs:               resq 1
    .ss:               resq 1
    .ds:               resq 1
    .es:               resq 1
    .fs:               resq 1
    .gs:               resq 1
    .tr:               resq 1

    .cs_access_rights: resq 1
    .ss_access_rights: resq 1
    .ds_access_rights: resq 1
    .es_access_rights: resq 1
    .fs_access_rights: resq 1
    .gs_access_rights: resq 1

    .cs_limit:         resq 1
    .ss_limit:         resq 1
    .ds_limit:         resq 1
    .es_limit:         resq 1
    .fs_limit:         resq 1
    .gs_limit:         resq 1

    .gdt:              resq 1
    .gdtr:             resq 1
    .idt:              resq 1
    .idtr:             resq 1

    .dr7:              resq 1

    .msr_efer:         resq 1
    .msr_lme:          resq 1

    .core_count:       resq 1

    .fs_base:          resq 1
    .gs_base:          resq 1
    .entry_page_ap:    resq 1
    .uncached:         resq 1
endstruc

section .data
        vmm_entry dq 0
	vmm_stack dq 0
	gdt_entry dq 0
	pg_table_entry dq 0

	enter_vmm_address dq 0

section .text

extern get_gdtr_offset
extern get_pg_table_offset

global enter_vmm
enter_vmm:
        cli

	mov rax, [rcx]
	mov [rel vmm_entry], rax

	mov rax, [rcx + 0x18]
	mov [rel enter_vmm_address], rax

	mov rbx, rcx
	call get_gdtr_offset
	mov rcx, rbx
	mov rbx, [rcx + rax]
	mov [gdt_entry], rbx

	mov rbx, rcx
	call get_pg_table_offset
	mov rcx, rbx
	mov rbx, [rcx + rax]
	mov [rel pg_table_entry], rbx

	mov rax, [rcx + 8]
	mov [rel vmm_stack], rax

	mov r8, [rel vmm_entry]
	mov r9, [rel gdt_entry]
	mov r10, [rel pg_table_entry]
	mov r11, [rel enter_vmm_address]
	mov r12, [rel vmm_stack]

	xor rax, rax
	xor rbx, rbx

	lea rbx, enter_vmm
	lea rax, .next_step
	sub rax, rbx

	mov rbx, r11
	add rax, rbx

	jmp rax


.next_step:
	mov dx, 0x3F8
	mov al, 'g'
	out dx, al

        lgdt [r9]

	xor ax, ax
	mov ds, ax
	mov es, ax
	mov ss, ax
	mov fs, ax
	mov gs, ax
	
	mov dx, 0x3F8
	mov al, 'g'
	out dx, al

	mov rsp, r12
	mov rax, 0x1e
	or rax, 0x20
	mov cr4, rax

        mov cr3, r10

        mov dx, 0x3F8
	mov al, 'g'
	out dx, al

	push 0x38
	push 0x01000000

	retfq