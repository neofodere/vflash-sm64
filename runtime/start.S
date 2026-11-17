.arm
.section .boot, "ax"
.global _start

_start:
	.ascii "BOOT"
	.word __image_size
	.word 0x10500000
	.word 0x10
	b reset
	.space 0x30 - 0x14

.equ TABLE, 0x10004000
.equ RAM_PHYS, 0x10000000
.equ RAM_VIRT, 0x80000000

reset:
	msr cpsr_c, #0xD3
	ldr r0, =TABLE
	mov r1, #0
	ldr r2, =0xC02
1:	orr r3, r2, r1, lsl #20
	str r3, [r0, r1, lsl #2]
	add r1, r1, #1
	cmp r1, #4096
	blo 1b
	mov r1, #0
	ldr r2, =0xC0A
	ldr r4, =(RAM_PHYS >> 20)
	ldr r5, =(RAM_VIRT >> 20)
2:	add r3, r4, r1
	orr r3, r2, r3, lsl #20
	add r6, r5, r1
	str r3, [r0, r6, lsl #2]
	add r1, r1, #1
	cmp r1, #16
	blo 2b
	ldr r2, =0x10000C02
	str r2, [r0]
	mcr p15, 0, r0, c2, c0, 0
	mov r1, #1
	mcr p15, 0, r1, c3, c0, 0
	mov r1, #0
	mcr p15, 0, r1, c8, c7, 0
	mcr p15, 0, r1, c7, c7, 0
	mrc p15, 0, r1, c1, c0, 0
	ldr r2, =0x1005
	orr r1, r1, r2
	mcr p15, 0, r1, c1, c0, 0
	nop
	nop
	ldr pc, =virt_entry

.pool

.text
virt_entry:
	ldr sp, =vf_stack_top
	ldr r0, =vf_vectors
	ldr r1, =0x80000000
	mov r2, #16
5:	ldr r3, [r0], #4
	str r3, [r1], #4
	subs r2, r2, #1
	bne 5b
	ldr r0, =__bss_start
	ldr r1, =__bss_end
	mov r2, #0
3:	cmp r0, r1
	strlo r2, [r0], #4
	blo 3b
	bl vf_main
4:	b 4b

.balign 4
vf_vectors:
	.rept 8
	ldr pc, [pc, #24]
	.endr
	.word vec_reset, vec_undef, vec_swi, vec_pabt, vec_dabt, vec_reset, vec_irq, vec_fiq

.macro VEC name, kind, adj
\name:
	ldr sp, =vf_stack_top
	stmfd sp!, {r0-r12, lr}
	mov r0, #\kind
	sub r1, lr, #\adj
	mov r2, sp
	b vf_fault
.endm

	VEC vec_reset, 0, 0
	VEC vec_undef, 1, 4
	VEC vec_swi, 2, 4
	VEC vec_pabt, 3, 4
	VEC vec_dabt, 4, 8
	VEC vec_irq, 6, 4
	VEC vec_fiq, 7, 4

.pool

.bss
.balign 16
.space 0x10000
.global vf_stack_top
vf_stack_top:
