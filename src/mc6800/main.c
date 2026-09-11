/*-------------------------------------------------------------------------
  main.h - mc6800 specific general function

  Copyright (C) 2003, Erik Petrich

  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the
  Free Software Foundation; either version 2, or (at your option) any
  later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
-------------------------------------------------------------------------*/
/*
    Note that mlh prepended _mc6800_ on the static functions.  Makes
    it easier to set a breakpoint using the debugger.
*/
#include "common.h"
#include "mc6800.h"
#include "main.h"
#include "ralloc.h"
#include "gen.h"
#include "dbuf_string.h"

extern char * iComments2;
extern DEBUGFILE dwarf2DebugFile;
extern int dwarf2FinalizeFile(FILE *);

static char _mc6800_defaultRules[] =
{
#include "peeph.rul"
};

MC6800_OPTS mc6800_opts;

static const mc6800opcodedata mc6800opcodeDataTable[] =
{
  { ".db",   {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {1, 0}, {0, 0}}, 0, 0, 0      , 0      ,  0, "......", OP_SPECIAL },
  { "adda",  {{2, 2}, {2, 3}, {2, 5}, {3, 4}, {0, 0}, {0, 0}}, 1, 0, M_A    , M_A    ,  0, "t.tttt", OP_NORMAL },
  { "addb",  {{2, 2}, {2, 3}, {2, 5}, {3, 4}, {0, 0}, {0, 0}}, 1, 0, M_B    , M_B    ,  0, "t.tttt", OP_NORMAL },
  { "adca",  {{2, 2}, {2, 3}, {2, 5}, {3, 4}, {0, 0}, {0, 0}}, 1, 0, M_A    , M_A    ,  0, "t.tttt", OP_NORMAL },
  { "adcb",  {{2, 2}, {2, 3}, {2, 5}, {3, 4}, {0, 0}, {0, 0}}, 1, 0, M_B    , M_B    ,  0, "t.tttt", OP_NORMAL },
  { "anda",  {{2, 2}, {2, 3}, {2, 5}, {3, 4}, {0, 0}, {0, 0}}, 1, 0, M_A    , M_A    ,  0, "..ttR.", OP_NORMAL },
  { "andb",  {{2, 2}, {2, 3}, {2, 5}, {3, 4}, {0, 0}, {0, 0}}, 1, 0, M_B    , M_B    ,  0, "..ttR.", OP_NORMAL },
  { "bita",  {{2, 2}, {2, 3}, {2, 5}, {3, 4}, {0, 0}, {0, 0}}, 1, 0, M_A    , 0      ,  0, "..ttR.", OP_NORMAL },
  { "bitb",  {{2, 2}, {2, 3}, {2, 5}, {3, 4}, {0, 0}, {0, 0}}, 1, 0, M_B    , 0      ,  0, "..ttR.", OP_NORMAL },
  { "cmpa",  {{2, 2}, {2, 3}, {2, 5}, {3, 4}, {0, 0}, {0, 0}}, 1, 0, M_A    , 0      ,  0, "..tttt", OP_NORMAL },
  { "cmpb",  {{2, 2}, {2, 3}, {2, 5}, {3, 4}, {0, 0}, {0, 0}}, 1, 0, M_B    , 0      ,  0, "..tttt", OP_NORMAL },
  { "eora",  {{2, 2}, {2, 3}, {2, 5}, {3, 4}, {0, 0}, {0, 0}}, 1, 0, M_A    , M_A    ,  0, "..ttR.", OP_NORMAL },
  { "eorb",  {{2, 2}, {2, 3}, {2, 5}, {3, 4}, {0, 0}, {0, 0}}, 1, 0, M_B    , M_B    ,  0, "..ttR.", OP_NORMAL },
  { "ldaa",  {{2, 2}, {2, 3}, {2, 5}, {3, 4}, {0, 0}, {0, 0}}, 1, 0, 0      , M_A    ,  0, "..ttR.", OP_NORMAL },
  { "ldab",  {{2, 2}, {2, 3}, {2, 5}, {3, 4}, {0, 0}, {0, 0}}, 1, 0, 0      , M_B    ,  0, "..ttR.", OP_NORMAL },
  { "oraa",  {{2, 2}, {2, 3}, {2, 5}, {3, 4}, {0, 0}, {0, 0}}, 1, 0, M_A    , M_A    ,  0, "..ttR.", OP_NORMAL },
  { "orab",  {{2, 2}, {2, 3}, {2, 5}, {3, 4}, {0, 0}, {0, 0}}, 1, 0, M_B    , M_B    ,  0, "..ttR.", OP_NORMAL },
  { "sbca",  {{2, 2}, {2, 3}, {2, 5}, {3, 4}, {0, 0}, {0, 0}}, 1, 0, M_A    , M_A    ,  0, "..tttt", OP_NORMAL },
  { "sbcb",  {{2, 2}, {2, 3}, {2, 5}, {3, 4}, {0, 0}, {0, 0}}, 1, 0, M_B    , M_B    ,  0, "..tttt", OP_NORMAL },
  { "suba",  {{2, 2}, {2, 3}, {2, 5}, {3, 4}, {0, 0}, {0, 0}}, 1, 0, M_A    , M_A    ,  0, "..tttt", OP_NORMAL },
  { "subb",  {{2, 2}, {2, 3}, {2, 5}, {3, 4}, {0, 0}, {0, 0}}, 1, 0, M_B    , M_B    ,  0, "..tttt", OP_NORMAL },
  { "staa",  {{0, 0}, {2, 4}, {2, 6}, {3, 5}, {0, 0}, {0, 0}}, 0, 1, M_A    , 0      ,  0, "..ttR.", OP_NORMAL },
  { "stab",  {{0, 0}, {2, 4}, {2, 6}, {3, 5}, {0, 0}, {0, 0}}, 0, 1, M_B    , 0      ,  0, "..ttR.", OP_NORMAL },
  { "clr",   {{0, 0}, {0, 0}, {2, 7}, {3, 6}, {0, 0}, {0, 0}}, 0, 1, 0      , 0      ,  0, "..RSRR", OP_NORMAL },
  { "com",   {{0, 0}, {0, 0}, {2, 7}, {3, 6}, {0, 0}, {0, 0}}, 1, 1, 0      , 0      ,  0, "..ttRS", OP_NORMAL },
  { "neg",   {{0, 0}, {0, 0}, {2, 7}, {3, 6}, {0, 0}, {0, 0}}, 1, 1, 0      , 0      ,  0, "..tttt", OP_NORMAL },
  { "dec",   {{0, 0}, {0, 0}, {2, 7}, {3, 6}, {0, 0}, {0, 0}}, 1, 1, 0      , 0      ,  0, "..ttt.", OP_NORMAL },
  { "inc",   {{0, 0}, {0, 0}, {2, 7}, {3, 6}, {0, 0}, {0, 0}}, 1, 1, 0      , 0      ,  0, "..ttt.", OP_NORMAL },
  { "rol",   {{0, 0}, {0, 0}, {2, 7}, {3, 6}, {0, 0}, {0, 0}}, 1, 1, 0      , 0      ,  0, "..tttt", OP_NORMAL },
  { "ror",   {{0, 0}, {0, 0}, {2, 7}, {3, 6}, {0, 0}, {0, 0}}, 1, 1, 0      , 0      ,  0, "..tttt", OP_NORMAL },
  { "asl",   {{0, 0}, {0, 0}, {2, 7}, {3, 6}, {0, 0}, {0, 0}}, 1, 1, 0      , 0      ,  0, "..tttt", OP_NORMAL },
  { "asr",   {{0, 0}, {0, 0}, {2, 7}, {3, 6}, {0, 0}, {0, 0}}, 1, 1, 0      , 0      ,  0, "..tttt", OP_NORMAL },
  { "lsr",   {{0, 0}, {0, 0}, {2, 7}, {3, 6}, {0, 0}, {0, 0}}, 1, 1, 0      , 0      ,  0, "..Rttt", OP_NORMAL },
  { "tst",   {{0, 0}, {0, 0}, {2, 7}, {3, 6}, {0, 0}, {0, 0}}, 1, 0, 0      , 0      ,  0, "..ttRR", OP_NORMAL },
  { "clra",  {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {1, 2}, {0, 0}}, 0, 0, 0      , M_A    ,  0, "..RSRR", OP_NORMAL },
  { "clrb",  {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {1, 2}, {0, 0}}, 0, 0, 0      , M_B    ,  0, "..RSRR", OP_NORMAL },
  { "coma",  {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {1, 2}, {0, 0}}, 0, 0, M_A    , M_A    ,  0, "..ttRS", OP_NORMAL },
  { "comb",  {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {1, 2}, {0, 0}}, 0, 0, M_B    , M_B    ,  0, "..ttRS", OP_NORMAL },
  { "nega",  {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {1, 2}, {0, 0}}, 0, 0, M_A    , M_A    ,  0, "..tttt", OP_NORMAL },
  { "negb",  {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {1, 2}, {0, 0}}, 0, 0, M_B    , M_B    ,  0, "..tttt", OP_NORMAL },
  { "deca",  {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {1, 2}, {0, 0}}, 0, 0, M_A    , M_A    ,  0, "..ttt.", OP_NORMAL },
  { "decb",  {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {1, 2}, {0, 0}}, 0, 0, M_B    , M_B    ,  0, "..ttt.", OP_NORMAL },
  { "inca",  {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {1, 2}, {0, 0}}, 0, 0, M_A    , M_A    ,  0, "..ttt.", OP_NORMAL },
  { "incb",  {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {1, 2}, {0, 0}}, 0, 0, M_B    , M_B    ,  0, "..ttt.", OP_NORMAL },
  { "rola",  {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {1, 2}, {0, 0}}, 0, 0, M_A    , M_A    ,  0, "..tttt", OP_NORMAL },
  { "rolb",  {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {1, 2}, {0, 0}}, 0, 0, M_B    , M_B    ,  0, "..tttt", OP_NORMAL },
  { "rora",  {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {1, 2}, {0, 0}}, 0, 0, M_A    , M_A    ,  0, "..tttt", OP_NORMAL },
  { "rorb",  {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {1, 2}, {0, 0}}, 0, 0, M_B    , M_B    ,  0, "..tttt", OP_NORMAL },
  { "asla",  {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {1, 2}, {0, 0}}, 0, 0, M_A    , M_A    ,  0, "..tttt", OP_NORMAL },
  { "aslb",  {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {1, 2}, {0, 0}}, 0, 0, M_B    , M_B    ,  0, "..tttt", OP_NORMAL },
  { "asra",  {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {1, 2}, {0, 0}}, 0, 0, M_A    , M_A    ,  0, "..tttt", OP_NORMAL },
  { "asrb",  {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {1, 2}, {0, 0}}, 0, 0, M_B    , M_B    ,  0, "..tttt", OP_NORMAL },
  { "lsra",  {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {1, 2}, {0, 0}}, 0, 0, M_A    , M_A    ,  0, "..Rttt", OP_NORMAL },
  { "lsrb",  {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {1, 2}, {0, 0}}, 0, 0, M_B    , M_B    ,  0, "..Rttt", OP_NORMAL },
  { "tsta",  {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {1, 2}, {0, 0}}, 0, 0, M_A    , 0      ,  0, "..ttRR", OP_NORMAL },
  { "tstb",  {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {1, 2}, {0, 0}}, 0, 0, M_B    , 0      ,  0, "..ttRR", OP_NORMAL },
  { "aba",   {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {1, 2}, {0, 0}}, 0, 0, M_A|M_B, M_A    ,  0, "t.tttt", OP_NORMAL },
  { "sba",   {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {1, 2}, {0, 0}}, 0, 0, M_A|M_B, M_A    ,  0, "..tttt", OP_NORMAL },
  { "cba",   {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {1, 2}, {0, 0}}, 0, 0, M_A|M_B, 0      ,  0, "..tttt", OP_NORMAL },
  { "daa",   {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {1, 2}, {0, 0}}, 0, 0, M_A    , M_A    ,  0, "..tttt", OP_NORMAL },
  { "tab",   {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {1, 2}, {0, 0}}, 0, 0, M_A    , M_B    ,  0, "..ttR.", OP_NORMAL },
  { "tba",   {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {1, 2}, {0, 0}}, 0, 0, M_B    , M_A    ,  0, "..ttR.", OP_NORMAL },
  { "psha",  {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {1, 4}, {0, 0}}, 0, 0, M_A    , 0      , -1, "......", OP_NORMAL },
  { "pshb",  {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {1, 4}, {0, 0}}, 0, 0, M_B    , 0      , -1, "......", OP_NORMAL },
  { "pula",  {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {1, 4}, {0, 0}}, 0, 0, 0      , M_A    ,  1, "......", OP_NORMAL },
  { "pulb",  {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {1, 4}, {0, 0}}, 0, 0, 0      , M_B    ,  1, "......", OP_NORMAL },
  { "cpx",   {{3, 3}, {2, 4}, {2, 6}, {3, 5}, {0, 0}, {0, 0}}, 1, 0, M_X    , 0      ,  0, "..ttt.", OP_NORMAL },
  { "ldx",   {{3, 3}, {2, 4}, {2, 6}, {3, 5}, {0, 0}, {0, 0}}, 1, 0, 0      , M_X    ,  0, "..ttR.", OP_NORMAL },
  { "lds",   {{3, 3}, {2, 4}, {2, 6}, {3, 5}, {0, 0}, {0, 0}}, 1, 0, 0      , M_S    ,  0, "..ttR.", OP_NORMAL },
  { "stx",   {{0, 0}, {2, 5}, {2, 7}, {3, 6}, {0, 0}, {0, 0}}, 0, 1, M_X    , 0      ,  0, "..ttR.", OP_NORMAL },
  { "sts",   {{0, 0}, {2, 5}, {2, 7}, {3, 6}, {0, 0}, {0, 0}}, 0, 1, M_S    , 0      ,  0, "..ttR.", OP_NORMAL },
  { "inx",   {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {1, 4}, {0, 0}}, 0, 0, M_X    , M_X    ,  0, "...t..", OP_NORMAL },
  { "dex",   {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {1, 4}, {0, 0}}, 0, 0, M_X    , M_X    ,  0, "...t..", OP_NORMAL },
  { "ins",   {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {1, 4}, {0, 0}}, 0, 0, M_S    , M_S    ,  1, "......", OP_NORMAL },
  { "des",   {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {1, 4}, {0, 0}}, 0, 0, M_S    , M_S    , -1, "......", OP_NORMAL },
  { "txs",   {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {1, 4}, {0, 0}}, 0, 0, M_X    , M_S    ,  0, "......", OP_NORMAL },
  { "tsx",   {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {1, 4}, {0, 0}}, 0, 0, M_S    , M_X    ,  0, "......", OP_NORMAL },
  { "bra",   {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {2, 4}}, 0, 0, 0      , 0      ,  0, "......", OP_BR },
  { "bcc",   {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {2, 4}}, 0, 0, 0      , 0      ,  0, "......", OP_BR },
  { "bcs",   {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {2, 4}}, 0, 0, 0      , 0      ,  0, "......", OP_BR },
  { "beq",   {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {2, 4}}, 0, 0, 0      , 0      ,  0, "......", OP_BR },
  { "bge",   {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {2, 4}}, 0, 0, 0      , 0      ,  0, "......", OP_BR },
  { "bgt",   {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {2, 4}}, 0, 0, 0      , 0      ,  0, "......", OP_BR },
  { "bhi",   {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {2, 4}}, 0, 0, 0      , 0      ,  0, "......", OP_BR },
  { "ble",   {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {2, 4}}, 0, 0, 0      , 0      ,  0, "......", OP_BR },
  { "bls",   {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {2, 4}}, 0, 0, 0      , 0      ,  0, "......", OP_BR },
  { "blt",   {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {2, 4}}, 0, 0, 0      , 0      ,  0, "......", OP_BR },
  { "bmi",   {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {2, 4}}, 0, 0, 0      , 0      ,  0, "......", OP_BR },
  { "bne",   {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {2, 4}}, 0, 0, 0      , 0      ,  0, "......", OP_BR },
  { "bvc",   {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {2, 4}}, 0, 0, 0      , 0      ,  0, "......", OP_BR },
  { "bvs",   {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {2, 4}}, 0, 0, 0      , 0      ,  0, "......", OP_BR },
  { "bpl",   {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {2, 4}}, 0, 0, 0      , 0      ,  0, "......", OP_BR },
  { "bsr",   {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {2, 8}}, 0, 0, 0      , 0      ,  0, "......", OP_BR },
  { "jmp",   {{0, 0}, {0, 0}, {2, 4}, {3, 3}, {0, 0}, {0, 0}}, 0, 0, 0      , 0      ,  0, "......", OP_JMP },
  { "jsr",   {{0, 0}, {0, 0}, {2, 8}, {3, 9}, {0, 0}, {0, 0}}, 0, 0, 0      , 0      ,  0, "......", OP_JMP },
  { "nop",   {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {1, 2}, {0, 0}}, 0, 0, 0      , 0      ,  0, "......", OP_SPECIAL },
  { "rti",   {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {1,10}, {0, 0}}, 0, 0, 0      , 0      ,  0, "tttttt", OP_SPECIAL },
  { "rts",   {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {1, 5}, {0, 0}}, 0, 0, 0      , 0      ,  0, "......", OP_SPECIAL },
  { "swi",   {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {1,12}, {0, 0}}, 0, 0, 0      , 0      ,  0, ".S....", OP_SPECIAL },
  { "wai",   {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {1, 9}, {0, 0}}, 0, 0, 0      , 0      ,  0, ".t....", OP_SPECIAL },
  { "clc",   {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {1, 2}, {0, 0}}, 0, 0, 0      , 0      ,  0, ".....R", OP_NORMAL },
  { "cli",   {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {1, 2}, {0, 0}}, 0, 0, 0      , 0      ,  0, ".R....", OP_NORMAL },
  { "clv",   {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {1, 2}, {0, 0}}, 0, 0, 0      , 0      ,  0, "....R.", OP_NORMAL },
  { "sec",   {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {1, 2}, {0, 0}}, 0, 0, 0      , 0      ,  0, ".....S", OP_NORMAL },
  { "sei",   {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {1, 2}, {0, 0}}, 0, 0, 0      , 0      ,  0, ".S....", OP_NORMAL },
  { "sev",   {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {1, 2}, {0, 0}}, 0, 0, 0      , 0      ,  0, "....S.", OP_NORMAL },
  { "tap",   {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {1, 2}, {0, 0}}, 0, 0, M_A    , 0      ,  0, "tttttt", OP_NORMAL },
  { "tpa",   {{0, 0}, {0, 0}, {0, 0}, {0, 0}, {1, 2}, {0, 0}}, 0, 0, 0      , M_A    ,  0, "......", OP_NORMAL },
};

const mc6800opcodedata *
mc6800_getOpcodeData (const char *inst)
{
  unsigned int i;

  for (i = 0; i < sizeof (mc6800opcodeDataTable) / sizeof (mc6800opcodedata); i++)
    if (!strcmp (inst, mc6800opcodeDataTable[i].name))
      return &mc6800opcodeDataTable[i];

  return NULL;
}

/* list of key words used by msc51 */
static char *_mc6800_keywords[] =
{
  "at",
  //"bit",
  "code",
  "critical",
  "data",
  "far",
  //"idata",
  "interrupt",
  "near",
  //"pdata",
  "reentrant",
  //"sfr",
  //"sbit",
  //"using",
  "xdata",
  "_data",
  "_code",
  "_generic",
  "_near",
  "_xdata",
  //"_pdata",
  //"_idata",
  "_naked",
  "_overlay",
  NULL
};


void mc6800_assignRegisters (ebbIndex *);

static int regParmFlg;      /* determine if we can register a parameter */
static struct sym_link *regParmFuncType;

static void
_mc6800_init (void)
{
  mc6800_opts.sub = SUB_MC6800;
  asm_addTree (&asm_asxxxx_mapping);
}

static void
_mc6800_reset_regparm (struct sym_link *funcType)
{
  regParmFlg = 0;
  regParmFuncType = funcType;
}

static int
_mc6800_regparm (sym_link * l, bool reentrant)
{
  if (IFFUNC_HASVARARGS (regParmFuncType))
    return 0;

  if (IS_STRUCT (l))
    return 0;

  int size = getSize(l);

  /* If they fit completely, the first two bytes of parameters can go */
  /* into A and X, otherwise, they go on the stack. Examples:         */
  /*   foo(char p1)                    A <- p1                        */
  /*   foo(char p1, char p2)           A <- p1, X <- p2               */
  /*   foo(char p1, char p2, char p3)  A <- p1, X <- p2, stack <- p3  */
  /*   foo(int p1)                     XA <- p1                       */
  /*   foo(long p1)                    stack <- p1                    */
  /*   foo(char p1, int p2)            A <- p1, stack <- p2           */
  /*   foo(int p1, char p2)            XA <- p1, stack <- p2          */

  if (regParmFlg>=2)
    return 0;

  if ((regParmFlg+size)>2)
    {
      regParmFlg = 2;
      return 0;
    }

  regParmFlg += size;
  return 1+regParmFlg-size;
}

static bool
_mc6800_parseOptions (int *pargc, char **argv, int *i)
{
  if (!strcmp (argv[*i], "--out-fmt-elf"))
    {
      options.out_fmt = 'E';
      debugFile = &dwarf2DebugFile;
      return true;
    }

  return false;
}

#define OPTION_SMALL_MODEL          "--model-small"
#define OPTION_LARGE_MODEL          "--model-large"
#define OPTION_NO_STD_CRT0          "--no-std-crt0"

static OPTION _mc6800_options[] =
  {
    {0, OPTION_SMALL_MODEL, NULL, "8-bit address space for data"},
    {0, OPTION_LARGE_MODEL, NULL, "16-bit address space for data (default)"},
    {0, "--out-fmt-elf", NULL, "Output executable in ELF format" },
    {0, OPTION_NO_STD_CRT0, &options.no_std_crt0, "Do not link default crt0.rel"},
    {0, NULL }
  };

static void
_mc6800_finaliseOptions (void)
{
  if (options.noXinitOpt)
    port->genXINIT = 0;

  if (options.model == MODEL_LARGE) {
      port->mem.default_local_map = xdata;
      port->mem.default_globl_map = xdata;
    }
  else
    {
      port->mem.default_local_map = data;
      port->mem.default_globl_map = data;
    }

  istack->ptrType = FPOINTER;
}

static void
_mc6800_setDefaultOptions (void)
{
  options.code_loc = 0x0100;
  options.data_loc = 0;
  options.xdata_loc = 0;        /* 0 means immediately following data */
  options.stack_loc = 0x7fff;
  options.out_fmt = 's';        /* use motorola S19 output */

  options.omitFramePtr = 1;     /* no frame pointer (we use SP */
                                /* offsets instead)            */
  options.noOverlay = 1;
}

static const char *
_mc6800_getRegName (const struct reg_info *reg)
{
  if (reg)
    return reg->name;
  return "err";
}

static void
_mc6800_genAssemblerStart (FILE * of)
{
  if (!options.noOptsdccInAsm)
    fprintf (of, "\t.optsdcc -m%s\n", port->target);

  fprintf (of, "\n");

  tfprintf (of, "\t!area\n",HOME_NAME);
  tfprintf (of, "\t.area GSINIT0 (CODE)\n");
  tfprintf (of, "\t!area\n",STATIC_NAME);
  tfprintf (of, "\t!area\n",GSFINAL_NAME);
  tfprintf (of, "\t!area\n",CODE_NAME);
  tfprintf (of, "\t!area\n",XINIT_NAME);
  tfprintf (of, "\t!area\n",CONST_NAME);
  tfprintf (of, "\t!area\n",DATA_NAME);
  tfprintf (of, "\t!area\n",OVERLAY_NAME);
  tfprintf (of, "\t!area\n",XDATA_NAME);
  tfprintf (of, "\t!area\n",XIDATA_NAME);

}

static void
_mc6800_genAssemblerEnd (FILE * of)
{
  if (options.out_fmt == 'E' && options.debug)
    {
      dwarf2FinalizeFile (of);
    }
}

static void
_mc6800_genExtraAreaLinkOptions (FILE * of)
{
  fprintf (of, "-g __sdcc_stack_top=0x%04x\n", options.stack_loc);
}

static void
_mc6800_genExtraAreas (FILE * asmFile, bool mainExists)
{
    fprintf (asmFile, "%s", iComments2);
    fprintf (asmFile, "; extended address mode data\n");
    fprintf (asmFile, "%s", iComments2);
    dbuf_write_and_destroy (&xdata->oBuf, asmFile);
}

/* Generate code to copy XINIT to XISEG */
static void _mc6800_genXINIT (FILE * of) {
  fprintf (of, ";       _mc6800_genXINIT() start\n");
  fprintf (of, ";       _mc6800_genXINIT() end\n");
}


/* Do CSE estimation */
static bool cseCostEstimation (iCode *ic, iCode *pdic)
{
    operand *result = IC_RESULT(ic);
    sym_link *result_type = operandType(result);

    /* if it is a pointer then return ok for now */
    if (IC_RESULT(ic) && IS_PTR(result_type)) return 1;

    if (ic->op == ADDRESS_OF)
      return 0;

    /* if bitwise | add & subtract then no since mc6800 is pretty good at it
       so we will cse only if they are local (i.e. both ic & pdic belong to
       the same basic block */
    if (IS_BITWISE_OP(ic) || ic->op == '+' || ic->op == '-') {
        /* then if they are the same Basic block then ok */
        if (ic->eBBlockNum == pdic->eBBlockNum) return 1;
        else return 0;
    }

    /* for others it is cheaper to do the cse */
    return 1;
}

/* Indicate which extended bit operations this port supports */
static bool
hasExtBitOp (int op, sym_link *left, int right)
{
  switch (op)
    {
    case GETABIT:
    case GETBYTE:
    case GETWORD:
      return true;
    case ROT:
      {
        unsigned int lbits = bitsForType (left);
        if (lbits % 8)
          return false;
        if (lbits == 8)
          return true;
        if (lbits > (unsigned)port->support.shift*8)
          return false;
        if (right % lbits  == 1 || right % lbits == lbits - 1)
          return true;
        if (lbits <= 16 && lbits == right * 2)
          return true;
      }
      return false;
    }
  return false;
}

/* Indicate the expense of an access to an output storage class */
static int
oclsExpense (struct memmap *oclass)
{
  /* The mc6800's addressing modes allow access to all storage classes */
  /* inexpensively (<=0) */

  if (IN_DIRSPACE (oclass))     /* direct addressing mode is fastest */
    return -2;
  if (IN_FARSPACE (oclass))     /* extended addressing mode is almost at fast */
    return -1;
  if (oclass == istack)         /* stack is the slowest, but still faster than */
    return 0;                   /* trying to copy to a temp location elsewhere */

  return 0; /* anything we missed */
}

/*----------------------------------------------------------------------*/
/* mc6800_dwarfRegNum - return the DWARF register number for a register.  */
/*   These are defined for the MC6800 in "Motorola 8- and 16-bit Embedded */
/*   Application Binary Interface (M8/16EABI)"                          */
/*----------------------------------------------------------------------*/
static int
mc6800_dwarfRegNum (const struct reg_info *reg)
{
  switch (reg->rIdx)
    {
    case A_IDX: return 0;
    case B_IDX: return 1;
    case X_IDX: return 7;
    case D_IDX: return 3;
    case CND_IDX: return 17;
    case SP_IDX: return 15;
    }
  return -1;
}

static bool
_hasNativeMulFor (iCode *ic, sym_link *left, sym_link *right)
{
  wassert (ic->op == '*' || ic->op == '/' || ic->op == '%');

  if (IS_BITINT (OP_SYM_TYPE (IC_RESULT(ic))) && SPEC_BITINTWIDTH (OP_SYM_TYPE (IC_RESULT(ic))) % 8)
    return false;

  return getSize (left) == 1 && getSize (right) == 1;
}

typedef struct asmLineNode
  {
    int size;
  }
asmLineNode;

static asmLineNode *
newAsmLineNode (void)
{
  asmLineNode *aln;

  aln = Safe_alloc ( sizeof (asmLineNode));
  aln->size = 0;

  return aln;
}

/*--------------------------------------------------------------------*/
/* Given an instruction and its first two operands, compute the       */
/* instruction size. There are a few cases where it's too complicated */
/* to distinguish between an 8-bit offset and 16-bit offset; in these */
/* cases we conservatively assume the 16-bit offset size.             */
/*--------------------------------------------------------------------*/
static int
mc6800_instructionSize (const char *inst, const char *op1, const char *op2)
{
  const mc6800opcodedata *opcode = mc6800_getOpcodeData (inst);
  int mode, only, i, n;

  if (!opcode)
    return 999;

  for (i = 0, n = 0, only = MODE_INH; i < MODE_COUNT; i++)
    if (opcode->mode[i].bytes)
      {
        n++;
        only = i;
      }

  if (n == 1)
    mode = only;
  else if (op2[0] == 'x')
    mode = MODE_IDX;
  else if (op1[0] == '#')
    mode = MODE_IMM;
  else if (op1[0] == '*')
    mode = MODE_DIR;
  else if (!op1[0])
    mode = MODE_INH;
  else
    mode = MODE_EXT;

  if (!opcode->mode[mode].bytes)
    return 999;

  return opcode->mode[mode].bytes;
}


static asmLineNode *
mc6800_asmLineNodeFromLineNode (lineNode *ln)
{
  asmLineNode *aln = newAsmLineNode();
  char *op, op1[256], op2[256];
  int opsize;
  const char *p;
  char inst[8];

  p = ln->line;

  while (*p && isspace(*p)) p++;
  for (op = inst, opsize=1; *p; p++)
    {
      if (isspace(*p) || *p == ';' || *p == ':' || *p == '=')
        break;
      else
        if (opsize < sizeof(inst))
          *op++ = tolower(*p), opsize++;
    }
  *op = '\0';

  if (*p == ';' || *p == ':' || *p == '=')
    return aln;

  while (*p && isspace(*p)) p++;
  if (*p == '=')
    return aln;

  for (op = op1, opsize=1; *p && *p != ','; p++)
    {
      if (!isspace(*p) && opsize < sizeof(op1))
        *op++ = tolower(*p), opsize++;
    }
  *op = '\0';

  if (*p == ',') p++;
  for (op = op2, opsize=1; *p && *p != ','; p++)
    {
      if (!isspace(*p) && opsize < sizeof(op2))
        *op++ = tolower(*p), opsize++;
    }
  *op = '\0';

  aln->size = mc6800_instructionSize(inst, op1, op2);

  return aln;
}

static int
mc6800_getInstructionSize (lineNode *line)
{
  if (!line->aln)
    line->aln = (asmLineNodeBase *) mc6800_asmLineNodeFromLineNode (line);

  return line->aln->size;
}

static const char *
mc6800_get_model (void)
{
    return(options.stackAuto ? "mc6800-stack-auto" : "s08");
}

/** $1 is always the basename.
    $2 is always the output file.
    $3 varies
    $l is the list of extra options that should be there somewhere...
    $L is the list of extra options that should be passed on the command line...
    MUST be terminated with a NULL.
*/
static const char *_linkCmd[] =
{
  "sdld6808", "-nf", "$1", "$L", NULL
};

/* $3 is replaced by assembler.debug_opts resp. port->assembler.plain_opts */
static const char *_asmCmd[] =
{
  "sdas6800", "$l", "$3", "$2", "$1.asm", NULL
};

static const char * const _crt[] = { "crt0.rel", NULL, };

static const char * const _libs_mc6800[] = { "mc6800", NULL, };

/* Globals */
PORT mc6800_port =
{
  TARGET_ID_MC6800,
  "mc6800",
  "MC6800",                       /* Target name */
  NULL,                         /* Processor name */
  {
    glue,
    false,                      /* Emit glue around main */
    MODEL_SMALL | MODEL_LARGE,
    MODEL_LARGE,
    NULL,                       /* model == target */
  },
  {
    _asmCmd,
    NULL,
    "-plosgffwy",               /* Options with debug */
    "-plosgffw",                /* Options without debug */
    0,
    ".asm",
    NULL                        /* no do_assemble function */
  },
  {                             /* Linker */
    _linkCmd,
    NULL,
    NULL,
    ".rel",
    1,
    _crt,                       /* crt */
    _libs_mc6800,                 /* libs */
  },
  {                             /* Peephole optimizer */
    _mc6800_defaultRules,
    mc6800_getInstructionSize,
  },
  {
    /* Sizes: char, short, int, long, long long, near ptr, far ptr, gptr, func ptr, banked func ptr, bit, float, _BitInt (in bits) */
    1, 2, 2, 4, 8, 2, 2, 2, 2, 0, 1, 4, 64
  },
  /* tags for generic pointers */
  { 0x00, 0x00, 0x00, 0x00 },           /* far, near, xstack, code */
  {
    "XSEG",
    "STACK",
    "CSEG    (CODE)",
    "ZP      (PAG)",
    NULL, /* "ISEG" */
    NULL, /* "PSEG" */
    "XSEG",
    NULL,                // xconst_name
    NULL, /* "BSEG" */
    "RSEG    (ABS)",
    "GSINIT  (CODE)",
    "OSEG    (PAG, OVR)",
    "GSFINAL (CODE)",
    "HOME    (CODE)",
    "XISEG",              // initialized xdata
    "XINIT   (CODE)",     // a code copy of xiseg
    "CONST   (CODE)",     // const_name - const data (code or not)
    "CABS    (ABS,CODE)", // cabs_name - const absolute data (code or not)
    "XABS    (ABS)",      // xabs_name - absolute xdata
    "IABS    (ABS)",      // iabs_name - absolute data
    NULL,                 // name of segment for initialized variables
    NULL,                 // name of segment for copies of initialized variables in code space
    NULL,
    NULL,
    1,
    false,                // doesn't matter, as port has no __sfr anyway
    1                     // No fancy alignments supported.
  },
  { _mc6800_genExtraAreas,
    _mc6800_genExtraAreaLinkOptions },
  0,                      // ABI revision
  {
    -1,         /* direction (-1 = stack grows down) */
    0,          /* bank_overhead (switch between register banks) */
    4,          /* isr_overhead */
    2,          /* call_overhead */
    0,          /* reent_overhead */
    0,          /* banked_overhead (switch between code banks) */
    1           /* sp is offset by 1 from last item pushed */
  },
  {
    5, false, false
  },
  {
    mc6800_emitDebuggerSymbol,
    {
      mc6800_dwarfRegNum,
      NULL,
      NULL,
      4,                        /* addressSize */
      14,                       /* regNumRet */
      15,                       /* regNumSP */
      -1,                       /* regNumBP */
      1,                        /* offsetSP */
    },
  },
  {
    256,        /* maxCount */
    2,          /* sizeofElement */
    {8,16,32},  /* sizeofMatchJump[] */
    {8,16,32},  /* sizeofRangeCompare[] */
    5,          /* sizeofSubtract */
    10,         /* sizeofDispatch */
  },
  "_",
  _mc6800_init,
  _mc6800_parseOptions,
  _mc6800_options,
  NULL,
  _mc6800_finaliseOptions,
  _mc6800_setDefaultOptions,
  mc6800_assignRegisters,
  _mc6800_getRegName,
  0,
  NULL,
  _mc6800_keywords,
  _mc6800_genAssemblerStart,
  _mc6800_genAssemblerEnd,        /* no genAssemblerEnd */
  NULL,                         /* genIVT */
  _mc6800_genXINIT,
  NULL,                         /* genInitStartup */
  _mc6800_reset_regparm,
  _mc6800_regparm,
  NULL,                         /* process_pragma */
  NULL,                         /* getMangledFunctionName */
  _hasNativeMulFor,             /* hasNativeMulFor */
  hasExtBitOp,                  /* hasExtBitOp */
  oclsExpense,                  /* oclsExpense */
  true,                         /* use_dw_for_init */
  false,                        /* little_endian */
  0,                            /* leave lt */
  0,                            /* leave gt */
  1,                            /* transform <= to ! > */
  1,                            /* transform >= to ! < */
  1,                            /* transform != to !(a == b) */
  0,                            /* leave == */
  false,                        /* No array initializer support. */
  cseCostEstimation,
  "",                           // no builtin functions
  GPOINTER,                     // treat unqualified pointers as "generic" pointers
  true,
  false,
  1,                            /* reset labelKey to 1 */
  1,                            /* globals & local statics allowed */
  4,                            /* Number of registers handled in the tree-decomposition-based register allocator in SDCCralloc.hpp */
  PORT_MAGIC
};

