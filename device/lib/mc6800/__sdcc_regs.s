; PSEUDO registers used by SDCC codegen
; ___SDCC_mc6800_retX used for return values
; ___SDCC_mc6800_tmpX used for temporaries
; Keep the number of temporaries in sync with NUM_TEMP_REGS in mc6800/gen.h

	.module __sdcc_regs

	.area	ZP (PAG)
___SDCC_mc6800_ret0::
	.ds 1
___SDCC_mc6800_ret1::
	.ds 1
___SDCC_mc6800_ret2::
	.ds 1
___SDCC_mc6800_ret3::
	.ds 1
___SDCC_mc6800_ret4::
	.ds 1
___SDCC_mc6800_ret5::
	.ds 1
___SDCC_mc6800_ret6::
	.ds 1
___SDCC_mc6800_ret7::
	.ds 1
___SDCC_mc6800_tmp0::
	.ds 2
___SDCC_mc6800_tmp1::
	.ds 2
___SDCC_mc6800_tmp2::
	.ds 2
___SDCC_mc6800_tmp3::
	.ds 2
___SDCC_mc6800_tmp4::
	.ds 2
___SDCC_mc6800_tmp5::
	.ds 2
___SDCC_mc6800_tmp6::
	.ds 2
___SDCC_mc6800_tmp7::
	.ds 2
