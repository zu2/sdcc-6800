;-------------------------------------------------------------------------
; _mulint.s - MC6800 multiplication for the mc6800 calling convention
;
; Adapted from chibicc-6800-v1 libc/__mul16x16.s.
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

	.module _mulint
	.globl ___mulint
	.globl ___muluchar
	.globl REGTEMP2
	.globl REGTEMP3
	.globl ___mulint_PARM_2
	.area XSEG
___mulint_PARM_2:
	.ds 2
	.area CSEG (CODE)

; A:B: first operand; ___mulint_PARM_2: second operand.
___mulint:
	staa *REGTEMP2
	oraa ___mulint_PARM_2
	bne 00001$
	ldaa ___mulint_PARM_2+1
	jmp ___muluchar
00001$:
	stab *(REGTEMP2+1)
	ldx ___mulint_PARM_2
	stx *REGTEMP3
	ldx #8
	clrb
	clra
	lsr REGTEMP3+1
00002$:
	bcc 00003$
	addb *(REGTEMP2+1)
	adca *REGTEMP2
00003$:
	rora
	rorb
	ror REGTEMP3+1
	dex
	bne 00002$
	ldx #8
	ldaa *REGTEMP3
	lsra
00004$:
	bcc 00005$
	addb *(REGTEMP2+1)
00005$:
	rorb
	rora
	dex
	bne 00004$
	ldab *(REGTEMP3+1)
	rts
