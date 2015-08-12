/* Copyright 2014-2015 Samsung Electronics Co., Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "bytecode-data.h"
#include "jsp-mm.h"
#include "scopes-tree.h"

#define HASH_SIZE 128

static hash_table lit_id_to_uid = null_hash;
static vm_instr_counter_t global_oc;
static idx_t next_uid;

static void
assert_tree (scopes_tree t)
{
  JERRY_ASSERT (t != NULL);
}

static idx_t
get_uid (op_meta *op, uint8_t i)
{
  JERRY_ASSERT (i < 4);
  raw_instr *raw = (raw_instr *) &op->op;
  return raw->uids[i + 1];
}

static void
set_uid (op_meta *op, uint8_t i, idx_t uid)
{
  JERRY_ASSERT (i < 4);
  raw_instr *raw = (raw_instr *) &op->op;
  raw->uids[i + 1] = uid;
}

vm_instr_counter_t
scopes_tree_instrs_num (scopes_tree t)
{
  assert_tree (t);
  return t->instrs_count;
}

/**
 * Get number of variable declarations in the scope
 *
 * @return number of variable declarations
 */
vm_instr_counter_t
scopes_tree_var_decls_num (scopes_tree t) /**< scope */
{
  assert_tree (t);
  return t->var_decls_cout;
} /* scopes_tree_var_decls_num */

void
scopes_tree_add_op_meta (scopes_tree tree, op_meta op)
{
  assert_tree (tree);
  linked_list_set_element (tree->instrs, tree->instrs_count++, &op);
}

/**
 * Add variable declaration to a scope
 */
void
scopes_tree_add_var_decl (scopes_tree tree, /**< scope, to which variable declaration is added */
                          op_meta op) /**< variable declaration instruction */
{
  assert_tree (tree);
  linked_list_set_element (tree->var_decls, tree->var_decls_cout++, &op);
} /* scopes_tree_add_var_decl */

void
scopes_tree_set_op_meta (scopes_tree tree, vm_instr_counter_t oc, op_meta op)
{
  assert_tree (tree);
  JERRY_ASSERT (oc < tree->instrs_count);
  linked_list_set_element (tree->instrs, oc, &op);
}

void
scopes_tree_set_instrs_num (scopes_tree tree, vm_instr_counter_t oc)
{
  assert_tree (tree);
  JERRY_ASSERT (oc < tree->instrs_count);
  tree->instrs_count = oc;
}

op_meta
scopes_tree_op_meta (scopes_tree tree, vm_instr_counter_t oc)
{
  assert_tree (tree);
  JERRY_ASSERT (oc < tree->instrs_count);
  return *(op_meta *) linked_list_element (tree->instrs, oc);
}

/**
 * Get variable declaration for the specified scope
 *
 * @return instruction, declaring a variable
 */
op_meta
scopes_tree_var_decl (scopes_tree tree, /**< scope, from which variable declaration is retrieved */
                      vm_instr_counter_t oc) /**< number of variable declaration in the scope */
{
  assert_tree (tree);
  JERRY_ASSERT (oc < tree->var_decls_cout);
  return *(op_meta *) linked_list_element (tree->var_decls, oc);
} /* scopes_tree_var_decl */

vm_instr_counter_t
scopes_tree_count_instructions (scopes_tree t)
{
  assert_tree (t);
  vm_instr_counter_t res = (vm_instr_counter_t) (t->instrs_count + t->var_decls_cout);
  for (uint8_t i = 0; i < t->t.children_num; i++)
  {
    res = (vm_instr_counter_t) (
      res + scopes_tree_count_instructions (
        *(scopes_tree *) linked_list_element (t->t.children, i)));
  }
  return res;
}

static uint16_t
lit_id_hash (void * lit_id)
{
  return ((lit_cpointer_t *) lit_id)->packed_value % HASH_SIZE;
}

static void
start_new_block_if_necessary (void)
{
  if (global_oc % BLOCK_SIZE == 0)
  {
    next_uid = 0;
    if (lit_id_to_uid != null_hash)
    {
      hash_table_free (lit_id_to_uid);
      lit_id_to_uid = null_hash;
    }
    lit_id_to_uid = hash_table_init (sizeof (lit_cpointer_t), sizeof (idx_t), HASH_SIZE, lit_id_hash);
  }
}

static bool
is_possible_literal (uint16_t mask, uint8_t index)
{
  int res;
  switch (index)
  {
    case 0:
    {
      res = mask >> 8;
      break;
    }
    case 1:
    {
      res = (mask & 0xF0) >> 4;
      break;
    }
    default:
    {
      JERRY_ASSERT (index = 2);
      res = mask & 0x0F;
    }
  }
  JERRY_ASSERT (res == 0 || res == 1);
  return res == 1;
}

static void
change_uid (op_meta *om, lit_id_hash_table *lit_ids, uint16_t mask)
{
  for (uint8_t i = 0; i < 3; i++)
  {
    if (is_possible_literal (mask, i))
    {
      if (get_uid (om, i) == LITERAL_TO_REWRITE)
      {
        JERRY_ASSERT (om->lit_id[i].packed_value != MEM_CP_NULL);
        lit_cpointer_t lit_id = om->lit_id[i];
        idx_t *uid = (idx_t *) hash_table_lookup (lit_id_to_uid, &lit_id);
        if (uid == NULL)
        {
          hash_table_insert (lit_id_to_uid, &lit_id, &next_uid);
          lit_id_hash_table_insert (lit_ids, next_uid, global_oc, lit_id);
          uid = (idx_t *) hash_table_lookup (lit_id_to_uid, &lit_id);
          JERRY_ASSERT (uid != NULL);
          JERRY_ASSERT (*uid == next_uid);
          next_uid++;
        }
        set_uid (om, i, *uid);
      }
      else
      {
        JERRY_ASSERT (om->lit_id[i].packed_value == MEM_CP_NULL);
      }
    }
    else
    {
      JERRY_ASSERT (om->lit_id[i].packed_value == MEM_CP_NULL);
    }
  }
}

static void
insert_uids_to_lit_id_map (op_meta *om, uint16_t mask)
{
  for (uint8_t i = 0; i < 3; i++)
  {
    if (is_possible_literal (mask, i))
    {
      if (get_uid (om, i) == LITERAL_TO_REWRITE)
      {
        JERRY_ASSERT (om->lit_id[i].packed_value != MEM_CP_NULL);
        lit_cpointer_t lit_id = om->lit_id[i];
        idx_t *uid = (idx_t *) hash_table_lookup (lit_id_to_uid, &lit_id);
        if (uid == NULL)
        {
          hash_table_insert (lit_id_to_uid, &lit_id, &next_uid);
          uid = (idx_t *) hash_table_lookup (lit_id_to_uid, &lit_id);
          JERRY_ASSERT (uid != NULL);
          JERRY_ASSERT (*uid == next_uid);
          next_uid++;
        }
      }
      else
      {
        JERRY_ASSERT (om->lit_id[i].packed_value == MEM_CP_NULL);
      }
    }
    else
    {
      JERRY_ASSERT (om->lit_id[i].packed_value == MEM_CP_NULL);
    }
  }
}

/**
 * Get instruction from instruction list
 *
 * @return instruction at specified position
 */
static op_meta *
extract_op_meta (linked_list instr_list, /**< instruction list */
                 vm_instr_counter_t instr_pos) /**< position inside the list */
{
  return (op_meta *) linked_list_element (instr_list, instr_pos);
} /* extract_op_meta */

/**
 * Add instruction to an instruction list
 *
 * @return generated instruction
 */
static vm_instr_t
generate_instr (linked_list instr_list, /**< instruction list */
                vm_instr_counter_t instr_pos, /**< position where to generate an instruction */
                lit_id_hash_table *lit_ids) /**< hash table binding operand identifiers and literals */
{
  start_new_block_if_necessary ();
  op_meta *om_p = extract_op_meta (instr_list, instr_pos);
  /* Now we should change uids of instructions.
     Since different instructions has different literals/tmps in different places,
     we should change only them.
     For each case possible literal positions are shown as 0xYYY literal,
     where Y is set to '1' when there is a possible literal in this position,
     and '0' otherwise.  */
  switch (om_p->op.op_idx)
  {
    case VM_OP_PROP_GETTER:
    case VM_OP_PROP_SETTER:
    case VM_OP_DELETE_PROP:
    case VM_OP_B_SHIFT_LEFT:
    case VM_OP_B_SHIFT_RIGHT:
    case VM_OP_B_SHIFT_URIGHT:
    case VM_OP_B_AND:
    case VM_OP_B_OR:
    case VM_OP_B_XOR:
    case VM_OP_EQUAL_VALUE:
    case VM_OP_NOT_EQUAL_VALUE:
    case VM_OP_EQUAL_VALUE_TYPE:
    case VM_OP_NOT_EQUAL_VALUE_TYPE:
    case VM_OP_LESS_THAN:
    case VM_OP_GREATER_THAN:
    case VM_OP_LESS_OR_EQUAL_THAN:
    case VM_OP_GREATER_OR_EQUAL_THAN:
    case VM_OP_INSTANCEOF:
    case VM_OP_IN:
    case VM_OP_ADDITION:
    case VM_OP_SUBSTRACTION:
    case VM_OP_DIVISION:
    case VM_OP_MULTIPLICATION:
    case VM_OP_REMAINDER:
    {
      change_uid (om_p, lit_ids, 0x111);
      break;
    }
    case VM_OP_CALL_N:
    case VM_OP_CONSTRUCT_N:
    case VM_OP_FUNC_EXPR_N:
    case VM_OP_DELETE_VAR:
    case VM_OP_TYPEOF:
    case VM_OP_B_NOT:
    case VM_OP_LOGICAL_NOT:
    case VM_OP_POST_INCR:
    case VM_OP_POST_DECR:
    case VM_OP_PRE_INCR:
    case VM_OP_PRE_DECR:
    case VM_OP_UNARY_PLUS:
    case VM_OP_UNARY_MINUS:
    {
      change_uid (om_p, lit_ids, 0x110);
      break;
    }
    case VM_OP_ASSIGNMENT:
    {
      switch (om_p->op.data.assignment.type_value_right)
      {
        case OPCODE_ARG_TYPE_SIMPLE:
        case OPCODE_ARG_TYPE_SMALLINT:
        case OPCODE_ARG_TYPE_SMALLINT_NEGATE:
        {
          change_uid (om_p, lit_ids, 0x100);
          break;
        }
        case OPCODE_ARG_TYPE_NUMBER:
        case OPCODE_ARG_TYPE_NUMBER_NEGATE:
        case OPCODE_ARG_TYPE_REGEXP:
        case OPCODE_ARG_TYPE_STRING:
        case OPCODE_ARG_TYPE_VARIABLE:
        {
          change_uid (om_p, lit_ids, 0x101);
          break;
        }
      }
      break;
    }
    case VM_OP_FUNC_DECL_N:
    case VM_OP_ARRAY_DECL:
    case VM_OP_OBJ_DECL:
    case VM_OP_THIS_BINDING:
    case VM_OP_WITH:
    case VM_OP_FOR_IN:
    case VM_OP_THROW_VALUE:
    case VM_OP_IS_TRUE_JMP_UP:
    case VM_OP_IS_TRUE_JMP_DOWN:
    case VM_OP_IS_FALSE_JMP_UP:
    case VM_OP_IS_FALSE_JMP_DOWN:
    case VM_OP_VAR_DECL:
    case VM_OP_RETVAL:
    {
      change_uid (om_p, lit_ids, 0x100);
      break;
    }
    case VM_OP_RET:
    case VM_OP_TRY_BLOCK:
    case VM_OP_JMP_UP:
    case VM_OP_JMP_DOWN:
    case VM_OP_REG_VAR_DECL:
    {
      change_uid (om_p, lit_ids, 0x000);
      break;
    }
    case VM_OP_META:
    {
      switch (om_p->op.data.meta.type)
      {
        case OPCODE_META_TYPE_VARG_PROP_DATA:
        case OPCODE_META_TYPE_VARG_PROP_GETTER:
        case OPCODE_META_TYPE_VARG_PROP_SETTER:
        {
          change_uid (om_p, lit_ids, 0x011);
          break;
        }
        case OPCODE_META_TYPE_VARG:
        case OPCODE_META_TYPE_CATCH_EXCEPTION_IDENTIFIER:
        {
          change_uid (om_p, lit_ids, 0x010);
          break;
        }
        case OPCODE_META_TYPE_UNDEFINED:
        case OPCODE_META_TYPE_END_WITH:
        case OPCODE_META_TYPE_FUNCTION_END:
        case OPCODE_META_TYPE_CATCH:
        case OPCODE_META_TYPE_FINALLY:
        case OPCODE_META_TYPE_END_TRY_CATCH_FINALLY:
        case OPCODE_META_TYPE_CALL_SITE_INFO:
        case OPCODE_META_TYPE_SCOPE_CODE_FLAGS:
        {
          change_uid (om_p, lit_ids, 0x000);
          break;
        }
      }
      break;
    }
  }
  return om_p->op;
} /* generate_instr */

/**
 * Count number of literals in instruction which were not seen previously
 *
 * @return number of new literals
 */
static idx_t
count_new_literals_in_instr (op_meta *om_p) /**< instruction */
{
  start_new_block_if_necessary ();
  idx_t current_uid = next_uid;
  switch (om_p->op.op_idx)
  {
    case VM_OP_PROP_GETTER:
    case VM_OP_PROP_SETTER:
    case VM_OP_DELETE_PROP:
    case VM_OP_B_SHIFT_LEFT:
    case VM_OP_B_SHIFT_RIGHT:
    case VM_OP_B_SHIFT_URIGHT:
    case VM_OP_B_AND:
    case VM_OP_B_OR:
    case VM_OP_B_XOR:
    case VM_OP_EQUAL_VALUE:
    case VM_OP_NOT_EQUAL_VALUE:
    case VM_OP_EQUAL_VALUE_TYPE:
    case VM_OP_NOT_EQUAL_VALUE_TYPE:
    case VM_OP_LESS_THAN:
    case VM_OP_GREATER_THAN:
    case VM_OP_LESS_OR_EQUAL_THAN:
    case VM_OP_GREATER_OR_EQUAL_THAN:
    case VM_OP_INSTANCEOF:
    case VM_OP_IN:
    case VM_OP_ADDITION:
    case VM_OP_SUBSTRACTION:
    case VM_OP_DIVISION:
    case VM_OP_MULTIPLICATION:
    case VM_OP_REMAINDER:
    {
      insert_uids_to_lit_id_map (om_p, 0x111);
      break;
    }
    case VM_OP_CALL_N:
    case VM_OP_CONSTRUCT_N:
    case VM_OP_FUNC_EXPR_N:
    case VM_OP_DELETE_VAR:
    case VM_OP_TYPEOF:
    case VM_OP_B_NOT:
    case VM_OP_LOGICAL_NOT:
    case VM_OP_POST_INCR:
    case VM_OP_POST_DECR:
    case VM_OP_PRE_INCR:
    case VM_OP_PRE_DECR:
    case VM_OP_UNARY_PLUS:
    case VM_OP_UNARY_MINUS:
    {
      insert_uids_to_lit_id_map (om_p, 0x110);
      break;
    }
    case VM_OP_ASSIGNMENT:
    {
      switch (om_p->op.data.assignment.type_value_right)
      {
        case OPCODE_ARG_TYPE_SIMPLE:
        case OPCODE_ARG_TYPE_SMALLINT:
        case OPCODE_ARG_TYPE_SMALLINT_NEGATE:
        {
          insert_uids_to_lit_id_map (om_p, 0x100);
          break;
        }
        case OPCODE_ARG_TYPE_NUMBER:
        case OPCODE_ARG_TYPE_NUMBER_NEGATE:
        case OPCODE_ARG_TYPE_REGEXP:
        case OPCODE_ARG_TYPE_STRING:
        case OPCODE_ARG_TYPE_VARIABLE:
        {
          insert_uids_to_lit_id_map (om_p, 0x101);
          break;
        }
      }
      break;
    }
    case VM_OP_FUNC_DECL_N:
    case VM_OP_ARRAY_DECL:
    case VM_OP_OBJ_DECL:
    case VM_OP_THIS_BINDING:
    case VM_OP_WITH:
    case VM_OP_THROW_VALUE:
    case VM_OP_IS_TRUE_JMP_UP:
    case VM_OP_IS_TRUE_JMP_DOWN:
    case VM_OP_IS_FALSE_JMP_UP:
    case VM_OP_IS_FALSE_JMP_DOWN:
    case VM_OP_VAR_DECL:
    case VM_OP_RETVAL:
    {
      insert_uids_to_lit_id_map (om_p, 0x100);
      break;
    }
    case VM_OP_RET:
    case VM_OP_TRY_BLOCK:
    case VM_OP_JMP_UP:
    case VM_OP_JMP_DOWN:
    case VM_OP_REG_VAR_DECL:
    {
      insert_uids_to_lit_id_map (om_p, 0x000);
      break;
    }
    case VM_OP_META:
    {
      switch (om_p->op.data.meta.type)
      {
        case OPCODE_META_TYPE_VARG_PROP_DATA:
        case OPCODE_META_TYPE_VARG_PROP_GETTER:
        case OPCODE_META_TYPE_VARG_PROP_SETTER:
        {
          insert_uids_to_lit_id_map (om_p, 0x011);
          break;
        }
        case OPCODE_META_TYPE_VARG:
        case OPCODE_META_TYPE_CATCH_EXCEPTION_IDENTIFIER:
        {
          insert_uids_to_lit_id_map (om_p, 0x010);
          break;
        }
        case OPCODE_META_TYPE_UNDEFINED:
        case OPCODE_META_TYPE_END_WITH:
        case OPCODE_META_TYPE_FUNCTION_END:
        case OPCODE_META_TYPE_CATCH:
        case OPCODE_META_TYPE_FINALLY:
        case OPCODE_META_TYPE_END_TRY_CATCH_FINALLY:
        case OPCODE_META_TYPE_CALL_SITE_INFO:
        case OPCODE_META_TYPE_SCOPE_CODE_FLAGS:
        {
          insert_uids_to_lit_id_map (om_p, 0x000);
          break;
        }
      }
      break;
    }
  }
  return (idx_t) (next_uid - current_uid);
} /* count_new_literals_in_instr */

/**
 * Count slots needed for a scope's hash table
 *
 * Before filling literal indexes 'hash' table we shall initiate it with number of neccesary literal indexes.
 * Since bytecode is divided into blocks and id of the block is a part of hash key, we shall divide bytecode
 *  into blocks and count unique literal indexes used in each block.
 *
 * @return total number of literals in scope
 */
size_t
scopes_tree_count_literals_in_blocks (scopes_tree tree) /**< scope */
{
  assert_tree (tree);
  size_t result = 0;

  if (lit_id_to_uid != null_hash)
  {
    hash_table_free (lit_id_to_uid);
    lit_id_to_uid = null_hash;
  }
  next_uid = 0;
  global_oc = 0;

  assert_tree (tree);
  vm_instr_counter_t instr_pos;
  bool header = true;
  for (instr_pos = 0; instr_pos < tree->instrs_count; instr_pos++)
  {
    op_meta *om_p = extract_op_meta (tree->instrs, instr_pos);
    if (om_p->op.op_idx != VM_OP_META && !header)
    {
      break;
    }
    if (om_p->op.op_idx == VM_OP_REG_VAR_DECL)
    {
      header = false;
    }
    result += count_new_literals_in_instr (om_p);
  }

  for (vm_instr_counter_t var_decl_pos = 0; var_decl_pos < tree->var_decls_cout; var_decl_pos++)
  {
    op_meta *om_p = extract_op_meta (tree->var_decls, var_decl_pos);
    result += count_new_literals_in_instr (om_p);
  }

  for (uint8_t child_id = 0; child_id < tree->t.children_num; child_id++)
  {
    result += scopes_tree_count_literals_in_blocks (*(scopes_tree *) linked_list_element (tree->t.children, child_id));
  }

  for (; instr_pos < tree->instrs_count; instr_pos++)
  {
    op_meta *om_p = extract_op_meta (tree->instrs, instr_pos);
    result += count_new_literals_in_instr (om_p);
  }

  return result;
} /* scopes_tree_count_literals_in_blocks */

/*
 * This function performs functions hoisting.
 *
 *  Each scope consists of four parts:
 *  1) Header with 'use strict' marker and reg_var_decl opcode
 *  2) Variable declarations, dumped by the preparser
 *  3) Function declarations
 *  4) Computational code
 *
 *  Header and var_decls are dumped first,
 *  then we shall recursively dump function declaration,
 *  and finally, other instructions.
 *
 *  For each instructions block (size of block is defined in bytecode-data.h)
 *  literal indexes 'hash' table is filled.
 */
static void
merge_subscopes (scopes_tree tree, /**< scopes tree to merge */
                 vm_instr_t *data_p, /**< instruction array, where the scopes are merged to */
                 lit_id_hash_table *lit_ids_p) /**< literal indexes 'hash' table */
{
  assert_tree (tree);
  JERRY_ASSERT (data_p);
  vm_instr_counter_t instr_pos;
  bool header = true;
  for (instr_pos = 0; instr_pos < tree->instrs_count; instr_pos++)
  {
    op_meta *om_p = extract_op_meta (tree->instrs, instr_pos);
    if (om_p->op.op_idx != VM_OP_VAR_DECL
        && om_p->op.op_idx != VM_OP_META && !header)
    {
      break;
    }
    if (om_p->op.op_idx == VM_OP_REG_VAR_DECL)
    {
      header = false;
    }
    data_p[global_oc] = generate_instr (tree->instrs, instr_pos, lit_ids_p);
    global_oc++;
  }

  for (vm_instr_counter_t var_decl_pos = 0; var_decl_pos < tree->var_decls_cout; var_decl_pos++)
  {
    data_p[global_oc] = generate_instr (tree->var_decls, var_decl_pos, lit_ids_p);
    global_oc++;
  }

  for (uint8_t child_id = 0; child_id < tree->t.children_num; child_id++)
  {
    merge_subscopes (*(scopes_tree *) linked_list_element (tree->t.children, child_id),
                     data_p, lit_ids_p);
  }

  for (; instr_pos < tree->instrs_count; instr_pos++)
  {
    data_p[global_oc] = generate_instr (tree->instrs, instr_pos, lit_ids_p);
    global_oc++;
  }
} /* merge_subscopes */

/* Postparser.
   Init literal indexes 'hash' table.
   Reorder function declarations.
   Rewrite instructions' temporary uids with their keys in literal indexes 'hash' table. */
vm_instr_t *
scopes_tree_raw_data (scopes_tree tree, /**< scopes tree to convert to byte-code array */
                      uint8_t *buffer_p, /**< buffer for byte-code array and literal identifiers hash table */
                      size_t instructions_array_size, /**< size of space for byte-code array */
                      lit_id_hash_table *lit_ids) /**< literal identifiers hash table */
{
  JERRY_ASSERT (lit_ids);
  assert_tree (tree);
  if (lit_id_to_uid != null_hash)
  {
    hash_table_free (lit_id_to_uid);
    lit_id_to_uid = null_hash;
  }
  next_uid = 0;
  global_oc = 0;

  /* Dump bytecode and fill literal indexes 'hash' table. */
  JERRY_ASSERT (instructions_array_size >=
                sizeof (insts_data_header_t) + (size_t) (scopes_tree_count_instructions (tree)) * sizeof (vm_instr_t));

  insts_data_header_t *opcodes_data = (insts_data_header_t *) buffer_p;
  memset (opcodes_data, 0, instructions_array_size);

  vm_instr_t *instrs = (vm_instr_t *)(((uint8_t*) opcodes_data) + sizeof (insts_data_header_t));
  merge_subscopes (tree, instrs, lit_ids);
  if (lit_id_to_uid != null_hash)
  {
    hash_table_free (lit_id_to_uid);
    lit_id_to_uid = null_hash;
  }

  MEM_CP_SET_POINTER (opcodes_data->lit_id_hash_cp, lit_ids);

  return instrs;
} /* scopes_tree_raw_data */

/**
 * Set up a flag, indicating that scope should be executed in strict mode
 */
void
scopes_tree_set_strict_mode (scopes_tree tree, /**< scope */
                             bool strict_mode) /**< value of the strict mode flag */
{
  assert_tree (tree);
  tree->strict_mode = strict_mode;
} /* scopes_tree_set_strict_mode */

/**
 * Set up a flag, indicating that "arguments" is used inside a scope
 */
void
scopes_tree_set_arguments_used (scopes_tree tree) /**< scope */
{
  assert_tree (tree);
  tree->ref_arguments = true;
} /* merge_subscopes */

/**
 * Set up a flag, indicating that "eval" is used inside a scope
 */
void
scopes_tree_set_eval_used (scopes_tree tree) /**< scope */
{
  assert_tree (tree);
  tree->ref_eval = true;
} /* scopes_tree_set_eval_used */

bool
scopes_tree_strict_mode (scopes_tree tree)
{
  assert_tree (tree);
  return (bool) tree->strict_mode;
}

/**
 * Initialize a scope
 *
 * @return initialized scope
 */
scopes_tree
scopes_tree_init (scopes_tree parent) /**< parent scope */
{
  scopes_tree tree = (scopes_tree) jsp_mm_alloc (sizeof (scopes_tree_int));
  memset (tree, 0, sizeof (scopes_tree_int));
  tree->t.parent = (tree_header *) parent;
  tree->t.children = null_list;
  tree->t.children_num = 0;
  if (parent != NULL)
  {
    if (parent->t.children_num == 0)
    {
      parent->t.children = linked_list_init (sizeof (scopes_tree));
    }
    linked_list_set_element (parent->t.children, parent->t.children_num, &tree);
    void *added = linked_list_element (parent->t.children, parent->t.children_num);
    JERRY_ASSERT (*(scopes_tree *) added == tree);
    parent->t.children_num++;
  }
  tree->instrs_count = 0;
  tree->strict_mode = false;
  tree->ref_eval = false;
  tree->ref_arguments = false;
  tree->instrs = linked_list_init (sizeof (op_meta));
  tree->var_decls_cout = 0;
  tree->var_decls = linked_list_init (sizeof (op_meta));
  return tree;
} /* scopes_tree_init */

void
scopes_tree_free (scopes_tree tree)
{
  assert_tree (tree);
  if (tree->t.children_num != 0)
  {
    for (uint8_t i = 0; i < tree->t.children_num; ++i)
    {
      scopes_tree_free (*(scopes_tree *) linked_list_element (tree->t.children, i));
    }
    linked_list_free (tree->t.children);
  }
  linked_list_free (tree->instrs);
  linked_list_free (tree->var_decls);
  jsp_mm_free (tree);
}
