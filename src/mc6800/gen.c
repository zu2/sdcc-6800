/*-------------------------------------------------------------------------
  gen.c - source file for code generation for the 68MC6800

  Copyright (C) 1998, Sandeep Dutta . sandeep.dutta@usa.net
  Copyright (C) 1999, Jean-Louis VERN.jlvern@writeme.com
  Bug Fixes - Wojciech Stryjewski  wstryj1@tiger.lsu.edu (1999 v2.1.9a)
  Hacked for the 68MC6800:
  Copyright (C) 2003, Erik Petrich
  Copyright (c) 2023, Philipp Klaus Krause philipp@colecovision.eu

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

/* Use the D macro for basic (unobtrusive) debugging messages */
#define D(x) do if (options.verboseAsm) { x; } while (0)
/* Use the DD macro for detailed debugging messages */
#define DD(x)
//#define DD(x) x

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "common.h"
#include "mc6800.h"
#include "ralloc.h"
#include "gen.h"
#include "dbuf_string.h"

extern int allocInfo;
static int pushReg (reg_info * reg, bool freereg);
static void pullReg (reg_info * reg);
static void transferAopAop (asmop * srcaop, int srcofs, asmop * dstaop, int dstofs);
static void adjustStack (int n);

static char *zero = "#0x00";
static char *one = "#0x01";

static struct
{
  int stackOfs;
  int stackPushes;
  int param_offset;
  set *sendSet;
  int tempOfs;
}
_G;

static asmop *mc6800_aop_pass[2];
static asmop *mc6800_aop_ret[8];

static const char *tempname[NUM_TEMP_REGS] =
  {
    "REGTEMP0", "REGTEMP1",
    "REGTEMP2", "REGTEMP3",
    "REGTEMP4", "REGTEMP5",
    "REGTEMP6", "REGTEMP7"
  };

static const char *
allocTemp (void)
{
  wassertl (_G.tempOfs < NUM_TEMP_REGS, "out of temporaries");
  return tempname[_G.tempOfs++];
}

static const char *
freeTemp (void)
{
  wassertl (_G.tempOfs > 0, "temporary underflow");
  return tempname[--_G.tempOfs];
}
static asmop tsxaop;

extern int mc6800_dry_stack_size;
extern int mc6800_nRegs;
extern struct dbuf_s *codeOutBuf;
static bool operandsEqu (operand * op1, operand * op2);
static void loadRegFromConst (reg_info * reg, int c);
static asmop *newAsmop (short type);
static const char *aopAdrStr (asmop * aop, int loffset, bool bit16);
static void setupXForAop (asmop * aop);
static void setupXFromSP (int stackOffset);
static const char *setupTmpFromSP (int stackOffset);
static void updateiTempRegisterUse (operand * op);
static bool sameRegs (asmop *aop1, asmop *aop2);
static reg_info *mc6800_findRegAop (asmop *aop, int loffset);
static void mc6800_dirtyRegAop (asmop *aop, int loffset);
static void mc6800_emitLabel (symbol *tlbl);
#define IS_AOP_A(x) ((x)->regmask == MC6800MASK_A)
#define IS_AOP_B(x) ((x)->regmask == MC6800MASK_B)
#define IS_AOP_X(x) ((x)->regmask == MC6800MASK_X)
#define IS_AOP_D(x) ((x)->regmask == MC6800MASK_D)
#define IS_AOP_WITH_A(x) (((x)->regmask & MC6800MASK_A) != 0)
#define IS_AOP_WITH_B(x) (((x)->regmask & MC6800MASK_B) != 0)
#define IS_AOP_WITH_X(x) (((x)->regmask & MC6800MASK_X) != 0)

#define IS_AOPOFS_A(x,o) (((x)->type == AOP_REG) && ((x)->aopu.aop_reg[o]->mask == MC6800MASK_A))
#define IS_AOPOFS_B(x,o) (((x)->type == AOP_REG) && ((x)->aopu.aop_reg[o]->mask == MC6800MASK_B))
#define IS_AOPOFS_X(x,o) (((x)->type == AOP_REG) && ((x)->aopu.aop_reg[o]->mask == MC6800MASK_X))


#define LSB     0
#define MSB16   1
#define MSB24   2
#define MSB32   3

#define AOP(op) op->aop
#define AOP_TYPE(op) AOP(op)->type
#define AOP_SIZE(op) AOP(op)->size
#define AOP_OP(aop) aop->op

static bool regalloc_dry_run;
static unsigned int regalloc_dry_run_cost;
static float regalloc_dry_run_cost_cycles;

#define UNIMPLEMENTED do {if (!regalloc_dry_run) fatal (1, E_INTERNAL_ERROR, __FILE__, __LINE__, "Unimplemented"); regalloc_dry_run_cost += 1000; regalloc_dry_run_cost_cycles += 1000;} while(0)

static void
va_mc6800_emitOp (const char *inst, int mode, const char *fmt, va_list ap)
{
  const mc6800opcodedata *opcode = mc6800_getOpcodeData (inst);

  if (!opcode
      || !((mode == MODE_IDX && strstr (fmt, ",x"))
           || (mode == MODE_IMM && fmt[0] == '#')
           || (mode == MODE_DIR && fmt[0] == '*')
           || (mode == MODE_INH && !fmt[0])
           || (mode == MODE_EXT && fmt[0] && fmt[0] != '#' && fmt[0] != '*' && !strstr (fmt, ",x")))
      || !opcode->mode[mode].bytes)
    fatal (1, E_INTERNAL_ERROR, __FILE__, __LINE__, "unknown opcode or invalid addressing mode");

  regalloc_dry_run_cost += opcode->mode[mode].bytes;
  regalloc_dry_run_cost_cycles += opcode->mode[mode].cycles;
  if (opcode->change & M_A)
    mc6800_dirtyReg (mc6800_reg_a, false);
  if (opcode->change & M_B)
    mc6800_dirtyReg (mc6800_reg_b, false);
  if ((opcode->change & M_X) && mc6800_reg_x->aop && (mc6800_reg_x->aop->type == AOP_DIR || mc6800_reg_x->aop->type == AOP_EXT))
    mc6800_reg_x->aop = NULL;
  if (opcode->write && (mode == MODE_IDX || mode == MODE_EXT))
    {
      asmop *xaop = mc6800_reg_x->aop;

      mc6800_dirtyRegAop (NULL, 0);
      if (xaop && (xaop->type == AOP_DIR || xaop->type == AOP_EXT) && xaop->op && IS_ITEMP (xaop->op))
        mc6800_reg_x->aop = xaop;
    }
  if (!strcmp (inst, "jsr") || !strcmp (inst, "bsr") || !strcmp (inst, "swi"))
    mc6800_dirtyRegAop (NULL, 0);

  va_emitcode (inst, fmt, ap);
}

static void
mc6800_emitOp (const char *inst, int mode, const char *fmt, ...)
{
  va_list ap;

  va_start (ap, fmt);
  va_mc6800_emitOp (inst, mode, fmt, ap);
  va_end (ap);
}

static void
mc6800_emitOpWithAcc (const char *inst, reg_info *acc, int mode, const char *fmt, ...)
{
  char buf[sizeof (((mc6800opcodedata *) 0)->name)];
  va_list ap;

  wassertl (acc == mc6800_reg_a || acc == mc6800_reg_b, "mc6800_emitOpWithAcc: register must be A or B");
  wassertl (strlen (inst) + 1 < sizeof (buf), "mc6800_emitOpWithAcc: instruction name too long");
  SNPRINTF (buf, sizeof (buf), "%s%c", inst, acc == mc6800_reg_a ? 'a' : 'b');
  va_start (ap, fmt);
  va_mc6800_emitOp (buf, mode, fmt, ap);
  va_end (ap);
}

static void
mc6800_emitOp_o (const char *inst, asmop *aop, int loffset)
{
  int mode;
  const mc6800opcodedata *opcode = mc6800_getOpcodeData (inst);

  if (loffset > aop->size - 1 && aop->type != AOP_LIT)
    mode = MODE_IMM;
  else if (aop->type == AOP_LIT || aop->type == AOP_IMMD)
    mode = MODE_IMM;
  else if (aop->type == AOP_DIR)
    mode = MODE_DIR;
  else if (aop->type == AOP_EXT)
    mode = MODE_EXT;
  else if (aop->type == AOP_SOF || aop->type == AOP_IDX)
    mode = MODE_IDX;
  else
    fatal (1, E_INTERNAL_ERROR, __FILE__, __LINE__, "unsupported operand");

  if (!opcode
      || !opcode->mode[mode].bytes
      || !strcmp (inst, "ldx")
      || !strcmp (inst, "stx")
      || !strcmp (inst, "cpx")
      || !strcmp (inst, "lds")
      || !strcmp (inst, "sts"))
    fatal (1, E_INTERNAL_ERROR, __FILE__, __LINE__, "unknown opcode or invalid operand");

  regalloc_dry_run_cost += opcode->mode[mode].bytes;
  regalloc_dry_run_cost_cycles += opcode->mode[mode].cycles;
  if (opcode->change & M_A)
    mc6800_dirtyReg (mc6800_reg_a, false);
  if (opcode->change & M_B)
    mc6800_dirtyReg (mc6800_reg_b, false);
  if ((opcode->change & M_X) && mc6800_reg_x->aop && (mc6800_reg_x->aop->type == AOP_DIR || mc6800_reg_x->aop->type == AOP_EXT))
    mc6800_reg_x->aop = NULL;
  if (opcode->write)
    mc6800_dirtyRegAop (aop, loffset);

  emitcode (inst, "%s", aopAdrStr (aop, loffset, false));
}

static void
mc6800_emitOpw_o (const char *inst, asmop *aop, int loffset)
{
  int mode;
  const mc6800opcodedata *opcode = mc6800_getOpcodeData (inst);

  if (loffset > aop->size - 1 && aop->type != AOP_LIT)
    mode = MODE_IMM;
  else if (loffset + 1 > aop->size - 1 && aop->type != AOP_LIT)
    fatal (1, E_INTERNAL_ERROR, __FILE__, __LINE__, "operand out of range");
  else if (aop->type == AOP_LIT || aop->type == AOP_IMMD)
    mode = MODE_IMM;
  else if (aop->type == AOP_DIR)
    mode = MODE_DIR;
  else if (aop->type == AOP_EXT)
    mode = MODE_EXT;
  else if (aop->type == AOP_SOF || aop->type == AOP_IDX)
    mode = MODE_IDX;
  else
    fatal (1, E_INTERNAL_ERROR, __FILE__, __LINE__, "unsupported operand");

  if (!opcode
      || !opcode->mode[mode].bytes
      || (strcmp (inst, "ldx")
          && strcmp (inst, "stx")
          && strcmp (inst, "cpx")
          && strcmp (inst, "lds")
          && strcmp (inst, "sts")))
    fatal (1, E_INTERNAL_ERROR, __FILE__, __LINE__, "unknown opcode or invalid operand");

  regalloc_dry_run_cost += opcode->mode[mode].bytes;
  regalloc_dry_run_cost_cycles += opcode->mode[mode].cycles;
  if ((opcode->change & M_X) && mc6800_reg_x->aop && (mc6800_reg_x->aop->type == AOP_DIR || mc6800_reg_x->aop->type == AOP_EXT))
    mc6800_reg_x->aop = NULL;
  if (opcode->write)
    {
      mc6800_dirtyRegAop (aop, loffset);
      mc6800_dirtyRegAop (aop, loffset + 1);
    }

  emitcode (inst, "%s", aopAdrStr (aop, loffset, true));
}

static void
emitBranch (char *branchop, symbol * tlbl)
{
  const mc6800opcodedata *opcode = mc6800_getOpcodeData (branchop);
  int mode = strcmp (branchop, "jmp") ? MODE_REL : MODE_EXT;

  if (!regalloc_dry_run)
    emitcode (branchop, "%05d$", labelKey2num (tlbl->key));
  regalloc_dry_run_cost += opcode ? opcode->mode[mode].bytes : 3;
  regalloc_dry_run_cost_cycles += opcode ? opcode->mode[mode].cycles : 0;
}

/*-----------------------------------------------------------------*/
/* mc6800_emitDebuggerSymbol - associate the current code location   */
/*   with a debugger symbol                                        */
/*-----------------------------------------------------------------*/
void
mc6800_emitDebuggerSymbol (const char *debugSym)
{
  genLine.lineElement.isDebug = 1;
  emitcode ("", "%s ==.", debugSym);
  genLine.lineElement.isDebug = 0;
}

/*--------------------------------------------------------------------------*/
/* transferRegReg - Transfer from register(s) sreg to register(s) dreg. If  */
/*                  freesrc is true, sreg is marked free and available for  */
/*                  reuse. sreg and dreg must be of equal size              */
/*--------------------------------------------------------------------------*/
static void
transferRegReg (reg_info *sreg, reg_info *dreg, bool freesrc)
{
  int srcidx;
  int dstidx;
  char error = 0;

  /* Nothing to do if no destination. */
  if (!dreg)
    return;

  /* But it's definitely an error if there's no source. */
  if (!sreg)
    {
      werror (E_INTERNAL_ERROR, __FILE__, __LINE__, "NULL sreg in transferRegReg");
      return;
    }

  D (emitcode ("", ";     transferRegReg(%s,%s)", sreg->name, dreg->name));
  DD (emitcode ("", "; transferRegReg(%s,%s)", sreg->name, dreg->name));

  srcidx = sreg->rIdx;
  dstidx = dreg->rIdx;

  if (srcidx == dstidx)
    {
      mc6800_useReg (dreg);
      return;
    }

  switch (dstidx)
    {
    case A_IDX:
      switch (srcidx)
        {
        case B_IDX:            /* B to A */
          mc6800_emitOp ("tba", MODE_INH, "");
          break;
        default:
          error = 1;
        }
      break;
    case B_IDX:
      switch (srcidx)
        {
        case A_IDX:            /* A to B */
          mc6800_emitOp ("tab", MODE_INH, "");
          break;
        default:
          error = 1;
        }
      break;
    case X_IDX:
      switch (srcidx)
        {
        case D_IDX:            /* D to X */
          {
            const char *tmp = allocTemp ();
            mc6800_emitOp ("stab", MODE_DIR, "*%s+1", tmp);
            mc6800_emitOp ("staa", MODE_DIR, "*%s", tmp);
            mc6800_emitOp ("ldx", MODE_DIR, "*%s", tmp);
            freeTemp ();
          }
          break;
        default:
          error = 1;
        }
      break;
    case D_IDX:
      switch (srcidx)
        {
        case X_IDX:
          {
            const char *tmp = allocTemp ();
            mc6800_emitOp ("stx", MODE_DIR, "*%s", tmp);
            mc6800_emitOp ("ldab", MODE_DIR, "*%s+1", tmp);
            mc6800_emitOp ("ldaa", MODE_DIR, "*%s", tmp);
            freeTemp ();
          }
          break;
        default:
          error = 1;
        }
      break;
    default:
      error = 1;
    }

  D (emitcode ("", ";     transferRegReg(%s,%s) 2", sreg->name, dreg->name));
  wassertl (!error, "bad combo in transferRegReg");

  if (freesrc)
    mc6800_freeReg (sreg);

  dreg->isFree = false;
  mc6800_dirtyReg (dreg, false);
  mc6800_useReg (dreg);
  if (sreg->isLitConst)
    {
      dreg->isLitConst = sreg->isLitConst;
      dreg->litConst = sreg->litConst;
    }
  else
    {
      dreg->aop = sreg->aop;
      dreg->aopofs = sreg->aopofs;
      dreg->stackOffset = sreg->stackOffset;
    }
}

/*--------------------------------------------------------------------------*/
/* updateCFA - update the debugger information to reflect the current       */
/*             canonical frame address relative to the stack pointer        */
/*--------------------------------------------------------------------------*/
static void
updateCFA (void)
{
  /* there is no frame unless there is a function */
  if (!currFunc)
    return;

  if (options.debug && !regalloc_dry_run)
    debugFile->writeFrameAddress (NULL, mc6800_reg_sp, 1 + _G.stackOfs + _G.stackPushes);
}

/*-----------------------------------------------------------------*/
/* aopIsLitVal - asmop from offset is val                          */
/*-----------------------------------------------------------------*/
static bool
aopIsLitVal (const asmop *aop, int offset, int size, unsigned long long int val)
{
  wassert_bt (size <= sizeof (unsigned long long int)); // Make sure we are not testing outside of argument val.

  for(; size; size--, offset++)
    {
      unsigned char b = val & 0xff;
      val >>= 8;

      // Leading zeroes
      if (aop->size <= offset && !b && aop->type != AOP_LIT)
        continue;

      // Information from generalized constant propagation analysis
      if (!aop->valinfo.anything &&
        ((aop->valinfo.knownbitsmask >> (offset * 8)) & 0xff) == 0xff &&
        ((aop->valinfo.knownbits >> (offset * 8)) & 0xff) == b)
        continue;

      if (aop->type != AOP_LIT)
        return (false);

      if (byteOfVal (aop->aopu.aop_lit, offset) != b)
        return (false);
    }

  return (true);
}

/*--------------------------------------------------------------------------*/
/* pushReg - Push register reg onto the stack. If freereg is true, reg is   */
/*           marked free and available for reuse.                           */
/*--------------------------------------------------------------------------*/
static int
pushReg (reg_info * reg, bool freereg)
{
  int regidx = reg->rIdx;
  const char *tmp;

  switch (regidx)
    {
    case A_IDX:
      mc6800_emitOp ("psha", MODE_INH, "");
      _G.stackPushes++;
      updateCFA ();
      break;
    case B_IDX:
      mc6800_emitOp ("pshb", MODE_INH, "");
      _G.stackPushes++;
      updateCFA ();
      break;
    case X_IDX:
      tmp = allocTemp ();
      mc6800_emitOp ("stx", MODE_DIR, "*%s", tmp);
      break;
    case D_IDX:
      mc6800_emitOp ("pshb", MODE_INH, "");
      updateCFA ();
      _G.stackPushes++;
      mc6800_emitOp ("psha", MODE_INH, "");
      updateCFA ();
      _G.stackPushes++;
      break;
    default:
      break;
    }
  if (freereg)
    mc6800_freeReg (reg);
  return -_G.stackOfs - _G.stackPushes;
}

/*--------------------------------------------------------------------------*/
/* pullReg - Pull register reg off the stack.                               */
/*--------------------------------------------------------------------------*/
static void
pullReg (reg_info * reg)
{
  int regidx = reg->rIdx;
  const char *tmp;

  switch (regidx)
    {
    case A_IDX:
      mc6800_emitOp ("pula", MODE_INH, "");
      _G.stackPushes--;
      updateCFA ();
      break;
    case B_IDX:
      mc6800_emitOp ("pulb", MODE_INH, "");
      _G.stackPushes--;
      updateCFA ();
      break;
    case X_IDX:
      tmp = freeTemp ();
      mc6800_emitOp ("ldx", MODE_DIR, "*%s", tmp);
      break;
    case D_IDX:
      mc6800_emitOp ("pula", MODE_INH, "");
      _G.stackPushes--;
      updateCFA ();
      mc6800_emitOp ("pulb", MODE_INH, "");
      _G.stackPushes--;
      updateCFA ();
      break;
    default:
      break;
    }
  mc6800_useReg (reg);
  mc6800_dirtyReg (reg, false);
}

/*--------------------------------------------------------------------------*/
/* pullNull - Discard n bytes off the top of the stack                      */
/*--------------------------------------------------------------------------*/
static void
pullNull (int n)
{
  wassert (n >= 0);
  adjustStack (n);
}

/*--------------------------------------------------------------------------*/
/* pushConst - Push a constant byte value onto stack                        */
/*--------------------------------------------------------------------------*/
static void
pushConst (int c)
{
  if (mc6800_reg_b->isFree)
    {
      loadRegFromConst (mc6800_reg_a, c);
      pushReg (mc6800_reg_a, true);
    }
  if (mc6800_reg_a->isFree)
    {
      loadRegFromConst (mc6800_reg_a, c);
      pushReg (mc6800_reg_a, true);
    }
  else
    {
      pushReg (mc6800_reg_a, false);
      loadRegFromConst (mc6800_reg_a, c);
      emitcode ("sta", "2,s");
      regalloc_dry_run_cost += 3;
      pullReg (mc6800_reg_a);
    }
}

/*--------------------------------------------------------------------------*/
/* pushRegIfUsed - Push register reg if marked in use. Returns true if the  */
/*                 push was performed, false otherwise.                     */
/*--------------------------------------------------------------------------*/
static bool
pushRegIfUsed (reg_info *reg)
{
  if (!reg->isFree)
    {
      pushReg (reg, true);
      return true;
    }
  else
    return false;
}

/*--------------------------------------------------------------------------*/
/* pushRegIfSurv - Push register reg if marked surviving. Returns true if   */
/*                 the push was performed, false otherwise.                 */
/*--------------------------------------------------------------------------*/
static bool
pushRegIfSurv (reg_info *reg)
{
  if (!reg->isDead)
    {
      pushReg (reg, true);
      return true;
    }
  else
    return false;
}

/*--------------------------------------------------------------------------*/
/* pullOrFreeReg - If needpull is true, register reg is pulled from the     */
/*                 stack. Otherwise register reg is marked as free.         */
/*--------------------------------------------------------------------------*/
static void
pullOrFreeReg (reg_info * reg, bool needpull)
{
  if (needpull)
    pullReg (reg);
  else
    mc6800_freeReg (reg);
}

/*--------------------------------------------------------------------------*/
/* adjustStack - Adjust the stack pointer by n bytes.                       */
/*--------------------------------------------------------------------------*/
static void
adjustStack (int n)
{
  if (n>0)
    {
      for (int i=0; i<n; i++ ){
        mc6800_emitOp ("ins", MODE_INH, "");
      }
      _G.stackPushes -= n;
      updateCFA ();
    }
  else if (n<0)
    {
      for (int i=0; i>n; i-- ){
        mc6800_emitOp ("des", MODE_INH, "");
      }
      _G.stackPushes -= n;
      updateCFA ();
    }
}

#if DD(1) -1 == 0
/*--------------------------------------------------------------------------*/
/* aopName - Return a string with debugging information about an asmop.     */
/*--------------------------------------------------------------------------*/
static char *
aopName (asmop * aop)
{
  static char buffer[256];
  char *buf = buffer;

  if (!aop)
    return "(asmop*)NULL";

  switch (aop->type)
    {
    case AOP_IMMD:
      sprintf (buf, "IMMD(%s)", aop->aopu.aop_immd);
      return buf;
    case AOP_LIT:
      sprintf (buf, "LIT(%s)", aopLiteral (aop->aopu.aop_lit, 0));
      return buf;
    case AOP_DIR:
      sprintf (buf, "DIR(%s)", aop->aopu.aop_dir);
      return buf;
    case AOP_EXT:
      sprintf (buf, "EXT(%s)", aop->aopu.aop_dir);
      return buf;
    case AOP_SOF:
      sprintf (buf, "SOF(%s)", OP_SYMBOL (aop->op)->name);
      return buf;
    case AOP_REG:
      sprintf (buf, "REG(%s,%s,%s,%s)",
               aop->aopu.aop_reg[3] ? aop->aopu.aop_reg[3]->name : "-",
               aop->aopu.aop_reg[2] ? aop->aopu.aop_reg[2]->name : "-",
               aop->aopu.aop_reg[1] ? aop->aopu.aop_reg[1]->name : "-",
               aop->aopu.aop_reg[0] ? aop->aopu.aop_reg[0]->name : "-");
      return buf;
    default:
      sprintf (buf, "?%d", aop->type);
      return buf;
    }

  return "?";
}
#endif

/*--------------------------------------------------------------------------*/
/* loadRegFromAop - Load register reg from logical offset loffset of aop.   */
/*                  For multi-byte registers, loffset is of the lsb reg.    */
/*--------------------------------------------------------------------------*/
static void
loadRegFromAop (reg_info * reg, asmop * aop, int loffset)
{
  int regidx = reg->rIdx;
  reg_info *holder = mc6800_findRegAop (aop, loffset);

  if (aop->type == AOP_STL && regidx == D_IDX)
    {
      const char *tmp = allocTemp ();
      int delta = 1 + _G.stackOfs + aop->aopu.aop_stk + _G.stackPushes;

      mc6800_emitOp ("sts", MODE_DIR, "*%s", tmp);
      mc6800_emitOp ("ldab", MODE_DIR, "*%s+1", tmp);
      mc6800_emitOp ("ldaa", MODE_DIR, "*%s", tmp);
      mc6800_emitOp ("addb", MODE_IMM, "#%d", delta & 0xff);
      mc6800_emitOp ("adca", MODE_IMM, "#%d", (delta >> 8) & 0xff);
      freeTemp ();
      mc6800_dirtyReg (mc6800_reg_d, false);
      return;
    }
  if (aop->type == AOP_STL
      && (regidx == A_IDX || regidx == B_IDX))
    {
      const char *tmp;
      int delta;

      tmp = allocTemp ();
      delta = 1 + _G.stackOfs + aop->aopu.aop_stk + _G.stackPushes;
      mc6800_emitOp ("sts", MODE_DIR, "*%s", tmp);
      mc6800_emitOpWithAcc ("lda", reg, MODE_DIR, "*%s+1", tmp);
      mc6800_emitOpWithAcc ("add", reg, MODE_IMM, "#%d", delta & 0xff);
      if (loffset)
        {
          mc6800_emitOpWithAcc ("lda", reg, MODE_DIR, "*%s", tmp);
          mc6800_emitOpWithAcc ("adc", reg, MODE_IMM, "#%d", (delta >> 8) & 0xff);
        }
      freeTemp ();
      mc6800_dirtyReg (reg, false);
      return;
    }
  if (aop->type == AOP_STL)
    {
      setupXFromSP (_G.stackOfs + aop->aopu.aop_stk);
      if (regidx != X_IDX)
        transferRegReg (mc6800_reg_x, reg, false);
      return;
    }

  setupXForAop (aop);

  if (aop->stacked && aop->stk_aop[loffset])
    {
      loadRegFromAop (reg, aop->stk_aop[loffset], 0);
      return;
    }

  DD (emitcode ("", ";     loadRegFromAop (%s, %s, %d)", reg->name, aopName (aop), loffset));

  switch (regidx)
    {
    case A_IDX:
      if (holder == reg)
        break;
      if (aop->type == AOP_REG && IS_AOP_X (aop) && loffset < aop->size)
        {
          const char *tmp = allocTemp ();

          mc6800_emitOp ("stx", MODE_DIR, "*%s", tmp);
          mc6800_emitOp ("ldaa", MODE_DIR, loffset ? "*%s" : "*%s+1", tmp);
          freeTemp ();
          mc6800_dirtyReg (reg, false);
          break;
        }
      if (aop->type == AOP_REG)
        {
          if (loffset < aop->size)
            transferRegReg (aop->aopu.aop_reg[loffset], reg, false);
          else
            loadRegFromConst (reg, 0);
        }
      else if (aop->type == AOP_LIT)
        loadRegFromConst (reg, byteOfVal (aop->aopu.aop_lit, loffset));
      else if (holder)
        transferRegReg (holder, reg, false);
      else
        {
          mc6800_emitOp_o ("ldaa", aop, loffset);
          mc6800_dirtyReg (reg, false);
          if ((aop->type == AOP_DIR || aop->type == AOP_EXT || aop->type == AOP_SOF)
              && aop->op && !isOperandVolatile (aop->op, false))
            {
              reg->aop = aop;
              reg->aopofs = loffset;
            }
        }
      break;
    case B_IDX:
      if (holder == reg)
        break;
      if (aop->type == AOP_REG && IS_AOP_X (aop) && loffset < aop->size)
        {
          const char *tmp = allocTemp ();

          mc6800_emitOp ("stx", MODE_DIR, "*%s", tmp);
          mc6800_emitOp ("ldab", MODE_DIR, loffset ? "*%s" : "*%s+1", tmp);
          freeTemp ();
          mc6800_dirtyReg (reg, false);
          break;
        }
      if (aop->type == AOP_REG)
        {
          if (loffset < aop->size)
            transferRegReg (aop->aopu.aop_reg[loffset], reg, false);
          else
            loadRegFromConst (reg, 0);
        }
      else if (aop->type == AOP_LIT)
        loadRegFromConst (reg, byteOfVal (aop->aopu.aop_lit, loffset));
      else if (holder)
        transferRegReg (holder, reg, false);
      else
        {
          mc6800_emitOp_o ("ldab", aop, loffset);
          mc6800_dirtyReg (reg, false);
          if ((aop->type == AOP_DIR || aop->type == AOP_EXT || aop->type == AOP_SOF)
              && aop->op && !isOperandVolatile (aop->op, false))
            {
              reg->aop = aop;
              reg->aopofs = loffset;
            }
        }
      break;
    case X_IDX:
      if (IS_AOP_X (aop))
        break;
      if (aop->type == AOP_LIT)
        {
          loadRegFromConst (reg, byteOfVal (aop->aopu.aop_lit, loffset + 1) << 8
                                 | byteOfVal (aop->aopu.aop_lit, loffset));
          break;
        }
      if (IS_AOP_D (aop))
        {
          transferRegReg (mc6800_reg_d, reg, false);
          break;
        }
      wassertl (aop->type != AOP_REG, "cannot load x from a register");
      mc6800_emitOpw_o ("ldx", aop, loffset);
      mc6800_dirtyReg (reg, false);
      if ((aop->type == AOP_DIR || aop->type == AOP_EXT) && aop->op && !isOperandVolatile (aop->op, false))
        {
          reg->aop = aop;
          reg->aopofs = loffset;
        }
      break;
    case D_IDX:
      if (IS_AOP_D (aop))
        break;
      if (IS_AOP_X (aop))
        {
          transferRegReg (mc6800_reg_x, reg, false);
          break;
        }
      loadRegFromAop (mc6800_reg_b, aop, loffset);
      loadRegFromAop (mc6800_reg_a, aop, loffset + 1);
      break;
    }
  mc6800_useReg (reg);
}


/*--------------------------------------------------------------------------*/
/* storeRegToAop - Store register reg to logical offset loffset of aop.     */
/*                 For multi-byte registers, loffset is of the lsb reg.     */
/*--------------------------------------------------------------------------*/
static void
storeRegToAop (reg_info *reg, asmop * aop, int loffset)
{
  int regidx = reg->rIdx;

  if (regidx != X_IDX)
    setupXForAop (aop);

  D (emitcode (";     storeRegToAop", ""));
  DD (emitcode ("", ";     storeRegToAop (%s, %s, %d), stacked=%d",
                reg->name, aopName (aop), loffset, aop->stacked));

  if ((reg->rIdx == D_IDX) && aop->stacked && (aop->stk_aop[loffset] || aop->stk_aop[loffset + 1]))
    {
      storeRegToAop (mc6800_reg_b, aop, loffset);
      storeRegToAop (mc6800_reg_a, aop, loffset + 1);
      return;
    }

  if (aop->stacked && aop->stk_aop[loffset])
    {
      storeRegToAop (reg, aop->stk_aop[loffset], 0);
      return;
    }

  if (aop->type == AOP_DUMMY)
    return;

  if (aop->type == AOP_CRY)     /* This can only happen if IFX was optimized */
    return;                     /* away, so just toss the result */

  switch (regidx)
    {
    case A_IDX:
      if ((aop->type == AOP_REG) && IS_AOP_X (aop) && (loffset < aop->size))
        {
          const char *tmp = allocTemp ();

          mc6800_emitOp ("stx", MODE_DIR, "*%s", tmp);
          mc6800_emitOp ("staa", MODE_DIR, loffset ? "*%s" : "*%s+1", tmp);
          mc6800_emitOp ("ldx", MODE_DIR, "*%s", tmp);
          freeTemp ();
          mc6800_dirtyReg (mc6800_reg_x, false);
          break;
        }
      if ((aop->type == AOP_REG) && (loffset < aop->size))
        transferRegReg (reg, aop->aopu.aop_reg[loffset], false);
      else
        {
          mc6800_emitOp_o ("staa", aop, loffset);
        }
      break;
    case B_IDX:
      if ((aop->type == AOP_REG) && IS_AOP_X (aop) && (loffset < aop->size))
        {
          const char *tmp = allocTemp ();

          mc6800_emitOp ("stx", MODE_DIR, "*%s", tmp);
          mc6800_emitOp ("stab", MODE_DIR, loffset ? "*%s" : "*%s+1", tmp);
          mc6800_emitOp ("ldx", MODE_DIR, "*%s", tmp);
          freeTemp ();
          mc6800_dirtyReg (mc6800_reg_x, false);
          break;
        }
      if ((aop->type == AOP_REG) && (loffset < aop->size))
        transferRegReg (reg, aop->aopu.aop_reg[loffset], false);
      else
        {
          mc6800_emitOp_o ("stab", aop, loffset);
        }
      break;
    case X_IDX:
      D (emitcode (";     storeRegToAop", "case X_IDX %s %s %d",__func__,__FILE__,__LINE__));
      D (emitcode (";     storeRegToAop", "aop->type==AOP_REG? %d",aop->type == AOP_REG));
      D (emitcode (";     storeRegToAop", "loffset %d, app->size %d",loffset,aop->size));
      if ((aop->type == AOP_REG) && IS_AOP_X (aop))
        break;
      if ((aop->type == AOP_REG) && IS_AOP_D (aop) && loffset == 0)
        {
          transferRegReg (reg, mc6800_reg_d, false);
          break;
        }
      if ((aop->type == AOP_REG) && (loffset < aop->size)
          && (aop->aopu.aop_reg[loffset] == mc6800_reg_a || aop->aopu.aop_reg[loffset] == mc6800_reg_b))
        {
          const char *tmp = allocTemp ();

          mc6800_emitOp ("stx", MODE_DIR, "*%s", tmp);
          mc6800_emitOpWithAcc ("lda", aop->aopu.aop_reg[loffset], MODE_DIR, "*%s+1", tmp);
          freeTemp ();
          mc6800_dirtyReg (aop->aopu.aop_reg[loffset], false);
          break;
        }
      if ((aop->type == AOP_REG) && (loffset < aop->size))
        transferRegReg (reg, aop->aopu.aop_reg[loffset], false);
      else
        {
          if (aop->type == AOP_SOF) {
            const char *tmp = allocTemp ();
            bool needpulla;
            bool xfree = mc6800_reg_x->isFree;

            mc6800_emitOp ("stx", MODE_DIR, "*%s", tmp);
            mc6800_freeReg (mc6800_reg_x);
            needpulla = pushRegIfUsed (mc6800_reg_a);
            mc6800_emitOp ("ldaa", MODE_DIR, "*%s+1", tmp);
            storeRegToAop (mc6800_reg_a, aop, loffset);
            if (loffset + 1 < aop->size)
              {
                mc6800_emitOp ("ldaa", MODE_DIR, "*%s", tmp);
                storeRegToAop (mc6800_reg_a, aop, loffset + 1);
              }
            pullOrFreeReg (mc6800_reg_a, needpulla);
            mc6800_emitOp ("ldx", MODE_DIR, "*%s", tmp);
            mc6800_dirtyReg (mc6800_reg_x, false);
            mc6800_reg_x->isFree = xfree;
            freeTemp ();
            break;
          }
          mc6800_emitOpw_o ("stx", aop, loffset);
        }
      break;
    case D_IDX:
      if ((aop->type == AOP_REG) && IS_AOP_D (aop))
        break;
      if (aop->type == AOP_REG && IS_AOP_X (aop) && loffset == 0) {
        transferRegReg (reg, mc6800_reg_x, false);
        break;
      }
      storeRegToAop (mc6800_reg_b, aop, loffset);
      storeRegToAop (mc6800_reg_a, aop, loffset + 1);
      break;
    default:
      wassert (0);
    }

  if ((regidx == A_IDX || regidx == B_IDX)
      && (aop->type == AOP_DIR || aop->type == AOP_EXT || aop->type == AOP_SOF)
      && aop->op && !isOperandVolatile (aop->op, false))
    {
      reg->aop = aop;
      reg->aopofs = loffset;
    }
  if (regidx == X_IDX && (aop->type == AOP_DIR || aop->type == AOP_EXT)
      && aop->op && !isOperandVolatile (aop->op, false))
    {
      reg->aop = aop;
      reg->aopofs = loffset;
    }
}

/*--------------------------------------------------------------------------*/
/* loadRegFromConst - Load register reg from constant c.                    */
/*--------------------------------------------------------------------------*/
static void
loadRegFromConst (reg_info * reg, int c)
{
  D (emitcode (";     loadRegFromConst", ""));

  switch (reg->rIdx)
    {
    case A_IDX:
      c &= 0xff;
      if (reg->isLitConst)
        {
          if (reg->litConst == c)
            break;
          if (((reg->litConst + 1) & 0xff) == c)
            {
              mc6800_emitOp ("inca", MODE_INH, "");
              break;
            }
          if (((reg->litConst - 1) & 0xff) == c)
            {
              mc6800_emitOp ("deca", MODE_INH, "");
              break;
            }
        }

      if (mc6800_reg_b->isLitConst && mc6800_reg_b->litConst == c)
        transferRegReg (mc6800_reg_b, reg, false);
      else if (!c)
        {
          mc6800_emitOp ("clra", MODE_INH, "");
        }
      else
        {
          mc6800_emitOp ("ldaa", MODE_IMM, "#0x%02x", c);
        }
      break;
    case B_IDX:
      c &= 0xff;
      if (reg->isLitConst)
        {
          if (reg->litConst == c)
            break;
          if (((reg->litConst + 1) & 0xff) == c)
            {
              mc6800_emitOp ("incb", MODE_INH, "");
              break;
            }
          if (((reg->litConst - 1) & 0xff) == c)
            {
              mc6800_emitOp ("decb", MODE_INH, "");
              break;
            }
        }

      if (mc6800_reg_a->isLitConst && mc6800_reg_a->litConst == c)
        transferRegReg (mc6800_reg_a, reg, false);
      else if (!c)
        {
          mc6800_emitOp ("clrb", MODE_INH, "");
        }
      else
        {
          mc6800_emitOp ("ldab", MODE_IMM, "#0x%02x", c);
        }
      break;
    case X_IDX:
      c &= 0xffff;
      if (reg->isLitConst)
        {
          if (reg->litConst == c)
            break;
          if (((reg->litConst + 1) & 0xffff) == c)
            {
              mc6800_emitOp ("inx", MODE_INH, "");
              break;
            }
          if (((reg->litConst - 1) & 0xffff) == c)
            {
              mc6800_emitOp ("dex", MODE_INH, "");
              break;
            }
        }
      mc6800_emitOp ("ldx", MODE_IMM, "#0x%04x", c);
      break;
    case D_IDX:
      c &= 0xffff;
      if (reg->isLitConst && reg->litConst == c)
	break;
      loadRegFromConst (mc6800_reg_a, c >> 8);
      loadRegFromConst (mc6800_reg_b, c);
      break;
    default:
      werror (E_INTERNAL_ERROR, __FILE__, __LINE__, "Bad rIdx in loadRegFromConst");
      return;
    }

  mc6800_dirtyReg (reg, false);
  reg->isLitConst = 1;
  reg->litConst = c;
  mc6800_useReg (reg);
}

/*--------------------------------------------------------------------------*/
/* loadRegFromImm - Load register reg from immediate value c.               */
/*--------------------------------------------------------------------------*/
static void
loadRegFromImm (reg_info * reg, char * c)
{
  if (*c == '#')
    c++;
  switch (reg->rIdx)
    {
    case A_IDX:
      mc6800_emitOp ("ldaa", MODE_IMM, "#%s", c);
      break;
    case B_IDX:
      mc6800_emitOp ("ldab", MODE_IMM, "#%s", c);
      break;
    case X_IDX:
      mc6800_emitOp ("ldx", MODE_IMM, "#%s", c);
      break;
    case D_IDX:
      mc6800_emitOp ("ldab", MODE_IMM, "#%s", c);
      mc6800_emitOp ("ldaa", MODE_IMM, "#%s >> 8", c);
      break;
    default:
      werror (E_INTERNAL_ERROR, __FILE__, __LINE__, "Bad rIdx in loadRegFromConst");
      return;
    }
  mc6800_dirtyReg (reg, false);
  mc6800_useReg (reg);
}

/*--------------------------------------------------------------------------*/
/* storeConstToAop- Store constant c to logical offset loffset of asmop aop.*/
/*--------------------------------------------------------------------------*/
static void
storeConstToAop (int c, asmop * aop, int loffset)
{
  setupXForAop (aop);

  if (aop->stacked && aop->stk_aop[loffset])
    {
      storeConstToAop (c, aop->stk_aop[loffset], 0);
      return;
    }

  /* If the value needed is already in A or B, just store it */
  if (mc6800_reg_a->isLitConst && mc6800_reg_a->litConst == c)
    {
      storeRegToAop (mc6800_reg_a, aop, loffset);
      return;
    }
  if (mc6800_reg_b->isLitConst && mc6800_reg_b->litConst == c)
    {
      storeRegToAop (mc6800_reg_b, aop, loffset);
      return;
    }

  switch (aop->type)
    {
    case AOP_REG:
      if (loffset > (aop->size - 1))
        break;
      if (IS_AOP_X (aop))
        {
          bool pula = pushRegIfUsed (mc6800_reg_a);

          loadRegFromConst (mc6800_reg_a, c);
          storeRegToAop (mc6800_reg_a, aop, loffset);
          pullOrFreeReg (mc6800_reg_a, pula);
          break;
        }
      loadRegFromConst (aop->aopu.aop_reg[loffset], c);
      break;
    case AOP_DUMMY:
      break;
    case AOP_EXT:
    case AOP_DIR:
    case AOP_SOF:
      /* clr operates with read-modify-write cycles, so don't use if the */
      /* destination is volatile to avoid the read side-effect. */
      if (!c && !(aop->op && isOperandVolatile (aop->op, false))
          && (aop->type != AOP_SOF || !mc6800_reg_a->isFree && !mc6800_reg_b->isFree))
        {
          const char *adr = aopAdrStr (aop, loffset, false);

          if (adr[0] == '*')
            adr++;
          /* clr dst : 3 bytes, 6 cycles */
          mc6800_emitOp ("clr", aop->type == AOP_SOF ? MODE_IDX : MODE_EXT, adr[0] == '*' ? adr + 1 : adr);
          mc6800_dirtyRegAop (aop, loffset);
          break;
        }
      /* fall through */
    default:
      if (mc6800_reg_a->isFree)
        {
          loadRegFromConst (mc6800_reg_a, c);
          storeRegToAop (mc6800_reg_a, aop, loffset);
          mc6800_freeReg (mc6800_reg_a);
        }
      else if (mc6800_reg_b->isFree)
        {
          loadRegFromConst (mc6800_reg_b, c);
          storeRegToAop (mc6800_reg_b, aop, loffset);
          mc6800_freeReg (mc6800_reg_b);
        }
      else
        {
          pushReg (mc6800_reg_a, true);
          loadRegFromConst (mc6800_reg_a, c);
          storeRegToAop (mc6800_reg_a, aop, loffset);
          pullReg (mc6800_reg_a);
        }
    }
}

/*--------------------------------------------------------------------------*/
/* storeImmToAop- Store immediate value c to logical offset loffset of asmop aop.*/
/*--------------------------------------------------------------------------*/
static void
storeImmToAop (char *c, asmop * aop, int loffset)
{
  setupXForAop (aop);

  if (aop->stacked && aop->stk_aop[loffset])
    {
      storeImmToAop (c, aop->stk_aop[loffset], 0);
      return;
    }

  switch (aop->type)
    {
    case AOP_REG:
      if (loffset > (aop->size - 1))
        break;
      loadRegFromImm (aop->aopu.aop_reg[loffset], c);
      break;
    case AOP_DUMMY:
      break;
    case AOP_EXT:
    case AOP_DIR:
    case AOP_SOF:
      /* clr operates with read-modify-write cycles, so don't use if the */
      /* destination is volatile to avoid the read side-effect. */
      if (!strcmp (c, zero) && !(aop->op && isOperandVolatile (aop->op, false))
          && (aop->type != AOP_SOF || !mc6800_reg_a->isFree && !mc6800_reg_b->isFree))
        {
          const char *adr = aopAdrStr (aop, loffset, false);

          if (adr[0] == '*')
            adr++;
          /* clr dst : 3 bytes, 6 cycles */
          mc6800_emitOp ("clr", aop->type == AOP_SOF ? MODE_IDX : MODE_EXT, adr[0] == '*' ? adr + 1 : adr);
          mc6800_dirtyRegAop (aop, loffset);
          break;
        }
      /* fall through */
    default:
      if (mc6800_reg_a->isFree)
        {
          loadRegFromImm (mc6800_reg_a, c);
          storeRegToAop (mc6800_reg_a, aop, loffset);
          mc6800_freeReg (mc6800_reg_a);
        }
      else if (mc6800_reg_b->isFree)
        {
          loadRegFromImm (mc6800_reg_b, c);
          storeRegToAop (mc6800_reg_b, aop, loffset);
          mc6800_freeReg (mc6800_reg_b);
        }
      else
        {
          pushReg (mc6800_reg_a, true);
          loadRegFromImm (mc6800_reg_a, c);
          storeRegToAop (mc6800_reg_a, aop, loffset);
          pullReg (mc6800_reg_a);
        }
    }
}


/*--------------------------------------------------------------------------*/
/* storeRegSignToUpperAop - If isSigned is true, the sign bit of register   */
/*                          reg is extended to fill logical offsets loffset */
/*                          and above of asmop aop. Otherwise, logical      */
/*                          offsets loffset and above of asmop aop are      */
/*                          zeroed. reg must be an 8-bit register.          */
/*--------------------------------------------------------------------------*/
static void
storeRegSignToUpperAop (reg_info * reg, asmop * aop, int loffset, bool isSigned)
{
//  int regidx = reg->rIdx;
  int size = aop->size;

  if (size <= loffset)
    return;

  if (!isSigned)
    {
      /* Unsigned case */
      while (loffset < size)
        storeConstToAop (0, aop, loffset++);
    }
  else
    {
      /* Signed case */
      transferRegReg (reg, mc6800_reg_a, false);
      mc6800_emitOp ("rola", MODE_INH, "");
      mc6800_emitOp ("ldaa", MODE_IMM, "#0");
      mc6800_emitOp ("sbca", MODE_IMM, "#0");
      mc6800_useReg (mc6800_reg_a);
      while (loffset < size)
        storeRegToAop (mc6800_reg_a, aop, loffset++);
      mc6800_freeReg (mc6800_reg_a);
    }
}

/*--------------------------------------------------------------------------*/
/* storeRegToFullAop - Store register reg to asmop aop with appropriate     */
/*                     padding and/or truncation as needed. If isSigned is  */
/*                     true, sign extension will take place in the padding. */
/*--------------------------------------------------------------------------*/
static void
storeRegToFullAop (reg_info *reg, asmop *aop, bool isSigned)
{
  int regidx = reg->rIdx;
  int size = aop->size;

  switch (regidx)
    {
    case A_IDX:
    case X_IDX:
    case B_IDX:
      storeRegToAop (reg, aop, 0);
      if (size > 1 && isSigned && aop->type == AOP_REG && aop->aopu.aop_reg[0]->rIdx == A_IDX)
        pushReg (mc6800_reg_a, true);
      storeRegSignToUpperAop (reg, aop, 1, isSigned);
      if (size > 1 && isSigned && aop->type == AOP_REG && aop->aopu.aop_reg[0]->rIdx == A_IDX)
        pullReg (mc6800_reg_a);
      break;
    case D_IDX:
      if (size == 1)
        {
          storeRegToAop (mc6800_reg_a, aop, 0);
        }
      else
        {
          storeRegToAop (reg, aop, 0);
          storeRegSignToUpperAop (mc6800_reg_x, aop, 2, isSigned);
        }
      break;
    default:
      wassert (0);
    }
}

/*--------------------------------------------------------------------------*/
/* transferAopAop - Transfer the value at logical offset srcofs of asmop    */
/*                  srcaop to logical offset dstofs of asmop dstaop.        */
/*--------------------------------------------------------------------------*/
static void
transferAopAop (asmop *srcaop, int srcofs, asmop *dstaop, int dstofs)
{
  bool needpula = false;
  reg_info *reg = NULL;
  bool keepreg = false;
  bool afree;

  wassert (srcaop && dstaop);

  /* ignore transfers at the same byte, unless its volatile */
  if (srcaop->op && !isOperandVolatile (srcaop->op, false)
      && dstaop->op && !isOperandVolatile (dstaop->op, false)
      && operandsEqu (srcaop->op, dstaop->op) && srcofs == dstofs && dstaop->type == srcaop->type)
    return;

  if (srcaop->stacked && srcaop->stk_aop[srcofs])
    {
      transferAopAop (srcaop->stk_aop[srcofs], 0, dstaop, dstofs);
      return;
    }

  if (dstaop->stacked && dstaop->stk_aop[srcofs])
    {
      transferAopAop (srcaop, srcofs, dstaop->stk_aop[dstofs], 0);
      return;
    }

//  DD(emitcode ("", "; transferAopAop (%s, %d, %s, %d)",
//            aopName (srcaop), srcofs, aopName (dstaop), dstofs));
//  DD(emitcode ("", "; srcaop->type = %d", srcaop->type));
//  DD(emitcode ("", "; dstaop->type = %d", dstaop->type));

  if (dstofs >= dstaop->size)
    return;

  if (srcaop->type == AOP_LIT)
    {
      storeConstToAop (byteOfVal (srcaop->aopu.aop_lit, srcofs), dstaop, dstofs);
      return;
    }

  if (dstaop->type == AOP_REG && !IS_AOP_X (dstaop))
    {
      reg = dstaop->aopu.aop_reg[dstofs];
      keepreg = true;
    }
  else if ((srcaop->type == AOP_REG) && !IS_AOP_X (srcaop) && (srcaop->aopu.aop_reg[srcofs]))
    {
      reg = srcaop->aopu.aop_reg[srcofs];
      keepreg = true;
    }

  if (!reg)
    {
      reg_info *held = mc6800_findRegAop (srcaop, srcofs);

      if (held)
        {
          reg = held;
          keepreg = true;
        }
    }

  afree = mc6800_reg_a->isFree;

  if (!reg)
    {
      if (mc6800_reg_a->isFree)
        reg = mc6800_reg_a;
      else if (mc6800_reg_b->isFree)
        reg = mc6800_reg_b;
      else
        {
          pushReg (mc6800_reg_a, true);
          needpula = true;
          reg = mc6800_reg_a;
        }
    }

  loadRegFromAop (reg, srcaop, srcofs);
  storeRegToAop (reg, dstaop, dstofs);

  if (!keepreg)
    pullOrFreeReg (mc6800_reg_a, needpula);

  mc6800_reg_a->isFree = afree;
}


/*--------------------------------------------------------------------------*/
/* accopWithAop - Emit accumulator modifying instruction accop with the     */
/*                byte at logical offset loffset of asmop aop.              */
/*                Supports: adc, add, and, bit, cmp, eor, ora, sbc, sub     */
/*--------------------------------------------------------------------------*/
static void
accopWithAop (const char *op, reg_info *acc, asmop *aop, int loffset)
{
  char accop[sizeof (((mc6800opcodedata *) 0)->name)];

  wassertl (acc == mc6800_reg_a || acc == mc6800_reg_b, "accopWithAop: register must be A or B");
  SNPRINTF (accop, sizeof (accop), "%s%c", op, acc == mc6800_reg_a ? 'a' : 'b');
  setupXForAop (aop);

  if (aop->stacked && aop->stk_aop[loffset])
    {
      accopWithAop (op, acc, aop->stk_aop[loffset], 0);
      return;
    }

  if (aop->type == AOP_DUMMY)
    return;

  if (aop->type == AOP_REG)
    {
  if (loffset < aop->size)
    {
      reg_info *reg = aop->aopu.aop_reg[loffset];

      if ((reg->rIdx == XL_IDX || reg->rIdx == XH_IDX) && _G.tempOfs + 1 <= NUM_TEMP_REGS)
        {
          const char *tmp = allocTemp ();
          mc6800_emitOp ("stx", MODE_DIR, "*%s", tmp);
          mc6800_emitOp (accop, MODE_DIR, (reg->rIdx == XL_IDX) ? "*%s+1" : "*%s", tmp);
          freeTemp ();
        }
      else if (reg->rIdx == B_IDX && aop->size == 1
               && (!strcmp (accop, "adda") || !strcmp (accop, "suba") || !strcmp (accop, "cmpa")))
        {
          mc6800_emitOp (!strcmp (accop, "adda") ? "aba" : !strcmp (accop, "suba") ? "sba" : "cba", MODE_INH, "");
        }
      else if ((reg->rIdx == A_IDX || reg->rIdx == B_IDX) && _G.tempOfs < NUM_TEMP_REGS)
        {
          const char *tmp = allocTemp ();
          mc6800_emitOpWithAcc ("sta", reg, MODE_DIR, "*%s", tmp);
          mc6800_emitOp (accop, MODE_DIR, "*%s", tmp);
          freeTemp ();
        }
      else
        {
          wassertl (reg->rIdx == A_IDX || reg->rIdx == B_IDX, "no temporary for register operand");
          wassertl (mc6800_reg_x->isFree && mc6800_reg_x->isDead, "X is not free for register operand");
          pushReg (reg, false);
          mc6800_emitOp ("tsx", MODE_INH, "");
          mc6800_dirtyReg (mc6800_reg_x, false);
          mc6800_emitOp (accop, MODE_IDX, "0,x");
          pullNull (1);
        }
    }
    else
      {
        mc6800_emitOp (accop, MODE_IMM, "#0");
      }
    }
  else
    {
      mc6800_emitOp_o (accop, aop, loffset);
    }
}


/*--------------------------------------------------------------------------*/
/* rmwWithReg - Emit read/modify/write instruction rmwop with register reg. */
/*              Register reg must be 8-bit.                                 */
/*              Supports: asl, asr, com, dec, inc, lsr, neg, rol, ror       */
/*--------------------------------------------------------------------------*/
static void
rmwWithReg (char *rmwop, reg_info * reg)
{
  char rmwbuf[10];
  char *rmwaop = rmwbuf;

  if (reg->rIdx == A_IDX)
    {
      sprintf (rmwaop, "%sa", rmwop);
      mc6800_emitOp (rmwaop, MODE_INH, "");
      mc6800_dirtyReg (mc6800_reg_a, false);
    }
  else if (reg->rIdx == B_IDX)
    {
      sprintf (rmwaop, "%sb", rmwop);
      mc6800_emitOp (rmwaop, MODE_INH, "");
      mc6800_dirtyReg (mc6800_reg_b, false);
    }
  else
    wassertl (0, "rmwWithReg: register must be A or B");
}

/*--------------------------------------------------------------------------*/
/* rmwWithAop - Emit read/modify/write instruction rmwop with the byte at   */
/*                logical offset loffset of asmop aop.                      */
/*                Supports: asl, asr, com, dec, inc, lsr, neg, rol, ror,    */
/*                tst                                                       */
/*--------------------------------------------------------------------------*/
static void
rmwWithAop (char *rmwop, asmop * aop, int loffset)
{
  bool needpull = false;
  reg_info * reg;

  setupXForAop (aop);

  if (aop->stacked && aop->stk_aop[loffset])
    {
      rmwWithAop (rmwop, aop->stk_aop[loffset], 0);
      return;
    }

  /* If we need a register: */
  /*   use A if it's free,  */
  /*   otherwise use B if it's free */
  /*   otherwise use A (and preserve original value via the stack) */
  if (!mc6800_reg_a->isFree && mc6800_reg_b->isFree)
    reg = mc6800_reg_b;
  else
    reg = mc6800_reg_a;

  switch (aop->type)
    {
    case AOP_REG:
      rmwWithReg (rmwop, aop->aopu.aop_reg[loffset]);
      break;
    case AOP_DUMMY:
      break;
    case AOP_SOF:
      {
        int offset = aop->size - 1 - loffset;
        offset += _G.stackOfs + _G.stackPushes + aop->aopu.aop_stk + 1;
        if ((offset > 0xff) || (offset < 0))
          {
            /* Indexed addressing only supports an offset of 0 to 255. */
            needpull = pushRegIfUsed (reg);
            loadRegFromAop (reg, aop, loffset);
            rmwWithReg (rmwop, reg);
            if (strcmp ("tst", rmwop))
              storeRegToAop (reg, aop, loffset);
            pullOrFreeReg (reg, needpull);
            break;
          }
        /* If the offset is small enough, fall through to default case */
      }
    default:
      mc6800_emitOp (rmwop, aop->type == AOP_SOF ? MODE_IDX : MODE_EXT, aopAdrStr (aop, loffset, false) + (aop->type == AOP_DIR ? 1 : 0));
      mc6800_dirtyRegAop (aop, loffset);
    }

}

/*--------------------------------------------------------------------------*/
/* addConstToX - Add a constant to the index register.                     */
/*--------------------------------------------------------------------------*/
static void
addConstToX (int n)
{
  const char *tmp;
  reg_info *acc;
  bool needpullacc;

  if (n == 0)
    return;

  if (abs (n) <= ((optimize.codeSize && !optimize.codeSpeed) ? 15 : 6))
    {
      while (n > 0)
        {
          mc6800_emitOp ("inx", MODE_INH, "");
          n--;
        }
      while (n < 0)
        {
          mc6800_emitOp ("dex", MODE_INH, "");
          n++;
        }
      mc6800_dirtyReg (mc6800_reg_x, false);
      return;
    }

  tmp = allocTemp ();
  acc = (mc6800_reg_a->isFree || !mc6800_reg_b->isFree) ? mc6800_reg_a : mc6800_reg_b;
  needpullacc = pushRegIfUsed (acc);
  mc6800_emitOp ("stx", MODE_DIR, "*%s", tmp);
  mc6800_emitOpWithAcc ("lda", acc, MODE_DIR, "*%s+1", tmp);
  mc6800_emitOpWithAcc ("add", acc, MODE_IMM, "#0x%02x", (unsigned int) n & 0xff);
  mc6800_emitOpWithAcc ("sta", acc, MODE_DIR, "*%s+1", tmp);
  mc6800_emitOpWithAcc ("lda", acc, MODE_DIR, "*%s", tmp);
  mc6800_emitOpWithAcc ("adc", acc, MODE_IMM, "#0x%02x", ((unsigned int) n >> 8) & 0xff);
  mc6800_emitOpWithAcc ("sta", acc, MODE_DIR, "*%s", tmp);
  mc6800_emitOp ("ldx", MODE_DIR, "*%s", tmp);
  freeTemp ();
  mc6800_dirtyReg (acc, false);
  pullOrFreeReg (acc, needpullacc);
  mc6800_dirtyReg (mc6800_reg_x, false);
}

/*--------------------------------------------------------------------------*/
/* loadRegIndexed - Load a register using indexed addressing mode.          */
/*                  NOTE: offset is physical (not logical)                  */
/*--------------------------------------------------------------------------*/
static void
loadRegIndexed (reg_info * reg, int offset, char * rematOfs)
{
  /* The rematerialized offset may have a "#" prefix; skip over it */
  if (rematOfs && rematOfs[0] == '#')
    rematOfs++;
  if (rematOfs && !rematOfs[0])
    rematOfs = NULL;

  wassertl (!rematOfs, "indexed access with a symbol offset is not supported");
  wassertl (offset >= 0 && offset <= 0xff, "indexed offset outside 0..255 is not supported yet");

  switch (reg->rIdx)
    {
    case A_IDX:
      mc6800_emitOp ("ldaa", MODE_IDX, "%d,x", offset);
      mc6800_dirtyReg (reg, false);
      break;
    case B_IDX:
      mc6800_emitOp ("ldab", MODE_IDX, "%d,x", offset);
      mc6800_dirtyReg (reg, false);
      break;
    case X_IDX:
      mc6800_emitOp ("ldx", MODE_IDX, "%d,x", offset);
      mc6800_dirtyReg (reg, false);
      break;
    case D_IDX:
      loadRegIndexed (mc6800_reg_b, offset + 1, rematOfs);
      loadRegIndexed (mc6800_reg_a, offset, rematOfs);
      break;
    default:
      wassert (0);
    }
}

/*--------------------------------------------------------------------------*/
/* storeRegIndexed - Store a register using indexed addressing mode.        */
/*                   NOTE: offset is physical (not logical)                 */
/*--------------------------------------------------------------------------*/
static void
storeRegIndexed (reg_info * reg, int offset, char * rematOfs)
{
  /* The rematerialized offset may have a "#" prefix; skip over it */
  if (rematOfs && rematOfs[0] == '#')
    rematOfs++;
  if (rematOfs && !rematOfs[0])
    rematOfs = NULL;

  wassertl (!rematOfs, "indexed access with a symbol offset is not supported");
  wassertl (offset >= 0 && offset <= 0xff, "indexed offset outside 0..255 is not supported yet");

  switch (reg->rIdx)
    {
    case A_IDX:
      mc6800_emitOp ("staa", MODE_IDX, "%d,x", offset);
      break;
    case B_IDX:
      mc6800_emitOp ("stab", MODE_IDX, "%d,x", offset);
      break;
    case D_IDX:
      storeRegIndexed (mc6800_reg_b, offset + 1, rematOfs);
      storeRegIndexed (mc6800_reg_a, offset, rematOfs);
      break;
    default:
      wassert (0);
    }
}


/*-----------------------------------------------------------------*/
/* newAsmop - creates a new asmOp                                  */
/*-----------------------------------------------------------------*/
static asmop *
newAsmop (short type)
{
  asmop *aop;

  aop = Safe_calloc (1, sizeof (asmop));
  aop->type = type;
  aop->op = NULL;
  aop->valinfo.anything = true;
  return aop;
}


/*-----------------------------------------------------------------*/
/* operandConflictsWithX - true if operand in x register           */
/*-----------------------------------------------------------------*/
static bool
operandConflictsWithX (operand *op)
{
  symbol *sym;
  int i;

  if (IS_ITEMP (op))
    {
      sym = OP_SYMBOL (op);
      if (!sym->isspilt)
        {
          for(i = 0; i < sym->nRegs; i++)
            if (sym->regs[i] == mc6800_reg_xl || sym->regs[i] == mc6800_reg_xh)
              return true;
        }
    }

  return false;
}


static void
adjustX (int diff)
{
  while (diff > 0)
    {
      mc6800_emitOp ("inx", MODE_INH, "");
      mc6800_reg_x->stackOffset++;
      diff--;
    }
  while (diff < 0)
    {
      mc6800_emitOp ("dex", MODE_INH, "");
      mc6800_reg_x->stackOffset--;
      diff++;
    }
}

static const char *
setupTmpFromSP (int stackOffset)
{
  const char *tmp = allocTemp ();
  bool saveb = !mc6800_reg_b->isFree && !mc6800_reg_a->isFree;
  int delta = (saveb ? 2 : 1) + stackOffset + _G.stackPushes;

  if (mc6800_reg_b->isFree || saveb)
    {
      if (saveb)
        {
          mc6800_emitOp ("pshb", MODE_INH, "");
        }
      mc6800_emitOp ("sts", MODE_DIR, "*%s", tmp);
      mc6800_emitOp ("ldab", MODE_DIR, "*%s+1", tmp);
      mc6800_emitOp ("addb", MODE_IMM, "#%d", delta & 0xff);
      mc6800_emitOp ("stab", MODE_DIR, "*%s+1", tmp);
      mc6800_emitOp ("ldab", MODE_DIR, "*%s", tmp);
      mc6800_emitOp ("adcb", MODE_IMM, "#%d", (delta >> 8) & 0xff);
      mc6800_emitOp ("stab", MODE_DIR, "*%s", tmp);
      if (saveb)
        {
          mc6800_emitOp ("pulb", MODE_INH, "");
        }
      else
        mc6800_dirtyReg (mc6800_reg_b, false);
    }
  else
    {
      mc6800_emitOp ("sts", MODE_DIR, "*%s", tmp);
      mc6800_emitOp ("ldaa", MODE_DIR, "*%s+1", tmp);
      mc6800_emitOp ("adda", MODE_IMM, "#%d", delta & 0xff);
      mc6800_emitOp ("staa", MODE_DIR, "*%s+1", tmp);
      mc6800_emitOp ("ldaa", MODE_DIR, "*%s", tmp);
      mc6800_emitOp ("adca", MODE_IMM, "#%d", (delta >> 8) & 0xff);
      mc6800_emitOp ("staa", MODE_DIR, "*%s", tmp);
      mc6800_dirtyReg (mc6800_reg_a, false);
    }
  return tmp;
}

static void
setupXFromSP (int stackOffset)
{
  const char *tmp = setupTmpFromSP (stackOffset);

  mc6800_emitOp ("ldx", MODE_DIR, "*%s", tmp);
  freeTemp ();
  mc6800_dirtyReg (mc6800_reg_x, false);
  mc6800_reg_x->aop = &tsxaop;
  mc6800_reg_x->stackOffset = stackOffset;
}

static void
setupXForAop (asmop * aop)
{
  int lo, hi, shift, limit;

  if (aop->type == AOP_IDX)
    {
      iCode *dic = hTabItemWithKey (iCodehTab, bitVectFirstBit (OP_DEFS (aop->op)));

      if (IS_AOP_X (AOP (IC_LEFT (dic))) || mc6800_reg_x->aop == aop)
        return;
      if (!mc6800_reg_x->isFree && !mc6800_reg_x->isDead)
        {
          UNIMPLEMENTED;
          return;
        }
      loadRegFromAop (mc6800_reg_x, AOP (IC_LEFT (dic)), 0);
      mc6800_freeReg (mc6800_reg_x);
      mc6800_reg_x->aop = aop;
      return;
    }
  if (aop->type != AOP_SOF)
    return;
  if (mc6800_reg_x->aop != &tsxaop)
    {
      if (!mc6800_reg_x->isFree)
        {
          UNIMPLEMENTED;
          return;
        }
      mc6800_emitOp ("tsx", MODE_INH, "");
      mc6800_dirtyReg (mc6800_reg_x, false);
      mc6800_reg_x->aop = &tsxaop;
      mc6800_reg_x->stackOffset = -_G.stackPushes;
    }
  if (regalloc_dry_run && !mc6800_dry_stack_size)
    return;

  lo = _G.stackOfs - mc6800_reg_x->stackOffset + aop->aopu.aop_stk;
  hi = lo + aop->size - 1;
  shift = 0;
  if (hi > 255)
    shift = hi - 255;
  if (lo < 0)
    shift = lo;
  limit = (mc6800_reg_a->isFree || mc6800_reg_b->isFree) ? 16 : 18;
  if (shift >= -limit && shift <= limit)
    adjustX (shift);
  else
    setupXFromSP (mc6800_reg_x->stackOffset + shift);
}

/*-----------------------------------------------------------------*/
/* aopForSym - for a true symbol                                   */
/*-----------------------------------------------------------------*/
static asmop *
aopForSym (iCode * ic, symbol * sym, bool result)
{
  asmop *aop;
  memmap *space;

  wassertl (ic != NULL, "Got a null iCode");
  wassertl (sym != NULL, "Got a null symbol");

  space = SPEC_OCLS (sym->etype);

  /* if already has one */
  if (sym->aop)
    {
      return sym->aop;
    }

  /* special case for a function */
  if (IS_FUNC (sym->type))
    {
      sym->aop = aop = newAsmop (AOP_IMMD);
      aop->aopu.aop_immd = Safe_calloc (1, strlen (sym->rname) + 1);
      strcpy (aop->aopu.aop_immd, sym->rname);
      aop->size = FARPTRSIZE;
      return aop;
    }

  /* if it is on the stack */
  if (sym->onStack)
    {
      sym->aop = aop = newAsmop (AOP_SOF);
//      aop->aopu.aop_dir = sym->rname;
      aop->size = getSize (sym->type);
      aop->aopu.aop_stk = sym->stack + (sym->stack > 0 ? _G.param_offset : 0);

      return aop;
    }

  /* if it is in direct space */
  if (IN_DIRSPACE (space))
    {
      sym->aop = aop = newAsmop (AOP_DIR);
      aop->aopu.aop_dir = sym->rname;
      aop->size = getSize (sym->type);
      return aop;
    }

  /* default to far space */
  sym->aop = aop = newAsmop (AOP_EXT);
  aop->aopu.aop_dir = sym->rname;
  aop->size = getSize (sym->type);
  return aop;
}

/*-----------------------------------------------------------------*/
/* aopForRemat - rematerializes an object                          */
/*-----------------------------------------------------------------*/
static asmop *
aopForRemat (symbol * sym)
{
  iCode *ic = sym->rematiCode;
  asmop *aop = NULL;
  int val = 0;

  if (!ic)
    {
      fprintf (stderr, "Symbol %s to be rematerialized, but has no rematiCode.\n", sym->name);
      wassert (0);
    }

  for (;;)
    {
      if (ic->op == '+')
        val += (int) operandLitValue (IC_RIGHT (ic));
      else if (ic->op == '-')
        val -= (int) operandLitValue (IC_RIGHT (ic));
      else if (IS_CAST_ICODE (ic))
        {
          ic = OP_SYMBOL (IC_RIGHT (ic))->rematiCode;
          continue;
        }
      else
        break;

      ic = OP_SYMBOL (IC_LEFT (ic))->rematiCode;
    }

  if (ic->op == ADDRESS_OF && OP_SYMBOL (IC_LEFT (ic))->onStack)
    {
      aop = newAsmop (AOP_STL);
      aop->aopu.aop_stk = OP_SYMBOL (IC_LEFT (ic))->stack + (OP_SYMBOL (IC_LEFT (ic))->stack > 0 ? _G.param_offset : 0) + val;
    }
  else if (ic->op == ADDRESS_OF)
    {
      if (val)
        {
          SNPRINTF (buffer, sizeof (buffer),
                    "(%s %c 0x%04x)", OP_SYMBOL (IC_LEFT (ic))->rname, val >= 0 ? '+' : '-', abs (val) & 0xffff);
        }
      else
        {
          strncpyz (buffer, OP_SYMBOL (IC_LEFT (ic))->rname, sizeof (buffer));
        }

      aop = newAsmop (AOP_IMMD);
      aop->aopu.aop_immd = Safe_strdup (buffer);
    }
  else if (ic->op == '=')
    {
      val += (int) operandLitValue (IC_RIGHT (ic));
      val &= 0xffff;
      SNPRINTF (buffer, sizeof (buffer), "0x%04x", val);
      aop = newAsmop (AOP_LIT);
      aop->aopu.aop_lit = constVal (buffer);
    }
  else
    {
      werror (E_INTERNAL_ERROR, __FILE__, __LINE__, "unexpected rematerialization");
    }

  return aop;
}

/*-----------------------------------------------------------------*/
/* regsInCommon - two operands have some registers in common       */
/*-----------------------------------------------------------------*/
static bool
regsInCommon (operand * op1, operand * op2)
{
  symbol *sym1, *sym2;
  int i;

  /* if they have registers in common */
  if (!IS_SYMOP (op1) || !IS_SYMOP (op2))
    return false;

  sym1 = OP_SYMBOL (op1);
  sym2 = OP_SYMBOL (op2);

  if (sym1->nRegs == 0 || sym2->nRegs == 0)
    return false;

  for (i = 0; i < sym1->nRegs; i++)
    {
      int j;
      if (!sym1->regs[i])
        continue;

      for (j = 0; j < sym2->nRegs; j++)
        {
          if (!sym2->regs[j])
            continue;

          if (sym2->regs[j] == sym1->regs[i])
            return true;
        }
    }

  return false;
}

/*-----------------------------------------------------------------*/
/* operandsEqu - equivalent                                        */
/*-----------------------------------------------------------------*/
static bool
operandsEqu (operand *op1, operand *op2)
{
  symbol *sym1, *sym2;

  /* if they not symbols */
  if (!IS_SYMOP (op1) || !IS_SYMOP (op2))
    return false;

  sym1 = OP_SYMBOL (op1);
  sym2 = OP_SYMBOL (op2);

  /* if both are itemps & one is spilt
     and the other is not then false */
  if (IS_ITEMP (op1) && IS_ITEMP (op2) && sym1->isspilt != sym2->isspilt)
    return false;

  /* if they are the same */
  if (sym1 == sym2)
    return true;

  /* if they have the same rname */
  if (sym1->rname[0] && sym2->rname[0] && strcmp (sym1->rname, sym2->rname) == 0)
    return true;

  /* if left is a tmp & right is not */
  if (IS_ITEMP (op1) && !IS_ITEMP (op2) && sym1->isspilt && (sym1->usl.spillLoc == sym2))
    return true;

  if (IS_ITEMP (op2) && !IS_ITEMP (op1) && sym2->isspilt && sym1->level > 0 && (sym2->usl.spillLoc == sym1))
    return true;

  return false;
}

/*-----------------------------------------------------------------*/
/* sameRegs - two asmops have the same registers                   */
/*-----------------------------------------------------------------*/
static bool
sameRegs (asmop *aop1, asmop *aop2)
{
  int i;

  if (aop1 == aop2)
    return true;

//  if (aop1->size != aop2->size)
//    return false;

  if (aop1->type == aop2->type && aop1->size == aop2->size)
    {
      switch (aop1->type)
        {
        case AOP_REG:
          for (i = 0; i < aop1->size; i++)
            if (aop1->aopu.aop_reg[i] != aop2->aopu.aop_reg[i])
              return false;
          return true;
        case AOP_SOF:
          if (regalloc_dry_run && aop1->op && aop2->op && IS_SYMOP (aop1->op) && IS_SYMOP (aop2->op))
            return ((IS_ITEMP (aop1->op) && OP_SYMBOL (aop1->op)->usl.spillLoc ? OP_SYMBOL (aop1->op)->usl.spillLoc : OP_SYMBOL (aop1->op)) ==
                    (IS_ITEMP (aop2->op) && OP_SYMBOL (aop2->op)->usl.spillLoc ? OP_SYMBOL (aop2->op)->usl.spillLoc : OP_SYMBOL (aop2->op)));
          return (aop1->aopu.aop_stk == aop2->aopu.aop_stk);
        case AOP_DIR:
          if (regalloc_dry_run)
            return (aop1->op && aop2->op && IS_SYMOP (aop1->op) && IS_SYMOP (aop2->op) &&
                    (IS_ITEMP (aop1->op) && OP_SYMBOL (aop1->op)->usl.spillLoc ? OP_SYMBOL (aop1->op)->usl.spillLoc : OP_SYMBOL (aop1->op)) ==
                    (IS_ITEMP (aop2->op) && OP_SYMBOL (aop2->op)->usl.spillLoc ? OP_SYMBOL (aop2->op)->usl.spillLoc : OP_SYMBOL (aop2->op)));
        case AOP_EXT:
          return (!strcmp (aop1->aopu.aop_dir, aop2->aopu.aop_dir));
        default:
          break;
        }
    }

  return false;
}

static reg_info *
mc6800_findRegAop (asmop *aop, int loffset)
{
  if (mc6800_reg_a->aop && sameRegs (mc6800_reg_a->aop, aop) && mc6800_reg_a->aopofs == loffset)
    return mc6800_reg_a;
  if (mc6800_reg_b->aop && sameRegs (mc6800_reg_b->aop, aop) && mc6800_reg_b->aopofs == loffset)
    return mc6800_reg_b;
  return NULL;
}

static void
mc6800_dirtyRegAop (asmop *aop, int loffset)
{
  if (mc6800_reg_x->aop && (mc6800_reg_x->aop->type == AOP_DIR || mc6800_reg_x->aop->type == AOP_EXT)
      && (!aop || sameRegs (mc6800_reg_x->aop, aop) && (loffset == mc6800_reg_x->aopofs || loffset == mc6800_reg_x->aopofs + 1)))
    mc6800_reg_x->aop = NULL;
  if (!aop)
    {
      mc6800_reg_a->aop = NULL;
      mc6800_reg_b->aop = NULL;
      return;
    }
  if (mc6800_reg_a->aop && sameRegs (mc6800_reg_a->aop, aop) && mc6800_reg_a->aopofs == loffset)
    mc6800_reg_a->aop = NULL;
  if (mc6800_reg_b->aop && sameRegs (mc6800_reg_b->aop, aop) && mc6800_reg_b->aopofs == loffset)
    mc6800_reg_b->aop = NULL;
}

static void
mc6800_emitLabel (symbol *tlbl)
{
  emitLabel (tlbl);
  mc6800_dirtyRegAop (NULL, 0);
}

/*-----------------------------------------------------------------*/
/* aopOp - allocates an asmop for an operand  :                    */
/*-----------------------------------------------------------------*/
static void
aopOp (operand *op, iCode * ic, bool result)
{
  asmop *aop = NULL;
  symbol *sym;
  int i;

  if (!op)
    return;

  /* if this a literal */
  if (IS_OP_LITERAL (op))
    {
      op->aop = aop = newAsmop (AOP_LIT);
      aop->aopu.aop_lit = OP_VALUE (op);
      aop->size = getSize (operandType (op));
      aop->op = op;
      if (!result)
        aop->valinfo = getOperandValinfo (ic, op);
      return;
    }

  /* if already has a asmop then continue */
  if (op->aop)
    {
      op->aop->op = op;
      return;
    }

  /* if the underlying symbol has a aop */
  if (IS_SYMOP (op) && OP_SYMBOL (op)->aop)
    {
      op->aop = aop = Safe_calloc (1, sizeof (*aop));
      memcpy (aop, OP_SYMBOL (op)->aop, sizeof (*aop));
      //op->aop = aop = OP_SYMBOL (op)->aop;
      aop->size = getSize (operandType (op));

      aop->op = op;
      return;
    }

  /* if this is a true symbol */
  if (IS_TRUE_SYMOP (op))
    {
      op->aop = aop = aopForSym (ic, OP_SYMBOL (op), result);
      aop->op = op;
      if (!result)
        aop->valinfo = getOperandValinfo (ic, op);
      return;
    }

  /* this is a temporary : this has
     only five choices :
     a) register
     b) spillocation
     c) rematerialize
     d) conditional
     e) can be a return use only */

  if (!IS_SYMOP (op))
    piCode (ic, NULL);
  sym = OP_SYMBOL (op);

  if (sym->regType == REG_CND && ic->prev && ic->prev->op == GET_VALUE_AT_ADDRESS && isOperandEqual (IC_RESULT (ic->prev), op))
    {
      aopOp (IC_LEFT (ic->prev), ic, false);
      sym->aop = op->aop = aop = newAsmop (AOP_IDX);
      aop->size = getSize (operandType (op));
      aop->op = op;
      aop->aopu.aop_stk = (int) operandLitValue (IC_RIGHT (ic->prev));
      return;
    }

  /* if the type is a conditional */
  if (sym->regType == REG_CND)
    {
      sym->aop = op->aop = aop = newAsmop (AOP_CRY);
      aop->size = 0;
      aop->op = op;
      if (!result)
        aop->valinfo = getOperandValinfo (ic, op);
      return;
    }

  /* if it is spilt then two situations
     a) is rematerialize
     b) has a spill location */
  if (sym->isspilt || sym->nRegs == 0)
    {
      /* rematerialize it NOW */
      if (sym->remat)
        {
          sym->aop = op->aop = aop = aopForRemat (sym);
          aop->size = getSize (sym->type);
          aop->op = op;
          if (!result)
            aop->valinfo = getOperandValinfo (ic, op);
          return;
        }

       wassertl (!sym->ruonly, "sym->ruonly not supported");

       if (regalloc_dry_run)     // Todo: Handle dummy iTemp correctly
        {
          if (options.stackAuto || (currFunc && IFFUNC_ISREENT (currFunc->type)))
            {
              sym->aop = op->aop = aop = newAsmop (AOP_SOF);
              if (!mc6800_dry_stack_size)
                aop->aopu.aop_stk = 8; /* bogus stack offset, high enough to prevent optimization */
              else if (sym->usl.spillLoc)
                aop->aopu.aop_stk = sym->usl.spillLoc->stack;
              else
                aop->aopu.aop_stk = -getSize (sym->type);
            }
          else if (sym->usl.spillLoc)
            sym->aop = op->aop = aop = aopForSym (ic, sym->usl.spillLoc, result);
          else
            sym->aop = op->aop = aop = newAsmop (AOP_DIR);
          aop->size = getSize (sym->type);
          aop->op = op;
          if (!result)
            aop->valinfo = getOperandValinfo (ic, op);
          return;
        }

      /* else spill location  */
      if (sym->isspilt && sym->usl.spillLoc || regalloc_dry_run)
        {
          asmop *oldAsmOp = NULL;

          if (sym->usl.spillLoc->aop && sym->usl.spillLoc->aop->size != getSize (sym->type))
            {
              /* force a new aop if sizes differ */
              oldAsmOp = sym->usl.spillLoc->aop;
              sym->usl.spillLoc->aop = NULL;
            }
          sym->aop = op->aop = aop = aopForSym (ic, sym->usl.spillLoc, result);
          if (sym->usl.spillLoc->aop->size != getSize (sym->type))
            {
              /* Don't reuse the new aop, go with the last one */
              sym->usl.spillLoc->aop = oldAsmOp;
            }
          aop->size = getSize (sym->type);
          aop->op = op;
          if (!result)
            aop->valinfo = getOperandValinfo (ic, op);
          return;
        }

      /* else must be a dummy iTemp */
      sym->aop = op->aop = aop = newAsmop (AOP_DUMMY);
      aop->size = getSize (sym->type);
      aop->op = op;
      if (!result)
        aop->valinfo = getOperandValinfo (ic, op);
      return;
    }

  /* must be in a register */
  wassert (sym->nRegs);
  sym->aop = op->aop = aop = newAsmop (AOP_REG);
  aop->size = sym->nRegs;
  for (i = 0; i < sym->nRegs; i++)
    {
       wassert (sym->regs[i] >= regsmc6800 && sym->regs[i] < regsmc6800 + 4);
       wassertl (sym->regs[i], "Symbol in register, but no register assigned.");
       aop->aopu.aop_reg[i] = sym->regs[i];
       aop->regmask |= sym->regs[i]->mask;
    }
  if ((sym->nRegs > 1) && (sym->regs[0]->mask > sym->regs[1]->mask))
    aop->regmask |= MC6800MASK_REV;
  aop->op = op;
  if (!result)
    aop->valinfo = getOperandValinfo (ic, op);
}

/*-----------------------------------------------------------------*/
/* freeAsmop - free up the asmop given to an operand               */
/*-----------------------------------------------------------------*/
static void
freeAsmop (operand * op, asmop * aaop, iCode * ic, bool pop)
{
  asmop *aop;

  if (!op)
    aop = aaop;
  else
    aop = op->aop;

  if (!aop)
    return;

  if (aop->type == AOP_IDX && !aop->freed)
    freeAsmop (IC_LEFT ((iCode *) hTabItemWithKey (iCodehTab, bitVectFirstBit (OP_DEFS (aop->op)))), NULL, ic, pop);

  if (aop->freed)
    goto dealloc;

  aop->freed = 1;

  if (aop->stacked)
    {
      int stackAdjust;
      int loffset;

      DD (emitcode ("", "; freeAsmop restoring stacked %s", aopName (aop)));
      aop->stacked = 0;
      stackAdjust = 0;
      for (loffset = 0; loffset < aop->size; loffset++)
        if (aop->stk_aop[loffset])
          {
            transferAopAop (aop->stk_aop[loffset], 0, aop, loffset);
            stackAdjust++;
          }
      pullNull (stackAdjust);
    }

dealloc:
  /* all other cases just dealloc */
  if (op)
    {
      op->aop = NULL;
      if (IS_SYMOP (op))
        {
          OP_SYMBOL (op)->aop = NULL;
          /* if the symbol has a spill */
          if (SPIL_LOC (op))
            SPIL_LOC (op)->aop = NULL;
        }
    }
}


/*-----------------------------------------------------------------*/
/* aopDerefAop - treating the aop parameter as a pointer, return   */
/*               an asmop for the object it references             */
/*-----------------------------------------------------------------*/
static asmop *
aopDerefAop (asmop * aop, int offset)
{
  int adr;
  asmop *newaop = NULL;
  sym_link *type, *etype;
  int p_type;
  struct dbuf_s dbuf;

  D (emitcode (";     aopDerefAop", ""));
  DD (emitcode ("", ";     aopDerefAop(%s)", aopName (aop)));
  if (aop->op)
    {

      type = operandType (aop->op);
      etype = getSpec (type);
      /* if op is of type of pointer then it is simple */
      if (IS_PTR (type) && !IS_FUNC (type->next))
        p_type = DCL_TYPE (type);
      else
        {
          /* we have to go by the storage class */
          p_type = PTR_TYPE (SPEC_OCLS (etype));
        }
    }
  else
    p_type = UPOINTER;

  switch (aop->type)
    {
    case AOP_IMMD:
      if (p_type == POINTER)
        newaop = newAsmop (AOP_DIR);
      else
        newaop = newAsmop (AOP_EXT);
      if (!offset)
        newaop->aopu.aop_dir = aop->aopu.aop_immd;
      else
        {
          dbuf_init (&dbuf, 64);
          dbuf_printf (&dbuf, "(%s+%d)", aop->aopu.aop_immd, offset);
          newaop->aopu.aop_dir = dbuf_detach_c_str (&dbuf);
        }
      break;
    case AOP_STL:
      newaop = newAsmop (AOP_SOF);
      newaop->aopu.aop_stk = aop->aopu.aop_stk + offset;
      newaop->op = aop->op;
      break;
    case AOP_LIT:
      adr = (int) ulFromVal (aop->aopu.aop_lit);
      if (p_type == POINTER)
        adr &= 0xff;
      adr = (adr + offset) & 0xffff;
      dbuf_init (&dbuf, 64);

      if (adr < 0x100)
        {
          newaop = newAsmop (AOP_DIR);
          dbuf_printf (&dbuf, "0x%02x", adr);
        }
      else
        {
          newaop = newAsmop (AOP_EXT);
          dbuf_printf (&dbuf, "0x%04x", adr);
        }
      newaop->aopu.aop_dir = dbuf_detach_c_str (&dbuf);
      break;
    default:
      werror (E_INTERNAL_ERROR, __FILE__, __LINE__, "unsupported asmop");
      return NULL;
    }

  return newaop;
}


/*-----------------------------------------------------------------*/
/* aopAdrStr - for referencing the address of the aop              */
/*-----------------------------------------------------------------*/
/* loffset is the logical offset (0 is the least significant byte)  */
static const char *
aopAdrStr (asmop * aop, int loffset, bool bit16)
{
  char *s = buffer;
  char *rs;
  int offset = aop->size - 1 - loffset - (bit16 ? 1 : 0);
  int xofs;

  /* offset is greater than
     size then zero */
  if (loffset > (aop->size - 1) && aop->type != AOP_LIT)
    return zero;

  /* depending on type */
  switch (aop->type)
    {
    case AOP_DUMMY:
      return zero;

    case AOP_IMMD:
      if (loffset)
        {
          if (loffset > 1)
            sprintf (s, "#(%s >> %d)", aop->aopu.aop_immd, loffset * 8);
          else
            sprintf (s, "#>%s", aop->aopu.aop_immd);
        }
      else
        sprintf (s, "#%s", aop->aopu.aop_immd);
      rs = Safe_calloc (1, strlen (s) + 1);
      strcpy (rs, s);
      return rs;

    case AOP_DIR:
      if (regalloc_dry_run)
        return "*dry";
      if (offset)
        sprintf (s, "*(%s + %d)", aop->aopu.aop_dir, offset);
      else
        sprintf (s, "*%s", aop->aopu.aop_dir);
      rs = Safe_calloc (1, strlen (s) + 1);
      strcpy (rs, s);
      return rs;

    case AOP_EXT:
      if (regalloc_dry_run)
        return "dry";
      if (offset)
        sprintf (s, "(%s + %d)", aop->aopu.aop_dir, offset);
      else
        sprintf (s, "%s", aop->aopu.aop_dir);
      rs = Safe_calloc (1, strlen (s) + 1);
      strcpy (rs, s);
      return rs;

    case AOP_REG:
      return aop->aopu.aop_reg[loffset]->name;

    case AOP_LIT:
      if (bit16)
        return aopLiteralLong (aop->aopu.aop_lit, loffset, 2);
      else
        return aopLiteral (aop->aopu.aop_lit, loffset);

    case AOP_SOF:
      if (!regalloc_dry_run && mc6800_reg_x->aop != &tsxaop)
        werror (E_INTERNAL_ERROR, __FILE__, __LINE__, "AOP_SOF without tsx");
      if (regalloc_dry_run)
        return "1,x";
      xofs = _G.stackOfs - mc6800_reg_x->stackOffset + aop->aopu.aop_stk + offset;
      if (xofs < 0 || xofs > 255)
        werror (E_INTERNAL_ERROR, __FILE__, __LINE__, "stack offset out of range");
      sprintf (s, "%d,x", xofs);
      rs = Safe_calloc (1, strlen (s) + 1);
      strcpy (rs, s);
      return rs;
    case AOP_IDX:
      xofs = aop->aopu.aop_stk + offset;
      if (xofs < 0 || xofs > 255)
        werror (E_INTERNAL_ERROR, __FILE__, __LINE__, "index offset out of range");
      if (regalloc_dry_run) /* Don't worry about the exact offset during the dry run */
        return "1,x";
      sprintf (s, "%d,x", xofs);
      rs = Safe_calloc (1, strlen (s) + 1);
      strcpy (rs, s);
      return rs;
    default:
      break;
    }

  werror (E_INTERNAL_ERROR, __FILE__, __LINE__, "aopAdrStr got unsupported aop->type");
  exit (1);
}


/*-----------------------------------------------------------------*/
/* getDataSize - get the operand data size                         */
/*-----------------------------------------------------------------*/
static int
getDataSize (operand *op)
{
  int size;
  size = AOP_SIZE (op);
  return size;
}


/*-----------------------------------------------------------------*/
/* genCopy - Copy the value from one operand to another            */
/*           The caller is responsible for aopOp and freeAsmop     */
/*-----------------------------------------------------------------*/
static void
genCopy (operand *result, operand *source)
{
  int size = AOP_SIZE (result);
  int srcsize = AOP_SIZE (source);
  int offset = 0;

  D (emitcode (";     genCopy", ""));
  D (emitcode (";     genCopy", "srcsize=%d,size=%d",srcsize,size));

  /* if they are the same and not volatile */
  if (operandsEqu (result, source) && !isOperandVolatile (result, false) &&
      !isOperandVolatile (source, false))
    return;

  /* The source and destinations may be different size due to optimizations. */
  /* This is not a cast, so there is no need to worry about sign extension. */
  /* When this happens, it is usually just 1 byte source to 2 byte dest, so */
  /* nothing significant to optimize. */
  if (srcsize < size)
    {
      if (IS_AOP_X (AOP (result)) && size == 2 && srcsize == 1)
        {
          const char *tmp = allocTemp ();

          if (AOP_TYPE (source) == AOP_REG && AOP (source)->aopu.aop_reg[0] == mc6800_reg_b)
            mc6800_emitOp ("stab", MODE_DIR, "*%s+1", tmp);
          else if (AOP_TYPE (source) == AOP_REG && AOP (source)->aopu.aop_reg[0] == mc6800_reg_a)
            mc6800_emitOp ("staa", MODE_DIR, "*%s+1", tmp);
          else
            {
              reg_info *acc = (mc6800_reg_a->isFree || !mc6800_reg_b->isFree) ? mc6800_reg_a : mc6800_reg_b;
              bool needpullacc = pushRegIfUsed (acc);

              loadRegFromAop (acc, AOP (source), 0);
              mc6800_emitOpWithAcc ("sta", acc, MODE_DIR, "*%s+1", tmp);
              pullOrFreeReg (acc, needpullacc);
            }
          mc6800_emitOp ("clr", MODE_EXT, "%s", tmp);
          mc6800_emitOp ("ldx", MODE_DIR, "*%s", tmp);
          freeTemp ();
          mc6800_dirtyReg (mc6800_reg_x, false);
          return;
        }
      size -= srcsize;
      while (srcsize)
        {
          transferAopAop (AOP (source), offset, AOP (result), offset);
          offset++;
          srcsize--;
        }
      while (size)
        {
          storeConstToAop (0, AOP (result), offset);
          offset++;
          size--;
        }

      return;
    }

  /* if they are the same registers */
  if (sameRegs (AOP (source), AOP (result)) && !isOperandVolatile (result, false) &&
      !isOperandVolatile (source, false))
    return;

  if (IS_AOP_X (AOP (result)) && srcsize == 2)
    {
      loadRegFromAop (mc6800_reg_x, AOP (source), 0);
      return;
    }
  if (IS_AOP_X (AOP (source)) && size == 2)
    {
      storeRegToAop (mc6800_reg_x, AOP (result), 0);
      return;
    }

  /* If the result and right are 2 bytes and both in registers, we have to be careful */
  /* to make sure the registers are not overwritten prematurely. */
  if (AOP_SIZE (result) == 2 && AOP (result)->type == AOP_REG && AOP (source)->type == AOP_REG)
    {
      if (AOP (result)->aopu.aop_reg[0] == AOP (source)->aopu.aop_reg[1] &&
          AOP (result)->aopu.aop_reg[1] == AOP (source)->aopu.aop_reg[0])
        {
          pushReg (AOP (source)->aopu.aop_reg[1], true);
          transferAopAop (AOP (source), 0, AOP (result), 0);
          pullReg (AOP (result)->aopu.aop_reg[1]);
        }
      else if (AOP (result)->aopu.aop_reg[0] == AOP (source)->aopu.aop_reg[1])
        {
          transferAopAop (AOP (source), 1, AOP (result), 1);
          transferAopAop (AOP (source), 0, AOP (result), 0);
        }
      else
        {
          transferAopAop (AOP (source), 0, AOP (result), 0);
          transferAopAop (AOP (source), 1, AOP (result), 1);
        }
      return;
    }

  if (size == 2 && AOP_TYPE (source) == AOP_STL)
    {
      bool needpullb = pushRegIfSurv (mc6800_reg_b);
      bool needpulla = pushRegIfSurv (mc6800_reg_a);

      loadRegFromAop (mc6800_reg_d, AOP (source), 0);
      storeRegToAop (mc6800_reg_d, AOP (result), 0);
      pullOrFreeReg (mc6800_reg_a, needpulla);
      pullOrFreeReg (mc6800_reg_b, needpullb);
      return;
    }

  /* general case */
  bool need_lsb_to_msb_order = true;
  if ((result->aop->type == AOP_DIR || result->aop->type == AOP_SOF) && // Avoid overwriting still-needed value.
    result->aop->type == source->aop->type)
   {
     bool overlap = false;
     bool result_at_lower_address = false;
     if (result->aop->type == AOP_DIR)
       {
         symbol *rsym = OP_SYMBOL (result);
         symbol *ssym = OP_SYMBOL (source);
         if(rsym && ssym && !strcmp (rsym->rname, ssym->rname))
           {
             overlap = true;
             result_at_lower_address = (result->aop->size < source->aop->size);
           }
       }
     else if (result->aop->type == AOP_SOF)
       {
         // todo.
       }
     else
       wassert (0);
     need_lsb_to_msb_order = !overlap || result_at_lower_address;
   }
  if (need_lsb_to_msb_order)
    {
      offset = 0;
      while (size)
        {
          if (size >= 2 && mc6800_reg_x->isDead &&
            (AOP_TYPE (source) == AOP_IMMD || AOP_TYPE (source) == AOP_LIT || AOP_TYPE (source) == AOP_EXT) &&
            (AOP_TYPE (result) == AOP_DIR || AOP_TYPE (result) == AOP_EXT))
            {
              loadRegFromAop (mc6800_reg_x, AOP (source), offset);
              storeRegToAop (mc6800_reg_x, AOP (result), offset);
              mc6800_freeReg (mc6800_reg_x);
              offset += 2;
              size -= 2;
            }
          else
            {
              transferAopAop (AOP (source), offset, AOP (result), offset);
              offset++;
              size--;
            }
        }
    }
  else
    {
      offset = size - 1;
      while (size)
        {
          if (size >= 2 && mc6800_reg_x->isDead &&
            (AOP_TYPE (source) == AOP_IMMD || AOP_TYPE (source) == AOP_LIT || AOP_TYPE (source) == AOP_EXT) &&
            (AOP_TYPE (result) == AOP_DIR || AOP_TYPE (result) == AOP_EXT))
            {
              loadRegFromAop (mc6800_reg_x, AOP (source), offset - 1);
              storeRegToAop (mc6800_reg_x, AOP (result), offset - 1);
              offset -= 2;
              size -= 2;
            }
          else
            {
              transferAopAop (AOP (source), offset, AOP (result), offset);
              offset--;
              size--;
            }
        }
    }
}

/*-----------------------------------------------------------------*/
/* genNot - generate code for ! operation                          */
/*-----------------------------------------------------------------*/
static void
genNot (iCode * ic)
{
  bool needpull;
  bool needpulla = false;
  reg_info *reg;
  asmop *aop;
  int offset;

  D (emitcode (";     genNot", ""));

  /* assign asmOps to operand & result */
  aopOp (IC_LEFT (ic), ic, false);
  aopOp (IC_RESULT (ic), ic, true);
  if (AOP_TYPE (IC_RESULT (ic)) == AOP_REG
      && (AOP (IC_RESULT (ic))->aopu.aop_reg[0] == mc6800_reg_a || AOP (IC_RESULT (ic))->aopu.aop_reg[0] == mc6800_reg_b))
    reg = AOP (IC_RESULT (ic))->aopu.aop_reg[0];
  else
    reg = (!mc6800_reg_b->isFree && mc6800_reg_a->isFree) ? mc6800_reg_a : mc6800_reg_b;
  needpull = pushRegIfSurv (reg);

  aop = AOP (IC_LEFT (ic));
  offset = aop->size - 1;
  if (IS_BOOL (operandType (IC_LEFT (ic))))
    {
      loadRegFromAop (reg, aop, 0);
      mc6800_emitOpWithAcc ("eor", reg, MODE_IMM, "#0x01");
    }
  else if (aop->type == AOP_LIT)
    loadRegFromConst (reg, !ullFromVal (aop->aopu.aop_lit));
  else if (aop->type == AOP_STL)
    loadRegFromConst (reg, 0);
  else if (aop->type == AOP_REG && aop->size == 1)
    {
      mc6800_emitOpWithAcc ("cmp", aop->aopu.aop_reg[0], MODE_IMM, "#0x01");
      mc6800_emitOpWithAcc ("lda", reg, MODE_IMM, "#0x00");
      rmwWithReg ("rol", reg);
    }
  else if (aop->type != AOP_REG && aop->size == 1)
    {
      loadRegFromConst (reg, 0);
      accopWithAop ("cmp", reg, aop, 0);
      mc6800_emitOpWithAcc ("sbc", reg, MODE_IMM, "#0xff");
    }
  else
    {
      if (IS_AOP_D (aop))
        {
          needpulla = reg == mc6800_reg_b && pushRegIfSurv (mc6800_reg_a);
          mc6800_emitOp ("aba", MODE_INH, "");
          mc6800_emitOp ("adca", MODE_IMM, "#0xff");
        }
      else if (IS_AOP_X (aop))
        {
          const char *tmp = allocTemp ();

          mc6800_emitOp ("stx", MODE_DIR, "*%s", tmp);
          mc6800_emitOpWithAcc ("lda", reg, MODE_DIR, "*%s", tmp);
          mc6800_emitOpWithAcc ("add", reg, MODE_DIR, "*%s+1", tmp);
          mc6800_emitOpWithAcc ("adc", reg, MODE_IMM, "#0xff");
          freeTemp ();
        }
      else if (aop->type == AOP_REG)
        werror (E_INTERNAL_ERROR, __FILE__, __LINE__, "Bad rIdx in genNot");
      else
        {
          loadRegFromAop (reg, aop, offset--);
          if (IS_FLOAT (operandType (IC_LEFT (ic))))
            mc6800_emitOpWithAcc ("and", reg, MODE_IMM, "#0x7F");
          accopWithAop ("add", reg, aop, offset--);
          while (offset >= 0)
            accopWithAop ("adc", reg, aop, offset--);
          mc6800_emitOpWithAcc ("adc", reg, MODE_IMM, "#0xff");
        }
      mc6800_emitOpWithAcc ("lda", reg, MODE_IMM, "#0x00");
      mc6800_emitOpWithAcc ("sbc", reg, MODE_IMM, "#0xff");
      if (needpulla)
        {
          mc6800_dirtyReg (mc6800_reg_a, false);
          pullReg (mc6800_reg_a);
        }
    }
  mc6800_dirtyReg (reg, false);
  storeRegToFullAop (reg, AOP (IC_RESULT (ic)), false);
  pullOrFreeReg (reg, needpull);

  freeAsmop (IC_RESULT (ic), NULL, ic, true);
  freeAsmop (IC_LEFT (ic), NULL, ic, true);
}


/*-----------------------------------------------------------------*/
/* genUminusFloat - unary minus for floating points                */
/*-----------------------------------------------------------------*/
static void
genUminusFloat (operand * op, operand * result)
{
  int size, offset = 0;
  bool needpula;

  D (emitcode (";     genUminusFloat", ""));

  /* for this we just copy and then flip the bit */

  size = AOP_SIZE (op) - 1;

  while (size--)
    {
      transferAopAop (AOP (op), offset, AOP (result), offset);
      offset++;
    }

  needpula = pushRegIfSurv (mc6800_reg_a);
  loadRegFromAop (mc6800_reg_a, AOP (op), offset);
  mc6800_emitOp ("eora", MODE_IMM, "#0x80");
  mc6800_useReg (mc6800_reg_a);
  storeRegToAop (mc6800_reg_a, AOP (result), offset);
  pullOrFreeReg (mc6800_reg_a, needpula);
}

/*-----------------------------------------------------------------*/
/* genUminus - unary minus code generation                         */
/*-----------------------------------------------------------------*/
static void
genUminus (iCode * ic)
{
  int offset, size;
  sym_link *optype;
  bool needpula, needpullb;
  asmop *result;

  sym_link *resulttype = operandType (IC_RESULT (ic));
  unsigned topbytemask = (IS_BITINT (resulttype) && SPEC_USIGN (resulttype) && (SPEC_BITINTWIDTH (resulttype) % 8)) ?
    (0xff >> (8 - SPEC_BITINTWIDTH (resulttype) % 8)) : 0xff;
  bool maskedtopbyte = (topbytemask != 0xff);

  D (emitcode (";     genUminus", ""));
  /* assign asmops */
  aopOp (IC_LEFT (ic), ic, false);
  aopOp (IC_RESULT (ic), ic, true);

  optype = operandType (IC_LEFT (ic));

  /* if float then do float stuff */
  if (IS_FLOAT (optype))
    {
      genUminusFloat (IC_LEFT (ic), IC_RESULT (ic));
      goto release;
    }

  /* otherwise subtract from zero */
  size = AOP_SIZE (IC_LEFT (ic));
  offset = 0;

  if (size == 1 && AOP_SIZE (IC_RESULT (ic)) == 1
  &&  (IS_AOP_B (AOP (IC_RESULT (ic))) || (IS_AOP_B (AOP (IC_LEFT (ic))) && mc6800_reg_b->isDead)))
    {
      loadRegFromAop (mc6800_reg_b, AOP (IC_LEFT (ic)), 0);
      rmwWithReg ("neg", mc6800_reg_b);
      if (maskedtopbyte)
        {
          mc6800_emitOp ("andb", MODE_IMM, "#0x%02x", topbytemask);
        }
      storeRegToAop (mc6800_reg_b, AOP (IC_RESULT (ic)), 0);
      goto release;
    }

  if (size == 1)
    {
      needpula = pushRegIfSurv (mc6800_reg_a);
      loadRegFromAop (mc6800_reg_a, AOP (IC_LEFT (ic)), 0);
      rmwWithReg ("neg", mc6800_reg_a);
      if (maskedtopbyte)
        {
          mc6800_emitOp ("anda", MODE_IMM, "#0x%02x", topbytemask);
        }
      mc6800_freeReg (mc6800_reg_a);
      storeRegToFullAop (mc6800_reg_a, AOP (IC_RESULT (ic)), SPEC_USIGN (operandType (IC_LEFT (ic))));
      pullOrFreeReg (mc6800_reg_a, needpula);
      goto release;
    }

  if (size == 2)
    {
      needpullb = pushRegIfSurv (mc6800_reg_b);
      needpula = pushRegIfSurv (mc6800_reg_a);
      loadRegFromAop (mc6800_reg_d, AOP (IC_LEFT (ic)), 0);
      rmwWithReg ("neg", mc6800_reg_a);
      rmwWithReg ("neg", mc6800_reg_b);
      mc6800_emitOp ("sbca", MODE_IMM, "#0x00");
      mc6800_dirtyReg (mc6800_reg_a, false);
      storeRegToAop (mc6800_reg_d, AOP (IC_RESULT (ic)), 0);
      pullOrFreeReg (mc6800_reg_a, needpula);
      pullOrFreeReg (mc6800_reg_b, needpullb);
      goto release;
    }

  result = AOP (IC_RESULT (ic));

  needpula = pushRegIfSurv (mc6800_reg_a);
  while (size--)
    {
      /* clra clears the carry, so the borrow would be lost */
      if (offset)
        {
          mc6800_emitOp ("ldaa", MODE_IMM, "#0x00");
          mc6800_dirtyReg (mc6800_reg_a, false);
        }
      else
        loadRegFromConst (mc6800_reg_a, 0);
      accopWithAop (offset ? "sbc" : "sub", mc6800_reg_a, AOP (IC_LEFT (ic)), offset);
      storeRegToAop (mc6800_reg_a, result, offset++);
    }
  storeRegSignToUpperAop (mc6800_reg_a, result, offset, SPEC_USIGN (operandType (IC_LEFT (ic))));
  pullOrFreeReg (mc6800_reg_a, needpula);

release:
  /* release the aops */
  freeAsmop (IC_RESULT (ic), NULL, ic, true);
  freeAsmop (IC_LEFT (ic), NULL, ic, false);
}

/*-----------------------------------------------------------------*/
/* saveRegisters - will look for a call and save the registers     */
/*-----------------------------------------------------------------*/
static void
saveRegisters (iCode *lic)
{
  int i;
  iCode *ic;

  /* look for call */
  for (ic = lic; ic; ic = ic->next)
    if (ic->op == CALL || ic->op == PCALL)
      break;

  if (!ic)
    {
      fprintf (stderr, "found parameter push with no function call\n");
      return;
    }

  /* if the registers have been saved already or don't need to be then
     do nothing */
  if (ic->regsSaved)
    return;
  if (IS_SYMOP (IC_LEFT (ic)) &&
      (IFFUNC_CALLEESAVES (OP_SYMBOL (IC_LEFT (ic))->type) || IFFUNC_ISNAKED (OP_SYM_TYPE (IC_LEFT (ic)))))
    return;

  if (!regalloc_dry_run)
    ic->regsSaved = 1;
  for (i = A_IDX; i <= B_IDX; i++)
    {
      if (bitVectBitValue (ic->rSurv, i))
        pushReg (mc6800_regWithIdx (i), false);
    }
}

/*-----------------------------------------------------------------*/
/* unsaveRegisters - pop the pushed registers                      */
/*-----------------------------------------------------------------*/
static void
unsaveRegisters (iCode *ic)
{
  int i;

  for (i = B_IDX; i >= A_IDX; i--)
    {
      if (bitVectBitValue (ic->rSurv, i))
        pullReg (mc6800_regWithIdx (i));
    }

}


/*-----------------------------------------------------------------*/
/* assignResultValue - store the return value of a call to oper   */
/*-----------------------------------------------------------------*/
static void
assignResultValue (operand * oper)
{
  int size = AOP_SIZE (oper);
  int offset = 0;
  bool delayed_x = false;
  asmop **retaop = (size > 2) ? mc6800_aop_ret : mc6800_aop_pass;
  while (size--)
    {
      if (!offset && AOP_TYPE (oper) == AOP_REG && AOP_SIZE (oper) > 1 && AOP (oper)->aopu.aop_reg[0]->rIdx == A_IDX)
        {
          pushReg (mc6800_reg_b, true);
          delayed_x = true;
        }
      else
        transferAopAop (retaop[offset], 0, AOP (oper), offset);
      if (retaop[offset]->type == AOP_REG)
        mc6800_freeReg (retaop[offset]->aopu.aop_reg[0]);
      offset++;
    }
  if (delayed_x)
    pullReg (mc6800_reg_a);
}

/*-----------------------------------------------------------------*/
/* genIpush - generate code for pushing this gets a little complex */
/*-----------------------------------------------------------------*/
static void
genIpush (iCode * ic)
{
  int size, offset = 0;

  D (emitcode (";", "genIpush"));

  /* if this is not a parm push : ie. it is spill push
     and spill push is always done on the local stack */
  if (!ic->parmPush)
    {
      /* and the item is spilt then do nothing */
      if (OP_SYMBOL (IC_LEFT (ic))->isspilt)
        return;

      aopOp (IC_LEFT (ic), ic, false);
      size = AOP_SIZE (IC_LEFT (ic));
      /* push it on the stack */
      while (size--)
        {
          loadRegFromAop (mc6800_reg_a, AOP (IC_LEFT (ic)), offset++);
          pushReg (mc6800_reg_a, true);
        }
      freeAsmop (IC_LEFT (ic), NULL, ic, true);
      return;
    }

  /* this is a parameter push: in this case we call
     the routine to find the call and save those
     registers that need to be saved */
  if (!regalloc_dry_run) /* Cost for saving registers is counted at CALL or PCALL */
    saveRegisters (ic);

  /* then do the push */
  aopOp (IC_LEFT (ic), ic, false);

  size = AOP_SIZE (IC_LEFT (ic));

  if (IS_AOP_X (AOP (IC_LEFT (ic))))
    {
      const char *tmp = allocTemp ();
      mc6800_emitOp ("stx", MODE_DIR, "*%s", tmp);
      mc6800_emitOp ("ldaa", MODE_DIR, "*%s+1", tmp);
      mc6800_dirtyReg (mc6800_reg_a, false);
      mc6800_useReg (mc6800_reg_a);
      pushReg (mc6800_reg_a, true);
      mc6800_emitOp ("ldaa", MODE_DIR, "*%s", tmp);
      mc6800_dirtyReg (mc6800_reg_a, false);
      mc6800_useReg (mc6800_reg_a);
      pushReg (mc6800_reg_a, true);
      freeTemp ();
      goto release;
    }

  if (AOP_TYPE (IC_LEFT (ic)) == AOP_REG)
    {
      while (size--)
        pushReg (AOP (IC_LEFT (ic))->aopu.aop_reg[offset++], true);
      goto release;
    }

  if (AOP_TYPE (IC_LEFT (ic)) == AOP_STL && size == 2 && mc6800_reg_b->isFree)
    {
      loadRegFromAop (mc6800_reg_d, AOP (IC_LEFT (ic)), 0);
      pushReg (mc6800_reg_d, true);
      goto release;
    }

  while (size--)
    {
      loadRegFromAop (mc6800_reg_a, AOP (IC_LEFT (ic)), offset++);
      pushReg (mc6800_reg_a, true);
    }
release:
  freeAsmop (IC_LEFT (ic), NULL, ic, true);
}

/*-----------------------------------------------------------------*/
/* genPointerPush - generate code for pushing                      */
/*-----------------------------------------------------------------*/
static void
genPointerPush (iCode *ic)
{
  operand *left = IC_LEFT (ic);

  D (emitcode (";     genPointerPush", ""));

  if (!regalloc_dry_run)
    saveRegisters (ic);

  aopOp (left, ic, false);

  wassertl (IC_RIGHT (ic), "IPUSH_VALUE_AT_ADDRESS without right operand");
  wassertl (IS_OP_LITERAL (IC_RIGHT (ic)), "IPUSH_VALUE_AT_ADDRESS with non-literal right operand");
  wassertl (!operandLitValue (IC_RIGHT(ic)), "IPUSH_VALUE_AT_ADDRESS with non-zero right operand");

  loadRegFromAop (mc6800_reg_x, left->aop, 0);
  /* so x now contains the address */

  int size = getSize (operandType (IC_LEFT (ic))->next);
  while (size--)
    {
      loadRegIndexed (mc6800_reg_a, size, 0);
      pushReg (mc6800_reg_a, true);
    }
  freeAsmop (IC_LEFT (ic), NULL, ic, true);
}

/*-----------------------------------------------------------------*/
/* genIpop - recover the registers: can happen only for spilling   */
/*-----------------------------------------------------------------*/
static void
genIpop (iCode * ic)
{
  int size, offset;

  D (emitcode (";", "genIpop"));

  /* if the temp was not pushed then */
  if (OP_SYMBOL (IC_LEFT (ic))->isspilt)
    return;

  aopOp (IC_LEFT (ic), ic, false);
  size = AOP_SIZE (IC_LEFT (ic));
  offset = size - 1;
  while (size--)
    {
      pullReg (mc6800_reg_a);
      storeRegToAop (mc6800_reg_a, AOP (IC_LEFT (ic)), offset--);
    }

  freeAsmop (IC_LEFT (ic), NULL, ic, true);
}

/*-----------------------------------------------------------------*/
/* genSend - gen code for SEND                                     */
/*-----------------------------------------------------------------*/
static void
genSend (set *sendSet)
{
  iCode *send1;
  iCode *send2;
  int size;

  D (emitcode (";", "genSend"));

  send1 = setFirstItem (sendSet);
  send2 = setNextItem (sendSet);
  wassert (send1);

  if (!send2)
    {
      aopOp (IC_LEFT (send1), send1, false);
      size = AOP_SIZE (IC_LEFT (send1));
      wassert (size <= 2);
      if (size == 1)
        {
          loadRegFromAop (send1->argreg == 2 ? mc6800_reg_a : mc6800_reg_b, AOP (IC_LEFT (send1)), 0);
        }
      else
        {
          loadRegFromAop (mc6800_reg_d, AOP (IC_LEFT (send1)), 0);
        }
      freeAsmop (IC_LEFT (send1), NULL, send1, true);
    }
  else
    {
      if (send1->argreg > send2->argreg)
        {
          iCode *sic = send1;
          send1 = send2;
          send2 = sic;
        }
      aopOp (IC_LEFT (send1), send1, false);
      aopOp (IC_LEFT (send2), send2, false);
      wassert (AOP_SIZE (IC_LEFT (send1)) == 1 && AOP_SIZE (IC_LEFT (send2)) == 1);
      if (IS_AOP_A (AOP (IC_LEFT (send1))) && IS_AOP_B (AOP (IC_LEFT (send2))))
        {
          pushReg (mc6800_reg_a, false);
          transferRegReg (mc6800_reg_b, mc6800_reg_a, false);
          pullReg (mc6800_reg_b);
        }
      else if (IS_AOP_B (AOP (IC_LEFT (send2))))
        {
          loadRegFromAop (mc6800_reg_a, AOP (IC_LEFT (send2)), 0);
          loadRegFromAop (mc6800_reg_b, AOP (IC_LEFT (send1)), 0);
        }
      else
        {
          loadRegFromAop (mc6800_reg_b, AOP (IC_LEFT (send1)), 0);
          loadRegFromAop (mc6800_reg_a, AOP (IC_LEFT (send2)), 0);
        }
      freeAsmop (IC_LEFT (send2), NULL, send2, true);
      freeAsmop (IC_LEFT (send1), NULL, send1, true);
    }
}

/*-----------------------------------------------------------------*/
/* pushbigreturn - emit code to push hidden pointer for struct return */
/*-----------------------------------------------------------------*/
static void
pushbigreturn (operand *result)
{
  wassert (result);

  D (emitcode (";", "pushbigreturn"));

  symbol *sym = OP_SYMBOL (result);
  wassert (sym);

  if (sym->onStack)
    {
      const char *tmp = setupTmpFromSP (_G.stackOfs + sym->stack + (sym->stack > 0 ? _G.param_offset : 0));

      mc6800_emitOp ("ldab", MODE_DIR, "*%s+1", tmp);
      mc6800_dirtyReg (mc6800_reg_b, false);
      pushReg (mc6800_reg_b, true);
      mc6800_emitOp ("ldab", MODE_DIR, "*%s", tmp);
      mc6800_dirtyReg (mc6800_reg_b, false);
      pushReg (mc6800_reg_b, true);
      freeTemp ();
    }
  else
    {
      mc6800_emitOp ("ldab", MODE_IMM, "#%s", sym->rname);
      mc6800_dirtyReg (mc6800_reg_b, false);
      pushReg (mc6800_reg_b, true);
      mc6800_emitOp ("ldab", MODE_IMM, "#>%s", sym->rname);
      mc6800_dirtyReg (mc6800_reg_b, false);
      pushReg (mc6800_reg_b, true);
    }
}

/*-----------------------------------------------------------------*/
/* genCall - generates a call statement                            */
/*-----------------------------------------------------------------*/
static void
genCall (iCode * ic)
{
  sym_link *dtype;
  sym_link *etype;

  D (emitcode (";", "genCall"));

  if (!ic->regsSaved)
    saveRegisters (ic);

  dtype = operandType (IC_LEFT (ic));
  etype = getSpec (dtype);

  const bool bigreturn = IS_STRUCT (dtype->next);

  if (bigreturn)
    pushbigreturn (IC_RESULT (ic));

  if (_G.sendSet && !regalloc_dry_run)
    {
      genSend (_G.sendSet);
      _G.sendSet = NULL;
    }

  if (IS_LITERAL (etype))
    {
      mc6800_emitOp ("jsr", MODE_EXT, "0x%04X", (unsigned int) ulFromVal (OP_VALUE (IC_LEFT (ic))));
    }
  else
    {
      mc6800_emitOp ("jsr", MODE_EXT, "%s", (OP_SYMBOL (IC_LEFT (ic))->rname[0] ?
                              OP_SYMBOL (IC_LEFT (ic))->rname : OP_SYMBOL (IC_LEFT (ic))->name));
    }

  mc6800_dirtyReg (mc6800_reg_a, false);
  mc6800_dirtyReg (mc6800_reg_b, false);
  mc6800_dirtyReg (mc6800_reg_x, true);

  if (!bigreturn &&
      ((IS_ITEMP (IC_RESULT (ic)) &&
       (OP_SYMBOL (IC_RESULT (ic))->nRegs || OP_SYMBOL (IC_RESULT (ic))->spildir)) || IS_TRUE_SYMOP (IC_RESULT (ic))))
    {
      if (operandSize (IC_RESULT (ic)) <= 2)
        {
          mc6800_useReg (mc6800_reg_b);
          if (operandSize (IC_RESULT (ic)) > 1)
            mc6800_useReg (mc6800_reg_a);
        }
      aopOp (IC_RESULT (ic), ic, false);

      assignResultValue (IC_RESULT (ic));

      freeAsmop (IC_RESULT (ic), NULL, ic, true);
    }

  if (ic->parmBytes + bigreturn * 2)
    pullNull (ic->parmBytes + bigreturn * 2);

  if ((ic->regsSaved || regalloc_dry_run) && !IFFUNC_CALLEESAVES (dtype))
    unsaveRegisters (ic);
}

/*-----------------------------------------------------------------*/
/* genPcall - generates a call by pointer statement                */
/*-----------------------------------------------------------------*/
static void
genPcall (iCode * ic)
{
  sym_link *dtype;
  sym_link *etype;
  symbol *rlbl = (regalloc_dry_run ? 0 : newiTempLabel (NULL));
  symbol *tlbl = (regalloc_dry_run ? 0 : newiTempLabel (NULL));
  iCode * sendic;
  const char *tmp = NULL;

  D (emitcode (";", "genPcall"));

  dtype = operandType (IC_LEFT (ic))->next;
  etype = getSpec (dtype);

  const bool bigreturn = IS_STRUCT (dtype->next);

  if (bigreturn)
    pushbigreturn (IC_RESULT (ic));

  /* if caller saves & we have not saved then */
  if (!ic->regsSaved)
    saveRegisters (ic);

  /* Go through the send set and mark any registers used by iTemps as */
  /* in use so we don't clobber them while setting up the return address */
  for (sendic = setFirstItem (_G.sendSet); sendic; sendic = setNextItem (_G.sendSet))
    {
      updateiTempRegisterUse (IC_LEFT (sendic));
    }

  if (!IS_LITERAL (etype))
    {
      aopOp (IC_LEFT (ic), ic, false);
      if (IS_AOP_D (AOP (IC_LEFT (ic))))
        {
          tmp = allocTemp ();
          mc6800_emitOp ("stab", MODE_DIR, "*%s+1", tmp);
          mc6800_emitOp ("staa", MODE_DIR, "*%s", tmp);
          mc6800_freeReg (mc6800_reg_d);
        }
    }

  /* if send set is not empty then assign */
  if (_G.sendSet && !regalloc_dry_run)
    {
      genSend (reverseSet (_G.sendSet));
      _G.sendSet = NULL;
    }

  /* make the call */
  if (!IS_LITERAL (etype))
    {
      if (IS_AOP_D (AOP (IC_LEFT (ic))))
        {
          mc6800_emitOp ("ldx", MODE_DIR, "*%s", tmp);
          mc6800_dirtyReg (mc6800_reg_x, false);
          freeTemp ();
        }
      else
        loadRegFromAop (mc6800_reg_x, AOP (IC_LEFT (ic)), 0);
      freeAsmop (IC_LEFT (ic), NULL, ic, true);
      mc6800_emitOp ("jsr", MODE_IDX, "0,x");
    }
  else
    {
      mc6800_emitOp ("jsr", MODE_EXT, "0x%04X", ulFromVal (OP_VALUE (IC_LEFT (ic))));
    }

  mc6800_dirtyReg (mc6800_reg_a, false);
  mc6800_dirtyReg (mc6800_reg_b, false);
  mc6800_dirtyReg (mc6800_reg_x, true);

  /* if we need assign a result value */
  if (!bigreturn &&
      ((IS_ITEMP (IC_RESULT (ic)) &&
       (OP_SYMBOL (IC_RESULT (ic))->nRegs || OP_SYMBOL (IC_RESULT (ic))->spildir)) || IS_TRUE_SYMOP (IC_RESULT (ic))))
    {
      if (operandSize (IC_RESULT (ic)) <= 2)
        {
          mc6800_useReg (mc6800_reg_b);
          if (operandSize (IC_RESULT (ic)) > 1)
            mc6800_useReg (mc6800_reg_a);
        }
      aopOp (IC_RESULT (ic), ic, false);

      assignResultValue (IC_RESULT (ic));

      freeAsmop (IC_RESULT (ic), NULL, ic, true);
    }

  /* adjust the stack for parameters if required */
  if (ic->parmBytes + bigreturn * 2)
    {
      pullNull (ic->parmBytes + bigreturn * 2);
    }
  /* if we had saved some registers then unsave them */
  if ((ic->regsSaved || regalloc_dry_run) && !IFFUNC_CALLEESAVES (dtype))
    unsaveRegisters (ic);
}

/*-----------------------------------------------------------------*/
/* resultRemat - result  is rematerializable                       */
/*-----------------------------------------------------------------*/
static int
resultRemat (iCode * ic)
{
  if (SKIP_IC (ic) || ic->op == IFX)
    return 0;

  if (IC_RESULT (ic) && IS_ITEMP (IC_RESULT (ic)))
    {
      symbol *sym = OP_SYMBOL (IC_RESULT (ic));
      if (sym->remat && !POINTER_SET (ic))
        return 1;
    }

  return 0;
}

/*-----------------------------------------------------------------*/
/* regsCmp - true if two register names are equal, ignoring case   */
/*-----------------------------------------------------------------*/
static int
regsCmp (void *p1, void *p2)
{
  return (STRCASECMP ((char *) p1, (char *) (p2)) == 0);
}

static bool
inExcludeList (char *s)
{
  const char *p = setFirstItem (options.excludeRegsSet);

  if (p == NULL || STRCASECMP (p, "none") == 0)
    return false;


  return isinSetWith (options.excludeRegsSet, s, regsCmp);
}

/*-----------------------------------------------------------------*/
/* genFunction - generated code for function entry                 */
/*-----------------------------------------------------------------*/
static void
genFunction (iCode * ic)
{
  symbol *sym = OP_SYMBOL (IC_LEFT (ic));
  sym_link *ftype;
  iCode *ric = (ic->next && ic->next->op == RECEIVE) ? ic->next : NULL;
  int stackAdjust = sym->stack;
  int accIsFree = sym->recvSize == 0;


  D (emitcode (";     genFunction", ""));
  _G.stackPushes = 0;
  /* create the function header */
  emitcode (";", "-----------------------------------------");
  emitcode (";", " function %s", sym->name);
  emitcode (";", "-----------------------------------------");
  emitcode (";", mc6800_assignment_optimal ? "Register assignment is optimal." : "Register assignment might be sub-optimal.");
  emitcode (";", "Stack space usage: %d bytes.", sym->stack);

  emitcode ("", "%s:", sym->rname);
  genLine.lineCurr->isLabel = 1;
  ftype = operandType (IC_LEFT (ic));

  _G.param_offset = IS_STRUCT (ftype->next) ? 2 : 0;
  _G.stackOfs = 0;
  _G.stackPushes = 0;
  if (options.debug && !regalloc_dry_run)
    debugFile->writeFrameAddress (NULL, mc6800_reg_sp, 0);

  if (IFFUNC_ISNAKED (ftype))
    {
      emitcode (";", "naked function: no prologue.");
      return;
    }

  if (IFFUNC_ISISR (sym->type))
    {
      symbol *tlbl = newiTempLabel (NULL);

      mc6800_emitOp ("ldx", MODE_IMM, "#___SDCC_mc6800_ret0");
      mc6800_emitLabel (tlbl);
      mc6800_emitOp ("ldaa", MODE_IDX, "0,x");
      mc6800_emitOp ("psha", MODE_INH, "");
      mc6800_emitOp ("inx", MODE_INH, "");
      mc6800_emitOp ("cpx", MODE_IMM, "#___SDCC_mc6800_ret0+24");
      emitBranch ("bne", tlbl);
      mc6800_dirtyReg (mc6800_reg_a, false);
      mc6800_dirtyReg (mc6800_reg_x, false);
      _G.stackPushes += 24;
      updateCFA ();
    }

  /* For some cases it is worthwhile to perform a RECEIVE iCode */
  /* before setting up the stack frame completely. */
  while (ric && ric->next && ric->next->op == RECEIVE)
    ric = ric->next;
  while (ric && IC_RESULT (ric))
    {
      symbol *rsym = OP_SYMBOL (IC_RESULT (ric));
      int rsymSize = rsym ? getSize (rsym->type) : 0;

      if (rsym->isitmp)
        {
          if (rsym && rsym->regType == REG_CND)
            rsym = NULL;
          if (rsym && (/*rsym->accuse ||*/ rsym->ruonly))
            rsym = NULL;
          if (rsym && (rsym->isspilt || rsym->nRegs == 0) && rsym->usl.spillLoc)
            rsym = rsym->usl.spillLoc;
        }

      /* If the RECEIVE operand immediately spills to the first entry on the  */
      /* stack, we can push it directly rather than use an sp relative store. */
      if (rsym && rsym->onStack && rsym->stack == -_G.stackPushes - rsymSize)
        {
          int ofs;

          genLine.lineElement.ic = ric;
          D (emitcode (";     genReceive", ""));
          for (ofs = 0; ofs < rsymSize; ofs++)
            {
              reg_info *reg = mc6800_aop_pass[ofs + (ric->argreg - 1)]->aopu.aop_reg[0];
              pushReg (reg, true);
              if (reg->rIdx == A_IDX)
                accIsFree = 1;
              stackAdjust--;
            }
          genLine.lineElement.ic = ic;
          ric->generated = 1;
        }
      ric = (ric->prev && ric->prev->op == RECEIVE) ? ric->prev : NULL;
    }

  /* A copy of a received parameter to the top of the stack frame can be pushed, too. */
  if (!regalloc_dry_run && ic->next && ic->next->op == RECEIVE)
    for (iCode *aic = ic->next->next; aic && (aic->op == '=' && !POINTER_SET (aic) || aic->op == CAST); aic = aic->next)
      {
        symbol *dsym = OP_SYMBOL (IC_RESULT (aic));
        int size = getSize (operandType (IC_RESULT (aic)));

        if (IS_ITEMP (IC_RESULT (aic)) && (dsym->isspilt || !dsym->nRegs) && dsym->usl.spillLoc)
          dsym = dsym->usl.spillLoc;
        if (IS_SYMOP (IC_RIGHT (aic)) && dsym == OP_SYMBOL (IC_RIGHT (aic)))
          continue;
        if (!isOperandEqual (IC_RESULT (ic->next), IC_RIGHT (aic)) || !dsym->onStack || dsym->stack != -_G.stackPushes - size
            || size != getSize (operandType (IC_RIGHT (aic))))
          break;
        for (int ofs = 0; ofs < size; ofs++, stackAdjust--)
          pushReg (mc6800_aop_pass[ofs + ic->next->argreg - 1]->aopu.aop_reg[0], false);
        aic->generated = 1;
      }

  /* adjust the stack for the function */
  if (stackAdjust)
    {
      adjustStack (-stackAdjust);
    }
  _G.stackOfs = sym->stack;
  _G.stackPushes = 0;

  /* if critical function then turn interrupts off */
  if (IFFUNC_ISCRITICAL (ftype))
    {
      if (!accIsFree)
        {
          /* Function was passed parameters, so make sure A is preserved */
          pushReg (mc6800_reg_a, false);
          pushReg (mc6800_reg_a, false);
          mc6800_emitOp ("tpa", MODE_INH, "");
          mc6800_emitOp ("tsx", MODE_INH, "");
          mc6800_emitOp ("staa", MODE_IDX, "1,x");
          mc6800_emitOp ("sei", MODE_INH, "");
          mc6800_dirtyReg (mc6800_reg_x, false);
          pullReg (mc6800_reg_a);
        }
      else
        {
          /* No passed parameters, so A can be freely modified */
          mc6800_emitOp ("tpa", MODE_INH, "");
          pushReg (mc6800_reg_a, true);
          mc6800_emitOp ("sei", MODE_INH, "");
        }
    }
}

/*-----------------------------------------------------------------*/
/* genEndFunction - generates epilogue for functions               */
/*-----------------------------------------------------------------*/
static void
genEndFunction (iCode * ic)
{
  symbol *sym = OP_SYMBOL (IC_LEFT (ic));

  D (emitcode (";     genReceive", ""));
  if (IFFUNC_ISNAKED (sym->type))
    {
      emitcode (";", "naked function: no epilogue.");
      if (options.debug && currFunc && !regalloc_dry_run)
        debugFile->writeEndFunction (currFunc, ic, 0);
      return;
    }

  if (IFFUNC_ISCRITICAL (sym->type))
    {
      if (!IS_VOID (sym->type->next))
        {
          /* Function has return value, so make sure A is preserved */
          pushReg (mc6800_reg_a, false);
          mc6800_emitOp ("tsx", MODE_INH, "");
          mc6800_emitOp ("ldaa", MODE_IDX, "1,x");
          mc6800_emitOp ("tap", MODE_INH, "");
          mc6800_dirtyReg (mc6800_reg_x, false);
          pullReg (mc6800_reg_a);
          pullNull (1);
        }
      else
        {
          /* Function returns void, so A can be freely modified */
          pullReg (mc6800_reg_a);
          mc6800_emitOp ("tap", MODE_INH, "");
        }
    }

  if (IFFUNC_ISREENT (sym->type) || options.stackAuto)
    {
    }

  if (sym->stack)
    {
      _G.stackPushes += sym->stack;
      adjustStack (sym->stack);
    }


  if ((IFFUNC_ISREENT (sym->type) || options.stackAuto))
    {
    }

  if (IFFUNC_ISISR (sym->type))
    {
      symbol *tlbl = newiTempLabel (NULL);

      mc6800_emitOp ("ldx", MODE_IMM, "#___SDCC_mc6800_ret0+24");
      mc6800_emitLabel (tlbl);
      mc6800_emitOp ("dex", MODE_INH, "");
      mc6800_emitOp ("pula", MODE_INH, "");
      mc6800_emitOp ("staa", MODE_IDX, "0,x");
      mc6800_emitOp ("cpx", MODE_IMM, "#___SDCC_mc6800_ret0");
      emitBranch ("bne", tlbl);
      _G.stackPushes -= 24;

      /* if debug then send end of function */
      if (options.debug && currFunc && !regalloc_dry_run)
        {
          debugFile->writeEndFunction (currFunc, ic, 1);
        }

      mc6800_emitOp ("rti", MODE_INH, "");
    }
  else
    {
      /* if debug then send end of function */
      if (options.debug && currFunc && !regalloc_dry_run)
        {
          debugFile->writeEndFunction (currFunc, ic, 1);
        }

      mc6800_emitOp ("rts", MODE_INH, "");
    }
}

/*-----------------------------------------------------------------*/
/* genRet - generate code for return statement                     */
/*-----------------------------------------------------------------*/
static void
genRet (iCode * ic)
{
  int size, offset = 0;
//  int pushed = 0;
  bool delayed_x = false;

  D (emitcode (";     genRet", ""));

  /* if we have no return value then
     just jump to the return */
  if (!IC_LEFT (ic))
    goto jumpret;

  /* we have something to return then
     move the return value into place */
  aopOp (IC_LEFT (ic), ic, false);
  size = AOP_SIZE (IC_LEFT (ic));
  const bool bigreturn = IS_STRUCT (operandType (IC_LEFT (ic)));

  if (bigreturn)
    {
      const char *dst = allocTemp ();

      mc6800_useReg (mc6800_reg_x);
      setupXFromSP (_G.stackOfs + 2);
      mc6800_emitOp ("ldx", MODE_IDX, "0,x");
      mc6800_dirtyReg (mc6800_reg_x, false);
      mc6800_emitOp ("stx", MODE_DIR, "*%s", dst);
      mc6800_freeReg (mc6800_reg_x);

      if (size <= 2)
        {
          loadRegFromAop (mc6800_reg_a, AOP (IC_LEFT (ic)), 0);
          if (size > 1)
            loadRegFromAop (mc6800_reg_b, AOP (IC_LEFT (ic)), 1);
          mc6800_emitOp ("ldx", MODE_DIR, "*%s", dst);
          mc6800_dirtyReg (mc6800_reg_x, false);
          mc6800_emitOp ("staa", MODE_IDX, "%d,x", size - 1);
          if (size > 1)
            mc6800_emitOp ("stab", MODE_IDX, "0,x");
        }
      else if (AOP_TYPE (IC_LEFT (ic)) == AOP_SOF)
        {
          const char *src = allocTemp ();

          setupXFromSP (_G.stackOfs + AOP (IC_LEFT (ic))->aopu.aop_stk);
          mc6800_emitOp ("stx", MODE_DIR, "*%s", src);

          for (offset = 0; offset < size; offset += 2)
            {
              if (size - 1 > 255)
                {
                  UNIMPLEMENTED;
                  break;
                }
              mc6800_emitOp ("ldx", MODE_DIR, "*%s", src);
              mc6800_dirtyReg (mc6800_reg_x, false);
              mc6800_emitOp ("ldaa", MODE_IDX, "%d,x", size - 1 - offset);
              if (offset + 1 < size)
                mc6800_emitOp ("ldab", MODE_IDX, "%d,x", size - 2 - offset);
              mc6800_emitOp ("ldx", MODE_DIR, "*%s", dst);
              mc6800_emitOp ("staa", MODE_IDX, "%d,x", size - 1 - offset);
              if (offset + 1 < size)
                mc6800_emitOp ("stab", MODE_IDX, "%d,x", size - 2 - offset);
            }
          freeTemp ();
        }
      else if (AOP_TYPE (IC_LEFT (ic)) == AOP_DIR || AOP_TYPE (IC_LEFT (ic)) == AOP_EXT)
        {
          for (offset = 0; offset < size; offset += 2)
            {
              if (size - 1 > 255)
                {
                  UNIMPLEMENTED;
                  break;
                }
              loadRegFromAop (mc6800_reg_a, AOP (IC_LEFT (ic)), offset);
              if (offset + 1 < size)
                loadRegFromAop (mc6800_reg_b, AOP (IC_LEFT (ic)), offset + 1);
              mc6800_emitOp ("ldx", MODE_DIR, "*%s", dst);
              mc6800_dirtyReg (mc6800_reg_x, false);
              mc6800_emitOp ("staa", MODE_IDX, "%d,x", size - 1 - offset);
              if (offset + 1 < size)
                mc6800_emitOp ("stab", MODE_IDX, "%d,x", size - 2 - offset);
            }
        }
      else
        UNIMPLEMENTED;

      mc6800_dirtyReg (mc6800_reg_a, false);
      mc6800_dirtyReg (mc6800_reg_b, false);
      mc6800_dirtyReg (mc6800_reg_x, true);
      freeTemp ();
      freeAsmop (IC_LEFT (ic), NULL, ic, true);
      goto jumpret;
    }

  asmop **retaop = (size > 2) ? mc6800_aop_ret : mc6800_aop_pass;

  if (AOP_TYPE (IC_LEFT (ic)) == AOP_LIT)
    {
      offset = 0;
      while (size--)
        {
          transferAopAop (AOP (IC_LEFT (ic)), offset, retaop[offset], 0);
          offset++;
        }
    }
  else
    {
      /* Take care when swapping a and b */
      if (AOP_TYPE (IC_LEFT (ic)) == AOP_REG && size > 1 && AOP (IC_LEFT (ic))->aopu.aop_reg[0]->rIdx == A_IDX)
        {
          delayed_x = true;
          pushReg (mc6800_reg_a, true);
        }

      if (!delayed_x && size > 2 && mc6800_findRegAop (AOP (IC_LEFT (ic)), size - 1))
        {
          transferAopAop (AOP (IC_LEFT (ic)), size - 1, retaop[size - 1], 0);
          size--;
        }

      for (offset = 0; offset < size; offset++)
        if (!(delayed_x && !offset))
          transferAopAop (AOP (IC_LEFT (ic)), offset, retaop[offset], 0);

      if (delayed_x)
        pullReg (mc6800_reg_b);
    }

  freeAsmop (IC_LEFT (ic), NULL, ic, true);

jumpret:
  /* generate a jump to the return label
     if the next is not the return statement */
  if (!(ic->next && ic->next->op == LABEL && IC_LABEL (ic->next) == returnLabel))
    {
      mc6800_emitOp ("jmp", MODE_EXT, "%05d$", labelKey2num (returnLabel->key));
    }
}

/*-----------------------------------------------------------------*/
/* genLabel - generates a label                                    */
/*-----------------------------------------------------------------*/
static void
genLabel (iCode * ic)
{
  int i;
  reg_info *reg;

  /* For the high level labels we cannot depend on any */
  /* register's contents. Amnesia time.                */
  for (i = A_IDX; i <= D_IDX; i++)
    {
      reg = mc6800_regWithIdx (i);
      if (reg)
        {
          reg->aop = NULL;
          reg->isLitConst = 0;
        }
    }

  /* special case never generate */
  if (IC_LABEL (ic) == entryLabel)
    return;

  if (options.debug && !regalloc_dry_run)
    debugFile->writeLabel (IC_LABEL (ic), ic);

  mc6800_emitLabel (IC_LABEL (ic));

}

/*-----------------------------------------------------------------*/
/* genGoto - generates a jmp                                      */
/*-----------------------------------------------------------------*/
static void
genGoto (iCode * ic)
{
  mc6800_emitOp ("jmp", MODE_EXT, "%05d$", labelKey2num (IC_LABEL (ic)->key));
}


/*-----------------------------------------------------------------*/
/* genPlusIncr :- does addition with increment if possible         */
/*-----------------------------------------------------------------*/
static bool
genPlusIncr (iCode * ic)
{
  float cycles;
  int icount;
  operand *left;
  operand *result;
  bool needpula;
  unsigned int size = getDataSize (IC_RESULT (ic));
  unsigned int offset;
  symbol *tlbl = NULL;

  D (emitcode (";     genPlusIncr", ""));
  left = IC_LEFT (ic);
  result = IC_RESULT (ic);

  if (AOP_TYPE (IC_RIGHT (ic)) != AOP_LIT)
    return false;

  if (IS_BITINT (operandType (result)) && SPEC_USIGN (operandType (result)) && (SPEC_BITINTWIDTH (operandType (result)) % 8))
    return false;

  icount = (int) ulFromVal (AOP (IC_RIGHT (ic))->aopu.aop_lit);

  if (IS_AOP_X (AOP (left)) && IS_AOP_X (AOP (result)))
    {
      icount = (short) icount;
      if (abs (icount) > ((optimize.codeSize && !optimize.codeSpeed) ? 15 : 6))
        return false;
      addConstToX (icount);
      return true;
    }

  if ((icount > 255) || (icount < 1))
    return false;

  if (!sameRegs (AOP (left), AOP (result)))
    {
      if (size != 2 || icount > ((optimize.codeSize && !optimize.codeSpeed) ? 15 : 6))
        return false;
      if (AOP_TYPE (result) == AOP_SOF || (AOP_TYPE (result) == AOP_REG && !IS_AOP_X (AOP (result))))
        return false;
      if (!mc6800_reg_x->isDead || (AOP_TYPE (left) == AOP_REG && !IS_AOP_X (AOP (left))))
        return false;
      loadRegFromAop (mc6800_reg_x, AOP (left), 0);
      addConstToX (icount);
      storeRegToAop (mc6800_reg_x, AOP (result), 0);
      return true;
    }

  if (AOP_TYPE (result) == AOP_REG)
    {
      if (size != 1 || icount != 1 || !(IS_AOP_A (AOP (result)) || IS_AOP_B (AOP (result))))
        return false;
      rmwWithAop ("inc", AOP (result), 0);
      return true;
    }

  if (size == 2 && icount <= ((optimize.codeSize && !optimize.codeSpeed) ? 15 : 6)
      && (AOP_TYPE (result) == AOP_DIR || AOP_TYPE (result) == AOP_EXT)
      && mc6800_reg_x->aop && sameRegs (mc6800_reg_x->aop, AOP (result)) && mc6800_reg_x->aopofs == 0
      && mc6800_reg_x->isFree && mc6800_reg_x->isDead)
    {
      addConstToX (icount);
      storeRegToAop (mc6800_reg_x, AOP (result), 0);
      return true;
    }

  if (size > 1)
    tlbl = regalloc_dry_run ? 0 : newiTempLabel (NULL);

  needpula = false;
  if (icount == 1)
    {
      rmwWithAop ("inc", AOP (result), 0);
      if (size > 1)
        emitBranch ("bne", tlbl);
    }
  else
    {
      needpula = pushRegIfUsed (mc6800_reg_a);
      loadRegFromAop (mc6800_reg_a, AOP (result), 0);
      accopWithAop ("add", mc6800_reg_a, AOP (IC_RIGHT (ic)), 0);
      storeRegToAop (mc6800_reg_a, AOP (result), 0);
      if (size > 1)
        emitBranch ("bcc", tlbl);
    }
  cycles = regalloc_dry_run_cost_cycles;
  for (offset = 1; offset < size; offset++)
    {
      rmwWithAop ("inc", AOP (result), offset);
      if (offset + 1 < size)
        emitBranch ("bne", tlbl);
    }
  regalloc_dry_run_cost_cycles = cycles;
  if (size > 1 && !regalloc_dry_run)
    mc6800_emitLabel (tlbl);
  pullOrFreeReg (mc6800_reg_a, needpula);
  return true;
}

static void
genPlus8 (iCode *ic)
{
  asmop *leftOp  = AOP (IC_LEFT (ic));
  asmop *rightOp = AOP (IC_RIGHT (ic));
  asmop *result  = AOP (IC_RESULT (ic));
  reg_info *reg;
  bool needpullb = false;
  sym_link *resulttype = operandType (IC_RESULT (ic));
  unsigned topbytemask = (IS_BITINT (resulttype) && SPEC_USIGN (resulttype) && (SPEC_BITINTWIDTH (resulttype) % 8)) ?
    (0xff >> (8 - SPEC_BITINTWIDTH (resulttype) % 8)) : 0xff;
  bool maskedtopbyte = (topbytemask != 0xff);

  if (IS_AOP_A (result) || IS_AOP_B (result))
    reg = result->aopu.aop_reg[0];
  else if ((IS_AOP_A (leftOp) || IS_AOP_B (leftOp)) && leftOp->aopu.aop_reg[0]->isDead)
    reg = leftOp->aopu.aop_reg[0];
  else if (mc6800_reg_b->isFree)
    reg = mc6800_reg_b;
  else if (mc6800_reg_a->isFree)
    reg = mc6800_reg_a;
  else
    {
      reg = mc6800_reg_b;
      needpullb = pushRegIfSurv (mc6800_reg_b);
    }
  if (rightOp->type == AOP_REG && rightOp->aopu.aop_reg[0] == reg)
    {
      rightOp = leftOp;
      leftOp = AOP (IC_RIGHT (ic));
    }

  loadRegFromAop (reg, leftOp, 0);
  if (!aopIsLitVal (rightOp, 0, 1, 0x00))
    {
      accopWithAop ("add", reg, rightOp, 0);
      if (maskedtopbyte)
        {
          mc6800_emitOpWithAcc ("and", reg, MODE_IMM, "#0x%02x", topbytemask);
        }
    }
  storeRegToAop (reg, result, 0);
  pullOrFreeReg (mc6800_reg_b, needpullb);
}

static void
genPlus16 (iCode *ic)
{
  int size = getDataSize (IC_RESULT (ic)), offset = 0;
  asmop *leftOp  = AOP (IC_LEFT (ic));
  asmop *rightOp = AOP (IC_RIGHT (ic));
  asmop *result  = AOP (IC_RESULT (ic));
  bool needpullb = pushRegIfSurv (mc6800_reg_b);
  bool needpulla = pushRegIfSurv (mc6800_reg_a);
  bool carry_set = false;
  sym_link *resulttype = operandType (IC_RESULT (ic));
  unsigned topbytemask = (IS_BITINT (resulttype) && SPEC_USIGN (resulttype) && (SPEC_BITINTWIDTH (resulttype) % 8)) ?
    (0xff >> (8 - SPEC_BITINTWIDTH (resulttype) % 8)) : 0xff;
  bool maskedtopbyte = (topbytemask != 0xff);

  if (leftOp->type == AOP_STL || rightOp->type == AOP_STL)
    {
      asmop *stl = (leftOp->type == AOP_STL) ? leftOp : rightOp;
      asmop *other = (leftOp->type == AOP_STL) ? rightOp : leftOp;
      const char *tmp;
      int delta;

      wassertl (other->type != AOP_STL, "both operands on the stack");
      if (other->size > 1)
        {
          loadRegFromAop (mc6800_reg_d, other, 0);
        }
      else
        {
          loadRegFromAop (mc6800_reg_b, other, 0);
          mc6800_emitOp ("clra", MODE_INH, "");
        }
      tmp = allocTemp ();
      delta = 1 + _G.stackOfs + stl->aopu.aop_stk + _G.stackPushes;
      mc6800_emitOp ("sts", MODE_DIR, "*%s", tmp);
      mc6800_emitOp ("addb", MODE_DIR, "*%s+1", tmp);
      mc6800_emitOp ("adca", MODE_DIR, "*%s", tmp);
      mc6800_emitOp ("addb", MODE_IMM, "#%d", delta & 0xff);
      mc6800_emitOp ("adca", MODE_IMM, "#%d", (delta >> 8) & 0xff);
      mc6800_dirtyReg (mc6800_reg_a, false);
      mc6800_dirtyReg (mc6800_reg_b, false);
      if (IS_AOP_X (result))
        {
          mc6800_emitOp ("stab", MODE_DIR, "*%s+1", tmp);
          mc6800_emitOp ("staa", MODE_DIR, "*%s", tmp);
          mc6800_emitOp ("ldx", MODE_DIR, "*%s", tmp);
          mc6800_dirtyReg (mc6800_reg_x, false);
          freeTemp ();
        }
      else
        {
          freeTemp ();
          storeRegToAop (mc6800_reg_d, result, 0);
        }
      pullOrFreeReg (mc6800_reg_a, needpulla);
      pullOrFreeReg (mc6800_reg_b, needpullb);
      return;
    }

  loadRegFromAop (mc6800_reg_b, leftOp, 0);
  loadRegFromAop (mc6800_reg_a, leftOp, 1);
  accopWithAop ("add", mc6800_reg_b, rightOp, 0);
  accopWithAop ("adc", mc6800_reg_a, rightOp, 1);
  storeRegToAop (mc6800_reg_d, result, 0);

  pullOrFreeReg (mc6800_reg_a, needpulla);
  pullOrFreeReg (mc6800_reg_b, needpullb);
}

static void
genPlusMANY (iCode *ic)
{
  asmop *leftOp = AOP (IC_LEFT (ic));
  asmop *rightOp = AOP (IC_RIGHT (ic));
  asmop *result = AOP (IC_RESULT (ic));
  int size = getDataSize (IC_RESULT (ic));
  int offset;
  reg_info *reg;
  bool needpull = false;

  if (mc6800_reg_b->isFree && !IS_AOP_WITH_B (result))
    reg = mc6800_reg_b;
  else if (mc6800_reg_a->isFree && !IS_AOP_WITH_A (result))
    reg = mc6800_reg_a;
  else
    {
      reg = IS_AOP_WITH_B (result) ? mc6800_reg_a : mc6800_reg_b;
      needpull = pushRegIfSurv (reg);
    }

  for (offset = 0; offset < size; offset++)
    {
      loadRegFromAop (reg, leftOp, offset);
      accopWithAop (offset ? "adc" : "add", reg, rightOp, offset);
      storeRegToAop (reg, result, offset);
    }
  pullOrFreeReg (reg, needpull);
}


/*-----------------------------------------------------------------*/
/* genPlus - generates code for addition                           */
/*-----------------------------------------------------------------*/
static void
genPlus (iCode *ic)
{
  int size, offset = 0;
  asmop *leftOp, *rightOp;
  bool earlystore = false;
  bool delayedstore = false;
  bool mayskip = true;
  bool skip = false;

  /* special cases :- */

  D (emitcode (";     genPlus", ""));

  aopOp (IC_LEFT (ic), ic, false);
  aopOp (IC_RIGHT (ic), ic, false);
  aopOp (IC_RESULT (ic), ic, true);

  /* we want registers on the left and literals on the right */
  if ((AOP_TYPE (IC_LEFT (ic)) == AOP_LIT) || (AOP_TYPE (IC_RIGHT (ic)) == AOP_REG && !IS_AOP_WITH_A (AOP (IC_LEFT (ic)))))
    {
      operand *t = IC_RIGHT (ic);
      IC_RIGHT (ic) = IC_LEFT (ic);
      IC_LEFT (ic) = t;
    }

  /* if I can do an increment instead
     of add then GOOD for ME */
  if (genPlusIncr (ic))
    goto release;

  DD (emitcode ("", ";  left size = %d", getDataSize (IC_LEFT (ic))));
  DD (emitcode ("", ";  right size = %d", getDataSize (IC_RIGHT (ic))));
  DD (emitcode ("", ";  result size = %d", getDataSize (IC_RESULT (ic))));

  size = getDataSize (IC_RESULT (ic));

  leftOp = AOP (IC_LEFT (ic));
  rightOp = AOP (IC_RIGHT (ic));

  D (emitcode (";     genPlus", "size = %d",size));
  switch (size)
    {
      case 1: genPlus8(ic);
              break;
      case 2: genPlus16(ic);
              break;
      default:genPlusMANY(ic);
              break;
    }


release:
  freeAsmop (IC_RESULT (ic), NULL, ic, true);
  freeAsmop (IC_RIGHT (ic), NULL, ic, true);
  freeAsmop (IC_LEFT (ic), NULL, ic, true);
}

/*-----------------------------------------------------------------*/
/* genMinusDec :- does subtraction with decrement if possible      */
/*-----------------------------------------------------------------*/
static bool
genMinusDec (iCode * ic)
{
  float cycles;
  int icount;
  operand *left;
  operand *result;
  bool needpula;
  unsigned int size = getDataSize (IC_RESULT (ic));
  symbol *tlbl = NULL;

  D (emitcode (";     genMinusDec", ""));
  left = IC_LEFT (ic);
  result = IC_RESULT (ic);

  if (AOP_TYPE (IC_RIGHT (ic)) != AOP_LIT)
    return false;

  icount = (int) ulFromVal (AOP (IC_RIGHT (ic))->aopu.aop_lit);

  if (IS_AOP_X (AOP (left)) && IS_AOP_X (AOP (result)))
    {
      icount = (short) icount;
      if (abs (icount) > ((optimize.codeSize && !optimize.codeSpeed) ? 15 : 6))
        return false;
      addConstToX (-icount);
      return true;
    }

  if ((icount > 255) || (icount < 1))
    return false;

  if (!sameRegs (AOP (left), AOP (result)))
    return false;

  if (AOP_TYPE (result) == AOP_REG)
    {
      if (size != 1 || icount != 1 || !(IS_AOP_A (AOP (result)) || IS_AOP_B (AOP (result))))
        return false;
      rmwWithAop ("dec", AOP (result), 0);
      return true;
    }

  if (size == 2 && icount <= ((optimize.codeSize && !optimize.codeSpeed) ? 15 : 6)
      && (AOP_TYPE (result) == AOP_DIR || AOP_TYPE (result) == AOP_EXT)
      && mc6800_reg_x->aop && sameRegs (mc6800_reg_x->aop, AOP (result)) && mc6800_reg_x->aopofs == 0
      && mc6800_reg_x->isFree && mc6800_reg_x->isDead)
    {
      addConstToX (-icount);
      storeRegToAop (mc6800_reg_x, AOP (result), 0);
      return true;
    }

  if (size > 2)
    return false;

  if (size > 1)
    tlbl = regalloc_dry_run ? 0 : newiTempLabel (NULL);

  if (icount == 1)
    {
      if (size > 1)
        {
          if (isOperandVolatile (result, false))
            return false;
          rmwWithAop ("tst", AOP (result), 0);
          emitBranch ("bne", tlbl);
          cycles = regalloc_dry_run_cost_cycles;
          rmwWithAop ("dec", AOP (result), 1);
          regalloc_dry_run_cost_cycles = cycles;
          if (!regalloc_dry_run)
            mc6800_emitLabel (tlbl);
        }
      rmwWithAop ("dec", AOP (result), 0);
      return true;
    }

  needpula = pushRegIfUsed (mc6800_reg_a);
  loadRegFromAop (mc6800_reg_a, AOP (result), 0);
  accopWithAop ("sub", mc6800_reg_a, AOP (IC_RIGHT (ic)), 0);
  storeRegToAop (mc6800_reg_a, AOP (result), 0);
  if (size > 1)
    {
      emitBranch ("bcc", tlbl);
      cycles = regalloc_dry_run_cost_cycles;
      rmwWithAop ("dec", AOP (result), 1);
      regalloc_dry_run_cost_cycles = cycles;
      if (!regalloc_dry_run)
        mc6800_emitLabel (tlbl);
    }
  pullOrFreeReg (mc6800_reg_a, needpula);
  return true;
}

/*-----------------------------------------------------------------*/
/* addSign - complete with sign                                    */
/*-----------------------------------------------------------------*/
static void
addSign (operand * result, int offset, int sign)
{
  int size = (getDataSize (result) - offset);
  if (size > 0)
    {
      if (sign)
        {
          mc6800_emitOp ("rola", MODE_INH, "");
          mc6800_emitOp ("ldaa", MODE_IMM, "%s", zero);
          mc6800_emitOp ("sbca", MODE_IMM, "%s", zero);
          mc6800_dirtyReg (mc6800_reg_a, false);
          while (size--)
            storeRegToAop (mc6800_reg_a, AOP (result), offset++);
        }
      else
        while (size--)
          storeConstToAop (0, AOP (result), offset++);
    }
}


/*-----------------------------------------------------------------*/
/* genMinus8 - generates code for 8-bit subtraction                */
/*-----------------------------------------------------------------*/
static void
genMinus8 (iCode *ic)
{
  asmop *leftOp  = AOP (IC_LEFT (ic));
  asmop *rightOp = AOP (IC_RIGHT (ic));
  asmop *result  = AOP (IC_RESULT (ic));
  reg_info *reg;
  bool needpulla = false;
  bool needpullb = false;
  sym_link *resulttype = operandType (IC_RESULT (ic));
  unsigned topbytemask = (IS_BITINT (resulttype) && SPEC_USIGN (resulttype) && (SPEC_BITINTWIDTH (resulttype) % 8)) ?
    (0xff >> (8 - SPEC_BITINTWIDTH (resulttype) % 8)) : 0xff;
  bool maskedtopbyte = (topbytemask != 0xff);

  if (IS_AOP_A (rightOp) || IS_AOP_B (rightOp))
    {
      if (IS_AOP_A (rightOp) && IS_AOP_B (leftOp))
        {
          needpulla = pushRegIfSurv (mc6800_reg_a);
          mc6800_emitOp ("nega", MODE_INH, "");
          mc6800_emitOp ("aba", MODE_INH, "");
          mc6800_dirtyReg (mc6800_reg_a, false);
          if (maskedtopbyte)
            {
              mc6800_emitOp ("anda", MODE_IMM, "#0x%02x", topbytemask);
            }
          storeRegToAop (mc6800_reg_a, result, 0);
          pullOrFreeReg (mc6800_reg_a, needpulla);
          pullOrFreeReg (mc6800_reg_b, needpullb);
          return;
        }
      if (IS_AOP_A (rightOp))
        {
          needpullb = pushRegIfSurv (mc6800_reg_b);
          transferRegReg (mc6800_reg_a, mc6800_reg_b, false);
        }
      needpulla = pushRegIfSurv (mc6800_reg_a);
      loadRegFromAop (mc6800_reg_a, leftOp, 0);
      mc6800_emitOp ("sba", MODE_INH, "");
      mc6800_dirtyReg (mc6800_reg_a, false);
      if (maskedtopbyte)
        {
          mc6800_emitOp ("anda", MODE_IMM, "#0x%02x", topbytemask);
        }
      storeRegToAop (mc6800_reg_a, result, 0);
      pullOrFreeReg (mc6800_reg_a, needpulla);
      pullOrFreeReg (mc6800_reg_b, needpullb);
      return;
    }

  if (IS_AOP_A (result) || IS_AOP_B (result))
    reg = result->aopu.aop_reg[0];
  else if ((IS_AOP_A (leftOp) || IS_AOP_B (leftOp)) && leftOp->aopu.aop_reg[0]->isDead)
    reg = leftOp->aopu.aop_reg[0];
  else if (mc6800_reg_b->isFree)
    reg = mc6800_reg_b;
  else if (mc6800_reg_a->isFree)
    reg = mc6800_reg_a;
  else
    {
      reg = mc6800_reg_b;
      needpullb = pushRegIfSurv (mc6800_reg_b);
    }

  loadRegFromAop (reg, leftOp, 0);
  if (!aopIsLitVal (rightOp, 0, 1, 0x00))
    {
      accopWithAop ("sub", reg, rightOp, 0);
      if (maskedtopbyte)
        {
          mc6800_emitOpWithAcc ("and", reg, MODE_IMM, "#0x%02x", topbytemask);
        }
    }
  storeRegToAop (reg, result, 0);
  pullOrFreeReg (mc6800_reg_b, needpullb);
}

static void
genMinus16 (iCode *ic)
{
  asmop *leftOp  = AOP (IC_LEFT (ic));
  asmop *rightOp = AOP (IC_RIGHT (ic));
  asmop *result  = AOP (IC_RESULT (ic));
  bool needpullb = pushRegIfSurv (mc6800_reg_b);
  bool needpulla = pushRegIfSurv (mc6800_reg_a);

  if (rightOp->type == AOP_STL && leftOp->type == AOP_STL)
    {
      loadRegFromConst (mc6800_reg_d, leftOp->aopu.aop_stk - rightOp->aopu.aop_stk);
    }
  else if (rightOp->type == AOP_STL)
    {
      const char *tmp;
      int delta;

      loadRegFromAop (mc6800_reg_b, leftOp, 0);
      loadRegFromAop (mc6800_reg_a, leftOp, 1);
      tmp = allocTemp ();
      delta = 1 + _G.stackOfs + rightOp->aopu.aop_stk + _G.stackPushes;
      mc6800_emitOp ("sts", MODE_DIR, "*%s", tmp);
      mc6800_emitOp ("subb", MODE_DIR, "*%s+1", tmp);
      mc6800_emitOp ("sbca", MODE_DIR, "*%s", tmp);
      mc6800_emitOp ("subb", MODE_IMM, "#%d", delta & 0xff);
      mc6800_emitOp ("sbca", MODE_IMM, "#%d", (delta >> 8) & 0xff);
      freeTemp ();
    }
  else if (leftOp->type == AOP_STL && IS_AOP_D (rightOp))
    {
      const char *tmp;
      int delta;

      tmp = allocTemp ();
      delta = 1 + _G.stackOfs + leftOp->aopu.aop_stk + _G.stackPushes;
      mc6800_emitOp ("sts", MODE_DIR, "*%s", tmp);
      mc6800_emitOp ("coma", MODE_INH, "");
      mc6800_emitOp ("comb", MODE_INH, "");
      mc6800_emitOp ("adcb", MODE_DIR, "*%s+1", tmp);
      mc6800_emitOp ("adca", MODE_DIR, "*%s", tmp);
      mc6800_emitOp ("addb", MODE_IMM, "#%d", delta & 0xff);
      mc6800_emitOp ("adca", MODE_IMM, "#%d", (delta >> 8) & 0xff);
      freeTemp ();
    }
  else if (leftOp->type == AOP_STL)
    {
      loadRegFromAop (mc6800_reg_d, leftOp, 0);
      accopWithAop ("sub", mc6800_reg_b, rightOp, 0);
      accopWithAop ("sbc", mc6800_reg_a, rightOp, 1);
    }
  else if (IS_AOP_D (rightOp))
    {
      mc6800_emitOp ("coma", MODE_INH, "");
      mc6800_emitOp ("comb", MODE_INH, "");
      accopWithAop ("adc", mc6800_reg_b, leftOp, 0);
      accopWithAop ("adc", mc6800_reg_a, leftOp, 1);
    }
  else
    {
      loadRegFromAop (mc6800_reg_b, leftOp, 0);
      loadRegFromAop (mc6800_reg_a, leftOp, 1);
      accopWithAop ("sub", mc6800_reg_b, rightOp, 0);
      accopWithAop ("sbc", mc6800_reg_a, rightOp, 1);
    }
  storeRegToAop (mc6800_reg_d, result, 0);

  pullOrFreeReg (mc6800_reg_a, needpulla);
  pullOrFreeReg (mc6800_reg_b, needpullb);
}

static void
genMinusMANY (iCode *ic)
{
  asmop *result = AOP (IC_RESULT (ic));
  int size = getDataSize (IC_RESULT (ic));
  int offset;
  reg_info *reg;
  bool needpull = false;

  if (mc6800_reg_b->isFree && !IS_AOP_WITH_B (result))
    reg = mc6800_reg_b;
  else if (mc6800_reg_a->isFree && !IS_AOP_WITH_A (result))
    reg = mc6800_reg_a;
  else
    {
      reg = IS_AOP_WITH_B (result) ? mc6800_reg_a : mc6800_reg_b;
      needpull = pushRegIfSurv (reg);
    }

  for (offset = 0; offset < size; offset++)
    {
      loadRegFromAop (reg, AOP (IC_LEFT (ic)), offset);
      accopWithAop (offset ? "sbc" : "sub", reg, AOP (IC_RIGHT (ic)), offset);
      storeRegToAop (reg, result, offset);
    }
  pullOrFreeReg (reg, needpull);
}

/*-----------------------------------------------------------------*/
static void
genMinus (iCode * ic)
{
  int size;

  sym_link *resulttype = operandType (IC_RESULT (ic));
  unsigned topbytemask = (IS_BITINT (resulttype) && SPEC_USIGN (resulttype) && (SPEC_BITINTWIDTH (resulttype) % 8)) ?
    (0xff >> (8 - SPEC_BITINTWIDTH (resulttype) % 8)) : 0xff;
  bool maskedtopbyte = (topbytemask != 0xff);

  D (emitcode (";     genMinus", ""));

  aopOp (IC_LEFT (ic), ic, false);
  aopOp (IC_RIGHT (ic), ic, false);
  aopOp (IC_RESULT (ic), ic, true);

  /* special cases :- */
  /* if I can do an decrement instead
     of subtract then GOOD for ME */
  if (!maskedtopbyte && genMinusDec (ic))
    goto release;

  size = getDataSize (IC_RESULT (ic));

  switch (size)
    {
    case 1:
      genMinus8 (ic);
      break;
    case 2:
      genMinus16 (ic);
      break;
    default:
      genMinusMANY (ic);
      break;
    }

release:
  freeAsmop (IC_LEFT (ic), NULL, ic, true);
  freeAsmop (IC_RIGHT (ic), NULL, ic, true);
  freeAsmop (IC_RESULT (ic), NULL, ic, true);
}



/*-----------------------------------------------------------------*/
/* genMultOneByte : 8*8=8/16 bit multiplication                    */
/*-----------------------------------------------------------------*/
static void
genMultOneByte (operand * left, operand * right, operand * result)
{
  /* sym_link *opetype = operandType (result); */
  symbol *tlbl1, *tlbl2, *tlbl3, *tlbl4;
  int size = AOP_SIZE (result);
  bool negLiteral = false;
  bool lUnsigned, rUnsigned;
  bool needpulla, needpullx;

  D (emitcode (";     genMultOneByte", ""));

#if 0
  if (size < 1 || size > 2)
    {
      // this should never happen
      fprintf (stderr, "size!=1||2 (%d) in %s at line:%d \n", AOP_SIZE (result), __FILE__, lineno);
      exit (1);
    }

  /* (if two literals: the value is computed before) */
  /* if one literal, literal on the right */
  if (AOP_TYPE (left) == AOP_LIT)
    {
      operand *t = right;
      right = left;
      left = t;
    }
  /* if an operand is in A, make sure it is on the left */
  if (IS_AOP_A (AOP (right)))
    {
      operand *t = right;
      right = left;
      left = t;
    }

  needpulla = pushRegIfSurv (mc6800_reg_a);
  needpullx = pushRegIfSurv (mc6800_reg_x);

  lUnsigned = SPEC_USIGN (getSpec (operandType (left)));
  rUnsigned = SPEC_USIGN (getSpec (operandType (right)));

  /* lUnsigned  rUnsigned  negLiteral  negate     case */
  /* false      false      false       odd        3    */
  /* false      false      true        even       3    */
  /* false      true       false       odd        3    */
  /* false      true       true        impossible      */
  /* true       false      false       odd        3    */
  /* true       false      true        always     2    */
  /* true       true       false       never      1    */
  /* true       true       true        impossible      */

  /* case 1 */
  if (size == 1 || (lUnsigned && rUnsigned))
    {
      // just an unsigned 8*8=8/16 multiply
      //DD(emitcode (";","unsigned"));

      loadRegFromAop (mc6800_reg_a, AOP (left), 0);
      loadRegFromAop (mc6800_reg_x, AOP (right), 0);
      emitcode ("mul", "");
      regalloc_dry_run_cost++;
      mc6800_dirtyReg (mc6800_reg_xa, false);
      storeRegToFullAop (mc6800_reg_xa, AOP (result), true);
      mc6800_freeReg (mc6800_reg_xa);
      pullOrFreeReg (mc6800_reg_x, needpullx);
      pullOrFreeReg (mc6800_reg_a, needpulla);

      return;
    }

  // we have to do a signed multiply

  /* case 2 */
  /* left unsigned, right signed literal -- literal determines sign handling */
  if (AOP_TYPE (right) == AOP_LIT && lUnsigned && !rUnsigned)
    {
      signed char val = (signed char) ulFromVal (AOP (right)->aopu.aop_lit);

      loadRegFromAop (mc6800_reg_a, AOP (left), 0);
      emitcode ("ldx", "#0x%02x", val < 0 ? -val : val);
      regalloc_dry_run_cost += 2;
      mc6800_dirtyReg (mc6800_reg_x, false);

      emitcode ("mul", "");
      regalloc_dry_run_cost++;
      mc6800_dirtyReg (mc6800_reg_xa, false);

      if (val < 0)
        {
          rmwWithReg ("neg", mc6800_reg_a);
          tlbl4 = (regalloc_dry_run ? 0 : newiTempLabel (NULL));
          emitBranch ("bcc", tlbl4);
          rmwWithReg ("inc", mc6800_reg_x);
          if (!regalloc_dry_run)
            mc6800_emitLabel (tlbl4);
          rmwWithReg ("neg", mc6800_reg_x);
        }

      storeRegToFullAop (mc6800_reg_xa, AOP (result), true);
      mc6800_freeReg (mc6800_reg_xa);
      pullOrFreeReg (mc6800_reg_x, needpullx);
      pullOrFreeReg (mc6800_reg_a, needpulla);
      return;
    }


  /* case 3 */
  adjustStack (-1);
  emitcode ("clr", "1,s");
  regalloc_dry_run_cost += 3;

  loadRegFromAop (mc6800_reg_a, AOP (left), 0);
  if (!lUnsigned)
    {
      tlbl1 = (regalloc_dry_run ? 0 : newiTempLabel (NULL));
      mc6800_emitOp ("tsta", MODE_INH, "");
      emitBranch ("bpl", tlbl1);
      emitcode ("inc", "1,s");
      regalloc_dry_run_cost += 3;
      rmwWithReg ("neg", mc6800_reg_a);
      regalloc_dry_run_cost++;
      if (!regalloc_dry_run)
        mc6800_emitLabel (tlbl1);
    }

  if (AOP_TYPE (right) == AOP_LIT && !rUnsigned)
    {
      signed char val = (signed char) ulFromVal (AOP (right)->aopu.aop_lit);
      /* AND literal negative */
      if (val < 0)
        {
          emitcode ("ldx", "#0x%02x", -val);
          regalloc_dry_run_cost += 2;
          negLiteral = true;
        }
      else
        {
          emitcode ("ldx", "#0x%02x", val);
          regalloc_dry_run_cost += 2;
        }
      mc6800_dirtyReg (mc6800_reg_x, false);
      mc6800_useReg (mc6800_reg_x);
    }
  else
    {
      loadRegFromAop (mc6800_reg_x, AOP (right), 0);
      if (!rUnsigned)
        {
          tlbl2 = (regalloc_dry_run ? 0 : newiTempLabel (NULL));
          emitcode ("tstx", "");
          regalloc_dry_run_cost++;
          emitBranch ("bpl", tlbl2);
          emitcode ("inc", "1,s");
          regalloc_dry_run_cost += 3;
          rmwWithReg ("neg", mc6800_reg_x);
          regalloc_dry_run_cost++;
          if (!regalloc_dry_run)
            mc6800_emitLabel (tlbl2);
        }
    }

  emitcode ("mul", "");
  regalloc_dry_run_cost++;
  mc6800_dirtyReg (mc6800_reg_xa, false);

  tlbl3 = (regalloc_dry_run ? 0 : newiTempLabel (NULL));
  emitcode ("dec", "1,s");
  regalloc_dry_run_cost += 3;
  if (!lUnsigned && !rUnsigned && negLiteral)
    emitBranch ("beq", tlbl3);
  else
    emitBranch ("bne", tlbl3);

  rmwWithReg ("neg", mc6800_reg_a);
  tlbl4 = (regalloc_dry_run ? 0 : newiTempLabel (NULL));
  emitBranch ("bcc", tlbl4);
  rmwWithReg ("inc", mc6800_reg_x);
  if (!regalloc_dry_run)
    mc6800_emitLabel (tlbl4);
  rmwWithReg ("neg", mc6800_reg_x);

  if (!regalloc_dry_run)
    mc6800_emitLabel (tlbl3);
  adjustStack (1);
  storeRegToFullAop (mc6800_reg_xa, AOP (result), true);
#endif
  pullOrFreeReg (mc6800_reg_x, needpullx);
  pullOrFreeReg (mc6800_reg_a, needpulla);
}

/*-----------------------------------------------------------------*/
/* genMult - generates code for multiplication                     */
/*-----------------------------------------------------------------*/
static void
genMult (iCode * ic)
{
  operand *left = IC_LEFT (ic);
  operand *right = IC_RIGHT (ic);
  operand *result = IC_RESULT (ic);

  D (emitcode (";     genMult", ""));

#if 0
  /* assign the amsops */
  aopOp (left, ic, false);
  aopOp (right, ic, false);
  aopOp (result, ic, true);

  /* special cases first */
  /* if both are of size == 1 */
//  if (getSize(operandType(left)) == 1 &&
//      getSize(operandType(right)) == 1)
  if (AOP_SIZE (left) == 1 && AOP_SIZE (right) == 1)
    {
      genMultOneByte (left, right, result);
      goto release;
    }

  /* should have been converted to function call */
  fprintf (stderr, "left: %d right: %d\n", getSize (OP_SYMBOL (left)->type), getSize (OP_SYMBOL (right)->type));
  fprintf (stderr, "left: %d right: %d\n", AOP_SIZE (left), AOP_SIZE (right));
  assert (0);
#endif
release:
  freeAsmop (left, NULL, ic, true);
  freeAsmop (right, NULL, ic, true);
  freeAsmop (result, NULL, ic, true);
}

/*-----------------------------------------------------------------*/
/* genDivOneByte : 8 bit division                                  */
/*-----------------------------------------------------------------*/
static void
genDivOneByte (operand * left, operand * right, operand * result)
{
  symbol *tlbl1, *tlbl2, *tlbl3;
  int size;
  bool lUnsigned, rUnsigned;
  bool runtimeSign, compiletimeSign;
  bool needpulla, needpullh;
  bool needpullx = false;
  bool preload_a = false;

  lUnsigned = SPEC_USIGN (getSpec (operandType (left)));
  rUnsigned = SPEC_USIGN (getSpec (operandType (right)));

  D (emitcode (";     genDivOneByte", ""));
#if 0
  needpulla = pushRegIfSurv (mc6800_reg_a);
  needpullh = pushRegIfSurv (mc6800_reg_h);
  needpullx = pushRegIfSurv (mc6800_reg_x);

  /* If both left and right are in registers and backwards from what we need, */
  /* the swap the operands and the registers. */
  if (IS_AOP_A (AOP (right)) && IS_AOP_X (AOP (left)))
    {
      operand * t;
      t = left;
      left = right;
      right = t;
      pushReg (mc6800_reg_a, false);
      transferRegReg (mc6800_reg_x, mc6800_reg_a, false);
      pullReg (mc6800_reg_x);
    }

  size = AOP_SIZE (result);
  /* signed or unsigned */
  if (lUnsigned && rUnsigned)
    {
      /* unsigned is easy */
      if (IS_AOP_A (AOP (right)))
        {
          loadRegFromAop (mc6800_reg_x, AOP (right), 0);
          loadRegFromAop (mc6800_reg_a, AOP (left), 0);
        }
      else
        {
          loadRegFromAop (mc6800_reg_a, AOP (left), 0);
          loadRegFromAop (mc6800_reg_x, AOP (right), 0);
        }
      loadRegFromConst (mc6800_reg_h, 0);
      emitcode ("div", "");
      regalloc_dry_run_cost++;
      mc6800_dirtyReg (mc6800_reg_a, false);
      mc6800_dirtyReg (mc6800_reg_h, false);
      storeRegToFullAop (mc6800_reg_a, AOP (result), false);
      pullOrFreeReg (mc6800_reg_x, needpullx);
      pullOrFreeReg (mc6800_reg_h, needpullh);
      pullOrFreeReg (mc6800_reg_a, needpulla);
      return;
    }

  /* signed is a little bit more difficult */

  /* now sign adjust for both left & right */

  /* let's see what's needed: */
  /* apply negative sign during runtime */
  runtimeSign = false;
  /* negative sign from literals */
  compiletimeSign = false;

  if (!lUnsigned)
    {
      if (AOP_TYPE (left) == AOP_LIT)
        {
          /* signed literal */
          signed char val = (char) ulFromVal (AOP (left)->aopu.aop_lit);
          if (val < 0)
            compiletimeSign = true;
        }
      else
        /* signed but not literal */
        runtimeSign = true;
    }

  if (!rUnsigned)
    {
      if (AOP_TYPE (right) == AOP_LIT)
        {
          /* signed literal */
          signed char val = (char) ulFromVal (AOP (right)->aopu.aop_lit);
          if (val < 0)
            compiletimeSign ^= true;
        }
      else
        /* signed but not literal */
        runtimeSign = true;
    }

  /* initialize the runtime sign */
  if (runtimeSign)
    {
      if (compiletimeSign)
        pushConst (1);    /* set sign flag */
      else
        pushConst (0);    /* reset sign flag */
    }

  if (IS_AOP_X (AOP (left)))
    {
      loadRegFromAop (mc6800_reg_a, AOP (left), 0);
      preload_a = true;
    }

  /* save the signs of the operands */
  if (AOP_TYPE (right) == AOP_LIT)
    {
      signed char val = (char) ulFromVal (AOP (right)->aopu.aop_lit);

      if (!rUnsigned && val < 0)
        {
          emitcode ("ldx", "#0x%02x", -val);
          regalloc_dry_run_cost += 2;
        }
      else
        {
          emitcode ("ldx", "#0x%02x", (unsigned char) val);
          regalloc_dry_run_cost += 2;
        }
      mc6800_dirtyReg (mc6800_reg_x, false);
      mc6800_useReg (mc6800_reg_x);
    }
  else                          /* ! literal */
    {
      loadRegFromAop (mc6800_reg_x, AOP (right), 0);
      if (!rUnsigned)
        {
          tlbl1 = (regalloc_dry_run ? 0 : newiTempLabel (NULL));
          emitcode ("tstx", "");
          regalloc_dry_run_cost++;
          emitBranch ("bpl", tlbl1);
          emitcode ("inc", "1,s");
          regalloc_dry_run_cost += 3;
          rmwWithReg ("neg", mc6800_reg_x);
          if (!regalloc_dry_run)
            mc6800_emitLabel (tlbl1);
        }
    }

  if (AOP_TYPE (left) == AOP_LIT)
    {
      signed char val = (char) ulFromVal (AOP (left)->aopu.aop_lit);

      if (!lUnsigned && val < 0)
        {
          emitcode ("lda", "#0x%02x", -val);
          regalloc_dry_run_cost += 2;
        }
      else
        {
          emitcode ("lda", "#0x%02x", (unsigned char) val);
          regalloc_dry_run_cost += 2;
        }
      mc6800_dirtyReg (mc6800_reg_a, false);
      mc6800_useReg (mc6800_reg_a);
    }
  else                          /* ! literal */
    {
      if (!preload_a)
        loadRegFromAop (mc6800_reg_a, AOP (left), 0);
      if (!lUnsigned)
        {
          tlbl2 = (regalloc_dry_run ? 0 : newiTempLabel (NULL));
          mc6800_emitOp ("tsta", MODE_INH, "");
          emitBranch ("bpl", tlbl2);
          emitcode ("inc", "1,s");
          regalloc_dry_run_cost += 3;
          rmwWithReg ("neg", mc6800_reg_a);
          if (!regalloc_dry_run)
            mc6800_emitLabel (tlbl2);
        }
    }

  loadRegFromConst (mc6800_reg_h, 0);
  emitcode ("div", "");
  regalloc_dry_run_cost++;
  mc6800_dirtyReg (mc6800_reg_x, false);
  mc6800_dirtyReg (mc6800_reg_a, false);
  mc6800_dirtyReg (mc6800_reg_h, false);

  if (runtimeSign || compiletimeSign)
    {
      if (runtimeSign)
        {
          tlbl3 = (regalloc_dry_run ? 0 : newiTempLabel (NULL));
          pullReg (mc6800_reg_x);
          rmwWithReg ("lsr", mc6800_reg_x);
          if (size > 1)
            loadRegFromConst (mc6800_reg_x, 0);
          emitBranch ("bcc", tlbl3);
          rmwWithReg ("neg", mc6800_reg_a);
          if (size > 1)
            rmwWithReg ("dec", mc6800_reg_x);
          if (!regalloc_dry_run)
            mc6800_emitLabel (tlbl3);
          /* signed result now in A or XA */
          if (size == 1)
            storeRegToAop (mc6800_reg_a, AOP (result), 0);
          else
            storeRegToAop (mc6800_reg_xa, AOP (result), 0);
        }
      else /* must be compiletimeSign */
        {
          mc6800_freeReg (mc6800_reg_x); /* in case we need a free reg for the 0xff */
          rmwWithReg ("neg", mc6800_reg_a);
          storeRegToAop (mc6800_reg_a, AOP (result), 0);
          if (size > 1)
            storeConstToAop (0xff, AOP (result), 1);
        }
    }
  else
    {
      storeRegToFullAop (mc6800_reg_a, AOP (result), false);
    }
  pullOrFreeReg (mc6800_reg_x, needpullx);
  pullOrFreeReg (mc6800_reg_b, needpullb);
  pullOrFreeReg (mc6800_reg_a, needpulla);
#endif
}

/*-----------------------------------------------------------------*/
/* genDiv - generates code for division                            */
/*-----------------------------------------------------------------*/
static void
genDiv (iCode * ic)
{
  operand *left = IC_LEFT (ic);
  operand *right = IC_RIGHT (ic);
  operand *result = IC_RESULT (ic);

  D (emitcode (";     genDiv", ""));

#if 0
  /* assign the amsops */
  aopOp (left, ic, false);
  aopOp (right, ic, false);
  aopOp (result, ic, true);

  /* special cases first */
  /* if both are of size == 1 */
  if (AOP_SIZE (left) == 1 && AOP_SIZE (right) == 1 && AOP_SIZE (result) <= 2)
    {
      genDivOneByte (left, right, result);
      goto release;
    }
#endif
  /* should have been converted to function call */
  assert (0);
release:
  freeAsmop (left, NULL, ic, true);
  freeAsmop (right, NULL, ic, true);
  freeAsmop (result, NULL, ic, true);
}

/*-----------------------------------------------------------------*/
/* genModOneByte : 8 bit modulus                                   */
/*-----------------------------------------------------------------*/
static void
genModOneByte (operand * left, operand * right, operand * result)
{
  symbol *tlbl1, *tlbl2, *tlbl3;
  int size;
  bool lUnsigned, rUnsigned;
  bool runtimeSign, compiletimeSign;
  bool needpulla, needpullh;
  bool needpullx = false;
  bool preload_a = false;

  lUnsigned = SPEC_USIGN (getSpec (operandType (left)));
  rUnsigned = SPEC_USIGN (getSpec (operandType (right)));

  D (emitcode (";     genModOneByte", ""));

#if 0
  size = AOP_SIZE (result);

  needpulla = pushRegIfSurv (mc6800_reg_a);
  needpullb = pushRegIfSurv (mc6800_reg_b);
  needpullx = pushRegIfSurv (mc6800_reg_x);

  /* If both left and right are in registers and backwards from what we need, */
  /* the swap the operands and the registers. */
  if (IS_AOP_A (AOP (right)) && IS_AOP_X (AOP (left)))
    {
      operand * t;
      t = left;
      left = right;
      right = t;
      pushReg (mc6800_reg_a, false);
      transferRegReg (mc6800_reg_x, mc6800_reg_a, false);
      pullReg (mc6800_reg_x);
    }

  if (lUnsigned && rUnsigned)
    {
      /* unsigned is easy */
      if (IS_AOP_A (AOP (right)))
        {
          loadRegFromAop (mc6800_reg_x, AOP (right), 0);
          loadRegFromAop (mc6800_reg_a, AOP (left), 0);
        }
      else
        {
          loadRegFromAop (mc6800_reg_a, AOP (left), 0);
          loadRegFromAop (mc6800_reg_x, AOP (right), 0);
        }
      loadRegFromConst (mc6800_reg_h, 0);
      emitcode ("div", "");
      regalloc_dry_run_cost++;
      mc6800_freeReg (mc6800_reg_x);
      mc6800_dirtyReg (mc6800_reg_a, true);
      mc6800_dirtyReg (mc6800_reg_h, false);
      storeRegToFullAop (mc6800_reg_h, AOP (result), false);
      pullOrFreeReg (mc6800_reg_x, needpullx);
      pullOrFreeReg (mc6800_reg_b, needpullb);
      pullOrFreeReg (mc6800_reg_a, needpulla);
      return;
    }

  /* signed is a little bit more difficult */
  if (IS_AOP_X (AOP (left)))
    {
      loadRegFromAop (mc6800_reg_a, AOP (left), 0);
      preload_a = true;
    }

  if (AOP_TYPE (right) == AOP_LIT)
    {
      signed char val = (char) ulFromVal (AOP (right)->aopu.aop_lit);

      if (!rUnsigned && val < 0)
        {
          emitcode ("ldx", "#0x%02x", -val);
          regalloc_dry_run_cost += 2;
        }
      else
        {
          emitcode ("ldx", "#0x%02x", (unsigned char) val);
          regalloc_dry_run_cost += 2;
        }
      mc6800_dirtyReg (mc6800_reg_x, false);
      mc6800_useReg (mc6800_reg_x);
    }
  else                          /* ! literal */
    {
      loadRegFromAop (mc6800_reg_x, AOP (right), 0);
      if (!rUnsigned)
        {
          tlbl1 = (regalloc_dry_run ? 0 : newiTempLabel (NULL));
          emitcode ("tstx", "");
          regalloc_dry_run_cost++;
          emitBranch ("bpl", tlbl1);
          rmwWithReg ("neg", mc6800_reg_x);
          if (!regalloc_dry_run)
            mc6800_emitLabel (tlbl1);
        }
    }

  /* let's see what's needed: */
  /* apply negative sign during runtime */
  runtimeSign = false;
  /* negative sign from literals */
  compiletimeSign = false;

  /* sign adjust left side */
  if (AOP_TYPE (left) == AOP_LIT)
    {
      signed char val = (char) ulFromVal (AOP (left)->aopu.aop_lit);

      if (!lUnsigned && val < 0)
        {
          compiletimeSign = true;       /* set sign flag */
          emitcode ("lda", "#0x%02x", -val);
          regalloc_dry_run_cost += 2;
        }
      else
        {
          emitcode ("lda", "#0x%02x", (unsigned char) val);
          regalloc_dry_run_cost += 2;
        }
      mc6800_dirtyReg (mc6800_reg_a, false);
      mc6800_useReg (mc6800_reg_a);
    }
  else                          /* ! literal */
    {
      if (lUnsigned)
        {
          if (!preload_a)
            loadRegFromAop (mc6800_reg_a, AOP (left), 0);
        }
      else
        {
          runtimeSign = true;
          pushConst (0);

          if (!preload_a)
            loadRegFromAop (mc6800_reg_a, AOP (left), 0);
          tlbl2 = (regalloc_dry_run ? 0 : newiTempLabel (NULL));
          mc6800_emitOp ("tsta", MODE_INH, "");
          emitBranch ("bpl", tlbl2);
          emitcode ("inc", "1,s");
          regalloc_dry_run_cost += 3;
          rmwWithReg ("neg", mc6800_reg_a);
          if (!regalloc_dry_run)
            mc6800_emitLabel (tlbl2);
        }
    }

  loadRegFromConst (mc6800_reg_h, 0);
  emitcode ("div", "");
  regalloc_dry_run_cost++;
  mc6800_freeReg (mc6800_reg_x);
  mc6800_dirtyReg (mc6800_reg_a, true);
  mc6800_dirtyReg (mc6800_reg_h, false);

  if (runtimeSign || compiletimeSign)
    {
      transferRegReg (mc6800_reg_h, mc6800_reg_a, true);
      if (runtimeSign)
        {
          tlbl3 = (regalloc_dry_run ? 0 : newiTempLabel (NULL));
          pullReg (mc6800_reg_x);
          rmwWithReg ("lsr", mc6800_reg_x);
          if (size > 1)
            loadRegFromConst (mc6800_reg_x, 0);
          emitBranch ("bcc", tlbl3);
          rmwWithReg ("neg", mc6800_reg_a);
          if (size > 1)
            rmwWithReg ("dec", mc6800_reg_x);
          if (!regalloc_dry_run)
            mc6800_emitLabel (tlbl3);
          /* signed result now in A or XA */
          if (size == 1)
            storeRegToAop (mc6800_reg_a, AOP (result), 0);
          else
            storeRegToAop (mc6800_reg_xa, AOP (result), 0);
        }
      else /* must be compiletimeSign */
        {
          mc6800_freeReg (mc6800_reg_x); /* in case we need a free reg for the 0xff */
          rmwWithReg ("neg", mc6800_reg_a);
          storeRegToAop (mc6800_reg_a, AOP (result), 0);
          if (size > 1)
            storeConstToAop (0xff, AOP (result), 1);
        }
    }
  else
    {
      storeRegToFullAop (mc6800_reg_h, AOP (result), false);
    }

  pullOrFreeReg (mc6800_reg_x, needpullx);
  pullOrFreeReg (mc6800_reg_b, needpullb);
  pullOrFreeReg (mc6800_reg_a, needpulla);
#endif
}

/*-----------------------------------------------------------------*/
/* genMod - generates code for modulus                             */
/*-----------------------------------------------------------------*/
static void
genMod (iCode * ic)
{
  operand *left = IC_LEFT (ic);
  operand *right = IC_RIGHT (ic);
  operand *result = IC_RESULT (ic);

  D (emitcode (";     genMod", ""));

  /* assign the amsops */
  aopOp (left, ic, false);
  aopOp (right, ic, false);
  aopOp (result, ic, true);

  /* special cases first */
  /* if both are of size == 1 */
  if (AOP_SIZE (left) == 1 && AOP_SIZE (right) == 1 && AOP_SIZE (result) <= 2)
    {
      genModOneByte (left, right, result);
      goto release;
    }

  /* should have been converted to function call */
  assert (0);

release:
  freeAsmop (left, NULL, ic, true);
  freeAsmop (right, NULL, ic, true);
  freeAsmop (result, NULL, ic, true);
}

/*-----------------------------------------------------------------*/
/* genIfxJump :- will create a jump depending on the ifx           */
/*-----------------------------------------------------------------*/
static void
genIfxJump (iCode * ic, char *jval)
{
  symbol *jlbl;
  symbol *tlbl = (regalloc_dry_run ? 0 : newiTempLabel (NULL));
  char *inst;

  D (emitcode (";     genIfxJump", ""));

  /* if true label then we jump if condition
     supplied is true */
  if (IC_TRUE (ic))
    {
      jlbl = IC_TRUE (ic);
      if (!strcmp (jval, "a"))
        inst = "beq";
      else if (!strcmp (jval, "c"))
        inst = "bcc";
      else if (!strcmp (jval, "n"))
        inst = "bpl";
      else
        inst = "bge";
    }
  else
    {
      /* false label is present */
      jlbl = IC_FALSE (ic);
      if (!strcmp (jval, "a"))
        inst = "bne";
      else if (!strcmp (jval, "c"))
        inst = "bcs";
      else if (!strcmp (jval, "n"))
        inst = "bmi";
      else
        inst = "blt";
    }
  emitBranch (inst, tlbl);
  emitBranch ("jmp", jlbl);
  if (!regalloc_dry_run)
    mc6800_emitLabel (tlbl);

  /* mark the icode as generated */
  ic->generated = 1;
}


/*-----------------------------------------------------------------*/
/* exchangedCmp : returns the opcode need if the two operands are  */
/*                exchanged in a comparison                        */
/*-----------------------------------------------------------------*/
static int
exchangedCmp (int opcode)
{
  switch (opcode)
    {
    case '<':
      return '>';
    case '>':
      return '<';
    case LE_OP:
      return GE_OP;
    case GE_OP:
      return LE_OP;
    case NE_OP:
      return NE_OP;
    case EQ_OP:
      return EQ_OP;
    default:
      werror (E_INTERNAL_ERROR, __FILE__, __LINE__, "opcode not a comparison");
    }
  return EQ_OP;                 /* shouldn't happen, but need to return something */
}

/*------------------------------------------------------------------*/
/* negatedCmp : returns the equivalent opcode for when a comparison */
/*              if not true                                         */
/*------------------------------------------------------------------*/
static int
negatedCmp (int opcode)
{
  switch (opcode)
    {
    case '<':
      return GE_OP;
    case '>':
      return LE_OP;
    case LE_OP:
      return '>';
    case GE_OP:
      return '<';
    case NE_OP:
      return EQ_OP;
    case EQ_OP:
      return NE_OP;
    default:
      werror (E_INTERNAL_ERROR, __FILE__, __LINE__, "opcode not a comparison");
    }
  return EQ_OP;                 /* shouldn't happen, but need to return something */
}

/*------------------------------------------------------------------*/
/* nameCmp : helper function for human readable debug output        */
/*------------------------------------------------------------------*/
static char *
nameCmp (int opcode)
{
  switch (opcode)
    {
    case '<':
      return "<";
    case '>':
      return ">";
    case LE_OP:
      return "<=";
    case GE_OP:
      return ">=";
    case NE_OP:
      return "!=";
    case EQ_OP:
      return "==";
    default:
      return "invalid";
    }
}

/*------------------------------------------------------------------*/
/* branchInstCmp : returns the conditional branch instruction that  */
/*                 will branch if the comparison is true            */
/*------------------------------------------------------------------*/
static char *
branchInstCmp (int opcode, int sign)
{
  switch (opcode)
    {
    case '<':
      if (sign)
        return "blt";
      else
        return "bcs";           /* same as blo */
    case '>':
      if (sign)
        return "bgt";
      else
        return "bhi";
    case LE_OP:
      if (sign)
        return "ble";
      else
        return "bls";
    case GE_OP:
      if (sign)
        return "bge";
      else
        return "bcc";           /* same as bhs */
    case NE_OP:
      return "bne";
    case EQ_OP:
      return "beq";
    default:
      werror (E_INTERNAL_ERROR, __FILE__, __LINE__, "opcode not a comparison");
    }
  return "brn";
}


static void
genCmp1 (iCode * ic, iCode * ifx, operand * left, operand * right, int opcode, int sign)
{
  reg_info *reg;
  bool needpull = false;

  if (IS_AOP_A (AOP (left)) || IS_AOP_B (AOP (left)))
    reg = AOP (left)->aopu.aop_reg[0];
  else
    {
      reg = (mc6800_reg_b->isDead && !IS_AOP_B (AOP (right))) ? mc6800_reg_b : mc6800_reg_a;
      needpull = pushRegIfSurv (reg);
      loadRegFromAop (reg, AOP (left), 0);
    }
  accopWithAop ("cmp", reg, AOP (right), 0);
  mc6800_freeReg (reg);
  freeAsmop (right, NULL, ic, false);
  freeAsmop (left, NULL, ic, false);

  if (ifx)
    {
      symbol *tlbl = (regalloc_dry_run ? 0 : newiTempLabel (NULL));
      symbol *jlbl = IC_TRUE (ifx) ? IC_TRUE (ifx) : IC_FALSE (ifx);

      pullOrFreeReg (reg, needpull);
      freeAsmop (IC_RESULT (ic), NULL, ic, true);
      emitBranch (branchInstCmp (opcode, sign), tlbl);
      emitBranch ("jmp", jlbl);
      if (!regalloc_dry_run)
        mc6800_emitLabel (tlbl);

      ifx->generated = 1;
    }
  else
    {
      symbol *tlbl1 = (regalloc_dry_run ? 0 : newiTempLabel (NULL));
      symbol *tlbl2 = (regalloc_dry_run ? 0 : newiTempLabel (NULL));

      if (reg != mc6800_reg_b)
        {
          pullOrFreeReg (reg, needpull);
          reg = mc6800_reg_b;
          needpull = pushRegIfSurv (reg);
        }
      emitBranch (branchInstCmp (opcode, sign), tlbl1);
      loadRegFromConst (reg, 0);
      emitBranch ("bra", tlbl2);
      if (!regalloc_dry_run)
        mc6800_emitLabel (tlbl1);
      mc6800_dirtyReg (reg, false);
      loadRegFromConst (reg, 1);
      if (!regalloc_dry_run)
        mc6800_emitLabel (tlbl2);
      mc6800_dirtyReg (reg, false);
      storeRegToFullAop (reg, AOP (IC_RESULT (ic)), false);
      pullOrFreeReg (reg, needpull);
      freeAsmop (IC_RESULT (ic), NULL, ic, true);
    }
}

static void
genCmp2 (iCode * ic, iCode * ifx, operand * left, operand * right, int opcode, int sign)
{
  reg_info *reg;
  bool needpull = false;

  if (AOP_TYPE (right) == AOP_STL && AOP_TYPE (left) != AOP_STL && AOP_TYPE (left) != AOP_REG && mc6800_reg_d->isDead)
    {
      operand *temp = left;
      left = right;
      right = temp;
      opcode = exchangedCmp (opcode);
    }
  if ((AOP_TYPE (right) == AOP_STL || AOP_TYPE (left) == AOP_STL) && !mc6800_reg_d->isDead && !IS_AOP_D (AOP (left)))
    {
      if ((opcode == '>') || (opcode == LE_OP))
        {
          operand *temp = left;

          left = right;
          right = temp;
          opcode = exchangedCmp (opcode);
        }
      pushReg (mc6800_reg_d, true);
      if (AOP_TYPE (right) == AOP_STL && AOP_TYPE (left) == AOP_LIT)
        {
          const char *tmp = setupTmpFromSP (_G.stackOfs + AOP (right)->aopu.aop_stk);

          loadRegFromAop (mc6800_reg_d, AOP (left), 0);
          mc6800_emitOp ("subb", MODE_DIR, "*%s+1", tmp);
          mc6800_emitOp ("sbca", MODE_DIR, "*%s", tmp);
          freeTemp ();
        }
      else if (AOP_TYPE (right) == AOP_STL)
        {
          const char *tmp = allocTemp ();
          int delta = 1 + _G.stackOfs + AOP (right)->aopu.aop_stk + _G.stackPushes;
          symbol *tlbl = (regalloc_dry_run || sign) ? NULL : newiTempLabel (NULL);

          loadRegFromAop (mc6800_reg_d, AOP (left), 0);
          // D < SP + delta ?
          //   D < SP : yes
          //   else   : D - SP < delta ?
          mc6800_emitOp ("sts", MODE_DIR, "*%s", tmp);
          mc6800_emitOp ("subb", MODE_DIR, "*%s+1", tmp);
          mc6800_emitOp ("sbca", MODE_DIR, "*%s", tmp);
          if (!sign)
            emitBranch ("bcs", tlbl);
          mc6800_emitOp ("subb", MODE_IMM, "#%d", delta & 0xff);
          mc6800_emitOp ("sbca", MODE_IMM, "#%d", (delta >> 8) & 0xff);
          if (tlbl)
            mc6800_emitLabel (tlbl);
          freeTemp ();
        }
      else
        {
          loadRegFromAop (mc6800_reg_d, AOP (left), 0);
          accopWithAop ("sub", mc6800_reg_b, AOP (right), 0);
          accopWithAop ("sbc", mc6800_reg_a, AOP (right), 1);
        }
      pullReg (mc6800_reg_d);
      freeAsmop (right, NULL, ic, false);
      freeAsmop (left, NULL, ic, false);
      if (ifx)
        {
          symbol *tlbl = (regalloc_dry_run ? 0 : newiTempLabel (NULL));

          freeAsmop (IC_RESULT (ic), NULL, ic, true);
          emitBranch (branchInstCmp (opcode, sign), tlbl);
          emitBranch ("jmp", IC_TRUE (ifx) ? IC_TRUE (ifx) : IC_FALSE (ifx));
          if (!regalloc_dry_run)
            mc6800_emitLabel (tlbl);
          ifx->generated = 1;
        }
      else
        {
          symbol *tlbl1 = (regalloc_dry_run ? 0 : newiTempLabel (NULL));
          symbol *tlbl2 = (regalloc_dry_run ? 0 : newiTempLabel (NULL));

          needpull = pushRegIfSurv (mc6800_reg_b);
          emitBranch (branchInstCmp (opcode, sign), tlbl1);
          loadRegFromConst (mc6800_reg_b, 0);
          emitBranch ("bra", tlbl2);
          if (!regalloc_dry_run)
            mc6800_emitLabel (tlbl1);
          mc6800_dirtyReg (mc6800_reg_b, false);
          loadRegFromConst (mc6800_reg_b, 1);
          if (!regalloc_dry_run)
            mc6800_emitLabel (tlbl2);
          mc6800_dirtyReg (mc6800_reg_b, false);
          storeRegToFullAop (mc6800_reg_b, AOP (IC_RESULT (ic)), false);
          pullOrFreeReg (mc6800_reg_b, needpull);
          freeAsmop (IC_RESULT (ic), NULL, ic, true);
        }
      return;
    }

  if (!mc6800_reg_d->isDead && !IS_AOP_D (AOP (left)) && !IS_AOP_D (AOP (right)))
    {
      int offset;

      if ((opcode == '>') || (opcode == LE_OP))
        {
          operand *temp = left;

          left = right;
          right = temp;
          opcode = exchangedCmp (opcode);
        }
      reg = mc6800_reg_b->isDead ? mc6800_reg_b : mc6800_reg_a;
      needpull = pushRegIfSurv (reg);
      for (offset = 0; offset < 2; offset++)
        {
          if (AOP_TYPE (left) == AOP_LIT)
            {
              mc6800_emitOpWithAcc ("lda", reg, MODE_IMM, "#0x%02x",
                             (unsigned int) ((ullFromVal (AOP (left)->aopu.aop_lit) >> (8 * offset)) & 0xff));
            }
          else
            loadRegFromAop (reg, AOP (left), offset);
          if (offset == 0)
            accopWithAop ("sub", reg, AOP (right), 0);
          else
            accopWithAop ("sbc", reg, AOP (right), 1);
        }
      mc6800_freeReg (reg);
      freeAsmop (right, NULL, ic, false);
      freeAsmop (left, NULL, ic, false);

      if (ifx)
        {
          symbol *tlbl = (regalloc_dry_run ? 0 : newiTempLabel (NULL));
          symbol *jlbl = IC_TRUE (ifx) ? IC_TRUE (ifx) : IC_FALSE (ifx);

          pullOrFreeReg (reg, needpull);
          freeAsmop (IC_RESULT (ic), NULL, ic, true);
          emitBranch (branchInstCmp (opcode, sign), tlbl);
          emitBranch ("jmp", jlbl);
          if (!regalloc_dry_run)
            mc6800_emitLabel (tlbl);

          ifx->generated = 1;
        }
      else
        {
          symbol *tlbl1 = (regalloc_dry_run ? 0 : newiTempLabel (NULL));
          symbol *tlbl2 = (regalloc_dry_run ? 0 : newiTempLabel (NULL));

          if (reg != mc6800_reg_b)
            {
              pullOrFreeReg (reg, needpull);
              reg = mc6800_reg_b;
              needpull = pushRegIfSurv (reg);
            }
          emitBranch (branchInstCmp (opcode, sign), tlbl1);
          loadRegFromConst (reg, 0);
          emitBranch ("bra", tlbl2);
          if (!regalloc_dry_run)
            mc6800_emitLabel (tlbl1);
          mc6800_dirtyReg (reg, false);
          loadRegFromConst (reg, 1);
          if (!regalloc_dry_run)
            mc6800_emitLabel (tlbl2);
          mc6800_dirtyReg (reg, false);
          storeRegToFullAop (reg, AOP (IC_RESULT (ic)), false);
          pullOrFreeReg (reg, needpull);
          freeAsmop (IC_RESULT (ic), NULL, ic, true);
        }
      return;
    }

  // Put the operand in D on the left.
  if (IS_AOP_D (AOP (right)))
    {
      operand *temp = left;

      left = right;
      right = temp;
      opcode = exchangedCmp (opcode);
    }
  if (sign && AOP_TYPE (right) == AOP_LIT && !(ullFromVal (AOP (right)->aopu.aop_lit) & 0xffffull)
      && (opcode == '<' || opcode == GE_OP))
    {
      if (!IS_AOP_D (AOP (left)))
        loadRegFromAop (mc6800_reg_a, AOP (left), 1);
      mc6800_emitOp ("tsta", MODE_INH, "");
    }
  else
    {
      if (!IS_AOP_D (AOP (left)))
        {
          if (IS_AOP_X (AOP (left)))
            {
              const char *tmp = allocTemp ();

              mc6800_emitOp ("stx", MODE_DIR, "*%s", tmp);
              mc6800_emitOp ("ldab", MODE_DIR, "*%s+1", tmp);
              mc6800_emitOp ("ldaa", MODE_DIR, "*%s", tmp);
              freeTemp ();
            }
          else
            loadRegFromAop (mc6800_reg_d, AOP (left), 0);
        }
      if (AOP_TYPE (right) == AOP_LIT && ((opcode == '>') || (opcode == LE_OP))
          && (ullFromVal (AOP (right)->aopu.aop_lit) & 0xffffull) != (sign ? 0x7fffull : 0xffffull))
        {
          unsigned int lim = (unsigned int) ((ullFromVal (AOP (right)->aopu.aop_lit) & 0xffffull) + 1);

          mc6800_emitOp ("subb", MODE_IMM, "#0x%02x", lim & 0xff);
          mc6800_emitOp ("sbca", MODE_IMM, "#0x%02x", (lim >> 8) & 0xff);
          opcode = (opcode == '>') ? GE_OP : '<';
        }
      else if (AOP_TYPE (right) == AOP_STL)
        {
          const char *tmp = allocTemp ();
          int delta = 1 + _G.stackOfs + AOP (right)->aopu.aop_stk + _G.stackPushes;
          symbol *tlbl = (regalloc_dry_run || sign) ? NULL : newiTempLabel (NULL);

          // D < SP + delta ?
          //   D < SP : yes
          //   else   : D - SP < delta ?
          mc6800_emitOp ("sts", MODE_DIR, "*%s", tmp);
          mc6800_emitOp ("subb", MODE_DIR, "*%s+1", tmp);
          mc6800_emitOp ("sbca", MODE_DIR, "*%s", tmp);
          if (!sign)
            emitBranch ("bcs", tlbl);
          mc6800_emitOp ("subb", MODE_IMM, "#%d", delta & 0xff);
          mc6800_emitOp ("sbca", MODE_IMM, "#%d", (delta >> 8) & 0xff);
          if (tlbl)
            mc6800_emitLabel (tlbl);
          freeTemp ();
        }
      else
        {
          accopWithAop ("sub", mc6800_reg_b, AOP (right), 0);
          accopWithAop ("sbc", mc6800_reg_a, AOP (right), 1);
        }
    }
  mc6800_freeReg (mc6800_reg_d);
  reg = mc6800_reg_b;
  freeAsmop (right, NULL, ic, false);
  freeAsmop (left, NULL, ic, false);

  if (ifx)
    {
      symbol *tlbl = (regalloc_dry_run ? 0 : newiTempLabel (NULL));
      symbol *jlbl = IC_TRUE (ifx) ? IC_TRUE (ifx) : IC_FALSE (ifx);

      freeAsmop (IC_RESULT (ic), NULL, ic, true);
      if ((opcode == '>') || (opcode == LE_OP))
        {
          symbol *tlbl2 = (regalloc_dry_run ? 0 : newiTempLabel (NULL));

          emitBranch (branchInstCmp ('<', sign), (opcode == '>') ? tlbl2 : tlbl);
          emitBranch (branchInstCmp ('>', sign), (opcode == '>') ? tlbl : tlbl2);
          mc6800_emitOp ("tstb", MODE_INH, "");
          emitBranch ((opcode == '>') ? "bne" : "beq", tlbl);
          if (!regalloc_dry_run)
            mc6800_emitLabel (tlbl2);
        }
      else
        emitBranch (branchInstCmp (opcode, sign), tlbl);
      emitBranch ("jmp", jlbl);
      if (!regalloc_dry_run)
        mc6800_emitLabel (tlbl);

      ifx->generated = 1;
    }
  else
    {
      symbol *tlbl1 = (regalloc_dry_run ? 0 : newiTempLabel (NULL));
      symbol *tlbl2 = (regalloc_dry_run ? 0 : newiTempLabel (NULL));

      if ((opcode == '>') || (opcode == LE_OP))
        {
          symbol *tlbl3 = (regalloc_dry_run ? 0 : newiTempLabel (NULL));

          emitBranch (branchInstCmp ('<', sign), (opcode == '>') ? tlbl3 : tlbl1);
          emitBranch (branchInstCmp ('>', sign), (opcode == '>') ? tlbl1 : tlbl3);
          mc6800_emitOp ("tstb", MODE_INH, "");
          emitBranch ((opcode == '>') ? "bne" : "beq", tlbl1);
          if (!regalloc_dry_run)
            mc6800_emitLabel (tlbl3);
        }
      else
        emitBranch (branchInstCmp (opcode, sign), tlbl1);
      loadRegFromConst (reg, 0);
      emitBranch ("bra", tlbl2);
      if (!regalloc_dry_run)
        mc6800_emitLabel (tlbl1);
      mc6800_dirtyReg (reg, false);
      loadRegFromConst (reg, 1);
      if (!regalloc_dry_run)
        mc6800_emitLabel (tlbl2);
      mc6800_dirtyReg (reg, false);
      storeRegToFullAop (reg, AOP (IC_RESULT (ic)), false);
      pullOrFreeReg (reg, needpull);
      freeAsmop (IC_RESULT (ic), NULL, ic, true);
    }
}

static void
genCmpMANY (iCode * ic, iCode * ifx, operand * left, operand * right, int size, int opcode, int sign)
{
  reg_info *reg;
  bool needpull;
  int offset = 0;
  char *sub;
  unsigned long long lit = 0ull;

  /* These conditions depend on the Z flag bit, but Z is */
  /* only valid for the last byte of the comparison, not */
  /* the whole value. So exchange the operands to get a  */
  /* comparison that doesn't depend on Z.               */
  if ((opcode == '>') || (opcode == LE_OP))
    {
      operand *temp = left;

      left = right;
      right = temp;
      opcode = exchangedCmp (opcode);
    }
  if ((AOP_TYPE (right) == AOP_LIT) && !isOperandVolatile (left, false))
    {
      lit = ullFromVal (AOP (right)->aopu.aop_lit);
      while ((size > 1) && (((lit >> (8 * offset)) & 0xff) == 0))
        {
          offset++;
          size--;
        }
    }
  reg = mc6800_reg_b->isDead ? mc6800_reg_b : mc6800_reg_a;
  needpull = pushRegIfSurv (reg);
  sub = "sub";
  while (size--)
    {
      D (emitcode (";     genCmp ", "sub=%s,size=%d", sub,size));
      if (AOP_TYPE (left) == AOP_LIT)
        {
          lit = ullFromVal (AOP (left)->aopu.aop_lit);
          mc6800_emitOpWithAcc ("lda", reg, MODE_IMM, "#0x%02x",
                         (unsigned int) ((lit >> (8 * offset)) & 0xff));
        }
      else
        loadRegFromAop (reg, AOP (left), offset);
      accopWithAop (sub, reg, AOP (right), offset);
      mc6800_freeReg (reg);
      offset++;
      sub = "sbc";
    }
  freeAsmop (right, NULL, ic, false);
  freeAsmop (left, NULL, ic, false);

  if (ifx)
    {
      symbol *tlbl = (regalloc_dry_run ? 0 : newiTempLabel (NULL));
      symbol *jlbl = IC_TRUE (ifx) ? IC_TRUE (ifx) : IC_FALSE (ifx);

      pullOrFreeReg (reg, needpull);
      freeAsmop (IC_RESULT (ic), NULL, ic, true);
      emitBranch (branchInstCmp (opcode, sign), tlbl);
      emitBranch ("jmp", jlbl);
      if (!regalloc_dry_run)
        mc6800_emitLabel (tlbl);

      ifx->generated = 1;
    }
  else
    {
      symbol *tlbl1 = (regalloc_dry_run ? 0 : newiTempLabel (NULL));
      symbol *tlbl2 = (regalloc_dry_run ? 0 : newiTempLabel (NULL));

      if (reg != mc6800_reg_b)
        {
          pullOrFreeReg (reg, needpull);
          reg = mc6800_reg_b;
          needpull = pushRegIfSurv (reg);
        }
      emitBranch (branchInstCmp (opcode, sign), tlbl1);
      loadRegFromConst (reg, 0);
      emitBranch ("bra", tlbl2);
      if (!regalloc_dry_run)
        mc6800_emitLabel (tlbl1);
      mc6800_dirtyReg (reg, false);
      loadRegFromConst (reg, 1);
      if (!regalloc_dry_run)
        mc6800_emitLabel (tlbl2);
      mc6800_dirtyReg (reg, false);
      storeRegToFullAop (reg, AOP (IC_RESULT (ic)), false);
      pullOrFreeReg (reg, needpull);
      freeAsmop (IC_RESULT (ic), NULL, ic, true);
    }
}

/*------------------------------------------------------------------*/
/* genCmp :- greater or less than (and maybe with equal) comparison */
/*------------------------------------------------------------------*/
static void
genCmp (iCode * ic, iCode * ifx)
{
  operand *left, *right, *result;
  sym_link *letype, *retype;
  int sign, opcode;
  int size;
  bool exchange;

  opcode = ic->op;

  D (emitcode (";     genCmp", "(%s)", nameCmp (opcode)));

  result = IC_RESULT (ic);
  left = IC_LEFT (ic);
  right = IC_RIGHT (ic);

  sign = 0;
  if (IS_SPEC (operandType (left)) && IS_SPEC (operandType (right)))
    {
      letype = getSpec (operandType (left));
      retype = getSpec (operandType (right));
      sign = !(SPEC_USIGN (letype) | SPEC_USIGN (retype));
    }

  /* assign the amsops */
  aopOp (left, ic, false);
  aopOp (right, ic, false);
  aopOp (result, ic, true);

  if (ifx && IC_TRUE (ifx))
    opcode = negatedCmp (opcode);

  size = max (AOP_SIZE (left), AOP_SIZE (right));

  /* need register operand on left, prefer literal operand on right */
  if (AOP_TYPE (left) == AOP_LIT)
    exchange = AOP_TYPE (right) != AOP_LIT;
  else if (AOP_TYPE (right) == AOP_LIT)
    exchange = false;
  else if (AOP_TYPE (right) == AOP_REG && !IS_AOP_A (AOP (left)))
    exchange = true;
  else if (AOP_TYPE (left) == AOP_REG && IS_AOP_A (AOP (left)))
    exchange = false;
  else
    exchange = false;

  if (exchange)
    {
      operand *temp = left;
      left = right;
      right = temp;
      opcode = exchangedCmp (opcode);
    }

  if (size == 1)
    genCmp1 (ic, ifx, left, right, opcode, sign);
  else if (size == 2)
    genCmp2 (ic, ifx, left, right, opcode, sign);
  else
    genCmpMANY (ic, ifx, left, right, size, opcode, sign);
}

/*-----------------------------------------------------------------*/
/* genCmpEQorNE - equal or not equal comparison                    */
/*-----------------------------------------------------------------*/
static void
genCmpEQorNE (iCode * ic, iCode * ifx)
{
  operand *left, *right, *result;
  int opcode;
  int size, offset = 0;
  symbol *jlbl = NULL;
  symbol *tlbl_NE = NULL;
  symbol *tlbl_EQ = NULL;
  bool needpulla = false;
  bool loaded;

  opcode = ic->op;

  D (emitcode (";     genCmpEQorNE", "(%s)", nameCmp (opcode)));
  result = IC_RESULT (ic);
  left = IC_LEFT (ic);
  right = IC_RIGHT (ic);

  /* assign the amsops */
  aopOp (left, ic, false);
  aopOp (right, ic, false);
  aopOp (result, ic, true);

  /* need register operand on left, prefer literal operand on right */
  if ((AOP_TYPE (right) == AOP_REG) || AOP_TYPE (left) == AOP_LIT)
    {
      operand *temp = left;
      left = right;
      right = temp;
      opcode = exchangedCmp (opcode);
    }

  if (IS_AOP_X (AOP (left)) && IS_AOP_D (AOP (right)))
    {
      operand *temp = left;
      left = right;
      right = temp;
    }
  if (AOP_TYPE (right) == AOP_IDX && AOP_TYPE (left) != AOP_IDX && AOP_TYPE (left) != AOP_REG)
    {
      operand *temp = left;
      left = right;
      right = temp;
    }
  if (AOP_TYPE (right) == AOP_STL && AOP_TYPE (left) != AOP_STL && AOP_TYPE (left) != AOP_REG)
    {
      operand *temp = left;
      left = right;
      right = temp;
    }

  if (ifx)
    {
      if (IC_TRUE (ifx))
        {
          jlbl = IC_TRUE (ifx);
          opcode = negatedCmp (opcode);
        }
      else
        {
          /* false label is present */
          jlbl = IC_FALSE (ifx);
        }
    }
  if (AOP_TYPE (left) == AOP_STL && AOP_TYPE (right) == AOP_STL)
    {
      bool cond = ((AOP (left)->aopu.aop_stk == AOP (right)->aopu.aop_stk) == (ic->op == EQ_OP));

      if (ifx)
        {
          if (IC_TRUE (ifx) ? cond : !cond)
            emitBranch ("jmp", jlbl);
          ifx->generated = 1;
        }
      else
        for (offset = 0; offset < AOP_SIZE (result); offset++)
          storeConstToAop (offset == 0 && cond, AOP (result), offset);
      freeAsmop (right, NULL, ic, false);
      freeAsmop (left, NULL, ic, false);
      freeAsmop (result, NULL, ic, true);
      return;
    }
  if (AOP_TYPE (right) == AOP_STL && !IS_AOP_X (AOP (left)) && !IS_AOP_D (AOP (left))
      || AOP_TYPE (left) == AOP_STL && AOP_SIZE (right) != 2)
    {
      UNIMPLEMENTED;
      freeAsmop (right, NULL, ic, false);
      freeAsmop (left, NULL, ic, false);
      freeAsmop (result, NULL, ic, true);
      return;
    }

  size = max (AOP_SIZE (left), AOP_SIZE (right));

  if (AOP_SIZE (left) == 2 &&
    (AOP_TYPE (left) == AOP_DIR || AOP_TYPE (left) == AOP_EXT || AOP_TYPE (left) == AOP_IDX || IS_AOP_X (AOP (left))) &&
    (mc6800_reg_x->isDead || IS_AOP_X (AOP (left))) &&
    ((AOP_TYPE (right) == AOP_LIT && AOP_SIZE (right) <= 2) ||
    (AOP_TYPE (right) == AOP_IMMD && AOP_SIZE (right) <= 2) ||
    (AOP_TYPE (right) == AOP_DIR && AOP_SIZE (right) == 2) ||
    (AOP_TYPE (right) == AOP_EXT && AOP_SIZE (right) == 2)))
    {
      loadRegFromAop (mc6800_reg_x, AOP (left), 0);
      mc6800_emitOpw_o ("cpx", AOP (right), 0);
      mc6800_freeReg (mc6800_reg_x);
    }
  else if (IS_AOP_X (AOP (left)) && AOP_TYPE (right) == AOP_STL)
    {
      const char *tmp = setupTmpFromSP (_G.stackOfs + AOP (right)->aopu.aop_stk);
      mc6800_emitOp ("cpx", MODE_DIR, "*%s", tmp);
      freeTemp ();
    }
  else if (IS_AOP_D (AOP (left)) && AOP_TYPE (right) == AOP_STL)
    {
      const char *tmp = setupTmpFromSP (_G.stackOfs + AOP (right)->aopu.aop_stk);
      mc6800_emitOp ("cmpb", MODE_DIR, "*%s+1", tmp);
      if (!ifx && !needpulla)
        needpulla = pushRegIfSurv (mc6800_reg_a);
      if (!tlbl_NE && !regalloc_dry_run)
        tlbl_NE = newiTempLabel (NULL);
      emitBranch ("bne", tlbl_NE);
      mc6800_emitOp ("cmpa", MODE_DIR, "*%s", tmp);
      freeTemp ();
    }
  else if (IS_AOP_D (AOP (left)) && IS_AOP_X (AOP (right)))
    {
      const char *tmp = allocTemp ();
      mc6800_emitOp ("stab", MODE_DIR, "*%s+1", tmp);
      mc6800_emitOp ("staa", MODE_DIR, "*%s", tmp);
      mc6800_emitOp ("cpx", MODE_DIR, "*%s", tmp);
      freeTemp ();
    }
  else if (AOP_TYPE (left) == AOP_STL && mc6800_reg_a->isDead && mc6800_reg_b->isDead)
    {
      loadRegFromAop (mc6800_reg_d, AOP (left), 0);
      accopWithAop ("cmp", mc6800_reg_b, AOP (right), 0);
      if (!ifx && !needpulla)
        needpulla = pushRegIfSurv (mc6800_reg_a);
      if (!tlbl_NE && !regalloc_dry_run)
        tlbl_NE = newiTempLabel (NULL);
      emitBranch ("bne", tlbl_NE);
      accopWithAop ("cmp", mc6800_reg_a, AOP (right), 1);
    }
  else if (AOP_TYPE (left) == AOP_STL && mc6800_reg_x->isDead && AOP_SIZE (right) == 2 && !IS_AOP_WITH_X (AOP (right)))
    {
      const char *tmp;

      loadRegFromAop (mc6800_reg_x, AOP (right), 0);
      tmp = setupTmpFromSP (_G.stackOfs + AOP (left)->aopu.aop_stk);
      mc6800_emitOp ("cpx", MODE_DIR, "*%s", tmp);
      freeTemp ();
      mc6800_freeReg (mc6800_reg_x);
    }
  else if (AOP_TYPE (left) == AOP_STL)
    {
      const char *xtmp = allocTemp ();
      const char *rtmp = allocTemp ();
      const char *tmp;
      symbol *tlbl = (regalloc_dry_run ? 0 : newiTempLabel (NULL));
      bool xfree = mc6800_reg_x->isFree;
      bool needpulla;

      mc6800_emitOp ("stx", MODE_DIR, "*%s", xtmp);
      mc6800_freeReg (mc6800_reg_x);
      needpulla = pushRegIfUsed (mc6800_reg_a);
      loadRegFromAop (mc6800_reg_a, AOP (right), 0);
      mc6800_emitOp ("staa", MODE_DIR, "*%s+1", rtmp);
      loadRegFromAop (mc6800_reg_a, AOP (right), 1);
      mc6800_emitOp ("staa", MODE_DIR, "*%s", rtmp);
      tmp = setupTmpFromSP (_G.stackOfs + AOP (left)->aopu.aop_stk);
      mc6800_emitOp ("ldx", MODE_DIR, "*%s", xtmp);
      mc6800_dirtyReg (mc6800_reg_x, false);
      mc6800_reg_x->isFree = xfree;
      mc6800_emitOp ("ldaa", MODE_DIR, "*%s+1", rtmp);
      mc6800_emitOp ("cmpa", MODE_DIR, "*%s+1", tmp);
      emitBranch ("bne", tlbl);
      mc6800_emitOp ("ldaa", MODE_DIR, "*%s", rtmp);
      mc6800_emitOp ("cmpa", MODE_DIR, "*%s", tmp);
      if (!regalloc_dry_run)
        mc6800_emitLabel (tlbl);
      freeTemp ();
      freeTemp ();
      freeTemp ();
      mc6800_dirtyReg (mc6800_reg_a, false);
      pullOrFreeReg (mc6800_reg_a, needpulla);
    }
  else
    {
      offset = 0;
      while (size--)
        {
          if (AOP_TYPE (left) == AOP_REG && AOP (left)->aopu.aop_reg[offset]->rIdx == B_IDX)
            {
              if (aopIsLitVal (right->aop, offset, 1, 0x00))
                {
                  mc6800_emitOp ("tstb", MODE_INH, "");
                }
              else if (AOP_TYPE (right) == AOP_REG)
                {
                  mc6800_emitOp ("cba", MODE_INH, "");
                }
              else
                accopWithAop ("cmp", mc6800_reg_b, AOP (right), offset);
            }
          else
            {
              loaded = false;
              if (!(AOP_TYPE (left) == AOP_REG && AOP (left)->aopu.aop_reg[offset]->rIdx == A_IDX))
                {
                  needpulla = pushRegIfSurv (mc6800_reg_a);
                  loaded = mc6800_findRegAop (AOP (left), offset) != mc6800_reg_a;
                  loadRegFromAop (mc6800_reg_a, AOP (left), offset);
                }
              if (aopIsLitVal (right->aop, offset, 1, 0x00))
                {
                  if (!loaded)
                    mc6800_emitOp ("tsta", MODE_INH, "");
                }
              else if (AOP_TYPE (right) == AOP_REG)
                {
                  mc6800_emitOp ("cba", MODE_INH, "");
                }
              else
                accopWithAop ("cmp", mc6800_reg_a, AOP (right), offset);
              if (!(AOP_TYPE (left) == AOP_REG && AOP (left)->aopu.aop_reg[offset]->rIdx == A_IDX))
                pullOrFreeReg (mc6800_reg_a, needpulla);
              needpulla = false;
            }
          if (size)
            {
              if (!needpulla && !ifx)
                needpulla = pushRegIfSurv (mc6800_reg_a);
              if (!tlbl_NE && !regalloc_dry_run)
                tlbl_NE = newiTempLabel (NULL);
              emitBranch ("bne", tlbl_NE);
              pullOrFreeReg (mc6800_reg_a, needpulla);
              needpulla = false;
            }
          offset++;
          }
    }
  freeAsmop (right, NULL, ic, false);
  freeAsmop (left, NULL, ic, false);

  if (ifx)
    {
      freeAsmop (result, NULL, ic, true);

      if (opcode == EQ_OP)
        {
          if (!tlbl_EQ && !regalloc_dry_run)
            tlbl_EQ = newiTempLabel (NULL);
          emitBranch ("beq", tlbl_EQ);
          if (tlbl_NE)
            mc6800_emitLabel (tlbl_NE);
          emitBranch ("jmp", jlbl);
          if (!regalloc_dry_run)
            mc6800_emitLabel (tlbl_EQ);
        }
      else
        {
          if (!tlbl_NE && !regalloc_dry_run)
            tlbl_NE = newiTempLabel (NULL);
          emitBranch ("bne", tlbl_NE);
          emitBranch ("jmp", jlbl);
          if (!regalloc_dry_run)
            mc6800_emitLabel (tlbl_NE);
        }

      /* mark the icode as generated */
      ifx->generated = 1;
    }
  else
    {
      symbol *tlbl = (regalloc_dry_run ? 0 : newiTempLabel (NULL));

      if (!needpulla)
        needpulla = pushRegIfSurv (mc6800_reg_a);
      if (opcode == EQ_OP)
        {
          if (!tlbl_EQ && !regalloc_dry_run)
            tlbl_EQ = newiTempLabel (NULL);
          emitBranch ("beq", tlbl_EQ);
          if (tlbl_NE)
            mc6800_emitLabel (tlbl_NE);
          mc6800_dirtyReg (mc6800_reg_a, false);
          loadRegFromConst (mc6800_reg_a, 0);
          emitBranch ("bra", tlbl);
          if (!regalloc_dry_run)
            mc6800_emitLabel (tlbl_EQ);
          mc6800_dirtyReg (mc6800_reg_a, false);
          loadRegFromConst (mc6800_reg_a, 1);
        }
      else
        {
          if (!tlbl_NE && !regalloc_dry_run)
            tlbl_NE = newiTempLabel (NULL);
          emitBranch ("bne", tlbl_NE);
          loadRegFromConst (mc6800_reg_a, 0);
          emitBranch ("bra", tlbl);
          if (!regalloc_dry_run)
            mc6800_emitLabel (tlbl_NE);
          mc6800_dirtyReg (mc6800_reg_a, false);
          loadRegFromConst (mc6800_reg_a, 1);
        }

      if (!regalloc_dry_run)
        mc6800_emitLabel (tlbl);
      mc6800_dirtyReg (mc6800_reg_a, false);
      storeRegToFullAop (mc6800_reg_a, AOP (result), false);
      pullOrFreeReg (mc6800_reg_a, needpulla);
      freeAsmop (result, NULL, ic, true);
    }
}

/*-----------------------------------------------------------------*/
/* hasIncmc6800 - operand is incremented before any other use        */
/*-----------------------------------------------------------------*/
iCode *
hasIncmc6800 (operand *op, const iCode *ic, int osize)
{
  sym_link *type = operandType (op);
  sym_link *retype = getSpec (type);
  iCode *lic = ic->next;
  int isize;

  /* this could from a cast, e.g.: "(char xdata *) 0x7654;" */
  if (!IS_SYMOP (op))
    return NULL;

  if (IS_BITVAR (retype) || !IS_PTR (type))
    return NULL;
  if (IS_AGGREGATE (type->next))
    return NULL;
  if (osize != (isize = getSize (type->next)))
    return NULL;

  while (lic)
    {
      /* if operand of the form op = op + <sizeof *op> */
      if (lic->op == '+' && isOperandEqual (IC_LEFT (lic), op) &&
          isOperandEqual (IC_RESULT (lic), op) &&
          isOperandLiteral (IC_RIGHT (lic)) && operandLitValue (IC_RIGHT (lic)) == isize)
        {
          return lic;
        }
      /* if the operand used or deffed */
      if (bitVectBitValue (OP_USES (op), lic->key) || lic->defKey == op->key)
        {
          return NULL;
        }
      /* if GOTO or IFX */
      if (lic->op == IFX || lic->op == GOTO || lic->op == LABEL)
        break;
      lic = lic->next;
    }
  return NULL;
}

/*-----------------------------------------------------------------*/
/* isLiteralBit - test if lit == 2^n                               */
/*-----------------------------------------------------------------*/
static int
isLiteralBit (unsigned long lit)
{
  unsigned long pw[32] =
  {
    1L, 2L, 4L, 8L, 16L, 32L, 64L, 128L,
    0x100L, 0x200L, 0x400L, 0x800L,
    0x1000L, 0x2000L, 0x4000L, 0x8000L,
    0x10000L, 0x20000L, 0x40000L, 0x80000L,
    0x100000L, 0x200000L, 0x400000L, 0x800000L,
    0x1000000L, 0x2000000L, 0x4000000L, 0x8000000L,
    0x10000000L, 0x20000000L, 0x40000000L, 0x80000000L
  };
  int idx;

  for (idx = 0; idx < 32; idx++)
    if (lit == pw[idx])
      return idx + 1;
  return 0;
}

/*-----------------------------------------------------------------*/
/* genAnd  - code for and                                          */
/*-----------------------------------------------------------------*/
static void
genAnd (iCode * ic, iCode * ifx)
{
  operand *left, *right, *result;
  int size, offset = 0;
  unsigned long long lit = 0ll;
  int bitpos = -1;
  unsigned char bytemask;
  bool needpulla = false;
  bool needpullb = false;
  bool earlystore = false;

  D (emitcode (";     genAnd", ""));

  aopOp ((left = IC_LEFT (ic)), ic, false);
  aopOp ((right = IC_RIGHT (ic)), ic, false);
  aopOp ((result = IC_RESULT (ic)), ic, true);

#ifdef DEBUG_TYPE
  DD (emitcode ("", "; Type res[%d] = l[%d]&r[%d]", AOP_TYPE (result), AOP_TYPE (left), AOP_TYPE (right)));
  DD (emitcode ("", "; Size res[%d] = l[%d]&r[%d]", AOP_SIZE (result), AOP_SIZE (left), AOP_SIZE (right)));
#endif

  /* if left is a literal & right is not then exchange them */
  if (AOP_TYPE (left) == AOP_LIT && AOP_TYPE (right) != AOP_LIT)
    {
      operand *tmp = right;
      right = left;
      left = tmp;
    }

  /* if right is in registers & left is not in A then exchange them */
  if (AOP_TYPE (right) == AOP_REG && ! IS_AOP_WITH_A (AOP (left)))
    {
      operand *tmp = right;
      right = left;
      left = tmp;
    }

  size = (AOP_SIZE (left) >= AOP_SIZE (right)) ? AOP_SIZE (left) : AOP_SIZE (right);

  if (AOP_TYPE (right) == AOP_LIT)
    {
      lit = ullFromVal (AOP (right)->aopu.aop_lit);
      if (size == 1)
        lit &= 0xff;
      else if (size == 2)
        lit &= 0xffff;
      else if (size == 4)
        lit &= 0xffffffff;
      else if (size == 8)
        lit &= 0xffffffffffffffff;
      bitpos = isLiteralBit (lit) - 1;
    }

  if (AOP_TYPE (result) == AOP_CRY && size > 1 && (isOperandVolatile (left, false) || isOperandVolatile (right, false)))
    {
      const char *ltmp = NULL;
      const char *tmp;

      if (size == 2 && (IS_AOP_D (AOP (left)) || IS_AOP_X (AOP (left))))
        {
          ltmp = allocTemp ();
          if (IS_AOP_X (AOP (left)))
            {
              mc6800_emitOp ("stx", MODE_DIR, "*%s", ltmp);
            }
          else
            {
              mc6800_emitOp ("stab", MODE_DIR, "*%s+1", ltmp);
              mc6800_emitOp ("staa", MODE_DIR, "*%s", ltmp);
            }
        }

      tmp = allocTemp ();

      needpulla = pushRegIfSurv (mc6800_reg_a);

      /* this generates ugly code, but meets volatility requirements */
      loadRegFromConst (mc6800_reg_a, 0);
      mc6800_emitOp ("staa", MODE_DIR, "*%s", tmp);

      offset = 0;
      while (size--)
        {
          if (ltmp)
            {
              mc6800_emitOp ("ldaa", MODE_DIR, offset ? "*%s" : "*%s+1", ltmp);
              mc6800_dirtyReg (mc6800_reg_a, false);
              mc6800_useReg (mc6800_reg_a);
            }
          else
            loadRegFromAop (mc6800_reg_a, AOP (left), offset);
          accopWithAop ("and", mc6800_reg_a, AOP (right), offset);
          mc6800_emitOp ("oraa", MODE_DIR, "*%s", tmp);
          mc6800_emitOp ("staa", MODE_DIR, "*%s", tmp);
          offset++;
        }

      mc6800_emitOp ("ldaa", MODE_DIR, "*%s", tmp);
      mc6800_dirtyReg (mc6800_reg_a, false);
      mc6800_useReg (mc6800_reg_a);
      freeTemp ();
      if (ltmp)
        {
          freeTemp ();
        }
      mc6800_emitOp ("tsta", MODE_INH, "");

      pullOrFreeReg (mc6800_reg_a, needpulla);

      genIfxJump (ifx, "a");

      goto release;
    }

  if (AOP_TYPE (result) == AOP_CRY && AOP_TYPE (right) == AOP_LIT)
    {
      if (bitpos >= 0 && (bitpos & 7) == 7)
        {
          rmwWithAop ("tst", AOP (left), bitpos >> 3);
          genIfxJump (ifx, "n");
          goto release;
        }
    }

  if (AOP_TYPE (result) == AOP_CRY && size == 1 && (IS_AOP_A (AOP (left)) || IS_AOP_A (AOP (right))))
    {
      if (IS_AOP_A (AOP (left)))
        accopWithAop ("bit", mc6800_reg_a, AOP (right), 0);
      else
        accopWithAop ("bit", mc6800_reg_a, AOP (left), 0);
      genIfxJump (ifx, "a");
      goto release;
    }

  if (AOP_TYPE (result) == AOP_CRY)
    {
      symbol *tlbl = NULL;

      needpulla = pushRegIfSurv (mc6800_reg_a);

      offset = 0;
      while (size--)
        {
          bytemask = (lit >> (offset * 8)) & 0xff;

          if (AOP_TYPE (right) == AOP_LIT && bytemask == 0)
            {
              /* do nothing */
            }
          else if (AOP_TYPE (right) == AOP_LIT && bytemask == 0xff)
            {
              rmwWithAop ("tst", AOP (left), offset);
              if (size)
                {
                  if (!tlbl && !regalloc_dry_run)
                    tlbl = newiTempLabel (NULL);
                  emitBranch ("bne", tlbl);
                }
            }
          else if (IS_AOP_D (AOP (left)) && offset == 0)
            {
              accopWithAop ("bit", mc6800_reg_b, AOP (right), offset);
              if (size)
                {
                  if (!tlbl && !regalloc_dry_run)
                    tlbl = newiTempLabel (NULL);
                  emitBranch ("bne", tlbl);
                }
            }
          else
            {
              loadRegFromAop (mc6800_reg_a, AOP (left), offset);
              accopWithAop ("bit", mc6800_reg_a, AOP (right), offset);
              mc6800_freeReg (mc6800_reg_a);
              if (size)
                {
                  if (!tlbl && !regalloc_dry_run)
                    tlbl = newiTempLabel (NULL);
                  emitBranch ("bne", tlbl);
                }
            }
          offset++;
        }
      if (tlbl)
        mc6800_emitLabel (tlbl);

      pullOrFreeReg (mc6800_reg_a, needpulla);

      if (ifx)
        genIfxJump (ifx, "a");
      goto release;
    }

  size = AOP_SIZE (result);

  if (AOP_SIZE (result) == 1 && !IS_AOP_WITH_B (AOP (right))
  &&  !aopIsLitVal (right->aop, 0, 1, 0x00) && !aopIsLitVal (right->aop, 0, 1, 0xff)
  &&  (IS_AOP_B (AOP (result)) || (IS_AOP_B (AOP (left)) && mc6800_reg_b->isDead)))
    {
      loadRegFromAop (mc6800_reg_b, AOP (left), 0);
      accopWithAop ("and", mc6800_reg_b, AOP (right), 0);
      mc6800_dirtyReg (mc6800_reg_b, false);
      storeRegToAop (mc6800_reg_b, AOP (result), 0);
      goto release;
    }

  needpulla = pushRegIfSurv (mc6800_reg_a);
  if (IS_AOP_D (AOP (left)))
    needpullb = pushRegIfSurv (mc6800_reg_b);

  offset = 0;
  while (size--)
    {
      if (earlystore && offset == 1)
        pullReg (mc6800_reg_a);
      if (aopIsLitVal (left->aop, offset, 1, 0x00) || aopIsLitVal (right->aop, offset, 1, 0x00))
        {
          if (isOperandVolatile (left, false))
            {
              loadRegFromAop (mc6800_reg_a, AOP (left), offset);
              mc6800_freeReg (mc6800_reg_a);
            }
          if (isOperandVolatile (right, false))
            {
              loadRegFromAop (mc6800_reg_a, AOP (left), offset);
              mc6800_freeReg (mc6800_reg_a);
            }
          storeConstToAop (0, AOP (result), offset);
        }
      else if (aopIsLitVal (right->aop, offset, 1, 0xff) && !isOperandVolatile (left, false))
        transferAopAop (left->aop, offset, result->aop, offset);
      else if (aopIsLitVal (left->aop, offset, 1, 0xff) && !isOperandVolatile (right, false))
        transferAopAop (right->aop, offset, result->aop, offset);
      else if (IS_AOP_D (AOP (left)) && offset == 0)
        {
          accopWithAop ("and", mc6800_reg_b, AOP (right), offset);
          mc6800_dirtyReg (mc6800_reg_b, false);
          storeRegToAop (mc6800_reg_b, AOP (result), offset);
        }
      else
        {
          loadRegFromAop (mc6800_reg_a, AOP (left), offset);
          accopWithAop ("and", mc6800_reg_a, AOP (right), offset);
          storeRegToAop (mc6800_reg_a, AOP (result), offset);
          mc6800_freeReg (mc6800_reg_a);
        }
      if (AOP_TYPE (result) == AOP_REG && size && AOP (result)->aopu.aop_reg[offset]->rIdx == A_IDX)
        {
          pushReg (mc6800_reg_a, true);
          needpulla = true;
        }
      offset++;
    }

  pullOrFreeReg (mc6800_reg_b, needpullb);
  pullOrFreeReg (mc6800_reg_a, needpulla);

release:
  freeAsmop (left, NULL, ic, true);
  freeAsmop (right, NULL, ic, true);
  freeAsmop (result, NULL, ic, true);
}

/*-----------------------------------------------------------------*/
/* genOr  - code for or                                            */
/*-----------------------------------------------------------------*/
static void
genOr (iCode * ic, iCode * ifx)
{
  operand *left, *right, *result;
  int size, offset = 0;
  unsigned long long lit = 0ull;
  unsigned char bytemask;
  bool needpulla = false;
  bool needpullb = false;
  bool earlystore = false;

  D (emitcode (";     genOr", ""));

  aopOp ((left = IC_LEFT (ic)), ic, false);
  aopOp ((right = IC_RIGHT (ic)), ic, false);
  aopOp ((result = IC_RESULT (ic)), ic, true);

#ifdef DEBUG_TYPE
  DD (emitcode ("", "; Type res[%d] = l[%d]|r[%d]", AOP_TYPE (result), AOP_TYPE (left), AOP_TYPE (right)));
  DD (emitcode ("", "; Size res[%d] = l[%d]|r[%d]", AOP_SIZE (result), AOP_SIZE (left), AOP_SIZE (right)));
#endif

  /* if left is a literal & right is not then exchange them */
  if (AOP_TYPE (left) == AOP_LIT && AOP_TYPE (right) != AOP_LIT)
    {
      operand *tmp = right;
      right = left;
      left = tmp;
    }

  /* if right is in registers & left is not in A then exchange them */
  if (AOP_TYPE (right) == AOP_REG && !IS_AOP_WITH_A (AOP (left)))
    {
      operand *tmp = right;
      right = left;
      left = tmp;
    }

  if (AOP_TYPE (right) == AOP_LIT)
    lit = ullFromVal (AOP (right)->aopu.aop_lit);

  size = (AOP_SIZE (left) >= AOP_SIZE (right)) ? AOP_SIZE (left) : AOP_SIZE (right);

  if (AOP_TYPE (result) == AOP_CRY && size > 1 && (isOperandVolatile (left, false) || isOperandVolatile (right, false)))
    {
      const char *ltmp = NULL;
      const char *tmp;

      if (size == 2 && (IS_AOP_D (AOP (left)) || IS_AOP_X (AOP (left))))
        {
          ltmp = allocTemp ();
          if (IS_AOP_X (AOP (left)))
            {
              mc6800_emitOp ("stx", MODE_DIR, "*%s", ltmp);
            }
          else
            {
              mc6800_emitOp ("stab", MODE_DIR, "*%s+1", ltmp);
              mc6800_emitOp ("staa", MODE_DIR, "*%s", ltmp);
            }
        }

      tmp = allocTemp ();

      needpulla = pushRegIfSurv (mc6800_reg_a);

      /* this generates ugly code, but meets volatility requirements */
      loadRegFromConst (mc6800_reg_a, 0);
      mc6800_emitOp ("staa", MODE_DIR, "*%s", tmp);

      offset = 0;
      while (size--)
        {
          if (ltmp)
            {
              mc6800_emitOp ("ldaa", MODE_DIR, offset ? "*%s" : "*%s+1", ltmp);
              mc6800_dirtyReg (mc6800_reg_a, false);
              mc6800_useReg (mc6800_reg_a);
            }
          else
            loadRegFromAop (mc6800_reg_a, AOP (left), offset);
          accopWithAop ("ora", mc6800_reg_a, AOP (right), offset);
          mc6800_emitOp ("oraa", MODE_DIR, "*%s", tmp);
          mc6800_emitOp ("staa", MODE_DIR, "*%s", tmp);
          offset++;
        }

      mc6800_emitOp ("ldaa", MODE_DIR, "*%s", tmp);
      mc6800_dirtyReg (mc6800_reg_a, false);
      mc6800_useReg (mc6800_reg_a);
      freeTemp ();
      if (ltmp)
        {
          freeTemp ();
        }
      mc6800_emitOp ("tsta", MODE_INH, "");

      pullOrFreeReg (mc6800_reg_a, needpulla);

      genIfxJump (ifx, "a");

      goto release;
    }

  if (AOP_TYPE (result) == AOP_CRY)
    {
      symbol *tlbl = NULL;

      needpulla = pushRegIfSurv (mc6800_reg_a);
      if (IS_AOP_D (AOP (left)))
        needpullb = pushRegIfSurv (mc6800_reg_b);

      offset = 0;
      while (size--)
        {
          bytemask = (lit >> (offset * 8)) & 0xff;

          if (AOP_TYPE (right) == AOP_LIT && bytemask == 0x00)
            {
              rmwWithAop ("tst", AOP (left), offset);
              if (size)
                {
                  if (!tlbl && !regalloc_dry_run)
                    tlbl = newiTempLabel (NULL);
                  emitBranch ("bne", tlbl);
                }
            }
          else if (IS_AOP_D (AOP (left)) && offset == 0)
            {
              accopWithAop ("ora", mc6800_reg_b, AOP (right), offset);
              mc6800_dirtyReg (mc6800_reg_b, false);
              if (size)
                {
                  if (!tlbl && !regalloc_dry_run)
                    tlbl = newiTempLabel (NULL);
                  emitBranch ("bne", tlbl);
                }
            }
          else
            {
              loadRegFromAop (mc6800_reg_a, AOP (left), offset);
              accopWithAop ("ora", mc6800_reg_a, AOP (right), offset);
              mc6800_freeReg (mc6800_reg_a);
              if (size)
                {
                  if (!tlbl && !regalloc_dry_run)
                    tlbl = newiTempLabel (NULL);
                  emitBranch ("bne", tlbl);
                }
            }
          offset++;
        }
      if (tlbl)
        mc6800_emitLabel (tlbl);

      pullOrFreeReg (mc6800_reg_b, needpullb);
      pullOrFreeReg (mc6800_reg_a, needpulla);

      if (ifx)
        genIfxJump (ifx, "a");

      goto release;
    }

  if (AOP_TYPE (right) == AOP_LIT)
    lit = ullFromVal (AOP (right)->aopu.aop_lit);

  size = AOP_SIZE (result);

  if (AOP_SIZE (result) == 1 && !IS_AOP_WITH_B (AOP (right))
  &&  !aopIsLitVal (right->aop, 0, 1, 0x00) && !aopIsLitVal (right->aop, 0, 1, 0xff)
  &&  (IS_AOP_B (AOP (result)) || (IS_AOP_B (AOP (left)) && mc6800_reg_b->isDead)))
    {
      loadRegFromAop (mc6800_reg_b, AOP (left), 0);
      accopWithAop ("ora", mc6800_reg_b, AOP (right), 0);
      mc6800_dirtyReg (mc6800_reg_b, false);
      storeRegToAop (mc6800_reg_b, AOP (result), 0);
      goto release;
    }

  needpulla = pushRegIfSurv (mc6800_reg_a);
  if (IS_AOP_D (AOP (left)))
    needpullb = pushRegIfSurv (mc6800_reg_b);

  offset = 0;
  while (size--)
    {
      if (earlystore && offset == 1)
        pullReg (mc6800_reg_a);
      if (aopIsLitVal (right->aop, offset, 1, 0xff))
        {
          if (isOperandVolatile (left, false))
            {
              loadRegFromAop (mc6800_reg_a, AOP (left), offset);
              mc6800_freeReg (mc6800_reg_a);
            }
          transferAopAop (AOP (right), offset, AOP (result), offset);
        }
      else if (aopIsLitVal (right->aop, offset, 1, 0x00))
        transferAopAop (left->aop, offset, result->aop, offset);
      else if (aopIsLitVal (left->aop, offset, 1, 0x00))
        transferAopAop (right->aop, offset, result->aop, offset);
      else if (IS_AOP_D (AOP (left)) && offset == 0)
        {
          accopWithAop ("ora", mc6800_reg_b, AOP (right), offset);
          mc6800_dirtyReg (mc6800_reg_b, false);
          storeRegToAop (mc6800_reg_b, AOP (result), offset);
        }
      else
        {
          loadRegFromAop (mc6800_reg_a, AOP (left), offset);
          accopWithAop ("ora", mc6800_reg_a, AOP (right), offset);
          storeRegToAop (mc6800_reg_a, AOP (result), offset);
          mc6800_freeReg (mc6800_reg_a);
        }
      if (AOP_TYPE (result) == AOP_REG && size && AOP (result)->aopu.aop_reg[offset]->rIdx == A_IDX)
        {
          pushReg (mc6800_reg_a, true);
          needpulla = true;
        }
      offset++;
    }

  pullOrFreeReg (mc6800_reg_b, needpullb);
  pullOrFreeReg (mc6800_reg_a, needpulla);

release:
  freeAsmop (left, NULL, ic, true);
  freeAsmop (right, NULL, ic, true);
  freeAsmop (result, NULL, ic, true);
}

/*-----------------------------------------------------------------*/
/* genXor - code for Exclusive or                                  */
/*-----------------------------------------------------------------*/
static void
genXor (iCode * ic, iCode * ifx)
{
  operand *left, *right, *result;
  int size, offset = 0;
  bool needpulla = false;
  bool needpullb = false;
  bool earlystore = false;

  D (emitcode (";     genXor", ""));

  aopOp ((left = IC_LEFT (ic)), ic, false);
  aopOp ((right = IC_RIGHT (ic)), ic, false);
  aopOp ((result = IC_RESULT (ic)), ic, true);

#ifdef DEBUG_TYPE
  DD (emitcode ("", "; Type res[%d] = l[%d]^r[%d]", AOP_TYPE (result), AOP_TYPE (left), AOP_TYPE (right)));
  DD (emitcode ("", "; Size res[%d] = l[%d]^r[%d]", AOP_SIZE (result), AOP_SIZE (left), AOP_SIZE (right)));
#endif

  /* if left is a literal & right is not then exchange them */
  if (AOP_TYPE (left) == AOP_LIT && AOP_TYPE (right) != AOP_LIT)
    {
      operand *tmp = right;
      right = left;
      left = tmp;
    }

  /* if right is in registers & left is not in A then exchange them */
  if (AOP_TYPE (right) == AOP_REG && !IS_AOP_WITH_A (AOP (left)))
    {
      operand *tmp = right;
      right = left;
      left = tmp;
    }

  if (AOP_TYPE (result) != AOP_CRY && AOP_SIZE (result) == 1 && !IS_AOP_WITH_B (AOP (right))
  &&  !aopIsLitVal (right->aop, 0, 1, 0x00)
  &&  (IS_AOP_B (AOP (result)) || (IS_AOP_B (AOP (left)) && mc6800_reg_b->isDead)))
    {
      loadRegFromAop (mc6800_reg_b, AOP (left), 0);
      if (aopIsLitVal (right->aop, 0, 1, 0xff))
        rmwWithReg ("com", mc6800_reg_b);
      else
        accopWithAop ("eor", mc6800_reg_b, AOP (right), 0);
      mc6800_dirtyReg (mc6800_reg_b, false);
      storeRegToAop (mc6800_reg_b, AOP (result), 0);
      goto release;
    }

  needpulla = pushRegIfSurv (mc6800_reg_a);
  if (IS_AOP_D (AOP (left)))
    needpullb = pushRegIfSurv (mc6800_reg_b);

  if (AOP_TYPE (result) == AOP_CRY)
    {
      symbol *tlbl;

      tlbl = (regalloc_dry_run ? 0 : newiTempLabel (NULL));
      size = (AOP_SIZE (left) >= AOP_SIZE (right)) ? AOP_SIZE (left) : AOP_SIZE (right);
      offset = 0;
      while (size--)
        {
          if (IS_AOP_D (AOP (left)) && offset == 0)
            {
              if (AOP_TYPE (right) == AOP_LIT && ((ullFromVal (AOP (right)->aopu.aop_lit) >> (offset * 8)) & 0xff) == 0)
                {
                  mc6800_emitOp ("tstb", MODE_INH, "");
                }
              else
                {
                  accopWithAop ("eor", mc6800_reg_b, AOP (right), offset);
                  mc6800_dirtyReg (mc6800_reg_b, false);
                }
            }
          else
            {
              loadRegFromAop (mc6800_reg_a, AOP (left), offset);
              if (AOP_TYPE (right) == AOP_LIT && ((ullFromVal (AOP (right)->aopu.aop_lit) >> (offset * 8)) & 0xff) == 0)
                {
                  mc6800_emitOp ("tsta", MODE_INH, "");
                }
              else
                accopWithAop ("eor", mc6800_reg_a, AOP (right), offset);

              mc6800_freeReg (mc6800_reg_a);
            }
          if (size)
            emitBranch ("bne", tlbl);
          else
            {
              /*
               * I think this is all broken here, (see simulation mismatch in bug1875933.c)
               *   multiple calls to emitLabel() ?!
               * and we can't genIfxJump, if there is none
               */
              if (!regalloc_dry_run)
                mc6800_emitLabel (tlbl);
              pullOrFreeReg (mc6800_reg_b, needpullb);
              pullOrFreeReg (mc6800_reg_a, needpulla);
              if (ifx)
                genIfxJump (ifx, "a");
            }
          offset++;
        }
      goto release;
    }

  size = AOP_SIZE (result);
  offset = 0;
  while (size--)
    {
      if (earlystore && offset == 1)
        pullReg (mc6800_reg_a);
      if (IS_AOP_D (AOP (left)) && offset == 0)
        {
          if (aopIsLitVal (right->aop, offset, 1, 0xff))
            rmwWithReg ("com", mc6800_reg_b);
          else if (!aopIsLitVal (right->aop, offset, 1, 0x00))
            {
              accopWithAop ("eor", mc6800_reg_b, right->aop, offset);
              mc6800_dirtyReg (mc6800_reg_b, false);
            }
          storeRegToAop (mc6800_reg_b, AOP (result), offset);
          offset++;
          continue;
        }
      loadRegFromAop (mc6800_reg_a, AOP (left), offset);
      if (aopIsLitVal (right->aop, offset, 1, 0xff))
        rmwWithReg ("com", mc6800_reg_a);
      else if (!aopIsLitVal (right->aop, offset, 1, 0x00))
        accopWithAop ("eor", mc6800_reg_a, right->aop, offset);
      storeRegToAop (mc6800_reg_a, AOP (result), offset);
      if (AOP_TYPE (result) == AOP_REG && size && AOP (result)->aopu.aop_reg[offset]->rIdx == A_IDX)
        {
          pushReg (mc6800_reg_a, true);
          needpulla = true;
        }
      mc6800_freeReg (mc6800_reg_a);
      offset++;
    }

  pullOrFreeReg (mc6800_reg_b, needpullb);
  pullOrFreeReg (mc6800_reg_a, needpulla);

release:

  freeAsmop (left, NULL, ic, true);
  freeAsmop (right, NULL, ic, true);
  freeAsmop (result, NULL, ic, true);
}

static const char *
expand_symbols (iCode * ic, const char *inlin)
{
  const char *begin = NULL, *p = inlin;
  bool inIdent = false;
  struct dbuf_s dbuf;

  dbuf_init (&dbuf, 128);

  while (*p)
    {
      if (inIdent)
        {
          if ('_' == *p || isalnum (*p))
            /* in the middle of identifier */
            ++p;
          else
            {
              /* end of identifier */
              symbol *sym, *tempsym;
              char *symname = Safe_strndup (p + 1, p - begin - 1);

              inIdent = 0;

              tempsym = newSymbol (symname, ic->level);
              tempsym->block = ic->block;
              sym = (symbol *) findSymWithLevel (SymbolTab, tempsym);
              if (!sym)
                {
                  dbuf_append (&dbuf, begin, p - begin);
                }
              else
                {
                  asmop *aop = aopForSym (ic, sym, false);
                  const char *l = aopAdrStr (aop, aop->size - 1, true);

                  if ('#' == *l)
                    l++;
                  sym->isref = 1;
                  if (sym->level && !sym->allocreq && !sym->ismyparm)
                    {
                      werror (E_ID_UNDEF, sym->name);
                      werror (W_CONTINUE,
                              "  Add 'volatile' to the variable declaration so that it\n"
                              "  can be referenced within inline assembly");
                    }
                  dbuf_append_str (&dbuf, l);
                }
              Safe_free (symname);
              begin = p++;
            }
        }
      else if ('_' == *p)
        {
          /* begin of identifier */
          inIdent = true;
          if (begin)
            dbuf_append (&dbuf, begin, p - begin);
          begin = p++;
        }
      else
        {
          if (!begin)
            begin = p;
          p++;
        }
    }

  if (begin)
    dbuf_append (&dbuf, begin, p - begin);

  return dbuf_detach_c_str (&dbuf);
}

/*-----------------------------------------------------------------*/
/* genInline - write the inline code out                           */
/*-----------------------------------------------------------------*/
static void
mc6800_genInline (iCode * ic)
{
  char *buf, *bp, *begin;
  const char *expanded;
  bool inComment = false;

  D (emitcode (";", "genInline"));

  genLine.lineElement.isInline += (!options.asmpeep);

  buf = bp = begin = Safe_strdup (IC_INLINE (ic));

  /* emit each line as a code */
  while (*bp)
    {
      switch (*bp)
        {
        case ';':
          inComment = true;
          ++bp;
          break;

        case '\x87':
        case '\n':
          inComment = false;
          *bp++ = '\0';
          expanded = expand_symbols (ic, begin);
          emitcode (expanded, NULL);
          dbuf_free (expanded);
          begin = bp;
          break;

        default:
          /* Add \n for labels, not dirs such as c:\mydir */
          if (!inComment && (*bp == ':') && (isspace ((unsigned char) bp[1])))
            {
              ++bp;
              *bp = '\0';
              ++bp;
              emitcode (begin, NULL);
              begin = bp;
            }
          else
            ++bp;
          break;
        }
    }
  if (begin != bp)
    {
      expanded = expand_symbols (ic, begin);
      emitcode (expanded, NULL);
      dbuf_free (expanded);
    }

  Safe_free (buf);

  /* consumed; we can free it here */
  dbuf_free (IC_INLINE (ic));

  genLine.lineElement.isInline -= (!options.asmpeep);
}

/*-----------------------------------------------------------------*/
/* genRRC - rotate right with carry                                */
/*-----------------------------------------------------------------*/
static void
genRRC (iCode * ic)
{
  operand *left, *result;
  int size, offset;
  bool needpull;

  D (emitcode (";     genRRC", ""));

  left = IC_LEFT (ic);
  result = IC_RESULT (ic);
  aopOp (left, ic, false);
  aopOp (result, ic, false);

  size = AOP_SIZE (result);

  if (size == 2)
    {
      symbol *tlbl = regalloc_dry_run ? 0 : newiTempLabel (NULL);

      needpull = pushRegIfSurv (mc6800_reg_d);
      loadRegFromAop (mc6800_reg_d, AOP (left), 0);
      mc6800_emitOp ("lsra", MODE_INH, "");
      mc6800_emitOp ("rorb", MODE_INH, "");
      emitBranch ("bcc", tlbl);
      mc6800_emitOp ("oraa", MODE_IMM, "#0x80");
      if (!regalloc_dry_run)
        mc6800_emitLabel (tlbl);
      mc6800_dirtyReg (mc6800_reg_d, false);
      storeRegToAop (mc6800_reg_d, AOP (result), 0);
      pullOrFreeReg (mc6800_reg_d, needpull);
    }
  else
    {
      needpull = pushRegIfSurv (mc6800_reg_a);
      loadRegFromAop (mc6800_reg_a, AOP (left), 0);
      mc6800_emitOp ("lsra", MODE_INH, "");
      mc6800_dirtyReg (mc6800_reg_a, false);
      for (offset = size - 1; offset >= 0; offset--)
        {
          loadRegFromAop (mc6800_reg_a, AOP (left), offset);
          mc6800_emitOp ("rora", MODE_INH, "");
          mc6800_dirtyReg (mc6800_reg_a, false);
          storeRegToAop (mc6800_reg_a, AOP (result), offset);
        }
      pullOrFreeReg (mc6800_reg_a, needpull);
    }

  freeAsmop (left, NULL, ic, true);
  freeAsmop (result, NULL, ic, true);
}

/*-----------------------------------------------------------------*/
/* genRLC - generate code for rotate left with carry               */
/*-----------------------------------------------------------------*/
static void
genRLC (iCode * ic)
{
  operand *left, *result;
  int size, offset;
  bool needpull;

  D (emitcode (";     genRLC", ""));

  left = IC_LEFT (ic);
  result = IC_RESULT (ic);
  aopOp (left, ic, false);
  aopOp (result, ic, false);

  size = AOP_SIZE (result);

  if (size == 2)
    {
      needpull = pushRegIfSurv (mc6800_reg_d);
      loadRegFromAop (mc6800_reg_d, AOP (left), 0);
      mc6800_emitOp ("aslb", MODE_INH, "");
      mc6800_emitOp ("rola", MODE_INH, "");
      mc6800_emitOp ("adcb", MODE_IMM, "#0x00");
      mc6800_dirtyReg (mc6800_reg_d, false);
      storeRegToAop (mc6800_reg_d, AOP (result), 0);
      pullOrFreeReg (mc6800_reg_d, needpull);
    }
  else
    {
      needpull = pushRegIfSurv (mc6800_reg_a);
      loadRegFromAop (mc6800_reg_a, AOP (left), size - 1);
      mc6800_emitOp ("asla", MODE_INH, "");
      mc6800_dirtyReg (mc6800_reg_a, false);
      for (offset = 0; offset < size; offset++)
        {
          loadRegFromAop (mc6800_reg_a, AOP (left), offset);
          mc6800_emitOp ("rola", MODE_INH, "");
          mc6800_dirtyReg (mc6800_reg_a, false);
          storeRegToAop (mc6800_reg_a, AOP (result), offset);
        }
      pullOrFreeReg (mc6800_reg_a, needpull);
    }

  freeAsmop (left, NULL, ic, true);
  freeAsmop (result, NULL, ic, true);
}

/*-----------------------------------------------------------------*/
/* genGetAbit - generates code get a single bit                    */
/*-----------------------------------------------------------------*/
static void
genGetAbit (iCode * ic)
{
  operand *left, *right, *result;
  int shCount;
  bool needpull;
  reg_info *reg;

  D (emitcode (";     genGetAbit", ""));

  left = IC_LEFT (ic);
  right = IC_RIGHT (ic);
  result = IC_RESULT (ic);
  aopOp (left, ic, false);
  aopOp (right, ic, false);
  aopOp (result, ic, false);

  shCount = (int) ulFromVal (AOP (IC_RIGHT (ic))->aopu.aop_lit);

  if (AOP_TYPE (result) == AOP_REG
      && (AOP (result)->aopu.aop_reg[0] == mc6800_reg_a || AOP (result)->aopu.aop_reg[0] == mc6800_reg_b))
    reg = AOP (result)->aopu.aop_reg[0];
  else if (!mc6800_reg_b->isFree && mc6800_reg_a->isFree)
    reg = mc6800_reg_a;
  else
    reg = mc6800_reg_b;

  needpull = pushRegIfSurv (reg);

  /* get the needed byte into Acc */
  loadRegFromAop (reg, AOP (left), shCount / 8);
  shCount %= 8;
  if (AOP_TYPE (result) == AOP_CRY)
    mc6800_emitOpWithAcc ("bit", reg, MODE_IMM, "#0x%02x", 1 << shCount);
  else
    {
      while (shCount--)
        mc6800_emitOpWithAcc ("lsr", reg, MODE_INH, "");
      mc6800_emitOpWithAcc ("and", reg, MODE_IMM, "#0x01");
      mc6800_dirtyReg (reg, false);
      storeRegToFullAop (reg, AOP (result), false);
    }
  pullOrFreeReg (reg, needpull);

  freeAsmop (result, NULL, ic, true);
  freeAsmop (right, NULL, ic, true);
  freeAsmop (left, NULL, ic, true);
}

/*-----------------------------------------------------------------*/
/* genGetByte - generates code get a single byte                   */
/*-----------------------------------------------------------------*/
static void
genGetByte (iCode * ic)
{
  operand *left, *right, *result;
  int offset;

  D (emitcode (";", "genGetByte"));

  left = IC_LEFT (ic);
  right = IC_RIGHT (ic);
  result = IC_RESULT (ic);
  aopOp (left, ic, false);
  aopOp (right, ic, false);
  aopOp (result, ic, false);

  offset = (int) ulFromVal (AOP (right)->aopu.aop_lit) / 8;
  transferAopAop (AOP (left), offset, AOP (result), 0);

  freeAsmop (result, NULL, ic, true);
  freeAsmop (right, NULL, ic, true);
  freeAsmop (left, NULL, ic, true);
}

/*-----------------------------------------------------------------*/
/* genGetWord - generates code get two bytes                       */
/*-----------------------------------------------------------------*/
static void
genGetWord (iCode * ic)
{
  operand *left, *right, *result;
  int offset;

  D (emitcode (";", "genGetWord"));

  left = IC_LEFT (ic);
  right = IC_RIGHT (ic);
  result = IC_RESULT (ic);
  aopOp (left, ic, false);
  aopOp (right, ic, false);
  aopOp (result, ic, false);

  offset = (int) ulFromVal (AOP (right)->aopu.aop_lit) / 8;
  transferAopAop (AOP (left), offset + 1, AOP (result), 1);
  transferAopAop (AOP (left), offset, AOP (result), 0);

  freeAsmop (result, NULL, ic, true);
  freeAsmop (right, NULL, ic, true);
  freeAsmop (left, NULL, ic, true);
}

/*-----------------------------------------------------------------*/
/* genSwap - generates code to swap nibbles or bytes               */
/*-----------------------------------------------------------------*/
static void
genSwap (iCode * ic)
{
  operand *left, *result;
  bool needpull;

  D (emitcode (";     genSwap", ""));

  left = IC_LEFT (ic);
  result = IC_RESULT (ic);
  aopOp (left, ic, false);
  aopOp (result, ic, false);

  needpull = pushRegIfSurv (mc6800_reg_d);
  if (IS_AOP_D (AOP (left)))
    {
      mc6800_emitOp ("psha", MODE_INH, "");
      mc6800_emitOp ("tba", MODE_INH, "");
      mc6800_emitOp ("pulb", MODE_INH, "");
    }
  else
    {
      loadRegFromAop (mc6800_reg_a, AOP (left), 0);
      loadRegFromAop (mc6800_reg_b, AOP (left), 1);
    }
  mc6800_dirtyReg (mc6800_reg_d, false);
  storeRegToAop (mc6800_reg_d, AOP (result), 0);
  pullOrFreeReg (mc6800_reg_d, needpull);

  freeAsmop (left, NULL, ic, true);
  freeAsmop (result, NULL, ic, true);
}

/*-----------------------------------------------------------------*/
/* AccRol - rotate left accumulator by known count                 */
/*-----------------------------------------------------------------*/
static void
AccRol (reg_info *reg, int shCount)
{
  D (emitcode (";     AccRol", ""));
  shCount &= 0x0007;
  if (shCount <= ((optimize.codeSize && !optimize.codeSpeed) ? 4 : 5))
    {
      while (shCount--)
        {
          mc6800_emitOpWithAcc ("asl", reg, MODE_INH, "");
          mc6800_emitOpWithAcc ("adc", reg, MODE_IMM, "#0x00");
        }
    }
  else
    {
      for (shCount = 8 - shCount; shCount; shCount--)
        {
          if (optimize.codeSize && !optimize.codeSpeed)
            {
              mc6800_emitOpWithAcc ("psh", reg, MODE_INH, "");
              mc6800_emitOpWithAcc ("lsr", reg, MODE_INH, "");
              mc6800_emitOpWithAcc ("pul", reg, MODE_INH, "");
              mc6800_emitOpWithAcc ("ror", reg, MODE_INH, "");
            }
          else
            {
              symbol *tlbl = regalloc_dry_run ? 0 : newiTempLabel (NULL);

              mc6800_emitOpWithAcc ("lsr", reg, MODE_INH, "");
              emitBranch ("bcc", tlbl);
              mc6800_emitOpWithAcc ("ora", reg, MODE_IMM, "#0x80");
              if (!regalloc_dry_run)
                mc6800_emitLabel (tlbl);
            }
        }
    }
  mc6800_dirtyReg (reg, false);
}

/*-----------------------------------------------------------------*/
/* AccLsh - left shift accumulator by known count                  */
/*-----------------------------------------------------------------*/
static void
AccLsh (reg_info *reg, int shCount)
{
  int i;

  shCount &= 0x0007;            // shCount : 0..7

  /* For shift counts of 6 and 7, the unrolled loop is never optimal.      */
  switch (shCount)
    {
    case 6:
      mc6800_emitOpWithAcc ("ror", reg, MODE_INH, "");
      mc6800_emitOpWithAcc ("ror", reg, MODE_INH, "");
      mc6800_emitOpWithAcc ("ror", reg, MODE_INH, "");
      mc6800_emitOpWithAcc ("and", reg, MODE_IMM, "#0xc0");
      /* total: 8 cycles, 5 bytes */
      mc6800_dirtyReg (reg, false);
      return;
    case 7:
      mc6800_emitOpWithAcc ("ror", reg, MODE_INH, "");
      mc6800_emitOpWithAcc ("lda", reg, MODE_IMM, "#0");
      mc6800_emitOpWithAcc ("ror", reg, MODE_INH, "");
      /* total: 6 cycles, 4 bytes */
      mc6800_dirtyReg (reg, false);
      return;
    }

  /* asla and aslb are 2 cycles and 1 byte each, so an unrolled   */
  /* loop is used for the shift counts below 6.                   */
  for (i = 0; i < shCount; i++)
    mc6800_emitOpWithAcc ("asl", reg, MODE_INH, "");
  mc6800_dirtyReg (reg, false);
}


/*-----------------------------------------------------------------*/
/* AccSRsh - signed right shift accumulator by known count         */
/*-----------------------------------------------------------------*/
static void
AccSRsh (reg_info *reg, int shCount)
{
  int i;

  shCount &= 0x0007;            // shCount : 0..7

  if (shCount == 7)
    {
      mc6800_emitOpWithAcc ("rol", reg, MODE_INH, "");
      mc6800_emitOpWithAcc ("lda", reg, MODE_IMM, "#0x00");
      mc6800_emitOpWithAcc ("sbc", reg, MODE_IMM, "#0x00");
      /* total: 6 cycles, 5 bytes */
      mc6800_dirtyReg (reg, false);
      return;
    }

  for (i = 0; i < shCount; i++)
    mc6800_emitOpWithAcc ("asr", reg, MODE_INH, "");
  mc6800_dirtyReg (reg, false);
}

/*-----------------------------------------------------------------*/
/* AccRsh - right shift accumulator by known count                 */
/*-----------------------------------------------------------------*/
static void
AccRsh (reg_info *reg, int shCount, bool sign)
{
  int i;

  if (sign)
    {
      AccSRsh (reg, shCount);
      return;
    }

  shCount &= 0x0007;            // shCount : 0..7

  /* For shift counts of 6 and 7, the unrolled loop is never optimal.      */
  switch (shCount)
    {
    case 6:
      mc6800_emitOpWithAcc ("rol", reg, MODE_INH, "");
      mc6800_emitOpWithAcc ("rol", reg, MODE_INH, "");
      mc6800_emitOpWithAcc ("rol", reg, MODE_INH, "");
      mc6800_emitOpWithAcc ("and", reg, MODE_IMM, "#0x03");
      /* total: 8 cycles, 5 bytes */
      mc6800_dirtyReg (reg, false);
      return;
    case 7:
      mc6800_emitOpWithAcc ("rol", reg, MODE_INH, "");
      mc6800_emitOpWithAcc ("rol", reg, MODE_INH, "");
      mc6800_emitOpWithAcc ("and", reg, MODE_IMM, "#0x01");
      /* total: 6 cycles, 4 bytes */
      mc6800_dirtyReg (reg, false);
      return;
    }

  /* lsra and lsrb are 2 cycles and 1 byte each, so an unrolled   */
  /* loop is used for the shift counts below 6.                   */ /* TODO */
  for (i = 0; i < shCount; i++)
    mc6800_emitOpWithAcc ("lsr", reg, MODE_INH, "");
  mc6800_dirtyReg (reg, false);
}


/*-----------------------------------------------------------------*/
/* movLeft2Result - move byte from left to result                  */
/*-----------------------------------------------------------------*/
static void
movLeft2Result (operand * left, int offl, operand * result, int offr, int sign)
{
  if (!sameRegs (AOP (left), AOP (result)) || (offl != offr))
    {
      transferAopAop (AOP (left), offl, AOP (result), offr);
    }
}




/*-----------------------------------------------------------------*/
/* shiftRLeftOrResult - shift right one byte from left,or to result */
/*-----------------------------------------------------------------*/
static void
shiftRLeftOrResult (operand * left, int offl, operand * result, int offr, int shCount)
{
  bool needpula;

  if (!IS_AOP_D (AOP (left)) && !IS_AOP_A (AOP (left)))
    needpula = pushRegIfUsed (mc6800_reg_a);
  else
    needpula = false;

  loadRegFromAop (mc6800_reg_a, AOP (left), offl);
  /* shift left accumulator */
  AccRsh (mc6800_reg_a, shCount, false);
  /* or with result */
  accopWithAop ("ora", mc6800_reg_a, AOP (result), offr);
  /* back to result */
  storeRegToAop (mc6800_reg_a, AOP (result), offr);

  pullOrFreeReg (mc6800_reg_a, needpula);
}

/*-----------------------------------------------------------------*/
/* genlshOne - left shift a one byte quantity by known count       */
/*-----------------------------------------------------------------*/
static void
genlshOne (operand * result, operand * left, int shCount)
{
  sym_link *resulttype = operandType (result);
  unsigned bytemask = (IS_BITINT (resulttype) && SPEC_USIGN (resulttype) && (SPEC_BITINTWIDTH (resulttype) % 8)) ?
    (0xff >> (8 - SPEC_BITINTWIDTH (resulttype) % 8)) : 0xff;
  bool maskedbyte = (bytemask != 0xff);
  bool needpull;
  reg_info *reg;

  D (emitcode (";     genlshOne", ""));

  if (AOP_TYPE (result) == AOP_REG
      && (AOP (result)->aopu.aop_reg[0] == mc6800_reg_a || AOP (result)->aopu.aop_reg[0] == mc6800_reg_b))
    reg = AOP (result)->aopu.aop_reg[0];
  else if (!mc6800_reg_b->isFree && mc6800_reg_a->isFree)
    reg = mc6800_reg_a;
  else
    reg = mc6800_reg_b;

  needpull = pushRegIfSurv (reg);
  loadRegFromAop (reg, AOP (left), LSB);
  AccLsh (reg, shCount); // Shift left accumulator.
  if (maskedbyte)
    mc6800_emitOpWithAcc ("and", reg, MODE_IMM, "#0x%02x", bytemask);
  storeRegToAop (reg, AOP (result), LSB);
  pullOrFreeReg (reg, needpull);
}

/*-----------------------------------------------------------------*/
/* genlshTwo - left shift two bytes by known amount != 0           */
/*-----------------------------------------------------------------*/
static void
genlshTwo (operand *result, operand *left, int shCount)
{
  int size, i;
  bool needpulla, needpullb;

  sym_link *resulttype = operandType (result);
  unsigned topbytemask = (IS_BITINT (resulttype) && SPEC_USIGN (resulttype) && (SPEC_BITINTWIDTH (resulttype) % 8)) ?
    (0xff >> (8 - SPEC_BITINTWIDTH (resulttype) % 8)) : 0xff;
  bool maskedtopbyte = (topbytemask != 0xff);

  D (emitcode (";     genlshTwo", ""));

  size = getDataSize (result);

  /* if shCount >= 8 */
  if (shCount >= 8)
    {
      shCount -= 8;

      needpulla = pushRegIfSurv (mc6800_reg_a);
      if (size > 1)
        {
          loadRegFromAop (mc6800_reg_a, AOP (left), 0);
          AccLsh (mc6800_reg_a, shCount);
          if (maskedtopbyte)
            {
              mc6800_emitOp ("anda", MODE_IMM, "#0x%02x", topbytemask);
            }
          storeRegToAop (mc6800_reg_a, AOP (result), 1);
        }
      storeConstToAop (0, AOP (result), LSB);
      pullOrFreeReg (mc6800_reg_a, needpulla);
    }

  /*  1 <= shCount <= 7 */
  else
    {
      needpulla = pushRegIfSurv (mc6800_reg_a);
      needpullb = pushRegIfSurv (mc6800_reg_b);
      loadRegFromAop (mc6800_reg_d, AOP (left), 0);
      for (i = 0; i < shCount; i++)
        {
          rmwWithReg ("asl", mc6800_reg_b);
          rmwWithReg ("rol", mc6800_reg_a);
        }
      if (maskedtopbyte)
        {
          mc6800_emitOp ("anda", MODE_IMM, "#0x%02x", topbytemask);
        }
      storeRegToFullAop (mc6800_reg_d, AOP (result), 0);
      pullOrFreeReg (mc6800_reg_b, needpullb);
      pullOrFreeReg (mc6800_reg_a, needpulla);
    }
}

/*-----------------------------------------------------------------*/
/* genlshFour - shift four byte by a known amount != 0             */
/*-----------------------------------------------------------------*/
static void
genlshFour (operand * result, operand * left, int shCount)
{
  int size, offset, i;
  char *shift;

  sym_link *resulttype = operandType (result);
  unsigned topbytemask = (IS_BITINT (resulttype) && SPEC_USIGN (resulttype) && (SPEC_BITINTWIDTH (resulttype) % 8)) ?
    (0xff >> (8 - SPEC_BITINTWIDTH (resulttype) % 8)) : 0xff;
  bool maskedtopbyte = (topbytemask != 0xff);

  D (emitcode (";     genlshFour", ""));

  size = AOP_SIZE (result);

  for (offset = size - 1; offset >= 0; offset--)
    {
      if (offset >= shCount / 8)
        transferAopAop (AOP (left), offset - shCount / 8, AOP (result), offset);
      else
        storeConstToAop (0, AOP (result), offset);
    }

  for (i = shCount % 8; i > 0; i--)
    {
      shift = "asl";
      for (offset = shCount / 8; offset < size; offset++)
        {
          rmwWithAop (shift, AOP (result), offset);
          shift = "rol";
        }
    }

  if (maskedtopbyte)
    {
      bool needpulla = pushRegIfUsed (mc6800_reg_a);

      loadRegFromAop (mc6800_reg_a, AOP (result), size - 1);
      mc6800_emitOp ("anda", MODE_IMM, "#0x%02x", topbytemask);
      storeRegToAop (mc6800_reg_a, AOP (result), size - 1);
      pullOrFreeReg (mc6800_reg_a, needpulla);
    }
}

/*-----------------------------------------------------------------*/
/* genRot1 - generates code for rotation of 8-bit values           */
/*-----------------------------------------------------------------*/
static void
genRot1 (iCode *ic)
{
  operand *left = IC_LEFT (ic);
  operand *right = IC_RIGHT (ic);
  operand *result = IC_RESULT (ic);

  aopOp (left, ic, false);
  aopOp (result, ic, false);

  wassert (bitsForType (operandType (left)) == 8);
  wassert (IS_OP_LITERAL (right));

  int s = operandLitValueUll (right) % 8;

  bool needpulla = pushRegIfSurv (mc6800_reg_a);
  loadRegFromAop (mc6800_reg_a, left->aop, 0);
  AccRol (mc6800_reg_a, s);
  storeRegToAop (mc6800_reg_a, result->aop, 0);
  pullOrFreeReg (mc6800_reg_a, needpulla);

  freeAsmop (result, NULL, ic, true);
  freeAsmop (left, NULL, ic, true);
}

/*-----------------------------------------------------------------*/
/* genRot - generates code for rotation                            */
/*-----------------------------------------------------------------*/
static void
genRot (iCode *ic)
{
  operand *left = IC_LEFT (ic);
  operand *right = IC_RIGHT (ic);
  unsigned int lbits = bitsForType (operandType (left));
  if (lbits == 8)
    genRot1 (ic);
  else if (IS_OP_LITERAL (right) && operandLitValueUll (right) % lbits == 1)
    genRLC (ic);
  else if (IS_OP_LITERAL (right) && operandLitValueUll (right) % lbits ==  lbits - 1)
    genRRC (ic);
  else if (IS_OP_LITERAL (right) && operandLitValueUll (right) %lbits == lbits / 2)
    genSwap (ic);
  else
    wassertl (0, "Unsupported rotation.");
}

/*-----------------------------------------------------------------*/
/* genLeftShiftLiteral - left shifting by known count              */
/*-----------------------------------------------------------------*/
static void
genLeftShiftLiteral (operand * left, operand * right, operand * result, iCode * ic)
{
  int shCount = (int) ulFromVal (AOP (right)->aopu.aop_lit);
  int size;

  D (emitcode (";     genLeftShiftLiteral", ""));

  freeAsmop (right, NULL, ic, true);

  aopOp (left, ic, false);
  aopOp (result, ic, false);

//  size = getSize (operandType (result));
  size = AOP_SIZE (result);

#if VIEW_SIZE
  DD (emitcode ("; shift left ", "result %d, left %d", size, AOP_SIZE (left)));
#endif

  if (shCount == 0)
    {
      genCopy (result, left);
    }
  else if (shCount >= (size * 8))
    {
      while (size--)
        storeConstToAop (0, AOP (result), size);
    }
  else
    {
      switch (size)
        {
        case 1:
          genlshOne (result, left, shCount);
          break;

        case 2:
          genlshTwo (result, left, shCount);
          break;

        case 4:
          genlshFour (result, left, shCount);
          break;

        default:
          werror (E_INTERNAL_ERROR, __FILE__, __LINE__, "*** ack! mystery literal shift!\n");
          fprintf (stderr, "Shift by %d\n", size);
          break;
        }
    }
  freeAsmop (left, NULL, ic, true);
  freeAsmop (result, NULL, ic, true);
}

/*-----------------------------------------------------------------*/
/* genLeftShift - generates code for left shifting                 */
/*-----------------------------------------------------------------*/
static void
genLeftShift (iCode *ic)
{
  operand *left, *right, *result;
  int size, offset;
  symbol *tlbl, *tlbl1;
  char *shift;
  asmop *aopResult;
  bool needpullcountreg = false;
  reg_info *countreg = NULL;
  const char *tmp = NULL;

  D (emitcode (";     genLeftShift", ""));

  right = IC_RIGHT (ic);
  left = IC_LEFT (ic);
  result = IC_RESULT (ic);

  aopOp (right, ic, false);

  /* if the shift count is known then do it
     as efficiently as possible */
  if (AOP_TYPE (right) == AOP_LIT &&
    (getSize (operandType (result)) == 1 || getSize (operandType (result)) == 2 || getSize (operandType (result)) == 4))
    {
      genLeftShiftLiteral (left, right, result, ic);
      return;
    }

  sym_link *resulttype = operandType (result);
  unsigned topbytemask = (IS_BITINT (resulttype) && SPEC_USIGN (resulttype) && (SPEC_BITINTWIDTH (resulttype) % 8)) ?
    (0xff >> (8 - SPEC_BITINTWIDTH (resulttype) % 8)) : 0xff;
  bool maskedtopbyte = (topbytemask != 0xff);

  /* shift count is unknown then we have to form
     a loop get the loop count in B, A or X : Note: we take
     only the lower order byte since shifting
     more that 32 bits make no sense anyway, ( the
     largest size of an object can be only 32 bits ) */

  aopOp (result, ic, false);
  aopOp (left, ic, false);

  wassertl (!IS_AOP_WITH_X (AOP (result)),
            "left shift by a variable count with the result in x is not supported yet");

  if (IS_AOP_X (AOP (right)))
    countreg = (AOP_TYPE (left) != AOP_SOF && AOP_TYPE (result) != AOP_SOF) ? mc6800_reg_x : NULL;
  else if (AOP_TYPE (right) == AOP_REG && AOP_TYPE (result) == AOP_REG
      && (AOP (right)->regmask & AOP (result)->regmask))
    countreg = NULL;
  else if (!IS_AOP_WITH_B (AOP (result)))
    countreg = mc6800_reg_b;
  else if (!IS_AOP_WITH_A (AOP (result)))
    countreg = mc6800_reg_a;

  if (!countreg)
    {
      tmp = allocTemp ();
      if (IS_AOP_X (AOP (right)))
        mc6800_emitOp ("stx", MODE_DIR, "*%s", tmp);
      else if (AOP_TYPE (right) == AOP_REG)
        mc6800_emitOpWithAcc ("sta", AOP (right)->aopu.aop_reg[0], MODE_DIR, "*%s", tmp);
      else
        {
          needpullcountreg = pushRegIfUsed (mc6800_reg_b);
          loadRegFromAop (mc6800_reg_b, AOP (right), 0);
          mc6800_emitOp ("stab", MODE_DIR, "*%s", tmp);
          pullOrFreeReg (mc6800_reg_b, needpullcountreg);
        }
    }

  /* now move the left to the result if they are not the
     same */
  if (!sameRegs (left->aop, AOP (result)))
    {
      size = AOP_SIZE (result);
      offset = 0;
      while (size--)
        {
          transferAopAop (AOP (left), offset, AOP (result), offset);
          offset++;
        }
    }
  freeAsmop (left, NULL, ic, true);

  size = AOP_SIZE (result);
  tlbl = (regalloc_dry_run ? 0 : newiTempLabel (NULL));
  tlbl1 = (regalloc_dry_run ? 0 : newiTempLabel (NULL));

  if (countreg)
    {
      needpullcountreg = pushRegIfSurv (countreg);
      countreg->isFree = false;
      loadRegFromAop (countreg, AOP (right), 0);
      if (countreg == mc6800_reg_x)
        mc6800_emitOp ("cpx", MODE_IMM, "#0");
      else
        {
          mc6800_emitOpWithAcc ("tst", countreg, MODE_INH, "");
        }
      emitBranch ("beq", tlbl1);
    }
  else
    emitBranch ("bra", tlbl1);

  if (!regalloc_dry_run)
    mc6800_emitLabel (tlbl);

  shift = "asl";
  for (offset = 0; offset < size; offset++)
    {
      rmwWithAop (shift, AOP (result), offset);
      shift = "rol";
    }
  if (countreg)
    {
      if (countreg == mc6800_reg_x)
        mc6800_emitOp ("dex", MODE_INH, "");
      else
        rmwWithReg ("dec", countreg);
      emitBranch ("bne", tlbl);
      if (!regalloc_dry_run)
        mc6800_emitLabel (tlbl1);
      pullOrFreeReg (countreg, needpullcountreg);
    }
  else
    {
      if (!regalloc_dry_run)
        mc6800_emitLabel (tlbl1);
      mc6800_emitOp ("dec", MODE_EXT, IS_AOP_X (AOP (right)) ? "%s+1" : "%s", tmp);
      emitBranch ("bpl", tlbl);
      freeTemp ();
    }

  if (maskedtopbyte)
    {
      bool in_a = (result->aop->type == AOP_REG && result->aop->aopu.aop_reg[size - 1]->rIdx == A_IDX);
      bool needpull = false;
      if (!in_a)
        {
          needpull = pushRegIfUsed (mc6800_reg_a);
          loadRegFromAop (mc6800_reg_a, result->aop, size - 1);
        }
      mc6800_emitOp ("anda", MODE_IMM, "#0x%02x", topbytemask);
      if (!in_a)
        {
          storeRegToAop (mc6800_reg_a, result->aop, size - 1);
          pullOrFreeReg (mc6800_reg_a, needpull);
        }
    }
  freeAsmop (result, NULL, ic, true);
  freeAsmop (right, NULL, ic, true);
}

/*-----------------------------------------------------------------*/
/* genrshOne - right shift a one byte quantity by known count      */
/*-----------------------------------------------------------------*/
static void
genrshOne (operand * result, operand * left, int shCount, int sign)
{
  bool needpull;
  reg_info *reg;
  D (emitcode (";     genrshOne", ""));
  if (AOP_TYPE (result) == AOP_REG
      && (AOP (result)->aopu.aop_reg[0] == mc6800_reg_a || AOP (result)->aopu.aop_reg[0] == mc6800_reg_b))
    reg = AOP (result)->aopu.aop_reg[0];
  else if (!mc6800_reg_b->isFree && mc6800_reg_a->isFree)
    reg = mc6800_reg_a;
  else
    reg = mc6800_reg_b;

  needpull = pushRegIfSurv (reg);
  loadRegFromAop (reg, AOP (left), 0);
  AccRsh (reg, shCount, sign);
  storeRegToFullAop (reg, AOP (result), sign);
  pullOrFreeReg (reg, needpull);
}

/*-----------------------------------------------------------------*/
/* genrshTwo - right shift two bytes by known amount != 0          */
/*-----------------------------------------------------------------*/
static void
genrshTwo (operand * result, operand * left, int shCount, int sign)
{
  int i;
  bool needpulla, needpullb;

  D (emitcode (";     genrshTwo", ""));

  if (shCount >= 8)
    {
      needpulla = pushRegIfSurv (mc6800_reg_a);
      loadRegFromAop (mc6800_reg_a, AOP (left), 1);
      AccRsh (mc6800_reg_a, shCount - 8, sign);
      storeRegToFullAop (mc6800_reg_a, AOP (result), sign);
      pullOrFreeReg (mc6800_reg_a, needpulla);
    }
  else
    {
      needpulla = pushRegIfSurv (mc6800_reg_a);
      needpullb = pushRegIfSurv (mc6800_reg_b);
      loadRegFromAop (mc6800_reg_d, AOP (left), 0);
      for (i = 0; i < shCount; i++)
        {
          rmwWithReg (sign ? "asr" : "lsr", mc6800_reg_a);
          rmwWithReg ("ror", mc6800_reg_b);
        }
      storeRegToAop (mc6800_reg_d, AOP (result), 0);
      pullOrFreeReg (mc6800_reg_b, needpullb);
      pullOrFreeReg (mc6800_reg_a, needpulla);
    }
}

/*-----------------------------------------------------------------*/
/* genrshFour - shift four byte by a known amount != 0             */
/*-----------------------------------------------------------------*/
static void
genrshFour (operand * result, operand * left, int shCount, int sign)
{
  int size, offset, i;
  char *shift;

  D (emitcode (";     genrshFour", ""));

  size = AOP_SIZE (result);

  for (offset = 0; offset + shCount / 8 < size; offset++)
    transferAopAop (AOP (left), offset + shCount / 8, AOP (result), offset);

  if (shCount / 8)
    {
      bool needpulla = pushRegIfSurv (mc6800_reg_a);

      loadRegFromAop (mc6800_reg_a, AOP (result), size - shCount / 8 - 1);
      storeRegSignToUpperAop (mc6800_reg_a, AOP (result), size - shCount / 8, sign);
      pullOrFreeReg (mc6800_reg_a, needpulla);
    }

  for (i = shCount % 8; i > 0; i--)
    {
      shift = sign ? "asr" : "lsr";
      for (offset = size - shCount / 8 - 1; offset >= 0; offset--)
        {
          rmwWithAop (shift, AOP (result), offset);
          shift = "ror";
        }
    }
}

/*-----------------------------------------------------------------*/
/* genRightShiftLiteral - right shifting by known count            */
/*-----------------------------------------------------------------*/
static void
genRightShiftLiteral (operand * left, operand * right, operand * result, iCode * ic, int sign)
{
  int shCount = (int) ulFromVal (AOP (right)->aopu.aop_lit);
  int size;

  D (emitcode (";     genRightShiftLiteral", ""));

  freeAsmop (right, NULL, ic, true);

  aopOp (left, ic, false);
  aopOp (result, ic, false);

#if VIEW_SIZE
  DD (emitcode ("; shift right ", "result %d, left %d", AOP_SIZE (result), AOP_SIZE (left)));
#endif

  size = getDataSize (left);
  /* test the LEFT size !!! */

  /* I suppose that the left size >= result size */
  if (shCount == 0)
    {
      genCopy (result, left);
    }
  else if (shCount >= (size * 8))
    {
      bool needpulla = pushRegIfSurv (mc6800_reg_a);
      if (sign)
        {

          /* get sign in acc.7 */
          loadRegFromAop (mc6800_reg_a, AOP (left), size - 1);
        }
      addSign (result, LSB, sign);
      pullOrFreeReg (mc6800_reg_a, needpulla);
    }
  else
    {
      switch (size)
        {
        case 1:
          genrshOne (result, left, shCount, sign);
          break;

        case 2:
          genrshTwo (result, left, shCount, sign);
          break;

        case 4:
          genrshFour (result, left, shCount, sign);
          break;
        default:
          wassertl (0, "Invalid operand size in right shift.");
          break;
        }
    }
  freeAsmop (left, NULL, ic, true);
  freeAsmop (result, NULL, ic, true);
}


/*-----------------------------------------------------------------*/
/* genRightShift - generate code for right shifting                */
/*-----------------------------------------------------------------*/
static void
genRightShift (iCode * ic)
{
  operand *right, *left, *result;
  int size, offset;
  symbol *tlbl, *tlbl1;
  char *shift;
  bool sign;
  bool needpullcountreg = false;
  reg_info *countreg = NULL;
  const char *tmp = NULL;

  D (emitcode (";     genRightShift", ""));

  right = IC_RIGHT (ic);
  left = IC_LEFT (ic);
  result = IC_RESULT (ic);

  sign = !SPEC_USIGN (getSpec (operandType (left)));

  aopOp (right, ic, false);

  if (AOP_TYPE (right) == AOP_LIT &&
      (getSize (operandType (result)) == 1 || getSize (operandType (result)) == 2 || getSize (operandType (result)) == 4))
    {
      genRightShiftLiteral (left, right, result, ic, sign);
      return;
    }

  aopOp (result, ic, false);
  aopOp (left, ic, false);

  wassertl (!IS_AOP_WITH_X (AOP (result)),
            "right shift by a variable count with the result in x is not supported yet");

  if (IS_AOP_X (AOP (right)))
    countreg = (AOP_TYPE (left) != AOP_SOF && AOP_TYPE (result) != AOP_SOF) ? mc6800_reg_x : NULL;
  else if (AOP_TYPE (right) == AOP_REG && AOP_TYPE (result) == AOP_REG
      && (AOP (right)->regmask & AOP (result)->regmask))
    countreg = NULL;
  else if (!IS_AOP_WITH_B (AOP (result)))
    countreg = mc6800_reg_b;
  else if (!IS_AOP_WITH_A (AOP (result)))
    countreg = mc6800_reg_a;

  if (!countreg)
    {
      tmp = allocTemp ();
      if (IS_AOP_X (AOP (right)))
        mc6800_emitOp ("stx", MODE_DIR, "*%s", tmp);
      else if (AOP_TYPE (right) == AOP_REG)
        mc6800_emitOpWithAcc ("sta", AOP (right)->aopu.aop_reg[0], MODE_DIR, "*%s", tmp);
      else
        {
          needpullcountreg = pushRegIfUsed (mc6800_reg_b);
          loadRegFromAop (mc6800_reg_b, AOP (right), 0);
          mc6800_emitOp ("stab", MODE_DIR, "*%s", tmp);
          pullOrFreeReg (mc6800_reg_b, needpullcountreg);
        }
    }

  if (!sameRegs (left->aop, AOP (result)))
    {
      size = AOP_SIZE (result);
      offset = 0;
      while (size--)
        {
          transferAopAop (AOP (left), offset, AOP (result), offset);
          offset++;
        }
    }
  freeAsmop (left, NULL, ic, true);

  size = AOP_SIZE (result);
  tlbl = (regalloc_dry_run ? 0 : newiTempLabel (NULL));
  tlbl1 = (regalloc_dry_run ? 0 : newiTempLabel (NULL));

  if (countreg)
    {
      needpullcountreg = pushRegIfSurv (countreg);
      countreg->isFree = false;
      loadRegFromAop (countreg, AOP (right), 0);
      if (countreg == mc6800_reg_x)
        mc6800_emitOp ("cpx", MODE_IMM, "#0");
      else
        {
          mc6800_emitOpWithAcc ("tst", countreg, MODE_INH, "");
        }
      emitBranch ("beq", tlbl1);
    }
  else
    emitBranch ("bra", tlbl1);

  if (!regalloc_dry_run)
    mc6800_emitLabel (tlbl);

  shift = sign ? "asr" : "lsr";
  for (offset = size - 1; offset >= 0; offset--)
    {
      rmwWithAop (shift, AOP (result), offset);
      shift = "ror";
    }
  if (countreg)
    {
      if (countreg == mc6800_reg_x)
        mc6800_emitOp ("dex", MODE_INH, "");
      else
        rmwWithReg ("dec", countreg);
      emitBranch ("bne", tlbl);
      if (!regalloc_dry_run)
        mc6800_emitLabel (tlbl1);
      pullOrFreeReg (countreg, needpullcountreg);
    }
  else
    {
      if (!regalloc_dry_run)
        mc6800_emitLabel (tlbl1);
      mc6800_emitOp ("dec", MODE_EXT, IS_AOP_X (AOP (right)) ? "%s+1" : "%s", tmp);
      emitBranch ("bpl", tlbl);
      freeTemp ();
    }

  freeAsmop (result, NULL, ic, true);
  freeAsmop (right, NULL, ic, true);
}

static bool
stackBasedOffset (operand * opOffset)
{
  if (!opOffset || !IS_ITEMP (opOffset) || !OP_SYMBOL (opOffset)->remat)
    return false;
  return aopForRemat (OP_SYMBOL (opOffset))->type == AOP_STL;
}

static void
addSPToX (void)
{
  const char *tmp = allocTemp ();
  const char *tmp2 = allocTemp ();
  reg_info *reg = (mc6800_reg_a->isFree || !mc6800_reg_b->isFree) ? mc6800_reg_a : mc6800_reg_b;
  bool savereg = !reg->isFree;

  mc6800_emitOp ("stx", MODE_DIR, "*%s", tmp);
  mc6800_emitOp ("sts", MODE_DIR, "*%s", tmp2);
  if (savereg)
    pushReg (reg, false);
  mc6800_emitOpWithAcc ("lda", reg, MODE_DIR, "*%s+1", tmp);
  mc6800_emitOpWithAcc ("add", reg, MODE_DIR, "*%s+1", tmp2);
  mc6800_emitOpWithAcc ("sta", reg, MODE_DIR, "*%s+1", tmp2);
  mc6800_emitOpWithAcc ("lda", reg, MODE_DIR, "*%s", tmp);
  mc6800_emitOpWithAcc ("adc", reg, MODE_DIR, "*%s", tmp2);
  mc6800_emitOpWithAcc ("sta", reg, MODE_DIR, "*%s", tmp2);
  mc6800_emitOp ("ldx", MODE_DIR, "*%s", tmp2);
  if (savereg)
    pullReg (reg);
  freeTemp ();
  freeTemp ();
  mc6800_dirtyReg (reg, false);
  mc6800_dirtyReg (mc6800_reg_x, false);
}

/*-----------------------------------------------------------------*/
/* decodePointerOffset - decode a pointer offset operand into a    */
/*                    literal offset and a rematerializable offset */
/*-----------------------------------------------------------------*/
static void
decodePointerOffset (operand * opOffset, int * litOffset, char ** rematOffset)
{
  *litOffset = 0;
  *rematOffset = NULL;

  if (!opOffset)
    return;

  if (IS_OP_LITERAL (opOffset))
    {
      *litOffset = (int)operandLitValue (opOffset);
    }
  else if (IS_ITEMP (opOffset) && OP_SYMBOL (opOffset)->remat)
    {
      asmop * aop = aopForRemat (OP_SYMBOL (opOffset));

      if (aop->type == AOP_LIT)
        *litOffset = (int) floatFromVal (aop->aopu.aop_lit);
      else if (aop->type == AOP_IMMD)
        *rematOffset = aop->aopu.aop_immd;
      else if (aop->type == AOP_STL)
        *litOffset = regalloc_dry_run ? 1 : _G.stackOfs + _G.stackPushes + aop->aopu.aop_stk + 1;
    }
  else
    wassertl (0, "Pointer get/set with non-constant offset");
}


/*-----------------------------------------------------------------*/
/* genUnpackBits - generates code for unpacking bits               */
/*-----------------------------------------------------------------*/
static void
genUnpackBits (operand * result, operand * left, operand * right, iCode * ifx)
{
  int offset = 0;               /* result byte offset */
  int rlen = 0;                 /* remaining bitfield length */
  sym_link *etype;              /* bitfield type information */
  unsigned blen;                /* bitfield length */
  unsigned bstr;                /* bitfield starting bit within byte */
  bool needpull = false;
  bool needpullx = false;
  int litOffset = 0;
  char * rematOffset = NULL;
  reg_info *reg;
  asmop *tmpaop = NULL;
  bool delayed = false;
  bool assigned = false;

  D (emitcode (";     genUnpackBits", ""));

  etype = getSpec (operandType (result));
  blen = SPEC_BLEN (etype);
  bstr = SPEC_BSTR (etype);

  if (IS_AOP_A (AOP (result)) || IS_AOP_B (AOP (result)))
    reg = AOP (result)->aopu.aop_reg[0];
  else
    reg = (!mc6800_reg_b->isFree && mc6800_reg_a->isFree) ? mc6800_reg_a : mc6800_reg_b;
  needpull = pushRegIfSurv (reg);

  if (blen >= 8)
    {
      tmpaop = newAsmop (AOP_DIR);
      tmpaop->aopu.aop_dir = (char *) allocTemp ();
      tmpaop->size = (blen + 7) / 8;
    }
  needpullx = pushRegIfSurv (mc6800_reg_x);
  loadRegFromAop (mc6800_reg_x, AOP (left), 0);
  if (stackBasedOffset (right))
    addSPToX ();
  decodePointerOffset (right, &litOffset, &rematOffset);

  if (rematOffset)
    {
      const char *tmp = allocTemp ();
      char ofs[128];

      if (litOffset)
        SNPRINTF (ofs, sizeof (ofs), "(%s+%d)", rematOffset, litOffset);
      else
        SNPRINTF (ofs, sizeof (ofs), "%s", rematOffset);
      litOffset = 0;
      mc6800_emitOp ("stx", MODE_DIR, "*%s", tmp);
      mc6800_emitOpWithAcc ("lda", reg, MODE_DIR, "*%s+1", tmp);
      mc6800_emitOpWithAcc ("add", reg, MODE_IMM, "#%s", ofs);
      mc6800_emitOpWithAcc ("sta", reg, MODE_DIR, "*%s+1", tmp);
      mc6800_emitOpWithAcc ("lda", reg, MODE_DIR, "*%s", tmp);
      mc6800_emitOpWithAcc ("adc", reg, MODE_IMM, "#>%s", ofs);
      mc6800_emitOpWithAcc ("sta", reg, MODE_DIR, "*%s", tmp);
      mc6800_emitOp ("ldx", MODE_DIR, "*%s", tmp);
      freeTemp ();
      mc6800_dirtyReg (reg, false);
      mc6800_dirtyReg (mc6800_reg_x, false);
      rematOffset = NULL;
    }

  if (!rematOffset && litOffset < 0)
    {
      addConstToX (litOffset);
      litOffset = 0;
    }
  else if (!rematOffset && litOffset + (int) ((blen + 7) / 8) - 1 > 0xff)
    {
      addConstToX (litOffset + (int) ((blen + 7) / 8) - 1 - 0xff);
      litOffset -= litOffset + (int) ((blen + 7) / 8) - 1 - 0xff;
    }

  if (blen < 8)
    {
      loadRegIndexed (reg, litOffset, rematOffset);
      pullOrFreeReg (mc6800_reg_x, needpullx);
      if (ifx)
        {
          mc6800_emitOpWithAcc ("and", reg, MODE_IMM, "#0x%02x", (((unsigned char) - 1) >> (8 - blen)) << bstr);
          mc6800_dirtyReg (reg, false);
          goto finish;
        }
      AccRsh (reg, bstr, false);
      mc6800_emitOpWithAcc ("and", reg, MODE_IMM, "#0x%02x", ((unsigned char) - 1) >> (8 - blen));
      mc6800_dirtyReg (reg, false);
      if (!SPEC_USIGN (etype) && !IS_BOOLEAN (etype))
        {
          symbol *tlbl = (regalloc_dry_run ? 0 : newiTempLabel (NULL));

          mc6800_emitOpWithAcc ("bit", reg, MODE_IMM, "#0x%02x", 1 << (blen - 1));
          emitBranch ("beq", tlbl);
          mc6800_emitOpWithAcc ("ora", reg, MODE_IMM, "#0x%02x", (unsigned char) (0xff << blen));
          if (!regalloc_dry_run)
            mc6800_emitLabel (tlbl);
        }
      storeRegToAop (reg, AOP (result), offset);
      goto finish;
    }

  for (offset = 0; offset < (int) ((blen + 7) / 8); offset++)
    {
      loadRegIndexed (reg, litOffset + offset, rematOffset);
      storeRegToAop (reg, tmpaop, tmpaop->size - offset - 1);
    }
  pullOrFreeReg (mc6800_reg_x, needpullx);

  if (ifx)
    {
      offset = (int) ((blen + 7) / 8) - 1;
      loadRegFromAop (reg, tmpaop, tmpaop->size - offset - 1);
      if (blen % 8)
        mc6800_emitOpWithAcc ("and", reg, MODE_IMM, "#0x%02x", ((unsigned char) - 1) >> (8 - blen % 8));
      while (offset--)
        accopWithAop ("ora", reg, tmpaop, tmpaop->size - offset - 1);
      mc6800_dirtyReg (reg, false);
      goto finish;
    }

  offset = 0;
  for (rlen = blen; rlen >= 8; rlen -= 8)
    {
      if (assigned && !delayed)
        {
          pushReg (reg, true);
          delayed = true;
        }
      loadRegFromAop (reg, tmpaop, tmpaop->size - offset - 1);
      storeRegToAop (reg, AOP (result), offset);
      if (AOP_TYPE (result) == AOP_REG && AOP (result)->aopu.aop_reg[offset]->rIdx == reg->rIdx)
        assigned = true;
      offset++;
    }

  if (rlen)
    {
      if (assigned && !delayed)
        {
          pushReg (reg, true);
          delayed = true;
        }
      loadRegFromAop (reg, tmpaop, tmpaop->size - offset - 1);
      mc6800_emitOpWithAcc ("and", reg, MODE_IMM, "#0x%02x", ((unsigned char) - 1) >> (8 - rlen));
      mc6800_dirtyReg (reg, false);
      if (!SPEC_USIGN (etype) && !IS_BOOLEAN (etype))
        {
          symbol *tlbl = (regalloc_dry_run ? 0 : newiTempLabel (NULL));

          mc6800_emitOpWithAcc ("bit", reg, MODE_IMM, "#0x%02x", 1 << (rlen - 1));
          emitBranch ("beq", tlbl);
          mc6800_emitOpWithAcc ("ora", reg, MODE_IMM, "#0x%02x", (unsigned char) (0xff << rlen));
          if (!regalloc_dry_run)
            mc6800_emitLabel (tlbl);
        }
      storeRegToAop (reg, AOP (result), offset);
    }

finish:
  if (tmpaop)
    freeTemp ();
  if (delayed)
    pullReg (reg);
  pullOrFreeReg (reg, needpull);
  if (ifx && !ifx->generated)
    genIfxJump (ifx, "a");
}


/*-----------------------------------------------------------------*/
/* genUnpackBitsImmed - generates code for unpacking bits          */
/*-----------------------------------------------------------------*/
static void
genUnpackBitsImmed (operand * left, operand *right, operand * result, iCode * ic, iCode * ifx)
{
  int size;
  int offset = 0;               /* result byte offset */
  int litOffset = 0;
  char * rematOffset = NULL;
  int rlen = 0;                 /* remaining bitfield length */
  sym_link *etype;              /* bitfield type information */
  unsigned blen;                /* bitfield length */
  unsigned bstr;                /* bitfield starting bit within byte */
  asmop *derefaop;
  reg_info *reg;
  bool delayed = false;
  bool assigned = false;
  bool needpull = false;

  D (emitcode (";     genUnpackBitsImmed", ""));

  decodePointerOffset (right, &litOffset, &rematOffset);
  wassert (rematOffset==NULL);

  aopOp (result, ic, true);
  size = getSize (operandType (result));

  derefaop = aopDerefAop (AOP (left), litOffset);
  freeAsmop (left, NULL, ic, true);
  derefaop->size = size;

  etype = getSpec (operandType (result));
  blen = SPEC_BLEN (etype);
  bstr = SPEC_BSTR (etype);

  if (IS_AOP_A (AOP (result)) || IS_AOP_B (AOP (result)))
    reg = AOP (result)->aopu.aop_reg[0];
  else
    reg = (!mc6800_reg_b->isFree && mc6800_reg_a->isFree) ? mc6800_reg_a : mc6800_reg_b;

  needpull = pushRegIfSurv (reg);

  /* If the bitfield length is less than a byte */
  if (blen < 8)
    {
      loadRegFromAop (reg, derefaop, 0);
      if (!ifx)
        {
          AccRsh (reg, bstr, false);
          mc6800_emitOpWithAcc ("and", reg, MODE_IMM, "#0x%02x", ((unsigned char) - 1) >> (8 - blen));
          mc6800_dirtyReg (reg, false);
          if (!SPEC_USIGN (etype) && !IS_BOOLEAN (etype))
            {
              /* signed bitfield */
              symbol *tlbl = (regalloc_dry_run ? 0 : newiTempLabel (NULL));

              mc6800_emitOpWithAcc ("bit", reg, MODE_IMM, "#0x%02x", 1 << (blen - 1));
              emitBranch ("beq", tlbl);
              mc6800_emitOpWithAcc ("ora", reg, MODE_IMM, "#0x%02x", (unsigned char) (0xff << blen));
              if (!regalloc_dry_run)
                mc6800_emitLabel (tlbl);
            }
          storeRegToAop (reg, AOP (result), offset);
        }
      else
        {
          mc6800_emitOpWithAcc ("and", reg, MODE_IMM, "#0x%02x", (((unsigned char) - 1) >> (8 - blen)) << bstr);
          mc6800_dirtyReg (reg, false);
        }
      goto finish;
    }

  if (ifx)
    {
      offset = (int) ((blen + 7) / 8) - 1;
      loadRegFromAop (reg, derefaop, size - offset - 1);
      if (blen % 8)
        mc6800_emitOpWithAcc ("and", reg, MODE_IMM, "#0x%02x", ((unsigned char) - 1) >> (8 - blen % 8));
      while (offset--)
        accopWithAop ("ora", reg, derefaop, size - offset - 1);
      mc6800_dirtyReg (reg, false);
      goto finish;
    }

  /* Bit field did not fit in a byte. Copy all
     but the partial byte at the end.  */
  for (rlen = blen; rlen >= 8; rlen -= 8)
    {
      if (assigned && !delayed)
        {
          pushReg (reg, true);
          delayed = true;
        }
      loadRegFromAop (reg, derefaop, size - offset - 1);
      storeRegToAop (reg, AOP (result), offset);
      if (AOP_TYPE (result) == AOP_REG && AOP(result)->aopu.aop_reg[offset]->rIdx == reg->rIdx)
        assigned = true;
      offset++;
    }

  /* Handle the partial byte at the end */
  if (rlen)
    {
      if (assigned && !delayed)
        {
          pushReg (reg, true);
          delayed = true;
        }
      loadRegFromAop (reg, derefaop, size - offset - 1);
      mc6800_emitOpWithAcc ("and", reg, MODE_IMM, "#0x%02x", ((unsigned char) - 1) >> (8 - rlen));
      if (!SPEC_USIGN (etype) && !IS_BOOLEAN (etype))
        {
          /* signed bitfield */
          symbol *tlbl = (regalloc_dry_run ? 0 : newiTempLabel (NULL));

          mc6800_emitOpWithAcc ("bit", reg, MODE_IMM, "#0x%02x", 1 << (rlen - 1));
          emitBranch ("beq", tlbl);
          mc6800_emitOpWithAcc ("ora", reg, MODE_IMM, "#0x%02x", (unsigned char) (0xff << rlen));
          if (!regalloc_dry_run)
            mc6800_emitLabel (tlbl);
        }
      storeRegToAop (reg, AOP (result), offset);
    }

finish:
  freeAsmop (NULL, derefaop, ic, true);
  freeAsmop (result, NULL, ic, true);

  if (delayed)
    pullReg (reg);

  pullOrFreeReg (reg, needpull);

  if (ifx && !ifx->generated)
    genIfxJump (ifx, "a");
}


/*-----------------------------------------------------------------*/
/* genDataPointerGet - generates code when ptr offset is known     */
/*-----------------------------------------------------------------*/
static void
genDataPointerGet (operand * left, operand * right, operand * result, iCode * ic, iCode * ifx)
{
  int size, offset;
  int litOffset = 0;
  char * rematOffset = NULL;
  asmop *derefaop;
  bool needpulla = false;
  bool needrestorex = false;
  reg_info *acc;

  D (emitcode (";     genDataPointerGet", ""));

  decodePointerOffset (right, &litOffset, &rematOffset);
  wassert (rematOffset==NULL);

  aopOp (result, ic, true);
  size = getSize (operandType (result));

  derefaop = aopDerefAop (AOP (left), litOffset);
  freeAsmop (left, NULL, ic, true);
  derefaop->size = size;

  acc = (mc6800_reg_a->isDead || !mc6800_reg_b->isFree || !mc6800_reg_b->isDead) ? mc6800_reg_a : mc6800_reg_b;
  if (derefaop->type == AOP_SOF && !IS_AOP_X (AOP (result)))
    needrestorex = pushRegIfSurv (mc6800_reg_x);

  if (IS_AOP_X (AOP (result)))
    loadRegFromAop (mc6800_reg_x, derefaop, 0);
  else if (ifx && size == 2 && mc6800_reg_x->isFree && mc6800_reg_x->isDead)
    {
      mc6800_dirtyReg (mc6800_reg_x, false);
      loadRegFromAop (mc6800_reg_x, derefaop, 0);
    }
  else
    {
      if (ifx && acc == mc6800_reg_a)
        needpulla = pushRegIfSurv (mc6800_reg_a);
      for (offset = 0; offset < size; offset++)
        {
          if (!ifx)
            transferAopAop (derefaop, offset, AOP (result), offset);
          else if (offset == 0)
            loadRegFromAop (acc, derefaop, offset);
          else
            accopWithAop ("ora", acc, derefaop, offset);
        }
    }

  if (needrestorex)
    {
      pullReg (mc6800_reg_x);
      if (ifx)
        mc6800_emitOpWithAcc ("tst", acc, MODE_INH, "");
    }

  freeAsmop (NULL, derefaop, ic, true);
  freeAsmop (result, NULL, ic, true);

  pullOrFreeReg (mc6800_reg_a, needpulla);
  if (ifx && !ifx->generated)
    {
      genIfxJump (ifx, "a");
    }
}


/*-----------------------------------------------------------------*/
/* genPointerGet - generate code for pointer get                   */
/*-----------------------------------------------------------------*/
static void
genPointerGet (iCode * ic, iCode * pi, iCode * ifx)
{
  operand *left = IC_LEFT (ic);
  operand *right = IC_RIGHT (ic);
  operand *result = IC_RESULT (ic);
  int size, offset, xoffset;
  int litOffset = 0;
  char * rematOffset = NULL;
  sym_link *retype = getSpec (operandType (result));
  bool needpulla = false;
  bool needpullb = false;
  bool needpullx = false;
  reg_info *acc = mc6800_reg_a;
  bool vol = false;
  bool xptr = false;

  D (emitcode (";     genPointerGet", ""));

  if ((size = getSize (operandType (result))) > 1 && IS_BITVAR (retype))
    ifx = NULL;

  aopOp (left, ic, false);

  /* if left is rematerialisable */
  if (AOP_TYPE (left) == AOP_IMMD || AOP_TYPE (left) == AOP_LIT || AOP_TYPE (left) == AOP_STL)
    {
      /* if result is not bit variable type */
      if (!IS_BITVAR (retype))
        {
          genDataPointerGet (left, right, result, ic, ifx);
          return;
        }
      else
        {
          genUnpackBitsImmed (left, right, result, ic, ifx);
          return;
        }
    }

  aopOp (result, ic, false);

  if (size > 1 && AOP_TYPE (result) == AOP_REG)
    ifx = NULL;

  /* if bit then unpack */
  if (IS_BITVAR (retype))
    {
      /* hasIncmc6800() will be false for bitfields, so no need */
      /* to consider post-increment in this case. */
      genUnpackBits (result, left, right, ifx);
      goto release;
    }

  if (!IS_AOP_X (AOP (left)))
    needpullx = pushRegIfSurv (mc6800_reg_x);

  /* if the operand is already in x
     then we do nothing else we move the value to x */
  loadRegFromAop (mc6800_reg_x, AOP (left), 0);
  /* so x now contains the address */
  xptr = (AOP_TYPE (left) == AOP_DIR || AOP_TYPE (left) == AOP_EXT) && !IS_AOP_WITH_X (AOP (result)) && AOP_TYPE (result) != AOP_SOF;

  if (stackBasedOffset (right))
    {
      addSPToX ();
      xptr = false;
    }
  decodePointerOffset (right, &litOffset, &rematOffset);
  if (rematOffset || litOffset < 0 || litOffset + size - 1 > 0xff)
    xptr = false;

  if (rematOffset)
    {
      const char *tmp = allocTemp ();
      reg_info *acc = (mc6800_reg_a->isFree || !mc6800_reg_b->isFree) ? mc6800_reg_a : mc6800_reg_b;
      bool needpullacc;
      char ofs[128];

      if (litOffset)
        SNPRINTF (ofs, sizeof (ofs), "(%s+%d)", rematOffset, litOffset);
      else
        SNPRINTF (ofs, sizeof (ofs), "%s", rematOffset);
      litOffset = 0;
      needpullacc = pushRegIfUsed (acc);
      mc6800_emitOp ("stx", MODE_DIR, "*%s", tmp);
      mc6800_emitOpWithAcc ("lda", acc, MODE_DIR, "*%s+1", tmp);
      mc6800_emitOpWithAcc ("add", acc, MODE_IMM, "#%s", ofs);
      mc6800_emitOpWithAcc ("sta", acc, MODE_DIR, "*%s+1", tmp);
      mc6800_emitOpWithAcc ("lda", acc, MODE_DIR, "*%s", tmp);
      mc6800_emitOpWithAcc ("adc", acc, MODE_IMM, "#>%s", ofs);
      mc6800_emitOpWithAcc ("sta", acc, MODE_DIR, "*%s", tmp);
      mc6800_emitOp ("ldx", MODE_DIR, "*%s", tmp);
      freeTemp ();
      mc6800_dirtyReg (acc, false);
      pullOrFreeReg (acc, needpullacc);
      mc6800_dirtyReg (mc6800_reg_x, false);
      rematOffset = NULL;
    }

  if (!rematOffset && litOffset < 0)
    {
      addConstToX (litOffset);
      litOffset = 0;
    }
  else if (!rematOffset && litOffset + size - 1 > 0xff)
    {
      addConstToX (litOffset + size - 1 - 0xff);
      litOffset -= litOffset + size - 1 - 0xff;
    }

  wassertl (!needpullx || !IS_AOP_X (AOP (result)) || AOP_TYPE (result) != AOP_REG,
            "duplicate assignment of X");

  if (AOP_TYPE (result) == AOP_REG && IS_AOP_X (AOP (result))
      && !needpullx)
    {
      mc6800_freeReg (mc6800_reg_x);
      loadRegIndexed (mc6800_reg_x, litOffset, rematOffset);
    }
  else if (AOP_TYPE (result) == AOP_REG && !IS_AOP_WITH_X (AOP (result))
      && AOP_SIZE (result) <= 2)
    {
      int i;

      for (i = 0; i < AOP_SIZE (result); i++)
        loadRegIndexed (AOP (result)->aopu.aop_reg[i], litOffset + AOP_SIZE (result) - 1 - i, rematOffset);
    }
  else if (!ifx && AOP_TYPE (result) == AOP_SOF)
    {
      const char *srctmp = NULL;
      const char *dsttmp = NULL;
      int dstofs = 0;
      bool keepx = IS_AOP_X (AOP (left)) && !mc6800_reg_x->isDead;

      needpullb = pushRegIfSurv (mc6800_reg_b);
      needpulla = pushRegIfSurv (mc6800_reg_a);
      if (size > 2 || keepx)
        {
          srctmp = allocTemp ();
          mc6800_emitOp ("stx", MODE_DIR, "*%s", srctmp);
        }
      offset = 0;
      while (offset < size)
        {
          xoffset = litOffset + (AOP_SIZE (result) - offset - 1);
          if (offset != 0)
            {
              mc6800_emitOp ("ldx", MODE_DIR, "*%s", srctmp);
              mc6800_dirtyReg (mc6800_reg_x, false);
            }
          loadRegIndexed (mc6800_reg_b, xoffset, rematOffset);
          if (offset + 1 < size)
            loadRegIndexed (mc6800_reg_a, xoffset - 1, rematOffset);
          mc6800_freeReg (mc6800_reg_x);
          mc6800_dirtyReg (mc6800_reg_x, false);
          if (dsttmp)
            {
              mc6800_emitOp ("ldx", MODE_DIR, "*%s", dsttmp);
              mc6800_dirtyReg (mc6800_reg_x, false);
              mc6800_reg_x->aop = &tsxaop;
              mc6800_reg_x->stackOffset = dstofs;
            }
          storeRegToAop (mc6800_reg_b, AOP (result), offset);
          if (offset + 1 < size)
            storeRegToAop (mc6800_reg_a, AOP (result), offset + 1);
          if (!dsttmp && offset + 2 < size && mc6800_reg_x->stackOffset != -_G.stackPushes)
            {
              dsttmp = allocTemp ();
              mc6800_emitOp ("stx", MODE_DIR, "*%s", dsttmp);
              dstofs = mc6800_reg_x->stackOffset;
            }
          offset += 2;
        }
      if (keepx)
        {
          mc6800_emitOp ("ldx", MODE_DIR, "*%s", srctmp);
          mc6800_dirtyReg (mc6800_reg_x, false);
        }
      if (dsttmp)
        freeTemp ();
      if (srctmp)
        freeTemp ();
    }
  else if (ifx && size == 2 && mc6800_reg_x->isDead && !needpullx && !rematOffset)
    {
      mc6800_emitOp ("ldx", MODE_IDX, "%d,x", litOffset);
      mc6800_dirtyReg (mc6800_reg_x, false);
    }
  else if (ifx && size == 1 && !needpullx && !rematOffset
           && !mc6800_reg_a->isDead && !(mc6800_reg_b->isFree && mc6800_reg_b->isDead))
    mc6800_emitOp ("tst", MODE_IDX, "%d,x", litOffset);
  else
    {
      acc = (!ifx || mc6800_reg_a->isDead || !mc6800_reg_b->isFree || !mc6800_reg_b->isDead) ? mc6800_reg_a : mc6800_reg_b;
      if (acc == mc6800_reg_a)
        needpulla = pushRegIfSurv (mc6800_reg_a);

      if (!ifx && AOP_TYPE (result) == AOP_REG && AOP_SIZE (result) == 2)
        {
          loadRegIndexed (mc6800_reg_a, litOffset + 1, rematOffset);
          pushReg (mc6800_reg_a, false);
          loadRegIndexed (mc6800_reg_a, litOffset, rematOffset);
          storeRegToAop (mc6800_reg_a, AOP (result), 1);
          pullReg (mc6800_reg_a);
          storeRegToAop (mc6800_reg_a, AOP (result), 0);
        }
      else if (!ifx && AOP_TYPE (result) != AOP_REG)
        for (offset = 0; offset < size; offset++)
          {
            xoffset = litOffset + (AOP_SIZE (result) - offset - 1);
            loadRegIndexed (mc6800_reg_a, xoffset, rematOffset);
            storeRegToAop (mc6800_reg_a, AOP (result), offset);
          }
      else
        for (offset = size - 1; offset >= 0; offset--)
          {
            xoffset = litOffset + offset;
            if (ifx && offset != size - 1)
              mc6800_emitOpWithAcc ("ora", acc, MODE_IDX, "%d,x", xoffset);
            else if (acc == mc6800_reg_a)
              loadRegIndexed (mc6800_reg_a, xoffset, rematOffset);
            else
              mc6800_emitOp ("ldab", MODE_IDX, "%d,x", xoffset);
            if (!ifx)
              storeRegToAop (mc6800_reg_a, AOP (result), size - offset - 1);
          }
    }

release:
  if (pi && xptr)
    {
      addConstToX ((int) operandLitValue (IC_RIGHT (pi)));
      storeRegToAop (mc6800_reg_x, AOP (left), 0);
    }
  size = AOP_SIZE (result);

  pullOrFreeReg (mc6800_reg_x, needpullx);
  if (ifx && needpullx)
    mc6800_emitOpWithAcc ("tst", acc, MODE_INH, "");
  pullOrFreeReg (mc6800_reg_a, needpulla);
  pullOrFreeReg (mc6800_reg_b, needpullb);

  if (pi && !xptr)
    {
      int i;

      for (i = A_IDX; i <= XH_IDX; i++)
        {
          reg_info *reg = mc6800_regWithIdx (i);
          bool live = bitVectBitValue (ic->rSurv, i) || (AOP (result)->regmask & reg->mask);

          reg->isDead = !live || (AOP (left)->regmask & reg->mask);
          reg->isFree = !live && !(AOP (left)->regmask & reg->mask);
        }
      mc6800_reg_d->isFree = mc6800_reg_a->isFree && mc6800_reg_b->isFree;
      mc6800_reg_d->isDead = mc6800_reg_a->isDead && mc6800_reg_b->isDead;
      mc6800_reg_x->isFree = mc6800_reg_xl->isFree && mc6800_reg_xh->isFree;
      mc6800_reg_x->isDead = mc6800_reg_xl->isDead && mc6800_reg_xh->isDead;
      genPlus (pi);
    }
  if (pi)
    pi->generated = 1;
  freeAsmop (left, NULL, ic, true);
  freeAsmop (result, NULL, ic, true);

  if (ifx && !ifx->generated)
    {
      genIfxJump (ifx, "a");
    }
}

/*-----------------------------------------------------------------*/
/* genPackBits - generates code for packed bit storage             */
/*-----------------------------------------------------------------*/
static void
genPackBits (operand * result, operand * left, sym_link * etype, operand * right)
{
  int offset = 0;               /* source byte offset */
  int rlen = 0;                 /* remaining bitfield length */
  unsigned blen;                /* bitfield length */
  unsigned bstr;                /* bitfield starting bit within byte */
  int litval;                   /* source literal value (if AOP_LIT) */
  unsigned char mask;           /* bitmask within current byte */
  int litOffset = 0;
  char *rematOffset = NULL;
  bool needpull = false;
  reg_info *reg;
  asmop *tmpaop = NULL;

  D (emitcode (";     genPackBits", ""));

  blen = SPEC_BLEN (etype);
  bstr = SPEC_BSTR (etype);

  if ((IS_AOP_A (AOP (right)) || IS_AOP_B (AOP (right))) && AOP (right)->aopu.aop_reg[0]->isDead)
    reg = AOP (right)->aopu.aop_reg[0];
  else
    reg = (!mc6800_reg_b->isFree && mc6800_reg_a->isFree) ? mc6800_reg_a : mc6800_reg_b;

  if (AOP_TYPE (right) != AOP_LIT && blen < 8
      && !IS_AOP_WITH_A (AOP (result)) && !IS_AOP_WITH_B (AOP (result)))
    {
      needpull = pushRegIfSurv (reg);
      loadRegFromAop (reg, AOP (right), 0);
      AccLsh (reg, bstr);
    }
  else if (AOP_TYPE (right) != AOP_LIT)
    {
      tmpaop = newAsmop (AOP_DIR);
      tmpaop->aopu.aop_dir = (char *) allocTemp ();
      tmpaop->size = (blen + 7) / 8;
      for (offset = 0; offset < (int) ((blen + 7) / 8); offset++)
        transferAopAop (AOP (right), offset, tmpaop, offset);
      offset = 0;
    }

  loadRegFromAop (mc6800_reg_x, AOP (result), 0);
  if (stackBasedOffset (left))
    addSPToX ();
  decodePointerOffset (left, &litOffset, &rematOffset);

  if (rematOffset)
    {
      const char *tmp = allocTemp ();
      reg_info *acc = (mc6800_reg_a->isFree || !mc6800_reg_b->isFree) ? mc6800_reg_a : mc6800_reg_b;
      bool needpullacc;
      char ofs[128];

      if (litOffset)
        SNPRINTF (ofs, sizeof (ofs), "(%s+%d)", rematOffset, litOffset);
      else
        SNPRINTF (ofs, sizeof (ofs), "%s", rematOffset);
      litOffset = 0;
      needpullacc = pushRegIfUsed (acc);
      mc6800_emitOp ("stx", MODE_DIR, "*%s", tmp);
      mc6800_emitOpWithAcc ("lda", acc, MODE_DIR, "*%s+1", tmp);
      mc6800_emitOpWithAcc ("add", acc, MODE_IMM, "#%s", ofs);
      mc6800_emitOpWithAcc ("sta", acc, MODE_DIR, "*%s+1", tmp);
      mc6800_emitOpWithAcc ("lda", acc, MODE_DIR, "*%s", tmp);
      mc6800_emitOpWithAcc ("adc", acc, MODE_IMM, "#>%s", ofs);
      mc6800_emitOpWithAcc ("sta", acc, MODE_DIR, "*%s", tmp);
      mc6800_emitOp ("ldx", MODE_DIR, "*%s", tmp);
      freeTemp ();
      mc6800_dirtyReg (acc, false);
      pullOrFreeReg (acc, needpullacc);
      mc6800_dirtyReg (mc6800_reg_x, false);
      rematOffset = NULL;
    }

  if (!rematOffset && litOffset < 0)
    {
      addConstToX (litOffset);
      litOffset = 0;
    }
  else if (!rematOffset && litOffset + (int) ((blen + 7) / 8) - 1 > 0xff)
    {
      addConstToX (litOffset + (int) ((blen + 7) / 8) - 1 - 0xff);
      litOffset -= litOffset + (int) ((blen + 7) / 8) - 1 - 0xff;
    }

  if (AOP_TYPE (right) == AOP_LIT || tmpaop)
    needpull = pushRegIfSurv (reg);

  if (blen < 8)
    {
      mask = ((unsigned char) (0xFF << (blen + bstr)) | (unsigned char) (0xFF >> (8 - bstr)));

      if (AOP_TYPE (right) == AOP_LIT)
        {
          litval = (int) ulFromVal (AOP (right)->aopu.aop_lit);
          litval <<= bstr;
          litval &= (~mask) & 0xff;

          loadRegIndexed (reg, litOffset, rematOffset);
          if ((mask | litval) != 0xff)
            mc6800_emitOpWithAcc ("and", reg, MODE_IMM, "#0x%02x", mask);
          if (litval)
            mc6800_emitOpWithAcc ("ora", reg, MODE_IMM, "#0x%02x", litval);
          mc6800_dirtyReg (reg, false);
          storeRegIndexed (reg, litOffset, rematOffset);
          goto release;
        }

      if (tmpaop)
        {
          loadRegFromAop (reg, tmpaop, 0);
          AccLsh (reg, bstr);
        }
      if (isOperandVolatile (result, false) || IS_VOLATILE (etype))
        {
          const char *tmp = allocTemp ();

          mc6800_emitOpWithAcc ("and", reg, MODE_IMM, "#0x%02x", (~mask) & 0xff);
          mc6800_emitOpWithAcc ("sta", reg, MODE_DIR, "*%s", tmp);
          loadRegIndexed (reg, litOffset, rematOffset);
          mc6800_emitOpWithAcc ("and", reg, MODE_IMM, "#0x%02x", mask);
          mc6800_emitOpWithAcc ("ora", reg, MODE_DIR, "*%s", tmp);
          freeTemp ();
        }
      else
        {
          mc6800_emitOpWithAcc ("eor", reg, MODE_IDX, "%d,x", litOffset);
          mc6800_emitOpWithAcc ("and", reg, MODE_IMM, "#0x%02x", (~mask) & 0xff);
          mc6800_emitOpWithAcc ("eor", reg, MODE_IDX, "%d,x", litOffset);
        }
      mc6800_dirtyReg (reg, false);
      storeRegIndexed (reg, litOffset, rematOffset);
      goto release;
    }

  for (rlen = blen; rlen >= 8; rlen -= 8)
    {
      if (AOP_TYPE (right) == AOP_LIT)
        loadRegFromAop (reg, AOP (right), offset);
      else
        loadRegFromAop (reg, tmpaop, offset);
      storeRegIndexed (reg, litOffset + offset, rematOffset);
      offset++;
    }

  if (rlen)
    {
      mask = (((unsigned char) - 1 << rlen) & 0xff);

      if (AOP_TYPE (right) == AOP_LIT)
        {
          litval = (int) ulFromVal (AOP (right)->aopu.aop_lit);
          litval >>= (blen - rlen);
          litval &= (~mask) & 0xff;
          loadRegIndexed (reg, litOffset + offset, rematOffset);
          if ((mask | litval) != 0xff)
            mc6800_emitOpWithAcc ("and", reg, MODE_IMM, "#0x%02x", mask);
          if (litval)
            mc6800_emitOpWithAcc ("ora", reg, MODE_IMM, "#0x%02x", litval);
          mc6800_dirtyReg (reg, false);
          storeRegIndexed (reg, litOffset + offset, rematOffset);
          goto release;
        }

      loadRegFromAop (reg, tmpaop, offset);
      if (isOperandVolatile (result, false) || IS_VOLATILE (etype))
        {
          mc6800_emitOpWithAcc ("and", reg, MODE_IMM, "#0x%02x", (~mask) & 0xff);
          storeRegToAop (reg, tmpaop, offset);
          loadRegIndexed (reg, litOffset + offset, rematOffset);
          mc6800_emitOpWithAcc ("and", reg, MODE_IMM, "#0x%02x", mask);
          accopWithAop ("ora", reg, tmpaop, offset);
        }
      else
        {
          mc6800_emitOpWithAcc ("eor", reg, MODE_IDX, "%d,x", litOffset + offset);
          mc6800_emitOpWithAcc ("and", reg, MODE_IMM, "#0x%02x", (~mask) & 0xff);
          mc6800_emitOpWithAcc ("eor", reg, MODE_IDX, "%d,x", litOffset + offset);
        }
      mc6800_dirtyReg (reg, false);
      storeRegIndexed (reg, litOffset + offset, rematOffset);
    }

release:
  if (tmpaop)
    freeTemp ();
  pullOrFreeReg (reg, needpull);
}

/*-----------------------------------------------------------------*/
/* genPackBitsImmed - generates code for packed bit storage        */
/*-----------------------------------------------------------------*/
static void
genPackBitsImmed (operand * result, operand * left, sym_link * etype, operand * right, iCode * ic)
{
  asmop *derefaop;
  int size;
  int offset = 0;               /* source byte offset */
  int rlen = 0;                 /* remaining bitfield length */
  unsigned blen;                /* bitfield length */
  unsigned bstr;                /* bitfield starting bit within byte */
  unsigned long long int litval;/* source literal value (if AOP_LIT) */
  unsigned char mask;           /* bitmask within current byte */
  bool needpull;
  int litOffset = 0;
  char *rematOffset = NULL;
  reg_info *reg;

  D (emitcode (";     genPackBitsImmed", ""));
  blen = SPEC_BLEN (etype);
  bstr = SPEC_BSTR (etype);

  aopOp (right, ic, false);
  size = AOP_SIZE (right);
  decodePointerOffset (left, &litOffset, &rematOffset);
  wassert (!rematOffset);

  derefaop = aopDerefAop (AOP (result), litOffset);
  freeAsmop (result, NULL, ic, true);
  derefaop->size = size;

  if ((IS_AOP_A (AOP (right)) || IS_AOP_B (AOP (right))) && AOP (right)->aopu.aop_reg[0]->isDead)
    reg = AOP (right)->aopu.aop_reg[0];
  else
    reg = (!mc6800_reg_b->isFree && mc6800_reg_a->isFree) ? mc6800_reg_a : mc6800_reg_b;

  if (blen < 8)
    {
      mask = ((unsigned char) (0xFF << (blen + bstr)) | (unsigned char) (0xFF >> (8 - bstr)));

      if (AOP_TYPE (right) == AOP_LIT)
        {
          litval = ullFromVal (AOP (right)->aopu.aop_lit);
          litval <<= bstr;
          litval &= (~mask) & 0xff;

          needpull = pushRegIfSurv (reg);
          loadRegFromAop (reg, derefaop, 0);
          if ((mask | litval) != 0xff)
            mc6800_emitOpWithAcc ("and", reg, MODE_IMM, "#0x%02x", mask);
          if (litval)
            mc6800_emitOpWithAcc ("ora", reg, MODE_IMM, "#0x%02llx", litval);
          mc6800_dirtyReg (reg, false);
          storeRegToAop (reg, derefaop, 0);

          pullOrFreeReg (reg, needpull);
          goto release;
        }

      needpull = pushRegIfSurv (reg);
      loadRegFromAop (reg, AOP (right), 0);
      AccLsh (reg, bstr);
      if (isOperandVolatile (result, false) || IS_VOLATILE (etype))
        {
          const char *tmp = allocTemp ();

          mc6800_emitOpWithAcc ("and", reg, MODE_IMM, "#0x%02x", (~mask) & 0xff);
          mc6800_emitOpWithAcc ("sta", reg, MODE_DIR, "*%s", tmp);
          loadRegFromAop (reg, derefaop, 0);
          mc6800_emitOpWithAcc ("and", reg, MODE_IMM, "#0x%02x", mask);
          mc6800_emitOpWithAcc ("ora", reg, MODE_DIR, "*%s", tmp);
          freeTemp ();
        }
      else
        {
          accopWithAop ("eor", reg, derefaop, 0);
          mc6800_emitOpWithAcc ("and", reg, MODE_IMM, "#0x%02x", (~mask) & 0xff);
          accopWithAop ("eor", reg, derefaop, 0);
        }
      mc6800_dirtyReg (reg, false);
      storeRegToAop (reg, derefaop, 0);

      pullOrFreeReg (reg, needpull);
      goto release;
    }

  for (rlen = blen; rlen >= 8; rlen -= 8)
    {
      transferAopAop (AOP (right), offset, derefaop, size - offset - 1);
      offset++;
    }

  if (rlen)
    {
      mask = (((unsigned char) - 1 << rlen) & 0xff);

      if (AOP_TYPE (right) == AOP_LIT)
        {
          litval = (int) ulFromVal (AOP (right)->aopu.aop_lit);
          litval >>= (blen - rlen);
          litval &= (~mask) & 0xff;
          needpull = pushRegIfSurv (reg);
          loadRegFromAop (reg, derefaop, size - offset - 1);
          if ((mask | litval) != 0xff)
            mc6800_emitOpWithAcc ("and", reg, MODE_IMM, "#0x%02x", mask);
          if (litval)
            mc6800_emitOpWithAcc ("ora", reg, MODE_IMM, "#0x%02llx", litval);
          mc6800_dirtyReg (reg, false);
          storeRegToAop (reg, derefaop, size - offset - 1);
          pullOrFreeReg (reg, needpull);
          goto release;
        }

      needpull = pushRegIfSurv (reg);
      loadRegFromAop (reg, AOP (right), offset);
      if (isOperandVolatile (result, false) || IS_VOLATILE (etype))
        {
          const char *tmp = allocTemp ();

          mc6800_emitOpWithAcc ("and", reg, MODE_IMM, "#0x%02x", (~mask) & 0xff);
          mc6800_emitOpWithAcc ("sta", reg, MODE_DIR, "*%s", tmp);
          loadRegFromAop (reg, derefaop, size - offset - 1);
          mc6800_emitOpWithAcc ("and", reg, MODE_IMM, "#0x%02x", mask);
          mc6800_emitOpWithAcc ("ora", reg, MODE_DIR, "*%s", tmp);
          freeTemp ();
        }
      else
        {
          accopWithAop ("eor", reg, derefaop, size - offset - 1);
          mc6800_emitOpWithAcc ("and", reg, MODE_IMM, "#0x%02x", (~mask) & 0xff);
          accopWithAop ("eor", reg, derefaop, size - offset - 1);
        }
      mc6800_dirtyReg (reg, false);
      storeRegToAop (reg, derefaop, size - offset - 1);
      pullOrFreeReg (reg, needpull);
      goto release;
    }

release:
  freeAsmop (right, NULL, ic, true);
  freeAsmop (NULL, derefaop, ic, true);
}

/*-----------------------------------------------------------------*/
/* genDataPointerSet - remat pointer to data space                 */
/*-----------------------------------------------------------------*/
static void
genDataPointerSet (operand * left, operand * right, operand * result, iCode * ic)
{
  int size, offset;
  asmop *derefaop;
  int litOffset = 0;
  char *rematOffset = NULL;
  bool needrestorex = false;

  D (emitcode (";     genDataPointerSet", ""));

  aopOp (right, ic, false);
  size = AOP_SIZE (right);
  decodePointerOffset (left, &litOffset, &rematOffset);
  wassert (!rematOffset);

  derefaop = aopDerefAop (AOP (result), litOffset);
  freeAsmop (result, NULL, ic, true);
  derefaop->size = size;

  if (derefaop->type == AOP_SOF && !IS_AOP_X (AOP (right)))
    needrestorex = pushRegIfSurv (mc6800_reg_x);

  if (IS_AOP_X (AOP (right)))
    {
      storeRegToAop (mc6800_reg_x, derefaop, 0);
    }
  else if (AOP_TYPE (right) == AOP_STL)
    {
      bool needpullb = pushRegIfSurv (mc6800_reg_b);
      bool needpulla = pushRegIfSurv (mc6800_reg_a);

      loadRegFromAop (mc6800_reg_d, AOP (right), 0);
      storeRegToAop (mc6800_reg_d, derefaop, 0);
      pullOrFreeReg (mc6800_reg_a, needpulla);
      pullOrFreeReg (mc6800_reg_b, needpullb);
    }
  else
    {
      for (offset = 0; offset < size; offset++)
        {
          transferAopAop (AOP (right), offset, derefaop, offset);
        }
    }

  if (needrestorex)
    pullReg (mc6800_reg_x);

  freeAsmop (right, NULL, ic, true);
  freeAsmop (NULL, derefaop, ic, true);
}


/*-----------------------------------------------------------------*/
/* genPointerSet - stores the value into a pointer location        */
/*-----------------------------------------------------------------*/
static void
genPointerSet (iCode * ic, iCode * pi)
{
  operand *left = IC_LEFT (ic);
  operand *right = IC_RIGHT (ic);
  operand *result = IC_RESULT (ic);
  int size, offset;
  bool needpulla = false;
  bool needpullb = false;
  bool needpullx = false;
  bool vol = false;
  int litOffset = 0;
  char *rematOffset = NULL;
  wassert (operandType (result)->next);
  bool bit_field = IS_BITVAR (operandType (result)->next);
  bool xptr = false;

  D (emitcode (";     genPointerSet", ""));

  aopOp (result, ic, false);

  /* if the result is rematerializable */
  if (AOP_TYPE (result) == AOP_IMMD || AOP_TYPE (result) == AOP_LIT || AOP_TYPE (result) == AOP_STL)
    {
      if (!bit_field)
        genDataPointerSet (left, right, result, ic);
      else
        genPackBitsImmed (result, left, operandType (result)->next, right, ic);
      return;
    }
  aopOp (right, ic, false);
  size = AOP_SIZE (right);

  if (!bit_field)
    decodePointerOffset (left, &litOffset, &rematOffset);
  if (!(IS_AOP_X (AOP (result)) && !stackBasedOffset (left) && !bit_field
        && !rematOffset && litOffset >= 0 && litOffset + size - 1 <= 0xff
        && AOP_TYPE (right) != AOP_SOF && !IS_AOP_WITH_X (AOP (right))))
    needpullx = pushRegIfSurv (mc6800_reg_x);

  /* if bit-field then pack */
  if (bit_field)
    {
      genPackBits (result, left, operandType (result)->next, right);
    }
  else
    {
      if (AOP_TYPE (right) == AOP_SOF)
        needpullb = pushRegIfSurv (mc6800_reg_b);
      needpulla = pushRegIfSurv (mc6800_reg_a);
      if (AOP_TYPE (right) == AOP_REG && (AOP (right)->aopu.aop_reg[0] == mc6800_reg_a || size > 1 && AOP (right)->aopu.aop_reg[1] == mc6800_reg_a))
        mc6800_useReg (mc6800_reg_a);
      if (AOP_TYPE (right) == AOP_REG && IS_AOP_WITH_X (AOP (right)))
        for (offset = 0; offset < size; offset++)
          {
            loadRegFromAop (mc6800_reg_a, AOP (right), offset);
            pushReg (mc6800_reg_a, false);
          }
      loadRegFromAop (mc6800_reg_x, AOP (result), 0);
      if (stackBasedOffset (left))
        addSPToX ();
      decodePointerOffset (left, &litOffset, &rematOffset);
      xptr = (AOP_TYPE (result) == AOP_DIR || AOP_TYPE (result) == AOP_EXT) && !stackBasedOffset (left)
             && !rematOffset && litOffset >= 0 && litOffset + size - 1 <= 0xff && AOP_TYPE (right) != AOP_SOF;

      if (rematOffset)
        {
          const char *tmp = allocTemp ();
          reg_info *acc = (mc6800_reg_a->isFree || !mc6800_reg_b->isFree) ? mc6800_reg_a : mc6800_reg_b;
          bool needpullacc;
          char ofs[128];

          if (litOffset)
            SNPRINTF (ofs, sizeof (ofs), "(%s+%d)", rematOffset, litOffset);
          else
            SNPRINTF (ofs, sizeof (ofs), "%s", rematOffset);
          litOffset = 0;
          needpullacc = pushRegIfUsed (acc);
          mc6800_emitOp ("stx", MODE_DIR, "*%s", tmp);
          mc6800_emitOpWithAcc ("lda", acc, MODE_DIR, "*%s+1", tmp);
          mc6800_emitOpWithAcc ("add", acc, MODE_IMM, "#%s", ofs);
          mc6800_emitOpWithAcc ("sta", acc, MODE_DIR, "*%s+1", tmp);
          mc6800_emitOpWithAcc ("lda", acc, MODE_DIR, "*%s", tmp);
          mc6800_emitOpWithAcc ("adc", acc, MODE_IMM, "#>%s", ofs);
          mc6800_emitOpWithAcc ("sta", acc, MODE_DIR, "*%s", tmp);
          mc6800_emitOp ("ldx", MODE_DIR, "*%s", tmp);
          freeTemp ();
          mc6800_dirtyReg (acc, false);
          pullOrFreeReg (acc, needpullacc);
          mc6800_dirtyReg (mc6800_reg_x, false);
          rematOffset = NULL;
        }

      if (!rematOffset && litOffset < 0)
        {
          addConstToX (litOffset);
          litOffset = 0;
        }
      else if (!rematOffset && litOffset + size - 1 > 0xff)
        {
          addConstToX (litOffset + size - 1 - 0xff);
          litOffset -= litOffset + size - 1 - 0xff;
        }

      if (AOP_TYPE (right) == AOP_SOF)
        {
          const char *dsttmp = allocTemp ();
          const char *srctmp = NULL;
          int srcofs;

          mc6800_emitOp ("stx", MODE_DIR, "*%s", dsttmp);
          mc6800_freeReg (mc6800_reg_x);
          mc6800_dirtyReg (mc6800_reg_x, false);
          setupXForAop (AOP (right));
          if (mc6800_reg_x->stackOffset != -_G.stackPushes)
            {
              srctmp = allocTemp ();
              mc6800_emitOp ("stx", MODE_DIR, "*%s", srctmp);
            }
          srcofs = mc6800_reg_x->stackOffset;
          offset = 0;
          while (offset < size)
            {
              if (srctmp && offset != 0)
                {
                  mc6800_emitOp ("ldx", MODE_DIR, "*%s", srctmp);
                  mc6800_dirtyReg (mc6800_reg_x, false);
                  mc6800_reg_x->aop = &tsxaop;
                  mc6800_reg_x->stackOffset = srcofs;
                }
              loadRegFromAop (mc6800_reg_b, AOP (right), offset);
              if (offset + 1 < size)
                loadRegFromAop (mc6800_reg_a, AOP (right), offset + 1);
              mc6800_emitOp ("ldx", MODE_DIR, "*%s", dsttmp);
              mc6800_dirtyReg (mc6800_reg_x, false);
              storeRegIndexed (mc6800_reg_b, litOffset + size - offset - 1, rematOffset);
              if (offset + 1 < size)
                storeRegIndexed (mc6800_reg_a, litOffset + size - offset - 2, rematOffset);
              mc6800_freeReg (mc6800_reg_a);
              mc6800_freeReg (mc6800_reg_b);
              offset += 2;
            }
          if (srctmp)
            freeTemp ();
          freeTemp ();
        }
      else
        {
          if (AOP_TYPE (right) == AOP_REG && IS_AOP_WITH_X (AOP (right)))
            {
              offset = size;

              while (offset--)
                {
                  pullReg (mc6800_reg_a);
                  storeRegIndexed (mc6800_reg_a, litOffset + size - offset - 1, rematOffset);
                  mc6800_freeReg (mc6800_reg_a);
                }
            }
          else if (AOP_TYPE (right) == AOP_REG && IS_AOP_D (AOP (right)))
            {
              storeRegIndexed (mc6800_reg_d, litOffset, rematOffset);
              mc6800_freeReg (mc6800_reg_a);
            }
          else if (AOP_TYPE (right) == AOP_REG)
            {
              offset = size;

              while (offset--)
                storeRegIndexed (AOP (right)->aopu.aop_reg[offset], litOffset + size - offset - 1, rematOffset);
            }
          else if (AOP_TYPE (right) == AOP_STL)
            {
              needpullb = pushRegIfSurv (mc6800_reg_b);
              loadRegFromAop (mc6800_reg_d, AOP (right), 0);
              storeRegIndexed (mc6800_reg_d, litOffset, rematOffset);
              mc6800_freeReg (mc6800_reg_a);
              pullOrFreeReg (mc6800_reg_b, needpullb);
              needpullb = false;
            }
          else
            for (offset = 0; offset < size; offset++)
              {
                loadRegFromAop (mc6800_reg_a, AOP (right), offset);
                storeRegIndexed (mc6800_reg_a, litOffset + size - offset - 1, rematOffset);
                mc6800_freeReg (mc6800_reg_a);
              }
        }
    }

  if (pi && xptr)
    {
      addConstToX ((int) operandLitValue (IC_RIGHT (pi)));
      storeRegToAop (mc6800_reg_x, AOP (result), 0);
    }

  pullOrFreeReg (mc6800_reg_a, needpulla);
  pullOrFreeReg (mc6800_reg_b, needpullb);
  pullOrFreeReg (mc6800_reg_x, needpullx);

  if (pi && !xptr)
    {
      int i;

      for (i = A_IDX; i <= XH_IDX; i++)
        {
          reg_info *reg = mc6800_regWithIdx (i);
          bool live = bitVectBitValue (ic->rSurv, i);

          reg->isDead = !live || (AOP (result)->regmask & reg->mask);
          reg->isFree = !live && !(AOP (result)->regmask & reg->mask);
        }
      mc6800_reg_d->isFree = mc6800_reg_a->isFree && mc6800_reg_b->isFree;
      mc6800_reg_d->isDead = mc6800_reg_a->isDead && mc6800_reg_b->isDead;
      mc6800_reg_x->isFree = mc6800_reg_xl->isFree && mc6800_reg_xh->isFree;
      mc6800_reg_x->isDead = mc6800_reg_xl->isDead && mc6800_reg_xh->isDead;
      genPlus (pi);
    }
  if (pi)
    pi->generated = 1;
  freeAsmop (result, NULL, ic, true);
  freeAsmop (right, NULL, ic, true);
}

/*-----------------------------------------------------------------*/
/* genIfx - generate code for Ifx statement                        */
/*-----------------------------------------------------------------*/
static void
genIfx (iCode * ic, iCode * popIc)
{
  operand *cond = IC_COND (ic);

  D (emitcode (";     genIfx", ""));

  aopOp (cond, ic, false);

  /* If the condition is a literal, we can just do an unconditional */
  /* branch or no branch */
  if (AOP_TYPE (cond) == AOP_LIT || AOP_TYPE (cond) == AOP_STL)
    {
      unsigned long long lit = AOP_TYPE (cond) == AOP_STL ? 1 : ullFromVal (AOP (cond)->aopu.aop_lit);
      freeAsmop (cond, NULL, ic, true);

      /* if there was something to be popped then do it */
      if (popIc)
        genIpop (popIc);
      if (lit)
        {
          if (IC_TRUE (ic))
            emitBranch ("jmp", IC_TRUE (ic));
        }
      else
        {
          if (IC_FALSE (ic))
            emitBranch ("jmp", IC_FALSE (ic));
        }
      ic->generated = 1;
      return;
    }

  /* evaluate the operand */
  if (AOP_TYPE (cond) != AOP_CRY)
    {
      asmop *aop = AOP (cond);
      int offset = aop->size - 1;
      reg_info *reg = aop->type == AOP_REG ? aop->aopu.aop_reg[0] : mc6800_findRegAop (aop, 0);

      if (aop->size == 1 && (reg == mc6800_reg_a || reg == mc6800_reg_b))
        mc6800_emitOpWithAcc ("tst", reg, MODE_INH, "");
      else if (IS_AOP_X (aop))
        mc6800_emitOp ("cpx", MODE_IMM, "#0");
      else if (IS_AOP_D (aop))
        {
          symbol *tlbl = (regalloc_dry_run ? 0 : newiTempLabel (NULL));

          mc6800_emitOp ("tstb", MODE_INH, "");
          emitBranch ("bne", tlbl);
          mc6800_emitOp ("tsta", MODE_INH, "");
          if (!regalloc_dry_run)
            mc6800_emitLabel (tlbl);
        }
      else if (aop->type == AOP_REG)
        werror (E_INTERNAL_ERROR, __FILE__, __LINE__, "Bad rIdx in genIfx");
      else if (aop->size == 2 && !IS_FLOAT (operandType (cond)) && mc6800_reg_x->isFree &&
        (aop->type == AOP_DIR || aop->type == AOP_EXT || aop->type == AOP_IMMD))
        {
          mc6800_emitOpw_o ("ldx", aop, 0);
          mc6800_dirtyReg (mc6800_reg_x, false);
        }
      else if (aop->size == 1 && !mc6800_reg_a->isFree && !mc6800_reg_b->isFree)
        rmwWithAop ("tst", aop, 0);
      else
        {
          bool needpull;

          reg = (mc6800_reg_a->isFree || !mc6800_reg_b->isFree) ? mc6800_reg_a : mc6800_reg_b;
          needpull = pushRegIfUsed (reg);
          loadRegFromAop (reg, aop, offset--);
          if (IS_FLOAT (operandType (cond)))
            mc6800_emitOpWithAcc ("and", reg, MODE_IMM, "#0x7F");
          while (offset >= 0)
            accopWithAop ("ora", reg, aop, offset--);
          if (aop->size > 1)
            mc6800_dirtyReg (reg, false);
          pullOrFreeReg (reg, needpull);
        }
    }
  /* the result is now in the z flag bit */
  freeAsmop (cond, NULL, ic, true);

  /* if there was something to be popped then do it */
  if (popIc)
    genIpop (popIc);

  genIfxJump (ic, "a");

  ic->generated = 1;
}

/*-----------------------------------------------------------------*/
/* genAddrOf - generates code for address of                       */
/*-----------------------------------------------------------------*/
static void
genAddrOf (iCode * ic)
{
  symbol *sym = OP_SYMBOL (IC_LEFT (ic));
  asmop *aopr;
  int size, offset;
  bool needpullx;
  struct dbuf_s dbuf;

  D (emitcode (";     genAddrOf", ""));

  aopOp (IC_RESULT (ic), ic, false);
  aopr = AOP (IC_RESULT (ic));

  /* if the operand is on the stack then we
     need to get the stack offset of this
     variable */
  if (sym->onStack && IS_AOP_X (aopr))
    {
      needpullx = pushRegIfSurv (mc6800_reg_x);
      mc6800_useReg (mc6800_reg_x);
      setupXFromSP (_G.stackOfs + sym->stack + (sym->stack > 0 ? _G.param_offset : 0));
      storeRegToAop (mc6800_reg_x, AOP (IC_RESULT (ic)), 0);
      pullOrFreeReg (mc6800_reg_x, needpullx);
      goto release;
    }
  if (sym->onStack)
    {
      bool needpullb = pushRegIfSurv (mc6800_reg_b);
      bool needpulla = pushRegIfSurv (mc6800_reg_a);
      const char *tmp = allocTemp ();
      int delta = 1 + _G.stackOfs + sym->stack + (sym->stack > 0 ? _G.param_offset : 0) + _G.stackPushes;

      mc6800_emitOp ("sts", MODE_DIR, "*%s", tmp);
      mc6800_emitOp ("ldab", MODE_DIR, "*%s+1", tmp);
      mc6800_emitOp ("ldaa", MODE_DIR, "*%s", tmp);
      mc6800_emitOp ("addb", MODE_IMM, "#%d", delta & 0xff);
      mc6800_emitOp ("adca", MODE_IMM, "#%d", (delta >> 8) & 0xff);
      freeTemp ();
      mc6800_dirtyReg (mc6800_reg_a, false);
      mc6800_dirtyReg (mc6800_reg_b, false);
      storeRegToAop (mc6800_reg_d, aopr, 0);
      pullOrFreeReg (mc6800_reg_a, needpulla);
      pullOrFreeReg (mc6800_reg_b, needpullb);
      goto release;
    }

  if (IS_AOP_X (aopr))
    {
      loadRegFromImm (mc6800_reg_x, sym->rname);
      goto release;
    }

  /* object not on stack then we need the name */
  size = AOP_SIZE (IC_RESULT (ic));
  offset = 0;

  while (size--)
    {
      dbuf_init (&dbuf, 64);
      switch (offset)
        {
        case 0:
          dbuf_printf (&dbuf, "#%s", sym->rname);
          break;
        case 1:
          dbuf_printf (&dbuf, "#>%s", sym->rname);
          break;
        default:
          dbuf_printf (&dbuf, "#0");
        }
      storeImmToAop (dbuf_detach_c_str (&dbuf), AOP (IC_RESULT (ic)), offset++);
    }

release:
  freeAsmop (IC_RESULT (ic), NULL, ic, true);
}

/*-----------------------------------------------------------------*/
/* genAssignLit - Try to generate code for literal assignment.     */
/*                result and right should already be asmOped       */
/*-----------------------------------------------------------------*/
static bool
genAssignLit (operand * result, operand * right)
{
  char assigned[8];
  unsigned char value[sizeof(assigned)];
  char dup[sizeof(assigned)];
  int size;
  int offset,offset2;
  int dups,multiples;
  bool needpula = false;
  bool canUseX = true;
  int remaining;

  D (emitcode (";     genAssignLit", ""));

  /* Make sure this is a literal assignment */
  if (AOP_TYPE (right) != AOP_LIT)
    return false;

  /* The general case already handles register assignment well */
  if (AOP_TYPE (result) == AOP_REG)
    return false;

  /* Some hardware registers require MSB to LSB assignment order */
  /* so don't optimize the assignment order if volatile */
  if (isOperandVolatile (result, false))
    return false;

  /* Make sure the assignment is not larger than we can handle */
  size = AOP_SIZE (result);
  if (size > sizeof(assigned))
    return false;

  for (offset=0; offset<size; offset++)
    {
      assigned[offset] = 0;
      dup[offset] = 0;
      value[offset] = byteOfVal (AOP (right)->aopu.aop_lit, offset);
    }

  if ((AOP_TYPE (result) != AOP_DIR ) && IS_MC6800)
    canUseX = false;

  if (canUseX)
    {
      /* Assign words that are already in X */
      for (offset=size-2; offset>=0; offset -= 2)
        {
          if (assigned[offset] || assigned[offset+1])
            continue;
          if (mc6800_reg_x->isLitConst && mc6800_reg_x->litConst == ((value[offset+1] << 8) + value[offset]))
            {
              storeRegToAop (mc6800_reg_x, AOP (result), offset);
              assigned[offset] = 1;
              assigned[offset+1] = 1;
            }
        }
    }

  if (!mc6800_reg_x->isDead)
    canUseX = false;

  if (canUseX && (size>=2))
    {
      /* Assign whatever remains to be assigned */
      for (offset=size-2; offset>=0; offset -= 2)
        {
          if (assigned[offset] && assigned[offset+1])
            continue;
          loadRegFromConst (mc6800_reg_x, (value[offset+1] << 8) + value[offset]);
          storeRegToAop (mc6800_reg_x, AOP (result), offset);
          assigned[offset] = 1;
          assigned[offset+1] = 1;
        }
    }

  remaining = size;
  for (offset=0; offset<size; offset++)
    remaining -= assigned[offset];
  if (!remaining)
    return true;

  /* Assign bytes that are already in A and/or X */
  for (offset=size-1; offset>=0; offset--)
    {
      if (assigned[offset])
        continue;
      if ((mc6800_reg_a->isLitConst && mc6800_reg_a->litConst == value[offset]) ||
          (mc6800_reg_x->isLitConst && mc6800_reg_x->litConst == value[offset]))
        {
          storeConstToAop (value[offset], AOP (result), offset);
          assigned[offset] = 1;
        }
    }

  /* Consider bytes that appear multiple times */
  multiples = 0;
  for (offset=size-1; offset>=0; offset--)
    {
      if (assigned[offset] || dup[offset])
        continue;
      dups = 0;
      for (offset2=offset-1; offset2>=0; offset2--)
        if (value[offset2] == value[offset])
          {
            dup[offset] = 1;
            dups++;
          }
      if (dups)
        multiples += (dups+1);
    }

  /* Assign bytes that appear multiple times if the register cost */
  /* isn't too high. */
  if (multiples > 2 || (multiples && !mc6800_reg_a->isDead))
    {
      needpula = pushRegIfSurv (mc6800_reg_a);
      for (offset=size-1; offset>=0; offset--)
        {
          if (assigned[offset])
            continue;
          if (dup[offset])
            {
              loadRegFromConst (mc6800_reg_a, value[offset]);
              for (offset2=offset; offset2>=0; offset2--)
                if (!assigned[offset2] && value[offset2] == value[offset])
                  {
                    storeRegToAop (mc6800_reg_a, AOP (result), offset2);
                    assigned[offset2] = 1;
                  }
              mc6800_freeReg (mc6800_reg_a);
            }
        }
    }

  /* Assign whatever remains to be assigned */
  for (offset=size-1; offset>=0; offset--)
    {
      if (assigned[offset])
        continue;
      storeConstToAop (value[offset], AOP (result), offset);
    }

  if (needpula)
    pullReg (mc6800_reg_a);

  return true;
}


/*-----------------------------------------------------------------*/
/* genAssign - generate code for assignment                        */
/*-----------------------------------------------------------------*/
static void
genAssign (iCode * ic)
{
  operand *result, *right;

  D (emitcode (";     genAssign", ""));

  result = IC_RESULT (ic);
  right = IC_RIGHT (ic);

  aopOp (right, ic, false);
  aopOp (result, ic, true);
  if (IS_SYMOP (result) && AOP (result)->op)
  {
    const char *varname = OP_SYMBOL (result)->name;
    asmop *aop = AOP (result);
    int i;
    for (i = 0; i < aop->size; i++)
      {
        if (aop->type == AOP_REG)
          D (emitcode ("", "; %s[%d] -> reg %s", varname, i, aop->aopu.aop_reg[i]->name));
        else if (aop->type == AOP_DIR || aop->type == AOP_EXT)
          D (emitcode ("", "; %s[%d] -> mem %s+%d", varname, i, aop->aopu.aop_dir, i));
        else if (aop->type == AOP_SOF)
          D (emitcode ("", "; %s[%d] -> stack %d", varname, i, aop->aopu.aop_stk + i));
      }
  }

  if (!genAssignLit (result, right))
    {
      genCopy (result, right);
    }

  freeAsmop (right, NULL, ic, true);
  freeAsmop (result, NULL, ic, true);
}

/*-----------------------------------------------------------------*/
/* genJumpTab - generates code for jump table                       */
/*-----------------------------------------------------------------*/
static void
genJumpTab (iCode * ic)
{
  symbol *jtab;
  symbol *jtablbl = (regalloc_dry_run ? 0 : newiTempLabel (NULL));
  const char *tmp;
  bool needpullb, needpulla;

  D (emitcode (";     genJumpTab", ""));

  aopOp (IC_JTCOND (ic), ic, false);

  if (elementsInSet (IC_JTLABELS (ic)) <= 4 && mc6800_reg_b->isDead)
    {
      int count = elementsInSet (IC_JTLABELS (ic));

      loadRegFromAop (mc6800_reg_b, AOP (IC_JTCOND (ic)), 0);
      freeAsmop (IC_JTCOND (ic), NULL, ic, true);
      mc6800_emitOp ("tstb", MODE_INH, "");
      for (jtab = setFirstItem (IC_JTLABELS (ic)); jtab; jtab = setNextItem (IC_JTLABELS (ic)))
        {
          symbol *tlbl;

          if (--count == 0)
            {
              emitBranch ("jmp", jtab);
              break;
            }
          if (count + 1 < elementsInSet (IC_JTLABELS (ic)))
            mc6800_emitOp ("decb", MODE_INH, "");
          tlbl = (regalloc_dry_run ? 0 : newiTempLabel (NULL));
          emitBranch ("bne", tlbl);
          emitBranch ("jmp", jtab);
          if (!regalloc_dry_run)
            mc6800_emitLabel (tlbl);
        }
      return;
    }

  if (!mc6800_reg_x->isDead)
    UNIMPLEMENTED;

  needpullb = pushRegIfSurv (mc6800_reg_b);
  needpulla = pushRegIfSurv (mc6800_reg_a);
  loadRegFromAop (mc6800_reg_b, AOP (IC_JTCOND (ic)), 0);
  freeAsmop (IC_JTCOND (ic), NULL, ic, true);
  tmp = allocTemp ();
  mc6800_emitOp ("clra", MODE_INH, "");
  mc6800_emitOp ("aslb", MODE_INH, "");
  mc6800_emitOp ("rola", MODE_INH, "");
  mc6800_emitOp ("addb", MODE_IMM, "#<%05d$", regalloc_dry_run ? 0 : labelKey2num (jtablbl->key));
  mc6800_emitOp ("adca", MODE_IMM, "#>%05d$", regalloc_dry_run ? 0 : labelKey2num (jtablbl->key));
  mc6800_emitOp ("stab", MODE_DIR, "*%s+1", tmp);
  mc6800_emitOp ("staa", MODE_DIR, "*%s", tmp);
  mc6800_emitOp ("ldx", MODE_DIR, "*%s", tmp);
  mc6800_emitOp ("ldx", MODE_IDX, "0,x");
  freeTemp ();
  pullOrFreeReg (mc6800_reg_a, needpulla);
  pullOrFreeReg (mc6800_reg_b, needpullb);
  mc6800_emitOp ("jmp", MODE_IDX, "0,x");

  if (!regalloc_dry_run)
    mc6800_emitLabel (jtablbl);
  for (jtab = setFirstItem (IC_JTLABELS (ic)); jtab; jtab = setNextItem (IC_JTLABELS (ic)))
    {
      if (!regalloc_dry_run)
        emitcode (".dw", "%05d$", labelKey2num (jtab->key));
      regalloc_dry_run_cost += 2;
    }
}

/*-----------------------------------------------------------------*/
/* genCast - gen code for casting                                  */
/*-----------------------------------------------------------------*/
static void
genCast (iCode * ic)
{
  operand *result = IC_RESULT (ic);
  operand *right = IC_RIGHT (ic);
  sym_link *resulttype = operandType (result);
  sym_link *righttype = operandType (right);
  int size, offset;
  bool signExtend;
  bool save_a;

  D (emitcode (";     genCast", ""));

  if (operandsEqu (IC_RESULT (ic), IC_RIGHT (ic)))
    return;

  unsigned topbytemask = (IS_BITINT (resulttype) && (SPEC_BITINTWIDTH (resulttype) % 8)) ?
    (0xff >> (8 - SPEC_BITINTWIDTH (resulttype) % 8)) : 0xff;

  aopOp (right, ic, false);
  aopOp (result, ic, false);

  if (IS_BITINT (resulttype) && (SPEC_BITINTWIDTH (resulttype) % 8) && bitsForType (resulttype) < bitsForType (righttype))
    {
      save_a = false;
      genCopy (result, right);
      if (result->aop->type != AOP_REG || result->aop->aopu.aop_reg[result->aop->size - 1] != mc6800_reg_a)
        {
          save_a = result->aop->aopu.aop_reg[0] == mc6800_reg_a || !mc6800_reg_a->isDead;
          if (save_a)
            {
              pushReg (mc6800_reg_a, false);
            }
          loadRegFromAop (mc6800_reg_a, result->aop, result->aop->size - 1);
        }
      mc6800_emitOp ("anda", MODE_IMM, "#0x%02x", topbytemask);
      if (!SPEC_USIGN (resulttype))
        {
          symbol *tlbl = regalloc_dry_run ? 0 : newiTempLabel (0);
          mc6800_emitOp ("bita", MODE_IMM, "#0x%02x", 1u << (SPEC_BITINTWIDTH (resulttype) % 8 - 1));
          emitBranch ("beq", tlbl);
          mc6800_emitOp ("oraa", MODE_IMM, "#0x%02x", ~topbytemask & 0xff);
          mc6800_emitLabel (tlbl);
        }
      storeRegToAop (mc6800_reg_a, result->aop, result->aop->size - 1);
      if (save_a)
        {
          pullReg (mc6800_reg_a);
        }
      goto release;
    }

  if (IS_BOOL (resulttype))
    {
      bool needpull;
      bool needpulla = false;
      reg_info *reg;
      asmop *aop = AOP (right);
      int offset = aop->size - 1;

      if (IS_AOP_A (AOP (result)) || IS_AOP_B (AOP (result)))
        reg = AOP (result)->aopu.aop_reg[0];
      else
        reg = (!mc6800_reg_b->isFree && mc6800_reg_a->isFree) ? mc6800_reg_a : mc6800_reg_b;
      needpull = pushRegIfSurv (reg);

      if (IS_BOOL (operandType (right)))
        loadRegFromAop (reg, aop, 0);
      else if (aop->type == AOP_LIT)
        loadRegFromConst (reg, !!ullFromVal (aop->aopu.aop_lit));
      else if (aop->type == AOP_STL)
        loadRegFromConst (reg, 1);
      else if (aop->type == AOP_REG && aop->size == 1)
        {
          mc6800_emitOpWithAcc ("cmp", aop->aopu.aop_reg[0], MODE_IMM, "#0x01");
          mc6800_emitOpWithAcc ("lda", reg, MODE_IMM, "#0x00");
          mc6800_emitOpWithAcc ("sbc", reg, MODE_IMM, "#0xff");
        }
      else if (aop->type != AOP_REG && aop->size == 1)
        {
          loadRegFromConst (reg, 0);
          accopWithAop ("cmp", reg, aop, 0);
          rmwWithReg ("rol", reg);
        }
      else
        {
          if (IS_AOP_D (aop))
            {
              needpulla = reg == mc6800_reg_b && pushRegIfSurv (mc6800_reg_a);
              mc6800_emitOp ("aba", MODE_INH, "");
              mc6800_emitOp ("adca", MODE_IMM, "#0xff");
            }
          else if (IS_AOP_X (aop))
            {
              const char *tmp = allocTemp ();

              mc6800_emitOp ("stx", MODE_DIR, "*%s", tmp);
              mc6800_emitOpWithAcc ("lda", reg, MODE_DIR, "*%s", tmp);
              mc6800_emitOpWithAcc ("add", reg, MODE_DIR, "*%s+1", tmp);
              mc6800_emitOpWithAcc ("adc", reg, MODE_IMM, "#0xff");
              freeTemp ();
            }
          else if (aop->type == AOP_REG)
            werror (E_INTERNAL_ERROR, __FILE__, __LINE__, "Bad rIdx in genCast");
          else
            {
              loadRegFromAop (reg, aop, offset--);
              if (IS_FLOAT (operandType (right)))
                mc6800_emitOpWithAcc ("and", reg, MODE_IMM, "#0x7F");
              accopWithAop ("add", reg, aop, offset--);
              while (offset >= 0)
                accopWithAop ("adc", reg, aop, offset--);
              mc6800_emitOpWithAcc ("adc", reg, MODE_IMM, "#0xff");
            }
          mc6800_emitOpWithAcc ("lda", reg, MODE_IMM, "#0x00");
          rmwWithReg ("rol", reg);
          if (needpulla)
            {
              mc6800_dirtyReg (mc6800_reg_a, false);
              pullReg (mc6800_reg_a);
            }
        }
      mc6800_dirtyReg (reg, false);
      storeRegToAop (reg, AOP (result), 0);
      pullOrFreeReg (reg, needpull);
      goto release;
    }

  if (result->aop->size <= right->aop->size)
    {
      wassert (!IS_BITINT (resulttype) || !(SPEC_BITINTWIDTH (resulttype) % 8));
      genCopy (result, right);
      goto release;
    }

  signExtend = AOP_SIZE (result) > AOP_SIZE (right) && !IS_BOOL (righttype) && IS_SPEC (righttype) && !SPEC_USIGN (righttype);
  bool masktopbyte = IS_BITINT (resulttype) && (SPEC_BITINTWIDTH (resulttype) % 8) && SPEC_USIGN (resulttype);

  if (AOP_SIZE (result) == 2 && AOP (result)->type == AOP_REG)
    {
      if (AOP_SIZE (right) == 2 && !IS_BITINT (resulttype))
        {
          if (IS_AOP_D (AOP (result)))
            {
              loadRegFromAop (mc6800_reg_d, AOP (right), 0);
              goto release;
            }
          if (IS_AOP_X (AOP (result)))
            {
              loadRegFromAop (mc6800_reg_x, AOP (right), 0);
              goto release;
            }
        }

      if (AOP_SIZE (right) == 1)
        {
          transferAopAop (AOP (right), 0, AOP (result), 0);
          if (!signExtend)
            {
              storeConstToAop (0, AOP (result), 1);
            }
            else
              {
                save_a = (AOP (result)->aopu.aop_reg[0] == mc6800_reg_a || !mc6800_reg_a->isDead);
                if (save_a)
                  {
                    pushReg (mc6800_reg_a, false);
                  }
                if (AOP (result)->aopu.aop_reg[0] != mc6800_reg_a)
                  {
                    loadRegFromAop (mc6800_reg_a, AOP (right), 0);
                  }
                mc6800_emitOp ("rola", MODE_INH, "");
                mc6800_emitOp ("ldaa", MODE_IMM, "#0x00");
                mc6800_emitOp ("sbca", MODE_IMM, "#0x00");
                if (masktopbyte)
                  {
                    mc6800_emitOp ("anda", MODE_IMM, "#0x%02x", topbytemask);
                  }
                storeRegToAop (mc6800_reg_a, AOP (result), 1);
                if (save_a)
                  {
                    pullReg (mc6800_reg_a);
                  }
              }
          goto release;
        }

      wassert (0);
    }

  wassert (AOP (result)->type != AOP_REG);

  save_a = !mc6800_reg_a->isDead && (signExtend || topbytemask != 0xff);
  if (save_a)
    {
      pushReg (mc6800_reg_a, true);
    }

  offset = 0;
  size = AOP_SIZE (right);
  if (AOP_SIZE (result) < size)
    {
      size = AOP_SIZE (result);
    }
  while (size)
    {
      if (size == 1 && signExtend)
        {
          loadRegFromAop (mc6800_reg_a, AOP (right), offset);
          storeRegToAop (mc6800_reg_a, AOP (result), offset);
          offset++;
          size--;
        }
      else if (size == 2 && AOP_TYPE (right) == AOP_STL)
        {
          bool needpullb = pushRegIfSurv (mc6800_reg_b);
          bool needpulla = pushRegIfSurv (mc6800_reg_a);

          loadRegFromAop (mc6800_reg_d, AOP (right), offset);
          storeRegToAop (mc6800_reg_d, AOP (result), offset);
          pullOrFreeReg (mc6800_reg_a, needpulla);
          pullOrFreeReg (mc6800_reg_b, needpullb);
          offset += 2;
          size -= 2;
        }
      else if ((size > 2 || size >= 2 && !signExtend)
      &&  mc6800_reg_x->isDead
      &&  AOP_TYPE (right) != AOP_SOF
      &&  AOP_TYPE (result) != AOP_SOF) {
        loadRegFromAop (mc6800_reg_x, AOP (right), offset);
        storeRegToAop (mc6800_reg_x, AOP (result), offset);
        offset += 2;
        size -= 2;
      }
      else
        {
          transferAopAop (AOP (right), offset, AOP (result), offset);
          offset++;
          size--;
        }
    }

  size = AOP_SIZE (result) - offset;
  if (size && !signExtend)
    {
      while (size--)
        {
          storeConstToAop (0, AOP (result), offset++);
        }
    }
    else if (size)
      {
        mc6800_emitOp ("rola", MODE_INH, "");
        mc6800_emitOp ("ldaa", MODE_IMM, "#0x00");
        mc6800_emitOp ("sbca", MODE_IMM, "#0x00");
        while (size--)
          {
            if (!size && masktopbyte)
              {
                mc6800_emitOp ("anda", MODE_IMM, "#0x%02x", topbytemask);
              }
            storeRegToAop (mc6800_reg_a, AOP (result), offset++);
          }
      }

  if (save_a)
    {
      pullReg (mc6800_reg_a);
    }

release:
  freeAsmop (right, NULL, ic, true);
  freeAsmop (result, NULL, ic, true);

}

/*-----------------------------------------------------------------*/
/* genDjnz - generate decrement & jump if not zero instruction      */
/*-----------------------------------------------------------------*/
static int
genDjnz (iCode * ic, iCode * ifx)
{
  operand *left = IC_LEFT (ic);
  operand *result = IC_RESULT (ic);

  if (!ifx)
    return 0;
  if (!IS_OP_LITERAL (IC_RIGHT (ic)) || operandLitValue (IC_RIGHT (ic)) != 1)
    return 0;

  D (emitcode (";     genDjnz", ""));

  aopOp (left, ic, false);
  aopOp (result, ic, true);

  if (AOP_SIZE (left) != AOP_SIZE (result))
    return 0;

  if ((IS_AOP_A (AOP (left)) || IS_AOP_B (AOP (left))) && !sameRegs (AOP (left), AOP (result)))
    {
      mc6800_emitOpWithAcc ("cmp", AOP (left)->aopu.aop_reg[0], MODE_IMM, "#1");
      genIfxJump (ifx, "a");
    }
  else if (IS_AOP_X (AOP (left)) && !sameRegs (AOP (left), AOP (result)))
    {
      mc6800_emitOp ("cpx", MODE_IMM, "#1");
      genIfxJump (ifx, "a");
    }
  else if (IS_AOP_D (AOP (left)) && !sameRegs (AOP (left), AOP (result)))
    {
      symbol *tlbl1 = (regalloc_dry_run ? 0 : newiTempLabel (NULL));
      symbol *tlbl2 = (regalloc_dry_run ? 0 : newiTempLabel (NULL));

      mc6800_emitOp ("cmpb", MODE_IMM, "#1");
      if (IC_TRUE (ifx))
        {
          emitBranch ("bne", tlbl1);
          mc6800_emitOp ("tsta", MODE_INH, "");
          emitBranch ("beq", tlbl2);
          if (!regalloc_dry_run)
            mc6800_emitLabel (tlbl1);
          emitBranch ("jmp", IC_TRUE (ifx));
          if (!regalloc_dry_run)
            mc6800_emitLabel (tlbl2);
        }
      else
        {
          emitBranch ("bne", tlbl2);
          mc6800_emitOp ("tsta", MODE_INH, "");
          emitBranch ("bne", tlbl2);
          emitBranch ("jmp", IC_FALSE (ifx));
          if (!regalloc_dry_run)
            mc6800_emitLabel (tlbl2);
        }
      ifx->generated = 1;
    }
  else if (IS_AOP_A (AOP (result)) || IS_AOP_B (AOP (result)))
    {
      loadRegFromAop (AOP (result)->aopu.aop_reg[0], AOP (left), 0);
      mc6800_emitOpWithAcc ("dec", AOP (result)->aopu.aop_reg[0], MODE_INH, "");
      genIfxJump (ifx, "a");
    }
  else if (IS_AOP_X (AOP (result)))
    {
      loadRegFromAop (mc6800_reg_x, AOP (left), 0);
      mc6800_emitOp ("dex", MODE_INH, "");
      genIfxJump (ifx, "a");
    }
  else if (IS_AOP_D (AOP (result)))
    {
      symbol *tlbl1 = (regalloc_dry_run ? 0 : newiTempLabel (NULL));
      symbol *tlbl2 = (regalloc_dry_run ? 0 : newiTempLabel (NULL));

      loadRegFromAop (mc6800_reg_d, AOP (left), 0);
      mc6800_emitOp ("subb", MODE_IMM, "#1");
      emitBranch ("bne", tlbl1);
      mc6800_emitOp ("tsta", MODE_INH, "");
      if (IC_TRUE (ifx))
        {
          emitBranch ("beq", tlbl2);
          if (!regalloc_dry_run)
            mc6800_emitLabel (tlbl1);
          mc6800_emitOp ("sbca", MODE_IMM, "#0");
          emitBranch ("jmp", IC_TRUE (ifx));
          if (!regalloc_dry_run)
            mc6800_emitLabel (tlbl2);
        }
      else
        {
          emitBranch ("bne", tlbl2);
          emitBranch ("jmp", IC_FALSE (ifx));
          if (!regalloc_dry_run)
            mc6800_emitLabel (tlbl1);
          mc6800_emitOp ("sbca", MODE_IMM, "#0");
          if (!regalloc_dry_run)
            mc6800_emitLabel (tlbl2);
        }
      mc6800_dirtyReg (mc6800_reg_d, false);
      ifx->generated = 1;
    }
  else
    return 0;

  freeAsmop (left, NULL, ic, true);
  freeAsmop (result, NULL, ic, true);
  return 1;
}

/*-----------------------------------------------------------------*/
/* genPostIncDec - n + 1 or n - 1 and jump on the old value of n    */
/*-----------------------------------------------------------------*/
static void
genPostIncDec (iCode *ic, iCode *ifx)
{
  operand *source = isOperandEqual (IC_LEFT (ic), IC_RESULT (ic->prev)) ? IC_RIGHT (ic->prev) : IC_LEFT (ic);
  operand *result = IC_RESULT (ic);
  bool inc = ic->op == '+';
  bool zflag = inc;
  symbol *tlbl = regalloc_dry_run ? 0 : newiTempLabel (NULL);
  asmop *aop;

  D (emitcode (";     genPostIncDec", ""));

  aopOp (source, ic, false);
  aopOp (result, ic, true);
  if (!sameRegs (AOP (source), AOP (result)))
    genCopy (result, source);
  aop = AOP (result);

  if (IS_AOP_X (aop) || aop->size > 1 && (aop->type == AOP_DIR || aop->type == AOP_EXT) && mc6800_reg_x->isFree && mc6800_reg_x->isDead)
    {
      loadRegFromAop (mc6800_reg_x, aop, 0);
      addConstToX (inc ? 1 : -1);
      storeRegToAop (mc6800_reg_x, aop, 0);
      mc6800_emitOp ("cpx", MODE_IMM, inc ? "#0x0001" : "#0xffff");
      zflag = true;
    }
  else if (aop->size == 1)
    {
      reg_info *acc = IS_AOP_A (aop) || IS_AOP_B (aop) ? aop->aopu.aop_reg[0] : mc6800_reg_a->isFree || !mc6800_reg_b->isFree ? mc6800_reg_a : mc6800_reg_b;
      bool needpull = !(aop->regmask & acc->mask) && pushRegIfUsed (acc);

      loadRegFromAop (acc, aop, 0);
      mc6800_emitOpWithAcc (inc ? "cmp" : "sub", acc, MODE_IMM, "#0x01");
      if (inc)
        mc6800_emitOpWithAcc ("inc", acc, MODE_INH, "");
      storeRegToAop (acc, aop, 0);
      pullOrFreeReg (acc, needpull);
      zflag = false;
    }
  else
    {
      bool needpullb = !(aop->regmask & MC6800MASK_B) && pushRegIfUsed (mc6800_reg_b);
      bool needpulla = !(aop->regmask & MC6800MASK_A) && pushRegIfUsed (mc6800_reg_a);
      symbol *zlbl = regalloc_dry_run || !inc ? 0 : newiTempLabel (NULL);

      loadRegFromAop (mc6800_reg_d, aop, 0);
      mc6800_emitOp (inc ? "addb" : "subb", MODE_IMM, "#0x01");
      mc6800_emitOp (inc ? "adca" : "sbca", MODE_IMM, "#0x00");
      storeRegToAop (mc6800_reg_d, aop, 0);
      if (inc)
        {
          mc6800_emitOp ("cmpb", MODE_IMM, "#0x01");
          emitBranch ("bne", zlbl);
          mc6800_emitOp ("tsta", MODE_INH, "");
          if (!regalloc_dry_run)
            mc6800_emitLabel (zlbl);
        }
      pullOrFreeReg (mc6800_reg_a, needpulla);
      pullOrFreeReg (mc6800_reg_b, needpullb);
    }

  if (IC_TRUE (ifx))
    emitBranch (zflag ? "beq" : "bcs", tlbl);
  else
    emitBranch (zflag ? "bne" : "bcc", tlbl);
  emitBranch ("jmp", IC_TRUE (ifx) ? IC_TRUE (ifx) : IC_FALSE (ifx));
  if (!regalloc_dry_run)
    mc6800_emitLabel (tlbl);
  ifx->generated = 1;

  freeAsmop (result, NULL, ic, true);
  freeAsmop (source, NULL, ic, true);
}

/*-----------------------------------------------------------------*/
/* genReceive - generate code for a receive iCode                  */
/*-----------------------------------------------------------------*/
static void
genReceive (iCode * ic)
{
  int size;
  int offset;

  D (emitcode (";", "genReceive"));

  aopOp (IC_RESULT (ic), ic, false);
  size = AOP_SIZE (IC_RESULT (ic));
  offset = 0;

  if (ic->argreg)
    {
      wassert (size <= 2);
      if (size == 2 && ic->argreg == 1 && IS_AOP_X (AOP (IC_RESULT (ic)))) {
        storeRegToAop (mc6800_reg_d, AOP (IC_RESULT (ic)), 0);
        mc6800_freeReg (mc6800_reg_b);
        mc6800_freeReg (mc6800_reg_a);
        size = 0;
      }
      while (size--)
        {
          transferAopAop (mc6800_aop_pass[offset + (ic->argreg - 1)], 0, AOP (IC_RESULT (ic)), offset);
          if (mc6800_aop_pass[offset + (ic->argreg - 1)]->type == AOP_REG)
            mc6800_freeReg (mc6800_aop_pass[offset + (ic->argreg - 1)]->aopu.aop_reg[0]);
          offset++;
        }
    }

  freeAsmop (IC_RESULT (ic), NULL, ic, true);
}

/*-----------------------------------------------------------------*/
/* genDummyRead - generate code for dummy read of volatiles        */
/*-----------------------------------------------------------------*/
static void
genDummyRead (iCode * ic)
{
  operand *op;
  int size, offset;
  bool needpulla;

  D (emitcode (";     genDummyRead", ""));

  op = IC_RIGHT (ic);

  needpulla = pushRegIfSurv (mc6800_reg_a);
  if (op && IS_SYMOP (op))
    {

      aopOp (op, ic, false);

      size = AOP_SIZE (op);
      offset = size - 1;

      while (size--)
        {
          loadRegFromAop (mc6800_reg_a, AOP (op), offset);
          mc6800_freeReg (mc6800_reg_a);
          offset--;
        }

      freeAsmop (op, NULL, ic, true);
    }
  op = IC_LEFT (ic);
  if (op && IS_SYMOP (op))
    {

      aopOp (op, ic, false);

      size = AOP_SIZE (op);
      offset = size - 1;

      while (size--)
        {
          loadRegFromAop (mc6800_reg_a, AOP (op), offset);
          mc6800_freeReg (mc6800_reg_a);
          offset--;
        }

      freeAsmop (op, NULL, ic, true);
    }
  pullOrFreeReg (mc6800_reg_a, needpulla);
}

/*-----------------------------------------------------------------*/
/* genCritical - generate code for start of a critical sequence    */
/*-----------------------------------------------------------------*/
static void
genCritical (iCode * ic)
{
  D (emitcode (";     genCritical", ""));

  if (IC_RESULT (ic))
    aopOp (IC_RESULT (ic), ic, true);

  mc6800_emitOp ("tpa", MODE_INH, "");
  mc6800_dirtyReg (mc6800_reg_a, false);
  mc6800_emitOp ("sei", MODE_INH, "");

  if (IC_RESULT (ic))
    storeRegToAop (mc6800_reg_a, AOP (IC_RESULT (ic)), 0);
  else
    pushReg (mc6800_reg_a, false);

  mc6800_freeReg (mc6800_reg_a);
  if (IC_RESULT (ic))
    freeAsmop (IC_RESULT (ic), NULL, ic, true);
}

/*-----------------------------------------------------------------*/
/* genEndCritical - generate code for end of a critical sequence   */
/*-----------------------------------------------------------------*/
static void
genEndCritical (iCode * ic)
{
  D (emitcode (";     genEndCritical", ""));

  if (IC_RIGHT (ic))
    {
      aopOp (IC_RIGHT (ic), ic, false);
      loadRegFromAop (mc6800_reg_a, AOP (IC_RIGHT (ic)), 0);
      mc6800_emitOp ("tap", MODE_INH, "");
      mc6800_freeReg (mc6800_reg_a);
      freeAsmop (IC_RIGHT (ic), NULL, ic, true);
    }
  else
    {
      pullReg (mc6800_reg_a);
      mc6800_emitOp ("tap", MODE_INH, "");
    }
}

static void
updateiTempRegisterUse (operand * op)
{
  symbol *sym;

  if (IS_ITEMP (op))
    {
      sym = OP_SYMBOL (op);
      if (!sym->isspilt)
        {
	  /* If only used by IFX, there might not be any register assigned */
          int i;
          for(i = 0; i < sym->nRegs; i++)
            if (sym->regs[i])
              mc6800_useReg (sym->regs[i]);
        }
    }
}

/*---------------------------------------------------------------------------------------*/
/* genmc6800iCode - generate code for MC6800 based controllers for a single iCode instruction */
/*---------------------------------------------------------------------------------------*/
static void
genmc6800iCode (iCode *ic)
{
  /* if the result is marked as
     spilt and rematerializable or code for
     this has already been generated then
     do nothing */
  if (resultRemat (ic) || ic->generated)
    return;

  {
    int i;
    reg_info *reg;

    initGenLineElement ();
    genLine.lineElement.ic = ic;

    for (i = A_IDX; i <= D_IDX; i++)
      {
        reg = mc6800_regWithIdx (i);
        //if (reg->aop)
        //  emitcode ("", "; %s = %s offset %d", reg->name, aopName (reg->aop), reg->aopofs);
        reg->isFree = true;
        if (regalloc_dry_run)
          {
            reg->isLitConst = 0;
            reg->aop = NULL;
          }
      }

    if (ic->op == IFX)
      updateiTempRegisterUse (IC_COND (ic));
    else if (ic->op == JUMPTABLE)
      updateiTempRegisterUse (IC_JTCOND (ic));
    else if (ic->op == RECEIVE)
      {
        mc6800_useReg (mc6800_reg_a);
        mc6800_useReg (mc6800_reg_b);
      }
    else
      {
        if (POINTER_SET (ic))
          updateiTempRegisterUse (IC_RESULT (ic));
        updateiTempRegisterUse (IC_LEFT (ic));
        updateiTempRegisterUse (IC_RIGHT (ic));
        if (ic->prev && ic->prev->op == GET_VALUE_AT_ADDRESS && OP_SYMBOL (IC_RESULT (ic->prev))->regType == REG_CND)
          updateiTempRegisterUse (IC_LEFT (ic->prev));
      }

    for (i = A_IDX; i <= XH_IDX; i++)
      {
        if (bitVectBitValue (ic->rSurv, i))
          {
            mc6800_regWithIdx (i)->isDead = false;
            mc6800_regWithIdx (i)->isFree = false;
          }
        else
          mc6800_regWithIdx (i)->isDead = true;
      }

    mc6800_reg_d->isFree = mc6800_reg_a->isFree && mc6800_reg_b->isFree;
    mc6800_reg_d->isDead = mc6800_reg_a->isDead && mc6800_reg_b->isDead;
    mc6800_reg_x->isFree = mc6800_reg_xl->isFree && mc6800_reg_xh->isFree;
    mc6800_reg_x->isDead = mc6800_reg_xl->isDead && mc6800_reg_xh->isDead;
  }

  /* depending on the operation */
  switch (ic->op)
    {
    case '!':
      genNot (ic);
      break;

    case UNARYMINUS:
          genUminus (ic);
          break;

    case IPUSH:
          genIpush (ic);
          break;

    case IPUSH_VALUE_AT_ADDRESS:
          genPointerPush (ic);
          break;

    case IPOP:
      /* IPOP happens only when trying to restore a
         spilt live range, if there is an ifx statement
         following this pop then the if statement might
         be using some of the registers being popped which
         would destroy the contents of the register so
         we need to check for this condition and handle it */
      if (ic->next && ic->next->op == IFX && regsInCommon (IC_LEFT (ic), IC_COND (ic->next)))
        genIfx (ic->next, ic);
      else
        genIpop (ic);
      break;

    case CALL:
      genCall (ic);
      break;

    case PCALL:
      genPcall (ic);
      break;

    case FUNCTION:
      genFunction (ic);
      break;

    case ENDFUNCTION:
      genEndFunction (ic);
      break;

    case RETURN:
      genRet (ic);
      break;

    case LABEL:
      genLabel (ic);
      break;

    case GOTO:
      genGoto (ic);
      break;

    case '+':
      if (ic->prev && ic->prev->op == '=' && !POINTER_SET (ic->prev) && IS_ITEMP (IC_RESULT (ic->prev))
          && OP_SYMBOL (IC_RESULT (ic->prev))->regType == REG_CND && ic->next && ic->next->op == IFX)
        genPostIncDec (ic, ic->next);
      else
        genPlus (ic);
      break;

    case '-':
      if (ic->prev && ic->prev->op == '=' && !POINTER_SET (ic->prev) && IS_ITEMP (IC_RESULT (ic->prev))
          && OP_SYMBOL (IC_RESULT (ic->prev))->regType == REG_CND && ic->next && ic->next->op == IFX)
        genPostIncDec (ic, ic->next);
      else if (!genDjnz (ic, ifxForOp (IC_RESULT (ic), ic)))
        genMinus (ic);
      break;

    case '*':
      genMult (ic);
      break;

    case '/':
      genDiv (ic);
      break;

    case '%':
      genMod (ic);
      break;

    case '>':
    case '<':
    case LE_OP:
    case GE_OP:
      genCmp (ic, ifxForOp (IC_RESULT (ic), ic));
      break;

    case NE_OP:
    case EQ_OP:
      genCmpEQorNE (ic, ifxForOp (IC_RESULT (ic), ic));
      break;

    case AND_OP:
    case OR_OP:
      wassertl (0, "Unimplemented iCode");
      break;

    case '^':
      genXor (ic, ifxForOp (IC_RESULT (ic), ic));
      break;

    case '|':
      genOr (ic, ifxForOp (IC_RESULT (ic), ic));
      break;

    case BITWISEAND:
      genAnd (ic, ifxForOp (IC_RESULT (ic), ic));
      break;

    case INLINEASM:
      mc6800_genInline (ic);
      break;

    case GETABIT:
      genGetAbit (ic);
      break;

    case GETBYTE:
      genGetByte (ic);
      break;

    case GETWORD:
      genGetWord (ic);
      break;

    case ROT:
      genRot (ic);
      break;

    case LEFT_OP:
      genLeftShift (ic);
      break;

    case RIGHT_OP:
      genRightShift (ic);
      break;

    case GET_VALUE_AT_ADDRESS:
      if (OP_SYMBOL (IC_RESULT (ic))->regType == REG_CND && ic->next && ic->next->op != IFX)
        break;
      genPointerGet (ic, hasIncmc6800 (IC_LEFT (ic), ic, getSize (operandType (IC_RESULT (ic)))), ifxForOp (IC_RESULT (ic), ic));
      break;

    case '=':
      if (!POINTER_SET (ic) && IS_ITEMP (IC_RESULT (ic)) && OP_SYMBOL (IC_RESULT (ic))->regType == REG_CND
          && ic->next && (ic->next->op == '-' || ic->next->op == '+'))
        break;
      if (POINTER_SET (ic))
        genPointerSet (ic, hasIncmc6800 (IC_RESULT (ic), ic, getSize (operandType (IC_RIGHT (ic)))));
      else
        genAssign (ic);
      break;

    case IFX:
      genIfx (ic, NULL);
      break;

    case ADDRESS_OF:
      genAddrOf (ic);
      break;

    case JUMPTABLE:
      genJumpTab (ic);
      break;

    case CAST:
      genCast (ic);
      break;

    case RECEIVE:
      genReceive (ic);
      break;

  _G.param_offset = (currFunc && IS_STRUCT (currFunc->type->next)) ? 2 : 0;
    case SEND:
      if (!regalloc_dry_run)
        addSet (&_G.sendSet, ic);
      else if (!(ic->prev && ic->prev->op == SEND))
        {
          set * sendSet = NULL;
          addSet (&sendSet, ic);
          if (ic->next && ic->next->op == SEND)
            addSet (&sendSet, ic->next);
          genSend (sendSet);
          deleteSet (&sendSet);
        }
      break;

    case DUMMY_READ_VOLATILE:
      genDummyRead (ic);
      break;

    case CRITICAL:
      genCritical (ic);
      break;

    case ENDCRITICAL:
      genEndCritical (ic);
      break;

    default:
      wassertl (0, "Unknown iCode");
      fprintf (stderr, "ic->op: %d\n", ic->op);
    }
}

static void
init_aop_pass(void)
{
  if (mc6800_aop_pass[0])
    return;

  mc6800_aop_pass[0] = newAsmop (AOP_REG);
  mc6800_aop_pass[0]->size = 1;
  mc6800_aop_pass[0]->aopu.aop_reg[0] = mc6800_reg_b;
  mc6800_aop_pass[1] = newAsmop (AOP_REG);
  mc6800_aop_pass[1]->size = 1;
  mc6800_aop_pass[1]->aopu.aop_reg[0] = mc6800_reg_a;

  static const char *retname[8] =
    {
      "___SDCC_mc6800_ret0",
      "___SDCC_mc6800_ret1",
      "___SDCC_mc6800_ret2",
      "___SDCC_mc6800_ret3",
      "___SDCC_mc6800_ret4",
      "___SDCC_mc6800_ret5",
      "___SDCC_mc6800_ret6",
      "___SDCC_mc6800_ret7"
    };
  for (int i = 0; i < 8; i++)
    {
      mc6800_aop_ret[i] = newAsmop (AOP_DIR);
      mc6800_aop_ret[i]->size = 1;
      mc6800_aop_ret[i]->aopu.aop_dir = retname[i];
    }
}

float
drymc6800iCode (iCode *ic)
{
  int byte_cost_weight = 1;

  regalloc_dry_run = true;
  regalloc_dry_run_cost = 0;
  regalloc_dry_run_cost_cycles = 0;
  _G.stackOfs = mc6800_dry_stack_size;

  init_aop_pass();

  genmc6800iCode (ic);

  destroy_line_list ();
  /*freeTrace (&_G.trace.aops);*/

  if (optimize.codeSize)
    byte_cost_weight *= 4;
  if (!optimize.codeSpeed)
    byte_cost_weight *= 2;

  return ((float)regalloc_dry_run_cost * byte_cost_weight + 4 * regalloc_dry_run_cost_cycles * ic->count);
}

/*-----------------------------------------------------------------*/
/* genmc6800Code - generate code for MC6800 based controllers          */
/*-----------------------------------------------------------------*/
void
genmc6800Code (iCode *lic)
{
  iCode *ic;
  int cln = 0;
  int clevel = 0;
  int cblock = 0;

  regalloc_dry_run = false;

  mc6800_dirtyReg (mc6800_reg_a, false);
  mc6800_dirtyReg (mc6800_reg_b, false);
  mc6800_dirtyReg (mc6800_reg_x, false);
  mc6800_dirtyReg (mc6800_reg_d, false);

  /* print the allocation information */
  if (allocInfo && currFunc)
    printAllocInfo (currFunc, codeOutBuf);
  /* if debug information required */
  if (options.debug && currFunc && !regalloc_dry_run)
    {
      debugFile->writeFunction (currFunc, lic);
    }

  if (options.debug && !regalloc_dry_run)
    debugFile->writeFrameAddress (NULL, NULL, 0); /* have no idea where frame is now */

  init_aop_pass();

  for (ic = lic; ic; ic = ic->next)
    ic->generated = false;

  for (ic = lic; ic; ic = ic->next)
    {
      initGenLineElement ();

      genLine.lineElement.ic = ic;

      if (ic->level != clevel || ic->block != cblock)
        {
          if (options.debug)
            {
              debugFile->writeScope (ic);
            }
          clevel = ic->level;
          cblock = ic->block;
        }

      if (ic->lineno && cln != ic->lineno)
        {
          if (options.debug)
            {
              debugFile->writeCLine (ic);
            }
          if (!options.noCcodeInAsm)
            {
              emitcode ("", ";%s:%d: %s", ic->filename, ic->lineno, printCLine (ic->filename, ic->lineno));
            }
          cln = ic->lineno;
        }
      if (options.iCodeInAsm)
        {
          char regsSurv[4];
          const char *iLine;

          regsSurv[0] = (bitVectBitValue (ic->rSurv, A_IDX)) ? 'a' : '-';
          regsSurv[1] = (bitVectBitValue (ic->rSurv, B_IDX)) ? 'h' : '-';
          regsSurv[2] = (bitVectBitValue (ic->rSurv, X_IDX)) ? 'x' : '-';
          regsSurv[3] = 0;
          iLine = printILine (ic);
          emitcode ("", "; [%s] ic:%d: %s", regsSurv, ic->key, iLine);
          dbuf_free (iLine);
        }

      regalloc_dry_run_cost = 0;
      genmc6800iCode(ic);
      //if (options.verboseAsm)
      //  emitcode (";", "iCode %d (key %d) total cost: %d\n", ic->seq, ic->key, (int) regalloc_dry_run_cost);

      if (!mc6800_reg_a->isFree)
        DD (emitcode ("", "; forgot to free a"));
      if (!mc6800_reg_b->isFree)
        DD (emitcode ("", "; forgot to free b"));
      if (!mc6800_reg_x->isFree)
        DD (emitcode ("", "; forgot to free x"));
      if (!mc6800_reg_d->isFree)
        DD (emitcode ("", "; forgot to free d"));
    }

  if (options.debug)
    debugFile->writeFrameAddress (NULL, NULL, 0); /* have no idea where frame is now */


  /* now we are ready to call the
     peep hole optimizer */
  if (!options.nopeep)
    peepHole (&genLine.lineHead);

  /* now do the actual printing */
  printLine (genLine.lineHead, codeOutBuf);

  /* destroy the line list */
  destroy_line_list ();
}

