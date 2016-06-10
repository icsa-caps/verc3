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

#ifndef VERC3_CORE_EVAL_HH_
#define VERC3_CORE_EVAL_HH_

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ts.hh"

namespace verc3 {
namespace core {

template <class TransitionSystemT>
class EvalBase {
 public:
  using TransitionSystem = TransitionSystemT;
  using State = typename TransitionSystemT::State;
  using Trace = std::vector<std::pair<State, std::string>>;

  class ExceptionTrace {
   public:
    explicit ExceptionTrace(Error error, Trace trace)
        : error_(std::move(error)), trace_(std::move(trace)) {}

    const Error& error() const { return error_; }
    const Trace& trace() const { return trace_; }

   private:
    Error error_;
    Trace trace_;
  };

  explicit EvalBase(bool trace_on_error = true)
      : trace_on_error_(trace_on_error) {}

  virtual void Reset() {
    num_visited_states_ = 0;
    num_queued_states_ = 0;
  }

  /**
   * Searches the state-space. Returns the set of accepting states: the
   * implementation of this function shall use a function State::Accept() to
   * query if a state is accepting.
   *
   * StateQueue rather than StateMap is chosen as the return type to provides
   * symmetry between input and output types. The Evaluate function needs to
   * take care not to insert duplicates, or specify explicitly the behaviour.
   *
   * @param initial The initial States.
   * @param ts The TransitionSystem.
   * @return Queue of accepting states, in order encountered.
   */
  virtual StateQueue<State> Evaluate(const StateQueue<State>& start_states,
                                     TransitionSystem* ts) = 0;

  bool trace_on_error() const { return trace_on_error_; }

  void set_trace_on_error(bool v) { trace_on_error_ = v; }

  std::size_t num_visited_states() const { return num_visited_states_; }

  std::size_t num_queued_states() const { return num_queued_states_; }

  void set_monitor(
      std::function<bool(const EvalBase&, StateQueue<State>*)> monitor) {
    monitor_valid_ = true;
    monitor_ = std::move(monitor);
  }

  void unset_monitor() { monitor_valid_ = false; }

  /**
   * The monitor may be used to obtain intermediate results. Note that, it can
   * also be used to modify the list of accept states. This implies that the
   * evaluation must not use the list of accept states itself.
   *
   * @return True if evaluation should continue; false otherwise.
   */
  bool monitor(StateQueue<State>* accept_states) {
    if (monitor_valid_) {
      return monitor_(*this, accept_states);
    }

    return true;
  }

 protected:
  Trace MakeTraceFromHashes(const StateQueue<State>& start_states,
                            const std::vector<StateHash<State>>& back_trace,
                            TransitionSystem* ts) {
    assert(!back_trace.empty());
    auto cur_hash_it = back_trace.rbegin();

    // Find start state.
    State current_state =
        *std::find_if(start_states.begin(), start_states.end(),
                      [&cur_hash_it](const State& state) {
                        return GetHash(state) == *cur_hash_it;
                      });

    ++cur_hash_it;
    Trace result;
    for (; cur_hash_it != back_trace.rend(); ++cur_hash_it) {
      // found_state is used to prevent duplicate states being entered; it is
      // possible that two different rules produce the same state. We are only
      // interested in one of them.
      bool found_state = false;
      auto next_states = ts->Evaluate(
          current_state,
          [&result, &current_state, &cur_hash_it, &found_state](
              const typename Rule<State>::Ptr& rule, const State& state) {
            if (*cur_hash_it == GetHash(state)) {
              if (!found_state) {
                result.emplace_back(
                    std::make_pair(current_state, rule->name()));
                found_state = true;
              }

              return true;
            }

            return false;
          });

      assert(found_state);

      // We expect that at least all next states of some state have unique
      // hashes (no hash collision).
      assert(next_states.size() == 1);
      current_state = std::move(next_states.begin()->second);
    }

    result.push_back(std::make_pair(current_state, ""));

    return result;
  }

  bool trace_on_error_;

  std::size_t num_visited_states_ = 0;
  std::size_t num_queued_states_ = 0;

  bool monitor_valid_ = false;
  std::function<bool(const EvalBase&, StateQueue<State>*)> monitor_;
};

}  // namespace core
}  // namespace verc3

#endif /* VERC3_CORE_EVAL_HH_ */

/* vim: set ts=2 sts=2 sw=2 et : */
