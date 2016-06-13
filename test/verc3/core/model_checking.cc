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

// Include tested header first, to assert it includes required headers itself!
#include "verc3/core/model_checking.hh"

#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include "verc3/core/types.hh"
#include "verc3/debug.hh"

using namespace verc3;
using namespace verc3::core;

template <class T>
class EvalBackend
    : public ::testing::TestWithParam<std::shared_ptr<core::EvalBase<T>>> {};

template <class T>
struct NumberState;

template <>
struct NumberState<int> {
  struct Hash {
    auto operator()(const NumberState& k) const {
      return std::hash<int>()(k.s);
    }
  };

  bool operator==(const NumberState& rhs) const { return s == rhs.s; }

  bool Accept() const { return (s == 3); }

  int s = 1;
};

typedef NumberState<int> IntState;

template <class T>
struct NumberState {
  struct Hash {
    auto operator()(const NumberState& k) const { return std::hash<T>()(k.s); }
  };

  // simulation relation
  operator NumberState<int>() const {
    NumberState<int> result;
    result.s = static_cast<int>(s);
    return result;
  };

  // simulation relation
  bool operator==(const NumberState<int>& rhs) const {
    return static_cast<int>(s) == rhs.s;
  }

  bool operator==(const NumberState& rhs) const { return s == rhs.s; }

  bool Accept() const { return false; }

  T s = static_cast<T>(1);
};

template <class T>
class Increment : public Rule<NumberState<T>> {
 public:
  explicit Increment(int reset_to = 0)
      : Rule<NumberState<T>>("Increment"), reset_to_(reset_to) {}

  bool PreCond(const NumberState<T>& state) const override { return true; }

  bool Action(NumberState<T>* state) const override {
    if (state->s == static_cast<T>(5)) {
      state->s = reset_to_;
    }

    ++state->s;
    return true;
  }

 private:
  int reset_to_;
};

typedef EvalBackend<TransitionSystem<IntState>> EvalBackendIntState;

TEST_P(EvalBackendIntState, AcceptState) {
  TransitionSystem<IntState> ts;
  ts <<= {std::make_shared<Increment<int>>()};
  ts *= TransitionSystem<IntState>(ts);  // should not affect outcome

  auto eval = GetParam();
  auto accept_states = eval->Evaluate({IntState()}, &ts);

  ASSERT_EQ(1U, accept_states.size());
  ASSERT_EQ(3, accept_states.begin()->s);
  ASSERT_EQ(5U, eval->num_visited_states());
  ASSERT_EQ(0U, eval->num_queued_states());
}

TEST_P(EvalBackendIntState, Deadlock) {
  TransitionSystem<IntState> ts(
      {std::make_shared<RuleF<IntState>>(
          "BrokenIncrement", [](const IntState& state) { return state.s != 4; },
          [](IntState* state) {
            state->s++;
            return state;
          })},

      {std::make_shared<InvariantF<IntState>>(
          "NeverViolateAsDeadlocksBefore",
          [](const IntState& state) { return state.s != 5; })});

  auto eval = GetParam();

  try {
    eval->Evaluate({IntState()}, &ts);
    FAIL();
  } catch (const decltype(eval)::element_type::ExceptionTrace& trace) {
    ASSERT_EQ("DEADLOCK", std::string(trace.error().what()));
  }

  ASSERT_EQ(3U, eval->num_visited_states());
  ASSERT_EQ(1U, eval->num_queued_states());
}

TEST_P(EvalBackendIntState, Monitor) {
  TransitionSystem<IntState> ts;
  ts <<= {std::make_shared<Increment<int>>()};

  auto eval = GetParam();

  eval->set_monitor([](const EvalBase<TransitionSystem<IntState>>& mc,
                       StateQueue<IntState>* accept) {
    if (mc.num_visited_states() == 4) {
      assert(1U == accept->size());
      return false;
    }

    return true;
  });

  ts *= TransitionSystem<IntState>(ts);  // should not affect outcome

  auto accept_states = eval->Evaluate({IntState()}, &ts);

  ASSERT_EQ(1U, accept_states.size());
  ASSERT_EQ(3, accept_states.begin()->s);
  ASSERT_EQ(4U, eval->num_visited_states());
  ASSERT_EQ(1U, eval->num_queued_states());
}

struct SomeInvariant : Property<IntState> {
  SomeInvariant() : Property<IntState>("SomeInvariant") {}

  bool Invariant(const IntState& state) const override { return state.s != 3; }
};

TEST_P(EvalBackendIntState, Invariant) {
  TransitionSystem<IntState> ts;
  ts.Make<Increment<int>>();
  ts.Make<SomeInvariant>();

  auto eval = GetParam();

  try {
    eval->Evaluate({IntState()}, &ts);
    FAIL();
  } catch (const decltype(eval)::element_type::ExceptionTrace& trace) {
    ASSERT_EQ(std::string(trace.error().what()), "SomeInvariant");
    ASSERT_EQ(3U, trace.trace().size());

    std::ostringstream oss;
    debug::PrintTraceDiff(
        trace.trace(),
        [](const decltype(eval)::element_type::Trace::value_type& v,
           std::ostream& os) { os << v.first.s << std::endl; },
        [](const decltype(eval)::element_type::Trace::value_type& v,
           std::ostream& os) { os << v.second << std::endl; },
        oss);
    ASSERT_EQ(60U, oss.str().size());
  }

  ASSERT_EQ(2U, eval->num_visited_states());
  ASSERT_EQ(1U, eval->num_queued_states());
}

TEST_P(EvalBackendIntState, NoTrace) {
  TransitionSystem<IntState> ts;
  ts.Make<Increment<int>>();
  ts.Make<SomeInvariant>();

  auto eval = GetParam();
  eval->set_trace_on_error(false);

  try {
    eval->Evaluate({IntState()}, &ts);
    FAIL();
  } catch (const Error& error) {
    ASSERT_EQ(std::string(error.what()), "SomeInvariant");
  }

  ASSERT_EQ(2U, eval->num_visited_states());
  ASSERT_EQ(1U, eval->num_queued_states());
}

class Liveness : public Property<IntState> {
 public:
  explicit Liveness() : Property<IntState>("Liveness") {}

  bool Invariant(const IntState& state) const override { return true; }

  void Next(const IntState& state,
            const StateMap<IntState>& next_states) override {
    for (const auto& kv : next_states) {
      assert(kv.first == GetHash(kv.second));

      // We want the system to always eventually reach state '1'.
      if (state.s != 1 && kv.second.s != 1) {
        state_graph_.Insert(state, kv.second);
      }
    }
  }

  bool IsSatisfied() const { return state_graph_.Acyclic(); }

 private:
  Relation<IntState> state_graph_;
};

TEST_P(EvalBackendIntState, LivenessSatisfied) {
  TransitionSystem<IntState> ts;

  ts <<= {std::make_shared<Increment<int>>()};
  auto liveness = std::make_shared<Liveness>();
  ts |= {liveness};

  auto eval = GetParam();

  eval->Evaluate({IntState()}, &ts);
  ASSERT_EQ(5U, eval->num_visited_states());
  ASSERT_EQ(0U, eval->num_queued_states());
  ASSERT_TRUE(liveness->IsSatisfied());
}

TEST_P(EvalBackendIntState, LivenessFail) {
  TransitionSystem<IntState> ts;

  ts <<= {std::make_shared<Increment<int>>(1)};
  auto liveness = std::make_shared<Liveness>();
  ts |= {liveness};

  auto eval = GetParam();

  eval->Evaluate({IntState()}, &ts);
  ASSERT_EQ(5U, eval->num_visited_states());
  ASSERT_EQ(0U, eval->num_queued_states());
  ASSERT_FALSE(liveness->IsSatisfied());
}

INSTANTIATE_TEST_CASE_P(
    CoreModelChecking, EvalBackendIntState,
    ::testing::Values(
        std::make_shared<Eval_BFS<TransitionSystem<IntState>>>(),
        std::make_shared<Eval_BFS_Hashing<TransitionSystem<IntState>>>()));

TEST(CoreModelChecking, SimulationFail) {
  TransitionSystem<IntState> ts;
  ts <<= {std::make_shared<Increment<int>>()};

  TransitionSysSimulation<NumberState<float>, IntState> ts_sim(ts);
  ts_sim <<= {std::make_shared<Increment<float>>()};
  ts_sim <<= {std::make_shared<Increment<float>>(3.0)};

  Eval_BFS<TransitionSysSimulation<NumberState<float>, IntState>> eval;

  try {
    eval.Evaluate({NumberState<float>()}, &ts_sim);
    FAIL();
  } catch (const decltype(eval)::ExceptionTrace& trace) {
    ASSERT_EQ(std::string(trace.error().what()), "SIMULATION");
  }

  ASSERT_EQ(4U, eval.num_visited_states());
  ASSERT_EQ(1U, eval.num_queued_states());
}

TEST(CoreModelChecking, SimulationSuccess) {
  TransitionSystem<IntState> ts;
  ts <<= {std::make_shared<Increment<int>>()};
  ts <<= {std::make_shared<Increment<int>>(3)};

  TransitionSysSimulation<NumberState<float>, IntState> ts_sim(ts);
  ts_sim <<= {std::make_shared<Increment<float>>()};

  Eval_BFS<TransitionSysSimulation<NumberState<float>, IntState>> eval;
  eval.Evaluate({NumberState<float>()}, &ts_sim);

  ASSERT_EQ(5U, eval.num_visited_states());
  ASSERT_EQ(0U, eval.num_queued_states());
}

/* vim: set ts=2 sts=2 sw=2 et : */
