// -*- coding: utf-8 -*-
// Copyright (C) by the Spot authors, see the AUTHORS file for details.
//
// This file is part of Spot, a model checking library.
//
// Spot is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
//
// Spot is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
// or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
// License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include "common_sys.hh"

#include <argp.h>
#include <error.h>
#include <argmatch.h>
#include <iomanip>
#include <sys/stat.h>

#include "common_aoutput.hh"
#include "common_finput.hh"
#include "common_setup.hh"
#include "common_trans.hh"
#include "common_ioap.hh"
#include <spot/priv/robin_hood.hh>
#include <spot/tl/formula.hh>
#include <spot/tl/apcollect.hh>
#include <spot/tl/print.hh>
#include <spot/tl/ltlf.hh>
#include <sys/stat.h>
#include <spot/twaalgos/dot.hh>
#include <spot/twaalgos/synthesis.hh> // for split_independent_formulas
#include <spot/twaalgos/ltlf2dfa.hh>
#include <spot/twaalgos/aiger.hh>
#include <spot/twaalgos/mealy_machine.hh>

enum
{
  OPT_AIGER = 256,
  OPT_BACKPROP,
  OPT_COMPOSITION,
  OPT_DECOMPOSE,
  OPT_GAME,
  OPT_GEQUIV,
  OPT_HIDE,
  OPT_INPUT,
  OPT_MINIMIZE,
  OPT_ONE_STEP,
  OPT_OUTPUT,
  OPT_PART_FILE,
  OPT_POLARITY,
  OPT_REALIZABILITY,
  OPT_SEMANTICS,
  OPT_SIMPLIFY_FORMULA,
  OPT_TLSF,
  OPT_TRANS,
  OPT_VERBOSE,
};


static const argp_option options[] =
  {
    /**************************************************/
    { nullptr, 0, nullptr, 0, "Input options:", 1 },
    { "outs", OPT_OUTPUT, "PROPS", 0,
      "comma-separated list of controllable (a.k.a. output) atomic"
      " propositions, , interpreted as a regex if enclosed in slashes", 0 },
    { "ins", OPT_INPUT, "PROPS", 0,
      "comma-separated list of uncontrollable (a.k.a. input) atomic"
      " propositions, interpreted as a regex if enclosed in slashes", 0 },
    { "part-file", OPT_PART_FILE, "FILENAME", 0,
      "read the I/O partition of atomic propositions from FILENAME", 0 },
    { "tlsf", OPT_TLSF, "FILENAME[/VAR=VAL[,VAR=VAL...]]", 0,
      "Read a TLSF specification from FILENAME, and call syfco to "
      "convert it into LTLf.  Any parameter assignment specified after a slash"
      " is passed as '-op VAR=VAL' to syfco." , 0 },
    { "semantics", OPT_SEMANTICS, "Moore|Mealy", 0,
      "Whether to work under Mealy (input-first) or Mealy "
      "(output-first) semantics.  The default is Mealy.", 0 },
    /**************************************************/
    { nullptr, 0, nullptr, 0, "Fine tuning:", 10 },
    { "translation", OPT_TRANS,
      "full|compositional|retricted|bfs-on-the-fly|dfs-on-the-fly|"
      "dfs-strict-on-the-fly", 0,
      "The type of translation to use: (full) is a direct translation to MTDFA,"
      " (compositional) breaks the specification on Boolean operators and "
      " builds the MTDFA by compositing minimized subautomata, (restrict) is"
      " a direct translation but that is restricted to the only part useful "
      "to synthesis, (dfs-on-the-fly) is the on-the-fly version of "
      "\"restrict\" that follow a dfs order that stop on previously seen "
      "BDD nodes, solving the game as the automaton is generated, "
      "(dfs-strict-on-the-fly) stops on visited states, (bfs-on-the-fly) "
      "same as dfs-on-the-fly but using bfs order.  "
      "The default is bfs-on-the-fly.", 0 },
    { "minimize", OPT_MINIMIZE, "yes|no", 0,
      "Minimize the automaton (disabled by default except for the compositional"
      " translation). Has no effect on on-the-fly translations.", 0 },
    { "composition", OPT_COMPOSITION, "size|ap", 0,
      "If the translation is set to \"compositional\" this option specify how"
      " to order n-ary compositions: by increasing size, or trying to group"
      " operands based on their APs (the default).",
      0 },
    { "decompose", OPT_DECOMPOSE, "yes|no", 0,
      "whether to decompose the specification as multiple output-disjoint "
      "problems to solve independently (enabled by default)", 0 },
    { "backprop", OPT_BACKPROP, "nodes|states|trival-states", 0,
      "whether backpropagation should be done at the node or state level "
      "(nodes by default)", 0 },
    { "polarity", OPT_POLARITY, "yes|no|before-decompose", 0,
      "whether to remove atomic propositions that always have the same "
      "polarity in the formula to speed things up (enabled by default, "
      "both before and after decomposition)", 0 },
    { "global-equivalence", OPT_GEQUIV, "yes|no|before-decompose", 0,
      "whether to remove atomic propositions that are always equivalent to "
      "another one (enabled by default, both before and after decomposition)",
      0 },
    { "one-step-preprocess", OPT_ONE_STEP, "yes|no", 0,
      "attempt check one-step realizability or unrealizability of each "
      "state during on-the-fly or restricted translations (enabled by "
      "default)", 0 },
    { "simplify-formula", OPT_SIMPLIFY_FORMULA, "yes|no", 0,
      "simplify the LTLf formula with cheap rewriting rules "
      "(enabled by default)", 0 },
    /**************************************************/
    { nullptr, 0, nullptr, 0, "Output options:", 20 },
    { "aiger", OPT_AIGER, "ite|isop|both[+ud][+dc]"
                                 "[+sub0|sub1|sub2]", OPTION_ARG_OPTIONAL,
      "encode the winning strategy as an AIG circuit and print it in AIGER"
      " format. The first word indicates the encoding to used: \"ite\" for "
      "If-Then-Else normal form; "
      "\"isop\" for irreducible sum of products; "
      "\"both\" tries both and keeps the smaller one. "
      "Other options further "
      "refine the encoding, see aiger::encode_bdd. Defaults to \"ite\".", 0 },
    { "dot", 'd', "game|strategy:OPT|aig:OPT", OPTION_ARG_OPTIONAL,
      "Use dot format when printing the result (game, strategy, or "
      "AIG circuit).  The options that may be used as OPT "
      "depend on the nature of what is printed. "
      "For strategy, standard automata rendering "
      "options are supported (e.g., see ltl2tgba --dot).  For AIG circuit, "
      "use (h) for horizontal and (v) for vertical layouts.", 0 },
    { "hoaf", 'H', hoa_option_doc_short, OPTION_ARG_OPTIONAL,
      hoa_option_doc_long, 0 },
    { "quiet", 'q', nullptr, 0, "suppress all normal output", 0 },
    { "hide-status", OPT_HIDE, nullptr, 0,
      "Hide the REALIZABLE or UNREALIZABLE line.  (Hint: exit status "
      "is enough of an indication.)", 0 },
    { "realizability", OPT_REALIZABILITY, nullptr, 0,
      "realizability only, do not compute a winning strategy", 0 },
    /**************************************************/
    { nullptr, 0, nullptr, 0, "Miscellaneous options:", -1 },
    { "verbose", OPT_VERBOSE, nullptr, 0, "verbose mode", 0 },
    { nullptr, 0, nullptr, 0, nullptr, 0 },
  };

static const struct argp_child children[] =
  {
    { &finput_argp_headless, 0, nullptr, 0 },
    { &misc_argp, 0, nullptr, 0 },
    { nullptr, 0, nullptr, 0 }
  };

static const char argp_program_doc[] = "\
Convert LTLf formulas to transition-based deterministic finite automata.\n\n\
If multiple formulas are supplied, several automata will be output.";

enum translation_type {
  translation_otf_dfs,
  translation_otf_dfs_strict,
  translation_otf_bfs,
  translation_direct_restricted,
  translation_direct_full,
  translation_compositional,
};

static const char* const translation_args[] =
  {
    "dfs", "dfs-otf", "dfs-on-the-fly",
    "dfs-strict-otf", "dfs-strict-on-the-fly",
    "bfs", "bfs-otf", "bfs-on-the-fly",
    "direct", "direct-restricted", "restricted-direct",
    "direct-full", "full-direct",
    "compositional", "compose",
    nullptr
  };
static translation_type translation_values[] =
  {
    translation_otf_dfs, translation_otf_dfs, translation_otf_dfs,
    translation_otf_dfs_strict, translation_otf_dfs_strict,
    translation_otf_bfs, translation_otf_bfs, translation_otf_bfs,
    translation_direct_restricted, translation_direct_restricted,
    translation_direct_restricted,
    translation_direct_full, translation_direct_full,
    translation_compositional, translation_compositional,
  };
ARGMATCH_VERIFY(translation_args, translation_values);
static translation_type opt_trans = translation_otf_bfs;

static const char* const minimize_args[] =
  {
    "yes", "true", "enabled", "1",
    "no", "false", "disabled", "0",
    nullptr
  };
static bool minimize_values[] =
  {
    true, true, true, true,
    false, false, false, false,
  };
ARGMATCH_VERIFY(minimize_args, minimize_values);
static bool opt_minimize = false;
static bool opt_minimize_set = false;
static bool opt_one_step = true;
static bool opt_simplify_ltl = true;

static std::ostream* opt_verbose = nullptr;

enum backprop_style { bp_nodes, bp_states, bp_trival_states };
static const char* const backprop_args[] =
  {
    "states", "nodes", "trival-states", nullptr
  };
static const backprop_style backprop_values[] =
  {
    bp_states, bp_nodes, bp_trival_states,
  };
ARGMATCH_VERIFY(backprop_args, backprop_values);
static backprop_style opt_backprop = bp_nodes;

static const char* const composition_args[] =
  {
    "size", "ap", nullptr
  };
static const bool composition_values[] =
  {
    false, true,
  };
ARGMATCH_VERIFY(composition_args, composition_values);
static bool opt_composition_by_ap = false;

static const char* const decompose_args[] =
  {
    "yes", "true", "enabled", "1",
    "no", "false", "disabled", "0",
    nullptr
  };
static const bool decompose_values[] =
  {
    true, true, true, true,
    false, false, false, false,
  };
ARGMATCH_VERIFY(decompose_args, decompose_values);
static const char* const polarity_args[] =
  {
    "yes", "true", "enabled", "1",
    "no", "false", "disabled", "0",
    "before-decompose",
    nullptr
  };
enum polarity_choice { pol_no, pol_yes, pol_before_decompose };
static const polarity_choice polarity_values[] =
  {
    pol_yes, pol_yes, pol_yes, pol_yes,
    pol_no, pol_no, pol_no, pol_no,
    pol_before_decompose
  };
ARGMATCH_VERIFY(polarity_args, polarity_values);

static const char* const semantics_args[] =
  {
    "mealy", "moore",
    "Mealy", "Moore",
    nullptr
  };
enum semantics_choice { semantics_default, semantics_mealy, semantics_moore };
static const semantics_choice semantics_values[] =
  {
    semantics_mealy, semantics_moore,
    semantics_mealy, semantics_moore,
  };
ARGMATCH_VERIFY(semantics_args, semantics_values);

static const char* const dot_args[] =
  {
    "game", "strategy", "aig", nullptr
  };
enum dot_choice { dot_none = 0, dot_game, dot_strategy, dot_aig };
static const dot_choice dot_values[] =
  {
    dot_game, dot_strategy, dot_aig
  };
ARGMATCH_VERIFY(dot_args, dot_values);

static bool opt_decompose_ltl = true;
static polarity_choice opt_polarity = pol_yes;
static polarity_choice opt_gequiv = pol_yes;
static bool opt_realizability = false;
static semantics_choice opt_semantics = semantics_default;
static const char* opt_aiger = nullptr;
static dot_choice opt_dot = dot_none;
static const char* opt_dot_arg = "";
static bool opt_show_status = true;

static int
parse_opt(int key, char *arg, struct argp_state *)
{
  // Called from C code, so should not raise any exception.
  BEGIN_EXCEPTION_PROTECT;
  switch (key)
    {
    case 'd':
      {
        automaton_format = Dot;
        automaton_format_opt = "";
        if (!arg)
          {
            opt_dot = dot_aig;
            break;
          }
        // split arg on ':'
        char *colon = strchr(arg, ':');
        if (colon)
          {
            automaton_format_opt = opt_dot_arg = colon + 1;
            *colon = 0;
          }
        if (arg)
          opt_dot = XARGMATCH("--dot", arg, dot_args, dot_values);
        else
          opt_dot = dot_aig;
        if (opt_dot == dot_aig && opt_aiger == nullptr)
          opt_aiger = "ite";
        break;
      }
    case 'H':
      opt_dot = dot_none;
      automaton_format = Hoa;
      automaton_format_opt = arg;
      break;
    case OPT_AIGER:
      opt_aiger = arg ? arg : "ite";
      break;
    case OPT_BACKPROP:
      opt_backprop = XARGMATCH("--backprop", arg,
                               backprop_args, backprop_values);
      break;
    case OPT_COMPOSITION:
      opt_composition_by_ap = XARGMATCH("--composition", arg,
                                        composition_args, composition_values);
      break;
    case OPT_DECOMPOSE:
      opt_decompose_ltl = XARGMATCH("--decompose", arg,
                                    decompose_args, decompose_values);
      break;
    case OPT_GEQUIV:
      opt_gequiv = XARGMATCH("--global-equivalence", arg,
                               polarity_args, polarity_values);
      break;
    case OPT_HIDE:
      opt_show_status = false;
      break;
    case OPT_INPUT:
      all_input_aps.emplace();
      split_aps(arg, *all_input_aps);
      break;
    case OPT_MINIMIZE:
      opt_minimize = XARGMATCH("--minimize", arg,
                               minimize_args, minimize_values);
      opt_minimize_set = true;
      break;
    case OPT_ONE_STEP:
      opt_one_step = XARGMATCH("--one-step", arg,
                               decompose_args, decompose_values);
      break;
    case OPT_OUTPUT:
      all_output_aps.emplace();
      split_aps(arg, *all_output_aps);
      break;
    case OPT_PART_FILE:
      read_part_file(arg);
      break;
    case OPT_POLARITY:
      opt_polarity = XARGMATCH("--polarity", arg,
                               polarity_args, polarity_values);
      break;
    case OPT_REALIZABILITY:
      opt_realizability = true;
      break;
    case OPT_SEMANTICS:
      opt_semantics = XARGMATCH("--semantics", arg,
                                semantics_args, semantics_values);
      break;
    case OPT_TLSF:
      jobs.emplace_back(arg, job_type::TLSF_FILENAME);
      break;
    case OPT_TRANS:
      opt_trans = XARGMATCH("--translation", arg,
                            translation_args, translation_values);
      break;
    case OPT_VERBOSE:
      opt_verbose = &std::cerr;
      break;
    case OPT_SIMPLIFY_FORMULA:
      opt_simplify_ltl = XARGMATCH("--simplify-formula", arg,
                                   decompose_args, decompose_values);
      break;
    case 'q':
      automaton_format = Quiet;
      opt_show_status = false;
      break;
    case ARGP_KEY_ARG:
      // FIXME: use stat() to distinguish filename from string?
      jobs.emplace_back(arg, ((*arg == '-' && !arg[1])
                              ? job_type::LTL_FILENAME
                              : job_type::LTL_STRING));
      break;

    default:
      return ARGP_ERR_UNKNOWN;
    }
  END_EXCEPTION_PROTECT;
  return 0;
}

namespace
{
  static int
  solve_formula(spot::formula original_f,
                const std::vector<std::string>& input_aps,
                const std::vector<std::string>& output_aps,
                bool mealy_semantics)
  {
    if (opt_verbose)
      *opt_verbose << "using "
                   << (mealy_semantics ? "Mealy" : "Moore")
                   << " semantics\n";
    spot::formula f = original_f;

    spot::bdd_dict_preorder dict;
    {
      std::unordered_set<spot::formula> outputs;
      for (const std::string& s: output_aps)
        outputs.insert(spot::formula::ap(s));
      // For Mealy semantics, inputs should appear first in the
      // MTBDDs.  For Moore semantics, outputs should be first.
      // Pre-registering those variables will ensure that.  We want to
      // register them in the order they are found in the formula,
      // this this ways variables that are used together are more
      // likely to be close in the order.
      f.traverse([&](const spot::formula& f)
      {
        if (f.is(spot::op::ap) &&
            ((outputs.find(f) == outputs.end()) == mealy_semantics))
          dict.register_proposition(f);
        return false;
      });
    }

    // Attempt to remove superfluous atomic propositions
    std::unique_ptr<spot::realizability_simplifier> rs = nullptr;
    if (opt_polarity != pol_no || opt_gequiv != pol_no)
      {
        unsigned opt = 0;
        if (opt_polarity != pol_no)
          opt |= spot::realizability_simplifier::polarity;
        if (opt_gequiv != pol_no)
          opt |= mealy_semantics ?
            spot::realizability_simplifier::global_equiv :
            spot::realizability_simplifier::global_equiv_moore;
        rs.reset(new spot::realizability_simplifier(original_f, input_aps,
                                                    opt, opt_verbose));
        f = rs->simplified_formula();
      }

    std::vector<spot::formula> sub_form;
    std::vector<std::set<spot::formula>> sub_outs;
    if (opt_decompose_ltl)
    {
      if (!f.is_syntactic_stutter_invariant())
        {
          // Avoid Issue #610 until we now a better restriction.
          if (opt_verbose)
            *opt_verbose
              << "decomposition not attempted because the formula uses X\n";
        }
      else
        {
          auto subs = split_independent_formulas(f, output_aps);
          if (subs.first.size() > 1)
            {
              if (opt_verbose)
                *opt_verbose << "there are " << subs.first.size()
                             << " subformulas\n";
              sub_form = subs.first;
              sub_outs = subs.second;
            }
          else if (opt_verbose)
            *opt_verbose << "no decomposition found\n";
        }
    }

    // FIXME: revisit this after split_independent_formulas() has
    // been tuned to LTLf.
    //
    // When trying to split the formula, we can apply transformations
    // that increase its size. This is why we will use the original
    // formula if it has not been cut.
    if (sub_form.empty())
      {
        sub_form = { f };
        sub_outs.resize(1);
        // Gather the list of output APs, including those that have
        // been removed during simplification.
        if (rs)
          {
            robin_hood::unordered_set<spot::formula> removed_outputs;
            for (auto [from, from_is_input, to] : rs->get_mapping())
              {
                (void) to;
                if (!from_is_input)
                  removed_outputs.insert(from);
              }
            for (const std::string& apstr: output_aps)
              {
                spot::formula ap = spot::formula::ap(apstr);
                if (removed_outputs.find(ap) == removed_outputs.end())
                  sub_outs[0].insert(ap);
              }
          }
        else
          {
            for (const std::string& apstr: output_aps)
              sub_outs[0].insert(spot::formula::ap(apstr));
          }
      }

    // convert sub_outs (which is a vector of vectors of formulas) to
    // a vector of vectors of strings.
    std::vector<std::vector<std::string>> sub_outs_str;
    std::transform(sub_outs.begin(), sub_outs.end(),
                   std::back_inserter(sub_outs_str),
                   [](const auto& forms) {
                     std::vector<std::string> r;
                     r.reserve(forms.size());
                     for (auto f: forms)
                       r.push_back(f.ap_name());
                     return r;
                   });

    assert((sub_form.size() == sub_outs.size())
           && (sub_form.size() == sub_outs_str.size()));

    auto sub_f = sub_form.begin();
    auto sub_o = sub_outs_str.begin();
    unsigned numsubs = sub_form.size();

    std::vector<spot::twa_graph_ptr> mealy_machines;
    const char* indent = "";

    spot::ltlf_simplifier simplify_cache;

    for (; sub_f != sub_form.end(); ++sub_f, ++sub_o)
      {
        if (numsubs > 1 && (opt_polarity == pol_yes || opt_gequiv == pol_yes))
          {
            unsigned opt = 0;
            if (opt_polarity == pol_yes)
              opt |= spot::realizability_simplifier::polarity;
            if (opt_gequiv == pol_yes)
              opt |= mealy_semantics ?
                spot::realizability_simplifier::global_equiv :
                spot::realizability_simplifier::global_equiv_moore;
            if (opt_verbose)
              *opt_verbose << "working on subformula " << *sub_f << '\n';
            spot::realizability_simplifier rsub(*sub_f, input_aps, opt,
                                                opt_verbose);
            *sub_f = rsub.simplified_formula();
            rs->merge_mapping(rsub);
            indent = "  ";
          }

        if (opt_simplify_ltl)
          if (spot::formula fsimpl = simplify_cache.simplify(*sub_f);
              fsimpl != *sub_f)
            {
              if (opt_verbose)
                *opt_verbose << indent << "formula simplified to "
                             << fsimpl << '\n';
              *sub_f = fsimpl;
            }

        spot::stopwatch st;
        auto stop_trans = [&] (spot::mtdfa_ptr a)
        {
          if (!opt_verbose)
            return;
          double trans_time = st.stop();
          *opt_verbose << indent << "translation to MTDFA ("
                       << a->num_roots() << " roots, "
                       << a->aps.size() << " APs) took "
                       << trans_time << " seconds\n";
        };
        auto minimize_maybe = [&] (spot::mtdfa_ptr& a)
        {
          if (!opt_minimize)
            return;
          st.start();
          a = spot::minimize_mtdfa(a);
          if (!opt_verbose)
            return;
          double trans_time = st.stop();
          *opt_verbose << indent << "minimization of MTDFA (now "
                       << a->num_roots() << " roots, "
                       << a->aps.size() << " APs) took "
                       << trans_time << " seconds\n";
        };

        st.start();
        spot::mtdfa_ptr a;
        bool a_is_strategy_already = false;
        switch (opt_trans)
          {
          case translation_direct_restricted:
            if (opt_verbose)
              *opt_verbose << indent
                           << "starting restricted translation with"
                           << (opt_one_step ? "" : "out")
                           << " one-step preprocess\n";
            a = spot::ltlf_to_mtdfa_for_synthesis(*sub_f, dict, *sub_o,
                                                  spot::state_refine,
                                                  opt_one_step,
                                                  false /* realizability */);
            a->names.clear();
            stop_trans(a);
            minimize_maybe(a);
            break;
          case translation_direct_full:
            if (opt_verbose)
              *opt_verbose << indent << "starting full translation\n";
            a = spot::ltlf_to_mtdfa(*sub_f, dict);
            a->names.clear();
            a->set_controllable_variables(*sub_o, true);
            stop_trans(a);
            minimize_maybe(a);
            break;
          case translation_compositional:
            if (opt_verbose)
              *opt_verbose << indent
                           << "starting compositional translation with"
                           << (opt_minimize ? "" : "out")
                           << " minimization, with "
                           << (opt_composition_by_ap ? "AP" : "size")
                           << "-based ordering\n";
            a = spot::ltlf_to_mtdfa_compose(*sub_f, dict,
                                            opt_minimize,
                                            opt_composition_by_ap,
                                            false);
            a->set_controllable_variables(*sub_o, true);
            stop_trans(a);
            break;
          case translation_otf_bfs:
          case translation_otf_dfs:
          case translation_otf_dfs_strict:
            {
              bool dfs_strict = opt_trans == translation_otf_dfs_strict;
              bool dfs = (opt_trans == translation_otf_dfs) | dfs_strict;
              if (opt_backprop != bp_nodes)
                error(2, 0,
                      "on-the-fly translations onlyl support --backprop=nodes");
              if (opt_verbose)
                {
                  *opt_verbose << indent
                               << ("starting on-the-fly translation with "
                                   "node-based backpropagation, with ");
                  if (dfs_strict)
                    *opt_verbose << "strict ";
                  *opt_verbose << (dfs ? 'D' : 'B') << "FS order, with"
                               << (opt_one_step ? "" : "out")
                               << " one-step preprocess\n";
                }
              auto bp = (dfs_strict ? spot::dfs_strict_node_backprop
                         : (dfs ? spot::dfs_node_backprop
                            : spot::bfs_node_backprop));
              a = spot::ltlf_to_mtdfa_for_synthesis(*sub_f, dict, *sub_o,
                                                    bp, opt_one_step,
                                                    opt_realizability);
              a->names.clear();
              stop_trans(a);
              a_is_strategy_already = true;
              minimize_maybe(a);
              break;
            }
          }
        if (opt_dot == dot_game)
          {
            a->print_dot(std::cout, -1, false);
            continue;
          }
        if (opt_realizability)
          {
            bool unrealizable = false;
            if (a_is_strategy_already)
              {
                if (opt_verbose)
                  *opt_verbose << indent
                               << "MTDFA game was solved during translation\n";
                unrealizable = a->states[0] == bddfalse;
              }
            else
              {
                if (opt_verbose)
                  {
                    *opt_verbose << indent
                                 << "solving game by backpropagation at "
                                 << (opt_backprop == bp_nodes ?
                                     "node" : "state");
                    if (opt_backprop == bp_trival_states)
                      *opt_verbose << " level with trivalued logic\n";
                    else
                      *opt_verbose << " level\n";
                  }
                st.start();
                if (opt_backprop == bp_nodes)
                  unrealizable = !mtdfa_to_backprop(a).winner(0);
                else if (opt_backprop == bp_states)
                  unrealizable = !mtdfa_winning_region_lazy(a)[0];
                else // bp_trival_states
                  unrealizable = !mtdfa_winning_region_lazy3(a)[0].is_true();
                double solve_time = st.stop();
                if (opt_verbose)
                  *opt_verbose << indent << "game solved in "
                               << solve_time << " seconds\n";
              }
            if (unrealizable)
              {
                if (opt_show_status)
                  std::cout << "UNREALIZABLE" << std::endl;
                return 1;
              }
          }
        else
          {
            if (a_is_strategy_already)
              {
                if (opt_verbose)
                  *opt_verbose << indent
                               << "translation produced a strategy already\n";
              }
            else
              {
                if (opt_verbose)
                  *opt_verbose << indent
                               << "solving game by backpropagation at "
                               << (opt_backprop ? "node" : "state")
                               << " level\n";
                st.start();
                a = spot::mtdfa_winning_strategy(a, opt_backprop);
                double time = st.stop();
                if (opt_verbose)
                  {
                    // count number of non-false roots
                    unsigned nf = 0;
                    for (const bdd& r: a->states)
                      nf += r != bddfalse;
                    *opt_verbose << indent << "strategy (" << nf
                                 << " roots) found in " << time
                                 << " seconds\n";
                  }
              }
            if (a->states[0] == bddfalse)
              {
                if (opt_show_status)
                  std::cout << "UNREALIZABLE" << std::endl;
                return 1;
              }
            st.start();
            spot::twa_graph_ptr m = spot::mtdfa_strategy_to_mealy(a);
            double time = st.stop();
            if (opt_verbose)
              *opt_verbose << indent << "Mealy machine ("
                           << m->num_states() << " states) created in "
                           << time << " seconds\n";
            mealy_machines.push_back(m);
          }
      }
    if (opt_dot == dot_game)
      return 0;
    if (opt_show_status)
      std::cout << "REALIZABLE" << std::endl;
    if (opt_realizability)
      return 0;

    if (!opt_aiger && (opt_dot == dot_strategy
                       || automaton_format == Hoa))
      {
        spot::twa_graph_ptr strat = nullptr;
        for (auto m: mealy_machines)
          if (strat)
            strat = spot::mealy_product(strat, m);
          else
            strat = m;
        if (rs)        // Add any AP we removed
          rs->patch_mealy(strat);
        strat->merge_edges();
        automaton_printer printer;
        spot::process_timer timer_printer_dummy;
        printer.print(strat, timer_printer_dummy);
        return 0;
      }

    if (opt_aiger)
      {
        spot::stopwatch sw2;
        sw2.start();
        spot::aig_ptr saig = spot::mealy_machines_to_aig(mealy_machines,
                                                         opt_aiger,
                                                         input_aps,
                                                         sub_outs_str,
                                                         rs.get());
        double aigtime = sw2.stop();
        if (opt_verbose)
          *opt_verbose << "AIG circuit ("
                       << saig->num_latches() << " latches, "
                       << saig->num_gates() << " gates) created in "
                       << aigtime << " seconds\n";

        if (automaton_format != Quiet)
          {
            if (opt_dot == dot_aig)
              spot::print_dot(std::cout, saig, opt_dot_arg);
            else
              spot::print_aiger(std::cout, saig) << '\n';
          }
      }
    return 0;
  }

  class trans_processor final: public job_processor
  {
  public:
    automaton_printer printer{ltl_input};

    int
    process_formula(spot::formula f,
                    const char* filename = nullptr, int linenum = 0) override
    {
      if (!f.is_ltl_formula())
        {
          std::string s = spot::str_psl(f);
          error_at_line(2, 0, filename, linenum,
                        "formula '%s' is not an LTLf formula",
                        s.c_str());
        }
      auto [input_aps, output_aps] =
        filter_list_of_aps(f, filename, linenum);
      return solve_formula(f, input_aps, output_aps,
                           opt_semantics != semantics_moore);
    }


    int
    process_tlsf_file(const char* filename) override
    {
      if (assignments)
        {
          free(assignments);
          assignments = nullptr;
        }
      char* syfco_filename = const_cast<char*>(filename);

      // The filename passed can be either a real filename, or
      // a string link FILENAME/ASSIGNMENTS where ASSIGNMENTS are
      // comma-separated assignments.  E.g., "../spec.tlsf/N=3,M=4".
      //
      // If the filename contains a slash followed by some equal sign,
      // and does not correspond to an existing file, then we remove
      // the part after the last slash and assume the rest is a
      // filename before passing it to syfco.  We don't check if the
      // new (truncated) filename exist, syfco will do it anyway.
      struct stat buf;
      if (const char* slash = strrchr(filename, '/');
          slash && strchr(slash, '=') && stat(filename, &buf) != 0)
        {
          if (real_filename)
            free(real_filename);
          real_filename = strndup(filename, slash - filename);
          assignments = strdup(slash + 1);
          syfco_filename = real_filename;
        }

      std::vector<char*> command;
      static char arg0[] = "syfco";
      command.push_back(arg0);
      // split assignments on commas, and pass each VAR=VALUE
      // as -op VAR=VALUE to syfco.
      if (assignments)
        {
          static char argop[] = "-op";
          char* assignment = strtok(assignments, ",");
          while (assignment)
            {
              command.push_back(argop);
              command.push_back(assignment);
              assignment = strtok(nullptr, ",");
            }
        }
      unsigned after_assignments = command.size();
      static char arg1[] = "-f";
      command.push_back(arg1);
      static char arg2[] = "ltlxba-fin";
      command.push_back(arg2);
      static char arg3[] = "-m";
      command.push_back(arg3);
      static char arg4[] = "fully";
      command.push_back(arg4);
      command.push_back(syfco_filename);
      command.push_back(nullptr);

      std::string tlsf_string = read_stdout_of_command(command, opt_verbose);

      // The set of atomic proposition will be temporary set to those
      // given by syfco, unless they were forced from the command-line.
      bool reset_aps = false;
      if (!all_input_aps.has_value() && !all_output_aps.has_value())
        {
          reset_aps = true;
          command.resize(after_assignments);
          static char arg[] = "--print-output-signals";
          command.push_back(arg);
          command.push_back(syfco_filename);
          command.push_back(nullptr);
          std::string res = read_stdout_of_command(command, opt_verbose);

          all_output_aps.emplace(std::vector<std::string>{});
          split_aps(res, *all_output_aps);
          for (const std::string& a: *all_output_aps)
            identifier_map.emplace(a, true);
        }
      semantics_choice old_semantics = opt_semantics;
      if (old_semantics == semantics_default)
        {
          command.resize(1);    // syfco
          static char arg[] = "--print-target";
          command.push_back(arg);
          command.push_back(syfco_filename);
          command.push_back(nullptr);
          std::string res = read_stdout_of_command(command, opt_verbose);

          auto not_space = [](unsigned char c){ return !std::isspace(c); };
          res.erase(std::find_if(res.rbegin(), res.rend(), not_space).base(),
                    res.end());
          if (res == "Mealy")
            opt_semantics = semantics_mealy;
          else if (res == "Moore")
            opt_semantics = semantics_moore;
          else
            error(2, 0, "%s: unknown target: `%s'", filename, res.c_str());
        }
      int res = process_string(tlsf_string, filename);
      opt_semantics = old_semantics;
      if (reset_aps)
        {
          all_output_aps.reset();
          identifier_map.clear();
        }
      return res;
    }

  };
}

int
main(int argc, char** argv)
{
  return protected_main(argv, [&] {
      const argp ap = { options, parse_opt, "[FORMULA...]",
                        argp_program_doc, children, nullptr, nullptr };

      if (int err = argp_parse(&ap, argc, argv, ARGP_NO_HELP, nullptr, nullptr))
        exit(err);

      check_no_formula();
      process_io_options();

      // For compositional translation, we enable minimization by default.
      if (!opt_minimize_set && opt_trans == translation_compositional)
        opt_minimize = true;

      trans_processor processor;
      if (int res = processor.run(); res == 0 || res == 1)
        return res;
      return 2;
  });
}
