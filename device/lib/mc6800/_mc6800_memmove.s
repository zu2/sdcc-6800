;-------------------------------------------------------------------------
; _memmove.s - MC6800 memmove for the mc6800 calling convention
;
; Adapted from chibicc-6800-v1 libc/__memmove.s.
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

	.module _mc6800_memmove
	.globl _memmove
	.globl REGTEMP2
	.globl REGTEMP3
	.globl REGTEMP4
	.globl REGTEMP5
	.globl REGTEMP6
	.globl _memmove_PARM_2
	.globl _memmove_PARM_3
	.area XSEG
_memmove_PARM_2:
	.ds 2
_memmove_PARM_3:
	.ds 2
	.area CSEG (CODE)
_memmove:
	staa *REGTEMP2
	stab *(REGTEMP2+1)
	staa *REGTEMP5
	stab *(REGTEMP5+1)
	ldx _memmove_PARM_2
	stx *REGTEMP3
	ldx _memmove_PARM_3
	stx *REGTEMP4
	ldaa *REGTEMP4
	ldab *(REGTEMP4+1)
	staa *REGTEMP6
	orab *REGTEMP6
	beq memmove_zero
	ldab *(REGTEMP4+1)
	tstb
	beq memmove_count_ready
	inc REGTEMP4
memmove_count_ready:
	ldaa *REGTEMP2
	ldab *(REGTEMP2+1)
	subb *(REGTEMP3+1)
	sbca *REGTEMP3
	bcs memmove_forward
	ldaa *REGTEMP4
	ldab *(REGTEMP4+1)
	tstb
	beq memmove_actual_n
	deca
memmove_actual_n:
	addb *(REGTEMP3+1)
	adca *REGTEMP3
	staa *REGTEMP3
	stab *(REGTEMP3+1)
	ldaa *REGTEMP4
	ldab *(REGTEMP4+1)
	tstb
	beq memmove_actual_n2
	deca
memmove_actual_n2:
	addb *(REGTEMP2+1)
	adca *REGTEMP2
	staa *REGTEMP2
	stab *(REGTEMP2+1)
	ldab *(REGTEMP4+1)
	ldx *REGTEMP3
memmove_backward_loop:
	dex
	ldaa 0,x
	stx *REGTEMP3
	ldx *REGTEMP2
	dex
	staa 0,x
	stx *REGTEMP2
	ldx *REGTEMP3
	decb
	bne memmove_backward_loop
	dec REGTEMP4
	bne memmove_backward_loop
	bra memmove_zero
memmove_forward:
	ldab *(REGTEMP4+1)
	ldx *REGTEMP3
memmove_forward_loop:
	ldaa 0,x
	inx
	stx *REGTEMP3
	ldx *REGTEMP2
	staa 0,x
	inx
	stx *REGTEMP2
	ldx *REGTEMP3
	decb
	bne memmove_forward_loop
	dec REGTEMP4
	bne memmove_forward_loop
memmove_zero:
	ldaa *REGTEMP5
	ldab *(REGTEMP5+1)
	rts
