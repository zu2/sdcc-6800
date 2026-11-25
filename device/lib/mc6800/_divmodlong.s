;-------------------------------------------------------------------------
; _divmodlong.s - MC6800 32-bit division and remainder for the mc6800 calling convention
;
; Adapted from chibicc-6800-v1 libc/__div32x32.s.
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

	.module _divmodlong
	.globl ___divulong
	.globl ___modulong
	.globl ___divslong
	.globl ___modslong
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
	.globl ___divulong_PARM_1
	.globl ___divulong_PARM_2
	.globl ___modulong_PARM_1
	.globl ___modulong_PARM_2
	.globl ___divslong_PARM_1
	.globl ___divslong_PARM_2
	.globl ___modslong_PARM_1
	.globl ___modslong_PARM_2
	.area XSEG
___divulong_PARM_1:
	.ds 4
___divulong_PARM_2:
	.ds 4
___modulong_PARM_1:
	.ds 4
___modulong_PARM_2:
	.ds 4
___divslong_PARM_1:
	.ds 4
___divslong_PARM_2:
	.ds 4
___modslong_PARM_1:
	.ds 4
___modslong_PARM_2:
	.ds 4
	.area CSEG (CODE)

; Return the 32-bit quotient or remainder in ret0..ret3.
___divulong:
	ldx ___divulong_PARM_1
	stx *REGTEMP6
	ldx ___divulong_PARM_1+2
	stx *REGTEMP7
	ldx ___divulong_PARM_2
	stx *REGTEMP4
	ldx ___divulong_PARM_2+2
	stx *REGTEMP5
	jsr div32_core
	ldaa *(REGTEMP6+3)
	staa *___SDCC_mc6800_ret0
	ldaa *(REGTEMP6+2)
	staa *___SDCC_mc6800_ret1
	ldaa *(REGTEMP6+1)
	staa *___SDCC_mc6800_ret2
	ldaa *(REGTEMP6+0)
	staa *___SDCC_mc6800_ret3
	rts
___modulong:
	ldx ___modulong_PARM_1
	stx *REGTEMP6
	ldx ___modulong_PARM_1+2
	stx *REGTEMP7
	ldx ___modulong_PARM_2
	stx *REGTEMP4
	ldx ___modulong_PARM_2+2
	stx *REGTEMP5
	jsr div32_core
	staa *REGTEMP7
	stab *(REGTEMP7+1)
	ldx *REGTEMP2
	stx *REGTEMP6
	ldaa *(REGTEMP6+3)
	staa *___SDCC_mc6800_ret0
	ldaa *(REGTEMP6+2)
	staa *___SDCC_mc6800_ret1
	ldaa *(REGTEMP6+1)
	staa *___SDCC_mc6800_ret2
	ldaa *(REGTEMP6+0)
	staa *___SDCC_mc6800_ret3
	rts
___divslong:
	ldx ___divslong_PARM_1
	stx *REGTEMP6
	ldx ___divslong_PARM_1+2
	stx *REGTEMP7
	ldx ___divslong_PARM_2
	stx *REGTEMP4
	ldx ___divslong_PARM_2+2
	stx *REGTEMP5
	clr REGTEMP3
	tst REGTEMP6
	bpl divslong_x_pos
	ldx #REGTEMP6
	jsr div32_negate
	com REGTEMP3
divslong_x_pos:
	tst REGTEMP4
	bpl divslong_y_pos
	ldx #REGTEMP4
	jsr div32_negate
	com REGTEMP3
divslong_y_pos:
	ldaa *REGTEMP3
	psha
	jsr div32_core
	pula
	tsta
	bpl divslong_result_positive
	ldx #REGTEMP6
	jsr div32_negate
divslong_result_positive:
	ldaa *(REGTEMP6+3)
	staa *___SDCC_mc6800_ret0
	ldaa *(REGTEMP6+2)
	staa *___SDCC_mc6800_ret1
	ldaa *(REGTEMP6+1)
	staa *___SDCC_mc6800_ret2
	ldaa *(REGTEMP6+0)
	staa *___SDCC_mc6800_ret3
	rts
___modslong:
	ldx ___modslong_PARM_1
	stx *REGTEMP6
	ldx ___modslong_PARM_1+2
	stx *REGTEMP7
	ldx ___modslong_PARM_2
	stx *REGTEMP4
	ldx ___modslong_PARM_2+2
	stx *REGTEMP5
	clr REGTEMP3
	tst REGTEMP6
	bpl modslong_x_pos
	ldx #REGTEMP6
	jsr div32_negate
	com REGTEMP3
modslong_x_pos:
	tst REGTEMP4
	bpl modslong_y_pos
	ldx #REGTEMP4
	jsr div32_negate
modslong_y_pos:
	ldaa *REGTEMP3
	psha
	jsr div32_core
	staa *REGTEMP7
	stab *(REGTEMP7+1)
	ldx *REGTEMP2
	stx *REGTEMP6
	pula
	tsta
	bpl modslong_result_positive
	ldx #REGTEMP6
	jsr div32_negate
modslong_result_positive:
	ldaa *(REGTEMP6+3)
	staa *___SDCC_mc6800_ret0
	ldaa *(REGTEMP6+2)
	staa *___SDCC_mc6800_ret1
	ldaa *(REGTEMP6+1)
	staa *___SDCC_mc6800_ret2
	ldaa *(REGTEMP6+0)
	staa *___SDCC_mc6800_ret3
	rts

; REGTEMP6:7 = quotient; REGTEMP2:A:B = remainder.
; REGTEMP4:5 = divisor; REGTEMP3 is scratch.
div32_core:
	ldx #REGTEMP6
	clra
	clrb
	stab *(REGTEMP2+1)
	staa *REGTEMP2
div32_byte:
	stab *(REGTEMP3+1)
	ldab #8
	stab *REGTEMP3
	ldab *(REGTEMP3+1)
div32_bit:
	asl 0,x
	rolb
	rola
	rol REGTEMP2+1
	rol REGTEMP2
	pshb
	psha
	subb *(REGTEMP5+1)
	sbca *REGTEMP5
	ldab *(REGTEMP2+1)
	sbcb *(REGTEMP4+1)
	ldaa *REGTEMP2
	sbca *REGTEMP4
	bcs div32_restore
	stab *(REGTEMP2+1)
	staa *REGTEMP2
	pula
	pulb
	subb *(REGTEMP5+1)
	sbca *REGTEMP5
	inc 0,x
	bra div32_next
div32_restore:
	pula
	pulb
div32_next:
	dec REGTEMP3
	bne div32_bit
	inx
	cpx #REGTEMP6+4
	bne div32_byte
	rts

; Negate the big-endian 32-bit value at X.
div32_negate:
	com 0,x
	com 1,x
	com 2,x
	com 3,x
	inc 3,x
	bne div32_neg_done
	inc 2,x
	bne div32_neg_done
	inc 1,x
	bne div32_neg_done
	inc 0,x
div32_neg_done:
	rts
