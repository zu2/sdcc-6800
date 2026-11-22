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
	.globl __divuchar
	.globl __moduchar
	.globl __divschar
	.globl __divuschar
	.globl __divsuchar
	.globl __modschar
	.globl __moduschar
	.globl __modsuchar
	.globl REGTEMP2
	.globl REGTEMP4
	.area CSEG (CODE)

; B: dividend, A: divisor; A:B: result.
__divuchar:
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
__moduchar:
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
__divschar:
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
__divuschar:
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
__divsuchar:
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
__modschar:
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
__moduschar:
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
__modsuchar:
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
