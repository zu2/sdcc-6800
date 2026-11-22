;-------------------------------------------------------------------------
; _mulschar.s - MC6800 signed multiplication for the mc6800 calling convention
;
; Adapted from chibicc-6800-v1 libc/__mul8x8s.s.
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

	.module _mulschar
	.globl __mulschar
	.globl __muluschar
	.globl __mulsuchar
	.globl __muluchar
	.globl REGTEMP3
	.area CSEG (CODE)

; First operand in B, second in A; result in A:B.
__mulschar:
	staa *REGTEMP3
	bpl muls_a_positive
	nega
muls_a_positive:
	tstb
	bpl muls_do
	negb
	com REGTEMP3
	bra muls_do

; Unsigned B times signed A.
__muluschar:
	staa *REGTEMP3
	bpl muls_do
	nega
	bra muls_do

; Signed B times unsigned A.
__mulsuchar:
	stab *REGTEMP3
	tstb
	bpl muls_do
	negb

muls_do:
	jsr __muluchar
	tst REGTEMP3
	bpl muls_return
	nega
	negb
	sbca #0
muls_return:
	rts
