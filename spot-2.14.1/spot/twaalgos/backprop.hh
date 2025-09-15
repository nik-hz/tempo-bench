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

#include <iosfwd>
#include <unordered_map>
#include <spot/misc/common.hh>
#include <spot/misc/trival.hh>
#include <spot/graph/adjlist.hh>

namespace spot
{

  class SPOT_API backprop_graph final
  {
    static constexpr unsigned target = (1U << (sizeof(unsigned)*8 - 4)) - 1;

      struct backprop_state
    {
      int counter;            // number of unknown successors
      bool owner:1;
      bool frozen:1;
      bool determined:1;
      bool winner:1;            // meaningful only if determined is true
      unsigned choice: sizeof(unsigned)*8 - 4;

      backprop_state(bool owner)
        : counter(0),
          owner(owner),
          frozen(false),
          determined(false),
          winner(false),
          choice(0)
      {
      }
    };
  public:
    backprop_graph(bool stop_asap = true)
      : stop_asap_(stop_asap)
    {
    }

    int new_state(bool owner)
    {
      return reverse_.new_state(owner);
    }

    void set_name(unsigned state, const std::string& s)
    {
      names_.emplace(state, s);
    }

    // return true if the status of src is now known
    bool new_edge(unsigned src, unsigned dst);

    // call once the successors of a state have all been declared to
    // see if the status of that state can be determined already
    bool freeze_state(unsigned state);

    bool is_frozen(unsigned state) const
    {
      return (*this)[state].frozen;
    }

    bool is_determined(unsigned state) const
    {
      return (*this)[state].determined;
    }

    bool winner(unsigned state) const
    {
      return (*this)[state].winner;
    }

    unsigned choice(unsigned state) const
    {
      return (*this)[state].choice;
    }

    bool set_winner(unsigned state, bool winner)
    {
      return set_winner(state, winner, target);
    }

    std::ostream& print_dot(std::ostream& os) const;

    unsigned num_edges() const
    {
      return reverse_.num_edges();
    }

  private:
    bool set_winner(unsigned state, bool winner, unsigned choice_state);

    adjlist<backprop_state> reverse_;
    bool stop_asap_;
    std::unordered_map<unsigned, std::string> names_;

    const backprop_state& operator[](unsigned state) const
    {
      return reverse_.state_data(state);
    }

    backprop_state& operator[](unsigned state)
    {
      return reverse_.state_data(state);
    }
  };


}
