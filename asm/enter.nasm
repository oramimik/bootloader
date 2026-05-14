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
	lea rax, next_step
	sub rax, rbx

	add rax, [rcx + 0x18]

	jmp rax

next_step:
	lgdt [r9]

	mov rax, r10
        mov cr3, rax
	jmp short ezpz

ezpz:
	xor ax, ax
	mov ds, ax
	mov es, ax
	mov ss, ax
	mov fs, ax
	mov gs, ax

        mov rax, 0x00400000
	jmp far rax

exit_vmm:
	mov rdx, [rel uefi_state]

	mov rbx, [rdx + uefi_state.ss]
	mov ss, bx

	mov rbx, [rdx + uefi_state.gs]
	mov gs, bx

	mov rbx, [rdx + uefi_state.es]
	mov es, bx

	ret
