/* Convert a chain of ifs into a switch.
   Copyright (C) 2010 Free Software Foundation, Inc.
   Contributed by Tom de Vries <tom@codesourcery.com>

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3, or (at your option) any
later version.

GCC is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */


/* The following pass converts a chain of ifs into a switch.

   The if-chain has the following properties:
   - all bbs end in a GIMPLE_COND.
   - all but the first bb are empty, apart from the GIMPLE_COND.
   - the GIMPLE_CONDs compare the same variable against integer constants.
   - the true gotos all target the same bb.
   - the false gotos target the next in the if-chain.

   F.i., consider the following if-chain:
   ...
   <bb 4>:
   ...
   if (D.1993_3 == 32)
     goto <bb 3>;
   else
     goto <bb 5>;

   <bb 5>:
   if (D.1993_3 == 13)
     goto <bb 3>;
   else
     goto <bb 6>;

   <bb 6>:
   if (D.1993_3 == 10)
     goto <bb 3>;
   else
     goto <bb 7>;

   <bb 7>:
   if (D.1993_3 == 9)
     goto <bb 3>;
   else
     goto <bb 8>;
   ...

   The pass will report this if-chain like this:
   ...
   var: D.1993_3
   first: <bb 4>
   true: <bb 3>
   last: <bb 7>
   constants: 9 10 13 32
   ...

   and then convert the if-chain into a switch:
   ...
   <bb 4>:
   ...
   switch (D.1993_3) <default: <L8>,
                      case 9: <L7>,
                      case 10: <L7>,
                      case 13: <L7>,
                      case 32: <L7>>
   ...

   The conversion does not happen if the chain is too short.  The threshold is
   determined by the parameter PARAM_IF_TO_SWITCH_THRESHOLD.

   The pass will try to construct a chain for each bb, unless the bb it is
   already contained in a chain.  This ensures that all chains will be found,
   and that no chain will be constructed twice.  The pass constructs and
   converts the chains one-by-one, rather than first calculating all the chains
   and then doing the conversions.

   The pass could detect range-checks in analyze_bb as well, and handle them.
   Simple ones, like 'c <= 5', and more complex ones, like
   '(unsigned char) c + 247 <= 1', which is generated by the C front-end from
   code like '(c == 9 || c == 10)' or '(9 <= c && c <= 10)'.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"

#include "params.h"
#include "flags.h"
#include "tree.h"
#include "basic-block.h"
#include "tree-flow.h"
#include "tree-flow-inline.h"
#include "tree-ssa-operands.h"
#include "diagnostic.h"
#include "tree-pass.h"
#include "tree-dump.h"
#include "timevar.h"

/* Information we've collected about a single bb.  */

struct ifsc_info
{
  /* The variable of the bb's ending GIMPLE_COND, NULL_TREE if not present.  */
  tree var;
  /* The cond_code of the bb's ending GIMPLE_COND.  */
  enum tree_code cond_code;
  /* The constant of the bb's ending GIMPLE_COND.  */
  tree constant;
  /* Successor edge of the bb if its GIMPLE_COND is true.  */
  edge true_edge;
  /* Successor edge of the bb if its GIMPLE_COND is false.  */
  edge false_edge;
  /* Set if the bb has valid ifsc_info.  */
   bool valid;
  /* Set if the bb is part of a chain.  */
  bool chained;
};

/* Macros to access the fields of struct ifsc_info.  */

#define BB_IFSC_VAR(bb) (((struct ifsc_info *)bb->aux)->var)
#define BB_IFSC_COND_CODE(bb) (((struct ifsc_info *)bb->aux)->cond_code)
#define BB_IFSC_CONSTANT(bb) (((struct ifsc_info *)bb->aux)->constant)
#define BB_IFSC_TRUE_EDGE(bb) (((struct ifsc_info *)bb->aux)->true_edge)
#define BB_IFSC_FALSE_EDGE(bb) (((struct ifsc_info *)bb->aux)->false_edge)
#define BB_IFSC_VALID(bb) (((struct ifsc_info *)bb->aux)->valid)
#define BB_IFSC_CHAINED(bb) (((struct ifsc_info *)bb->aux)->chained)

/* Data-type describing an if-chain.  */

struct if_chain
{
  /* First bb in the chain.  */
  basic_block first;
  /* Last bb in the chain.  */
  basic_block last;
  /* Variable that GIMPLE_CONDs of all bbs in chain compare against.  */
  tree var;
  /* bb that all GIMPLE_CONDs jump to if comparison succeeds.  */
  basic_block true_dest;
  /* Constants that GIMPLE_CONDs of all bbs in chain compare var against.  */
  VEC (tree, heap) *constants;
  /* Same as previous, but sorted and with duplicates removed.  */
  VEC (tree, heap) *unique_constants;
};

/* Utility macro.  */

#define SWAP(T, X, Y) do { T tmp = (X); (X) = (Y); (Y) = tmp; } while (0)

/* Helper function for sort_constants.  */

static int
compare_constants (const void *p1, const void *p2)
{
  const_tree const c1 = *(const_tree const*)p1;
  const_tree const c2 = *(const_tree const*)p2;

  return tree_int_cst_compare (c1, c2);
}

/* Sort constants in constants and copy to unique_constants, while skipping
   duplicates.  */

static void
sort_constants (VEC (tree,heap) *constants, VEC (tree,heap) **unique_constants)
{
  size_t len = VEC_length (tree, constants);
  unsigned int ix;
  tree prev = NULL_TREE, constant;

  /* Sort constants.  */
  qsort (VEC_address (tree, constants), len, sizeof (tree),
         compare_constants);

  /* Copy to unique_constants, while skipping duplicates.  */
  for (ix = 0; VEC_iterate (tree, constants, ix, constant); ix++)
    {
      if (prev != NULL_TREE && tree_int_cst_compare (prev, constant) == 0)
        continue;
      prev = constant;

      VEC_safe_push (tree, heap, *unique_constants, constant);
    }
}

/* Get true_edge and false_edge of a bb ending in a conditional jump.  */

static void
get_edges (basic_block bb, edge *true_edge, edge *false_edge)
{
  edge e0, e1;
  int e0_true;
  int n = EDGE_COUNT (bb->succs);
  gcc_assert (n == 2);

  e0 = EDGE_SUCC (bb, 0);
  e1 = EDGE_SUCC (bb, 1);

  e0_true = e0->flags & EDGE_TRUE_VALUE;

  *true_edge = e0_true ? e0 : e1;
  *false_edge = e0_true ? e1 : e0;

  gcc_assert ((*true_edge)->flags & EDGE_TRUE_VALUE);
  gcc_assert ((*false_edge)->flags & EDGE_FALSE_VALUE);

  gcc_assert (((*true_edge)->flags & EDGE_FALLTHRU) == 0);
  gcc_assert (((*false_edge)->flags & EDGE_FALLTHRU) == 0);
}

/* Analyze bb and store results in ifsc_info struct.  */

static void
analyze_bb (basic_block bb)
{
  gimple stmt = last_stmt (bb);
  tree lhs, rhs, var, constant;
  edge true_edge, false_edge;
  enum tree_code cond_code;

  /* Don't redo analysis.  */
  if (BB_IFSC_VALID (bb))
    return;
  BB_IFSC_VALID (bb) = true;


  /* bb needs to end in GIMPLE_COND.  */
  if (!stmt || gimple_code (stmt) != GIMPLE_COND)
    return;

  /* bb needs to end in EQ_EXPR or NE_EXPR.  */
  cond_code = gimple_cond_code (stmt);
  if (cond_code != EQ_EXPR && cond_code != NE_EXPR)
    return;

  lhs = gimple_cond_lhs (stmt);
  rhs = gimple_cond_rhs (stmt);

  /* GIMPLE_COND needs to compare variable to constant.  */
  if ((TREE_CONSTANT (lhs) == 0)
      == (TREE_CONSTANT (rhs) == 0))
    return;

  var = TREE_CONSTANT (lhs) ? rhs : lhs;
  constant = TREE_CONSTANT (lhs)? lhs : rhs;

  /* Switches cannot handle non-integral types.  */
  if (!INTEGRAL_TYPE_P(TREE_TYPE (var)))
    return;

  get_edges (bb, &true_edge, &false_edge);

  if (cond_code == NE_EXPR)
    SWAP (edge, true_edge, false_edge);

  /* TODO: loosen this constraint.  In principle it's ok if true_edge->dest has
     phis, as long as for each phi all the edges coming from the chain have the
     same value.  */
  if (!gimple_seq_empty_p (phi_nodes (true_edge->dest)))
    return;

  /* Store analysis in ifsc_info struct.  */
  BB_IFSC_VAR (bb) = var;
  BB_IFSC_COND_CODE (bb) = cond_code;
  BB_IFSC_CONSTANT (bb) = constant;
  BB_IFSC_TRUE_EDGE (bb) = true_edge;
  BB_IFSC_FALSE_EDGE (bb) = false_edge;
}

/* Grow if-chain forward.  */

static void
grow_if_chain_forward (struct if_chain *chain)
{
  basic_block next_bb;

  while (1)
    {
      next_bb = BB_IFSC_FALSE_EDGE (chain->last)->dest;

      /* next_bb is already part of another chain.  */
      if (BB_IFSC_CHAINED (next_bb))
        break;

      /* next_bb needs to be dominated by the last bb.  */
      if (!single_pred_p (next_bb))
        break;

      analyze_bb (next_bb);

      /* Does next_bb fit in chain?  */
      if (BB_IFSC_VAR (next_bb) != chain->var
          || BB_IFSC_TRUE_EDGE (next_bb)->dest != chain->true_dest)
        break;

      /* We can only add empty bbs at the end of the chain.  */
      if (first_stmt (next_bb) != last_stmt (next_bb))
        break;

      /* Add next_bb at end of chain.  */
      VEC_safe_push (tree, heap, chain->constants, BB_IFSC_CONSTANT (next_bb));
      BB_IFSC_CHAINED (next_bb) = true;
      chain->last = next_bb;
    }
}

/* Grow if-chain backward.  */

static void
grow_if_chain_backward (struct if_chain *chain)
{
  basic_block prev_bb;

  while (1)
    {
      /* First bb is not empty, cannot grow backwards.  */
      if (first_stmt (chain->first) != last_stmt (chain->first))
        break;

      /* First bb has no single predecessor, cannot grow backwards.  */
      if (!single_pred_p (chain->first))
        break;

      prev_bb = single_pred (chain->first);

      /* prev_bb is already part of another chain.  */
      if (BB_IFSC_CHAINED (prev_bb))
        break;

      analyze_bb (prev_bb);

      /* Does prev_bb fit in chain?  */
      if (BB_IFSC_VAR (prev_bb) != chain->var
          || BB_IFSC_TRUE_EDGE (prev_bb)->dest != chain->true_dest)
        break;

      /* Add prev_bb at beginning of chain.  */
      VEC_safe_push (tree, heap, chain->constants, BB_IFSC_CONSTANT (prev_bb));
      BB_IFSC_CHAINED (prev_bb) = true;
      chain->first = prev_bb;
    }
}

/* Grow if-chain containing bb.  */

static void
grow_if_chain (basic_block bb, struct if_chain *chain)
{
  /* Initialize chain to empty.  */
  VEC_truncate (tree, chain->constants, 0);
  VEC_truncate (tree, chain->unique_constants, 0);

  /* bb is already part of another chain.  */
  if (BB_IFSC_CHAINED (bb))
    return;

  analyze_bb (bb);

  /* bb is not fit to be part of a chain.  */
  if (BB_IFSC_VAR (bb) == NULL_TREE)
    return;

  /* Set bb as initial part of the chain.  */
  VEC_safe_push (tree, heap, chain->constants, BB_IFSC_CONSTANT (bb));
  chain->first = chain->last = bb;
  chain->var = BB_IFSC_VAR (bb);
  chain->true_dest = BB_IFSC_TRUE_EDGE (bb)->dest;

  /* bb is part of a chain now.  */
  BB_IFSC_CHAINED (bb) = true;

  /* Grow chain to its maximum size.  */
  grow_if_chain_forward (chain);
  grow_if_chain_backward (chain);

  /* Sort constants and skip duplicates.  */
  sort_constants (chain->constants, &chain->unique_constants);
}

static void
dump_tree_vector (VEC (tree, heap) *vec)
{
  unsigned int ix;
  tree constant;

  for (ix = 0; VEC_iterate (tree, vec, ix, constant); ix++)
    {
      if (ix != 0)
        fprintf (dump_file, " ");
      print_generic_expr (dump_file, constant, 0);
    }
  fprintf (dump_file, "\n");
}

/* Dump if-chain to dump_file.  */

static void
dump_if_chain (struct if_chain *chain)
{
  if (!dump_file)
    return;

  fprintf (dump_file, "var: ");
  print_generic_expr (dump_file, chain->var, 0);
  fprintf (dump_file, "\n");
  fprintf (dump_file, "first: <bb %d>\n", chain->first->index);
  fprintf (dump_file, "true: <bb %d>\n", chain->true_dest->index);
  fprintf (dump_file, "last: <bb %d>\n",chain->last->index);

  fprintf (dump_file, "constants: ");
  dump_tree_vector (chain->constants);

  if (VEC_length (tree, chain->unique_constants)
      != VEC_length (tree, chain->constants))
    {
      fprintf (dump_file, "unique_constants: ");
      dump_tree_vector (chain->unique_constants);
    }
}

/* Remove redundant bbs and edges.  */

static void
remove_redundant_bbs_and_edges (struct if_chain *chain, int *false_prob)
{
  basic_block bb, next;
  edge true_edge, false_edge;

  for (bb = chain->first;; bb = next)
    {
      true_edge = BB_IFSC_TRUE_EDGE (bb);
      false_edge = BB_IFSC_FALSE_EDGE (bb);

      /* Determine next, before we delete false_edge.  */
      next = false_edge->dest;

      /* Accumulate probability.  */
      *false_prob = (*false_prob * false_edge->probability) / REG_BR_PROB_BASE;

      /* Don't remove the new true_edge.  */
      if (bb != chain->first)
        remove_edge (true_edge);

      /* Don't remove the new false_edge.  */
      if (bb != chain->last)
        remove_edge (false_edge);

      /* Don't remove the first bb.  */
      if (bb != chain->first)
        delete_basic_block (bb);

      /* Stop after last.  */
      if (bb == chain->last)
        break;
    }
}

/* Update control flow graph.  */

static void
update_cfg (struct if_chain *chain)
{
  edge true_edge, false_edge;
  int false_prob;
  int flags_mask = ~(EDGE_FALLTHRU|EDGE_TRUE_VALUE|EDGE_FALSE_VALUE);

  /* We keep these 2 edges, and remove the rest.  We need this specific
     false_edge, because a phi in chain->last->dest might reference (the index
     of) this edge.  For true_edge, we could pick any of them.  */
  true_edge = BB_IFSC_TRUE_EDGE (chain->first);
  false_edge = BB_IFSC_FALSE_EDGE (chain->last);

  /* Update true edge.  */
  true_edge->flags &= flags_mask;

  /* Update false edge.  */
  redirect_edge_pred (false_edge, chain->first);
  false_edge->flags &= flags_mask;

  false_prob = REG_BR_PROB_BASE;
  remove_redundant_bbs_and_edges (chain, &false_prob);

  /* Repair probabilities.  */
  true_edge->probability = REG_BR_PROB_BASE - false_prob;
  false_edge->probability = false_prob;

  /* Force recalculation of dominance info.  */
  free_dominance_info (CDI_DOMINATORS);
  free_dominance_info (CDI_POST_DOMINATORS);
}

/* Create switch statement.  Borrows from gimplify_switch_expr.  */

static void
convert_if_chain_to_switch (struct if_chain *chain)
{
  tree label_decl_true, label_decl_false;
  gimple label_true, label_false, gimple_switch;
  gimple_stmt_iterator gsi;
  tree default_case, other_case, constant;
  unsigned int ix;
  VEC (tree, heap) *labels;

  labels = VEC_alloc (tree, heap, 8);

  /* Create and insert true jump label.  */
  label_decl_true = create_artificial_label (UNKNOWN_LOCATION);
  label_true = gimple_build_label (label_decl_true);
  gsi = gsi_start_bb (chain->true_dest);
  gsi_insert_before (&gsi, label_true, GSI_SAME_STMT);

  /* Create and insert false jump label.  */
  label_decl_false = create_artificial_label (UNKNOWN_LOCATION);
  label_false = gimple_build_label (label_decl_false);
  gsi = gsi_start_bb (BB_IFSC_FALSE_EDGE (chain->last)->dest);
  gsi_insert_before (&gsi, label_false, GSI_SAME_STMT);

  /* Create default case label.  */
  default_case = build3 (CASE_LABEL_EXPR, void_type_node,
                         NULL_TREE, NULL_TREE,
                         label_decl_false);

  /* Create case labels.  */
  for (ix = 0; VEC_iterate (tree, chain->unique_constants, ix, constant); ix++)
    {
      /* TODO: use ranges, as in gimplify_switch_expr.  */
      other_case = build3 (CASE_LABEL_EXPR, void_type_node,
                           constant, NULL_TREE,
                           label_decl_true);
      VEC_safe_push (tree, heap, labels, other_case);
    }

  /* Create and insert switch.  */
  gimple_switch = gimple_build_switch_vec (chain->var, default_case, labels);
  gsi = gsi_for_stmt (last_stmt (chain->first));
  gsi_insert_before (&gsi, gimple_switch, GSI_SAME_STMT);

  /* Remove now obsolete if.  */
  gsi_remove (&gsi, true);

  VEC_free (tree, heap, labels);
}

/* Allocation and initialization.  */

static void
init_pass (struct if_chain *chain)
{
  alloc_aux_for_blocks (sizeof (struct ifsc_info));

  chain->constants = VEC_alloc (tree, heap, 8);
  chain->unique_constants = VEC_alloc (tree, heap, 8);
}

/* Deallocation.  */

static void
finish_pass (struct if_chain *chain)
{
  free_aux_for_blocks ();

  VEC_free (tree, heap, chain->constants);
  VEC_free (tree, heap, chain->unique_constants);
}

/* Find if-chains and convert them to switches.  */

static unsigned int
do_if_to_switch (void)
{
  basic_block bb;
  struct if_chain chain;
  unsigned int convert_threshold = PARAM_VALUE (PARAM_IF_TO_SWITCH_THRESHOLD);

  init_pass (&chain);

  for (bb = cfun->cfg->x_entry_block_ptr->next_bb;
       bb != cfun->cfg->x_exit_block_ptr;)
    {
      grow_if_chain (bb, &chain);

      do
        bb = bb->next_bb;
      while (BB_IFSC_CHAINED (bb));

      /* Determine if the chain is long enough.  */
      if (VEC_length (tree, chain.unique_constants) < convert_threshold)
        continue;

      dump_if_chain (&chain);

      convert_if_chain_to_switch (&chain);

      update_cfg (&chain);
    }

  finish_pass (&chain);

  return 0;
}

/* The pass gate.  */

static bool
if_to_switch_gate (void)
{
  return flag_tree_if_to_switch_conversion;
}

/* The pass definition.  */

struct gimple_opt_pass pass_if_to_switch =
{
 {
  GIMPLE_PASS,
  "iftoswitch",                         /* name */
  if_to_switch_gate,                    /* gate */
  do_if_to_switch,                      /* execute */
  NULL,                                 /* sub */
  NULL,                                 /* next */
  0,                                    /* static_pass_number */
  TV_TREE_SWITCH_CONVERSION,            /* tv_id */
  PROP_cfg | PROP_ssa,                  /* properties_required */
  0,                                    /* properties_provided */
  0,                                    /* properties_destroyed */
  0,                                    /* todo_flags_start */
  TODO_update_ssa | TODO_dump_func
  | TODO_ggc_collect | TODO_verify_ssa  /* todo_flags_finish */
 }
};
