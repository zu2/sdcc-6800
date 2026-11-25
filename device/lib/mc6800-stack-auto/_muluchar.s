;-------------------------------------------------------------------------
; _muluchar.s - MC6800 multiplication for the mc6800-stack-auto calling convention
;
; Adapted from chibicc-6800-v1 libc/__mul8x8u.s.
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

	.module _muluchar
	.globl ___muluchar
	.globl REGTEMP2
	.area CSEG (CODE)

; B and A: unsigned 8-bit operands; A:B: 16-bit result.
___muluchar:
	ldx #8
	staa *REGTEMP2
	clra
	lsrb
00001$:
	bcc 00002$
	adda *REGTEMP2
00002$:
	rora
	rorb
	dex
	bne 00001$
	rts
