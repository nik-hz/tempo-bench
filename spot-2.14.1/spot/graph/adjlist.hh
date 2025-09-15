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

#include <spot/misc/common.hh>
#include <spot/misc/_config.h>

#include <vector>
#include <iterator>
#include <cstddef>
#include <spot/graph/graph.hh>

namespace spot
{
  // This works almost like a digraph, but it does not support removal
  // of edges, does not support data on edges, prepend edges instead
  // of appending them, and only stores the destinations of edges, not
  // their source.   So this is a more compact memory representation.
  template<class State_Data>
  class SPOT_API adjlist
  {
  private:
    // Edge structure in a linked list format
    struct edge
    {
      unsigned dst;
      // index of next edge, or 0.
      unsigned next_index;
    };

    struct state_storage: public internal::boxed_label<State_Data>
    {
      unsigned first_edge = 0;

#ifndef SWIG
      template <typename... Args,
                typename = typename std::enable_if<
                  !internal::first_is_base_of<state_storage,
                                              Args...>::value>::type>
      state_storage(Args&&... args)
        noexcept(std::is_nothrow_constructible
                 <internal::boxed_label<State_Data>, Args...>::value)
        : internal::boxed_label<State_Data>{std::forward<Args>(args)...}
      {
      }
#endif
    };

    std::vector<edge> edges_;
    std::vector<state_storage> states_;

  public:
    adjlist(unsigned max_states = 10, unsigned max_trans = 0)
    {
      states_.reserve(max_states);
      if (max_trans == 0)
        max_trans = max_states * 2;
      edges_.reserve(max_trans + 1);
      // Add a dummy edge at index 0 to simplify later comparisons.
      // when next_index == 0, there is no successor.
      edges_.push_back({-1U, 0U});
    }

    template <typename... Args>
    unsigned new_state(Args&&... args)
    {
      unsigned s = states_.size();
      states_.emplace_back(std::forward<Args>(args)...);
      return s;
    }

    template <typename... Args>
    unsigned new_states(unsigned n, Args&&... args)
    {
      unsigned s = states_.size();
      states_.reserve(s + n);
      while (n--)
        states_.emplace_back(std::forward<Args>(args)...);
      return s;
    }

    typename internal::boxed_label<State_Data>::data_t&
    state_data(unsigned s)
    {
      return states_[s].data();
    }

    const typename internal::boxed_label<State_Data>::data_t&
    state_data(unsigned s) const
    {
      return states_[s].data();
    }

    void new_edge(unsigned src, unsigned dst)
    {
      unsigned pos = edges_.size();
      state_storage& ss = states_[src];
      edges_.emplace_back(edge{dst, ss.first_edge});
      ss.first_edge = pos;
    }

    // Iterator for range-based for loop support
    class successor_iterator
    {
    private:
      const adjlist* graph;
      unsigned edge_index;

    public:
      // Iterator traits
      using iterator_category = std::input_iterator_tag;
      using value_type = unsigned;
      using difference_type = std::ptrdiff_t;
      using pointer = const unsigned*;
      using reference = const unsigned&;

      successor_iterator(const adjlist* g, unsigned idx)
        : graph(g), edge_index(idx)
      {
      }

      int operator*() const
      {
        return graph->edges_[edge_index].dst;
      }

      successor_iterator& operator++() {
        edge_index = graph->edges_[edge_index].next_index;
        return *this;
      }

      successor_iterator operator++(int) {
        successor_iterator tmp = *this;
        ++(*this);
        return tmp;
      }

      friend bool operator==(const successor_iterator& iter, std::nullptr_t)
      {
        return iter.edge_index == 0;
      }

      friend bool operator==(std::nullptr_t, const successor_iterator& iter)
      {
        return iter.edge_index == 0;
      }

      friend bool operator!=(const successor_iterator& iter, std::nullptr_t)
      {
        return iter.edge_index != 0;
      }

      friend bool operator!=(std::nullptr_t, const successor_iterator& iter)
      {
        return iter.edge_index != 0;
      }
    };

    class successor_range
    {
    private:
      const adjlist* graph;
      unsigned state;

    public:
      successor_range(const adjlist* g, unsigned s)
        : graph(g), state(s)
      {
      }

      successor_iterator begin() const
      {
        unsigned first_edge = (state < graph->states_.size()) ?
          graph->states_[state].first_edge : 0;
        return successor_iterator(graph, first_edge);
      }

      std::nullptr_t end() const
      {
        return nullptr;
      }
    };

    successor_range out(unsigned state) const
    {
      return successor_range(this, state);
    }

    unsigned num_states() const
    {
      return states_.size();
    }

    unsigned num_edges() const
    {
      return edges_.size() - 1;
    }
  };
}
