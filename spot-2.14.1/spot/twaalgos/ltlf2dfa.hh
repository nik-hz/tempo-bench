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

#pragma once

#include <spot/twa/twagraph.hh>
#include <spot/misc/bddlt.hh>
#include <spot/misc/trival.hh>
#include <spot/twaalgos/backprop.hh>

namespace spot
{
  /// \defgroup mtdfa Multi-Terminal DFAs
  ///
  /// MTDFAs are a representation of transition-based finite
  /// deterministic automata labeled by Boolean formulas.  Each state
  /// is represented by a BDD encoding the Boolean formulas labeling
  /// the successor transition.  The leaves of these BDDs are not only
  /// the usual `bddfalse` and `bddtrue`, but some integer-valued
  /// terminal representing the destination state.  A terminal with
  /// integer label $2d+b$ represents destination state $d$ and uses
  /// $b\in\{0,1\}$ to indicate whether the transition is accepting
  /// (i.e., the evaluation can stop after reading the last letter).
  /// The `bddfalse` and `bddtrue` nodes are kept to represent
  /// rejecting and accepting sinks; using them helps some to shortcut
  /// some BDD operations.
  ///
  /// \cite duret.25.ciaa

  /// \ingroup mtdfa
  /// \brief statistics about an mtdfa instance
  ///
  /// This holds the result of a call to mtdfa::get_stats().
  struct SPOT_API mtdfa_stats
    {
      /// \brief number of roots
      ///
      /// This excludes true and false, that are never
      /// listed as roots.
      unsigned states;

      /// \brief number of atomic propositions
      ///
      /// This are the number of atomic proposition used in the original
      /// formula.  The proposition actually used in the automaton may be
      /// less.
      unsigned aps;

      /// \brief Number of internal nodes (or decision nodes)
      ///
      /// Only filled if mtdfa::get_stats() was passed the `nodes` option.
      unsigned nodes;

      /// \brief Number of terminal nodes.
      ///
      /// This excludes the true and false, constant nodes.
      ///
      /// Only filled if mtdfa::get_stats() was passed the `nodes` option.
      unsigned terminals;

      /// \brief Whether the true and false constants are used.
      ///
      /// Only filled if mtdfa::get_stats() was passed the `nodes` option.
      ///@{
      bool has_true;
      bool has_false;
      ///@}

      /// \brief Number of paths between a root and a leaf (terminal
      /// or constant)
      ///
      /// Only filled if mtdfa::get_stats() was passed the `edges` option.
      unsigned long long paths;

      /// \brief Number of pairs (root, leaf) for which a path exists.
      ///
      /// Only filled if mtdfa::get_stats() was passed the `edges` option.
      unsigned long long edges;
    };

  /// \ingroup mtdfa
  /// \brief a DFA represented using shared multi-terminal BDDs
  ///
  /// Such a DFA is represented by a vector a BDDs: one BDD per state.
  /// Each BDD encodes set of outgoing transitions of a state.  The
  /// of the transitions encoded naturally using BDD decision variables,
  /// and the destination state is stored as a "terminal" node.
  ///
  /// Those DFA use transition-based acceptance, and that acceptance
  /// is represented by the last bit of the value stored in the
  /// terminal.
  ///
  /// If a transition should reach state V, the terminal stores the
  /// value 2*V if the transition is rejecting, or 2*V+1 if the
  /// transition is accepting.
  ///
  /// `bddfalse` and `bddtrue` terminals are used to represent
  /// rejecting and accepting sink states.
  ///
  /// \cite duret.25.ciaa
  struct SPOT_API mtdfa: public std::enable_shared_from_this<mtdfa>
  {
    public:
    /// \brief create an empty mtdfa
    ///
    /// The \a dict is used to record how BDD variables map to atomic
    /// propositions.
    mtdfa(const bdd_dict_ptr& dict) noexcept
    : dict_(dict)
    {
    }

    ~mtdfa()
    {
      dict_->unregister_all_my_variables(this);
    }

    std::vector<bdd> states;
    std::vector<formula> names;
    /// \brief The list of atomic propositions possibly used by the automaton.
    ///
    /// This is actually the list of atomic propositions that appeared
    /// in the formulas/automata that were used to build this
    /// automaton.  The automaton itself may use fewer atomic
    /// propositions, for instance in cases some of them canceled each other.
    ///
    /// This vector is sorted by formula ID, to make it easy to merge
    /// with another sorted vector.
    std::vector<formula> aps;

    /// \brief the number of MTBDDs roots
    ///
    /// This is the size of the `states` array.  It does not account
    /// for any bddfalse or bddtrue state.
    unsigned num_roots() const
    {
      return states.size();
    }

    /// \brief The number of states in the automaton
    ///
    /// This counts the number of roots, plus one if the `bddtrue` state
    /// is reachable.  This is therefore the size that the
    /// transition-based output of `as_twa()` would have.
    unsigned num_states() const
    {
      return states.size() + bdd_has_true(states);
    }

    // This assumes that all states are reachable, so we just have to
    // check if one terminal is accepting.
    bool is_empty() const;

    /// \brief Print the `states` array of MTBDD in graphviz format.
    ///
    /// If \a index is non-negative, print only the single state
    /// specified by \a index.
    ///
    /// By default states will be named according to the formulas
    /// given in the `names` array, if available.  Set \a labels to
    /// `false` (or clear `names`) if you prefer states to by
    /// numbered.
    std::ostream& print_dot(std::ostream& os,
                            int index = -1,
                            bool labels = true) const;

    /// \brief Convert this automaton to a spot::twa_graph
    ///
    /// The twa_graph class is not meant to represent finite automata,
    /// so this will actually abuse the twa_graph class by creating a
    /// deterministic Büchi automaton in which accepting transitions
    /// should be interpreted as final transitions.  If \a state_based
    /// is set, then a DBA with state-based acceptance is created
    /// instead.
    ///
    /// The conversion can be costly, since it requires creating
    /// BDD-labeled transitions for each path between a root and a
    /// leaf of the state array.  However it can be useful to explain
    /// the MTDBA semantics.
    ///
    /// By default, the created automaton will have its states named
    /// using the LTLf formulas that label the original automaton if
    /// available.  Set \a labels to `false` if you do not want that.
    twa_graph_ptr as_twa(bool state_based = false, bool labels = true) const;

    /// \brief compute some statistics about the automaton
    ///
    /// If \a nodes and \a paths are false, this only fetches
    /// statistics that are available in constant time.
    ///
    /// If \a nodes is true, this will additionally count the number
    /// of internal nodes and leaves.  It requires scanning the BDDs
    /// for the entire array of states, so this is linear in what the
    /// number of nodes involved.
    ///
    /// If \a paths is true, this will additionally count the number
    /// of paths from roots to leaves.  This is potentially
    /// exponential in the number of atomic propositions.
    mtdfa_stats get_stats(bool nodes, bool paths) const;

    /// \brief get the bdd_dict associated to this automaton
    bdd_dict_ptr get_dict() const
    {
      return dict_;
    }

    /// \brief declare a list of controllable variables
    ///
    /// Doing so affect the way the automaton is printed in dot
    /// format, but this is also a prerequisite for interpreting
    /// the automaton as a game.
    ///
    /// This function is expected to be after you have built the
    /// automaton, in some way (causing atomic propositions to be
    /// registered).  If \a ignore_non_registered_ap is set, variable
    /// listed as output but not registered by the automaton will be
    /// dropped.  Else, an exception will be raised for those
    /// variables.  @{
    void set_controllable_variables(const std::vector<std::string>& vars,
                                    bool ignore_non_registered_ap = false);
    void set_controllable_variables(bdd vars);
    /// @}

    /// \brief Returns the conjunction of controllable variables.
    bdd get_controllable_variables() const
    {
      return controllable_variables_;
    }

    private:
    bdd_dict_ptr dict_;
    bdd controllable_variables_ = bddtrue;
    };

  typedef std::shared_ptr<mtdfa> mtdfa_ptr;
  typedef std::shared_ptr<const mtdfa> const_mtdfa_ptr;

  /// \ingroup mtdfa
  /// \brief Convert an LTLf formula into an MTDFA
  ///
  /// This converts the LTLf formula \a f into an MTDFA, one state at
  /// a time, using a recursive computation of the successors of an
  /// LTLf-labeled state.
  ///
  /// By default, the construction includes some very cheap
  /// optimizations that can be disabled with the relevant flags
  /// to study their effact:
  ///
  /// - States that have exactly the same MTBDD representation are
  ///   merged (\a fuse_same_bdds)
  ///
  /// - formulas generated for states are simplified using very cheap
  ///   rewritings (\a simplify_terms)
  ///
  /// - if, after construction of the whole automaton, it was found
  ///   that all states were rejecting, or all states were accepting,
  ///   the automaton is reduced to a rejecting or accepting sink
  ///   state (\a detect_empty_univ)
  ///
  /// States will be labeled using LTLf formulas, this is required by
  /// the construction.
  ///
  /// \cite duret.25.ciaa
  SPOT_API mtdfa_ptr
  ltlf_to_mtdfa(formula f, const bdd_dict_ptr& dict,
                bool fuse_same_bdds = true,
                bool simplify_terms = true,
                bool detect_empty_univ = true);


  enum ltlf_synthesis_backprop {
    state_refine,         ///< no backpropagation, just local refinement
    bfs_node_backprop,    ///< on-the-fly
    dfs_node_backprop,    ///< on-the-fly, DFS that stops on visited nodes
    dfs_strict_node_backprop, ///< on-the-fly, DFS that stops on visited states
  };

  /// \ingroup mtdfa
  /// \brief Solve (or start solving) LTLf synthesis
  ///
  /// This is similar to ltlf_to_mtdfa, but with the intent of solving
  /// LTLf synthesis problems.  Typically, all accepting terminals
  /// will be replaced by bddtrue, and some nodes will be simplified
  /// according to their controllability.
  ///
  /// The set of output variables should be specified with \a outvars.
  ///
  /// If \a backprop is set to `bdd_node_backprop`,
  /// `dfs_node_backprop`, or `dfs_strict_node_backprop`, then a
  /// backpropagation graph it constructed while the automaton for \a
  /// f is explored.  This may help to abort the construction earlier,
  /// and it is enough to solve the game and return a strategy.  That
  /// strategy is returned if \a realizability is set to `false` (if a
  /// strategy does not exist, a DFA that has a single bddfalse state
  /// is reaturned.  When \a realizability is `true`, then the
  /// returned MTDFA will just have a single state that is bddtrue
  /// (realizable) or bddfalse (unrealizable).
  ///
  /// When \a backprop is set to `state_refine`, each state is locally
  /// simplified according to the accepting terminals/bddtrue/bddfalse
  /// it can reach, but the game still needs to be solved by other
  /// means.
  ///
  /// If \a one_step_preprocess is set, the formula for each step is
  /// first simplified in attempt to prove realizability or
  /// unrealizability in one step.  This require translating two
  /// different Boolean formulas to BDDs and then quantifying them.
  ///
  /// See ltlf_to_mtdfa for the purpose of \a fuse_same_bdds, \a
  /// simplify_terms, \a detect_empty_univ.
  ///
  /// \cite duret.25.ciaa
  SPOT_API mtdfa_ptr
  ltlf_to_mtdfa_for_synthesis(formula f, const bdd_dict_ptr& dict,
                              const std::vector<std::string>& outvars,
                              ltlf_synthesis_backprop backprop
                              = dfs_node_backprop,
                              bool one_step_preprocess = false,
                              bool realizability = false,
                              bool fuse_same_bdds = true,
                              bool simplify_terms = true,
                              bool detect_empty_univ = true);

  /// \ingroup mtdfa
  /// \brief Convert an LTLf formula into a MTDFA, with a compositional
  /// approach.
  ///
  /// This splits the LTLf formula on the Boolean operators at the top
  /// of the formula.  Maximal subformula that have a temporal
  /// operator as root are translated using ltlf_to_mtdfa(), and the
  /// resulting automata are then minimized and then composed
  /// according to the Boolean operators above those subformulas.
  ///
  /// This approach makes it possible to minimize the intermediate
  /// automata before combining them.  (Set \a minimize to `false` to
  /// disable that.)
  ///
  /// When combining multiple automata with AND or OR, there is some
  /// flexibility in the order in which this is done.  When \a
  /// order_for_aps is `false`, a heap of automata to combine is used:
  /// the two smallest automata are combined and their result it put
  /// back in the heap.  When \a order_for_aps is `true`, the automata
  /// are also ordered by size, but the smallest automaton is combined
  /// with the next smallest automaton that share an atomic
  /// proposition.  The idea is to delay the combination of automata
  /// with disjoint APs as late as possible: indeed, the combination
  /// of those automata do not need to be minimized.
  ///
  /// The compositional approach will keep track of LTLf formulas
  /// labeling the state by default, but this is not necessary.  Set
  /// \a want_names to `false` to save tiny amount of work.
  ///
  /// Options \a fuse_same_bdds and \a simplify_terms are passed
  /// directly to ltlf_to_mtdfa(), so see this function for details.
  SPOT_API mtdfa_ptr
  ltlf_to_mtdfa_compose(formula f, const bdd_dict_ptr& dict,
                        bool minimize = true, bool order_for_aps = true,
                        bool want_names = true,
                        bool fuse_same_bdds = true,
                        bool simplify_terms = true);

  /// \ingroup mtdfa
  /// \brief Minimize a MTDFA
  ///
  /// Build a minimal DFA equivalent to \a dfa.  This implements
  /// Moore's minimization algorithm by partition refinement: the
  /// MTDFA data structure make this particularly easy to implement.
  ///
  /// Each state is assigned to an equivalence class.  Initially, all
  /// state are in the same class.  At each iteration, the original
  /// state array of MTBDD has its terminal relabeled according to the
  /// class of their state, preserving only the acceptance bit.  After
  /// this relabeling, the set of equivalence classes is adjusted to
  /// that state are in the same class iff they have the MTBDD
  /// encoding.
  ///
  /// Each iteration is linear in the number of nodes of the entire
  /// MTBDD array, and the number of iteration is at most linear in
  /// the number of states.
  SPOT_API mtdfa_ptr minimize_mtdfa(const mtdfa_ptr& dfa);

  /// \ingroup mtdfa
  /// \brief Combine two MTDFAs to intersect their languages
  SPOT_API mtdfa_ptr product(const mtdfa_ptr& dfa1, const mtdfa_ptr& dfa2);

  /// \ingroup mtdfa
  /// \brief Combine two MTDFAs to sum their languages
  SPOT_API mtdfa_ptr product_or(const mtdfa_ptr& dfa1, const mtdfa_ptr& dfa2);

  /// \ingroup mtdfa
  /// \brief Combine two MTDFAs to build the exclusive sum of their languages
  ///
  /// The results will recognize words that are their by only one of
  /// \a dfa1 or \a dfa2.  If the resulting automaton has an empty language,
  /// then the two input automata were equivalent.
  SPOT_API mtdfa_ptr product_xor(const mtdfa_ptr& dfa1, const mtdfa_ptr& dfa2);

  /// \ingroup mtdfa
  /// \brief Combine two MTDFAs to keep words that are handled
  /// similarly in both operands.
  ///
  /// The results will recognize words that are their recognized by \a
  /// dfa1 and \a dfa2, or that are rejected by both.
  SPOT_API mtdfa_ptr product_xnor(const mtdfa_ptr& dfa1, const mtdfa_ptr& dfa2);

  /// \ingroup mtdfa
  /// \brief Combine two MTDFAs to build an implication.
  ///
  /// The results will recognize words that are rejected by \a dfa1 or
  /// accepted by \a dfa2.
  SPOT_API mtdfa_ptr product_implies(const mtdfa_ptr& dfa1,
                                     const mtdfa_ptr& dfa2);

  /// \ingroup mtdfa
  /// \brief Complement an MTDFA.
  SPOT_API mtdfa_ptr complement(const mtdfa_ptr& dfa);

  /// \ingroup mtdfa
  /// \brief Convert a TWA (representing a DFA) into an MTDFA.
  SPOT_API mtdfa_ptr twadfa_to_mtdfa(const twa_graph_ptr& twa);


  /// \ingroup mtdfa
  /// \brief "Semi-internal" class used to implement spot::ltlf_to_mtdfa()
  ///
  /// It is public only to make it possible to demonstrate the inner
  /// working of the translation.  Do not rely on the interface to be
  /// stable.
  class SPOT_API ltlf_translator
    {
    public:
    ltlf_translator(const bdd_dict_ptr& dict,
                    bool simplify_terms = true);

    mtdfa_ptr ltlf_to_mtdfa(formula f, bool fuse_same_bdds,
                            bool detect_empty_univ = true,
                            const std::vector<std::string>* outvars = nullptr,
                            bool do_backprop = false,
                            bool realizability = false,
                            bool one_step_preprocess = false,
                            bool bfs = true);

    mtdfa_ptr ltlf_synthesis_with_dfs(formula f,
                                      const std::vector<std::string>*
                                      outvars = nullptr,
                                      bool realizability = false,
                                      bool ont_step_preprocess = false);

    bdd ltlf_to_mtbdd(formula f);
    std::pair<formula, bool>  leaf_to_formula(int b, int term) const;

    formula terminal_to_formula(int t) const;
    int formula_to_int(formula f);
    int formula_to_terminal(formula f, bool may_stop = false);
    bdd formula_to_terminal_bdd(formula f, bool may_stop = false);
    int formula_to_terminal_bdd_as_int(formula f, bool may_stop = false);

    bdd combine_and(bdd left, bdd right);
    bdd combine_or(bdd left, bdd right);
    bdd combine_implies(bdd left, bdd right);
    bdd combine_equiv(bdd left, bdd right);
    bdd combine_xor(bdd left, bdd right);
    bdd combine_not(bdd b);

    formula propeq_representative(formula f);

    bddExtCache* get_cache()
    {
      return &cache_;
    }

    ~ltlf_translator();
    private:
    std::unordered_map<formula, int> formula_to_var_;
    std::unordered_map<bdd, formula, bdd_hash> propositional_equiv_;

    std::unordered_map<formula, bdd> formula_to_bdd_;
    std::unordered_map<formula, int> formula_to_int_;
    std::vector<formula> int_to_formula_;
    bdd_dict_ptr dict_;
    bddExtCache cache_;
    bool simplify_terms_;
    };

  /// \ingroup mtdfa
  /// \brief Compute the winning region of the MTDFA interpreted
  /// as a game.
  ///
  /// This assumes that controllable variable have been registered
  /// with set_controllable_variables().
  ///
  /// The winning region is the set of states from which the
  /// controllable variables can force the automaton to reach an
  /// accepting state.
  ///
  /// \return a Boolean vector indicating whether a state is winning
  /// (true) or losing (false).
  SPOT_API std::vector<bool>
  mtdfa_winning_region(mtdfa_ptr dfa);

  /// \brief Compute the winning region of the MTDFA interpreted
  /// as a game.  Lazy version.
  ///
  /// This is similar to mtdfa_winning_region, but it will only
  /// compute the winning status of states that are reachable from the
  /// initial state without crossing any accepting terminal.
  ///
  /// In the trival version, the returned vector indicates whether
  /// the environment can force the game to reach false (false),
  /// the controller can force the game to reach an accepting state (true),
  /// or no player can force the game to reach its target (maybe).
  ///@{
  SPOT_API std::vector<bool>
  mtdfa_winning_region_lazy(mtdfa_ptr dfa);

  SPOT_API std::vector<trival>
  mtdfa_winning_region_lazy3(mtdfa_ptr dfa);
  ///@}

  /// \ingroup mtdfa
  /// \brief Build a generalized strategy from a set of winning states.
  ///
  /// This maps all accepting terminal to true.  If a winning_states
  /// array is given, this also maps all non-winning terminal to false.
  ///
  /// This will renumber all states.
  /// @{
  SPOT_API mtdfa_ptr mtdfa_restrict_as_game(mtdfa_ptr dfa);
  SPOT_API mtdfa_ptr
  mtdfa_restrict_as_game(mtdfa_ptr dfa,
                         const std::vector<bool>& winning_states);
  SPOT_API mtdfa_ptr
  mtdfa_restrict_as_game(mtdfa_ptr dfa,
                         const std::vector<trival>& winning_states);
  /// @}


  /// \ingroup mtdfa
  /// \brief Build a backprop_graph from \a dfa
  ///
  /// This creates a backprop_graph based in the game interpretation
  /// of \a dfa.
  ///
  /// Set \a early_stop to `false` if you want to build the entire
  /// graph.  Otherwise, this will stop as soon as the initial state is
  /// determined.
  ///
  /// Set \a preserve_names to `true` if you want to decorate the
  /// backprop_graph with a few annotations indicating which the
  /// correspondence between some states of the backprop_graph and the
  /// roots of \a dfa.
  ///
  /// \cite duret.25.ciaa
  SPOT_API backprop_graph
  mtdfa_to_backprop(mtdfa_ptr dfa, bool early_stop = true,
                    bool preserve_names = false);

  /// \ingroup mtdfa
  /// \brief Compute a strategy for an MTDFA interpreted
  /// as a game.
  ///
  /// Create a strategy, i.e, an MTDFA in which each controllable
  /// node has exactly one "bddfalse" child.  If the initial state
  /// cannot be won by the controller, the strategy returned is bddfalse.
  ///
  /// The \a backprop_node argument controls the algorithm used to
  /// solve the game.  If is `true`, a `backprop_graph` is constructed
  /// from the MTDFA, mapping each MTBDD node to a node of the graph.
  /// This allows a linear-time resolution.  If `false`, the game is
  /// solved by refining the MTDFA in-place; this use some kind of
  /// state-based back propagation that does not have linear
  /// complexity.
  SPOT_API mtdfa_ptr
  mtdfa_winning_strategy(mtdfa_ptr dfa, bool backprop_nodes);

  /// \ingroup mtdfa
  /// \brief Convert an MTDFA representing a strategy to a TwA with
  /// the "synthesis-output" property.
  ///
  /// By default the created automaton will have its states named
  /// using the LTLf formula for the original state if available.
  /// Set \a labels to `false` if you do not want that.
  SPOT_API twa_graph_ptr
  mtdfa_strategy_to_mealy(mtdfa_ptr strategy, bool labels = true);
}
