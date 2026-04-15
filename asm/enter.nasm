bits 64
align 16

default rel

section .data
        vmm_entry dq 0
	gdt_entry dq 0
	pg_table_entry dq 0

	bridge_address dq 0

section .text

extern get_gdtr_offset
extern get_pg_table_offset

global enter_vmm
enter_vmm:
        cli

	mov rax, [rcx]
	mov [rel vmm_entry], rax

	mov rax, [rcx + 0x10]
	mov [rel bridge_address], rax

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

	mov r8, [rel vmm_entry]
	mov r9, [rel gdt_entry]
	mov r10, [rel pg_table_entry]
	mov r11, [rel bridge_address]

	xor rax, rax
	xor rbx, rbx

	lea rbx, enter_vmm
	lea rax, .next_step
	sub rax, rbx

	mov rbx, [rel bridge_address]
	add rax, rbx

	jmp rax


.next_step:
	mov dx, 0x3F8
	mov al, 'g'
	out dx, al