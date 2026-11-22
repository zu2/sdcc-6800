;-------------------------------------------------------------------------
; _memcmp.s - MC6800 memcmp for the mc6800-stack-auto calling convention
;
; Adapted from chibicc-6800-v1 libc/__memcmp.s.
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

	.module _mc6800_memcmp
	.globl _memcmp
	.globl REGTEMP2
	.globl REGTEMP3
	.globl REGTEMP4
	.globl REGTEMP5
	.globl REGTEMP6
	.area CSEG (CODE)
_memcmp:
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
	beq memcmp_zero
	ldab *(REGTEMP4+1)
	tstb
	beq memcmp_count_ready
	inc REGTEMP4
memcmp_count_ready:
	ldx *REGTEMP3
memcmp_loop:
	ldaa 0,x
	inx
	stx *REGTEMP3
	ldx *REGTEMP2
	cmpa 0,x
	bne memcmp_different
	inx
	stx *REGTEMP2
	ldx *REGTEMP3
	decb
	bne memcmp_loop
	dec REGTEMP4
	bne memcmp_loop
memcmp_zero:
	clra
	clrb
	rts
memcmp_different:
	bhi memcmp_less
	ldaa #0
	ldab #1
	rts
memcmp_less:
	ldaa #0xff
	ldab #0xff
	rts
