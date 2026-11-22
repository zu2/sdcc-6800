;-------------------------------------------------------------------------
; _mullong.s - MC6800 32-bit multiplication for the mc6800 calling convention
;
; Adapted from chibicc-6800-v1 libc/__mul32x32.s.
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

	.module _mullong
	.globl __mullong
	.globl REGTEMP2
	.globl REGTEMP3
	.globl REGTEMP4
	.globl REGTEMP5
	.globl REGTEMP6
	.globl REGTEMP7
	.globl ___SDCC_mc6800_ret0
	.globl ___SDCC_mc6800_ret1
	.globl ___SDCC_mc6800_ret2
	.globl ___SDCC_mc6800_ret3
	.globl __mullong_PARM_1
	.globl __mullong_PARM_2
	.area XSEG
__mullong_PARM_1:
	.ds 4
__mullong_PARM_2:
	.ds 4
	.area CSEG (CODE)

; Return the low 32 bits of the product.
__mullong:
	ldx __mullong_PARM_1
	stx *REGTEMP4
	ldx __mullong_PARM_1+2
	stx *REGTEMP5
	ldx __mullong_PARM_2
	stx *REGTEMP3
	ldx __mullong_PARM_2+2
	stx *REGTEMP6
        ldx #0
        stx *REGTEMP2
;
        clrb
        clra
;
	ldx #8
;
        lsr  *REGTEMP4+3
00001$:
        bcc  00002$
        addb *REGTEMP6+1
        adca *REGTEMP6
        psha
        ldaa *REGTEMP3+1
        adca *REGTEMP2+1
        staa *REGTEMP2+1
        ldaa *REGTEMP3
        adca *REGTEMP2
        staa *REGTEMP2
        pula
00002$: ror REGTEMP2
        ror *REGTEMP2+1
        rora
        rorb
        ror *REGTEMP4+3
	dex
	bne 00001$
;
	ldx #8
	lsr *REGTEMP4+2
00003$:
        bcc  00004$
        addb *REGTEMP6+1
        adca *REGTEMP6
        psha
        ldaa *REGTEMP3+1
        adca *REGTEMP2+1
        staa *REGTEMP2+1
        pula
00004$: ror REGTEMP2+1
        rora
        rorb
        ror *REGTEMP4+2
	dex
	bne 00003$
;
	ldx #8
	lsr *REGTEMP4+1
00005$:
        bcc  00006$
        addb *REGTEMP6+1
        adca *REGTEMP6
00006$: rora
        rorb
        ror *REGTEMP4+1
	dex
	bne 00005$
;
	ldx #8
	ldaa *REGTEMP4
	lsra
00007$:
        bcc  00008$
        addb *REGTEMP6+1
00008$: rorb
        rora
	dex
	bne 00007$
;
	staa *REGTEMP4
	ldaa *(REGTEMP4+3)
	staa *___SDCC_mc6800_ret0
	ldaa *(REGTEMP4+2)
	staa *___SDCC_mc6800_ret1
	ldaa *(REGTEMP4+1)
	staa *___SDCC_mc6800_ret2
	ldaa *(REGTEMP4+0)
	staa *___SDCC_mc6800_ret3
	rts
