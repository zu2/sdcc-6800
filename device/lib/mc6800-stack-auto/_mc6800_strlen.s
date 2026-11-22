;-------------------------------------------------------------------------
; _memcpy.s - MC6800 memcpy for the mc6800-stack-auto calling convention
;
; Adapted from chibicc-6800-v1 libc/__strlen.s.
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

	.module _mc6800_strlen
	.globl _strlen
	.globl REGTEMP2
	.globl REGTEMP3
	.globl REGTEMP4
	.globl REGTEMP5
	.globl REGTEMP6
	.area CSEG (CODE)
_strlen:
	staa *REGTEMP2
	stab *(REGTEMP2+1)
	ldx *REGTEMP2
strlen_loop:
	ldaa 0,x
	beq strlen_end
	inx
	bra strlen_loop
strlen_end:
	stx *REGTEMP3
	ldab *(REGTEMP3+1)
	ldaa *REGTEMP3
	subb *(REGTEMP2+1)
	sbca *REGTEMP2
	rts
