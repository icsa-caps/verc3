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
#include <cstddef>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <gsl/gsl>

#include "ts.hh"

namespace verc3 {
namespace core {

template <class TransitionSystemT>
class EvalBase {
 public:
  using TransitionSystem = TransitionSystemT;
  using State = typename TransitionSystemT::State;

  /**
   * HashTrace is a sequences of state hashes ordered from last state to
   * initial state.
   */
  using HashTrace = std::vector<StateHash<State>>;

  /**
   * Trace is a sequence of full state-action pairs from initial state to last
   * state.
   */
  using Trace = std::vector<std::pair<State, std::string>>;

  class ErrorTrace : public std::exception {
   public:
    explicit ErrorTrace(Error error, Trace trace)
        : error_(std::move(error)), trace_(std::move(trace)) {}

    const Error& error() const { return error_; }
    const Trace& trace() const { return trace_; }

    const char* what() const noexcept { return error_.what(); }

   private:
    Error error_;
    Trace trace_;
  };

  class ErrorHashTrace : public std::exception {
   public:
    explicit ErrorHashTrace(Error error, HashTrace hash_trace)
        : error_(std::move(error)), hash_trace_(std::move(hash_trace)) {}

    const Error& error() const { return error_; }
    const HashTrace& hash_trace() const { return hash_trace_; }

    const char* what() const noexcept { return error_.what(); }

   private:
    Error error_;
    HashTrace hash_trace_;
  };

  explicit EvalBase(bool verbose_on_error = true)
      : verbose_on_error_(verbose_on_error) {}

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
                                     TransitionSystem* ts) {
    // This function is not pure virtual, as EvalBase can be used without
    // Evaluate -- see MakeTraceFromHashTrace.
    throw std::logic_error("Evaluate not implemented!");
  }

  bool verbose_on_error() const { return verbose_on_error_; }

  void set_verbose_on_error(bool v) { verbose_on_error_ = v; }

  std::size_t num_visited_states() const { return num_visited_states_; }

  std::size_t num_queued_states() const { return num_queued_states_; }

  void set_monitor(
      std::function<bool(const EvalBase&, StateQueue<State>*)> monitor) {
    monitor_ = std::move(monitor);
  }

  void unset_monitor() {
    monitor_ = std::function<bool(const EvalBase&, StateQueue<State>*)>();
  }

  /**
   * The monitor may be used to obtain intermediate results. Note that, it can
   * also be used to modify the list of accept states. This implies that the
   * evaluation must not use the list of accept states itself.
   *
   * @return True if evaluation should continue; false otherwise.
   */
  bool monitor(StateQueue<State>* accept_states) {
    if (monitor_) {
      return monitor_(*this, accept_states);
    }

    return true;
  }

  /**
   * Make a trace from a sequence of state hashes.
   *
   * Note that TransitionSystem ts is not reset here, but since we do not make
   * use of any properties here, this is irrelevant.
   *
   * @param start_states The set of start states, that include the start of the
   *    trace.
   * @param hash_trace A trace of hashes for which to reconstruct a concrete
   *    state trace.
   * @param ts The transition system instance in which the trace is valid.
   * @return A concrete State trace.
   *
   * @pre hash_trace is not empty.
   */
  Trace MakeTraceFromHashTrace(const StateQueue<State>& start_states,
                               const HashTrace& hash_trace,
                               TransitionSystem* ts) {
    Expects(!hash_trace.empty());
    auto cur_hash_it = hash_trace.rbegin();

    // Find start state.
    State current_state =
        *std::find_if(start_states.begin(), start_states.end(),
                      [&cur_hash_it](const State& state) {
                        return GetHash(state) == *cur_hash_it;
                      });

    ++cur_hash_it;
    Trace result;
    for (; cur_hash_it != hash_trace.rend(); ++cur_hash_it) {
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
                result.emplace_back(current_state, rule->name());
                found_state = true;
              }

              return true;
            }

            return false;
          });

      Expects(found_state);

      // We expect that at least all next states of some state have unique
      // hashes (no hash collision).
      Expects(next_states.size() == 1);
      current_state = std::move(next_states.begin()->second);
    }

    result.emplace_back(current_state, "");

    return result;
  }

 protected:
  bool verbose_on_error_;

  std::size_t num_visited_states_ = 0;
  std::size_t num_queued_states_ = 0;

  std::function<bool(const EvalBase&, StateQueue<State>*)> monitor_;
};

}  // namespace core
}  // namespace verc3

#endif /* VERC3_CORE_EVAL_HH_ */

/* vim: set ts=2 sts=2 sw=2 et : */
