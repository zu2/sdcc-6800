	.module crt0
	.globl _main
	.globl ___sdcc_external_startup
	.globl s_ZP
	.globl l_ZP
	.globl s_ZPISEG
	.globl s_ZPINIT
	.globl l_ZPINIT
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
	.area ZPISEG
	.area HOME    (CODE)
	.area GSINIT0 (CODE)
	.area GSINIT  (CODE)
	.area GSFINAL (CODE)
	.area CSEG    (CODE)
	.area ZPINIT  (CODE)
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
	ldx	#s_ZP
	ldaa	#>l_ZP
	ldab	#<l_ZP
	jsr	clear_data
;
	ldx	#s_ZPINIT
	stx	*___SDCC_mc6800_tmp0
	ldx	#s_ZPISEG
	ldaa	#>l_ZPINIT
	ldab	#<l_ZPINIT
	jsr	copy_data
;
	ldx	#s_XSEG
	ldaa	#>l_XSEG
	ldab	#<l_XSEG
	jsr	clear_data
;
	ldx	#s_XINIT
	stx	*___SDCC_mc6800_tmp0
	ldx	#s_XISEG
	ldaa	#>l_XINIT
	ldab	#<l_XINIT
	jsr	copy_data

	.area GSINIT
; Compiler writes initialization code into GSINIT segment.

	.area GSFINAL
	jmp	__sdcc_program_startup

	.area CSEG
;
__sdcc_program_startup:
	jsr	_main
	ldaa	#SIMIF_STOP
	staa	SIMIF
	bra	.

;
;
;
clear_data:
	tstb
	bne	clear_data_loop
clear_data_next_page:
	tsta
	beq	clear_data_done
	deca
clear_data_loop:
	clr	0,x
	inx
	decb
	bne	clear_data_loop
	bra	clear_data_next_page
clear_data_done:
	rts
;
copy_data:
	tsta
	bne	copy_data_setup
	tstb
	beq	copy_data_done
copy_data_setup:
	stx	*___SDCC_mc6800_tmp1
	addb	*___SDCC_mc6800_tmp1+1
	adca	*___SDCC_mc6800_tmp1
	stab	*___SDCC_mc6800_tmp2+1
	staa	*___SDCC_mc6800_tmp2
copy_data_loop:
	stx	*___SDCC_mc6800_tmp1
	ldx	*___SDCC_mc6800_tmp0
	ldaa	0,x
	inx
	stx	*___SDCC_mc6800_tmp0
	ldx	*___SDCC_mc6800_tmp1
	staa	0,x
	inx
	cpx	*___SDCC_mc6800_tmp2
	bne	copy_data_loop
copy_data_done:
	rts
