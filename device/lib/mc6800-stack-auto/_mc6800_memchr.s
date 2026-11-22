;-------------------------------------------------------------------------
; _memchr.s - MC6800 memchr for the mc6800-stack-auto calling convention
;
; Adapted from chibicc-6800-v1 libc/__memchr.s.
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

	.module _mc6800_memchr
	.globl _memchr
	.globl REGTEMP2
	.globl REGTEMP3
	.globl REGTEMP4
	.globl REGTEMP5
	.globl REGTEMP6
	.area CSEG (CODE)
_memchr:
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
	beq memchr_zero
	ldab *(REGTEMP4+1)
	tstb
	beq memchr_count_ready
	inc REGTEMP4
memchr_count_ready:
	ldaa *(REGTEMP3+1)
	ldx *REGTEMP2
memchr_loop:
	cmpa 0,x
	beq memchr_found
	inx
	decb
	bne memchr_loop
	dec REGTEMP4
	bne memchr_loop
memchr_zero:
	clra
	clrb
	rts
memchr_found:
	stx *REGTEMP2
	ldaa *REGTEMP2
	ldab *(REGTEMP2+1)
	rts
