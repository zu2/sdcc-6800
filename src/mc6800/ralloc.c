/*------------------------------------------------------------------------

  SDCCralloc.c - source file for register allocation. 68MC6800 specific

                Written By -  Sandeep Dutta . sandeep.dutta@usa.net (1998)

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

   In other words, you are welcome to use, share and improve this program.
   You are forbidden to forbid anyone else to use, share and improve
   what you give them.   Help stamp out software-hoarding!
-------------------------------------------------------------------------*/

#include "common.h"
#include "ralloc.h"
#include "gen.h"
#include "dbuf_string.h"

extern void genmc6800Code (iCode *);

/* Shared with gen.c */
bool mc6800_far_frame;
static int mc6800_call_stack_size;

/* 6800 registers */
reg_info regsmc6800[] =
{

  {A_IDX,   "a",  MC6800MASK_A,  1, {A_IDX}, NULL, 0, 1},
  {B_IDX,   "b",  MC6800MASK_B,  1, {B_IDX}, NULL, 0, 1},
  {XL_IDX,  "xl", MC6800MASK_XL, 1, {XL_IDX}, NULL, 0, 1},
  {XH_IDX,  "xh", MC6800MASK_XH, 1, {XH_IDX}, NULL, 0, 1},
  {TEMP0L_IDX, "REGTEMP0+1", 0, 1, {TEMP0L_IDX}, NULL, 0, 1},
  {TEMP0H_IDX, "REGTEMP0", 0, 1, {TEMP0H_IDX}, NULL, 0, 1},
  {TEMP1L_IDX, "REGTEMP1+1", 0, 1, {TEMP1L_IDX}, NULL, 0, 1},
  {TEMP1H_IDX, "REGTEMP1", 0, 1, {TEMP1H_IDX}, NULL, 0, 1},
  {X_IDX,   "x",  MC6800MASK_X,  2, {XL_IDX, XH_IDX}, NULL, 0, 1},
  {D_IDX,   "d",  MC6800MASK_D,  2, {B_IDX, A_IDX}, NULL, 0, 1},
  {TEMP0_IDX, "temp0", 0, 2, {TEMP0L_IDX, TEMP0H_IDX}, NULL, 0, 1},
  {TEMP1_IDX, "temp1", 0, 2, {TEMP1L_IDX, TEMP1H_IDX}, NULL, 0, 1},

  {CND_IDX, "C",  0, 1, {CND_IDX}, NULL, 0, 1},
  {SP_IDX,  "sp", 0, 1, {SP_IDX}, NULL, 0, 1},
};
static const int mc6800_nRegs = TEMP1_IDX + 1;

reg_info *mc6800_reg_a;
reg_info *mc6800_reg_b;
reg_info *mc6800_reg_xl;
reg_info *mc6800_reg_xh;
reg_info *mc6800_reg_x;
reg_info *mc6800_reg_d;
reg_info *mc6800_reg_sp;

static void freeAllRegs ();

/*-----------------------------------------------------------------*/
/* mc6800_regWithIdx - returns pointer to register with index number */
/*-----------------------------------------------------------------*/
reg_info *
mc6800_regWithIdx (int idx)
{
  int i;

  for (i = 0; i < mc6800_nRegs; i++)
    if (regsmc6800[i].rIdx == idx)
      return &regsmc6800[i];

  werror (E_INTERNAL_ERROR, __FILE__, __LINE__,
          "regWithIdx not found");
  exit (1);
}

/*-----------------------------------------------------------------*/
/* mc6800_freeReg - frees a register                                 */
/*-----------------------------------------------------------------*/
void
mc6800_freeReg (reg_info * reg)
{
  if (!reg)
    {
      werror (E_INTERNAL_ERROR, __FILE__, __LINE__,
              "mc6800_freeReg - Freeing NULL register");
      exit (1);
    }

  reg->isFree = 1;

  switch (reg->rIdx)
    {
      case A_IDX:
        if (mc6800_reg_b->isFree)
          mc6800_reg_d->isFree = 1;
        break;
      case B_IDX:
        if (mc6800_reg_a->isFree)
          mc6800_reg_d->isFree = 1;
        break;
      case XL_IDX:
        if (mc6800_reg_xh->isFree)
          mc6800_reg_x->isFree = 1;
        break;
      case XH_IDX:
        if (mc6800_reg_xl->isFree)
          mc6800_reg_x->isFree = 1;
        break;
      case X_IDX:
        mc6800_reg_xl->isFree = 1;
        mc6800_reg_xh->isFree = 1;
        break;
      case D_IDX:
        mc6800_reg_a->isFree = 1;
        mc6800_reg_b->isFree = 1;
        break;
      default:
        break;
    }
}


/*-----------------------------------------------------------------*/
/* mc6800_useReg - marks a register  as used                         */
/*-----------------------------------------------------------------*/
void
mc6800_useReg (reg_info * reg)
{
  reg->isFree = 0;

  switch (reg->rIdx)
    {
      case A_IDX:
        mc6800_reg_d->isFree = 0;
        break;
      case B_IDX:
        mc6800_reg_d->isFree = 0;
        break;
      case XL_IDX:
        mc6800_reg_x->aop = NULL;
        mc6800_reg_x->isFree = 0;
        break;
      case XH_IDX:
        mc6800_reg_x->aop = NULL;
        mc6800_reg_x->isFree = 0;
        break;
      case X_IDX:
        mc6800_reg_xl->isFree = 0;
        mc6800_reg_xh->isFree = 0;
        break;
      case D_IDX:
        mc6800_reg_a->isFree = 0;
        mc6800_reg_b->isFree = 0;
        break;
      default:
        break;
    }
}

/*-----------------------------------------------------------------*/
/* mc6800_dirtyReg - marks a register as dirty                       */
/*-----------------------------------------------------------------*/
void
mc6800_dirtyReg (reg_info * reg, bool freereg)
{
  switch (reg->rIdx)
    {
      case XL_IDX:
      case XH_IDX:
      case X_IDX:
        mc6800_reg_x->aop = NULL;
        break;
      default:
        break;
    }
  if (freereg)
    mc6800_freeReg(reg);
}

/*-----------------------------------------------------------------*/
/* createStackSpil - create a location on the stack to spil        */
/*-----------------------------------------------------------------*/
static symbol *
createStackSpil (symbol * sym)
{
  static int slocNum;
  symbol *sloc = NULL;
  struct dbuf_s dbuf;

  dbuf_init (&dbuf, 128);
  dbuf_printf (&dbuf, "sloc%d", slocNum++);
  sloc = newiTemp (dbuf_c_str (&dbuf));
  dbuf_destroy (&dbuf);

  /* set the type to the spilling symbol */
  sloc->type = copyLinkChain (sym->type);
  sloc->etype = getSpec (sloc->type);
  SPEC_SCLS (sloc->etype) = options.xdata_spill ? S_XDATA : S_DATA;
  SPEC_EXTR (sloc->etype) = 0;
  SPEC_STAT (sloc->etype) = 0;
  SPEC_VOLATILE(sloc->etype) = 0;
  SPEC_ABSA(sloc->etype) = 0;

  allocLocal (sloc);

  sloc->isref = 1;              /* to prevent compiler warning */

  /* if it is on the stack then update the stack */
  if (IN_STACK (sloc->etype))
    currFunc->stack += getSize (sloc->type);

  sym->usl.spillLoc = sloc;
  sym->stackSpil = 1;

  /* add it to the set of itempStack set
     of the spill location */
  addSetHead (&sloc->usl.itmpStack, sym);
  return sym;
}

/*-----------------------------------------------------------------*/
/* spillThis - spils a specific operand                            */
/*-----------------------------------------------------------------*/
void
mc6800SpillThis (symbol * sym)
{
  wassert (!sym->regs[0]);

  /* if this is rematerializable or has a spillLocation
     we are okay, else we need to create a spillLocation
     for it */
  if (!(sym->remat || sym->usl.spillLoc))
    createStackSpil (sym);

  /* mark it as spilt */
  sym->isspilt = sym->spillA = 1;

  if (sym->usl.spillLoc && !sym->remat)
    sym->usl.spillLoc->allocreq++;
  return;
}

/*------------------------------------------------------------------*/
/* verifyRegsAssigned - make sure an iTemp is properly initialized; */
/* it should either have registers or have beed spilled. Otherwise, */
/* there was an uninitialized variable, so just spill this to get   */
/* the operand in a valid state.                                    */
/*------------------------------------------------------------------*/
static void
verifyRegsAssigned (operand *op, iCode * ic)
{
  symbol * sym;

  if (!op) return;
  if (!IS_ITEMP (op)) return;

  sym = OP_SYMBOL (op);
  if (sym->regType == REG_CND) return;
  if (sym->isspilt) return;
  if (!sym->nRegs) return;
  if (sym->regs[0]) return;

  mc6800SpillThis (sym);
}

/*-----------------------------------------------------------------*/
/* regTypeNum - computes the type & number of registers required   */
/*-----------------------------------------------------------------*/
static void
regTypeNum (void)
{
  symbol *sym;
  int k;

  /* for each live range do */
  for (sym = hTabFirstItem (liveRanges, &k); sym;
       sym = hTabNextItem (liveRanges, &k))
    {
      /* if used zero times then no registers needed */
      if ((sym->liveTo - sym->liveFrom) == 0)
        continue;

      /* if the live range is a temporary */
      if (sym->isitmp)
        {
          /* if the type is marked as a conditional */
          if (sym->regType == REG_CND)
            continue;

          /* if used in return only then we don't
             need registers */
          if (sym->ruonly || sym->accuse)
            {
              if (IS_AGGREGATE (sym->type) || sym->isptr)
                sym->type = aggrToPtr (sym->type, false);
              continue;
            }

          /* if not then we require registers */
          sym->nRegs = ((IS_AGGREGATE (sym->type) || sym->isptr) ?
                        getSize (sym->type = aggrToPtr (sym->type, false)) :
                        getSize (sym->type));
          if (sym->nRegs > 8)
            {
              fprintf (stderr, "allocated more than 8 registers for type ");
              printTypeChain (sym->type, stderr);
              fprintf (stderr, "\n");
            }

          sym->regType = REG_GPR;
        }
      else
        /* for the first run we don't provide */
        /* registers for true symbols we will */
        /* see how things go                  */
        sym->nRegs = 0;
    }
}

/*-----------------------------------------------------------------*/
/* freeAllRegs - mark all registers as free                        */
/*-----------------------------------------------------------------*/
static void
freeAllRegs ()
{
  int i;

  for (i = 0; i < mc6800_nRegs; i++)
    regsmc6800[i].isFree = 1;
  mc6800_reg_x->aop = NULL;
}

/*-----------------------------------------------------------------*/
/* packRegsForAssign - register reduction for assignment           */
/*-----------------------------------------------------------------*/
static int
packRegsForAssign (iCode * ic, eBBlock * ebp)
{
  iCode *dic, *sic;

  if (!IS_ITEMP (IC_RIGHT (ic)) ||
      OP_SYMBOL (IC_RIGHT (ic))->isind ||
      OP_LIVETO (IC_RIGHT (ic)) > ic->seq)
    {
      return 0;
    }

  /* find the definition of iTempNN scanning backwards if we find
     a use of the true symbol before we find the definition then
     we cannot */
  for (dic = ic->prev; dic; dic = dic->prev)
    {
      if (dic->op == CRITICAL || dic->op == INLINEASM)
        {
          dic = NULL;
          break;
        }

      if (SKIP_IC2 (dic))
        continue;

      if (dic->op == IFX)
        {
          if (IS_SYMOP (IC_COND (dic)) &&
              (IC_COND (dic)->key == IC_RESULT (ic)->key ||
               IC_COND (dic)->key == IC_RIGHT (ic)->key))
            {
              dic = NULL;
              break;
            }
        }
      else
        {
          if (IS_TRUE_SYMOP (IC_RESULT (dic)) &&
              IS_OP_VOLATILE (IC_RESULT (dic)))
            {
              dic = NULL;
              break;
            }

          if (IS_SYMOP (IC_RESULT (dic)) &&
              IC_RESULT (dic)->key == IC_RIGHT (ic)->key)
            {
              if (POINTER_SET (dic))
                dic = NULL;
              break;
            }

          if (IS_SYMOP (IC_RIGHT (dic)) &&
              (IC_RIGHT (dic)->key == IC_RESULT (ic)->key ||
               IC_RIGHT (dic)->key == IC_RIGHT (ic)->key))
            {
              dic = NULL;
              break;
            }

          if (IS_SYMOP (IC_LEFT (dic)) &&
              (IC_LEFT (dic)->key == IC_RESULT (ic)->key ||
               IC_LEFT (dic)->key == IC_RIGHT (ic)->key))
            {
              dic = NULL;
              break;
            }

          if (POINTER_SET (dic) &&
              IC_RESULT (dic)->key == IC_RESULT (ic)->key)
            {
              dic = NULL;
              break;
            }

          if (dic->op == CALL || dic->op == PCALL)
            {
              dic = NULL;
              break;
            }
        }
    }

  if (!dic)
    return 0;                   /* did not find */

  if (IS_VOLATILE (operandType (IC_RESULT (ic))))
    return 0;

  /* check that right is not a bit */
  if (IS_BITFIELD (operandType (IC_RESULT (dic))) && !IS_BITFIELD (operandType (IC_RESULT (ic))))
    return 0;

  /* found the definition */

  /* delete from liverange table also
     delete from all the points inbetween and the new
     one */
  for (sic = dic; sic != ic; sic = sic->next)
    {
      bitVectUnSetBit (sic->rlive, IC_RESULT (ic)->key);
      if (IS_ITEMP (IC_RESULT (dic)))
        bitVectSetBit (sic->rlive, IC_RESULT (dic)->key);
    }

  /* replace the result with the result of */
  /* this assignment and remove this assignment */
  bitVectUnSetBit (OP_SYMBOL (IC_RESULT (dic))->defs, dic->key);
  ReplaceOpWithCheaperOp (&IC_RESULT (dic), IC_RESULT (ic));

  if (IS_ITEMP (IC_RESULT (dic)) && OP_SYMBOL (IC_RESULT (dic))->liveFrom > dic->seq)
    {
      OP_SYMBOL (IC_RESULT (dic))->liveFrom = dic->seq;
    }
  // TODO: and the otherway around?

  remiCodeFromeBBlock (ebp, ic);
  bitVectUnSetBit (OP_DEFS (IC_RESULT (ic)), ic->key);
  hTabDeleteItem (&iCodehTab, ic->key, ic, DELETE_ITEM, NULL);
  OP_DEFS (IC_RESULT (dic)) = bitVectSetBit (OP_DEFS (IC_RESULT (dic)), dic->key);
  return 1;
}

/*-----------------------------------------------------------------*/
/* packRegsForOneuse - use the variable instead of its single-use  */
/*                     copy in an iTemp                            */
/*-----------------------------------------------------------------*/
static int
packRegsForOneuse (iCode *ic, operand **opp, eBBlock *ebp)
{
  iCode *dic;
  operand *op = *opp;

  if (!IS_ITEMP (op))
    return 0;

  if (OP_SYMBOL (op)->remat)
    return 0;

  if (bitVectnBitsOn (OP_USES (op)) != 1 || bitVectnBitsOn (OP_DEFS (op)) != 1)
    return 0;

  if (IS_SYMOP (IC_LEFT (ic)) && IS_SYMOP (IC_RIGHT (ic)) && IC_LEFT (ic)->key == IC_RIGHT (ic)->key)
    return 0;

  if (!(dic = hTabItemWithKey (iCodehTab, bitVectFirstBit (OP_DEFS (op)))))
    return 0;

  if (dic->seq < ebp->fSeq || dic->seq > ebp->lSeq)
    return 0;

  if (dic->op != '=' || POINTER_SET (dic) || !IS_TRUE_SYMOP (IC_RIGHT (dic)) || isOperandVolatile (IC_RIGHT (dic), true))
    return 0;

  if (compareType (operandType (op), operandType (IC_RIGHT (dic)), false) != 1)
    return 0;

  for (iCode *nic = dic->next; nic && nic != ic; nic = nic->next)
    {
      if (nic->op == CALL || nic->op == PCALL || POINTER_SET (nic) ||
          nic->op == INLINEASM || nic->op == CRITICAL || nic->op == ENDCRITICAL)
        return 0;

      if (nic->op == ADDRESS_OF && OP_SYMBOL (IC_RESULT (nic))->remat)
        continue;

      if (isOperandGlobal (IC_RESULT (nic)))
        return 0;

      if (IS_SYMOP (IC_RESULT (nic)) && IC_RESULT (nic)->key == IC_RIGHT (dic)->key)
        return 0;
    }

  *opp = operandFromOperand (IC_RIGHT (dic));
  (*opp)->isaddr = true;

  bitVectUnSetBit (OP_SYMBOL (op)->defs, dic->key);
  bitVectUnSetBit (OP_SYMBOL (op)->uses, ic->key);

  for (iCode *nic = dic; nic != ic; nic = nic->next)
    bitVectUnSetBit (nic->rlive, op->key);

  remiCodeFromeBBlock (ebp, dic);
  hTabDeleteItem (&iCodehTab, dic->key, dic, DELETE_ITEM, NULL);

  return 1;
}

/*------------------------------------------------------------------*/
/* moveSendToCall - move SEND to immediately precede its CALL/PCALL */
/*------------------------------------------------------------------*/
static iCode *
moveSendToCall (iCode *sic, eBBlock *ebp)
{
  iCode * prev = sic->prev;
  iCode * sic2 = NULL;
  iCode * cic;

  /* Go find the CALL/PCALL */
  cic = sic;
  while (cic && cic->op != CALL && cic->op != PCALL)
    cic = cic->next;
  if (!cic)
    return sic;

  /* Is there a second SEND? If so, we'll need to move it too. */
  if (sic->next->op == SEND)
    sic2 = sic->next;

  /* relocate the SEND(s) */
  remiCodeFromeBBlock (ebp, sic);
  addiCodeToeBBlock (ebp, sic, cic);
  if (sic2)
    {
      remiCodeFromeBBlock (ebp, sic2);
      addiCodeToeBBlock (ebp, sic2, cic);
    }

  /* Return the iCode to continue processing at. */
  if (prev)
    return prev->next;
  else
    return ebp->sch;
}


/*---------------------------------------------------------------------*/
/* removePointerCopy - make the get and set iCodes that use the offset */
/*                     pointer use the base pointer, and delete dic    */
/* All uses of pointer must follow dic in its basic block.             */
/*---------------------------------------------------------------------*/
static bool
removePointerCopy (iCode * dic, operand * pointer, operand * nonOffsetOp, eBBlock * ebp)
{
  int nuses = bitVectnBitsOn (OP_USES (pointer));
  int found = 0;
  iCode *w;
  iCode *last = NULL;

  if (!IS_ITEMP (nonOffsetOp) || !IS_PTR (operandType (nonOffsetOp)) ||
      DCL_TYPE (operandType (nonOffsetOp)) != DCL_TYPE (operandType (pointer)))
    return false;

  if (dic->seq < ebp->fSeq || dic->seq > ebp->lSeq || dic == ebp->ech)
    return false;

  for (w = dic->next; found < nuses; w = w->next)
    {
      bool defsBase = IC_RESULT (w) && IS_SYMOP (IC_RESULT (w)) && !POINTER_SET (w) &&
        OP_SYMBOL (IC_RESULT (w))->key == OP_SYMBOL (nonOffsetOp)->key;

      if (bitVectBitValue (OP_USES (pointer), w->key))
        {
          if (POINTER_SET (w) && IC_RIGHT (w) && IS_SYMOP (IC_RIGHT (w)) &&
              OP_SYMBOL (IC_RIGHT (w))->key == OP_SYMBOL (pointer)->key)
            return false;
          found++;
          last = w;
        }
      if (found < nuses && (defsBase || w == ebp->ech))
        return false;
    }

  for (w = dic->next; ; w = w->next)
    {
      w->rlive = bitVectSetBit (w->rlive, OP_SYMBOL (nonOffsetOp)->key);
      bitVectUnSetBit (w->rlive, OP_SYMBOL (pointer)->key);
      if (bitVectBitValue (OP_USES (pointer), w->key))
        {
          operand **ptr = POINTER_GET (w) ? &IC_LEFT (w) : &IC_RESULT (w);
          operand *newop = operandFromOperand (nonOffsetOp);

          newop->isaddr = (*ptr)->isaddr;
          *ptr = newop;
          OP_USES (nonOffsetOp) = bitVectSetBit (OP_USES (nonOffsetOp), w->key);
        }
      if (w == last)
        break;
    }
  if (OP_SYMBOL (nonOffsetOp)->liveTo < last->seq)
    OP_SYMBOL (nonOffsetOp)->liveTo = last->seq;

  bitVectUnSetBit (OP_USES (nonOffsetOp), dic->key);
  bitVectUnSetBit (OP_DEFS (pointer), dic->key);
  remiCodeFromeBBlock (ebp, dic);
  hTabDeleteItem (&iCodehTab, dic->key, dic, DELETE_ITEM, NULL);
  return true;
}

/*---------------------------------------------------------------------*/
/* packPointerOp - see if we can move an offset from addition iCode    */
/*                 to the pointer iCode to used indexed addr mode      */
/* The z80-related ports do this in SDCCopt.c, offsetFoldUse()         */
/*---------------------------------------------------------------------*/
static void
packPointerOp (iCode * ic, eBBlock * ebp)
{
  operand * pointer;
  operand * offsetOp;
  operand * nonOffsetOp;
  iCode * dic;
  iCode * uic;
  int key;

  if (POINTER_SET (ic))
    {
      pointer = IC_RESULT (ic);
      offsetOp = IC_LEFT (ic);
    }
  else if (POINTER_GET (ic))
    {
      pointer = IC_LEFT (ic);
      offsetOp = IC_RIGHT (ic);
    }
  else
    return;

  if (!IS_ITEMP (pointer))
    return;

  /* If the pointer is rematerializable, it's already fully optimized */
  if (OP_SYMBOL (pointer)->remat)
    return;

  if (offsetOp && IS_OP_LITERAL (offsetOp) && operandLitValue (offsetOp) != 0)
    return;
  if (offsetOp && IS_SYMOP (offsetOp))
    return;

  /* There must be only one definition */
  if (bitVectnBitsOn (OP_DEFS (pointer)) != 1)
    return;
  /* find the definition */
  if (!(dic = hTabItemWithKey (iCodehTab, bitVectFirstBit (OP_DEFS (pointer)))))
    return;

  if (dic->op == '+' && IS_OP_LITERAL (IC_RIGHT (dic)) && operandLitValue (IC_RIGHT (dic)) >= 0)
    {
      nonOffsetOp = IC_LEFT (dic);
      offsetOp = IC_RIGHT (dic);
    }
  else
    return;


  /* Now check all of the uses to make sure they are all get/set pointer */
  /* and don't already have a non-zero offset operand */
  for (key=0; key<OP_USES (pointer)->size; key++)
    {
      if (bitVectBitValue (OP_USES (pointer), key))
        {
          uic = hTabItemWithKey (iCodehTab, key);
          if (POINTER_GET (uic))
            {
              if (IC_RIGHT (uic) && IS_OP_LITERAL (IC_RIGHT (uic)) && operandLitValue (IC_RIGHT (uic)) != 0)
                return;
              if (IC_RIGHT (uic) && IS_SYMOP (IC_RIGHT (uic)))
                return;
              if (operandLitValue (offsetOp) + getSize (operandType (IC_RESULT (uic))) - 1 > 255)
                return;
            }
          else if (POINTER_SET (uic))
            {
              if (IC_LEFT (uic) && IS_OP_LITERAL (IC_LEFT (uic)) && operandLitValue (IC_LEFT (uic)) != 0)
                return;
              if (IC_LEFT (uic) && IS_SYMOP (IC_LEFT (uic)))
                return;
              if (operandLitValue (offsetOp) + getSize (operandType (IC_RIGHT (uic))) - 1 > 255)
                return;
            }
          else
            return;
        }
    }

  /* Everything checks out. Move the literal or rematerializable offset */
  /* to the pointer get/set iCodes */
  for (key=0; key<OP_USES (pointer)->size; key++)
    {
      if (bitVectBitValue (OP_USES (pointer), key))
        {
          uic = hTabItemWithKey (iCodehTab, key);
          if (POINTER_GET (uic))
            IC_RIGHT (uic) = offsetOp;
          else
            IC_LEFT (uic) = offsetOp;
        }
    }

  if (removePointerCopy (dic, pointer, nonOffsetOp, ebp))
    return;

  /* Put the remaining operand on the right and convert to assignment     */
  IC_RIGHT (dic) = nonOffsetOp;
  IC_LEFT (dic) = NULL;
  SET_ISADDR (IC_RESULT (dic), 0);
  dic->op = '=';
}

/*-----------------------------------------------------------------*/
/* packRegisters - does some transformations to reduce register    */
/*                   pressure                                      */
/*-----------------------------------------------------------------*/
static void
packRegisters (eBBlock ** ebpp, int count)
{
  iCode *ic;
  int change = 0;
  int blockno;

  for (blockno=0; blockno<count; blockno++)
    {
      eBBlock *ebp = ebpp[blockno];

      do
        {
          change = 0;

          /* look for assignments of the form */
          /* iTempNN = TrueSym (someoperation) SomeOperand */
          /*       ....                       */
          /* TrueSym := iTempNN:1             */
          for (ic = ebp->sch; ic; ic = ic->next)
            {
              /* find assignment of the form TrueSym := iTempNN:1 */
              if (ic->op == '=' && !POINTER_SET (ic))
                change += packRegsForAssign (ic, ebp);
            }
        }
      while (change);

      for (ic = ebp->sch; ic; ic = ic->next)
        {
          /* move SEND to immediately precede its CALL/PCALL */
          if (ic->op == SEND && ic->next &&
              ic->next->op != CALL && ic->next->op != PCALL)
            {
              ic = moveSendToCall (ic, ebp);
            }

          /* if this is an itemp & result of an address of a true sym
             then mark this as rematerialisable   */
          if (ic->op == ADDRESS_OF &&
              IS_ITEMP (IC_RESULT (ic)) &&
              IS_TRUE_SYMOP (IC_LEFT (ic)) &&
              bitVectnBitsOn (OP_DEFS (IC_RESULT (ic))) == 1 &&
              !IS_PARM (IC_RESULT (ic)))
            {
              OP_SYMBOL (IC_RESULT (ic))->remat = 1;
              OP_SYMBOL (IC_RESULT (ic))->rematiCode = ic;
              OP_SYMBOL (IC_RESULT (ic))->usl.spillLoc = NULL;
            }

          /* if straight assignment then carry remat flag if
             this is the only definition */
          if (ic->op == '=' &&
              !POINTER_SET (ic) &&
              IS_SYMOP (IC_RIGHT (ic)) &&
              OP_SYMBOL (IC_RIGHT (ic))->remat &&
              bitVectnBitsOn (OP_SYMBOL (IC_RESULT (ic))->defs) <= 1 &&
              !OP_SYMBOL (IC_RESULT (ic))->_isparm &&
              !OP_SYMBOL (IC_RESULT (ic))->addrtaken &&
              !isOperandGlobal (IC_RESULT (ic)))
            {
              OP_SYMBOL (IC_RESULT (ic))->remat = OP_SYMBOL (IC_RIGHT (ic))->remat;
              OP_SYMBOL (IC_RESULT (ic))->rematiCode = OP_SYMBOL (IC_RIGHT (ic))->rematiCode;
            }

          /* if cast to a generic pointer & the pointer being
             cast is remat, then we can remat this cast as well */
          if (ic->op == CAST &&
              IS_SYMOP(IC_RIGHT(ic)) &&
              OP_SYMBOL(IC_RIGHT(ic))->remat &&
              bitVectnBitsOn (OP_DEFS (IC_RESULT (ic))) == 1 &&
              !OP_SYMBOL (IC_RESULT (ic))->_isparm &&
              !OP_SYMBOL (IC_RESULT (ic))->addrtaken &&
              !isOperandGlobal (IC_RESULT (ic)))
            {
              sym_link *to_type = operandType(IC_LEFT(ic));
              sym_link *from_type = operandType(IC_RIGHT(ic));
              if (IS_PTR(to_type) && IS_PTR(from_type))
                {
                  OP_SYMBOL (IC_RESULT (ic))->remat = 1;
                  OP_SYMBOL (IC_RESULT (ic))->rematiCode = ic;
                  OP_SYMBOL (IC_RESULT (ic))->usl.spillLoc = NULL;
                }
            }

          /* if this is a +/- operation with a rematerizable
             then mark this as rematerializable as well */
          if ((ic->op == '+' || ic->op == '-') &&
              (IS_SYMOP (IC_LEFT (ic)) &&
               IS_ITEMP (IC_RESULT (ic)) &&
               IS_OP_LITERAL (IC_RIGHT (ic))) &&
               OP_SYMBOL (IC_LEFT (ic))->remat &&
              (!IS_SYMOP (IC_RIGHT (ic)) || !IS_CAST_ICODE(OP_SYMBOL (IC_RIGHT (ic))->rematiCode)) &&
               bitVectnBitsOn (OP_DEFS (IC_RESULT (ic))) == 1)
            {
              OP_SYMBOL (IC_RESULT (ic))->remat = 1;
              OP_SYMBOL (IC_RESULT (ic))->rematiCode = ic;
              OP_SYMBOL (IC_RESULT (ic))->usl.spillLoc = NULL;
            }
          if (ic->op == GET_VALUE_AT_ADDRESS ||
            ic->op == '+' || ic->op == '-' || ic->op == UNARYMINUS ||
            ic->op == '|' || ic->op == BITWISEAND || ic->op == '^' ||
            ic->op == EQ_OP || ic->op == NE_OP ||
            ic->op == IFX && operandSize (IC_COND (ic)) == 1 ||
            ic->op == IPUSH && operandSize (IC_LEFT (ic)) <= 2 ||
            ic->op == LEFT_OP || ic->op == RIGHT_OP)
            packRegsForOneuse (ic, &(IC_LEFT (ic)), ebp);
          if (ic->op == '+' || ic->op == '-' ||
            ic->op == '|' || ic->op == BITWISEAND || ic->op == '^' ||
            ic->op == EQ_OP || ic->op == NE_OP ||
            ic->op == LEFT_OP || ic->op == RIGHT_OP)
            packRegsForOneuse (ic, &(IC_RIGHT (ic)), ebp);

          if (POINTER_SET (ic) || POINTER_GET (ic))
            packPointerOp (ic, ebp);
        }
    }
}

void
mc6800RegFix (eBBlock ** ebbs, int count)
{
  int i;

  /* Check for and fix any problems with uninitialized operands */
  for (i = 0; i < count; i++)
    {
      iCode *ic;

      if (ebbs[i]->noPath && (ebbs[i]->entryLabel != entryLabel && ebbs[i]->entryLabel != returnLabel))
        continue;

      for (ic = ebbs[i]->sch; ic; ic = ic->next)
        {
          if (SKIP_IC2 (ic))
            continue;

          verifyRegsAssigned (IC_RESULT (ic), ic);
          verifyRegsAssigned (IC_LEFT (ic), ic);
          verifyRegsAssigned (IC_RIGHT (ic), ic);
        }
    }
}

/** Serially allocate registers to the variables.
    This was the main register allocation function.  It is called after
    packing.
    In the new register allocator it only serves to mark variables for the new register allocator.
 */
static void
serialRegMark (eBBlock ** ebbs, int count)
{
  int i;
  short int max_alloc_bytes = SHRT_MAX; // Byte limit. Set this to a low value to pass only few variables to the register allocator. This can be useful for debugging.

  mc6800_call_stack_size = 0;

  /* for all blocks */
  for (i = 0; i < count; i++)
    {
      iCode *ic;

      if (ebbs[i]->noPath &&
          (ebbs[i]->entryLabel != entryLabel &&
           ebbs[i]->entryLabel != returnLabel))
        continue;

      /* for all instructions do */
      for (ic = ebbs[i]->sch; ic; ic = ic->next)
        {
          if ((ic->op == CALL || ic->op == PCALL) && ic->parmBytes + 2 > mc6800_call_stack_size)
            mc6800_call_stack_size = ic->parmBytes + 2;

          /* if result is present && is a true symbol */
          if (IC_RESULT (ic) && ic->op != IFX &&
              IS_TRUE_SYMOP (IC_RESULT (ic)))
            {
              OP_SYMBOL (IC_RESULT (ic))->allocreq++;
            }

          /* some don't need registers */
          if (SKIP_IC2 (ic) ||
              ic->op == JUMPTABLE ||
              ic->op == IFX ||
              ic->op == IPUSH ||
              (IC_RESULT (ic) && POINTER_SET (ic)))
            {
              continue;
            }

          /* now we need to allocate registers only for the result */
          if (IC_RESULT (ic))
            {
              symbol *sym = OP_SYMBOL (IC_RESULT (ic));

              if (sym->isspilt && sym->usl.spillLoc)
                {
                  sym->usl.spillLoc->allocreq--;
                  sym->isspilt = false;
                }

              /* if it does not need or is spilt
                 or will not live beyond this instructions */
              if (!sym->nRegs ||
                  sym->isspilt ||
                  sym->liveTo <= ic->seq)
                {
                  continue;
                }

              if (sym->usl.spillLoc && !sym->isreqv && !sym->stackSpil)
                sym->usl.spillLoc = NULL;

              if (max_alloc_bytes >= sym->nRegs)
                {
                  sym->for_newralloc = 1;
                  max_alloc_bytes -= sym->nRegs;
                }
              else if (!sym->for_newralloc)
                {
                  mc6800SpillThis (sym);
                  printf ("Spilt %s due to byte limit.\n", sym->name);
                }
            }
        }
    }
}

/*-----------------------------------------------------------------*/
/* New register allocator                                          */
/*-----------------------------------------------------------------*/
void
mc6800_assignRegisters (ebbIndex * ebbi)
{
  eBBlock ** ebbs = ebbi->bbOrder;
  int count = ebbi->count;
  iCode *ic;

  mc6800_far_frame = false;
  mc6800_placeFixedStackVars ();
  mc6800_reg_a = mc6800_regWithIdx(A_IDX);
  mc6800_reg_b = mc6800_regWithIdx(B_IDX);
  mc6800_reg_xl = mc6800_regWithIdx(XL_IDX);
  mc6800_reg_xh = mc6800_regWithIdx(XH_IDX);
  mc6800_reg_x = mc6800_regWithIdx(X_IDX);
  mc6800_reg_d = mc6800_regWithIdx(D_IDX);
  mc6800_reg_sp = &regsmc6800[SP_IDX];

  /* change assignments this will remove some
     live ranges reducing some register pressure */

  packRegisters (ebbs, count);

  /* liveranges probably changed by register packing
     so we compute them again */
  recomputeLiveRanges (ebbs, count, false);

  if (options.dump_i_code)
    dumpEbbsToFileExt (DUMP_PACK, ebbi);

  /* first determine for each live range the number of
     registers & the type of registers required for each */
  regTypeNum ();

  /* and serially allocate registers */
  serialRegMark (ebbs, count);

  /* The new register allocator invokes its magic */
  mc6800_ralloc2_cc (ebbi);

  if (currFunc && currFunc->stack + mc6800_call_stack_size > 255)
    {
      mc6800_far_frame = true;
      serialRegMark (ebbs, count);
      mc6800_ralloc2_cc (ebbi);
    }

  if (options.dump_i_code)
    {
      dumpEbbsToFileExt (DUMP_RASSGN, ebbi);
      dumpLiveRanges (DUMP_LRANGE, liveRanges);
    }

  /* do the overlaysegment stuff SDCCmem.c */
  doOverlays (ebbs, count);

  /* now get back the chain */
  ic = iCodeLabelOptimize (iCodeFromeBBlock (ebbs, count));

  genmc6800Code (ic);

  /* mark all registers as free */
  freeAllRegs ();

  return;
}

