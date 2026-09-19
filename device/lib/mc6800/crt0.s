	.module crt0
	.globl _main
	.globl ___sdcc_external_startup
	.globl s_ZP
	.globl l_ZP
	.globl s_XSEG
	.globl l_XSEG
	.globl s_XINIT
	.globl l_XINIT
	.globl s_XISEG
	.globl ___SDCC_mc6800_tmp0
	.globl ___SDCC_mc6800_tmp1
	.globl ___SDCC_mc6800_tmp2
	.globl __sdcc_stack_top

SIMIF	=	0xfefe
SIMIF_STOP =	0x73

;--------------------------------------------------------
;  Ordering of segments for the linker.
;--------------------------------------------------------
	.area ZP      (PAG)
	.area HOME    (CODE)
	.area GSINIT0 (CODE)
	.area GSINIT  (CODE)
	.area GSFINAL (CODE)
	.area CSEG    (CODE)
	.area XINIT   (CODE)
	.area CONST   (CODE)
	.area XSEG
	.area XISEG
	.area OSEG    (PAG, OVR)

;--------------------------------------------------------
;  Reset vector
;--------------------------------------------------------
	.area CODEIVT (ABS)
	.org	0xfffe
	.dw	__sdcc_gs_init_startup

;--------------------------------------------------------
;  Startup code
;--------------------------------------------------------
	.area GSINIT0
__sdcc_gs_init_startup:
	lds	#__sdcc_stack_top
	jsr	___sdcc_external_startup
	beq	__sdcc_init_data
	jmp	__sdcc_program_startup
;
__sdcc_init_data:
	ldab	#<l_ZP			; assume l_ZP < 256
	beq	00002$
	ldx	#s_ZP
	clra
00001$:
	staa	0,x
	inx
	decb
	bne	00001$
;
00002$:
	ldx	#l_XSEG
	beq	00004$
	stx	*___SDCC_mc6800_tmp0
	ldx	#s_XSEG
	ldab	#<l_XSEG
	beq	00003$
	inc	___SDCC_mc6800_tmp0
;
00003$:
	staa	0,x
	inx
	decb
	bne	00003$
	dec	___SDCC_mc6800_tmp0
	bne	00003$
;
00004$:
	ldab	#<s_XISEG
	addb	#<l_XINIT
	ldaa	#>s_XISEG
	adca	#>l_XINIT
	stab	*___SDCC_mc6800_tmp2+1
	staa	*___SDCC_mc6800_tmp2
;
	ldx	#s_XINIT
	ldab	#<l_XINIT
	lsrb
	bcc	00007$
	ldaa	0,x
	inx
;
00007$:
	stx	*___SDCC_mc6800_tmp0
	ldx	#s_XISEG
	bcc	00009$
	staa	0,x
	inx
	bra	00009$
;
00005$:
	stx	*___SDCC_mc6800_tmp1
	ldx	*___SDCC_mc6800_tmp0
	ldaa	0,x
	ldab	1,x
	inx
	inx
	stx	*___SDCC_mc6800_tmp0
	ldx	*___SDCC_mc6800_tmp1
	staa	0,x
	stab	1,x
	inx
	inx
;
00009$:
	cpx	*___SDCC_mc6800_tmp2
	bne	00005$

	.area GSFINAL
	jmp	__sdcc_program_startup

	.area CSEG
__sdcc_program_startup:
	jsr	_main
	ldaa	#SIMIF_STOP
	staa	SIMIF
	bra	.
