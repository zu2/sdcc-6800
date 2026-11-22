;-------------------------------------------------------------------------
; _memcpy.s - MC6800 memcpy for the mc6800 calling convention
;
; Adapted from chibicc-6800-v1 libc/__strncat.s.
; Copyright (c) 2026, ZUKERAN, shin
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

	.module _mc6800_strncat
	.globl _strncat
	.globl REGTEMP2
	.globl REGTEMP3
	.globl REGTEMP4
	.globl REGTEMP5
	.globl REGTEMP6
	.globl _strncat_PARM_2
	.globl _strncat_PARM_3
	.area XSEG
_strncat_PARM_2:
	.ds 2
_strncat_PARM_3:
	.ds 2
	.area CSEG (CODE)
_strncat:
	staa *REGTEMP2
	stab *(REGTEMP2+1)
	staa *REGTEMP5
	stab *(REGTEMP5+1)
	ldx _strncat_PARM_2
	stx *REGTEMP3
	ldx _strncat_PARM_3
	stx *REGTEMP4
	ldaa *REGTEMP4
	ldab *(REGTEMP4+1)
	staa *REGTEMP6
	orab *REGTEMP6
	beq strncat_return
	ldab *(REGTEMP4+1)
	tstb
	beq strncat_count_ready
	inc REGTEMP4
strncat_count_ready:
	ldx *REGTEMP2
strncat_seek:
	ldaa 0,x
	beq strncat_end_seek
	inx
	bra strncat_seek
strncat_end_seek:
	stx *REGTEMP2
	ldx *REGTEMP3
strncat_loop:
	ldaa 0,x
	inx
	stx *REGTEMP3
	ldx *REGTEMP2
	staa 0,x
	beq strncat_return
	inx
	stx *REGTEMP2
	ldx *REGTEMP3
	decb
	bne strncat_loop
	dec REGTEMP4
	bne strncat_loop
	ldx *REGTEMP2
	clr 0,x
	bra strncat_return
strncat_return:
	ldaa *REGTEMP5
	ldab *(REGTEMP5+1)
	rts
