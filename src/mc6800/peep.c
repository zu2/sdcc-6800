#include "common.h"
#include "SDCCgen.h"

#include "ralloc.h"
#include "peep.h"

#define NOTUSEDERROR() do {werror(E_INTERNAL_ERROR, __FILE__, __LINE__, "error in notUsed()");} while(0)

typedef enum
{
  S4O_CONDJMP,
  S4O_WR_OP,
  S4O_RD_OP,
  S4O_TERM,
  S4O_VISITED,
  S4O_ABORT
} S4O_RET;

static struct
{
  lineNode *head;
} _G;

static bool
incLabelJmpToCount (const char *label)
{
  labelHashEntry *entry;

  entry = getLabelRef (label, _G.head);
  if (!entry)
    return false;
  entry->jmpToCount++;
  return true;
}

static lineNode *
findLabel (const lineNode *pl)
{
  const char *p;
  lineNode *cpl;

  p = strlen (pl->line) - 1 + pl->line;

  while (p > pl->line && isspace (*p))
    p--;

  for (; p > pl->line; p--)
    if (isspace (*p) || *p == ',')
      break;

  if (p == pl->line)
    {
      NOTUSEDERROR ();
      return NULL;
    }

  ++p;

  if (!incLabelJmpToCount (p))
    return NULL;

  for (cpl = _G.head; cpl; cpl = cpl->next)
    if (cpl->isLabel && strncmp (p, cpl->line, strlen (p)) == 0 && cpl->line[strlen (p)] == ':')
      return cpl;

  return NULL;
}

static const mc6800opcodedata *
opcodeOfLine (const lineNode *pl)
{
  char inst[sizeof (((mc6800opcodedata *) 0)->name)];
  size_t n;

  for (n = 0; pl->line[n] && !isspace (pl->line[n]); n++)
    if (n + 1 >= sizeof (inst))
      return NULL;
  memcpy (inst, pl->line, n);
  inst[n] = 0;
  return mc6800_getOpcodeData (inst);
}

static int
flagIndex (const char *what)
{
  if (!strcmp (what, "h"))
    return 0;
  if (!strcmp (what, "n"))
    return 2;
  if (!strcmp (what, "z"))
    return 3;
  if (!strcmp (what, "v"))
    return 4;
  if (!strcmp (what, "c"))
    return 5;
  return -1;
}

static unsigned char
regMask (const char *what)
{
  if (!strcmp (what, "a"))
    return M_A;
  if (!strcmp (what, "b"))
    return M_B;
  if (!strcmp (what, "d"))
    return M_A | M_B;
  if (!strcmp (what, "x"))
    return M_X;
  return 0;
}

static bool
mc6800MightReadFlag (const lineNode *pl, const char *what)
{
  static const struct
  {
    const char *inst;
    const char *flags;
  } flagReaders[] =
  {
    {"beq", "z"}, {"bne", "z"}, {"bcc", "c"}, {"bcs", "c"}, {"bmi", "n"}, {"bpl", "n"},
    {"bvc", "v"}, {"bvs", "v"}, {"bhi", "cz"}, {"bls", "cz"}, {"bge", "nv"}, {"blt", "nv"},
    {"bgt", "nvz"}, {"ble", "nvz"}, {"adca", "c"}, {"adcb", "c"}, {"sbca", "c"}, {"sbcb", "c"},
    {"rol", "c"}, {"rola", "c"}, {"rolb", "c"}, {"ror", "c"}, {"rora", "c"}, {"rorb", "c"},
    {"daa", "ch"}, {"tpa", "hnzvc"}
  };

  for (size_t i = 0; i < sizeof (flagReaders) / sizeof (flagReaders[0]); i++)
    if (lineIsInst (pl, flagReaders[i].inst))
      return strchr (flagReaders[i].flags, what[0]);
  return false;
}

static bool
mc6800ReturnReads (const char *what)
{
  sym_link *type;
  int size;

  if (!currFunc)
    return true;
  type = currFunc->type->next;
  if (IS_VOID (type) || IS_STRUCT (type))
    return false;
  size = getSize (type);
  if (size == 1)
    return regMask (what) & M_B;
  if (size == 2)
    return regMask (what) & (M_A | M_B);
  return false;
}

static bool
mc6800MightRead (const lineNode *pl, const char *what)
{
  const mc6800opcodedata *op;

  if (flagIndex (what) >= 0)
    return mc6800MightReadFlag (pl, what);

  if (lineIsInst (pl, "rts"))
    return mc6800ReturnReads (what);
  if (lineIsInst (pl, "rti"))
    return false;
  if (lineIsInst (pl, "jsr") || lineIsInst (pl, "bsr"))
    return (regMask (what) & (M_A | M_B)) || ((regMask (what) & M_X) && strstr (pl->line, ",x"));

  op = opcodeOfLine (pl);
  if (!op)
    return true;
  if (op->use & regMask (what))
    return true;
  if ((regMask (what) & M_X) && strstr (pl->line, ",x"))
    return true;
  return false;
}

static bool
mc6800SurelyWrites (const lineNode *pl, const char *what)
{
  const mc6800opcodedata *op;
  int f = flagIndex (what);

  if (f >= 0 && (lineIsInst (pl, "jsr") || lineIsInst (pl, "bsr")))
    return true;

  op = opcodeOfLine (pl);
  if (!op)
    return false;
  if (f >= 0)
    return op->flags[f] != '.';
  return (op->change & regMask (what)) == regMask (what);
}

static bool
mc6800UncondJump (const lineNode *pl)
{
  return lineIsInst (pl, "jmp") || lineIsInst (pl, "bra");
}

static bool
mc6800CondJump (const lineNode *pl)
{
  const mc6800opcodedata *op = opcodeOfLine (pl);

  return op && op->type == OP_BR && !lineIsInst (pl, "bra") && !lineIsInst (pl, "bsr");
}

static bool
mc6800SurelyReturns (const lineNode *pl)
{
  return lineIsInst (pl, "rts") || lineIsInst (pl, "rti");
}

static S4O_RET
scan4op (lineNode **pl, const char *what, lineNode **plCond)
{
  for (; *pl; *pl = (*pl)->next)
    {
      if (!(*pl)->line || (*pl)->isDebug || (*pl)->isComment || (*pl)->isLabel)
        continue;
      if ((*pl)->isInline)
        return S4O_ABORT;
      if ((*pl)->visited)
        return S4O_VISITED;
      (*pl)->visited = true;

      if (lineIsInst (*pl, "swi") || lineIsInst (*pl, "wai"))
        return S4O_ABORT;

      if (mc6800MightRead (*pl, what))
        return S4O_RD_OP;

      if (mc6800SurelyWrites (*pl, what))
        return S4O_WR_OP;

      if (mc6800UncondJump (*pl))
        {
          if (strstr ((*pl)->line, ",x"))
            return S4O_ABORT;
          *pl = findLabel (*pl);
          if (!*pl)
            return S4O_ABORT;
        }
      if (mc6800CondJump (*pl))
        {
          *plCond = findLabel (*pl);
          if (!*plCond)
            return S4O_ABORT;
          return S4O_CONDJMP;
        }

      if (mc6800SurelyReturns (*pl))
        return S4O_TERM;
    }
  return S4O_ABORT;
}

static bool
doTermScan (lineNode **pl, const char *what)
{
  lineNode *plConditional;

  for (;; *pl = (*pl)->next)
    {
      switch (scan4op (pl, what, &plConditional))
        {
        case S4O_TERM:
        case S4O_VISITED:
        case S4O_WR_OP:
          return true;
        case S4O_CONDJMP:
          {
            lineNode *pl2 = plConditional;
            if (!doTermScan (&pl2, what))
              return false;
          }
          continue;
        case S4O_RD_OP:
        default:
          return false;
        }
    }
}

static void
unvisitLines (lineNode *pl)
{
  for (; pl; pl = pl->next)
    pl->visited = false;
}

bool
mc6800notUsed (const char *what, lineNode *endPl, lineNode *head)
{
  lineNode *pl;

  if (flagIndex (what) < 0 && !regMask (what))
    {
      NOTUSEDERROR ();
      return false;
    }

  _G.head = head;
  unvisitLines (_G.head);
  pl = endPl->next;
  return doTermScan (&pl, what);
}

bool
mc6800notUsedFrom (const char *what, const char *label, lineNode *head)
{
  lineNode *cpl;

  for (cpl = head; cpl; cpl = cpl->next)
    if (cpl->isLabel && !strncmp (label, cpl->line, strlen (label)) && cpl->line[strlen (label)] == ':')
      return mc6800notUsed (what, cpl, head);

  return false;
}
