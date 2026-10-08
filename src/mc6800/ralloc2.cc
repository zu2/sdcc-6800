// Philipp Klaus Krause, philipp@informatik.uni-frankfurt.de, pkk@spth.de, 2010 - 2011
//
// (c) 2012 Goethe-Universität Frankfurt
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2, or (at your option) any
// later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
//
// An optimal, polynomial-time register allocator.

//#define DEBUG_RALLOC_DEC // Uncomment to get debug messages while doing register allocation on the tree decomposition.
//#define DEBUG_RALLOC_DEC_ASS // Uncomment to get debug messages about assignments while doing register allocation on the tree decomposition (much more verbose than the one above).

#include "SDCCralloc.hpp"
#include "SDCCsalloc.hpp"

extern "C"
{
  #include "ralloc.h"
  #include "gen.h"
  float drymc6800iCode (iCode *ic);
  bool mc6800_assignment_optimal;
}

static const short places[] = {A_IDX, B_IDX, D_IDX, X_IDX, TEMP0_IDX, TEMP1_IDX};

static int place_bytes(reg_t r)
{
  const reg_info *reg = mc6800_regWithIdx(places[r]);
  int bytes = 0;

  for(int k = 0; k < reg->size; k++)
    bytes |= 1 << reg->bytes[k];

  return(bytes);
}

template <class I_t>
static void add_operand_conflicts_in_node(const cfg_node &n, I_t &I)
{
}

template <class G_t>
static int operand_reg(const operand *o, const assignment &a, unsigned short int i, const G_t &G)
{
  if(!o || !IS_SYMOP(o))
    return(-1);

  operand_map_t::const_iterator oi = G[i].operands.find(OP_SYMBOL_CONST(o)->key);
  if(oi == G[i].operands.end() || a.global[oi->second] < 0)
    return(-1);

  return(places[a.global[oi->second]]);
}

static bool survives_in(const iCode *ic, int idx)
{
  const reg_info *reg = mc6800_regWithIdx(idx);

  for(int k = 0; k < reg->size; k++)
    if(bitVectBitValue(ic->rSurv, reg->bytes[k]))
      return(true);

  return(false);
}

static void assign_symbol(symbol *sym, reg_t r, int size)
{
  if(r < 0)
    {
      for(int k = 0; k < size; k++)
        sym->regs[k] = 0;
      return;
    }

  const reg_info *reg = mc6800_regWithIdx(places[r]);
  for(int k = 0; k < reg->size; k++)
    sym->regs[k] = mc6800_regWithIdx(reg->bytes[k]);
}

template <class G_t, class I_t>
static void set_surviving_regs(const assignment &a, unsigned short int i, const G_t &G, const I_t &I)
{
  iCode *ic = G[i].ic;

  bitVectClear(ic->rSurv);

  cfg_alive_t::const_iterator v, v_end;
  for (v = G[i].alive.begin(), v_end = G[i].alive.end(); v != v_end; ++v)
    {
      if(a.global[*v] < 0)
        continue;
      if(G[i].dying.find(*v) == G[i].dying.end())
        if(!((IC_RESULT(ic) && !POINTER_SET(ic)) && IS_SYMOP(IC_RESULT(ic)) && OP_SYMBOL_CONST(IC_RESULT(ic))->key == I[*v].v))
          for(int k = 0; k < I[*v].size; k++)
            ic->rSurv = bitVectSetBit(ic->rSurv, mc6800_regWithIdx(places[a.global[*v]])->bytes[k]);
    }
}

template <class G_t, class I_t>
static void assign_operand_for_cost(operand *o, const assignment &a, unsigned short int i, const G_t &G, const I_t &I)
{
  if(!o || !IS_SYMOP(o))
    return;
  symbol *sym = OP_SYMBOL(o);
  operand_map_t::const_iterator oi = G[i].operands.find(OP_SYMBOL_CONST(o)->key);
  if(oi == G[i].operands.end())
    return;
  assign_symbol(sym, a.global[oi->second], I[oi->second].size);
  sym->isspilt = (a.global[oi->second] < 0);
}

template <class G_t, class I_t>
static void assign_operands_for_cost(const assignment &a, unsigned short int i, const G_t &G, const I_t &I)
{
  const iCode *ic = G[i].ic;

  assign_operand_for_cost(IC_LEFT(ic), a, i, G, I);
  assign_operand_for_cost(IC_RIGHT(ic), a, i, G, I);
  assign_operand_for_cost(IC_RESULT(ic), a, i, G, I);

  if(ic->op == SEND && (ic->builtinSEND || ic->next && ic->next->op == SEND))
    {
      assign_operands_for_cost(a, *(adjacent_vertices(i, G).first), G, I);
    }
}

// Cost function.

template <class G_t, class I_t>
static float instruction_cost(const assignment &a, unsigned short int i, const G_t &G, const I_t &I)
{
  iCode *ic = G[i].ic;
  float c;

  wassert (TARGET_IS_MC6800);

#if 0
  std::cout << "Calculating at cost at ic " << ic->key << " for: ";
  for(unsigned int i = 0; i < boost::num_vertices(I); i++)
  {
  	std::cout << "(" << i << ", " << int(a.global[i]) << ") ";
  }
  std::cout << "\n";
  std::cout.flush();
#endif

  if(ic->generated)
    return(0.0f);

  set_surviving_regs(a, i, G, I);

  if(ic->op != '=' && operand_reg(IC_RESULT(ic), a, i, G) == A_IDX &&
    (operand_reg(IC_LEFT(ic), a, i, G) == D_IDX || operand_reg(IC_RIGHT(ic), a, i, G) == D_IDX))
    return(std::numeric_limits<float>::infinity());

  if((ic->op == CALL || ic->op == PCALL) && (survives_in(ic, X_IDX) || survives_in(ic, TEMP0_IDX) || survives_in(ic, TEMP1_IDX)))
    return(std::numeric_limits<float>::infinity());

  if(ic->op == RECEIVE && ic->next && ic->next->op == RECEIVE && operand_reg(IC_RESULT(ic), a, i, G) == A_IDX)
    return(std::numeric_limits<float>::infinity());

  if(ic->op == INLINEASM && (survives_in(ic, D_IDX) || survives_in(ic, X_IDX)))
    return(std::numeric_limits<float>::infinity());

  switch(ic->op)
    {
    // Register assignment doesn't matter for these:
    case FUNCTION:
    case ENDFUNCTION:
    case LABEL:
    case GOTO:
    case INLINEASM:
      return(0.0f);
    case '!':
    case UNARYMINUS:
    case '+':
    case '-':
    case '^':
    case '|':
    case BITWISEAND:
    case IPUSH:
    case IPUSH_VALUE_AT_ADDRESS:
    case CALL:
    case PCALL:
    case RETURN:
    case '*':
    case '/':
    case '%':
    case '>':
    case '<':
    case LE_OP:
    case GE_OP:
    case EQ_OP:
    case NE_OP:
    case AND_OP:
    case OR_OP:
    case GETABIT:
    case GETBYTE:
    case GETWORD:
    case ROT:
    case LEFT_OP:
    case RIGHT_OP:
    case GET_VALUE_AT_ADDRESS:
    case '=':
    case IFX:
    case ADDRESS_OF:
    case JUMPTABLE:
    case CAST:
    case RECEIVE:
    case SEND:
    case DUMMY_READ_VOLATILE:
    case CRITICAL:
    case ENDCRITICAL:
      assign_operands_for_cost(a, i, G, I);
      c = drymc6800iCode(ic);
      ic->generated = false;
      return(c);
    default:
      wassertl (0, "Unknown iCode");
      return(0.0f);
    }
}

template <class G_t, class I_t>
static bool assignment_hopeless(const assignment &a, unsigned short int i, const G_t &G, const I_t &I, const var_t lastvar)
{
  reg_t r = a.global[lastvar];

  if(I[lastvar].size != mc6800_regWithIdx(places[r])->size)
    return(true);

  varset_t::const_iterator w, w_end;
  for(w = a.local.begin(), w_end = a.local.end(); w != w_end; ++w)
    {
      reg_t s = a.global[*w];
      if(s != r && (place_bytes(s) & place_bytes(r)) && boost::edge(*w, lastvar, I).second)
        return(true);
    }

  return(false);
}

// Increase chance of finding good compatible assignments at join nodes. This is just a dummy for now, it probably isn't really needed for mc6800 due to the low number of registers.
template <class T_t>
static void get_best_local_assignment_biased(assignment &a, typename boost::graph_traits<T_t>::vertex_descriptor t, const T_t &T)
{
  a = *T[t].assignments.begin();

  varset_t newlocal;
  std::set_union(T[t].alive.begin(), T[t].alive.end(), a.local.begin(), a.local.end(), std::inserter(newlocal, newlocal.end()));
  a.local = newlocal;
}

// This is just a dummy for now, it probably isn't really needed for mc6800 due to the low number of registers.
template <class G_t, class I_t>
static float rough_cost_estimate(const assignment &a, unsigned short int i, const G_t &G, const I_t &I)
{
  return(0.0f);
}

// Code for another ic is generated when generating this one. Mark the other as generated.
static void extra_ic_generated(iCode *ic)
{
  if(IS_CONDITIONAL (ic) || IS_BITWISE_OP (ic) || ic->op == GET_VALUE_AT_ADDRESS)
    {
      iCode *ifx;
      if (ic->op == GET_VALUE_AT_ADDRESS && getSize(operandType(IC_RESULT (ic))) > 1 && IS_BITVAR (getSpec (operandType (IC_RESULT (ic)))))
        return;
      if (ifx = ifxForOp (IC_RESULT (ic), ic))
        {
          OP_SYMBOL (IC_RESULT (ic))->for_newralloc = false;
          OP_SYMBOL (IC_RESULT (ic))->regType = REG_CND;
          ifx->generated = true;
        }
    }
}

template <class T_t, class G_t, class I_t>
static bool tree_dec_ralloc(T_t &T, G_t &G, const I_t &I)
{
  bool assignment_optimal;

  con2_t I2(boost::num_vertices(I));
  for(unsigned int i = 0; i < boost::num_vertices(I); i++)
    {
      I2[i].v = I[i].v;
      I2[i].byte = I[i].byte;
      I2[i].size = I[i].size;
      I2[i].name = I[i].name;
    }
  typename boost::graph_traits<I_t>::edge_iterator e, e_end;
  for(boost::tie(e, e_end) = boost::edges(I); e != e_end; ++e)
    add_edge(boost::source(*e, I), boost::target(*e, I), I2);

  assignment ac;
  assignment_optimal = true;
  tree_dec_ralloc_nodes(T, find_root(T), G, I2, ac, &assignment_optimal);

  const assignment &winner = *(T[find_root(T)].assignments.begin());

#ifdef DEBUG_RALLOC_DEC
  std::cout << "Winner: ";
  for(unsigned int i = 0; i < boost::num_vertices(I); i++)
  {
  	std::cout << "(" << i << ", " << int(winner.global[i]) << ") ";
  }
  std::cout << "\n";
  std::cout << "Cost: " << winner.s << "\n";
  std::cout.flush();
#endif

  // Todo: Make this an assertion
  if(winner.global.size() != boost::num_vertices(I))
    {
      std::cerr << "ERROR: No Assignments at root\n";
      exit(-1);
    }

  for(unsigned int v = 0; v < boost::num_vertices(I); v++)
    {
      symbol *sym = (symbol *)(hTabItemWithKey(liveRanges, I[v].v));
      assign_symbol(sym, winner.global[v], I[v].size);
      if(winner.global[v] < 0)
        mc6800SpillThis(sym);
      else
        sym->isspilt = false;
    }

  for(unsigned int i = 0; i < boost::num_vertices(G); i++)
    set_surviving_regs(winner, i, G, I);

  return(!assignment_optimal);
}

static void make_value_graph(cfg_t &G, con_t &I)
{
  std::vector<var_t> value_of(boost::num_vertices(I));
  var_t n = -1;
  for(var_t v = 0; v < (var_t)boost::num_vertices(I); v++)
    {
      if(I[v].byte == 0)
        n++;
      value_of[v] = n;
    }

  con_t J(n + 1);
  for(var_t v = 0; v < (var_t)boost::num_vertices(I); v++)
    if(I[v].byte == 0)
      J[value_of[v]] = I[v];
  boost::graph_traits<con_t>::edge_iterator e, e_end;
  for(boost::tie(e, e_end) = boost::edges(I); e != e_end; ++e)
    if(value_of[boost::source(*e, I)] != value_of[boost::target(*e, I)])
      boost::add_edge(value_of[boost::source(*e, I)], value_of[boost::target(*e, I)], J);
  I = J;

  for(unsigned int i = 0; i < boost::num_vertices(G); i++)
    {
      cfg_alive_t alive;
      for(cfg_alive_t::const_iterator v = G[i].alive.begin(); v != G[i].alive.end(); ++v)
        alive.push_back(value_of[*v]);
      alive.erase(std::unique(alive.begin(), alive.end()), alive.end());
      G[i].alive = alive;

      cfg_dying_t dying;
      for(cfg_dying_t::const_iterator v = G[i].dying.begin(); v != G[i].dying.end(); ++v)
        dying.insert(value_of[*v]);
      G[i].dying = dying;

      operand_map_t operands;
      for(operand_map_t::const_iterator oi = G[i].operands.begin(); oi != G[i].operands.end(); ++oi)
        if(operands.find(oi->first) == operands.end())
          operands.insert(std::pair<int, var_t>(oi->first, value_of[oi->second]));
      G[i].operands = operands;
    }
}

iCode *mc6800_ralloc2_cc(ebbIndex *ebbi)
{
#ifdef DEBUG_RALLOC_DEC
  std::cout << "Processing " << currFunc->name << " from " << dstFileName << "\n"; std::cout.flush();
#endif

  cfg_t control_flow_graph;

  con_t conflict_graph;

  iCode *ic = create_cfg(control_flow_graph, conflict_graph, ebbi);

  make_value_graph(control_flow_graph, conflict_graph);

  if (optimize.genconstprop)
    recomputeValinfos (ic, ebbi, "_2");

  guessCounts(ic, ebbi);

  if(options.dump_graphs)
    dump_cfg(control_flow_graph);

  if(options.dump_graphs)
    dump_con(conflict_graph);

  tree_dec_t tree_decomposition;

  get_nice_tree_decomposition(tree_decomposition, control_flow_graph);

  alive_tree_dec(tree_decomposition, control_flow_graph);

  good_re_root(tree_decomposition);
  nicify(tree_decomposition);
  alive_tree_dec(tree_decomposition, control_flow_graph);

  if(options.dump_graphs)
    dump_tree_decomposition(tree_decomposition);

  guessCounts (ic, ebbi);

  mc6800_assignment_optimal = !tree_dec_ralloc(tree_decomposition, control_flow_graph, conflict_graph);

  mc6800RegFix(ebbi->bbOrder, ebbi->count);

  scon_t stack_conflict_graph;

  set_spilt(control_flow_graph, conflict_graph, stack_conflict_graph);

  if(options.stackAuto || IFFUNC_ISREENT(currFunc->type))
    {
      std::map<const symbol *, var_t> sindex;

      for(var_t v = 0; v < (var_t)boost::num_vertices(stack_conflict_graph); v++)
        sindex[stack_conflict_graph[v].sym] = v;
      for(unsigned int i = 0; i < boost::num_vertices(control_flow_graph); i++)
        {
          const iCode *gic = control_flow_graph[i].ic;

          if(gic->op != GETWORD || !IS_SYMOP(IC_RESULT(gic)) || !IS_SYMOP(IC_LEFT(gic)))
            continue;
          auto result = sindex.find(OP_SYMBOL(IC_RESULT(gic)));
          auto left = sindex.find(OP_SYMBOL(IC_LEFT(gic)));
          if(result == sindex.end() || left == sindex.end())
            continue;
          const auto edge = boost::add_edge(result->second, left->second, stack_conflict_graph).first;
          stack_conflict_graph[edge].alignment_conflict_only = false;
        }

      mergeSpiltParms(stack_conflict_graph);
      chaitin_salloc(stack_conflict_graph);

      if(options.dump_graphs)
        dump_scon(stack_conflict_graph);
    }

  return(ic);
}

