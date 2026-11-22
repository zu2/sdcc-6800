;-------------------------------------------------------------------------
; _memset.s - MC6800 memset for the mc6800 calling convention
;
; Adapted from chibicc-6800-v1 libc/__memset.s.
; Copyright (c) 2025-2026, ZUKERAN, shin
;
; MIT License
;
; Permission is hereby granted, free of charge, to any person obtaining a copy
; of this software and associated documentation files (the "Software"), to deal
; in the Software without restriction, including without limitation the rights
; to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
; copies of the Software, and to permit persons to whom the Software is
; furnished to do so, subject to the following conditions:
;
; The above copyright notice and this permission notice shall be included in all
; copies or substantial portions of the Software.
;
; THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
; IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
; FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
; AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
; LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
; OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
; SOFTWARE.
;-------------------------------------------------------------------------

	.module _mc6800_memset
	.globl _memset
	.globl REGTEMP2
	.globl REGTEMP3
	.globl REGTEMP4
	.globl REGTEMP5
	.globl REGTEMP6
	.globl _memset_PARM_2
	.globl _memset_PARM_3
	.area XSEG
_memset_PARM_2:
	.ds 2
_memset_PARM_3:
	.ds 2
	.area CSEG (CODE)
_memset:
	staa *REGTEMP2
	stab *(REGTEMP2+1)
	staa *REGTEMP5
	stab *(REGTEMP5+1)
	ldx _memset_PARM_2
	stx *REGTEMP3
	ldx _memset_PARM_3
	stx *REGTEMP4
	ldaa *REGTEMP4
	ldab *(REGTEMP4+1)
	staa *REGTEMP6
	orab *REGTEMP6
	beq memset_zero
	ldab *(REGTEMP4+1)
	tstb
	beq memset_count_ready
	inc REGTEMP4
memset_count_ready:
	ldaa *REGTEMP4
	ldab *(REGTEMP4+1)
	tstb
	beq memset_n_ready
	deca
memset_n_ready:
	andb #0xfc
	bne memset_blocks
	tsta
	beq memset_tail_setup
memset_blocks:
	addb *(REGTEMP2+1)
	adca *REGTEMP2
	stab *(REGTEMP6+1)
	staa *REGTEMP6
	ldab *(REGTEMP3+1)
	ldx *REGTEMP2
memset_loop:
	stab 0,x
	inx
	stab 0,x
	inx
	stab 0,x
	inx
	stab 0,x
	inx
	cpx REGTEMP6
	bne memset_loop
	bra memset_tail
memset_tail_setup:
	ldab *(REGTEMP3+1)
	ldx *REGTEMP2
memset_tail:
	ldaa *(REGTEMP4+1)
	anda #3
	beq memset_zero
	stab 0,x
	deca
	beq memset_zero
	stab 1,x
	deca
	beq memset_zero
	stab 2,x
memset_zero:
	ldaa *REGTEMP5
	ldab *(REGTEMP5+1)
	rts
