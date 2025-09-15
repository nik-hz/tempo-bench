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

#include "config.h"
#include <queue>
#include <deque>
#include <unordered_map>
#include <algorithm>
#include <array>
#include <climits>
#include <spot/tl/ltlf.hh>
#include <spot/misc/bddlt.hh>
#include <spot/misc/escape.hh>
#include <spot/twaalgos/ltlf2dfa.hh>
#include <spot/twaalgos/isdet.hh>
#include <spot/tl/apcollect.hh>
#include <spot/tl/print.hh>
#include <spot/priv/robin_hood.hh>
#include <spot/misc/bitvect.hh>
#include <spot/graph/adjlist.hh>
#include <spot/twaalgos/backprop.hh>

// Some of the MTBDD operations may share the same operation cache, so
// they need an hash key to be distinguished.
constexpr int hash_key_and = 1;
constexpr int hash_key_or = 2;
constexpr int hash_key_implies = 3;
constexpr int hash_key_equiv = 4;
constexpr int hash_key_xor = 5;
constexpr int hash_key_not = 6;
constexpr int hash_key_rename = 7;
constexpr int hash_key_strat = 8;
constexpr int hash_key_strat_bool = 9;
constexpr int hash_key_finalstrat = 10;

namespace spot
{

  ////////////////////////////////////////////////////////////////////////
  //                        size estimates                              //
  ////////////////////////////////////////////////////////////////////////

  // The MTBDD operations we perform may use two types of operation
  // caches.  There are operation caches that grows proportionally to
  // the number of BDD nodes allocated so far (those cache may be
  // reset and reallocated when the garbage collection is triggered),
  // or we can use operation caches that are specifically allocated
  // for one operation.  In the later case, we need a good estimate of
  // the MTBDD that will be constructed by the operation, so create a
  // cache of similar size.
  //
  // The following functions provide such estimations.

  namespace
  {
    static int size_estimate_product(int left_states,
                                     int right_states,
                                     int sum_aps)
    {
      if (right_states > left_states)
        std::swap(left_states, right_states);
      left_states /= 4;
      ++left_states;
      int prod1 = left_states * right_states;
      if (prod1 / left_states != right_states) // overflow
        return INT_MAX / 16;
      int prod2 = prod1 * sum_aps;
      if ((sum_aps > 0) && ((prod2 / sum_aps != prod1) || // overflow
                            prod2 > (INT_MAX / 16)))
        return INT_MAX / 16;
      if (prod2 < (1 << 14))
        return 1 << 14;
      return prod2;
    }

    static int size_estimate_product(const mtdfa_ptr& left,
                                     const mtdfa_ptr& right)
    {
      // Compute the number of atomic propositions in the product.
      // The logic is similar to std::set_union except we only
      // count the number of elements in the union.
      auto lbegin = left->aps.begin();
      auto lend = left->aps.end();
      auto rbegin = right->aps.begin();
      auto rend = right->aps.end();
      int apsz = 0;
      while (lbegin != lend && rbegin != rend)
        {
          ++apsz;
          bool adv_left = *lbegin <= *rbegin;
          bool adv_right = *rbegin <= *lbegin;
          lbegin += adv_left;
          rbegin += adv_right;
        }
      // Parentheses are important here, because rend should not be
      // added to (lend - lbegin) in theory even if it's ok in
      // practice..  (Compile the STL in debug mode will catch this.)
      apsz += (lend - lbegin) + (rend - rbegin);

      return size_estimate_product(left->num_roots(),
                                   right->num_roots(),
                                   apsz);
    }

    static int size_estimate_unary(const mtdfa_ptr& aut)
    {
      int states = aut->num_roots();
      states /= 2;
      ++states;
      int num_aps = aut->aps.size();
      int prod = states * num_aps;
      if ((num_aps > 0) && ((prod / num_aps != states) || // overflow
                            prod > (INT_MAX / 16)))
        return INT_MAX / 16;
      if (prod < (1 << 14))
        return 1<<14;
      return prod;
    }
  }

  ////////////////////////////////////////////////////////////////////////
  //                       LTLf translator class                        //
  ////////////////////////////////////////////////////////////////////////

  ltlf_translator::ltlf_translator(const bdd_dict_ptr& dict,
                                   bool simplify_terms)
    : dict_(dict), simplify_terms_(simplify_terms)
  {
    bdd_extcache_init(&cache_, -4, true);

    int_to_formula_.reserve(32);
  }

  ltlf_translator::~ltlf_translator()
  {
    bdd_extcache_done(&cache_);
    dict_->unregister_all_my_variables(this);
  }

  // This implement propositional equivalence plus some very light
  // simplifications
  formula ltlf_translator::propeq_representative(formula f)
  {
    // We start we the simplifications
  again:
    switch (f.kind())
      {
      case op::And:
        {
          if (!simplify_terms_)
            break;
          // The following cheap simplifications avoid creating
          // unnecessary terminals that will eventually be found
          // to be equivalent.
          //
          // (α M β) ∧ β ≡ (α M β)
          // (α R β) ∧ β ≡ (α R β)
          // Gα ∧ α ≡ Gα
          robin_hood::unordered_set<formula> removable;
          for (const formula& sub: f)
            if (sub.is(op::M) || sub.is(op::R))
              removable.insert(sub[1]);
            else if (sub.is(op::G))
              removable.insert(sub[0]);
          if (removable.empty())
            break;
          std::vector<formula> vec;
          for (const formula& sub: f)
            if (removable.find(sub) == removable.end())
              vec.push_back(sub);
          if (vec.size() == f.size())
            break;
          f = formula::And(std::move(vec));
          goto again;
          // Rules that are not implemented but for which I have
          // seen both sides of the equality during some translation:
          //
          //  α ∧ Fα ≡ α  (generalizes to α ∧ (β [UW∨] α) ≡ α).
          //  Gα ∧ (β ∨ α) ≡ Gα
          //  Fα ∨ (β ∧ α) ≡ Fα
          //
          // All of these can be seen as some type of unit-propagation.
          // See Issue #606.
        }
      case op::Or:
        {
          if (!simplify_terms_)
            break;
          // (α U β) ∨ β ≡ (α U β)
          // (α W β) ∨ β ≡ (α W β)
          // Fα ∨ α ≡ Fα
          robin_hood::unordered_set<formula> removable;
          for (const formula& sub: f)
            if (sub.is(op::U) || sub.is(op::W))
              removable.insert(sub[1]);
            else if (sub.is(op::F))
              removable.insert(sub[0]);
          if (removable.empty())
            break;
          std::vector<formula> vec;
          for (const formula& sub: f)
            if (removable.find(sub) == removable.end())
              vec.push_back(sub);
          if (vec.size() == f.size())
            break;
          f = formula::Or(std::move(vec));
          goto again;
        }
      case op::Not:
      case op::Xor:
      case op::Implies:
      case op::Equiv:
        break;
      default:
        // abort immediately if the top-level operator is not Boolean
        return f;
      }


    auto formula_to_bddvar = [&] (formula f) -> int
    {
      if (auto it = formula_to_var_.find(f);
          it != formula_to_var_.end())
        return it->second;
      if (f.is(op::ap))
        {
          int v = dict_->register_proposition(f, this);
          formula_to_var_[f] = v;
          return v;
        }
      int v = dict_->register_anonymous_variables(1, this);
      formula_to_var_[f] = v;
      return v;
    };

    // Convert the formula to a BDD suitable for propositional
    // equivalence.  Any subformula that has a non-boolean
    // operator is replaced by atomic proposition.
    auto encode_rec = [&] (formula f, auto rec) -> bdd
    {
      switch (f.kind())
        {
        case op::tt:
          return bddtrue;
        case op::ff:
          return bddfalse;
        case op::ap:
          return bdd_ithvar(formula_to_bddvar(f));
        case op::Not:
          if (f[0].is_leaf())   // skip one application of bdd_not.
            {
              if (f[0].is_tt())
                return bddfalse;
              if (f[0].is_ff())
                return bddtrue;
              return bdd_nithvar(formula_to_bddvar(f[0]));
            }
          return bdd_not(rec(f[0], rec));
        case op::And:
          {
            bdd res = bddtrue;
            for (const formula& sub: f)
              res &= rec(sub, rec);
            return res;
          }
        case op::Or:
          {
            bdd res = bddfalse;
            for (const formula& sub: f)
              res |= rec(sub, rec);
            return res;
          }
        case op::Xor:
          {
            bdd left = rec(f[0], rec);
            return left ^ rec(f[1], rec);
          }
        case op::Implies:
          {
            bdd left = rec(f[0], rec);
            return left >> rec(f[1], rec);
          }
        case op::Equiv:
          {
            bdd left = rec(f[0], rec);
            return bdd_biimp(left, rec(f[1], rec));
          }
        default:
          return bdd_ithvar(formula_to_bddvar(f));
        }
    };

    bdd enc = encode_rec(f, encode_rec);
    if (enc == bddtrue)
      f = formula::tt();
    else if (enc == bddfalse)
      f = formula::ff();
    auto [it, _] = propositional_equiv_.emplace(enc, f);
    (void) _;
    // std::cerr << f << " ≡ " << it->second << '\n';
    return it->second;
  }

  formula ltlf_translator::terminal_to_formula(int v) const
  {
    v /= 2;
    assert((unsigned) v < int_to_formula_.size());
    return int_to_formula_[v];
  }

  std::pair<formula, bool> ltlf_translator::leaf_to_formula(int b, int v) const
  {
    if (b == 0)
      return {formula::ff(), false};
    if (b == 1)
      return {formula::tt(), true};
    return {terminal_to_formula(v), v & 1};
  }

  int ltlf_translator::formula_to_int(formula f)
  {
    if (auto it = formula_to_int_.find(f);
        it != formula_to_int_.end())
      return it->second;

    if (formula g = propeq_representative(f); g != f)
      {
        auto it = formula_to_int_.find(g);
        if (it == formula_to_int_.end())
          {
            // This can occur if propeq_representative simplifies
            // the formula.
            int v = int_to_formula_.size();
            int_to_formula_.push_back(g);
            formula_to_int_[g] = v;
            formula_to_int_[f] = v;
            return v;
          }
        int v = it->second;
        formula_to_int_[f] = v;
        return v;
      }

    int v = int_to_formula_.size();
    int_to_formula_.push_back(f);
    formula_to_int_[f] = v;
    return v;
  }

  int ltlf_translator::formula_to_terminal(formula f, bool maystop)
  {
    return formula_to_int(f) * 2 + maystop;
  }

  int ltlf_translator::formula_to_terminal_bdd_as_int(formula f,
                                                      bool maystop)
  {
    if (SPOT_UNLIKELY(f.is_ff() && !maystop))
      return 0;
    if (SPOT_UNLIKELY(f.is_tt() && maystop))
      return 1;
    int v = formula_to_int(f);
    f = int_to_formula_[v];     // The formula might have been reduced to tt/ff.
    if (SPOT_UNLIKELY(f.is_ff() && !maystop))
      return 0;
    if (SPOT_UNLIKELY(f.is_tt() && maystop))
      return 1;
    return bdd_terminal_as_int(v * 2 + maystop);
  }

  bdd ltlf_translator::formula_to_terminal_bdd(formula f, bool maystop)
  {
    return bdd_from_int(formula_to_terminal_bdd_as_int(f, maystop));
  }

  namespace
  {
    static ltlf_translator* term_combine_trans;
    static int term_combine_and(int left, int left_term,
                                int right, int right_term)
    {
      auto [lf, lb] = term_combine_trans->leaf_to_formula(left, left_term);
      auto [rf, rb] = term_combine_trans->leaf_to_formula(right, right_term);
      formula res = formula::And({lf, rf});
      return term_combine_trans->formula_to_terminal_bdd_as_int(res, lb && rb);
    }

    static int term_combine_or(int left, int left_term,
                               int right, int right_term)
    {
      auto [lf, lb] = term_combine_trans->leaf_to_formula(left, left_term);
      auto [rf, rb] = term_combine_trans->leaf_to_formula(right, right_term);
      formula res = formula::Or({lf, rf});
      return term_combine_trans->formula_to_terminal_bdd_as_int(res, lb || rb);
    }

    static int term_combine_implies(int left, int left_term,
                                    int right, int right_term)
    {
      auto [lf, lb] = term_combine_trans->leaf_to_formula(left, left_term);
      auto [rf, rb] = term_combine_trans->leaf_to_formula(right, right_term);
      formula res = formula::Implies(lf, rf);
      return term_combine_trans->formula_to_terminal_bdd_as_int(res, !lb || rb);
    }

    static int term_combine_equiv(int left, int left_term,
                                  int right, int right_term)
    {
      auto [lf, lb] = term_combine_trans->leaf_to_formula(left, left_term);
      auto [rf, rb] = term_combine_trans->leaf_to_formula(right, right_term);
      formula res = formula::Equiv(lf, rf);
      return term_combine_trans->formula_to_terminal_bdd_as_int(res, lb == rb);
    }

    static int term_combine_xor(int left, int left_term,
                                int right, int right_term)
    {
      auto [lf, lb] = term_combine_trans->leaf_to_formula(left, left_term);
      auto [rf, rb] =  term_combine_trans->leaf_to_formula(right, right_term);
      formula res = formula::Xor(lf, rf);
      return term_combine_trans->formula_to_terminal_bdd_as_int(res, lb != rb);
    }

    static int term_combine_not(int left)
    {
      formula ll = term_combine_trans->terminal_to_formula(left);
      formula res = formula::Not(ll);
      return term_combine_trans->formula_to_terminal(res, !(left & 1));
    }
  }

  bdd ltlf_translator::combine_and(bdd left, bdd right)
  {
    term_combine_trans = this;
    return bdd_mt_apply2_leaves(left, right,
                                term_combine_and, &cache_, hash_key_and,
                                bddop_and);
  }

  bdd ltlf_translator::combine_or(bdd left, bdd right)
  {
    term_combine_trans = this;
    return bdd_mt_apply2_leaves(left, right,
                                term_combine_or, &cache_, hash_key_or,
                                bddop_or);
  }

  bdd ltlf_translator::combine_implies(bdd left, bdd right)
  {
    term_combine_trans = this;
    return bdd_mt_apply2_leaves(left, right,
                                term_combine_implies, &cache_, hash_key_implies,
                                bddop_imp);
  }

  bdd ltlf_translator::combine_equiv(bdd left, bdd right)
  {
    term_combine_trans = this;
    return bdd_mt_apply2_leaves(left, right,
                                term_combine_equiv, &cache_, hash_key_equiv,
                                bddop_biimp);
  }

  bdd ltlf_translator::combine_xor(bdd left, bdd right)
  {
    term_combine_trans = this;
    return bdd_mt_apply2_leaves(left, right,
                                term_combine_xor, &cache_, hash_key_xor,
                                bddop_xor);
  }

  bdd ltlf_translator::combine_not(bdd left)
  {
    term_combine_trans = this;
    return bdd_mt_apply1(left, term_combine_not,
                         bddtrue, bddfalse,
                         &cache_, hash_key_not);
  }

  bdd ltlf_translator::ltlf_to_mtbdd(formula f)
  {
    if (auto it = formula_to_bdd_.find(f); it != formula_to_bdd_.end())
      return it->second;

    bdd res = bddfalse;
    switch (f.kind())
      {
      case op::tt:
        res = bddtrue;
        break;
      case op::ff:
        res = bddfalse;
        break;
      case op::ap:
        res = bdd_ithvar(dict_->register_proposition(f, this));
        break;
      case op::Not:
        // For all purely Boolean subformulas, we want to use the
        // regular BDD operators, so that the cache entries are long
        // lived.
        if (f.is_boolean())
          res = !ltlf_to_mtbdd(f[0]);
        else
          res = combine_not(ltlf_to_mtbdd(f[0]));
        break;
      case op::Xor:
        {
          bdd left = ltlf_to_mtbdd(f[0]);
          bdd right = ltlf_to_mtbdd(f[1]);
          if (f.is_boolean())
            res = left ^ right;
          else
            res = combine_xor(left, right);
          break;
        }
      case op::Implies:
        {
          bdd left = ltlf_to_mtbdd(f[0]);
          bdd right = ltlf_to_mtbdd(f[1]);
          if (f.is_boolean())
            res = left >> right;
          else
            res = combine_implies(left, right);
          break;
        }
      case op::Equiv:
        {
          bdd left = ltlf_to_mtbdd(f[0]);
          bdd right = ltlf_to_mtbdd(f[1]);
          if (f.is_boolean())
            res = bdd_apply(left, right, bddop_biimp);
          else
            res = combine_equiv(left, right);
          break;
        }
      case op::eword:
      case op::AndNLM:
      case op::AndRat:
      case op::Closure:
      case op::Concat:
      case op::EConcat:
      case op::EConcatMarked:
      case op::first_match:
      case op::FStar:
      case op::Fusion:
      case op::NegClosure:
      case op::NegClosureMarked:
      case op::OrRat:
      case op::Star:
      case op::UConcat:
        throw std::runtime_error("ltlf_to_mtbdd: unsupported operator");
      case op::And:
        {
          unsigned n = f.size();
          res = ltlf_to_mtbdd(f[0]);
          for (unsigned i = 1; i < n; ++i)
            res = combine_and(res, ltlf_to_mtbdd(f[i]));
          break;
        }
      case op::Or:
        {
          unsigned n = f.size();
          res = ltlf_to_mtbdd(f[0]);
          for (unsigned i = 1; i < n; ++i)
            res = combine_or(res, ltlf_to_mtbdd(f[i]));
          break;
        }
      case op::X:
        res = formula_to_terminal_bdd(f[0], true);
        break;
      case op::strong_X:
        res = formula_to_terminal_bdd(f[0]);
        break;
      case op::U:
        {
          bdd f0 = ltlf_to_mtbdd(f[0]);
          bdd f1 = ltlf_to_mtbdd(f[1]);
          bdd term = formula_to_terminal_bdd(f);
          res = combine_or(f1, combine_and(f0, term));
          break;
        }
      case op::W:
        {
          bdd f0 = ltlf_to_mtbdd(f[0]);
          bdd f1 = ltlf_to_mtbdd(f[1]);
          bdd term = formula_to_terminal_bdd(f, true);
          res = combine_or(f1, combine_and(f0, term));
          break;
        }
      case op::R:
        {
          bdd f0 = ltlf_to_mtbdd(f[0]);
          bdd f1 = ltlf_to_mtbdd(f[1]);
          bdd term = formula_to_terminal_bdd(f, true);
          res = combine_and(f1, combine_or(f0, term));
          break;
        }
      case op::M:
        {
          bdd f0 = ltlf_to_mtbdd(f[0]);
          bdd f1 = ltlf_to_mtbdd(f[1]);
          bdd term = formula_to_terminal_bdd(f);
          res = combine_and(f1, combine_or(f0, term));
          break;
        }
      case op::G:
        {
          bdd term = formula_to_terminal_bdd(f, true);
          res = combine_and(ltlf_to_mtbdd(f[0]), term);
          break;
        }
      case op::F:
        {
          bdd term = formula_to_terminal_bdd(f);
          res = combine_or(ltlf_to_mtbdd(f[0]), term);
          break;
        }
      }
    formula_to_bdd_[f] = res;
    return res;
  }

  namespace
  {
    static std::unordered_map<int, int> terminal_to_state_map;

    static int terminal_to_state(int terminal)
    {
#if NDEBUG
      int v = terminal_to_state_map[terminal / 2];
#else
      int v = terminal_to_state_map.at(terminal / 2);
#endif
      return 2 * v + (terminal & 1);
    }

    static int strategy_map_true(int* root_ptr, int term)
    {
      // replace accepting terminals by bddtrue
      if (term & 1)
        {
          *root_ptr = 1;
          return 1;
        }
      return 0;
    }

    struct backprop_bdd_encoder
    {
      backprop_graph backprop;
      robin_hood::unordered_map<int, unsigned> rootnum_to_backprop_state;
      robin_hood::unordered_map<int, unsigned> bdd_to_backprop_state;
      // only used if recompute_succ
      robin_hood::unordered_set<int> bdd_seen;

      bool root_is_determined(unsigned root_number) const
      {
        auto it = rootnum_to_backprop_state.find(root_number);
        if (it == rootnum_to_backprop_state.end())
          return false;
        return backprop.is_determined(it->second);
      }

      bool root_winner(unsigned root_number) const
      {
        auto it = rootnum_to_backprop_state.find(root_number);
        assert(it != rootnum_to_backprop_state.end());
        return backprop.winner(it->second);
      }

      // ~backprop_bdd_encoder()
      // {
      //   std::cerr << "backprop graph had size: "
      //             << backprop.new_state(false) << '\n';
      // }

      bool root_winner_set_if_unknown(unsigned root_number, bool winner)
      {
        auto it = rootnum_to_backprop_state.find(root_number);
        assert(it != rootnum_to_backprop_state.end());
        if (backprop.is_determined(it->second))
          return false;
        else
          return backprop.set_winner(it->second, winner);
      }

      // This encodes an MTDFA state into the backpropgation
      // graph (aka game arena)
      //
      // The state is specified by its root_number, and and MTBDD
      // encoding the successors.  Vertices of the game arena will be
      // created for all nodes, including terminals.  The terminal
      // corresponding to the root is created as well.
      //
      // For the purpose of debuging, a name may be passed.  It will
      // be attached to the root.
      //
      // As a side effect, the function will record the root numbers stored
      // on the terminals it reaches in new_rootnums or old_rootnums
      // depending on whether the corresponding vertex had to be created
      // in the game or if it was already existing.
      //
      // If recompute_succ is false, the encoding stops its
      // "recursion" whenever it finds a node that has already been
      // encoded into the game.  If it is true, it will continue the
      // recursion even through nodes that have already been encoded,
      // provided they correspond to underterminate vertices.  Doing
      // so allows to collect all undeterminate successors even if
      // they were already encoded.  This is necessary for our DFS
      // construction.
      template<bool recompute_succ = false>
      bool encode_state(unsigned root_number, bdd mtbdd,
                        std::string* name = nullptr,
                        std::vector<int>* new_rootnums = nullptr,
                        std::vector<int>* old_rootnums = nullptr)
      {
        if constexpr (recompute_succ)
          bdd_seen.clear();
        // hold (backprop state, low bdd, high bdd)
        std::deque<std::tuple<unsigned, int, int>> todo;

        auto rootnum_to_state = [&] (int t) -> unsigned
        {
          auto [it, is_new] = rootnum_to_backprop_state.emplace(t, 0);
          if (is_new)
            {
              // owner does not matter, because this state will have only
              // one successor.
              it->second = backprop.new_state(false);
              if (new_rootnums)
                new_rootnums->push_back(t);
            }
          else if (old_rootnums)
            old_rootnums->push_back(t);
          return it->second;
        };

        auto bdd_to_state = [&] (int b, auto self) -> unsigned
        {
          auto [it, is_new] = bdd_to_backprop_state.emplace(b, 0);
          if (!is_new)
            {
              if (!recompute_succ || b == 0 || b == 1)
                return it->second;
            }
          if (b == 0 || b == 1)
            {
              unsigned s = backprop.new_state(!b);
              it->second = s;
              backprop.set_winner(s, b);
              if (name)
                backprop.set_name(s, b ? "true" : "false");
              return s;
            }
          if constexpr (recompute_succ)
            {
              // Make sure we see each node only once per call to
              // encode_state.
              if (!bdd_seen.emplace(b).second)
                return it->second;
            }
          if (bdd_is_terminal(b))
            {
              if constexpr (recompute_succ)
                if (!is_new)
                  {
                    int term = bdd_get_terminal(b);
                    if (term & 1)
                      return it->second;
                    return rootnum_to_state(term / 2);
                  }
              int term = bdd_get_terminal(b);
              if (term & 1)
                {
                  unsigned n = self(1, self);
                  // "it" might have been invalidated by the recursive call
                  bdd_to_backprop_state[b] = n;
                  return n;
                }
              return it->second = rootnum_to_state(term / 2);
            }
          if constexpr (recompute_succ)
            if (!is_new && backprop.is_determined(it->second))
              return it->second;
          auto [owner, low, high] = bdd_mt_quantified_low_high(b);
          if constexpr (recompute_succ)
            if (!is_new)
              {
                todo.emplace_back(it->second, low, high);
                return it->second;
              }
          unsigned s = backprop.new_state(owner);
          it->second = s;
          todo.emplace_back(s, low, high);
          return s;
        };

        // create one state for the root number (if it does not exist yet)
        unsigned root_state = rootnum_to_state(root_number);
        if (name)
          backprop.set_name(root_state, *name);
        // std::cerr << "encoding term " << root_number
        //           << " on vertex " << root_state << '\n';

        // link it to the actual BDD root, as the only child
        if (backprop.new_edge(root_state,
                              bdd_to_state(mtbdd.id(), bdd_to_state)))
          return true;
        if (backprop.freeze_state(root_state))
          return true;

        // now encode all that BDD, when they reach terminal, this
        // will create "root number" nodes for those, and those can
        // later be connnected to their BDD encoding once we know it.
        while (!todo.empty())
          {
            auto [state, low, high] = todo.front();
            todo.pop_front();
            if constexpr (recompute_succ)
              if (backprop.is_frozen(state))
                {
                  assert(!backprop.is_determined(state));
                  bdd_to_state(low, bdd_to_state);
                  bdd_to_state(high, bdd_to_state);
                  continue;
                }
            // We could encode high before low if we wanted.  That
            // makes sense if we know that a state for high already
            // exists and is determined.  However, deciding this is an
            // extra hash lookup, so this is unlikely to be worth it.
            unsigned low_state = bdd_to_state(low, bdd_to_state);
            if (backprop.new_edge(state, low_state))
              return true;
            // If the previous edge determined the source state, no
            // need to process the other branch.
            if (backprop.is_determined(state))
              continue;
            unsigned high_state = bdd_to_state(high, bdd_to_state);
            if (backprop.new_edge(state, high_state))
              return true;
            if (backprop.freeze_state(state))
              return true;
          }
        return false;
      }

      int get_choice(int node)
      {
        auto it = bdd_to_backprop_state.find(node);
        if ((it == bdd_to_backprop_state.end())
            || !backprop.winner(it->second))
          return 0;
        unsigned ch = backprop.choice(it->second);
        int lowid = bdd_low(node);
        auto it2 = bdd_to_backprop_state.find(lowid);
        assert(it2 != bdd_to_backprop_state.end());
        if (it2->second == ch)
          return lowid;
        int highid = bdd_high(node);
#ifndef NDEBUG
        auto it3 = bdd_to_backprop_state.find(highid);
        assert(it3 != bdd_to_backprop_state.end());
        assert(it3->second == ch);
#endif
        return highid;
      }
    };

    static backprop_bdd_encoder* global_backprop = nullptr;

    static int strategy_choice(int bddid)
    {
      return global_backprop->get_choice(bddid);
    }

    static int strategy_map_finalize(int* root_ptr, int term)
    {
      // replace accepting terminals by bddtrue
      if (term & 1)
        {
          *root_ptr = 1;
          return 1;
        }
      term /= 2;
      // remplace losing terminals by bddfalse
      if (!global_backprop->root_winner(term))
        {
          *root_ptr = 0;
          return 0;
        }
      // keep winning terminals, just replace them by their state
      // number
#if NDEBUG
      int v = terminal_to_state_map[term];
#else
      int v = terminal_to_state_map.at(term);
#endif
      if (v != term)
        *root_ptr = bdd_terminal_as_int(2 * v);
      return 1;
    }

    static int strategy_finalize(int* root_ptr, int term)
    {
      // replace accepting terminals by bddtrue
      if (term & 1)
        {
          *root_ptr = 1;
          return 1;
        }
      // remplace losing terminals by bddfalse
      if (!global_backprop->root_winner(term / 2))
        {
          *root_ptr = 0;
          return 0;
        }
      // keep winning terminals as-is
      return 1;
    }
  }

  // This is the main translation function.  It has grown to do a bit
  // too much, as it optionally performs on-the-fly game solving.
  mtdfa_ptr
  ltlf_translator::ltlf_to_mtdfa(formula f,
                                 bool fuse_same_bdds,
                                 bool detect_empty_univ,
                                 const std::vector<std::string>* outvars,
                                 bool do_backprop,
                                 bool realizability,
                                 bool preprocess,
                                 bool bfs)
  {
    mtdfa_ptr dfa = std::make_shared<mtdfa>(dict_);
    std::unordered_map<bdd, int, bdd_hash> bdd_to_state;
    std::unordered_map<formula, int> formula_to_state;
    std::vector<bdd> states;
    std::vector<int> new_rootnums; // used only if do_backprop
    std::vector<formula> names;
    std::deque<formula> todo;
    terminal_to_state_map.clear();

    if (do_backprop && outvars == nullptr)
      throw std::runtime_error
        ("ltlf_to_mtdfa: backpropagation requires outvars");

    std::unique_ptr<realizability_simplifier_base> realsimp;
    std::unique_ptr<backprop_bdd_encoder> backprop;
    if (do_backprop)
      backprop.reset(global_backprop = new backprop_bdd_encoder());

    bdd bddoutvars = bddtrue;      // used if outvars was passed;
    // this is the number of variables we had the last time
    // we called bdd_mt_quantify_prepare().
    int varnum = 0;

    auto quantify_prepare_maybe = [&] {
      // Everytime a new BDD variable is created, the quantification
      // buffer is wiped out.  Adding variables can happen as a
      // side-effect of ltlf_to_mtbdd().  As a consequence, we have to
      // call bdd_mt_quantify_prepare() when the number of BDD
      // variables changed.
      if (int vn = bdd_varnum(); vn != varnum)
        {
          bdd_mt_quantify_prepare(bddoutvars);
          varnum = vn;
        }
    };

    auto restrict_bdd = [&](bdd& b) -> bool {
      quantify_prepare_maybe();
      return bdd_mt_apply1_synthesis(b, strategy_map_true,
                                     &cache_, hash_key_strat);
    };

    auto restrict_bdd_bool = [&](bdd& b, bool realizability) -> bool {
      quantify_prepare_maybe();
      if (!realizability)
        return bdd_mt_apply1_synthesis(b, nullptr,
                                       &cache_, hash_key_strat);
      return bdd_mt_quantify_to_bool(b, nullptr,
                                     &cache_, hash_key_strat_bool);
    };

    // Keep track of atomic propositions used in he automaton.
    // Actually, the automaton might use fewer atomic propositions
    // than what appears in the formula, but we do not pay attention
    // to that.
    {
      atomic_prop_set* a = atomic_prop_collect(f);
      dfa->aps.assign(a->begin(), a->end());

      if (outvars)
        {
          if (preprocess)
            {
              unsigned o = realizability_simplifier_base::polarity;
              realsimp.reset(new realizability_simplifier_base(*outvars,
                                                               false, o));
            }
          // We need to register the output variables already so we can
          // call bdd_mt_quantify_prepare.  Let's do it in the order in
          // which they will be discovered in the formula.
          std::unordered_set<spot::formula> outputs;
          for (const std::string& s: *outvars)
            outputs.insert(spot::formula::ap(s));
          f.traverse([&](const spot::formula& f)
          {
            if (f.is(spot::op::ap) && outputs.find(f) != outputs.end()
                && a->erase(f))
              {
                int i = dict_->register_proposition(f, dfa);
                bddoutvars &= bdd_ithvar(i);
              }
            return false;
          });
          dfa->set_controllable_variables(bddoutvars);
        }
      delete a;
    }

    // Keep track of whether we have seen an accepting or rejecting
    // state.  If we are missing one of them, we can reduce the
    // automaton to a single state.
    bool has_accepting = false;
    bool has_rejecting = false;

    todo.push_back(f);
    do
      {
        formula label;
        if (bfs)
          {
            label = todo.front();
            todo.pop_front();
          }
        else
          {
            label = todo.back();
            todo.pop_back();
          }
        int label_term = formula_to_terminal(label) / 2;

        // already processed
        if (terminal_to_state_map.find(label_term)
            != terminal_to_state_map.end())
          continue;


        bool b_done = false;
        bdd b;

        if (realsimp && !label.is_boolean())
          {
            formula g = ltlf_one_step_sat_rewrite(label);

            realizability_simplifier_base::mapping_t simpl_map;
            std::tie(g, simpl_map) = realsimp->simplify(g);

            b = ltlf_to_mtbdd(g);
            if (restrict_bdd_bool(b, realizability))
              {
                b_done = true;
                if (realizability)
                  {
                    b = bddtrue;
                  }
                else
                  {
                    assert(b != bddfalse);
                    bdd fix = bddtrue;
                    for (auto [k, k_is_input, v]: simpl_map)
                      {
                        if (k_is_input)
                          continue;
                        int i = dict_->register_proposition(k, this);
                        if (v.is_tt())
                          fix &= bdd_ithvar(i);
                        else
                          fix &= bdd_nithvar(i);
                      }
                    b &= fix;
                  }
                if (do_backprop)
                  backprop->encode_state<>(label_term, b, nullptr,
                                           &new_rootnums);
              }
            else
              {
                g = ltlf_one_step_unsat_rewrite(label);
                std::tie(g, simpl_map) = realsimp->simplify(g);
                b = ltlf_to_mtbdd(g);

                if (!restrict_bdd_bool(b, true))
                  {
                    b_done = true;
                    if (do_backprop)
                      backprop->encode_state<>(label_term, bddfalse, nullptr,
                                               &new_rootnums);
                  }

              }
          }
        if (!b_done)
          {
            b = ltlf_to_mtbdd(label);
            if (outvars)
              {
                if (realizability && label.is_boolean())
                  {
                    if (restrict_bdd_bool(b, true))
                      b = bddtrue;
                    else
                      b = bddfalse;
                  }
                else
                  restrict_bdd(b);
                if (do_backprop)
                  backprop->encode_state(label_term, b, nullptr,
                                         &new_rootnums);
              }
          }

        if (fuse_same_bdds)
          if (auto it = bdd_to_state.find(b); it != bdd_to_state.end())
            {
              formula_to_state[label] = it->second;
              terminal_to_state_map[label_term] = it->second;
              continue;
            }
        unsigned n = states.size();
        formula_to_state[label] = n;
        bdd_to_state[b] = n;
        states.push_back(b);
        names.push_back(label);
        terminal_to_state_map[label_term] = n;

        if (do_backprop)
          {
            if (SPOT_UNLIKELY(backprop->root_is_determined(0)))
              break;
            if (backprop->root_is_determined(label_term))
              continue;
            // We stored all successors in new_rootnums to
            // avoid calling leaves_of.
            for (unsigned root: new_rootnums)
              todo.push_back(int_to_formula_[root]);
            new_rootnums.clear();
            continue;
          }

        for (bdd leaf: leaves_of(b))
          {
            if (leaf == bddfalse)
              {
                has_rejecting = true;
                continue;
              }
            if (leaf == bddtrue)
              {
                has_accepting = true;
                continue;
              }
            int term = bdd_get_terminal(leaf);
            if (term & 1)
              has_accepting = true;
            else
              has_rejecting = true;
            if (terminal_to_state_map.find(term / 2)
                == terminal_to_state_map.end())
              todo.push_back(terminal_to_formula(term));
          }
      }
    while (!todo.empty());

    if (do_backprop)            // finalize backpropagation
      {
        if (realizability)
          {
            if (backprop->root_winner(0))
              {
                dfa->states.push_back(bddtrue);
                dfa->names.push_back(formula::tt());
                return dfa;
              }
            else
              {
                dfa->states.push_back(bddfalse);
                dfa->names.push_back(formula::ff());
                return dfa;
              }
          }
        unsigned sz = states.size();
        for (unsigned i = 0; i < sz; ++i)
          bdd_mt_apply1_synthesis_with_choice(states[i],
                                              strategy_choice,
                                              strategy_map_finalize,
                                              &cache_, hash_key_finalstrat);
        dfa->states = std::move(states);
        dfa->names = std::move(names);
        dict_->register_all_propositions_of(this, dfa);
        return dfa;
      }

    // If we reach this point, we are only doing translation, not game
    // solving.
    if (detect_empty_univ)
      {
        if (!has_accepting)     // return a false MTDFA.
          {
            dfa->states.push_back(bddfalse);
            dfa->names.push_back(formula::ff());
            return dfa;
          }
        if (!has_rejecting)     // return a true MTDFA.
          {
            dfa->states.push_back(bddtrue);
            dfa->names.push_back(formula::tt());
            return dfa;
          }
      }

    // Currently, state[i] contains a bdd representing outgoing
    // transitions from state i, however the terminal values represent
    // formulas.  We need to remap the terminal values to state values.
    unsigned sz = states.size();
    for (unsigned i = 0; i < sz; ++i)
      states[i] = bdd_mt_apply1(states[i], terminal_to_state,
                                bddfalse, bddtrue,
                                &cache_, hash_key_rename);

    dfa->states = std::move(states);
    dfa->names = std::move(names);
    dict_->register_all_propositions_of(this, dfa);
    return dfa;
  }

  mtdfa_ptr
  ltlf_translator::ltlf_synthesis_with_dfs(formula f,
                                           const std::vector<std::string>*
                                           outvars,
                                           bool realizability,
                                           bool preprocess)
  {
    mtdfa_ptr dfa = std::make_shared<mtdfa>(dict_);
    std::unordered_map<bdd, int, bdd_hash> bdd_to_state;
    std::unordered_map<formula, int> formula_to_state;
    std::vector<bdd> states;
    std::vector<int> new_rootnums;
    std::vector<int> old_rootnums;
    std::vector<formula> names;
    std::deque<int> todo;       // stack of MTBDD root numbers
    // An entry (state, size) in prev indicates that
    // when todo.size() == size, we have processed
    // all successors of state and should backtrack;
    std::deque<std::pair<int, unsigned>> prev;

    terminal_to_state_map.clear();

    realizability_simplifier_base
      realsimp(*outvars, false, realizability_simplifier_base::polarity);

    backprop_bdd_encoder backprop;
    global_backprop = &backprop;
    bdd bddoutvars = bddtrue;
    // this is the number of variable we had the last time
    // we called bdd_mt_quantify_prepare().
    int varnum = 0;

    auto quantify_prepare_maybe = [&] {
      // Everytime a new BDD variable is created, the quantification
      // buffer is wiped out.  Adding variables can happen as a
      // side-effect of ltlf_to_mtbdd().  As a consequence, we have to
      // call bdd_mt_quantify_prepare() when the number of BDD
      // variables changed.
      if (int vn = bdd_varnum(); vn != varnum)
        {
          bdd_mt_quantify_prepare(bddoutvars);
          varnum = vn;
        }
    };

    auto restrict_bdd = [&](bdd& b) -> bool {
      quantify_prepare_maybe();
      return bdd_mt_apply1_synthesis(b, strategy_map_true,
                                     &cache_, hash_key_strat);
    };

    auto restrict_bdd_bool = [&](bdd& b, bool realizability) -> bool {
      quantify_prepare_maybe();
      if (!realizability)
        return bdd_mt_apply1_synthesis(b, nullptr,
                                       &cache_, hash_key_strat);
      return bdd_mt_quantify_to_bool(b, nullptr,
                                     &cache_, hash_key_strat_bool);
    };

    // Keep track of atomic propositions used in he automaton.
    // Actually, the automaton might use fewer atomic propositions
    // than what appears in the formula, but we do not pay attention
    // to that.
    {
      atomic_prop_set* a = atomic_prop_collect(f);
      dfa->aps.assign(a->begin(), a->end());

      // We need to register the output variables already so we can
      // call bdd_mt_quantify_prepare.  Let's do it in the order in
      // which they will be discovered in the formula.
      std::unordered_set<spot::formula> outputs;
      for (const std::string& s: *outvars)
        outputs.insert(spot::formula::ap(s));
      f.traverse([&](const spot::formula& f)
      {
        if (f.is(spot::op::ap) && outputs.find(f) != outputs.end()
            && a->erase(f))
          {
            int i = dict_->register_proposition(f, dfa);
            bddoutvars &= bdd_ithvar(i);
          }
        return false;
      });
      dfa->set_controllable_variables(bddoutvars);
      delete a;
    }

    prev.emplace_back(0, 0);
    todo.emplace_back(formula_to_int(f));
    do
      {
        auto& [prev_state, size] = prev.back();
        // std::cerr << "prev_state=" << prev_state
        //           << " size=" << size
        //           << " todo.size=" << todo.size() << '\n';

        // If prev_state is determined, skip the exploration of its successors
        // and backtrack immediately
        if (todo.size() >= size && backprop.root_is_determined(prev_state))
          {
            while (todo.size() > size)
              todo.pop_back();
            prev.pop_back();
            continue;
          }
        if (todo.size() == size) // DFS backtrack
          {
            // All successors have been explored and that was
            // not enough to mark the prev_state as winning.
            //
            // It would be tempting to mark that state as losing, but
            // in fact it is not true that we have explored all
            // successors.  Some of those successors might actually be
            // on the path leading to this state.
            prev.pop_back();
            continue;
          }
        int label_term = todo.back();
        todo.pop_back();
        formula label = int_to_formula_[label_term];

        // std::cerr << "DFS considering term " << label_term << '\t'
        //          << label << '\n';

        // already processed
        if (terminal_to_state_map.find(label_term)
            != terminal_to_state_map.end())
          continue;

        bdd b;
        bool b_done = false;
        if (preprocess && !label.is_boolean())
          {
            formula g = ltlf_one_step_sat_rewrite(label);

            realizability_simplifier_base::mapping_t simpl_map;
            std::tie(g, simpl_map) = realsimp.simplify(g);

            b = ltlf_to_mtbdd(g);
            if (restrict_bdd_bool(b, realizability))
              {
                b_done = true;
                if (realizability)
                  {
                    b = bddtrue;
                  }
                else
                  {
                    assert(b != bddfalse);
                    bdd fix = bddtrue;
                    for (auto [k, k_is_input, v]: simpl_map)
                      {
                        if (k_is_input)
                          continue;
                        int i = dict_->register_proposition(k, this);
                        if (v.is_tt())
                          fix &= bdd_ithvar(i);
                        else
                          fix &= bdd_nithvar(i);
                      }
                    b &= fix;
                  }
                backprop.encode_state<>(label_term, b, nullptr,
                                        &new_rootnums, &old_rootnums);
              }
            else
              {
                g = ltlf_one_step_unsat_rewrite(label);
                std::tie(g, simpl_map) = realsimp.simplify(g);
                b = ltlf_to_mtbdd(g);
                if (!restrict_bdd_bool(b, true))
                  {
                    b_done = true;
                    backprop.encode_state<>(label_term, bddfalse, nullptr,
                                            &new_rootnums, &old_rootnums);
                  }
              }
          }
        if (!b_done)
          {
            b = ltlf_to_mtbdd(label);
            if (realizability && label.is_boolean())
              {
                if (restrict_bdd_bool(b, true))
                  b = bddtrue;
                else
                  b = bddfalse;
              }
            else
              restrict_bdd(b);
            backprop.encode_state<true>(label_term, b, nullptr,
                                        &new_rootnums, &old_rootnums);
          }

        unsigned n = states.size();
        formula_to_state[label] = n;
        bdd_to_state[b] = n;
        states.push_back(b);
        names.push_back(label);
        terminal_to_state_map[label_term] = n;

        if (SPOT_UNLIKELY(backprop.root_is_determined(0)))
          break;
        // If the status of this state is known, we can skip the
        // exploration of its successors.
        if (backprop.root_is_determined(label_term))
          continue;
        // Schedule all successors for processing in DFS order
        prev.emplace_back(label_term, todo.size());
        for (unsigned root: new_rootnums)
          todo.push_back(root);
        for (unsigned root: old_rootnums)
          {
            auto it = terminal_to_state_map.find(root);
            if (it == terminal_to_state_map.end())
              todo.push_back(root);
          }
        old_rootnums.clear();
        new_rootnums.clear();
      }
    while (!todo.empty());
    // finalize backpropagation
    if (realizability)
      {
        if (backprop.root_winner(0))
          {
            dfa->states.push_back(bddtrue);
            dfa->names.push_back(formula::tt());
            return dfa;
          }
        else
          {
            dfa->states.push_back(bddfalse);
            dfa->names.push_back(formula::ff());
            return dfa;
          }
      }
    unsigned sz = states.size();
    for (unsigned i = 0; i < sz; ++i)
      bdd_mt_apply1_synthesis_with_choice(states[i],
                                          strategy_choice,
                                          strategy_map_finalize,
                                          &cache_, hash_key_finalstrat);
    dfa->states = std::move(states);
    dfa->names = std::move(names);
    dict_->register_all_propositions_of(this, dfa);
    return dfa;
  }

  ////////////////////////////////////////////////////////////////////////
  //                       minimization of MTDFA                        //
  ////////////////////////////////////////////////////////////////////////

  // callback for minimize_mtdfa
  namespace
  {
    static std::vector<int> classes;
    static int num_states;
    static bool accepting_false_seen;
    static bool rejecting_true_seen;

    static int rename_class(int val)
    {
      assert((unsigned) val/2 < classes.size());
      bool accepting = val & 1;
      val = classes[val / 2];
      if (val == num_states + accepting)
        {
          if (accepting)
            accepting_false_seen = true;
          else
            rejecting_true_seen = true;
        }
      return 2 * val + accepting;
    }
  }

  mtdfa_ptr minimize_mtdfa(const mtdfa_ptr& dfa,
                           bddExtCache* cache,
                           int& iteration)
  {
    if (iteration >= (1 << 20))
      {
        // wipe the cache every 2^20 iterations.
        bdd_extcache_reset(cache);
        iteration = 0;
      }

    unsigned n = num_states = dfa->num_roots();

    // This minimization implements Moore's partition-refinement
    // algorithm using MTBDDs.  The idea is relatively simple: each
    // state of the MTDFA is assigned a class. Initially every state
    // is in the same class.  The MTBDD used to represent the states
    // are all rewritten, replacing each terminal (dst, b) by
    // (class[dst], b).  After this rewriting, states whose MTBDD are
    // different are put into different classes, and we start again.
    // We iterate the process until no more classes are created.
    //
    // The implementation is made a bit more difficult because of the
    // possible use of bddfalse, and bddtrue in the MTDFA.  In order
    // other states in the automaton that should be reduced to
    // bddfalse/bddtrue, we have to introduce fake states for
    // bddfalse/bddtrue.

    // class is a global vector assigning classes to each state
    classes.clear();
    classes.resize(n + 2, 0); // two extra classes for bddtrue/bddfalse

    // The "signature" of each state is their encoding using
    // the current set of classes.  The following vector remember
    // each unique signature in the order they were discovered.
    std::vector<bdd> signatures;
    signatures.reserve(n);
    // For each distinct signature, GROUPS retains the list of
    // states that have this signature.
    std::unordered_map<bdd, std::vector<int>, bdd_hash> groups;
    for (;;)
      {
        ++iteration;
        bdd true_term = bdd_terminal(2 * classes[n] + 1);
        bdd false_term = bdd_terminal(2 * classes[n + 1]);
        accepting_false_seen = false;
        rejecting_true_seen = false;
        for (unsigned i = 0; i < n; ++i)
          {
            bdd sig = bdd_mt_apply1(dfa->states[i], rename_class,
                                    false_term, true_term,
                                    cache, iteration);
            auto& v = groups[sig];
            if (v.empty())
              signatures.push_back(sig);
            v.push_back(i);
          }
        // Now we add the "fake" states for bddtrue amd bddfalse.
        // We do this after all other states, because we are not sure
        // if those will correspond to real states in the automaton.
        {
          auto& v = groups[true_term];
          if (v.empty())
            signatures.push_back(true_term);
          v.push_back(n);
        }
        {
          auto& v = groups[false_term];
          if (v.empty())
            signatures.push_back(false_term);
          v.push_back(n + 1);
        }

        // { // debug
        //   std::cerr << "iteration " << iteration << '\n';
        //   std::cerr << signatures.size() << " states\n";
        // }

        // Assign each state to its class number, using the order in
        // which signatures were discovered.  In this order, the
        // initial state will always have class 0.
        //
        // An exception is if the class contains the fake true/false
        // state.  In this case, we map the class back to n/n+1.
        int curclass = 0;
        bool changed = false;
        for (bdd sig: signatures)
          {
            int mapclass = curclass++;
            auto& v = groups[sig];
            unsigned vb = v.back();
            if (vb >= n)        // contains the fake true/false state
              mapclass = vb;
            for (unsigned i: v)
              if (classes[i] != mapclass)
                {
                  changed = true;
                  classes[i] = mapclass;
                }
            // { // debug
            //   std::cerr << "class " << mapclass << ':';
            //   for (unsigned i: v)
            //     std::cerr << ' ' << i;
            //   if (mapclass == (int) n)
            //     std::cerr << "  (true)";
            //   else if (mapclass == (int) n + 1)
            //     std::cerr << "  (false)";
            //   std::cerr << "\n      " << sig << '\n';
            // }
          }
        // for (unsigned i = 0; i <= n + 1; ++i)
        //    std::cerr << "classes[" << i << "]=" << classes[i] << '\n';
        if (!changed)
          break;
        groups.clear();
        signatures.clear();
      }

    // Unless we have states equivalent to false/true, the BDDs in
    // SIGNATURES are actually our new MTBDD representation.
    //
    // If we have state equivalent to true & false, we just have get
    // rid of the terms we introduced to replace bddtrue/bddfalse.  Be
    // careful that bddtrue/bddfalse only replace (tt,⊤)/(ff,⊥).  We
    // still need state of (tt,⊥) or (ff,⊤) if those appear in the
    // automaton.
    //
    // In any case, if WANT_NAMES is set we also have to keep one name
    // per class for display.
    bool want_names = dfa->names.size() == n;
    std::vector<formula> names;
    // Our automaton will SZ states, minus any bddfalse/bddtrue state.
    unsigned sz = signatures.size();
    if (want_names)
      names.reserve(sz);
    unsigned j = 0;             // next free state number
    ++iteration;
    bdd true_term = bdd_terminal(2 * classes[n] + 1);
    bdd false_term = bdd_terminal(2 * classes[n + 1]);
    bool need_remap = false;
    for (unsigned i = 0; i < sz; ++i)
      {
        bdd sig = signatures[i];
        auto& v = groups[sig];
        assert(!v.empty());
        unsigned vb = v.back();
        if (vb == n + 1)        // equivalent to ff!
          {
            if (i == 0)         // the initial state is false
              {
                assert(v.front() == 0);
                if (want_names)
                  names.push_back(formula::ff());
                signatures[0] = bddfalse;
                ++j;
                break;
              }
            if (!accepting_false_seen)
              continue;
            // since (ff,⊤) exists, give ff a state number.
            classes[n + 1] = j;
            need_remap = true;
          }
        if (vb == n)            // equivalent to tt!
          {
            if (i == 0)         // the source state is true
              {
                assert(v.front() == 0);
                if (want_names)
                  names.push_back(formula::tt());
                signatures[0] = bddtrue;
                ++j;
                break;
              }
            if (!rejecting_true_seen)
              continue;
            classes[n] = j;
            need_remap = true;
          }
        if (want_names)
          {
            // We can pick the name of any state in v to label the
            // class.  Here we simply pick the first one, but this
            // can be changed if needed (e.g. pick the shortest one
            // since it is more readable?)
            assert((unsigned) v.front() < dfa->names.size());
            names.push_back(dfa->names[v.front()]);
          }
        // replace false_term/true_term by bddfalse/bddtrue
        // note that this does not change the other terminals.
        sig = bdd_terminal_to_const(sig, false_term, true_term,
                                    cache, iteration);
        classes[i] = j;
        if (i != j)
          need_remap = true;
        signatures[j++] = sig;
      }
    if (j < sz)
      signatures.resize(j);

    // If we skipped some class equivalent to bddtrue/bddfalse, we
    // have to the remaining class to fill the holes.
    if (need_remap)
      {
        ++iteration;
        for (bdd& sig: signatures)
          sig = bdd_mt_apply1(sig, rename_class, bddfalse, bddtrue,
                              cache, iteration);
      }

    mtdfa_ptr res = std::make_shared<mtdfa>(dfa->get_dict());
    // If the automaton hasn't been reduced to true/false or has some
    // controllable variables, assume it still uses all atomic
    // propositions.
    bdd controllable = dfa->get_controllable_variables();
    if ((signatures[0] != bddfalse && signatures[0] != bddtrue)
        || (controllable != bddtrue))
      {
        res->get_dict()->register_all_propositions_of(dfa, res);
        res->aps = dfa->aps;
      }
    res->set_controllable_variables(controllable);
    std::swap(res->names, names);
    std::swap(res->states, signatures);
    return res;
  }

  mtdfa_ptr minimize_mtdfa(const mtdfa_ptr& dfa)
  {
    bddExtCache cache;
    bdd_extcache_init(&cache, size_estimate_unary(dfa), false);
    int iteration = 0;
    mtdfa_ptr res = minimize_mtdfa(dfa, &cache, iteration);
    bdd_extcache_done(&cache);
    return res;
  }

  //////////////////////////////////////////////////////////////////////
  //                  Boolean operations on MTDFAs                    //
  //////////////////////////////////////////////////////////////////////

  namespace
  {
    typedef std::pair<unsigned, unsigned> product_state;

    struct product_state_hash
    {
      size_t
      operator()(product_state s) const noexcept
      {
        return wang32_hash(s.first ^ wang32_hash(s.second));
      }
    };

    inline std::pair<bdd, formula>
    bdd_and_formula_from_state(unsigned s, const mtdfa_ptr& dfa)
    {
      if (s == -2U)
        return {bddfalse, formula::ff()};
      if (s == -1U)
        return {bddtrue, formula::tt()};
      if (s >= dfa->names.size())
        return {dfa->states[s], nullptr};
      return {dfa->states[s], dfa->names[s]};
    }

    struct product_data
    {
      // Cache the BDD node representing the terminals associated to a
      // pair of states.  We may have up to two terminals par state,
      // to distinguish between accepting states (2*value+1) or
      // rejecting state (2*value).  However, while we know we need at
      // least one of terminal, we may not always need the second one.
      // So in the interest of reducing the calls to BuDDy, we store
      // the 1-complement of value until in the other field until we
      // find we actually need that terminal.
      //
      // The array can therefore hold either
      //    [bdd_terminal(value*2), ~value]
      // or [~value, bdd_terminal(value*2+1)]
      // or [bdd_terminai(value*2), bdd_terminal(value*2+1)]
      //
      // The distinction between the three cases can be made with
      // the sign bit of the array element.
      std::unordered_map<product_state, std::array<int, 2>,
                         product_state_hash> pair_to_terminal_map;
      mtdfa_ptr left;
      mtdfa_ptr right;
      std::queue<product_state> todo;

      std::pair<unsigned, bool> leaf_to_state(int b, int v) const
      {
        if (b == 0)
          return {-2U, false};
        if (b == 1)
          return {-1U, true};
        return {v / 2, v & 1};
      }

      int pair_to_terminal(unsigned left,
                           unsigned right,
                           bool may_stop = false)
      {
        if (auto it = pair_to_terminal_map.find({left, right});
            it != pair_to_terminal_map.end())
          {
            int& id = it->second[may_stop];
            if (id < 0)
              id = bdd_terminal_as_int(2 * ~id + may_stop);
            return id;
          }

        unsigned v = pair_to_terminal_map.size();
        std::array<int, 2> entry;
        int id = bdd_terminal_as_int(2 * v + may_stop);
        entry[may_stop] = id;
        entry[!may_stop] = ~v;

        product_state ps{left, right};
        pair_to_terminal_map.emplace(ps, entry);
        todo.emplace(ps);

        return id;
      }

      int pair_to_terminal_bdd(unsigned left,
                               unsigned right,
                               bool may_stop = false)
      {
        if (SPOT_UNLIKELY(left == -2U && right == -2U && !may_stop))
          return 0;
        else if (SPOT_UNLIKELY(left == -1U && right == -1U && may_stop))
          return 1;
        else
          return pair_to_terminal(left, right, may_stop);
      }
    } the_product_data;

    static int leaf_combine_and(int left, int left_term,
                                int right, int right_term)
    {
      if (left == 0 || right == 0)
        return 0;
      auto [ls, lb] = the_product_data.leaf_to_state(left, left_term);
      auto [rs, rb] = the_product_data.leaf_to_state(right, right_term);
      return the_product_data.pair_to_terminal_bdd(ls, rs, lb & rb);
    }

    static int leaf_combine_or(int left, int left_term,
                               int right, int right_term)
    {
      if (left == 1 || right == 1)
        return 1;
      auto [ls, lb] = the_product_data.leaf_to_state(left, left_term);
      auto [rs, rb] = the_product_data.leaf_to_state(right, right_term);
      return the_product_data.pair_to_terminal_bdd(ls, rs, lb | rb);
    }

    static int leaf_combine_implies(int left, int left_term,
                                    int right, int right_term)
    {
      if (left == 0 || right == 1)
        return 1;
      auto [ls, lb] = the_product_data.leaf_to_state(left, left_term);
      auto [rs, rb] = the_product_data.leaf_to_state(right, right_term);
      return the_product_data.pair_to_terminal_bdd(ls, rs, !lb | rb);
    }

    static int leaf_combine_equiv(int left, int left_term,
                                  int right, int right_term)
    {
      if (SPOT_UNLIKELY(left == 0 || left == 1))
        {
          if (left == right)
            return 1;
          if ((left ^ right) == 1)
            return 0;
        }
      auto [ls, lb] = the_product_data.leaf_to_state(left, left_term);
      auto [rs, rb] = the_product_data.leaf_to_state(right, right_term);
      return the_product_data.pair_to_terminal_bdd(ls, rs, lb == rb);
    }

    static int leaf_combine_xor(int left, int left_term,
                                int right, int right_term)
    {
      if (SPOT_UNLIKELY(left == 0 || left == 1))
        {
          if (left == right)
            return 0;
          if ((left ^ right) == 1)
            return 1;
        }
      auto [ls, lb] = the_product_data.leaf_to_state(left, left_term);
      auto [rs, rb] = the_product_data.leaf_to_state(right, right_term);
      return the_product_data.pair_to_terminal_bdd(ls, rs, lb != rb);
    }

    static mtdfa_ptr
    product_mtdfa_aux(const mtdfa_ptr& dfa1,
                      const mtdfa_ptr& dfa2, op o,
                      bddExtCache* cache, int hash_key)
    {
      if (dfa1->get_dict() != dfa2->get_dict())
        throw std::runtime_error
          ("product_mtdfa_and: DFAs should share their dictionaries");

      int (*combine)(int, int, int, int);
      int applyop_shortcut = -1;
      switch (o)
        {
        case op::And:
          combine = leaf_combine_and;
          applyop_shortcut = bddop_and_zero;
          break;
        case op::Or:
          combine = leaf_combine_or;
          applyop_shortcut = bddop_or_one;
          break;
        case op::Implies:
          combine = leaf_combine_implies;
          applyop_shortcut = bddop_imp_one;
          break;
        case op::Equiv:
          combine = leaf_combine_equiv;
          applyop_shortcut = -1;
          break;
        case op::Xor:
          combine = leaf_combine_xor;
          applyop_shortcut = -1;
          break;
        default:
          throw std::runtime_error("product_mtdfa_aux: unsupported operator");
        }

      the_product_data.left = dfa1;
      the_product_data.right = dfa2;

      bdd_dict_ptr dict = dfa1->get_dict();
      mtdfa_ptr res = std::make_shared<mtdfa>(dict);
      dict->register_all_propositions_of(dfa1, res);
      dict->register_all_propositions_of(dfa2, res);

      std::queue<product_state>& todo = the_product_data.todo;
      // this will initialize TODO with the initial state of the product
      (void) the_product_data.pair_to_terminal(0, 0);
      while (!todo.empty())
        {
          product_state s = todo.front();
          todo.pop();

          auto [left, left_f] = bdd_and_formula_from_state(s.first, dfa1);
          auto [right, right_f] = bdd_and_formula_from_state(s.second, dfa2);
          bdd b = bdd_mt_apply2_leaves(left, right, combine, cache, hash_key,
                                       applyop_shortcut);
          res->states.push_back(b);

          if (left_f && right_f)
            switch (o)
              {
              case op::And:
                res->names.push_back(formula::And({left_f, right_f}));
                break;
              case op::Or:
                res->names.push_back(formula::Or({left_f, right_f}));
                break;
              case op::Implies:
                res->names.push_back(formula::Implies(left_f, right_f));
                break;
              case op::Equiv:
                res->names.push_back(formula::Equiv(left_f, right_f));
                break;
              case op::Xor:
                res->names.push_back(formula::Xor(left_f, right_f));
                break;
              default:
                SPOT_UNREACHABLE();
              }
        }

      // combine the sorted list of atomic propositions from DFA1 and DFA2
      // keeping the result sorted
      res->aps.reserve(dfa1->aps.size() + dfa2->aps.size());
      std::set_union(dfa1->aps.begin(), dfa1->aps.end(),
                     dfa2->aps.begin(), dfa2->aps.end(),
                     std::back_inserter(res->aps));

      the_product_data.left = nullptr;
      the_product_data.right = nullptr;
      the_product_data.pair_to_terminal_map.clear();
      return res;
    }

    static int
    complement_term(int v)
    {
      return v ^ 1;
    }

    static mtdfa_ptr
    complement_aux(const mtdfa_ptr& dfa, bddExtCache* cache, int hash_key)
    {
      unsigned n = dfa->states.size();
      unsigned ns = dfa->names.size();

      bdd_dict_ptr dict = dfa->get_dict();
      mtdfa_ptr res = std::make_shared<mtdfa>(dict);
      dict->register_all_propositions_of(dfa, res);
      res->names.reserve(n);
      res->states.reserve(ns);
      res->aps = dfa->aps;

      for (unsigned i = 0; i < n; ++i)
        res->states.push_back(bdd_mt_apply1(dfa->states[i], complement_term,
                                            bddtrue, bddfalse, cache,
                                            hash_key));

      for (unsigned i = 0; i < ns; ++i)
        res->names.push_back(formula::Not(dfa->names[i]));
      return res;
    }
  }

  mtdfa_ptr product(const mtdfa_ptr& dfa1, const mtdfa_ptr& dfa2)
  {
    bddExtCache cache;
    bdd_extcache_init(&cache, size_estimate_product(dfa1, dfa2), true);
    mtdfa_ptr res = product_mtdfa_aux(dfa1, dfa2, op::And, &cache, 0);
    bdd_extcache_done(&cache);
    return res;
  }

  mtdfa_ptr product_or(const mtdfa_ptr& dfa1, const mtdfa_ptr& dfa2)
  {
    bddExtCache cache;
    bdd_extcache_init(&cache, size_estimate_product(dfa1, dfa2), true);
    mtdfa_ptr res = product_mtdfa_aux(dfa1, dfa2, op::Or, &cache, 0);
    bdd_extcache_done(&cache);
    return res;
  }

  mtdfa_ptr product_xnor(const mtdfa_ptr& dfa1, const mtdfa_ptr& dfa2)
  {
    bddExtCache cache;
    bdd_extcache_init(&cache, size_estimate_product(dfa1, dfa2), true);
    mtdfa_ptr res = product_mtdfa_aux(dfa1, dfa2, op::Equiv, &cache, 0);
    bdd_extcache_done(&cache);
    return res;
  }

  mtdfa_ptr product_xor(const mtdfa_ptr& dfa1, const mtdfa_ptr& dfa2)
  {
    bddExtCache cache;
    bdd_extcache_init(&cache, size_estimate_product(dfa1, dfa2), true);
    mtdfa_ptr res = product_mtdfa_aux(dfa1, dfa2, op::Xor, &cache, 0);
    bdd_extcache_done(&cache);
    return res;
  }

  mtdfa_ptr product_implies(const mtdfa_ptr& dfa1, const mtdfa_ptr& dfa2)
  {
    bddExtCache cache;
    bdd_extcache_init(&cache, size_estimate_product(dfa1, dfa2), true);
    mtdfa_ptr res = product_mtdfa_aux(dfa1, dfa2, op::Implies, &cache, 0);
    bdd_extcache_done(&cache);
    return res;
  }

  mtdfa_ptr complement(const mtdfa_ptr& dfa)
  {
    bddExtCache cache;
    bdd_extcache_init(&cache, 0, true);
    mtdfa_ptr res = complement_aux(dfa, &cache, 0);
    bdd_extcache_done(&cache);
    return res;
  }


  struct compose_data
  {
    bdd_dict_ptr dict;
    bddExtCache mincache;
    int minimize_iteration;
    bddExtCache opcache;
    int opcache_iteration;
    bool simplify_terms;
    bool fuse_same_bdds;
    bool want_minimize;
    bool order_for_aps;
    bool want_names;

    compose_data(bdd_dict_ptr dict, bool simplify_terms, bool fuse_same,
                 bool want_minimize, bool order_for_aps, bool want_names)
      : dict(dict),
        minimize_iteration(0),
        opcache_iteration(0),
        simplify_terms(simplify_terms),
        fuse_same_bdds(fuse_same),
        want_minimize(want_minimize),
        order_for_aps(order_for_aps),
        want_names(want_names)
    {
      bdd_extcache_init(&mincache, 0, false);
      bdd_extcache_init(&opcache, 0, false);
    }

    ~compose_data()
    {
      bdd_extcache_done(&mincache);
      bdd_extcache_done(&opcache);
    }

    mtdfa_ptr trans(formula left)
    {
      return ltlf_to_mtdfa(left, dict, fuse_same_bdds, simplify_terms);
    }

    mtdfa_ptr product_aux(mtdfa_ptr left, mtdfa_ptr right, op o)
    {
      bdd_extcache_reserve(&opcache, size_estimate_product(left, right));
      return product_mtdfa_aux(left, right, o, &opcache, opcache_iteration++);
    }

    mtdfa_ptr minimize(mtdfa_ptr dfa)
    {
      if (!want_minimize)
        return dfa;
      bdd_extcache_reserve(&mincache, size_estimate_unary(dfa));
      return minimize_mtdfa(dfa, &mincache, minimize_iteration);
    }
  };

  static mtdfa_ptr
  ltlf_to_mtdfa_compose(compose_data& data, formula f)
  {
    auto rec = [&](formula f)
    {
      return ltlf_to_mtdfa_compose(data, f);
    };

    auto byminrootcount = [&](const mtdfa_ptr& left,
                              const mtdfa_ptr& right)
    {
      return left->num_roots() > right->num_roots();
    };

    mtdfa_ptr dfa;
    if (f.is_boolean())
      return data.trans(f);
    switch (op o = f.kind())
      {
      case op::tt:
      case op::ff:
      case op::ap:
        SPOT_UNREACHABLE();
      case op::Not:
        {
          mtdfa_ptr sub = rec(f[0]);
          bdd_extcache_reserve(&data.opcache, size_estimate_unary(sub));
          return complement_aux(sub, &data.opcache, data.opcache_iteration++);
        }
      case op::And:
      case op::Or:
        if (f.size() == 2)
          goto no_order_needed;
        if (!data.order_for_aps)
          {
            std::vector<mtdfa_ptr> dfas;
            dfas.reserve(f.size());
            for (const formula& sub: f)
              dfas.push_back(rec(sub));
            // Build the product of all DFAs by increasing size.
            std::make_heap(dfas.begin(), dfas.end(), byminrootcount);
            // bddCacheStat cs;
            // bdd_cachestats(&cs);
            // std::cerr << "opHit " << cs.opHit
            //           << " opMiss " << cs.opMiss
            //           << " cachesize " << data.opcache.tablesize << '\n';
            while (dfas.size() > 1)
              {
                std::pop_heap(dfas.begin(), dfas.end(), byminrootcount);
                mtdfa_ptr left = dfas.back();
                dfas.pop_back();
                std::pop_heap(dfas.begin(), dfas.end(), byminrootcount);
                mtdfa_ptr right = dfas.back();
                dfas.pop_back();
                mtdfa_ptr prod = data.product_aux(left, right, o);
                bool wantmin =
                  left->aps.size() + right->aps.size() != prod->aps.size();
                dfas.push_back(wantmin ? data.minimize(prod) : prod);
                // std::cerr << "op: " << (int)o
                //           << " left: " << left->num_roots()
                //           << " (" << left->aps.size()
                //           << ") right: " << right->num_roots()
                //           << " (" << right->aps.size()
                //           << ") prod: " << prod->num_roots()
                //           << " (" << prod->aps.size()
                //           << ") min: " << dfas.back()->num_roots()
                //           << (wantmin ? "" : " (skipped)") << '\n';
                std::push_heap(dfas.begin(), dfas.end(), byminrootcount);
                // bdd_cachestats(&cs);
                // std::cerr << "opHit " << cs.opHit
                //           << " opMiss " << cs.opMiss
                //           << " cachesize " << data.opcache.tablesize << '\n';
              }
            return dfas[0];
          }
        else
          {
            auto byminrootcountp =
              [&](const std::pair<mtdfa_ptr, bitvect*>& left,
                  const std::pair<mtdfa_ptr, bitvect*>& right)
              {
                return left.first->num_roots() < right.first->num_roots();
              };
            auto* apset = atomic_prop_collect(f);
            std::vector<formula> aps(apset->begin(), apset->end());
            delete apset;
            unsigned apsz = aps.size();
            std::vector<std::pair<mtdfa_ptr, bitvect*>> dfas_and_aps;
            dfas_and_aps.reserve(f.size());
            for (const formula& sub: f)
              {
                bitvect* apsvec = make_bitvect(apsz);
                mtdfa_ptr dfasub = rec(sub);
                auto& apsub = dfasub->aps;
                auto apsubit = apsub.begin();
                for (unsigned i = 0; i < apsz; ++i)
                  {
                    if (apsubit == apsub.end())
                      break;
                    if (*apsubit == aps[i])
                      {
                        apsvec->set(i);
                        ++apsubit;
                      }
                  }
                // std::cerr << "sub " << sub << "\t\t"
                //           << *apsvec << '\n';
                dfas_and_aps.emplace_back(dfasub, apsvec);
              }
            std::sort(dfas_and_aps.begin(), dfas_and_aps.end(),
                      byminrootcountp);
            std::vector<mtdfa_ptr> independent_dfas; // no AP in common
            while (dfas_and_aps.size() > 1)
              {
                auto [dfa_left, aps_left] = dfas_and_aps.front();
                // scan the rest of dfas_and_aps for the first pair
                // that share some atomic proposition with aps_left.
                auto it = dfas_and_aps.begin() + 1;
                for (; it != dfas_and_aps.end(); ++it)
                  if (aps_left->intersects(*it->second))
                    break;
                if (it == dfas_and_aps.end())
                  {
                    independent_dfas.push_back(dfa_left);
                    dfas_and_aps.erase(dfas_and_aps.begin());
                    continue;
                  }
                mtdfa_ptr dfa_right = it->first;
                bitvect* aps_right = it->second;
                mtdfa_ptr prod = data.product_aux(dfa_left, dfa_right, o);
                mtdfa_ptr min = data.minimize(prod);
                *aps_left |= *aps_right;
                delete aps_right;
                // update dfa_and_aps_size.  We have to remove the
                // first element and the one pointed by it.
                auto dst = dfas_and_aps.begin();
                dst = std::copy(dst + 1, it, dst);
                std::copy(it + 1, dfas_and_aps.end(), dst);
                dfas_and_aps.resize(dfas_and_aps.size() - 2);
                // Find the place where to insert the new pair.
                auto p = std::make_pair(min, aps_left);
                auto lb = std::lower_bound(dfas_and_aps.begin(),
                                           dfas_and_aps.end(),
                                           p, byminrootcountp);
                dfas_and_aps.insert(lb, p);
                // std::cerr << "op: " << (int)o
                //           << " left: " << dfa_left->num_roots()
                //           << " (" << dfa_left->aps.size()
                //           << ") right: " << dfa_right->num_roots()
                //           << " (" << dfa_right->aps.size()
                //           << ") prod: " << prod->num_roots()
                //           << " (" << prod->aps.size()
                //           << ") min: " << min->num_roots() << '\n';
              }
            delete dfas_and_aps[0].second;
            if (independent_dfas.empty())
              return dfas_and_aps[0].first;
            independent_dfas.push_back(dfas_and_aps[0].first);
            std::make_heap(independent_dfas.begin(), independent_dfas.end(),
                           byminrootcount);
            while (independent_dfas.size() > 1)
              {
                std::pop_heap(independent_dfas.begin(), independent_dfas.end(),
                              byminrootcount);
                mtdfa_ptr left = independent_dfas.back();
                independent_dfas.pop_back();
                std::pop_heap(independent_dfas.begin(), independent_dfas.end(),
                              byminrootcount);
                mtdfa_ptr right = independent_dfas.back();
                independent_dfas.pop_back();
                mtdfa_ptr prod = data.product_aux(left, right, o);
                // std::cerr << "op: " << (int)o
                //           << " left: " << left->num_roots()
                //           << " (" << left->aps.size()
                //           << ") right: " << right->num_roots()
                //           << " (" << right->aps.size()
                //           << ") prod: " << prod->num_roots()
                //           << " (" << prod->aps.size()
                //           << ") no minimization needed\n";
                independent_dfas.push_back(prod);
                std::push_heap(independent_dfas.begin(), independent_dfas.end(),
                               byminrootcount);
              }
            return independent_dfas.front();
          }
      case op::Xor:
      case op::Implies:
      case op::Equiv:
        no_order_needed:
        {
            mtdfa_ptr left = rec(f[0]);
            mtdfa_ptr right = rec(f[1]);
            mtdfa_ptr prod = data.product_aux(left, right, o);
            if (left->aps.size() + right->aps.size() == prod->aps.size())
              return prod;
            return data.minimize(prod);
          }
      case op::U:
      case op::R:
      case op::W:
      case op::M:
      case op::G:
      case op::F:
      case op::X:
      case op::strong_X:
        {
          mtdfa_ptr dfa  = data.trans(f);
          if (!data.want_names)
            dfa->names.clear();
          return data.minimize(dfa);
        }
      case op::eword:
      case op::AndNLM:
      case op::AndRat:
      case op::Closure:
      case op::Concat:
      case op::EConcat:
      case op::EConcatMarked:
      case op::first_match:
      case op::FStar:
      case op::Fusion:
      case op::NegClosure:
      case op::NegClosureMarked:
      case op::OrRat:
      case op::Star:
      case op::UConcat:
        throw std::runtime_error("ltlf_to_mtdfa: unsupported operator");
      }
    SPOT_UNREACHABLE();
    return nullptr;
  }

  ////////////////////////////////////////////////////////////////////////
  //                 various LTLf translation interafaces               //
  ////////////////////////////////////////////////////////////////////////

  mtdfa_ptr ltlf_to_mtdfa(formula f, const bdd_dict_ptr& dict,
                          bool fuse_same_bdds, bool simplify_terms,
                          bool detect_empty_univ)
  {
    ltlf_translator trans(dict, simplify_terms);
    return trans.ltlf_to_mtdfa(f, fuse_same_bdds, detect_empty_univ);
  }

  mtdfa_ptr ltlf_to_mtdfa_for_synthesis(formula f, const bdd_dict_ptr& dict,
                                        const std::vector<std::string>& outvars,
                                        ltlf_synthesis_backprop backprop,
                                        bool preprocess,
                                        bool realizability,
                                        bool fuse_same_bdds,
                                        bool simplify_terms,
                                        bool detect_empty_univ)
  {
    ltlf_translator trans(dict, simplify_terms);
    switch (backprop)
        {
        case bfs_node_backprop:
          return trans.ltlf_to_mtdfa(f, fuse_same_bdds, detect_empty_univ,
                                     &outvars, true, realizability,
                                     preprocess, false);
        case dfs_node_backprop:
          return trans.ltlf_to_mtdfa(f, fuse_same_bdds, detect_empty_univ,
                                     &outvars, true, realizability,
                                     preprocess, true);
        case state_refine:
          return trans.ltlf_to_mtdfa(f, fuse_same_bdds, detect_empty_univ,
                                     &outvars, false, realizability,
                                     preprocess);
        case dfs_strict_node_backprop:
          return trans.ltlf_synthesis_with_dfs(f, &outvars, realizability,
                                               preprocess);
        }
    SPOT_UNREACHABLE();
    return nullptr;
  }

  mtdfa_ptr ltlf_to_mtdfa_compose(formula f, const bdd_dict_ptr& dict,
                                  bool want_minimize, bool order_for_aps,
                                  bool want_names, bool fuse_same_bdds,
                                  bool simplify_terms)
  {
    compose_data data(dict, simplify_terms, fuse_same_bdds,
                      want_minimize, order_for_aps, want_names);
    return ltlf_to_mtdfa_compose(data, f);
  }


  ////////////////////////////////////////////////////////////////////////
  //                         MTDFA methods                              //
  ////////////////////////////////////////////////////////////////////////

  namespace
  {
    static bool leaf_is_accepting(int v)
    {
      if (v == 0)
        return false;
      if (v == 1)
        return true;
      return bdd_get_terminal(v) & 1;
    }
  }

  bool mtdfa::is_empty() const
  {
    return !bdd_find_leaf(states, leaf_is_accepting);
  }

  std::ostream& mtdfa::print_dot(std::ostream& os,
                                 int state, bool labels) const
  {
    std::ostringstream edges;
    std::unordered_set<int> controllable;
    {
      bdd b = get_controllable_variables();
      while (b != bddtrue)
        {
          controllable.insert(bdd_var(b));
          b = bdd_high(b);
        }
    }

    os << "digraph mtdfa {\n  rankdir=TB;\n  node [shape=circle];\n";

    int statemin = 0;
    int statemax = states.size();
    int ns = names.size();
    if (state >= 0 && state < statemax)
      {
        statemin = state;
        statemax = state + 1;
      }
    else
      {
        os << "  { rank = source; I [label=\"\", style=invis, width=0]; }\n";
        edges << "  I -> S0 [tooltip=\"initial state\"]\n";
      }

    os << "  { rank = same;\n";
    for (int i = statemin; i < statemax; ++i)
      {
        os << "    S" << i << (" [shape=box, style=\"filled,rounded\", "
                               "fillcolor=\"#e9f4fb\", label=\"");
        if (labels && i < ns)
          escape_str(os, str_psl(names[i]));
        else
          os << i;
        os <<  "\", tooltip=\"";
        if (labels || i >= ns)
          os << '[' << i << ']';
        else
          os << str_psl(names[i]);
        os << "\"];\n";
      }

    for (int i = statemin; i < statemax; ++i)
      edges << "  S" << i << " -> B" << states[i].id()
            << " [tooltip=\"[" << i << "]\"];\n";

    // This is a heap of BDD nodes, with smallest level at the top.
    std::vector<bdd> nodes;
    robin_hood::unordered_set<int> seen;

    nodes.reserve(states.size());
    for (int i = statemin; i < statemax; ++i)
      if (bdd b = states[i]; seen.insert(b.id()).second)
        nodes.push_back(b);

    auto bylvl = [&] (bdd a, bdd b) {
      return bdd_level(a) > bdd_level(b);
    };
    std::make_heap(nodes.begin(), nodes.end(), bylvl);

    int oldvar = -1;

    while (!nodes.empty())
      {
        std::pop_heap(nodes.begin(), nodes.end(), bylvl);
        bdd n = nodes.back();
        nodes.pop_back();
        if (n.id() <= 1)
          {
            if (oldvar != -2)
              os << "  }\n  { rank = sink;\n";
            os << "    B" << n.id()
               << (" [shape=square, style=filled, fillcolor=\"#ffe6cc\", "
                   "label=\"")
               << n.id()
               << "\", tooltip=\"bdd(" << n.id() << ")\" ";
            if (n.id() == 1)
              os << ", peripheries=2";
            os << "];\n";
            oldvar = -2;
            continue;
          }
        if (bdd_is_terminal(n))
          {
            if (oldvar != -2)
              os << "  }\n  { rank = sink;\n";
            os << "    B" << n.id()
               << (" [shape=box, style=\"filled,rounded\", "
                   "fillcolor=\"#ffe5f1\", label=\"");
            int t = bdd_get_terminal(n);
            bool acc = t & 1;
            int th = t / 2;
            if (labels && th < ns)
              {
                escape_str(os, str_psl(names[th]));
              }
            else
              {
                os << th;
              }
            os << "\", tooltip=\"";
            if (!labels && th < ns)
              escape_str(os, str_psl(names[th])) << '\n';
            os << "bdd(" << n.id()
               << ")=term(" << t << ")=[" << th << "]\"";
            if (acc)
              os << ", peripheries=2";
            os << "];\n";
            oldvar = -2;
            continue;
          }
        int var = bdd_var(n);
        if (var != oldvar)
          {
            os << "  }\n  { rank = same;\n";
            oldvar = var;
          }
        std::string label;

        if ((unsigned) var < dict_->bdd_map.size()
            && dict_->bdd_map[bdd_var(n)].type == bdd_dict::var)
          label = escape_str(str_psl(dict_->bdd_map[var].f));
        else
          label = "var" + std::to_string(var);

        bool outputnode = (!controllable.empty()
                           && controllable.find(var) != controllable.end());
        const char* shape = outputnode ? "diamond" : "circle";

        os << "    B" << n.id()
           << " [shape=" << shape
           << ", style=filled, fillcolor=\"#ffffff\", label=\"" << label
           << "\", tooltip=\"bdd(" << n.id() << ")\"];\n";

        bdd low = bdd_low(n);
        bdd high = bdd_high(n);
        if (seen.insert(low.id()).second)
          {
            nodes.push_back(low);
            std::push_heap(nodes.begin(), nodes.end(), bylvl);
          }
        if (seen.insert(high.id()).second)
          {
            nodes.push_back(high);
            std::push_heap(nodes.begin(), nodes.end(), bylvl);
          }
        const char* colorlow = "";
        const char* colorhigh = "";
        if (outputnode)
          {
            if (low == bddfalse)
              colorlow = ",color=LightGray";
            else if (high == bddfalse)
              colorhigh = ",color=LightGray";
          }
        edges << "  B" << n.id() << " -> B" << low.id()
              << " [style=dotted, tooltip=\"" << label
              << "=0\"" << colorlow << "];\n  B" << n.id()
              << " -> B" << high.id() << " [style=filled, tooltip=\""
              << label << "=1\"" << colorhigh << "];\n";
      }

    os << "  }\n";
    os << edges.str();
    os << "}\n";
    return os;
  }


  // convert the MTBDD DFA representation into a DFA.
  twa_graph_ptr mtdfa::as_twa(bool state_based, bool labels) const
  {
    twa_graph_ptr res = make_twa_graph(dict_);
    res->set_buchi();
    dict_->register_all_propositions_of(this, res);
    res->register_aps_from_dict();
    res->prop_state_acc(state_based);
    res->prop_universal(true);

    unsigned n = states.size();
    assert(n > 0);

    std::vector<std::string>* names = nullptr;
    if (labels && this->names.size() == this->states.size())
      {
        names = new std::vector<std::string>;
        names->reserve(n);
        res->set_named_prop("state-names", names);
        if (!state_based)
          for (unsigned i = 0; i < n; ++i)
            names->push_back(str_psl(this->names[i]));
      }

    if (!state_based)
      {
        int true_state = -1;
        res->new_states(n);
        for (unsigned i = 0; i < n; ++i)
          for (auto [b, t]: paths_mt_of(states[i]))
            if (t != bddtrue)
              {
                int v = bdd_get_terminal(t);
                assert((unsigned) v / 2 < n);
                res->new_acc_edge(i, v / 2, b, v & 1);
              }
            else
              {
                if (true_state == -1)
                  {
                    true_state = res->new_state();
                    res->new_acc_edge(true_state, true_state, bddtrue, true);
                    if (names)
                      names->push_back("1");
                  }
                res->new_acc_edge(i, true_state, b, true);
              }
        res->merge_edges();
      }
    else                        // state-based
      {
        robin_hood::unordered_map<int, unsigned> bdd_to_state_map;
        std::vector<bdd> states;
        states.reserve(n);
        bdd init_state = bdd_terminal(0);
        states.push_back(init_state);
        bdd_to_state_map[init_state.id()] = res->new_state();
        // List of dead states that should be accepting. We
        // expect at most one in practice, but more could occur
        // if the translation is change.
        std::vector<int> dead_acc;

        // states.size() will increase in this loop
        for (unsigned i = 0; i < states.size(); ++i)
          {
            bdd src = states[i];
            if (src == bddtrue)
              {
                res->new_acc_edge(i, i, bddtrue, true);
                if (names)
                  names->push_back("1");
                continue;
              }
            int term = bdd_get_terminal(src);
            bool acc = term & 1;
            term /= 2;
            if (names)
              names->push_back(str_psl(this->names[term]));
            bool has_edge = false;
            for (auto [b, t]: paths_mt_of(this->states[term]))
              {
                auto j = bdd_to_state_map.emplace(t.id(), 0);
                if (j.second)
                  {
                    j.first->second = res->new_state();
                    states.push_back(t);
                  }
                res->new_acc_edge(i, j.first->second, b, acc);
                has_edge = true;
              }
            if (acc && !has_edge)
              dead_acc.push_back(i);
          }
        res->merge_edges();
        // only add bddfalse self-loop after merge_edges.
        for (unsigned i: dead_acc)
          res->new_acc_edge(i, i, bddfalse, true);
      }
    return res;
  }




  // Convert a TWA (representing a DFA) into an MTDFA.
  mtdfa_ptr twadfa_to_mtdfa(const twa_graph_ptr& twa)
  {
    if (!is_deterministic(twa))
      throw std::runtime_error("twadfa_to_mtdfa: input is not deterministic");
    bdd_dict_ptr dict = twa->get_dict();
    mtdfa_ptr dfa = std::make_shared<mtdfa>(dict);
    dict->register_all_propositions_of(&twa, dfa);
    unsigned n = twa->num_states();
    unsigned init = twa->get_init_state_number();

    // twa's state i should be named remap[i] in dfa.  The remaping is
    // needed because the dfa only accept 0 as initial state, and we
    // do not want to represent sink states.
    std::vector<unsigned> remap;
    remap.reserve(n);
    unsigned next = 1;
    for (unsigned i = 0; i < n; ++i)
      {
        if (i == init)
          {
            remap.push_back(0);
            continue;
          }
        // Is it a sink?
        bool sink = false;
        for (auto& e: twa->out(i))
          if (e.dst == i && e.acc && e.cond == bddtrue)
            {
              sink = true;
              break;
            }
        if (sink)
          {
            remap.push_back(-1U);
            continue;
          }
        remap.push_back(next++);
      }

    dfa->states.resize(next);

    bool sbacc = twa->prop_state_acc().is_true();
    for (unsigned i = 0; i < n; ++i)
      {
        unsigned state = remap[i];
        if (state == -1U)     // sink
          continue;
        bdd b = bddfalse;
        for (auto& e: twa->out(i))
          {
            unsigned dst = remap[e.dst];
            if (dst == -1U)   // sink
              b |= e.cond;
            else if (sbacc)
              b |= e.cond & bdd_terminal(2 * dst +
                                         twa->state_is_accepting(e.dst));
            else
              b |= e.cond & bdd_terminal(2 * dst + !!e.acc);
          }
          dfa->states[state] = b;
      }
    return dfa;
  }

  mtdfa_stats mtdfa::get_stats(bool nodes, bool paths) const
  {
    mtdfa_stats res;
    res.states = states.size();
    res.aps = aps.size();
    if (nodes)
      {
        int terms;
        res.nodes = bdd_anodecount(states, terms, res.has_false, res.has_true);
        res.terminals = terms;
      }
    if (paths)
      {
        res.edges = 0;
        res.paths = 0;
        robin_hood::unordered_set<int> terms;
        for (bdd b: states)
          {
            terms.clear();
            for (auto t: silent_paths_mt_of(b))
              {
                ++res.paths;
                terms.insert(t.id());
              }
            res.edges += terms.size();
          }
      }
    return res;
  }

  namespace
  {
    static bdd
    ap_to_bdd(mtdfa_ptr dfa, const std::vector<std::string>& controllable,
              bool ignore_non_registered_ap)
    {
      bdd_dict_ptr dict = dfa->get_dict();
      // build the conjunction of all controllable variables
      bdd controllable_bdd = bddtrue;
      for (const std::string& s: controllable)
        {
          int v = dict->has_registered_proposition(formula::ap(s), dfa);
          if (v < 0)
            {
              if (ignore_non_registered_ap)
                continue;
              throw std::runtime_error
                ("atomic proposition " + s + " is not registered by automaton");
            }
          controllable_bdd &= bdd_ithvar(v);
        }
      return controllable_bdd;
    }
  }

  void
  mtdfa::set_controllable_variables(bdd vars)
  {
    controllable_variables_ = vars;
  }

  void
  mtdfa::set_controllable_variables(const std::vector<std::string>& vars,
                                    bool ignore_non_registered_ap)
  {
    set_controllable_variables(ap_to_bdd(shared_from_this(), vars,
                                         ignore_non_registered_ap));
  }


  ////////////////////////////////////////////////////////////////////////
  //                   offline game solving algorithms                  //
  ////////////////////////////////////////////////////////////////////////

  namespace
  {
    // Build the reverse graph of the subautomaton reachable from the
    // initial state of 0 without traversing any accepting terminal.
    //
    // For each path root->leaf in DFA where leaf is not bddfalse, and not
    // a terminal with value 0, this create the following edges in the
    // returned digraph:
    //    (terminal value / 2) -> root    if terminal is even (non accepting)
    //    0 -> root         if the leaf is bddtrue or an odd terminal value
    static adjlist<void>
    build_reverse_of_reachable_graph(mtdfa_ptr dfa)
    {
      unsigned n = dfa->num_roots();
      adjlist<void> reverse(n, n);
      reverse.new_states(n);

      std::queue<int> todo;
      std::vector<bool> seen(n, false); // added to todo
      std::vector<int> seen_local(n, -1); // seen as predecessor of src
      todo.push(0);
      seen[0] = true;
      while (!todo.empty())
        {
          int src = todo.front();
          todo.pop();
          bool has_acc = false;   // already seen an accepting terminal
          for (auto t: silent_paths_mt_of(dfa->states[src]))
            {
              if (t == bddfalse)
                continue;
              if (t == bddtrue)
                {
                  if (!has_acc)
                    {
                      reverse.new_edge(0, src);
                      has_acc = true;
                    }
                  continue;
                }
              int dst = bdd_get_terminal(t);
              if (dst & 1)
                {
                  if (!has_acc)
                    {
                      reverse.new_edge(0, src);
                      has_acc = true;
                    }
                  continue;
                }
              dst /= 2;
              // we don't record predecessors from 0, as they are not
              // needed for backward propagation.
              if (dst == 0)
                continue;
              if (seen_local[dst] == src)
                continue;
              seen_local[dst] = src;
              reverse.new_edge(dst, src);
              if (!seen[dst])
                {
                  todo.push(dst);
                  seen[dst] = true;
                }
            }
        }
      return reverse;
    }

    static const std::vector<bool>* global_is_winning;
    static const std::vector<trival>* global_is_winning3;
    static int is_winning_terminal(int v)
    {
      int dst = v / 2;
      assert((v >= 0) && (global_is_winning->size() > (unsigned) dst));
      return (v & 1) || (*global_is_winning)[dst];
    }

    static int is_winning_terminal3(int v)
    {
      int dst = v / 2;
      assert((v >= 0) && (global_is_winning3->size() > (unsigned) dst));
      if (v & 1)
        return 3;
      trival w = (*global_is_winning3)[dst];
      if (w.is_true())
        return 3;
      if (w.is_false())
        return 0;
      return 2;
    }
  }

  std::vector<bool>
  mtdfa_winning_region(mtdfa_ptr dfa)
  {
    bddExtCache cache;
    bdd_extcache_init(&cache, size_estimate_unary(dfa), false);
    int iteration = 0;

    bdd controllable = dfa->get_controllable_variables();

    unsigned nroots = dfa->num_roots();
    std::vector<bool> winning(nroots, false);
    global_is_winning = &winning;

    bdd_mt_quantify_prepare(controllable);

    bool has_changed;
    do
      {
        has_changed = false;
        for (unsigned i = 0; i < nroots; ++i)
          {
            if (winning[i])
              continue;
            bdd b = dfa->states[i];
            if (bdd_mt_quantify_to_bool(b, is_winning_terminal,
                                        &cache, iteration))
              {
                has_changed = true;
                // By modifying winning, we modify the behavior of
                // is_winning_terminal.  That should normally call for
                // an invalidation of the cache (or equivalently, an
                // increment of the iteration number), but it is
                // actually OK if the cache uses previous values, as
                // if winning was constant during one iteration.  The
                // new values are sure to be used on next iteration.
                winning[i] = true;
              }
          }
        ++iteration;
      }
    while (has_changed);

    bdd_extcache_done(&cache);
    return winning;
  }

  namespace
  {
    template <typename T>
    std::vector<T>
    mtdfa_winning_region_lazy_do(mtdfa_ptr dfa)
    {
      bddExtCache cache;
      bdd_extcache_init(&cache, size_estimate_unary(dfa), false);

      bdd controllable = dfa->get_controllable_variables();

      adjlist<void> rev = build_reverse_of_reachable_graph(dfa);

      unsigned nroots = dfa->num_roots();
      // winning is initialized to the default value of T, which is
      // false for bool, and "maybe" for trival.
      std::vector<T> winning(nroots);
      std::vector<int> seen(nroots, -1); // last iteration seen
      if constexpr (std::is_same<T, bool>::value)
        global_is_winning = &winning;
      else
        global_is_winning3 = &winning;

      // The upcoming calls to bdd_mt_quantify_to_bool depend on this
      // setup.
      bdd_mt_quantify_prepare(controllable);

      std::deque<unsigned> todo;
      // By convention, states that can reach an accepting terminal are
      // listed as predecessors of 0.  (Since the predecessors of 0
      // would never be needed otherwise.)
      for (unsigned p: rev.out(0))
        todo.push_back(p);

      std::deque<unsigned> changed;

      for (int iteration = 0; !todo.empty(); ++iteration)
        {
          do
            {
              unsigned i = todo.front();
              todo.pop_front();

              if constexpr (std::is_same<T, bool>::value)
                {
                  assert(!winning[i]);
                  if (bdd_mt_quantify_to_bool(dfa->states[i],
                                              is_winning_terminal,
                                              &cache, iteration))
                    {
                      // By modifying winning, we modify the behavior of
                      // is_winning_terminal_lazy.  That should normally
                      // call for an invalidation of the cache (or
                      // equivalently, an increment of the iteration
                      // number), but it is actually OK if the cache
                      // uses previous values, as if winning was
                      // constant during one iteration.  The new values
                      // are sure to be used on next iteration.
                      winning[i] = true;
                      // if the initial state is winning, we can stop
                      if (i == 0)
                        goto done;
                      changed.push_back(i);
                    }
                }
              else // trival version
                {
                  assert(winning[i].is_maybe());
                  if (int res = bdd_mt_quantify_to_trival(dfa->states[i],
                                                          is_winning_terminal3,
                                                          &cache, 0, iteration);
                      res != 2)
                    {
                      winning[i] = trival(res != 0);
                      if (i == 0)
                        goto done;
                      changed.push_back(i);
                    }
                }
            }
          while (!todo.empty());
          // Schedule unknown predecessors for next iteration.
          for (unsigned i: changed)
            for (unsigned p: rev.out(i))
              if constexpr (std::is_same<T, bool>::value)
                {
                  if (!winning[p] && seen[p] != iteration)
                    {
                      seen[p] = iteration;
                      todo.push_front(p);
                    }
                }
              else
                {
                  if (winning[p].is_maybe() && seen[p] != iteration)
                    {
                      seen[p] = iteration;
                      todo.push_front(p);
                    }
                }
          changed.clear();
        }
    done:
      bdd_extcache_done(&cache);
      return winning;
    }
  }

  std::vector<bool>
  mtdfa_winning_region_lazy(mtdfa_ptr dfa)
  {
    return mtdfa_winning_region_lazy_do<bool>(dfa);
  }

  std::vector<trival>
  mtdfa_winning_region_lazy3(mtdfa_ptr dfa)
  {
    return mtdfa_winning_region_lazy_do<trival>(dfa);
  }

  namespace
  {
    static std::unordered_map<int, int>* global_term_map;
    static std::queue<int>* global_todo;

    static int map_restrict_as_game(int root, int term)
    {
      if (root == 0 || root == 1)
        return root;
      if (term & 1)
        return 1;
      int dst = term / 2;
      if (global_is_winning && !(*global_is_winning)[dst])
        return 0;

      int new_term = global_term_map->size() * 2;
      auto [it, b] = global_term_map->emplace(term, new_term);
      if (b)
        global_todo->push(dst);
      else
        new_term = it->second;
      if (term == new_term)
        return root;
      return bdd_terminal_as_int(new_term);
    }

    static int map_restrict_as_game3(int root, int term)
    {
      if (root == 0 || root == 1)
        return root;
      if (term & 1)
        return 1;
      int dst = term / 2;
      if (global_is_winning && !(*global_is_winning3)[dst].is_true())
        return 0;

      int new_term = global_term_map->size() * 2;
      auto [it, b] = global_term_map->emplace(term, new_term);
      if (b)
        global_todo->push(dst);
      else
        new_term = it->second;
      if (term == new_term)
        return root;
      return bdd_terminal_as_int(new_term);
    }

    template <typename T>
    mtdfa_ptr
    mtdfa_restrict_as_game_aux(mtdfa_ptr dfa,
                               const std::vector<T>* winning_states)
    {
      bddExtCache cache;
      bdd_extcache_init(&cache, size_estimate_unary(dfa), false);

      bdd_dict_ptr dict = dfa->get_dict();
      mtdfa_ptr res = std::make_shared<mtdfa>(dict);
      dict->register_all_propositions_of(dfa, res);
      res->set_controllable_variables(dfa->get_controllable_variables());

      bool keep_names = dfa->names.size() == dfa->states.size();

      if constexpr (std::is_same<T, bool>::value)
        global_is_winning = winning_states;
      else
        global_is_winning3 = winning_states;

      std::unordered_map<int, int> term_map;
      global_term_map = &term_map;
      term_map.emplace(0, 0);

      std::queue<int> todo;
      global_todo = &todo;
      todo.push(0);
      do
        {
          int state = todo.front();
          todo.pop();
          bdd b = dfa->states[state];
          b = bdd_mt_apply1_leaves(b,
                                   std::is_same<T, bool>::value ?
                                   map_restrict_as_game : map_restrict_as_game3,
                                   &cache, 0);
          res->states.push_back(b);
          if (keep_names)
            res->names.push_back(dfa->names[state]);
        }
      while (!todo.empty());
      bdd_extcache_done(&cache);
      return res;
    }
  }

  mtdfa_ptr
  mtdfa_restrict_as_game(mtdfa_ptr dfa)
  {
    return mtdfa_restrict_as_game_aux<bool>(dfa, nullptr);
  }

  mtdfa_ptr
  mtdfa_restrict_as_game(mtdfa_ptr dfa,
                         const std::vector<bool>& winning_states)
  {
    return mtdfa_restrict_as_game_aux<bool>(dfa, &winning_states);
  }

  mtdfa_ptr
  mtdfa_restrict_as_game(mtdfa_ptr dfa,
                         const std::vector<trival>& winning_states)
  {
    return mtdfa_restrict_as_game_aux<trival>(dfa, &winning_states);
  }


  namespace
  {
    static int strategy_term_map(int* root_ptr, int term)
    {
      // replace accepting terminals by bddtrue
      if (term & 1)
        {
          *root_ptr = 1;
          return 1;
        }
      term /= 2;
      return (*global_is_winning)[term];
    }

    static mtdfa_ptr
    mtdfa_winning_strategy_by_refinement(mtdfa_ptr dfa)
    {
      bddExtCache cache;
      bdd_extcache_init(&cache, size_estimate_unary(dfa), false);

      bdd controllable = dfa->get_controllable_variables();

      adjlist<void> rev = build_reverse_of_reachable_graph(dfa);

      bdd_dict_ptr dict = dfa->get_dict();
      mtdfa_ptr res = std::make_shared<mtdfa>(dict);
      dict->register_all_propositions_of(dfa, res);
      res->states = dfa->states;
      res->names = dfa->names;
      res->set_controllable_variables(dfa->get_controllable_variables());

      unsigned nroots = res->states.size();

      std::vector<bool> winning(nroots, false);
      global_is_winning = &winning;

      std::vector<int> seen(nroots, -1); // last iteration seen

      bdd_mt_quantify_prepare(controllable);

      std::deque<unsigned> todo;
      // states that can reach an accepting terminal are listed as
      // predecessors of 0 in the reverse graph.
      for (unsigned p: rev.out(0))
        todo.push_front(p);
      std::deque<unsigned> changed;

      for (int iteration = 0; !todo.empty(); ++iteration)
        {
          do
            {
              int i = todo.front();
              todo.pop_front();

              // State i may have been aded to visit_next before knowing
              // it was winning.
              if (winning[i])
                continue;
              if (bdd_mt_apply1_synthesis(res->states[i],
                                          strategy_term_map,
                                          &cache, iteration))
                {
                  // By modifying winning, we modify the behavior of
                  // is_winning_terminal.  That should normally call for
                  // an invalidation of the cache (or equivalently, an
                  // increment of the iteration number), but it is
                  // actually OK if the cache uses previous values, as
                  // if winning was constant during one iteration.  The
                  // new values are sure to be used on next iteration.
                  winning[i] = true;
                  // if the initial state is winning, we can stop
                  if (i == 0)
                    goto done;
                  changed.push_back(i);
                }
            }
          while (!todo.empty());
          // Schedule non-winning predecessors for next iteration.
          for (unsigned i: changed)
            for (unsigned p: rev.out(i))
              if (!winning[p] && seen[p] != iteration)
                {
                  seen[p] = iteration;
                  todo.push_front(p);
                }
          changed.clear();
        }
    done:
      for (unsigned i = 0; i < nroots; ++i)
        if (!winning[i])
          res->states[i] = bddfalse;

      bdd_extcache_done(&cache);
      return res;
    }

    static mtdfa_ptr
    mtdfa_winning_strategy_by_backprop(mtdfa_ptr dfa)
    {
      bdd_dict_ptr dict = dfa->get_dict();
      mtdfa_ptr res = std::make_shared<mtdfa>(dict);
      backprop_bdd_encoder enc;
      unsigned ns = dfa->num_roots();
      bdd outputs = dfa->get_controllable_variables();
      bdd_mt_quantify_prepare(outputs);
      for (unsigned i = 0; i < ns; ++i)
        if (enc.encode_state(i, dfa->states[i]))
          break;
      if (!enc.backprop.winner(0))
        {
          res->states.push_back(bddfalse);
          res->names.push_back(formula::ff());
          return res;
        }
      global_backprop = &enc;

      bddExtCache cache;
      bdd_extcache_init(&cache, size_estimate_unary(dfa), false);

      res->states = dfa->states;
      res->names = dfa->names;
      for (unsigned i = 0; i < ns; ++i)
        bdd_mt_apply1_synthesis_with_choice(res->states[i],
                                            strategy_choice,
                                            strategy_finalize,
                                            &cache, hash_key_finalstrat);
      dict->register_all_propositions_of(dfa, res);
      res->set_controllable_variables(outputs);

      bdd_extcache_done(&cache);
      return res;
    }
  }

  mtdfa_ptr
  mtdfa_winning_strategy(mtdfa_ptr dfa, bool backprop)
  {
    if (backprop)
      return mtdfa_winning_strategy_by_backprop(dfa);
    return mtdfa_winning_strategy_by_refinement(dfa);
  }

  twa_graph_ptr
  mtdfa_strategy_to_mealy(mtdfa_ptr strategy,
                          bool labels)
  {
    bdd_dict_ptr dict = strategy->get_dict();
    twa_graph_ptr res = make_twa_graph(dict);
    dict->register_all_propositions_of(strategy, res);
    res->register_aps_from_dict();
    res->prop_universal(true);

    unsigned n = strategy->num_roots();
    assert(n > 0);

    bdd outputs = strategy->get_controllable_variables();
    res->set_named_prop<bdd>("synthesis-outputs", new bdd(outputs));

    std::vector<std::string>* names = nullptr;
    if (labels && strategy->names.size() == strategy->states.size())
      {
        names = new std::vector<std::string>;
        names->reserve(n);
        res->set_named_prop("state-names", names);
      }

    robin_hood::unordered_map<int, unsigned> bdd_to_state_map;
    std::vector<bdd> states;
    states.reserve(n);

    auto map_state = [&](int state_index) {
      bdd succs = bddtrue;
      if (state_index >= 0)
        succs = strategy->states[state_index];
      auto [it, b] = bdd_to_state_map.emplace(succs.id(), 0);
      if (!b)
        return it->second;
      unsigned res_index = res->new_state();
      assert(res_index == states.size());
      it->second = res_index;
      states.push_back(succs);
      if (names)
        {
          if (state_index >= 0)
            names->push_back(str_psl(strategy->names[state_index]));
          else
            names->push_back("1");
        }
      return res_index;
    };

    map_state(0);
    // states.size() will increase in this loop
    for (unsigned i = 0; i < states.size(); ++i)
      {
        bdd succs = states[i];
        if (succs == bddfalse)
          continue;
        if (succs == bddtrue)
          {
            res->new_edge(i, i, bddtrue);
            continue;
          }
        bdd previous_output_label = bddfalse;
        unsigned previous_dst = -1U;
        unsigned previous_edge = 0;
        for (auto [b, t]: paths_mt_of(succs))
          {
            int dst = -1;
            if (t != bddtrue)
              {
                int term = bdd_get_terminal(t);
                if ((term & 1) == 0)
                  dst = term / 2;
              }
            unsigned dst_idx = map_state(dst);
            bdd output_label = bdd_existcomp(b, outputs);
            if (previous_dst == dst_idx
                && previous_output_label == output_label)
              {
                res->edge_storage(previous_edge).cond |= b;
                continue;
              }
            previous_edge = res->new_edge(i, dst_idx, b);
            previous_dst = dst_idx;
            previous_output_label = output_label;
          }
      }
    return res;
  }

  backprop_graph mtdfa_to_backprop(mtdfa_ptr dfa, bool early_stop,
                                   bool preserve_names)
  {
    backprop_bdd_encoder enc;
    unsigned ns = dfa->num_roots();
    std::string name;
    bdd_mt_quantify_prepare(dfa->get_controllable_variables());
    for (unsigned i = 0; i < ns; ++i)
      {
        std::string* nameptr = nullptr;
        if (preserve_names)
          {
            if (dfa->names.size() > i)
              name = str_psl(dfa->names[i]);
            else
              name = "state " + std::to_string(i);
            nameptr = &name;
          }
        bool res = enc.encode_state(i, dfa->states[i], nameptr);
        if (res && early_stop)
          break;
      }
    return enc.backprop;
  }
}
