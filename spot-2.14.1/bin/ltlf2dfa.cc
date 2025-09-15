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
#include <spot/tl/formula.hh>
#include <spot/tl/print.hh>
#include <spot/twaalgos/ltlf2dfa.hh>
#include <spot/tl/ltlf.hh>

enum
{
  OPT_COMPOSITION = 256,
  OPT_NEGATE,
  OPT_KEEP_NAMES,
  OPT_MTDFA_DOT,
  OPT_MTDFA_STATS,
  OPT_SIMPLIFY_FORMULA,
  OPT_TLSF,
  OPT_TRANS,
  OPT_MINIMIZE,
};


static const argp_option options[] =
  {
    /**************************************************/
    { nullptr, 0, nullptr, 0, "Input options:", 1 },
    { "tlsf", OPT_TLSF, "FILENAME[/VAR=VAL[,VAR=VAL...]]", 0,
      "Read a TLSF specification from FILENAME, and call syfco to "
      "convert it into LTLf.  Any parameter assignment specified after a slash"
      " is passed as '-op VAR=VAL' to syfco." , 0 },
    { "negate", OPT_NEGATE, nullptr, 0, "negate each formula", 0 },
    /**************************************************/
    { nullptr, 0, nullptr, 0, "Processing options:", 10 },
    { "translation", OPT_TRANS, "direct|compositional", 0,
      "Whether to translate the formula directly as a whole, or to "
      "assemble translations from subformulas.  Default is compositional.",
      0 },
    { "keep-names", OPT_KEEP_NAMES, nullptr, 0,
      "Keep the names of formulas that label states in the output automaton.",
      0 },
    { "minimize", OPT_MINIMIZE, "yes|no", 0,
      "Minimize the automaton (enabled by default).", 0 },
    { "composition", OPT_COMPOSITION, "size|ap", 0,
      "How to order n-ary compositions in the compositional translation.  "
      "By increasing size, or trying to group operands based on their APs.",
      0 },
    { "simplify-formula", OPT_SIMPLIFY_FORMULA, "yes|no", 0,
      "simplify the LTLf formula with cheap rewriting rules "
      "(disabled by default)", 0 },
    /**************************************************/
    { nullptr, 0, nullptr, 0, "Output options:", 20 },
    { "hoaf", 'H', "1.1|b|i|k|l|m|s|t|v", OPTION_ARG_OPTIONAL,
      "Output the automaton in HOA format (default).  Add letters to select "
      "(1.1) version 1.1 of the format, "
      "(b) create an alias basis if >=2 AP are used, "
      "(i) use implicit labels for complete deterministic automata, "
      "(s) prefer state-based acceptance when possible [default], "
      "(t) force transition-based acceptance, "
      "(m) mix state and transition-based acceptance, "
      "(k) use state labels when possible, "
      "(l) single-line output, "
      "(v) verbose properties", 0 },
    { "dot", 'd', "options", OPTION_ARG_OPTIONAL,
      "print the automaton in DOT format", 0 },
    { "mtdfa-dot", OPT_MTDFA_DOT, nullptr, 0,
      "print the MTDFA in DOT format", 0 },
    { "mtdfa-stats", OPT_MTDFA_STATS, "basic|nodes|paths", OPTION_ARG_OPTIONAL,
      "print statistics about the MTDFA: 'basic' (the default) displays "
      "only the number of states and atomic propositions (this is obtained in "
      "constant time), 'nodes' additionally displays nodes "
      "counts (computing those is proportional to the size of the BDD) "
      "'paths' additionally displays path counts (this can be exponential in "
      " number of atomic propositions", 0 },
    { "quiet", 'q', nullptr, 0, "suppress all normal output", 0 },
    /**************************************************/
    { nullptr, 0, nullptr, 0, "Miscellaneous options:", -1 },
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

enum translation_type { translation_direct, translation_compositional };

static const char* const translation_args[] =
  {
    "direct", "compositional", "compose", nullptr
  };
static translation_type translation_values[] =
  {
    translation_direct, translation_compositional, translation_compositional,
  };
ARGMATCH_VERIFY(translation_args, translation_values);
static translation_type opt_trans = translation_compositional;

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
static bool opt_minimize = true;
static bool opt_simplify_formula = false;
static bool opt_keep_names = false;


static const char* const composition_args[] =
  {
    "size", "ap", nullptr
  };
static bool composition_values[] =
  {
    false, true,
  };
ARGMATCH_VERIFY(composition_args, composition_values);
static bool opt_composition_by_ap = false;


static const char* const stats_args[] =
  {
    "basic", "nodes", "paths", nullptr
  };
static int stats_values[] =
  {
    0, 1, 2,
  };
ARGMATCH_VERIFY(stats_args, stats_values);
static int opt_stats = 0;


enum mtdfa_output_type { mtdfa_none, mtdfa_dot, mtdfa_stats };
static mtdfa_output_type mtdfa_output = mtdfa_none;

static bool negate = false;

static int
parse_opt(int key, char *arg, struct argp_state *)
{
  // Called from C code, so should not raise any exception.
  BEGIN_EXCEPTION_PROTECT;
  switch (key)
    {
    case OPT_COMPOSITION:
      opt_composition_by_ap = XARGMATCH("--composition", arg,
                                        composition_args, composition_values);
      break;
    case OPT_NEGATE:
      negate = true;
      break;
    case OPT_KEEP_NAMES:
      opt_keep_names = true;
      break;
    case OPT_TLSF:
      jobs.emplace_back(arg, job_type::TLSF_FILENAME);
      break;
    case OPT_TRANS:
      opt_trans = XARGMATCH("--translation", arg,
                            translation_args, translation_values);
      break;
    case OPT_MINIMIZE:
      opt_minimize = XARGMATCH("--minimize", arg,
                               minimize_args, minimize_values);
      break;
    case OPT_SIMPLIFY_FORMULA:
      opt_simplify_formula = XARGMATCH("--simplify-formula", arg,
                                       minimize_args, minimize_values);
      break;
    case 'd':
      automaton_format = Dot;
      automaton_format_opt = arg;
      mtdfa_output = mtdfa_none;
      break;
    case 'H':
      automaton_format = Hoa;
      automaton_format_opt = arg;
      mtdfa_output = mtdfa_none;
      break;
    case 'q':
      automaton_format = Quiet;
      mtdfa_output = mtdfa_none;
      break;
    case OPT_MTDFA_DOT:
      mtdfa_output = mtdfa_dot;
      break;
    case OPT_MTDFA_STATS:
      mtdfa_output = mtdfa_stats;
      if (arg)
        opt_stats = XARGMATCH("--mtdfa-stats", arg,
                              stats_args, stats_values);
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
  class trans_processor final: public job_processor
  {
  public:
    automaton_printer printer{ltl_input};
    spot::bdd_dict_ptr& dict;

    explicit trans_processor(spot::bdd_dict_ptr& dict)
      : dict(dict)
    {
    }

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

      spot::process_timer timer;
      timer.start();

      if (negate)
        f = spot::formula::Not(f);

      if (opt_simplify_formula)
        {
          spot::ltlf_simplifier ls;
          f = ls.simplify(f);
        }

      spot::mtdfa_ptr a;
      if (opt_trans == translation_direct)
        {
          a = spot::ltlf_to_mtdfa(f, dict);
          if (!opt_keep_names)
            a->names.clear();
          if (opt_minimize)
            a = spot::minimize_mtdfa(a);
        }
      else
        {
          a = spot::ltlf_to_mtdfa_compose(f, dict,
                                          opt_minimize,
                                          opt_composition_by_ap,
                                          opt_keep_names);
        }

      timer.stop();

      if (mtdfa_output == mtdfa_none && automaton_format == Quiet)
        {
          return 0;
        }
      else if (mtdfa_output == mtdfa_dot)
        {
          a->print_dot(std::cout);
        }
      else if (mtdfa_output == mtdfa_stats)
        {
          spot::mtdfa_stats s = a->get_stats(opt_stats >= 1, opt_stats >= 2);
          std::cout << "states: " << s.states << '\n'
                    << "aps: " << s.aps << '\n';
          if (opt_stats >= 1)
            {
              std::cout << "internal nodes: " << s.nodes << '\n'
                        << "terminal nodes: " << s.terminals << '\n'
                        << "constant nodes: " << s.has_true + s.has_false;
              if (s.has_true && s.has_false)
                std::cout << " (false and true)\n";
              else if (s.has_true && !s.has_false)
                std::cout << " (true)\n";
              else if (!s.has_true && s.has_false)
                std::cout << " (true)\n";
              else
                std::cout << '\n';
              unsigned long long total_nodes
                = s.nodes + s.terminals + s.has_true + s.has_false;
              std::cout << "total nodes: " << total_nodes;
              std::cout << " (" << (total_nodes + 32) / 64 << "KB)\n";
            }
          if (opt_stats >= 2)
            std::cout << "paths: " << s.paths << '\n'
                      << "edges: " << s.edges << '\n';
          bddStat bs;
          bdd_stats(&bs);
          std::cout << "BuDDy nodenum: " << bs.nodenum
            // a node is 16 bytes, so 64 nodes is 1KB
                    << " (" << ((bs.nodenum+32)/64) << "KB)\n"
                    << "BuDDy freenodes: " << bs.freenodes
                    << " (" << std::fixed << std::setprecision(2)
                    << (bs.freenodes * 100. / bs.nodenum) << "%)\n"
                    << "BuDDy produced: " << bs.produced << '\n'
                    << "BuDDy cachesize: " << bs.cachesize
            // a cache entry is 16 bytes, but there are 6 caches.
                    << " (" << ((bs.cachesize+32)/64) << "KB * 6 = "
                    << ((bs.cachesize*6+32)/64) << "KB)\n"
                    << "BuDDy hashsize: " << bs.hashsize
            // a has entry is 4 bytes, so 256 entries is 1KB
                    << " (" << ((bs.hashsize+128)/256) << "KB)\n"
                    << "BuDDy gbcnum: " << bs.gbcnum << '\n';
        }
      else
        {
          spot::twa_graph_ptr aut = a->as_twa();
          static unsigned index = 0;
          printer.print(aut, timer, f, filename, linenum, index++,
                        nullptr, prefix, suffix);
        }
      return 0;
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

      std::string tlsf_string = read_stdout_of_command(command);
      int res = process_string(tlsf_string, filename);
      return res;
    }

  };
}

int
main(int argc, char** argv)
{
  return protected_main(argv, [&] {
      // By default we name automata using the formula.
      opt_name = "%f";

      const argp ap = { options, parse_opt, "[FORMULA...]",
                        argp_program_doc, children, nullptr, nullptr };

      if (int err = argp_parse(&ap, argc, argv, ARGP_NO_HELP, nullptr, nullptr))
        exit(err);

      check_no_formula();

      spot::bdd_dict_ptr dict = spot::make_bdd_dict();
      trans_processor processor(dict);
      if (processor.run())
        return 2;
      return 0;
  });
}
