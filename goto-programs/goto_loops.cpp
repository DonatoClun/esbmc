/*
 * loopst.cpp
 *
 *  Created on: Jun 30, 2015
 *      Author: mramalho
 */

#include "goto_loops.h"

#include <util/expr_util.h>

void goto_loopst::find_function_loops()
{
  std::map<unsigned int, goto_programt::instructionst::iterator> targets;

  for(goto_programt::instructionst::iterator
      it=goto_function.body.instructions.begin();
      it!=goto_function.body.instructions.end();
      it++)
  {
    // Record location number and targets
    if (it->is_target())
      targets[it->location_number] = it;

    // We found a loop, let's record its instructions
    if (it->is_backwards_goto())
    {
      assert(it->targets.size() == 1);

      if((*it->targets.begin())->location_number == it->location_number)
        continue;

      create_function_loop(
        targets[(*it->targets.begin())->location_number], it);
    }
  }
}

void goto_loopst::create_function_loop(
  goto_programt::instructionst::iterator loop_head,
  goto_programt::instructionst::iterator loop_exit)
{
  goto_programt::instructionst::iterator it=loop_head;

  std::pair<goto_programt::targett, loopst>
    p(loop_head, loopst(context, goto_programt()));

  function_loopst::iterator it1 =
    function_loops.insert(p).first;

  // Set original iterators
  it1->second.set_original_loop_head(loop_head);
  it1->second.set_original_loop_exit(loop_exit);

  // Copy the loop body
  while (it != loop_exit)
  {
    goto_programt::targett new_instruction=
      it1->second.get_goto_program().add_instruction();

    // This should be done only when we're running k-induction
    // Maybe a flag on the class?
    get_modified_variables(it, it1, function_name);

    *new_instruction=*it;
    ++it;
  }

  // Finally, add the loop exit
  goto_programt::targett new_instruction=
    it1->second.get_goto_program().add_instruction();
  *new_instruction=*loop_exit;
}

void goto_loopst::get_modified_variables(
  goto_programt::instructionst::iterator instruction,
  function_loopst::iterator loop,
  const irep_idt &_function_name)
{
  if(instruction->is_assign())
  {
    const code_assign2t &assign = to_code_assign2t(instruction->code);
    add_loop_var(loop->second, migrate_expr_back(assign.target));
  }
  else if(instruction->is_function_call())
  {
    // Functions are a bit tricky
    code_function_call2t &function_call =
      to_code_function_call2t(instruction->code);

    // Don't do function pointers
    if(is_dereference2t(function_call.function))
      return;

    // First, add its return
    add_loop_var(loop->second, migrate_expr_back(function_call.ret));

    // The run over the function body and get the modified variables there
    irep_idt &identifier = to_symbol2t(function_call.function).thename;

    // This means recursion, do nothing
    if(identifier == _function_name)
      return;

    // find code in function map
    goto_functionst::function_mapt::iterator it =
      goto_functions.function_map.find(identifier);

    if (it == goto_functions.function_map.end()) {
      std::cerr << "failed to find `" + id2string(identifier) +
                   "' in function_map";
      abort();
    }

    // Avoid iterating over functions that don't have a body
    if(!it->second.body_available)
      return;

    for(goto_programt::instructionst::iterator head=
        it->second.body.instructions.begin();
        head != it->second.body.instructions.end();
        ++head)
    {
      get_modified_variables(head, loop, identifier);
    }
  }
}

void goto_loopst::output(std::ostream &out)
{
  for(function_loopst::iterator
      h_it=function_loops.begin();
      h_it!=function_loops.end();
      ++h_it)
  {
    h_it->second.output(out);
  }
}

void goto_loopst::add_loop_var(loopst &loop, const exprt& expr)
{
  if (expr.is_symbol() && expr.type().id() != "code")
  {
    if(check_var_name(expr))
      loop.add_var_to_loop(expr);
  }
  else
  {
    forall_operands(it, expr)
      add_loop_var(loop, *it);
  }
}
