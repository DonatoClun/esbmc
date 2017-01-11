/*******************************************************************\

Module: Program Transformation

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#include <iomanip>

#include <langapi/language_util.h>

#include "goto_program.h"

void goto_programt::instructiont::dump() const
{
  output_instruction(*migrate_namespace_lookup, "", std::cout);
}

void goto_programt::instructiont::output_instruction(
  const class namespacet& ns,
  const irep_idt& identifier,
  std::ostream& out,
  bool show_location,
  bool show_variables) const
{
  if (show_location)
  {
    out << "        // " << location_number << " ";

    if(!location.is_nil())
      out << location.as_string();
    else
      out << "no location";

    out << "\n";
  }

  if(show_variables && !local_variables.empty())
  {
    out << "        // Variables:";
    for(local_variablest::const_iterator
        l_it = local_variables.begin();
        l_it != local_variables.end();
        l_it++)
      out << " " << *l_it;

    out << std::endl;
  }

  if(!labels.empty())
  {
    out << "        // Labels:";
    for(instructiont::labelst::const_iterator
        l_it = labels.begin();
        l_it != labels.end();
        l_it++)
    {
      out << " " << *l_it;
    }

    out << std::endl;
  }

  if(is_target())
    out << std::setw(6) << target_number << ": ";
  else
    out << "        ";

  switch(type)
  {
  case NO_INSTRUCTION_TYPE:
    out << "NO INSTRUCTION TYPE SET" << std::endl;
    break;

  case GOTO:
    if (!is_constant_bool2t(guard) ||
        !to_constant_bool2t(guard).value)
    {
      out << "IF "
          << from_expr(ns, identifier, guard)
          << " THEN ";
    }

    out << "GOTO ";

    for(instructiont::targetst::const_iterator
        gt_it = targets.begin();
        gt_it != targets.end();
        gt_it++)
    {
      if(gt_it != targets.begin()) out << ", ";
      out << (*gt_it)->target_number;
    }

    out << std::endl;
    break;

  case FUNCTION_CALL:
    out << "FUNCTION_CALL:  " << from_expr(ns, "", migrate_expr_back(code)) << std::endl;
    break;

  case RETURN:
    {
    std::string arg = "";
    const code_return2t &ref = to_code_return2t(code);
    if (!is_nil_expr(ref.operand))
      arg = from_expr(ns, "", ref.operand);
    out << "RETURN: " << arg << std::endl;
    }
    break;

  case DECL:
  case DEAD:
  case OTHER:
  case ASSIGN:
      out << from_expr(ns, identifier, code) << std::endl;
    break;

  case ASSUME:
  case ASSERT:
    if(is_assume())
      out << "ASSUME ";
    else
      out << "ASSERT ";

    {
      out << from_expr(ns, identifier, guard);

      const irep_idt &comment = location.comment();
      if(comment!="") out << " // " << comment;
    }

    out << std::endl;
    break;

  case SKIP:
    out << "SKIP" << std::endl;
    break;

  case END_FUNCTION:
    out << "END_FUNCTION" << std::endl;
    break;

  case LOCATION:
    out << "LOCATION" << std::endl;
    break;

  case THROW:
    out << "THROW";

    {
      const code_cpp_throw2t &throw_ref = to_code_cpp_throw2t(code);
      forall_names(it, throw_ref.exception_list) {
      	if(it != throw_ref.exception_list.begin()) out << ",";
        out << " " << *it;
      }

      if (!is_nil_expr(throw_ref.operand))
        out << ": " << from_expr(ns, identifier, throw_ref.operand);
    }

    out << std::endl;
    break;

  case CATCH:
    out << "CATCH ";

    {
      unsigned i=0;
      const code_cpp_catch2t &catch_ref = to_code_cpp_catch2t(code);
      assert(targets.size() == catch_ref.exception_list.size());

      for(instructiont::targetst::const_iterator
          gt_it = targets.begin();
          gt_it != targets.end();
          gt_it++,
          i++)
      {
        if(gt_it != targets.begin()) out << ", ";
        out << catch_ref.exception_list[i] << "->"
            << (*gt_it)->target_number;
      }
    }

    out << std::endl;
    break;

  case ATOMIC_BEGIN:
    out << "ATOMIC_BEGIN" << std::endl;
    break;

  case ATOMIC_END:
    out << "ATOMIC_END" << std::endl;
    break;

  case THROW_DECL:
    out << "THROW_DECL (";

    {
      const code_cpp_throw_decl2t &ref = to_code_cpp_throw_decl2t(code);

      for(unsigned int i=0; i < ref.exception_list.size(); ++i)
      {
        if(i) out << ", ";
        out << ref.exception_list[i];
      }
      out << ")";
    }

    out << std::endl;
    break;

  case THROW_DECL_END:
    out << "THROW_DECL_END (";

    if (!is_nil_expr(code))
    {
      const code_cpp_throw_decl_end2t &decl_end =
        to_code_cpp_throw_decl_end2t(code);

      forall_names(it, decl_end.exception_list)
      {
        if (it != decl_end.exception_list.begin())
          out << ", ";
        out << *it;
      }
    }

    out << ")";

    out << std::endl;
    break;

  default:
    throw "unknown statement";
  }
}

bool operator<(const goto_programt::const_targett i1,
               const goto_programt::const_targett i2)
{
  const goto_programt::instructiont &_i1=*i1;
  const goto_programt::instructiont &_i2=*i2;
  return &_i1<&_i2;
}

void goto_programt::compute_loop_numbers(unsigned int &num)
{
  for(instructionst::iterator
      it=instructions.begin();
      it!=instructions.end();
      it++)
    if(it->is_backwards_goto())
    {
      (*it->targets.begin())->loop_number = num;
      it->loop_number = num++;
    }
}

void goto_programt::get_successors(
  targett target,
  targetst &successors)
{
  successors.clear();
  if(target==instructions.end()) return;

  targett next=target;
  next++;

  const instructiont &i=*target;

  if(i.is_goto())
  {
    for(targetst::const_iterator
        t_it=i.targets.begin();
        t_it!=i.targets.end();
        t_it++)
      successors.push_back(*t_it);

    if(!is_true(i.guard))
      successors.push_back(next);
  }
  else if(i.is_throw())
  {
    // the successors are non-obvious
  }
  else if(i.is_return())
  {
    // the successor is the end_function at the end of the function
    successors.push_back(--instructions.end());
  }
  else if(i.is_assume())
  {
    if(!is_false(i.guard))
      successors.push_back(next);
  }
  else
    successors.push_back(next);
}

void goto_programt::get_successors(
  const_targett target,
  const_targetst &successors) const
{
  successors.clear();
  if(target==instructions.end()) return;

  const_targett next=target;
  next++;

  const instructiont &i=*target;

  if(i.is_goto())
  {
    for(targetst::const_iterator
        t_it=i.targets.begin();
        t_it!=i.targets.end();
        t_it++)
      successors.push_back(*t_it);

    if(!is_true(i.guard))
      successors.push_back(next);
  }
  else if(i.is_return())
  {
    // the successor is the end_function at the end
    successors.push_back(--instructions.end());
  }
  else if(i.is_assume())
  {
    if(!is_false(i.guard))
      successors.push_back(next);
  }
  else
    successors.push_back(next);
}

void goto_programt::update()
{
  compute_target_numbers();
  compute_location_numbers();
}

std::ostream& goto_programt::output(
  const namespacet &ns,
  const irep_idt &identifier,
  std::ostream& out) const
{
  // output program

  for(instructionst::const_iterator
      it=instructions.begin();
      it!=instructions.end();
      it++)
    it->output_instruction(ns, identifier, out);

  return out;
}

void goto_programt::compute_target_numbers()
{
  // reset marking

  for(instructionst::iterator
      it=instructions.begin();
      it!=instructions.end();
      it++)
    it->target_number=-1;

  // mark the goto targets

  for(instructionst::const_iterator
      it=instructions.begin();
      it!=instructions.end();
      it++)
  {
    for(instructiont::targetst::const_iterator
        t_it=it->targets.begin();
        t_it!=it->targets.end();
        t_it++)
    {
      targett t=*t_it;
      if(t!=instructions.end())
        t->target_number=0;
    }
  }

  // number the targets properly
  unsigned cnt=0;

  for(instructionst::iterator
      it=instructions.begin();
      it!=instructions.end();
      it++)
  {
    if(it->is_target())
    {
      it->target_number=++cnt;
      assert(it->target_number!=0);
    }
  }

  // check the targets!
  // (this is a consistency check only)

  for(instructionst::const_iterator
      it=instructions.begin();
      it!=instructions.end();
      it++)
  {
    for(instructiont::targetst::const_iterator
        t_it=it->targets.begin();
        t_it!=it->targets.end();
        t_it++)
    {
      targett t=*t_it;
      if(t!=instructions.end())
      {
        assert(t->target_number!=0);
        assert(t->target_number!=unsigned(-1));
      }
    }
  }

}

void goto_programt::copy_from(const goto_programt &src)
{
  // Definitions for mapping between the two programs
  typedef std::map<const_targett, targett> targets_mappingt;
  targets_mappingt targets_mapping;

  clear();

  // Loop over program - 1st time collects targets and copy

  for(instructionst::const_iterator
      it=src.instructions.begin();
      it!=src.instructions.end();
      it++)
  {
    targett new_instruction=add_instruction();
    targets_mapping[it]=new_instruction;
    *new_instruction=*it;
  }

  // Loop over program - 2nd time updates targets

  for(instructionst::iterator
      it=instructions.begin();
      it!=instructions.end();
      it++)
  {
    for(instructiont::targetst::iterator
        t_it=it->targets.begin();
        t_it!=it->targets.end();
        t_it++)
    {
      targets_mappingt::iterator
        m_target_it=targets_mapping.find(*t_it);

      if(m_target_it==targets_mapping.end())
        throw "copy_from: target not found";

      *t_it=m_target_it->second;
    }
  }

  compute_target_numbers();
}

std::ostream &operator<<(std::ostream &out, goto_program_instruction_typet t)
{
  switch(t)
  {
  case NO_INSTRUCTION_TYPE: out << "NO_INSTRUCTION_TYPE"; break;
  case GOTO: out << "GOTO"; break;
  case ASSUME: out << "ASSUME"; break;
  case ASSERT: out << "ASSERT"; break;
  case OTHER: out << "OTHER"; break;
  case SKIP: out << "SKIP"; break;
  case LOCATION: out << "LOCATION"; break;
  case END_FUNCTION: out << "END_FUNCTION"; break;
  case ATOMIC_BEGIN: out << "ATOMIC_BEGIN"; break;
  case ATOMIC_END: out << "ATOMIC_END"; break;
  case RETURN: out << "RETURN"; break;
  case ASSIGN: out << "ASSIGN"; break;
  case FUNCTION_CALL: out << "FUNCTION_CALL"; break;
  case THROW: out << "THROW"; break;
  case CATCH: out << "CATCH"; break;
  case THROW_DECL: out << "THROW_DECL"; break;
  default:
    out << "? (number: " << t << ")";
  }

  return out;
}

void goto_programt::dump() const
{
  output(*migrate_namespace_lookup, "", std::cout);
}
