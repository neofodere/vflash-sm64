.arm
.text

.global vf_ctx_switch
vf_ctx_switch:
	stmia r0, {r4-r11, sp, lr}
	ldmia r1, {r4-r11, sp, lr}
	bx lr

.global vf_thread_entry
vf_thread_entry:
	mov r0, r5
	blx r4
	bl osThreadExit
1:	b 1b
