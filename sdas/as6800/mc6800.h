/* m6808.h */

/*
 *  Copyright (C) 1993-2025  Alan R. Baldwin
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Alan R. Baldwin
 * 721 Berkeley St.
 * Kent, Ohio  44240
 */

/*)BUILD
	$(PROGRAM) =	AS6800
	$(INCLUDE) = {
		ASXXXX.H
		MC6800.H
	}
	$(FILES) = {
		MC6800MCH.C
		MC6800ADR.C
		MC6800PST.C
		ASMAIN.C
		ASDBG.C
		ASLEX.C
		ASSYM.C
		ASSUBR.C
		ASEXPR.C
		ASDATA.C
		ASLIST.C
		ASOUT.C
	}
	$(STACK) = 3000
*/

struct adsym
{
	char	a_str[4];	/* addressing string */
	int	a_val;		/* addressing mode value */
};

/*
 * Addressing types
 */
#define	S_IMMED	30
#define	S_DIR	31
#define	S_EXT	32
#define	S_IX	33

/*
 * Registers
 */
#define	S_X	43

/*
 * Instruction types
 */
#define	S_INH	60	/* inherent */
#define	S_BRA	61	/* 8 bit relative */
#define	S_TYP1	62	/* n,x and extended (neg com lsr ... clr jmp) */
#define	S_TYP2	63	/* 1 byte immediate (suba ldaa ...) */
#define	S_TYP3	64	/* 2 byte immediate (cpx lds ldx) */
#define	S_TYP4	65	/* no immediate (staa stab sts stx) */
#define	S_TYP5	66	/* n,x and extended (jsr) */

/*
 * Special Types
 */
#define	S_SDP	80

/*
 * CPU Option
 */
#define	S_CPU	82

/*
 * Processor Types (S_CPU)
 */
#define	X_6800	0


	/* machine dependent functions */

	/* mc6800adr.c */
extern	struct	adsym	axs[];
extern	int		addr(struct expr *esp);
extern	int		admode(struct adsym *sp);
extern	int		srch(char *str);

	/* mc6800mch.c */
extern	struct  area	*zpg;
extern	void		machine(struct mne *mp);
extern	int		mchpcr(struct expr *esp);
extern	void		minit(void);
