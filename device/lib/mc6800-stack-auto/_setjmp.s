	.module _setjmp

	.globl ___setjmp
	.globl _longjmp

	.area CSEG    (CODE)

___setjmp:
	stab	*(___SDCC_mc6800_tmp0 + 1)
	staa	*___SDCC_mc6800_tmp0
	ldx	*___SDCC_mc6800_tmp0
	pula
	pulb
	sts	0,x
	stab	3,x
	staa	2,x
	pshb
	psha
	clrb
	clra
	rts

_longjmp:
	stab	*(___SDCC_mc6800_tmp0 + 1)
	staa	*___SDCC_mc6800_tmp0
	tsx
	ldab	3,x
	ldaa	2,x
	bne	00001$
	cmpb	#1
	adcb	#0
00001$:
	ldx	*___SDCC_mc6800_tmp0
	lds	0,x
	ldx	2,x
	jmp	0,x
