.arm
.text

.global __aeabi_uidivmod
__aeabi_uidivmod:
	push {r4, lr}
	sub sp, sp, #8
	mov r2, sp
	bl udivmod32
	ldr r1, [sp]
	add sp, sp, #8
	pop {r4, pc}

.global __aeabi_idivmod
__aeabi_idivmod:
	push {r4, lr}
	sub sp, sp, #8
	mov r2, sp
	bl idivmod_core
	ldr r1, [sp]
	add sp, sp, #8
	pop {r4, pc}

.global __aeabi_uldivmod
__aeabi_uldivmod:
	push {r4, lr}
	sub sp, sp, #16
	add r4, sp, #8
	str r4, [sp]
	bl uldivmod_core
	ldr r2, [sp, #8]
	ldr r3, [sp, #12]
	add sp, sp, #16
	pop {r4, pc}

.global __aeabi_ldivmod
__aeabi_ldivmod:
	push {r4, lr}
	sub sp, sp, #16
	add r4, sp, #8
	str r4, [sp]
	bl ldivmod_core
	ldr r2, [sp, #8]
	ldr r3, [sp, #12]
	add sp, sp, #16
	pop {r4, pc}
