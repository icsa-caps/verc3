/*
 * Copyright (c) 2016, The University of Edinburgh
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
*/

#ifndef VERC3_CORE_MODEL_CHECKING_HH_
#define VERC3_CORE_MODEL_CHECKING_HH_

#include <algorithm>
#include <cassert>
#include <unordered_map>
#include <vector>

#include "eval.hh"
#include "ts.hh"

namespace verc3 {
namespace core {

/**
 * Breadth-First-Search based search.
 *
 * Implements straight-forward BFS, where each visited state is stored
 * (precise, but needs more memory).
 */
template <class TransitionSystemT>
class Eval_BFS : public EvalBase<TransitionSystemT> {
 public:
  using typename EvalBase<TransitionSystemT>::TransitionSystem;
  using typename EvalBase<TransitionSystemT>::State;
  using typename EvalBase<TransitionSystemT>::Trace;
  using typename EvalBase<TransitionSystemT>::ExceptionTrace;

  StateQueue<State> Evaluate(const StateQueue<State>& start_states,
                             TransitionSystem* ts) override {
    assert(!start_states.empty());
    assert(this->num_visited_states() == 0);
    assert(this->num_queued_states() == 0);

    StateQueue<State> result;
    std::unordered_map<State, const State*, typename State::Hash> parents;

    StateQueue<const State*> current_states;

    // Denote start states
    for (const auto& start_state : start_states) {
      const auto key_ptr = &parents.emplace(start_state, nullptr).first->first;
      current_states.push_back(key_ptr);
    }

    this->num_queued_states_ = current_states.size();

    while (!current_states.empty() && this->monitor(&result)) {
      const State* current_state = current_states.front();

      if (current_state->Accept()) {
        result.push_back(*current_state);
      }

      StateMap<State> next_states;

      try {
        next_states = ts->Evaluate(*current_state);
        ++this->num_visited_states_;
      } catch (const Error& error) {
        std::vector<StateHash<State>> back_trace;

        for (;;) {
          back_trace.push_back(GetHash(*current_state));

          const auto parent_state = parents[*current_state];
          if (parent_state == nullptr) {
            // Found start state.
            throw ExceptionTrace(
                error, this->MakeTraceFromHashes(start_states, back_trace, ts));
          }

          current_state = parent_state;
        }
      }

      for (auto& next_state : next_states) {
        const auto parent = parents.find(next_state.second);
        if (parent == parents.end()) {
          // invalidates next_state
          const auto key_ptr =
              &parents.emplace(std::move(next_state.second), current_state)
                   .first->first;

          current_states.emplace_back(key_ptr);
          ++this->num_queued_states_;
        }
      }

      current_states.pop_front();
      --this->num_queued_states_;
    }

    return result;
  }
};

/**
 * Breadth-First-Search based search with state hashing.
 *
 * Space efficient search based on state hashing, where the states themselves
 * are discarded after evaluation. However, due to hash collisions there is a
 * possibility of some distinct states are considered the same [1].
 *
 * [1] <a href="http://spinroot.com/spin/Doc/pstv87.pdf">
 *      Holzmann, Gerard J. "On Limits and Possibilities of Automated Protocol
 *      Analysis." PSTV. Vol. 87. 1987</a>
 */
template <class TransitionSystemT>
class Eval_BFS_Hashing : public EvalBase<TransitionSystemT> {
 public:
  using typename EvalBase<TransitionSystemT>::TransitionSystem;
  using typename EvalBase<TransitionSystemT>::State;
  using typename EvalBase<TransitionSystemT>::Trace;
  using typename EvalBase<TransitionSystemT>::ExceptionTrace;

  StateQueue<State> Evaluate(const StateQueue<State>& start_states,
                             TransitionSystem* ts) override {
    assert(!start_states.empty());
    assert(this->num_visited_states() == 0);
    assert(this->num_queued_states() == 0);

    StateQueue<State> result;
    std::unordered_map<StateHash<State>, StateHash<State>> parents;

    // Denote start states
    for (const auto& start_state : start_states) {
      parents[GetHash(start_state)] = GetHash(start_state);
    }

    auto current_states = start_states;
    this->num_queued_states_ = current_states.size();

    while (!current_states.empty() && this->monitor(&result)) {
      const auto& current_state = current_states.front();
      const auto current_state_hash = GetHash(current_state);

      if (current_state.Accept()) {
        result.push_back(current_state);
      }

      StateMap<State> next_states;

      try {
        next_states = ts->Evaluate(current_state);
        ++this->num_visited_states_;
      } catch (const Error& error) {
        std::vector<StateHash<State>> back_trace;

        for (auto current_hash = GetHash(current_state);;) {
          back_trace.push_back(current_hash);

          const auto parent_hash = parents[current_hash];
          if (parent_hash == current_hash) {
            // Found start state.
            throw ExceptionTrace(
                error, this->MakeTraceFromHashes(start_states, back_trace, ts));
          }

          current_hash = parent_hash;
        }
      }

      for (auto& next_state : next_states) {
        const auto parent = parents.find(next_state.first);
        if (parent == parents.end()) {
          parents[next_state.first] = current_state_hash;

          // invalidates next_state
          current_states.emplace_back(std::move(next_state.second));
          ++this->num_queued_states_;
        }
      }

      current_states.pop_front();
      --this->num_queued_states_;
    }

    return result;
  }
};

}  // namespace core
}  // namespace verc3

#endif /* VERC3_CORE_MODEL_CHECKING_HH_ */

/* vim: set ts=2 sts=2 sw=2 et : */
