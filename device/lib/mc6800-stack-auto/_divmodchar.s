;-------------------------------------------------------------------------
; _divmodchar.s - MC6800 division and remainder for the mc6800-stack-auto calling convention
;
; Adapted from chibicc-6800-v1 libc/__div8x8.s.
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

	.module _divmodchar
	.globl ___divuchar
	.globl ___moduchar
	.globl ___divschar
	.globl ___divuschar
	.globl ___divsuchar
	.globl ___modschar
	.globl ___moduschar
	.globl ___modsuchar
	.globl REGTEMP2
	.globl REGTEMP4
	.area CSEG (CODE)

; B: dividend, A: divisor; A:B: result.
___divuchar:
	clr REGTEMP4
	staa *REGTEMP2
	jsr divmod8_core
	clra
	tst REGTEMP4
	bmi divmod8_neg_0
	rts
divmod8_neg_0:
	nega
	negb
	sbca #0
	rts
___moduchar:
	clr REGTEMP4
	staa *REGTEMP2
	jsr divmod8_core
	tab
	clra
	tst REGTEMP4
	bmi divmod8_neg_1
	rts
divmod8_neg_1:
	nega
	negb
	sbca #0
	rts
___divschar:
	clr REGTEMP4
	tsta
	bpl divschar_y_pos
	nega
	com REGTEMP4
divschar_y_pos:
	tstb
	bpl divschar_x_pos
	negb
	com REGTEMP4
divschar_x_pos:
	staa *REGTEMP2
	jsr divmod8_core
	clra
	tst REGTEMP4
	bmi divmod8_neg_2
	rts
divmod8_neg_2:
	nega
	negb
	sbca #0
	rts
___divuschar:
	clr REGTEMP4
	tsta
	bpl divuschar_y_pos
	nega
	com REGTEMP4
divuschar_y_pos:
	staa *REGTEMP2
	jsr divmod8_core
	clra
	tst REGTEMP4
	bmi divmod8_neg_3
	rts
divmod8_neg_3:
	nega
	negb
	sbca #0
	rts
___divsuchar:
	clr REGTEMP4
	tstb
	bpl divsuchar_x_pos
	negb
	com REGTEMP4
divsuchar_x_pos:
	staa *REGTEMP2
	jsr divmod8_core
	clra
	tst REGTEMP4
	bmi divmod8_neg_4
	rts
divmod8_neg_4:
	nega
	negb
	sbca #0
	rts
___modschar:
	clr REGTEMP4
	tsta
	bpl modschar_y_pos
	nega
modschar_y_pos:
	tstb
	bpl modschar_x_pos
	negb
	com REGTEMP4
modschar_x_pos:
	staa *REGTEMP2
	jsr divmod8_core
	tab
	clra
	tst REGTEMP4
	bmi divmod8_neg_5
	rts
divmod8_neg_5:
	nega
	negb
	sbca #0
	rts
___moduschar:
	clr REGTEMP4
	tsta
	bpl moduschar_y_pos
	nega
moduschar_y_pos:
	staa *REGTEMP2
	jsr divmod8_core
	tab
	clra
	tst REGTEMP4
	bmi divmod8_neg_6
	rts
divmod8_neg_6:
	nega
	negb
	sbca #0
	rts
___modsuchar:
	clr REGTEMP4
	tstb
	bpl modsuchar_x_pos
	negb
	com REGTEMP4
modsuchar_x_pos:
	staa *REGTEMP2
	jsr divmod8_core
	tab
	clra
	tst REGTEMP4
	bmi divmod8_neg_7
	rts
divmod8_neg_7:
	nega
	negb
	sbca #0
	rts
; Returns quotient in B and remainder in A.
divmod8_core:
	ldx #8
	clra
00001$:
	aslb
	rola
	cmpa *REGTEMP2
	bcs 00002$
	suba *REGTEMP2
	incb
00002$:
	dex
	bne 00001$
	rts
