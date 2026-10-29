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

    .gdt:              resq 1
    .gdtr:             resq 1
    .idt:              resq 1
    .idtr:             resq 1

    .dr7:              resq 1

    .msr	       resq 1

    .core_count:       resq 1
endstruc

section .text

global enter_vmm
global exit_vmm

enter_vmm:
        cli

	mov r9, [rcx + 0x38]
	mov r10, [rcx + 0x40]

	push rbx

	mov rbx, [rdx]
	mov [rbx + uefi_state.rax], rax
	pop rbx

	mov rax, [rdx]
	mov [rax + uefi_state.rbx], rbx
	mov [rax + uefi_state.rcx], rcx
	mov [rax + uefi_state.rdx], rdx
	mov [rax + uefi_state.r8], r8
	mov [rax + uefi_state.r9], r9
	mov [rax + uefi_state.r10], r10
	mov [rax + uefi_state.r11], r11
	mov [rax + uefi_state.r12], r12
	mov [rax + uefi_state.r13], r13
	mov [rax + uefi_state.r14], r14
	mov [rax + uefi_state.r15], r15

	xor rax, rax
	xor rbx, rbx

	lea rbx, enter_vmm
	lea rax, chg_gdtr_pg
	sub rax, rbx

	add rax, [rcx + 0x18]

	jmp rax

chg_gdtr_pg:
	lgdt [r9]

	mov rax, r10
        mov cr3, rax
	;mov rbx, [rcx + 0x08]
	;mov rsp, rbx

	;jmp short .reload_cs
	jmp short .reload_segments

;.reload_cs:
	;push 0x38 ; problem
	;lea rbx, [rel .reload_segments]
	;push rbx
	;retfq
	
.reload_segments:
	;mov bx, 0x40
	mov bx, 0x0
	mov ds, bx
	mov es, bx
	mov fs, bx
	mov gs, bx
	mov ss, bx

        mov rax, 0x00400000
	jmp far rax

exit_vmm:
	lea rdx, [rel uefi_state]

	mov bx, [rdx + uefi_state.ss]
	mov ss, bx

	mov bx, [rdx + uefi_state.gs]
	mov gs, bx

	mov rbx, [rdx + uefi_state.es]
	mov es, bx

	mov rbx, [rdx + uefi_state.cs]
	mov cs, bx

	mov rbx, [rdx + uefi_state.fs]
	mov fs, bx

	mov rbx, [rdx + uefi_state.ds]
	mov fs, bx

	xor bx, bx

	;mov rbx, [rdx + uefi_state.tr]
	;mov tr, rbx

	mov r9, [rdx + uefi_state.gdtr]
	lgdt [r9]

	mov rbx, [rdx + uefi_state.cr3]
	mov cr3, rbx

	mov rax, [rdx + uefi_state.rax]
	mov rbx, [rdx + uefi_state.rbx]
	mov rcx, [rdx + uefi_state.rcx]
	mov r8, [rdx + uefi_state.r8]
	mov r9, [rdx + uefi_state.r9]
	mov r10, [rdx + uefi_state.r10]
	mov r11, [rdx + uefi_state.r11]
	mov r12, [rdx + uefi_state.r12]
	mov r13, [rdx + uefi_state.r13]
	mov r14, [rdx + uefi_state.r14]
	mov r15, [rdx + uefi_state.r15]

	; not complete

	ret
