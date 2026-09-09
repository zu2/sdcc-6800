	.module _xinit
	.globl __sdcc_init_xdata
	.globl s_XINIT
	.globl l_XINIT
	.globl s_XISEG

	.area ZP (PAG)
xisrc:	.blkb	2
xidst:	.blkb	2
xiend:	.blkb	2

	.area CSEG (CODE)
__sdcc_init_xdata:
	ldx	#l_XINIT
	beq	00002$
	ldab	#<s_XISEG
	addb	#<l_XINIT
	ldaa	#>s_XISEG
	adca	#>l_XINIT
	stab	*xiend+1
	staa	*xiend
	ldx	#s_XINIT
	stx	*xisrc
	ldx	#s_XISEG
	stx	*xidst
00001$:
	ldx	*xisrc
	ldaa	0,x
	inx
	stx	*xisrc
	ldx	*xidst
	staa	0,x
	inx
	stx	*xidst
	cpx	*xiend
	bne	00001$
00002$:
	rts
