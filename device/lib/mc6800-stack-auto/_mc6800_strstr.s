;-------------------------------------------------------------------------
; _memcpy.s - MC6800 memcpy for the mc6800-stack-auto calling convention
;
; Adapted from chibicc-6800-v1 libc/__strstr.s.
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

	.module _mc6800_strstr
	.globl _strstr
	.globl REGTEMP2
	.globl REGTEMP3
	.globl REGTEMP4
	.globl REGTEMP5
	.globl REGTEMP6
	.area CSEG (CODE)
_strstr:
	staa *REGTEMP2
	stab *(REGTEMP2+1)
	tsx
	ldaa 2,x
	ldab 3,x
	staa *REGTEMP3
	stab *(REGTEMP3+1)
	ldx *REGTEMP3
	ldab 0,x
	beq strstr_found
strstr_outer:
	ldx *REGTEMP2
	ldab 0,x
	beq strstr_null
	stx *REGTEMP4
	ldx *REGTEMP3
	stx *REGTEMP5
strstr_inner:
	ldx *REGTEMP5
	ldab 0,x
	beq strstr_found
	inx
	stx *REGTEMP5
	ldx *REGTEMP4
	cmpb 0,x
	bne strstr_advance
	inx
	stx *REGTEMP4
	bra strstr_inner
strstr_advance:
	ldx *REGTEMP2
	inx
	stx *REGTEMP2
	bra strstr_outer
strstr_found:
	ldaa *REGTEMP2
	ldab *(REGTEMP2+1)
	rts
strstr_null:
	clra
	clrb
	rts
