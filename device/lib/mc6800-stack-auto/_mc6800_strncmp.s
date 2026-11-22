;-------------------------------------------------------------------------
; _memcpy.s - MC6800 memcpy for the mc6800-stack-auto calling convention
;
; Adapted from chibicc-6800-v1 libc/__strncmp.s.
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

	.module _mc6800_strncmp
	.globl _strncmp
	.globl REGTEMP2
	.globl REGTEMP3
	.globl REGTEMP4
	.globl REGTEMP5
	.globl REGTEMP6
	.area CSEG (CODE)
_strncmp:
	staa *REGTEMP2
	stab *(REGTEMP2+1)
	tsx
	ldaa 2,x
	ldab 3,x
	staa *REGTEMP3
	stab *(REGTEMP3+1)
	ldaa 4,x
	ldab 5,x
	staa *REGTEMP4
	stab *(REGTEMP4+1)
	ldaa *REGTEMP4
	ldab *(REGTEMP4+1)
	staa *REGTEMP6
	orab *REGTEMP6
	beq strncmp_equal
	ldab *(REGTEMP4+1)
	tstb
	beq strncmp_count_ready
	inc REGTEMP4
strncmp_count_ready:
strncmp_loop:
	ldx *REGTEMP2
	ldaa 0,x
	inx
	stx *REGTEMP2
	ldx *REGTEMP3
	cmpa 0,x
	bne strncmp_different
	tsta
	beq strncmp_equal
	inx
	stx *REGTEMP3
	decb
	bne strncmp_loop
	dec REGTEMP4
	bne strncmp_loop
strncmp_equal:
	clra
	clrb
	rts
strncmp_different:
	bcs strncmp_less
	clra
	ldab #1
	rts
strncmp_less:
	ldaa #0xff
	ldab #0xff
	rts
