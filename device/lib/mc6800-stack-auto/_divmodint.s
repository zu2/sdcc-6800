;-------------------------------------------------------------------------
; _divmodint.s - MC6800 division and remainder for the mc6800-stack-auto calling convention
;
; Adapted from chibicc-6800-v1 libc/__div16x16.s.
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

	.module _divmodint
	.globl ___divuint
	.globl ___moduint
	.globl ___divsint
	.globl ___modsint
	.globl REGTEMP2
	.globl REGTEMP3
	.globl REGTEMP4
	.area CSEG (CODE)

; A:B: dividend; second operand in memory or on the stack.
___divuint:
	staa *REGTEMP2
	stab *(REGTEMP2+1)
	tsx
	ldaa 2,x
	ldab 3,x
	staa *REGTEMP3
	stab *(REGTEMP3+1)
	clr REGTEMP4
	jsr divmod16_core
	ldaa *REGTEMP2
	ldab *(REGTEMP2+1)
	coma
	comb
	tst REGTEMP4
	bmi divmod16_neg_0
	rts
divmod16_neg_0:
	nega
	negb
	sbca #0
	rts
___moduint:
	staa *REGTEMP2
	stab *(REGTEMP2+1)
	tsx
	ldaa 2,x
	ldab 3,x
	staa *REGTEMP3
	stab *(REGTEMP3+1)
	clr REGTEMP4
	jsr divmod16_core
	tst REGTEMP4
	bmi divmod16_neg_1
	rts
divmod16_neg_1:
	nega
	negb
	sbca #0
	rts
___divsint:
	staa *REGTEMP2
	stab *(REGTEMP2+1)
	tsx
	ldaa 2,x
	ldab 3,x
	staa *REGTEMP3
	stab *(REGTEMP3+1)
	clr REGTEMP4
	ldaa *REGTEMP2
	ldab *(REGTEMP2+1)
	tsta
	bpl divsint_x_pos
	nega
	negb
	sbca #0
	staa *REGTEMP2
	stab *(REGTEMP2+1)
	com REGTEMP4
divsint_x_pos:
	ldaa *REGTEMP3
	ldab *(REGTEMP3+1)
	tsta
	bpl divsint_y_pos
	nega
	negb
	sbca #0
	staa *REGTEMP3
	stab *(REGTEMP3+1)
	com REGTEMP4
divsint_y_pos:
	jsr divmod16_core
	ldaa *REGTEMP2
	ldab *(REGTEMP2+1)
	coma
	comb
	tst REGTEMP4
	bmi divmod16_neg_2
	rts
divmod16_neg_2:
	nega
	negb
	sbca #0
	rts
___modsint:
	staa *REGTEMP2
	stab *(REGTEMP2+1)
	tsx
	ldaa 2,x
	ldab 3,x
	staa *REGTEMP3
	stab *(REGTEMP3+1)
	clr REGTEMP4
	ldaa *REGTEMP2
	ldab *(REGTEMP2+1)
	tsta
	bpl modsint_x_pos
	nega
	negb
	sbca #0
	staa *REGTEMP2
	stab *(REGTEMP2+1)
	com REGTEMP4
modsint_x_pos:
	ldaa *REGTEMP3
	ldab *(REGTEMP3+1)
	tsta
	bpl modsint_y_pos
	nega
	negb
	sbca #0
	staa *REGTEMP3
	stab *(REGTEMP3+1)
modsint_y_pos:
	jsr divmod16_core
	tst REGTEMP4
	bmi divmod16_neg_3
	rts
divmod16_neg_3:
	nega
	negb
	sbca #0
	rts
; Returns one's-complement quotient in REGTEMP2 and remainder in A:B.
divmod16_core:
	ldx #8
	clra
	clrb
00001$:
	rol REGTEMP2
	rolb
	rola
	subb *(REGTEMP3+1)
	sbca *REGTEMP3
	bcc 00002$
	addb *(REGTEMP3+1)
	adca *REGTEMP3
00002$:
	dex
	bne 00001$
	rol REGTEMP2
	ldx #8
00003$:
	rol REGTEMP2+1
	rolb
	rola
	subb *(REGTEMP3+1)
	sbca *REGTEMP3
	bcc 00004$
	addb *(REGTEMP3+1)
	adca *REGTEMP3
00004$:
	dex
	bne 00003$
	rol REGTEMP2+1
	rts
