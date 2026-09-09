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

static OPTION _mc6800_options[] =
  {
    {0, OPTION_SMALL_MODEL, NULL, "8-bit address space for data"},
    {0, OPTION_LARGE_MODEL, NULL, "16-bit address space for data (default)"},
    {0, "--out-fmt-elf", NULL, "Output executable in ELF format" },
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
  options.code_loc = 0x8000;
  options.data_loc = 0x80;
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
  int i;
  int needOrg = 1;
  symbol *mainExists=newSymbol("main", 0);
  mainExists->block=0;

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

  if ((mainExists=findSymWithLevel(SymbolTab, mainExists)))
    {
      // generate interrupt vector table
      fprintf (of, "\t.area\tCODEIVT (ABS)\n");

      for (i=maxInterrupts;i>0;i--)
        {
          if (interrupts[i])
            {
              if (needOrg)
                {
                  fprintf (of, "\t.org\t0x%04x\n", (0xfffe - (i * 2)));
                  needOrg = 0;
                }
              fprintf (of, "\t.dw\t%s\n", interrupts[i]->rname);
            }
          else
            needOrg = 1;
        }
      if (needOrg)
        fprintf (of, "\t.org\t0xfffe\n");
      fprintf (of, "\t.dw\t%s", "__sdcc_gs_init_startup\n\n");

      fprintf (of, "\t.area GSINIT0\n");
      fprintf (of, "__sdcc_gs_init_startup:\n");
      if (options.stack_loc)
        {
          fprintf (of, "\tldhx\t#0x%04x\n", options.stack_loc+1);
          fprintf (of, "\ttxs\n");
        }
      else
        fprintf (of, "\trsp\n");
      fprintf (of, "\tjsr\t___sdcc_external_startup\n");
      fprintf (of, "\tbeq\t__sdcc_init_data\n");
      fprintf (of, "\tjmp\t__sdcc_program_startup\n");
      fprintf (of, "__sdcc_init_data:\n");

      fprintf (of, "; _mc6800_genXINIT() start\n");
      fprintf (of, "        ldhx #0\n");
      fprintf (of, "00001$:\n");
      fprintf (of, "        cphx #l_XINIT\n");
      fprintf (of, "        beq  00002$\n");
      fprintf (of, "        lda  s_XINIT,x\n");
      fprintf (of, "        sta  s_XISEG,x\n");
      fprintf (of, "        aix  #1\n");
      fprintf (of, "        bra  00001$\n");
      fprintf (of, "00002$:\n");
      fprintf (of, "; _mc6800_genXINIT() end\n");

      fprintf (of, "\t.area GSFINAL\n");
      fprintf (of, "\tjmp\t__sdcc_program_startup\n\n");

      fprintf (of, "\t.area CSEG\n");
      fprintf (of, "__sdcc_program_startup:\n");
      fprintf (of, "\tjsr\t_main\n");
      fprintf (of, "\tbra\t.\n");

    }
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
_mc6800_genExtraAreas (FILE * asmFile, bool mainExists)
{
    fprintf (asmFile, "%s", iComments2);
    fprintf (asmFile, "; extended address mode data\n");
    fprintf (asmFile, "%s", iComments2);
    dbuf_write_and_destroy (&xdata->oBuf, asmFile);
}

/* Generate interrupt vector table. */
static int
_mc6800_genIVT (struct dbuf_s * oBuf, symbol ** interrupts, int maxInterrupts)
{
  int i;

  dbuf_printf (oBuf, "\t.area\tCODEIVT (ABS)\n");
  dbuf_printf (oBuf, "\t.org\t0x%04x\n",
    (0xfffe - (maxInterrupts * 2)));

  for (i=maxInterrupts;i>0;i--)
    {
      if (interrupts[i])
        dbuf_printf (oBuf, "\t.dw\t%s\n", interrupts[i]->rname);
      else
        dbuf_printf (oBuf, "\t.dw\t0xffff\n");
    }
  dbuf_printf (oBuf, "\t.dw\t%s", "__sdcc_gs_init_startup\n");

  return true;
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

typedef struct mc6800opcodedata
  {
    char name[6];
    char adrmode;
    /* info for registers used and/or modified by an instruction will be added here */
  }
mc6800opcodedata;

#define MC6800OP_STD 1
#define MC6800OP_RMW 2
#define MC6800OP_INH 3
#define MC6800OP_IM1 4
#define MC6800OP_BR 5
#define MC6800OP_BTB 6
#define MC6800OP_BSC 7
#define MC6800OP_MOV 8
#define MC6800OP_CBEQ 9
#define MC6800OP_CPHX 10
#define MC6800OP_LDHX 11
#define MC6800OP_STHX 12
#define MC6800OP_DBNZ 13

/* These must be kept sorted by opcode name */
static mc6800opcodedata mc6800opcodeDataTable[] =
  {
    {".db",   MC6800OP_INH}, /* used by the code generator only in the jump table */
    {"adc",   MC6800OP_STD},
    {"add",   MC6800OP_STD},
    {"ais",   MC6800OP_IM1},
    {"aix",   MC6800OP_IM1},
    {"and",   MC6800OP_STD},
    {"asl",   MC6800OP_RMW},
    {"asla",  MC6800OP_INH},
    {"aslx",  MC6800OP_INH},
    {"asr",   MC6800OP_RMW},
    {"asra",  MC6800OP_INH},
    {"asrx",  MC6800OP_INH},
    {"bcc",   MC6800OP_BR,},
    {"bclr",  MC6800OP_BSC},
    {"bcs",   MC6800OP_BR},
    {"beq",   MC6800OP_BR},
    {"bge",   MC6800OP_BR},
    {"bgnd",  MC6800OP_INH},
    {"bgt",   MC6800OP_BR},
    {"bhcc",  MC6800OP_BR},
    {"bhcs",  MC6800OP_BR},
    {"bhi",   MC6800OP_BR},
    {"bhs",   MC6800OP_BR},
    {"bih",   MC6800OP_BR},
    {"bil",   MC6800OP_BR},
    {"bit",   MC6800OP_STD},
    {"ble",   MC6800OP_BR},
    {"blo",   MC6800OP_BR},
    {"bls",   MC6800OP_BR},
    {"blt",   MC6800OP_BR},
    {"bmc",   MC6800OP_BR},
    {"bmi",   MC6800OP_BR},
    {"bms",   MC6800OP_BR},
    {"bne",   MC6800OP_BR},
    {"bpl",   MC6800OP_BR},
    {"bra",   MC6800OP_BR},
    {"brclr", MC6800OP_BTB},
    {"brn",   MC6800OP_BR},
    {"brset", MC6800OP_BTB},
    {"bset",  MC6800OP_BSC},
    {"bsr",   MC6800OP_BR},
    {"cbeq",  MC6800OP_CBEQ},
    {"cbeqa", MC6800OP_CBEQ},
    {"cbeqx", MC6800OP_CBEQ},
    {"clc",   MC6800OP_INH},
    {"cli",   MC6800OP_INH},
    {"clr",   MC6800OP_RMW},
    {"clra",  MC6800OP_INH},
    {"clrh",  MC6800OP_INH},
    {"clrx",  MC6800OP_INH},
    {"cmp",   MC6800OP_STD},
    {"com",   MC6800OP_RMW},
    {"coma",  MC6800OP_INH},
    {"comx",  MC6800OP_INH},
    {"cphx",  MC6800OP_CPHX},
    {"cpx",   MC6800OP_STD},
    {"daa",   MC6800OP_INH},
    {"dbnz",  MC6800OP_DBNZ},
    {"dbnza", MC6800OP_BR},
    {"dbnzx", MC6800OP_BR},
    {"dec",   MC6800OP_RMW},
    {"deca",  MC6800OP_INH},
    {"decx",  MC6800OP_INH},
    {"div",   MC6800OP_INH},
    {"eor",   MC6800OP_STD},
    {"inc",   MC6800OP_RMW},
    {"inca",  MC6800OP_INH},
    {"incx",  MC6800OP_INH},
    {"jmp",   MC6800OP_STD},
    {"jsr",   MC6800OP_STD},
    {"lda",   MC6800OP_STD},
    {"ldhx",  MC6800OP_LDHX},
    {"ldx",   MC6800OP_STD},
    {"lsl",   MC6800OP_RMW},
    {"lsla",  MC6800OP_INH},
    {"lslx",  MC6800OP_INH},
    {"lsr",   MC6800OP_RMW},
    {"lsra",  MC6800OP_INH},
    {"lsrx",  MC6800OP_INH},
    {"mov",   MC6800OP_MOV},
    {"mul",   MC6800OP_INH},
    {"neg",   MC6800OP_RMW},
    {"nega",  MC6800OP_INH},
    {"negx",  MC6800OP_INH},
    {"nop",   MC6800OP_INH},
    {"nsa",   MC6800OP_INH},
    {"ora",   MC6800OP_STD},
    {"psha",  MC6800OP_INH},
    {"pshh",  MC6800OP_INH},
    {"pshx",  MC6800OP_INH},
    {"pula",  MC6800OP_INH},
    {"pulh",  MC6800OP_INH},
    {"pulx",  MC6800OP_INH},
    {"rol",   MC6800OP_RMW},
    {"rola",  MC6800OP_INH},
    {"rolx",  MC6800OP_INH},
    {"ror",   MC6800OP_RMW},
    {"rora",  MC6800OP_INH},
    {"rorx",  MC6800OP_INH},
    {"rsp",   MC6800OP_INH},
    {"rti",   MC6800OP_INH},
    {"rts",   MC6800OP_INH},
    {"sbc",   MC6800OP_STD},
    {"sec",   MC6800OP_INH},
    {"sei",   MC6800OP_INH},
    {"sta",   MC6800OP_STD},
    {"sthx",  MC6800OP_STHX},
    {"stop",  MC6800OP_INH},
    {"stx",   MC6800OP_STD},
    {"sub",   MC6800OP_STD},
    {"swi",   MC6800OP_INH},
    {"tap",   MC6800OP_INH},
    {"tax",   MC6800OP_INH},
    {"tpa",   MC6800OP_INH},
    {"tst",   MC6800OP_RMW},
    {"tsta",  MC6800OP_INH},
    {"tstx",  MC6800OP_INH},
    {"tsx",   MC6800OP_INH},
    {"txa",   MC6800OP_INH},
    {"txs",   MC6800OP_INH},
    {"wait",  MC6800OP_INH}
  };

static int
mc6800_opcodeCompare (const void *key, const void *member)
{
  return strcmp((const char *)key, ((mc6800opcodedata *)member)->name);
}

/*--------------------------------------------------------------------*/
/* Given an instruction and its first two operands, compute the       */
/* instruction size. There are a few cases where it's too complicated */
/* to distinguish between an 8-bit offset and 16-bit offset; in these */
/* cases we conservatively assume the 16-bit offset size.             */
/*--------------------------------------------------------------------*/
static int
mc6800_instructionSize(const char *inst, const char *op1, const char *op2)
{
  mc6800opcodedata *opcode;
  int size;
  long offset;
  char * endnum = NULL;
  
  opcode = bsearch (inst, mc6800opcodeDataTable,
                    sizeof(mc6800opcodeDataTable)/sizeof(mc6800opcodedata),
                    sizeof(mc6800opcodedata), mc6800_opcodeCompare);

  if (!opcode)
    return 999;
  switch (opcode->adrmode)
    {
      case MC6800OP_INH: /* Inherent addressing mode */
        return 1;
        
      case MC6800OP_BSC: /* Bit set/clear direct addressing mode */
      case MC6800OP_BR:  /* Branch (1 byte signed offset) */
      case MC6800OP_IM1: /* 1 byte immediate addressing mode */
        return 2;
        
      case MC6800OP_BTB:  /* Bit test direct addressing mode and branch */
        return 3;
        
      case MC6800OP_RMW: /* read/modify/write instructions */
        if (!op2[0]) /* if not ,x or ,sp must be direct addressing mode */
          return 2;
        if (!op1[0])  /* if ,x with no offset */
          return 1;
        if (op2[0] == 'x')  /* if ,x with offset */
          return 2;
        return 3;  /* Otherwise, must be ,sp with offset */
        
      case MC6800OP_STD: /* standard instruction */
        if (!op2[0])
          {
            if (op1[0] == '#') /* Immediate addressing mode */
              return 2;
            if (op1[0] == '*') /* Direct addressing mode */
              return 2;
            return 3; /* Otherwise, must be extended addressing mode */
          }
        else
          {
            if (!op1[0]) /* if ,x with no offset */
              return 1;
            size = 2;
            if (op2[0] == 's')
              size++;
            offset = strtol (op1, &endnum, 0) & 0xffff;
            if (endnum && *endnum)
              size++;
            else if (offset > 0xff)
              size++;
            return size;
          }
      case MC6800OP_MOV:
        if (op2[0] == 'x')
          return 2;
        return 3;
      case MC6800OP_CBEQ:
        if (op2[0] == 'x' && !op1[0])
          return 2;  /* cbeq ,x+,rel */
        if (op2[0] == 's')
          return 4;  /* cbeq oprx8,sp,rel */
        return 3;
      case MC6800OP_CPHX:
        if (op1[0] == '*')
          return 2;
        return 3;
      case MC6800OP_DBNZ:
        if (!op2[0])
          return 2;
        if (!op1[0] && op2[0] == 'x')
          return 2;
        if (op2[0] == 's')
          return 4;
        return 3;
      case MC6800OP_LDHX:
      case MC6800OP_STHX:
        if (op1[0] == '*')
          return 2;
        if (!op1[0] && op2[0] == 'x')
          return 2;
        if (op2[0] == 's' || op1[0] == '#' || !op2[0])
          return 3;
        size = 3;
        offset = strtol (op1, &endnum, 0) & 0xffff;
        if (endnum && *endnum)
          size++;
        else if (offset > 0xff)
          size++;
        return size;
      default:
        return 4;
    }
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
    NULL,                       /* crt */
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
    "DSEG    (PAG)",
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
    NULL },
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
  _mc6800_genIVT,
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

