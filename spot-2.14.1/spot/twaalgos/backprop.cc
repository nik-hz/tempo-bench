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
#include <deque>
#include <bddx.h>
#include <spot/twaalgos/backprop.hh>
#include <spot/misc/escape.hh>

namespace spot
{

  bool backprop_graph::new_edge(unsigned src, unsigned dst)
  {
    backprop_state& ss = (*this)[src];
    if (SPOT_UNLIKELY(ss.frozen))
      throw std::runtime_error
        ("backprop_graph: cannot add successor to frozen state");
    if (SPOT_UNLIKELY(ss.determined)) // the edge is useless
      return false;
    backprop_state& ds = (*this)[dst];
    if (!ds.determined)
      {
        // declare an edge for backward propagation
        reverse_.new_edge(dst, src);
        ss.counter += 1;
      }
    else if (ss.owner == ds.winner)
      {
        return set_winner(src, ss.owner, dst);
      }
    // ignore other edges
    return false;
  }

  bool backprop_graph::freeze_state(unsigned state)
  {
    backprop_state& ss = (*this)[state];
    ss.frozen = true;
    if (!ss.determined && ss.counter == 0)
      return set_winner(state, !ss.owner, 0);
    return false;
  }

  bool backprop_graph::set_winner(unsigned state, bool winner,
                                  unsigned choice_state)
  {
    {
      backprop_state& ss = (*this)[state];
      if (SPOT_UNLIKELY(ss.determined))
        throw std::runtime_error
          ("backprop_graph: cannot change status of determined state");
      ss.determined = true;
      ss.winner = winner;
      ss.choice = choice_state;
    }
    std::deque<unsigned> todo;
    todo.push_back(state);
    bool result = false;
    do
      {
        unsigned s = todo.front();
        todo.pop_front();

#ifndef NDEBUG
        backprop_state& bs = (*this)[state];
        assert(bs.determined && (bs.winner == winner));
#endif
        for (unsigned p: reverse_.out(s))
          {
            backprop_state& prev = (*this)[p];
            if (prev.determined)
              continue;
            bool exist_choice = prev.owner == winner;
            if (exist_choice || (--prev.counter == 0 && prev.frozen))
              {
                prev.determined = true;
                prev.winner = winner;
                if (exist_choice)
                  prev.choice = s;
                if (SPOT_UNLIKELY(p == 0))
                  {
                    if (stop_asap_)
                      return true;
                    else
                      result = true;
                  }
                todo.push_back(p);
              }
          }
      }
    while (!todo.empty());
    return result;
  }

  std::ostream& backprop_graph::print_dot(std::ostream& os) const
  {
    os << "digraph mtdfa {\n  rankdir=TB;\n";
    unsigned num_states = reverse_.num_states();
    for (unsigned state = 0; state < num_states; ++state)
      {
        const backprop_state& bs = (*this)[state];
        os << "  " << state << " [shape="
           << (bs.owner ? "diamond" : "box")
           << ", style=\"filled";
        if (!bs.owner)
          os << ",rounded";
        if (!bs.frozen)
          os << ",dashed";
        os << "\" fillcolor="
           << (!bs.determined ? "white" :
               bs.winner ? "\"#33A02C\"" : "\"#E31A1C\"");
        if (bs.choice == target)
          os << ", penwidth=3";
        os << ", label=\"";
        if (auto it = names_.find(state); it != names_.end())
          escape_str(os, it->second);
        else
          os << state;
        os << "\"];\n";
      }
    for (unsigned state = 0; state < num_states; ++state)
      {
        backprop_state ss = (*this)[state];
        unsigned ch = ss.choice;
        if (ss.determined && ss.winner == ss.owner && ss.choice != target)
          os << "  " << state << " -> " << ch << " [penwidth=2]\n";
        for (unsigned p: reverse_.out(state))
          {
            backprop_state sp = (*this)[p];
            if (!sp.determined || sp.winner != sp.owner || sp.choice != state)
              os << "  " << p << " -> " << state << ";\n";
          }
      }
    return os << "}\n";
  }

}
