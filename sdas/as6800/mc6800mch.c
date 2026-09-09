/* mc6800mch.c */

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

#include "asxxxx.h"
#include "mc6800.h"

char	*cpu	= "Motorola MC6800";
char	*dsft	= "asm";

/*
 * Opcode Cycle Definitions
 */
#define	OPCY_SDP	((char) (0xFF))
#define	OPCY_ERR	((char) (0xFE))
#define	OPCY_CPU	((char) (0xFD))

/*
 * The opcode of an MC6800 instruction follows the addressing mode:
 *
 *	S_TYP2	immediate op+0x00  direct   op+0x10
 *		indexed   op+0x20  extended op+0x30
 *	S_TYP3	the same, the immediate operand is 2 bytes
 *	S_TYP4	the same, there is no immediate form
 *	S_TYP1	indexed   op+0x00  extended op+0x10
 *	S_TYP5	indexed   op+0x20  extended op+0x30
 *
 * The cycle count is not filled in.  opcycles keeps the OPCY_NONE
 * value that asmain() sets, and the listing shows no cycle count.
 */

int mchtyp;
struct area *zpg;

/*
 * Process a machine op.
 */
void
machine(struct mne *mp)
{
	int op, t1, type;
	struct expr e1;
	struct area *espa;
	char id[NCPS];
	int c, v1;

	clrexpr(&e1);
	op = (int) mp->m_valu;
	type = mp->m_type;
	switch (type) {

	case S_SDP:
		opcycles = OPCY_SDP;
		espa = NULL;
		if (more()) {
			expr(&e1, 0);
			if (e1.e_flag == 0 && e1.e_base.e_ap == NULL) {
				if (e1.e_addr) {
					err('b');
				}
			}
			if ((c = getnb()) == ',') {
				getid(id, -1);
				espa = alookup(id);
				if (espa == NULL) {
					err('u');
				}
			} else {
				unget(c);
			}
		}
		if (espa) {
			outdp(espa, &e1, 0);
		} else {
			outdp(dot.s_area, &e1, 0);
		}
		lmode = SLIST;
		break;

	case S_CPU:
		opcycles = OPCY_CPU;
		mchtyp = op;
		sym[2].s_addr = op;
		lmode = SLIST;
		break;

	case S_INH:
		outab(op);
		break;

	case S_BRA:
		expr(&e1, 0);
		outab(op);
		if (mchpcr(&e1)) {
			v1 = (int) (e1.e_addr - dot.s_addr - 1);
			if ((v1 < -128) || (v1 > 127))
				aerr();
			outab(v1);
		} else {
			outrb(&e1, R_PCR);
		}
		if (e1.e_mode != S_USER)
			rerr();
		break;

	case S_TYP1:
		t1 = addr(&e1);
		if (t1 == S_IX) {
			outab(op);
			outrb(&e1, R_USGN);
			break;
		}
		if (t1 == S_DIR || t1 == S_EXT) {
			outab(op+0x10);
			outrw(&e1, 0);
			break;
		}
		aerr();
		break;

	case S_TYP2:
		t1 = addr(&e1);
		if (t1 == S_IMMED) {
			outab(op);
			outrb(&e1, 0);
			break;
		}
		goto memory;

	case S_TYP3:
		t1 = addr(&e1);
		if (t1 == S_IMMED) {
			outab(op);
			outrw(&e1, 0);
			break;
		}
		goto memory;

	case S_TYP4:
		t1 = addr(&e1);
memory:
		if (t1 == S_DIR) {
			outab(op+0x10);
			outrb(&e1, R_PAG0);
			break;
		}
		if (t1 == S_IX) {
			outab(op+0x20);
			outrb(&e1, R_USGN);
			break;
		}
		if (t1 == S_EXT) {
			outab(op+0x30);
			outrw(&e1, 0);
			break;
		}
		aerr();
		break;

	case S_TYP5:
		t1 = addr(&e1);
		if (t1 == S_IX) {
			outab(op+0x20);
			outrb(&e1, R_USGN);
			break;
		}
		if (t1 == S_DIR || t1 == S_EXT) {
			outab(op+0x30);
			outrw(&e1, 0);
			break;
		}
		aerr();
		break;

	default:
		opcycles = OPCY_ERR;
		err('o');
		break;
	}
}

/*
 * Branch/Jump PCR Mode Check
 */
int
mchpcr(struct expr *esp)
{
	if (esp->e_base.e_ap == dot.s_area) {
		return(1);
	}
	if (esp->e_flag==0 && esp->e_base.e_ap==NULL) {
		/*
		 * Absolute Destination
		 *
		 * Use the global symbol '.__.ABS.'
		 * of value zero and force the assembler
		 * to use this absolute constant as the
		 * base value for the relocation.
		 */
		esp->e_flag = 1;
		esp->e_base.e_sp = &sym[1];
	}
	return(0);
}

/*
 * Machine specific initialization.
 */
void
minit(void)
{
	/*
	 * Byte Order
	 */
	hilo = 1;

	/*
	 * Zero Page
	 */
	zpg = NULL;

	mchtyp = X_6800;
	sym[2].s_addr = X_6800;
}
