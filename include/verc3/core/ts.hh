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
  typedef std::shared_ptr<Rule> Ptr;

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
  typedef std::shared_ptr<Property> Ptr;

  explicit Property(std::string name) : name_(std::move(name)) {
    assert(!name_.empty());
  }

  /**
   * Global invariant, checked before calling Evaluate on a state.
   */
  virtual bool Invariant(const State& state) const = 0;

  /**
   * Called after the set of next states from state has been enumerated.
   *
   * One of its intended use-cases is to implement temporal properties, which
   * rely on traces of states.
   */
  virtual void Next(const State& state, const StateMap<State>& next_states) {}

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

  bool Invariant(const State& state) const override { return verify_(state); }

 private:
  std::function<bool(const State& state)> verify_;
};

template <class StateT>
class TransitionSystem {
 public:
  typedef StateT State;
  typedef typename Rule<State>::Ptr RulePtr;
  typedef typename Property<State>::Ptr PropertyPtr;

  explicit TransitionSystem(bool deadlock_detection = true)
      : deadlock_detection_(deadlock_detection) {}

  explicit TransitionSystem(std::vector<RulePtr> rules,
                            std::vector<PropertyPtr> properties,
                            bool deadlock_detection = true)
      : deadlock_detection_(deadlock_detection),
        rules_(std::move(rules)),
        properties_(std::move(properties)) {}

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

  void Register(RulePtr rule) { rules_.push_back(std::move(rule)); }

  void Register(PropertyPtr property) {
    properties_.push_back(std::move(property));
  }

  template <class T, class... Args>
  auto Make(Args&&... args) {
    auto result = std::make_shared<T>(std::forward<Args>(args)...);
    Register(result);
    return result;
  }

  TransitionSystem& operator<<=(const std::vector<RulePtr>& rules) {
    rules_.insert(rules_.end(), rules.begin(), rules.end());
    return *this;
  }

  TransitionSystem& operator<<=(std::vector<RulePtr>&& rules) {
    if (rules_.empty()) {
      rules_ = std::move(rules);
    } else {
      std::move(rules.begin(), rules.end(), std::back_inserter(rules_));
    }

    return *this;
  }

  TransitionSystem& operator<<=(const TransitionSystem& ts) {
    *this <<= ts.rules_;
    return *this;
  }

  TransitionSystem& operator<<=(TransitionSystem&& ts) {
    *this <<= std::move(ts.rules_);
    return *this;
  }

  TransitionSystem& operator|=(const std::vector<PropertyPtr>& properties) {
    properties_.insert(properties_.end(), properties.begin(), properties.end());
    return *this;
  }

  TransitionSystem& operator|=(std::vector<PropertyPtr>&& properties) {
    if (properties_.empty()) {
      properties_ = std::move(properties);
    } else {
      std::move(properties.begin(), properties.end(),
                std::back_inserter(properties_));
    }

    return *this;
  }

  TransitionSystem& operator|=(const TransitionSystem& ts) {
    *this |= ts.properties_;
    return *this;
  }

  TransitionSystem& operator|=(TransitionSystem&& ts) {
    *this |= std::move(ts.properties_);
    return *this;
  }

  TransitionSystem& operator*=(const TransitionSystem& ts) {
    assert(&ts != this);

    *this <<= ts.rules_;
    *this |= ts.properties_;
    return *this;
  }

  TransitionSystem& operator*=(TransitionSystem&& ts) {
    assert(&ts != this);

    *this <<= std::move(ts.rules_);
    *this |= std::move(ts.properties_);
    return *this;
  }

  bool deadlock_detection() const { return deadlock_detection_; }

  void set_deadlock_detection(bool val) { deadlock_detection_ = val; }

  const std::vector<RulePtr>& rules() { return rules_; }

  const std::vector<PropertyPtr>& properties() { return properties_; }

 private:
  bool deadlock_detection_;
  std::vector<RulePtr> rules_;
  std::vector<PropertyPtr> properties_;
};

template <class State, class SimulatedByState>
class TransitionSysSimulation : public TransitionSystem<State> {
 public:
  explicit TransitionSysSimulation(TransitionSystem<SimulatedByState> sim_by_ts)
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
