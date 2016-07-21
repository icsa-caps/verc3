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

#ifndef VERC3_CORE_TS_HH_
#define VERC3_CORE_TS_HH_

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <functional>
#include <list>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace verc3 {
namespace core {

template <class State>
using StateHash =
    typename std::result_of<typename State::Hash(const State&)>::type;

/**
 * The reason for using a map (rather than just a set of States) is for
 * efficiency; STL associative containers retain the keys as const, which
 * prevents moving them into other containers. We need to use a multimap, as
 * otherwise we may discard states that map to same same hash (due to hash
 * collisions); note that, for some derived classes of EvalBase this is
 * irrelevant, e.g. Eval_BFS_Hashing.
 */
template <class State>
using StateMap = std::unordered_multimap<StateHash<State>, State>;

template <class State>
using StateQueue = std::list<State>;

template <class State>
inline StateHash<State> GetHash(const State& state) {
  return typename State::Hash()(state);
}

template <class State>
inline bool StateExists(const State& state, const StateMap<State>& state_map) {
  const auto range = state_map.equal_range(GetHash(state));
  return std::distance(range.first, range.second) != 0 &&
         std::find_if(range.first, range.second,
                      [&state](const typename StateMap<State>::value_type& v) {
                        return v.second == state;
                      }) != range.second;
}

struct Error : std::logic_error {
  using std::logic_error::logic_error;
};

struct Deadlock : Error {
  using Error::Error;
};

struct PropertyViolation : Error {
  using Error::Error;
};

inline void ErrorIf(bool cond, const char* what) {
  if (cond) {
    throw Error(what);
  }
}

template <class State>
class Rule {
 public:
  typedef std::unique_ptr<Rule> Ptr;

  explicit Rule(std::string name) : name_(std::move(name)) {
    assert(!name_.empty());
  }

  virtual bool PreCond(const State& state) const = 0;

  virtual bool Action(State* state) const = 0;

  virtual bool PostCond(const State& prev, const State& next) const {
    return true;
  }

  const std::string& name() const { return name_; }

 protected:
  std::string name_;
};

template <class State>
class RuleF : public Rule<State> {
 public:
  explicit RuleF(std::string name,
                 std::function<bool(const State& state)> guard,
                 std::function<bool(State* state)> action)
      : Rule<State>(std::move(name)),
        guard_(std::move(guard)),
        action_(std::move(action)) {}

  bool PreCond(const State& state) const override { return guard_(state); }

  bool Action(State* state) const override { return action_(state); }

 private:
  std::function<bool(const State& state)> guard_;
  std::function<bool(State* state)> action_;
};

template <class State>
class Property {
 public:
  typedef std::unique_ptr<Property> Ptr;

  explicit Property(std::string name) : name_(std::move(name)) {
    assert(!name_.empty());
  }

  virtual Ptr Clone() const = 0;

  /**
   * Property may maintain state (modified in Next). Must call Reset before
   * reuse.
   */
  virtual void Reset() {}

  /**
   * Global invariant, checked before calling Evaluate on a state.
   *
   * @return true if state satisfies invariant; false otherwise.
   */
  virtual bool Invariant(const State& state) const = 0;

  /**
   * Called after the set of next states from state has been enumerated.
   *
   * One of its intended use-cases is to implement temporal properties, which
   * rely on traces of states.
   */
  virtual void Next(const State& state, const StateMap<State>& next_states) {}

  /**
   * Check if property is satisfied for all evaluated states. Used in
   * conjunction with Next.
   *
   * @param verbose_on_error Print trace on error.
   * @return true if satisfied; false otherwise.
   */
  virtual bool IsSatisfied(bool verbose_on_error = true) const { return true; }

  const std::string& name() const { return name_; }

 protected:
  std::string name_;
};

template <class State>
class InvariantF : public Property<State> {
 public:
  explicit InvariantF(std::string name,
                      std::function<bool(const State& state)> verify)
      : Property<State>(std::move(name)), verify_(std::move(verify)) {}

  typename Property<State>::Ptr Clone() const override {
    return std::make_unique<InvariantF>(*this);
  }

  bool Invariant(const State& state) const override { return verify_(state); }

 private:
  std::function<bool(const State& state)> verify_;
};

/**
 * Transition system description.
 *
 * The state of TransitionSystem must not be affected by evaluation, except for
 * properties that record auxiliary information (e.g. temporal properties).
 */
template <class StateT>
class TransitionSystem {
 public:
  typedef StateT State;
  typedef typename Rule<State>::Ptr RulePtr;
  typedef typename Property<State>::Ptr PropertyPtr;

  explicit TransitionSystem(bool deadlock_detection = true)
      : deadlock_detection_(deadlock_detection) {}

  TransitionSystem(const TransitionSystem& rhs) = delete;

  TransitionSystem(TransitionSystem&& rhs) = default;

  void Reset() {
    // *ONLY* reset properties here; nothing else should maintain state.
    for (auto& property : properties_) {
      property->Reset();
    }
  }

  StateMap<State> Evaluate(const State& state) {
    return Evaluate(state, [](const RulePtr&, const State&) { return true; });
  }

  template <class FilterFunc>
  StateMap<State> Evaluate(const State& state, FilterFunc filter_state) {
    for (auto& property : properties_) {
      if (!property->Invariant(state)) {
        throw PropertyViolation(property->name());
      }
    }

    StateMap<State> next_states;

    for (auto& rule : rules_) {
      if (rule->PreCond(state)) {
        State next_state(state);
        auto valid = rule->Action(&next_state);

        if (valid && !StateExists(next_state, next_states) &&
            rule->PostCond(state, next_state) &&
            filter_state(rule, next_state)) {
          next_states.emplace(GetHash(next_state), std::move(next_state));
        }
      }
    }

    if (deadlock_detection_ && next_states.empty()) {
      throw Deadlock("DEADLOCK");
    }

    for (auto& property : properties_) {
      property->Next(state, next_states);
    }

    return next_states;
  }

  void Register(RulePtr rule) { rules_.emplace_back(std::move(rule)); }

  void Register(PropertyPtr property) {
    properties_.emplace_back(std::move(property));
  }

  template <class T, class... Args>
  const T* Make(Args&&... args) {
    auto own = std::make_unique<T>(std::forward<Args>(args)...);
    auto result = own.get();
    Register(std::move(own));
    return result;
  }

  bool deadlock_detection() const { return deadlock_detection_; }

  void set_deadlock_detection(bool val) { deadlock_detection_ = val; }

  const std::vector<RulePtr>& rules() const { return rules_; }

  const std::vector<PropertyPtr>& properties() const { return properties_; }

 private:
  bool deadlock_detection_;
  std::vector<RulePtr> rules_;
  std::vector<PropertyPtr> properties_;
};

template <class State, class SimulatedByState>
class TransitionSysSimulation : public TransitionSystem<State> {
 public:
  explicit TransitionSysSimulation(
      TransitionSystem<SimulatedByState>&& sim_by_ts)
      : sim_by_ts_(std::move(sim_by_ts)) {}

  using TransitionSystem<State>::Evaluate;

  StateMap<State> Evaluate(const State& state) {
    const auto sim_by_state =
        static_cast<SimulatedByState>(state);  // simulation relation
    const auto sim_by_states = sim_by_ts_.Evaluate(sim_by_state);

    return Evaluate(state, [&sim_by_states](const typename Rule<State>::Ptr& t,
                                            const State& s) {
      auto match = std::find_if(
          sim_by_states.begin(), sim_by_states.end(),
          [&s](const typename decltype(sim_by_states)::value_type& ss) {
            return s == ss.second;
          });

      if (match == sim_by_states.end()) {
        // Disallowed transition.
        throw PropertyViolation("SIMULATION");
      }

      return true;
    });
  }

  const auto& sim_by_ts() const { return sim_by_ts; }

 private:
  TransitionSystem<SimulatedByState> sim_by_ts_;
};

struct StateNonAccepting {
  bool Accept() const { return false; }
};

}  // namespace core
}  // namespace verc3

#endif /* VERC3_CORE_TS_HH_ */

/* vim: set ts=2 sts=2 sw=2 et : */
