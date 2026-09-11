	.module crt0
	.globl _main
	.globl ___sdcc_external_startup
	.globl __sdcc_init_xdata
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
__sdcc_init_data:
	jsr	__sdcc_init_xdata

	.area GSFINAL
	jmp	__sdcc_program_startup

	.area CSEG
__sdcc_program_startup:
	jsr	_main
	ldaa	#SIMIF_STOP
	staa	SIMIF
	bra	.
