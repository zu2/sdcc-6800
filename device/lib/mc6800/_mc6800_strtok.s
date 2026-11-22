;-------------------------------------------------------------------------
; _memcpy.s - MC6800 memcpy for the mc6800 calling convention
;
; Adapted from chibicc-6800-v1 libc/__strtok.s.
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

	.module _mc6800_strtok
	.globl _strtok
	.globl REGTEMP2
	.globl REGTEMP3
	.globl REGTEMP4
	.globl REGTEMP5
	.area XSEG
strtok_next:
	.ds 2
	.globl _strtok_PARM_2
_strtok_PARM_2:
	.ds 2
	.area CSEG (CODE)
_strtok:
	staa *REGTEMP2
	stab *(REGTEMP2+1)
	ldx _strtok_PARM_2
	stx *REGTEMP3
	ldaa *REGTEMP2
	ldab *(REGTEMP2+1)
	staa *REGTEMP5
	orab *REGTEMP5
	bne strtok_skip
	ldx strtok_next
	stx *REGTEMP2
strtok_skip:
	ldx *REGTEMP2
	beq strtok_null
	ldaa 0,x
	beq strtok_finish
	ldx *REGTEMP3
strtok_skip_scan:
	ldab 0,x
	beq strtok_token
	cba
	beq strtok_skip_one
	inx
	bra strtok_skip_scan
strtok_skip_one:
	ldx *REGTEMP2
	inx
	stx *REGTEMP2
	bra strtok_skip
strtok_token:
	ldx *REGTEMP2
	stx *REGTEMP4
strtok_advance:
	ldx *REGTEMP2
	ldaa 0,x
	beq strtok_finish_token
	ldx *REGTEMP3
strtok_find_scan:
	ldab 0,x
	beq strtok_next_char
	cba
	beq strtok_delimiter
	inx
	bra strtok_find_scan
strtok_next_char:
	ldx *REGTEMP2
	inx
	stx *REGTEMP2
	bra strtok_advance
strtok_delimiter:
	ldx *REGTEMP2
	clr 0,x
	inx
	stx strtok_next
	bra strtok_return
strtok_finish_token:
	ldx #0
	stx strtok_next
strtok_return:
	ldaa *REGTEMP4
	ldab *(REGTEMP4+1)
	rts
strtok_finish:
	ldx #0
	stx strtok_next
strtok_null:
	clra
	clrb
	rts
