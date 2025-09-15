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

namespace spot
{
  /// \ingroup twa_algorithms
  /// \brief build "bounded automata" from knowledge
  ///
  /// In a model checking context, if \a aut represents the negation
  /// of a property that one wants to check on some system S, and we
  /// know (by any mean) that the behaviors of S always satisfies some
  /// \a fact (expressed as an automaton, or LTL formula), we can construct
  /// a new automaton `aut2` that intersects S iff \a aut intersects S.
  ///
  /// The update_bounds_given and update_bounds_given_here functions
  /// do the first step of that automaton creation.  They simply
  /// integrate the knowledge in the form of "bounds" for the labels
  /// of the automaton.  If the labels of the automaton are chosen to
  /// be anything between those bounds, then the above "intersection"
  /// equivalence hold.  Bounds may be updated multiple times to
  /// integrate multiple knowledge about the system S.
  ///
  /// Then use bounds_simplify() to simplify the bounds and get
  /// back a standard automaton.
  ///
  /// \cite duret.25.pn
  /// @{
  SPOT_API twa_graph_ptr
  update_bounds_given_here(twa_graph_ptr& aut,
                           const_twa_graph_ptr& fact,
                           bool* changed = nullptr);
  SPOT_API twa_graph_ptr
  update_bounds_given(const_twa_graph_ptr& aut,
                      const_twa_graph_ptr& fact);
  SPOT_API twa_graph_ptr
  update_bounds_given_here(twa_graph_ptr& aut,
                           formula fact,
                           bool* changed = nullptr);
  SPOT_API twa_graph_ptr
  update_bounds_given(const_twa_graph_ptr& aut, formula fact);
  /// @}

  /// \ingroup twa_algorithms
  /// \brief Choose labels in a bounded automaton.
  ///
  /// This uses the Minato algorithm to select a label
  /// for each transition, between the bounds computed
  /// by update_bounds_given().
  ///
  /// \cite duret.25.pn
  /// @{
  SPOT_API twa_graph_ptr
  bounds_simplify_here(twa_graph_ptr& aut);
  SPOT_API twa_graph_ptr
  bounds_simplify(const_twa_graph_ptr& aut);
  /// @}

  /// \ingroup twa_algorithms
  /// \brief Attempt to make an automaton stutter-invariant given some knowledge
  ///
  /// In a model checking context, if \a aut represents the negation
  /// of a property that one wants to check on some system S, and we
  /// know (by any mean) that the behaviors of S always satisfies some
  /// \a fact (expressed as an automaton, or LTL formula), we can construct
  /// a new automaton `aut2` that intersects S iff \a aut intersects S.
  ///
  /// This attempts to build an automaton `aut2` that is
  /// stutter-invariant using all `facts` (a vector of a priori
  /// knowledge about S).
  ///
  /// The algorithm has two variant (relax and restrict) that can be
  /// selected using the \a relax argument.
  ///
  /// \cite duret.25.pn
  SPOT_API
  twa_graph_ptr stutterize_given(twa_graph_ptr& aut,
                                 std::vector<const_twa_graph_ptr>& facts,
                                 bool relax = true);
}
