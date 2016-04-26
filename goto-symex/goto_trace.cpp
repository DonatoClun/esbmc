/*******************************************************************\

   Module: Traces of GOTO Programs

   Author: Daniel Kroening

   Date: July 2005

\*******************************************************************/

#include <assert.h>
#include <string.h>

#include <ansi-c/printf_formatter.h>
#include <langapi/language_util.h>
#include <arith_tools.h>
#include <boost/version.hpp>

#include "goto_trace.h"
#include <std_types.h>

#include <boost/algorithm/string.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

#include "witnesses.h"

void
goto_tracet::output(
  const class namespacet &ns, std::ostream &out) const
{
  for (stepst::const_iterator it = steps.begin();
       it != steps.end();
       it++)
    it->output(ns, out);
}

void
goto_trace_stept::output(
  const namespacet &ns, std::ostream &out) const
{
  out << "*** ";

  switch (type)
  {
    case goto_trace_stept::ASSERT:
      out << "ASSERT";
      break;

    case goto_trace_stept::ASSUME:
      out << "ASSUME";
      break;

    case goto_trace_stept::ASSIGNMENT:
      out << "ASSIGNMENT";
      break;

    default:
      assert(false);
  }

  if (type == ASSERT || type == ASSUME)
    out << " (" << guard << ")";

  out << std::endl;

  if (!pc->location.is_nil())
    out << pc->location << std::endl;

  if (pc->is_goto())
    out << "GOTO   ";
  else if (pc->is_assume())
    out << "ASSUME ";
  else if (pc->is_assert())
    out << "ASSERT ";
  else if (pc->is_other())
    out << "OTHER  ";
  else if (pc->is_assign())
    out << "ASSIGN ";
  else if (pc->is_function_call())
    out << "CALL   ";
  else
    out << "(?)    ";

  out << std::endl;

  if (pc->is_other() || pc->is_assign())
  {
    irep_idt identifier;

    if (!is_nil_expr(original_lhs))
      identifier = to_symbol2t(original_lhs).get_symbol_name();
    else
      identifier = to_symbol2t(lhs).get_symbol_name();

    out << "  " << identifier << " = " << from_expr(ns, identifier, value)
        << std::endl;
  }
  else if (pc->is_assert())
  {
    if (!guard)
    {
      out << "Violated property:" << std::endl;
      if (pc->location.is_nil())
        out << "  " << pc->location << std::endl;

      if (comment != "")
        out << "  " << comment << std::endl;
      out << "  " << from_expr(ns, "", pc->guard) << std::endl;
      out << std::endl;
    }
  }

  out << std::endl;
}

void
counterexample_value(
  std::ostream &out, const namespacet &ns, const expr2tc &lhs,
  const expr2tc &value)
{
  const irep_idt &identifier = to_symbol2t(lhs).get_symbol_name();
  std::string value_string;

  if (is_nil_expr(value))
    value_string = "(assignment removed)";
  else
  {
    value_string = from_expr(ns, identifier, value);

    if (is_constant_expr(value))
    {
      if (is_bv_type(value))
      {
        value_string +=
          " (" + integer2string(to_constant_int2t(value).constant_value) + ")";
      }
      else if (is_fixedbv_type(value))
      {
        value_string +=
          " (" + to_constant_fixedbv2t(value).value.to_ansi_c_string() + ")";
      }
    }
  }

  std::string name = id2string(identifier);

  const symbolt *symbol;
  if (!ns.lookup(identifier, symbol))
    if (symbol->pretty_name != "")
      name = id2string(symbol->pretty_name);
  out << "  " << name << "=" << value_string << std::endl;
}

void
show_goto_trace_gui(
  std::ostream &out, const namespacet &ns, const goto_tracet &goto_trace)
{
  locationt previous_location;

  for (goto_tracet::stepst::const_iterator it = goto_trace.steps.begin();
      it != goto_trace.steps.end(); it++)
  {
    const locationt &location = it->pc->location;

    if ((it->type == goto_trace_stept::ASSERT) && !it->guard)
    {
      out << "FAILED" << std::endl << it->comment
          << std::endl // value
          << std::endl // PC
          << location.file() << std::endl << location.line() << std::endl
          << location.column() << std::endl;
    }
    else if (it->type == goto_trace_stept::ASSIGNMENT)
    {
      irep_idt identifier;

      if (!is_nil_expr(it->original_lhs))
        identifier = to_symbol2t(it->original_lhs).get_symbol_name();
      else
        identifier = to_symbol2t(it->lhs).get_symbol_name();

      std::string value_string = from_expr(ns, identifier, it->value);

      const symbolt *symbol;
      irep_idt base_name;
      if (!ns.lookup(identifier, symbol))
        base_name = symbol->base_name;

      out << "TRACE" << std::endl;

      out << identifier << "," << base_name << ","
          << get_type_id(it->value->type) << "," << value_string << std::endl
          << it->step_nr << std::endl << it->pc->location.file() << std::endl
          << it->pc->location.line() << std::endl << it->pc->location.column()
          << std::endl;
    }
    else if (location != previous_location)
    {
      // just the location

      if (location.file() != "")
      {
        out << "TRACE" << std::endl;

        out
            << ","             // identifier
            << ","             // base_name
            << ","             // type
            << ""
            << std::endl // value
            << it->step_nr << std::endl << location.file() << std::endl
            << location.line() << std::endl << location.column() << std::endl;
      }
    }

    previous_location = location;
  }
}

void
show_state_header(
  std::ostream &out, const goto_trace_stept &state, const locationt &location,
  unsigned step_nr)
{
  out << std::endl;

  if (step_nr == 0)
    out << "Initial State";
  else
    out << "State " << step_nr;

  out << " " << location << " thread " << state.thread_nr << std::endl;

  // Print stack trace

  std::vector<dstring>::const_iterator it;
  for (it = state.stack_trace.begin(); it != state.stack_trace.end(); it++)
    out << it->as_string() << std::endl;

  out << "----------------------------------------------------" << std::endl;
}

void generate_goto_trace_in_graphml_format(std::string & tokenizer_path,
    std::string & filename, const namespacet & ns,
    const goto_tracet & goto_trace)
{
  tokenizer_executable_path = tokenizer_path;

  boost::property_tree::ptree graphml;
  boost::property_tree::ptree graph;
  std::map<int, std::map<int, std::string> > mapped_tokens;

  bool already_initialized = false;
  boost::property_tree::ptree last_created_node;
  std::string last_function = "";

  for(goto_tracet::stepst::const_iterator it = goto_trace.steps.begin();
      it != goto_trace.steps.end(); it++)
  {

    /* check if is an internal call */
    std::string::size_type find_bt =
      it->pc->location.to_string().find("built-in", 0);
    std::string::size_type find_lib =
      it->pc->location.to_string().find("library", 0);
    bool is_internal_call = (find_bt != std::string::npos)
        || (find_lib != std::string::npos);

    /** ignore internal calls and non assignments */
    if(!(it->type == goto_trace_stept::ASSIGNMENT)
        || (is_internal_call == true))
    {
      continue;
    }

    const irep_idt &identifier = to_symbol2t(it->lhs).get_symbol_name();

    /* check if is a temporary assignment */
    std::string id_str = id2string(identifier);
    std::string::size_type find_tmp = id_str.find("::$tmp::", 0);
    if(find_tmp != std::string::npos)
    {
      continue;
    }

    std::string filename = it->pc->location.get_file().as_string();

    if(already_initialized == false)
    {
      create_graphml(graphml, filename);
      create_graph(graph);
      boost::property_tree::ptree first_node;
      node_p first_node_p;
      first_node_p.isEntryNode = true;
      create_node(first_node, first_node_p);
      graph.add_child("node", first_node);
      last_created_node = first_node;
      already_initialized = true;
    }

    /* creating nodes and edges */
    boost::property_tree::ptree current_node;
    node_p current_node_p;
    create_node(current_node, current_node_p);
    graph.add_child("node", current_node);

    int line_number = std::atoi(
        it->pc->location.get_line().as_string().c_str());

    /* check if tokens already ok */
    if(mapped_tokens.size() == 0)
    {
      convert_c_file_in_tokens(filename, mapped_tokens);
    }
    boost::property_tree::ptree current_edge;
    edge_p current_edge_p;
    current_edge_p.originFileName = filename;

    /* adjusts assumptions */
    /* left hand */
    std::vector<std::string> split;
    std::string lhs_str = from_expr(ns, identifier, it->lhs);
    boost::split(split, lhs_str, boost::is_any_of("@"));
    lhs_str = split[0];
    std::string::size_type findamp = lhs_str.find("&", 0);
    if(findamp != std::string::npos)
    {
      lhs_str = lhs_str.substr(0, findamp);
    }
    std::string::size_type findds = lhs_str.find("$", 0);
    if(findds != std::string::npos)
    {
      lhs_str = lhs_str.substr(0, findds);
    }

    /* check if isn't in an array (modify assumptions) */
    if(it->lhs->type->type_id != it->lhs->type->array_id)
    {
      /* common cases */
      std::string value_str = from_expr(ns, identifier, it->value);
      /* remove memory address */
      std::string::size_type findat = value_str.find("@", 0);
      if(findat != std::string::npos)
      {
        value_str = value_str.substr(0, findat);
      }
      /* remove float suffix */
      std::string::size_type findfs = value_str.find("f", 0);
      if(findfs != std::string::npos)
      {
        value_str = value_str.substr(0, findfs);
      }
      /* check if has a double &quote */
      std::string::size_type findq1 = value_str.find("\"", 0);
      if(findq1 != std::string::npos)
      {
        std::string::size_type findq2 = value_str.find("\"", findq1 + 1);
        if(findq2 == std::string::npos)
        {
          value_str = value_str + "\"";
        }
      }
      std::string assumption = lhs_str + " = " + value_str + ";";
      std::string::size_type findesbm = assumption.find("__ESBMC", 0);
      std::string::size_type finddma = assumption.find("&dynamic_", 0);
      std::string::size_type findivo = assumption.find("invalid-object", 0);
      bool is_union = (it->rhs->type->type_id == it->rhs->type->union_id);
      bool is_struct = (it->rhs->type->type_id == it->rhs->type->struct_id);
      /* TODO check if is an union, struct or dynamic attr.
       * However, we need more details of the validation tools */
      bool is_esbmc_or_dynamic = ((findesbm != std::string::npos)
          || (finddma != std::string::npos) || (findivo != std::string::npos)
          || is_union || is_struct);
      if(is_esbmc_or_dynamic == false)
      {
        current_edge_p.assumption = assumption;
      }
    }

    /* check if entered in a function */
    std::string function_name = it->pc->function.as_string();
    size_t f = function_name.find("c::");
    if(f == 0)
    {
      function_name.replace(f, std::string("c::").length(), "");
    }
    if(function_name != last_function)
    {
      current_edge_p.enterFunction = function_name;
      last_function = function_name;
    }

    /* check if has a line number (to get tokens) */
    if(line_number != 0)
    {
      current_edge_p.startline = line_number;
      current_edge_p.endline = line_number;
      if(mapped_tokens.size() != 0)
      {
        std::map<int, std::string> current_line_tokens =
            mapped_tokens[line_number];
        std::map<int, std::string>::iterator it;
        std::string token_set = "";
        if(current_line_tokens.size() == 1)
        {
          token_set = std::to_string(current_line_tokens.begin()->first);
        }
        else
        {
          int first = current_line_tokens.begin()->first;
          int end = first + current_line_tokens.end()->first - 1;
          token_set = token_set
              + std::to_string(current_line_tokens.begin()->first) + ","
              + std::to_string(end);
        }
        std::string source_code = "";
        for(it = current_line_tokens.begin(); it != current_line_tokens.end();
            ++it)
        {
          source_code = source_code + it->second + " ";
        }
        current_edge_p.sourcecode = source_code.substr(0,
            source_code.length() - 1);
      }
    }
    create_edge(current_edge, current_edge_p, last_created_node, current_node);
    graph.add_child("edge", current_edge);
    last_created_node = current_node;
  }

  /* violation node */
  boost::property_tree::ptree violation_node;
  node_p violation_node_p;
  violation_node_p.isViolationNode = true;
  create_node(violation_node, violation_node_p);
  graph.add_child("node", violation_node);

  boost::property_tree::ptree violation_edge;
  edge_p violation_edge_p;
  create_edge(violation_edge, violation_edge_p, last_created_node,
      violation_node);
  graph.add_child("edge", violation_edge);

  /* write graphml */
  graphml.add_child("graphml.graph", graph);

#if (BOOST_VERSION >= 105700)
  boost::property_tree::xml_writer_settings<std::string> settings('\t', 1);
#else
  boost::property_tree::xml_writer_settings<char> settings('\t', 1);
#endif
  boost::property_tree::write_xml(filename, graphml, std::locale(), settings);
}

void
show_goto_trace(
  std::ostream &out, const namespacet &ns, const goto_tracet &goto_trace)
{
  unsigned prev_step_nr = 0;
  bool first_step = true;

  for (goto_tracet::stepst::const_iterator it = goto_trace.steps.begin();
      it != goto_trace.steps.end(); it++)
  {
    switch (it->type)
    {
      case goto_trace_stept::ASSERT:
        if (!it->guard)
        {
          show_state_header(out, *it, it->pc->location, it->step_nr);
          out << "Violated property:" << std::endl;
          if (!it->pc->location.is_nil())
            out << "  " << it->pc->location << std::endl;
          out << "  " << it->comment << std::endl;

          if (it->pc->is_assert())
            out << "  " << from_expr(ns, "", it->pc->guard) << std::endl;
          out << std::endl;

          // Having printed a property violation, don't print more steps.
          return;
        }
        break;

      case goto_trace_stept::ASSUME:
        break;

      case goto_trace_stept::ASSIGNMENT:
        if (it->pc->is_assign() || it->pc->is_return()
            || (it->pc->is_other() && is_nil_expr(it->lhs)))
        {
          if (prev_step_nr != it->step_nr || first_step)
          {
            first_step = false;
            prev_step_nr = it->step_nr;
            show_state_header(out, *it, it->pc->location, it->step_nr);
          }
          counterexample_value(out, ns, it->original_lhs, it->value);
        }
        break;

      case goto_trace_stept::OUTPUT:
      {
        printf_formattert printf_formatter;

        std::list<exprt> vec;

        for (std::list<expr2tc>::const_iterator it2 = it->output_args.begin();
            it2 != it->output_args.end(); it2++)
        {
          vec.push_back(migrate_expr_back(*it2));
        }

        printf_formatter(it->format_string, vec);
        printf_formatter.print(out);
        out << std::endl;

        break;
      }

      case goto_trace_stept::SKIP:
        // Something deliberately ignored
        break;

      case goto_trace_stept::RENUMBER:
        out << "Renumbered pointer to ";
        counterexample_value(out, ns, it->lhs, it->value);
        break;

      default:
        assert(false);
    }
  }
}

