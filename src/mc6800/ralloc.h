/*-------------------------------------------------------------------------

  SDCCralloc.h - header file register allocation

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
#include "SDCCicode.h"
#include "SDCCBBlock.h"
#ifndef SDCCRALLOC_H
#define SDCCRALLOC_H 1

enum
  {
    A_IDX,
    B_IDX,
    X_IDX,
    D_IDX,
    CND_IDX,
    SP_IDX
  };


#define REG_PTR 0x01
#define REG_GPR 0x02
#define REG_CND 0x04

/* Must preserve the relation MC6800MASK_H > MC6800MASK_X > MC08MASK_A  */
/* so that MC6800MASK_REV can be automatically applied when reversing */
/* the usual register pair ordering. */
#define MC6800MASK_A 0x01
#define MC6800MASK_B 0x02
#define MC6800MASK_X 0x04
#define MC6800MASK_REV 0x08
#define MC6800MASK_D (MC6800MASK_REV | MC6800MASK_A | MC6800MASK_B)
    
/* definition for the registers */
typedef struct reg_info
  {
    short type;			/* can have value 
				   REG_GPR, REG_PTR or REG_CND */
    short rIdx;			/* index into register table */
    char *name;			/* name */
    short mask;			/* bitmask for pair allocation */
    struct asmop *aop;		/* last operand */
    int aopofs;			/* last operand offset */
    unsigned isFree:1;		/* is currently unassigned */
    unsigned isDead:1;      /* does not need to survive current instruction */
    unsigned isLitConst:1;      /* has an literal constant loaded */
    int litConst;		/* last literal constant */
  }
reg_info;
extern reg_info regsmc6800[];
extern reg_info *mc6800_reg_a;
extern reg_info *mc6800_reg_b;
extern reg_info *mc6800_reg_x;
extern reg_info *mc6800_reg_d;
extern reg_info *mc6800_reg_sp;

reg_info *mc6800_regWithIdx (int);
void mc6800_useReg (reg_info * reg);
void mc6800_freeReg (reg_info * reg);
void mc6800_dirtyReg (reg_info * reg, bool freereg);
bitVect *mc6800_rUmaskForOp (operand * op);

iCode *mc6800_ralloc2_cc(ebbIndex *ebbi);

#endif
