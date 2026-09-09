/* mc6800pst.c */

/*
 *  Copyright (C) 1993-2023  Alan R. Baldwin
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

/*
 * Mnemonic Structure
 */
struct	mne	mne[] = {

        /* machine */

        /* system */

    {   NULL,   "CON",          S_ATYP,         0,      A_CON   },
    {   NULL,   "OVR",          S_ATYP,         0,      A_OVR   },
    {   NULL,   "REL",          S_ATYP,         0,      A_REL   },
    {   NULL,   "ABS",          S_ATYP,         0,      A_ABS   },
    {   NULL,   "NOPAG",        S_ATYP,         0,      A_NOPAG },
    {   NULL,   "PAG",          S_ATYP,         0,      A_PAG   },

    {   NULL,   "CODE",         S_ATYP,         0,      A_CODE  },
    {   NULL,   "DATA",         S_ATYP,         0,      A_DATA  },
    {   NULL,   "LOAD",         S_ATYP,         0,      A_LOAD  },
    {   NULL,   "NOLOAD",       S_ATYP,         0,      A_NOLOAD },

    {	NULL,	".page",	S_PAGE,		0,	0	},
    {	NULL,	".title",	S_HEADER,	0,	O_TITLE	},
    {	NULL,	".sbttl",	S_HEADER,	0,	O_SBTTL	},
    {	NULL,	".module",	S_MODUL,	0,	0	},
    {	NULL,	".include",	S_INCL,		0,	I_CODE	},
    {	NULL,	".incbin",	S_INCL,		0,	I_BNRY	},
    {	NULL,	".area",	S_AREA,		0,	0	},
//    {	NULL,	".psharea",	S_AREA,		0,	O_PSH	},
//    {	NULL,	".poparea",	S_AREA,		0,	O_POP	},
//    {	NULL,	".bank",	S_BANK,		0,	0	},
    {	NULL,	".org",		S_ORG,		0,	0	},
    {	NULL,	".radix",	S_RADIX,	0,	0	},
    {	NULL,	".globl",	S_GLOBL,	0,	0	},
    {	NULL,	".local",	S_LOCAL,	0,	0	},
    {	NULL,	".if",		S_CONDITIONAL,	0,	O_IF	},
    {	NULL,	".iff",		S_CONDITIONAL,	0,	O_IFF	},
    {	NULL,	".ift",		S_CONDITIONAL,	0,	O_IFT	},
    {	NULL,	".iftf",	S_CONDITIONAL,	0,	O_IFTF	},
    {	NULL,	".ifdef",	S_CONDITIONAL,	0,	O_IFDEF	},
    {	NULL,	".ifndef",	S_CONDITIONAL,	0,	O_IFNDEF},
    {	NULL,	".ifgt",	S_CONDITIONAL,	0,	O_IFGT	},
    {	NULL,	".iflt",	S_CONDITIONAL,	0,	O_IFLT	},
    {	NULL,	".ifge",	S_CONDITIONAL,	0,	O_IFGE	},
    {	NULL,	".ifle",	S_CONDITIONAL,	0,	O_IFLE	},
    {	NULL,	".ifeq",	S_CONDITIONAL,	0,	O_IFEQ	},
    {	NULL,	".ifne",	S_CONDITIONAL,	0,	O_IFNE	},
    {	NULL,	".ifb",		S_CONDITIONAL,	0,	O_IFB	},
    {	NULL,	".ifnb",	S_CONDITIONAL,	0,	O_IFNB	},
    {	NULL,	".ifidn",	S_CONDITIONAL,	0,	O_IFIDN	},
    {	NULL,	".ifdif",	S_CONDITIONAL,	0,	O_IFDIF	},
    {	NULL,	".iif",		S_CONDITIONAL,	0,	O_IIF	},
    {	NULL,	".iiff",	S_CONDITIONAL,	0,	O_IIFF	},
    {	NULL,	".iift",	S_CONDITIONAL,	0,	O_IIFT	},
    {	NULL,	".iiftf",	S_CONDITIONAL,	0,	O_IIFTF	},
    {	NULL,	".iifdef",	S_CONDITIONAL,	0,	O_IIFDEF},
    {	NULL,	".iifndef",	S_CONDITIONAL,	0,	O_IIFNDEF},
    {	NULL,	".iifgt",	S_CONDITIONAL,	0,	O_IIFGT	},
    {	NULL,	".iiflt",	S_CONDITIONAL,	0,	O_IIFLT	},
    {	NULL,	".iifge",	S_CONDITIONAL,	0,	O_IIFGE	},
    {	NULL,	".iifle",	S_CONDITIONAL,	0,	O_IIFLE	},
    {	NULL,	".iifeq",	S_CONDITIONAL,	0,	O_IIFEQ	},
    {	NULL,	".iifne",	S_CONDITIONAL,	0,	O_IIFNE	},
    {	NULL,	".iifb",	S_CONDITIONAL,	0,	O_IIFB	},
    {	NULL,	".iifnb",	S_CONDITIONAL,	0,	O_IIFNB	},
    {	NULL,	".iifidn",	S_CONDITIONAL,	0,	O_IIFIDN},
    {	NULL,	".iifdif",	S_CONDITIONAL,	0,	O_IIFDIF},
    {	NULL,	".else",	S_CONDITIONAL,	0,	O_ELSE	},
    {	NULL,	".endif",	S_CONDITIONAL,	0,	O_ENDIF	},
    {	NULL,	".list",	S_LISTING,	0,	O_LIST	},
    {	NULL,	".nlist",	S_LISTING,	0,	O_NLIST	},
    {   NULL,   ".uleb128",     S_ULEB128,      0,      0       },
    {   NULL,   ".sleb128",     S_SLEB128,      0,      0       },
    {	NULL,	".equ",		S_EQU,		0,	O_EQU	},
    {	NULL,	".gblequ",	S_EQU,		0,	O_GBLEQU},
    {	NULL,	".lclequ",	S_EQU,		0,	O_LCLEQU},
    {	NULL,	".byte",	S_DATA,		0,	O_1BYTE	},
    {	NULL,	".db",		S_DATA,		0,	O_1BYTE	},
    {	NULL,	".fcb",		S_DATA,		0,	O_1BYTE	},
    {	NULL,	".word",	S_DATA,		0,	O_2BYTE	},
    {	NULL,	".dw",		S_DATA,		0,	O_2BYTE	},
    {	NULL,	".fdb",		S_DATA,		0,	O_2BYTE	},
/*    {	NULL,	".3byte",	S_DATA,		0,	O_3BYTE	},	*/
/*    {	NULL,	".triple",	S_DATA,		0,	O_3BYTE	},	*/
/*    {	NULL,	".dl",		S_DATA,		0,	O_4BYTE	},	*/
/*    {	NULL,	".4byte",	S_DATA,		0,	O_4BYTE	},	*/
/*    {	NULL,	".quad",	S_DATA,		0,	O_4BYTE	},	*/
/*    {	NULL,	".long",	S_DATA,		0,	O_4BYTE	},	*/
    {	NULL,	".blkb",	S_BLK,		0,	O_1BYTE	},
    {	NULL,	".ds",		S_BLK,		0,	O_1BYTE	},
    {	NULL,	".rmb",		S_BLK,		0,	O_1BYTE	},
    {	NULL,	".rs",		S_BLK,		0,	O_1BYTE	},
    {	NULL,	".blkw",	S_BLK,		0,	O_2BYTE	},
/*    {	NULL,	".blk3",	S_BLK,		0,	O_3BYTE	},	*/
/*    {	NULL,	".blk4",	S_BLK,		0,	O_4BYTE	},	*/
/*    {	NULL,	".blkl",	S_BLK,		0,	O_4BYTE	},	*/
    {	NULL,	".ascii",	S_ASCIX,	0,	O_ASCII	},
    {	NULL,	".ascis",	S_ASCIX,	0,	O_ASCIS	},
    {	NULL,	".asciz",	S_ASCIX,	0,	O_ASCIZ	},
    {	NULL,	".str",		S_ASCIX,	0,	O_ASCII	},
    {	NULL,	".strs",	S_ASCIX,	0,	O_ASCIS	},
    {	NULL,	".strz",	S_ASCIX,	0,	O_ASCIZ	},
    {	NULL,	".fcc",		S_ASCIX,	0,	O_ASCII	},
    {	NULL,	".define",	S_DEFINE,	0,	O_DEF	},
    {	NULL,	".undefine",	S_DEFINE,	0,	O_UNDEF	},
    {	NULL,	".even",	S_BOUNDARY,	0,	O_EVEN	},
    {	NULL,	".odd",		S_BOUNDARY,	0,	O_ODD	},
    {	NULL,	".bndry",	S_BOUNDARY,	0,	O_BNDRY	},
    {	NULL,	".msg"	,	S_MSG,		0,	0	},
    {	NULL,   ".assume",      S_ERROR,        0,      O_ASSUME},
    {	NULL,   ".error",       S_ERROR,        0,      O_ERROR	},
/*    {	NULL,	".msb",		S_MSB,		0,	0	},	*/
/*    {	NULL,	".lohi",	S_MSB,		0,	O_LOHI	},	*/
/*    {	NULL,	".hilo",	S_MSB,		0,	O_HILO	},	*/
/*    {	NULL,	".8bit",	S_BITS,		0,	O_1BYTE	},	*/
/*    {	NULL,	".16bit",	S_BITS,		0,	O_2BYTE	},	*/
/*    {	NULL,	".24bit",	S_BITS,		0,	O_3BYTE	},	*/
/*    {	NULL,	".32bit",	S_BITS,		0,	O_4BYTE	},	*/
//    {	NULL,	".end",		S_END,		0,	0	},

/* sdas specific */
    {   NULL,   ".optsdcc",     S_OPTSDCC,      0,      0       },
/* end sdas specific */

	/* Macro Processor */

    {	NULL,	".macro",	S_MACRO,	0,	O_MACRO	},
    {	NULL,	".endm",	S_MACRO,	0,	O_ENDM	},
    {	NULL,	".mexit",	S_MACRO,	0,	O_MEXIT	},

    {	NULL,	".narg",	S_MACRO,	0,	O_NARG	},
    {	NULL,	".nchr",	S_MACRO,	0,	O_NCHR	},
    {	NULL,	".ntyp",	S_MACRO,	0,	O_NTYP	},

    {	NULL,	".irp",		S_MACRO,	0,	O_IRP	},
    {	NULL,	".irpc",	S_MACRO,	0,	O_IRPC	},
    {	NULL,	".rept",	S_MACRO,	0,	O_REPT	},

    {	NULL,	".nval",	S_MACRO,	0,	O_NVAL	},

    {	NULL,	".mdelete",	S_MACRO,	0,	O_MDEL	},

	/* Special */

    {	NULL,	".setdp",	S_SDP,		0,	0	},
//    {	NULL,	".dpgbl",	S_PGD,		0,	0	},

	/* Machines */

    {   NULL,   ".6800",        S_CPU,          0,      X_6800  },

        /* MC6800 */

    {   NULL,   "nop",          S_INH,          0,      0x01    },
    {   NULL,   "tap",          S_INH,          0,      0x06    },
    {   NULL,   "tpa",          S_INH,          0,      0x07    },
    {   NULL,   "inx",          S_INH,          0,      0x08    },
    {   NULL,   "dex",          S_INH,          0,      0x09    },
    {   NULL,   "clv",          S_INH,          0,      0x0A    },
    {   NULL,   "sev",          S_INH,          0,      0x0B    },
    {   NULL,   "clc",          S_INH,          0,      0x0C    },
    {   NULL,   "sec",          S_INH,          0,      0x0D    },
    {   NULL,   "cli",          S_INH,          0,      0x0E    },
    {   NULL,   "sei",          S_INH,          0,      0x0F    },
    {   NULL,   "sba",          S_INH,          0,      0x10    },
    {   NULL,   "cba",          S_INH,          0,      0x11    },
    {   NULL,   "tab",          S_INH,          0,      0x16    },
    {   NULL,   "tba",          S_INH,          0,      0x17    },
    {   NULL,   "daa",          S_INH,          0,      0x19    },
    {   NULL,   "aba",          S_INH,          0,      0x1B    },
    {   NULL,   "tsx",          S_INH,          0,      0x30    },
    {   NULL,   "ins",          S_INH,          0,      0x31    },
    {   NULL,   "pula",         S_INH,          0,      0x32    },
    {   NULL,   "pulb",         S_INH,          0,      0x33    },
    {   NULL,   "des",          S_INH,          0,      0x34    },
    {   NULL,   "txs",          S_INH,          0,      0x35    },
    {   NULL,   "psha",         S_INH,          0,      0x36    },
    {   NULL,   "pshb",         S_INH,          0,      0x37    },
    {   NULL,   "rts",          S_INH,          0,      0x39    },
    {   NULL,   "rti",          S_INH,          0,      0x3B    },
    {   NULL,   "wai",          S_INH,          0,      0x3E    },
    {   NULL,   "swi",          S_INH,          0,      0x3F    },

    {   NULL,   "nega",         S_INH,          0,      0x40    },
    {   NULL,   "coma",         S_INH,          0,      0x43    },
    {   NULL,   "lsra",         S_INH,          0,      0x44    },
    {   NULL,   "rora",         S_INH,          0,      0x46    },
    {   NULL,   "asra",         S_INH,          0,      0x47    },
    {   NULL,   "asla",         S_INH,          0,      0x48    },
    {   NULL,   "lsla",         S_INH,          0,      0x48    },
    {   NULL,   "rola",         S_INH,          0,      0x49    },
    {   NULL,   "deca",         S_INH,          0,      0x4A    },
    {   NULL,   "inca",         S_INH,          0,      0x4C    },
    {   NULL,   "tsta",         S_INH,          0,      0x4D    },
    {   NULL,   "clra",         S_INH,          0,      0x4F    },

    {   NULL,   "negb",         S_INH,          0,      0x50    },
    {   NULL,   "comb",         S_INH,          0,      0x53    },
    {   NULL,   "lsrb",         S_INH,          0,      0x54    },
    {   NULL,   "rorb",         S_INH,          0,      0x56    },
    {   NULL,   "asrb",         S_INH,          0,      0x57    },
    {   NULL,   "aslb",         S_INH,          0,      0x58    },
    {   NULL,   "lslb",         S_INH,          0,      0x58    },
    {   NULL,   "rolb",         S_INH,          0,      0x59    },
    {   NULL,   "decb",         S_INH,          0,      0x5A    },
    {   NULL,   "incb",         S_INH,          0,      0x5C    },
    {   NULL,   "tstb",         S_INH,          0,      0x5D    },
    {   NULL,   "clrb",         S_INH,          0,      0x5F    },

    {   NULL,   "neg",          S_TYP1,         0,      0x60    },
    {   NULL,   "com",          S_TYP1,         0,      0x63    },
    {   NULL,   "lsr",          S_TYP1,         0,      0x64    },
    {   NULL,   "ror",          S_TYP1,         0,      0x66    },
    {   NULL,   "asr",          S_TYP1,         0,      0x67    },
    {   NULL,   "asl",          S_TYP1,         0,      0x68    },
    {   NULL,   "lsl",          S_TYP1,         0,      0x68    },
    {   NULL,   "rol",          S_TYP1,         0,      0x69    },
    {   NULL,   "dec",          S_TYP1,         0,      0x6A    },
    {   NULL,   "inc",          S_TYP1,         0,      0x6C    },
    {   NULL,   "tst",          S_TYP1,         0,      0x6D    },
    {   NULL,   "jmp",          S_TYP1,         0,      0x6E    },
    {   NULL,   "clr",          S_TYP1,         0,      0x6F    },

    {   NULL,   "suba",         S_TYP2,         0,      0x80    },
    {   NULL,   "cmpa",         S_TYP2,         0,      0x81    },
    {   NULL,   "sbca",         S_TYP2,         0,      0x82    },
    {   NULL,   "anda",         S_TYP2,         0,      0x84    },
    {   NULL,   "bita",         S_TYP2,         0,      0x85    },
    {   NULL,   "ldaa",         S_TYP2,         0,      0x86    },
    {   NULL,   "staa",         S_TYP4,         0,      0x87    },
    {   NULL,   "eora",         S_TYP2,         0,      0x88    },
    {   NULL,   "adca",         S_TYP2,         0,      0x89    },
    {   NULL,   "oraa",         S_TYP2,         0,      0x8A    },
    {   NULL,   "adda",         S_TYP2,         0,      0x8B    },
    {   NULL,   "cpx",          S_TYP3,         0,      0x8C    },
    {   NULL,   "jsr",          S_TYP5,         0,      0x8D    },
    {   NULL,   "lds",          S_TYP3,         0,      0x8E    },
    {   NULL,   "sts",          S_TYP4,         0,      0x8F    },

    {   NULL,   "subb",         S_TYP2,         0,      0xC0    },
    {   NULL,   "cmpb",         S_TYP2,         0,      0xC1    },
    {   NULL,   "sbcb",         S_TYP2,         0,      0xC2    },
    {   NULL,   "andb",         S_TYP2,         0,      0xC4    },
    {   NULL,   "bitb",         S_TYP2,         0,      0xC5    },
    {   NULL,   "ldab",         S_TYP2,         0,      0xC6    },
    {   NULL,   "stab",         S_TYP4,         0,      0xC7    },
    {   NULL,   "eorb",         S_TYP2,         0,      0xC8    },
    {   NULL,   "adcb",         S_TYP2,         0,      0xC9    },
    {   NULL,   "orab",         S_TYP2,         0,      0xCA    },
    {   NULL,   "addb",         S_TYP2,         0,      0xCB    },
    {   NULL,   "ldx",          S_TYP3,         0,      0xCE    },
    {   NULL,   "stx",          S_TYP4,         0,      0xCF    },

    {   NULL,   "bra",          S_BRA,          0,      0x20    },
    {   NULL,   "bhi",          S_BRA,          0,      0x22    },
    {   NULL,   "bls",          S_BRA,          0,      0x23    },
    {   NULL,   "bcc",          S_BRA,          0,      0x24    },
    {   NULL,   "bhs",          S_BRA,          0,      0x24    },
    {   NULL,   "bcs",          S_BRA,          0,      0x25    },
    {   NULL,   "blo",          S_BRA,          0,      0x25    },
    {   NULL,   "bne",          S_BRA,          0,      0x26    },
    {   NULL,   "beq",          S_BRA,          0,      0x27    },
    {   NULL,   "bvc",          S_BRA,          0,      0x28    },
    {   NULL,   "bvs",          S_BRA,          0,      0x29    },
    {   NULL,   "bpl",          S_BRA,          0,      0x2A    },
    {   NULL,   "bmi",          S_BRA,          0,      0x2B    },
    {   NULL,   "bge",          S_BRA,          0,      0x2C    },
    {   NULL,   "blt",          S_BRA,          0,      0x2D    },
    {   NULL,   "bgt",          S_BRA,          0,      0x2E    },
    {   NULL,   "ble",          S_BRA,          0,      0x2F    },
    {   NULL,   "bsr",          S_BRA,          S_EOL,  0x8D    }
};
