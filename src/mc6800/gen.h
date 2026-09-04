/*-------------------------------------------------------------------------
  gen.h - header file for code generation for hc(s)08

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
-------------------------------------------------------------------------*/

#ifndef SDCCGENMC6800_H
#define SDCCGENMC6800_H

typedef enum
  {
  AOP_INVALID,
  AOP_LIT = 1,   /* operand is a literal value */
  AOP_REG,       /* is in registers */
  AOP_DIR,       /* operand using direct addressing mode */
  AOP_STK,       /* should be pushed on stack this
                    can happen only for the result */
  AOP_IMMD,      /* immediate value for eg. remateriazable */
  AOP_STR,       /* array of strings */
  AOP_CRY,       /* carry contains the value of this */
  AOP_EXT,       /* operand using extended addressing mode */
  AOP_SOF,       /* operand at an offset on the stack */
  AOP_DUMMY,     /* Read undefined, discard writes */
  AOP_IDX        /* operand using indexed addressing mode */
  }
AOP_TYPE;

enum
  {
    ACCUSE_D = 1,
    ACCUSE_X
  };

/* type asmop : a homogenised type for 
   all the different spaces an operand can be
   in */
typedef struct asmop
  {
    AOP_TYPE type;		
    short coff;			/* current offset */
    short size;			/* total size */
    short regmask;              /* register mask if AOP_REG */
    operand *op;		/* originating operand */
    unsigned code:1;		/* is in Code space */
    unsigned freed:1;		/* already freed    */
    unsigned stacked:1;		/* partial results stored on stack */
    struct asmop *stk_aop[4];	/* asmops for the results on the stack */
    union
      {
	value *aop_lit;		/* if literal */
	reg_info *aop_reg[4];	/* array of registers */
	char *aop_dir;		/* if direct  */
        char *aop_immd;         /* if immediate */
	int aop_stk;		/* stack offset when AOP_STK */
      }
    aopu;
    struct valinfo valinfo;
  }
asmop;

void genmc6800Code (iCode *);
void mc6800_emitDebuggerSymbol (const char *);

extern unsigned fReturnSizeMC6800;

iCode *hasIncmc6800 (operand *op, const iCode *ic, int osize);
extern bool mc6800_assignment_optimal;

#endif

