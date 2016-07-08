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

#ifndef VERC3_DOMAIN_PROTOCOL_HH_
#define VERC3_DOMAIN_PROTOCOL_HH_

#include <functional>
#include <iostream>

#include "verc3/core/ts.hh"
#include "verc3/core/types.hh"
#include "verc3/debug.hh"

namespace verc3 {
namespace protocol {

/**
 * Livelock freedom liveness property.
 *
 * If a processor issues a memory access, this memory access must eventually be
 * satisfied [1]. To assert this is true, this class records the state graph
 * (for each processor individually) between transient state. If there exists a
 * cycle in the transient state graph, it is possible to never reach a stable
 * state again.
 *
 * [1] <a href="http://www.kenmcmil.com/pubs/ISSMM91.pdf"> Kenneth L. McMillan,
 *      J. Schwalbe, "Formal verification of the Gigamax cache consistency
 *      protocol" ISSM. 1991</a>
 */
template <class State, class ScalarSet>
class LivelockFreedom : public core::Property<State> {
 public:
  typedef typename ScalarSet::Element NodeState;

  explicit LivelockFreedom(
      std::function<const ScalarSet&(const State&)> scalar_set_accessor,
      std::function<bool(const NodeState&)> stable_pred)
      : core::Property<State>("LivelockFreedom"),
        scalar_set_accessor_(std::move(scalar_set_accessor)),
        stable_pred_(std::move(stable_pred)) {}

  void Reset() override { state_graph_.Clear(); }

  bool Invariant(const State& state) const override { return true; }

  void Next(const State& state,
            const core::StateMap<State>& next_states) override {
    for (const auto& kv : next_states) {
      // We want each cache to always eventually reach a stable state. For now
      // this only checks that a node's state does not ping-pong between some
      // transient states.
      scalar_set_accessor_(state).for_each_ID(
          [this, &state, &kv](const typename ScalarSet::ID id) {
            auto prev_node_state = scalar_set_accessor_(state)[id];
            auto next_node_state = scalar_set_accessor_(kv.second)[id];
            if (!stable_pred_(*prev_node_state) &&
                !stable_pred_(*next_node_state) &&
                !(*prev_node_state == *next_node_state)) {
              state_graph_.Insert(*prev_node_state, *next_node_state);
            }
          });
    }
  }

  bool IsSatisfied(bool trace_on_error = true) const override {
    typename core::Relation<NodeState>::Path path;
    if (!state_graph_.Acyclic(&path)) {
      if (trace_on_error) {
        std::cout << std::endl;
        PrintTraceDiff(
            path, [](const NodeState& node, std::ostream& os) { os << node; },
            [](const NodeState& node, std::ostream& os) {
              os << "\e[1;32m================>\e[0m" << std::endl;
            },
            std::cout);

        std::cout << "\e[1;31m===> VERIFICATION FAILED (" << path.size()
                  << " steps): LIVELOCK\e[0m" << std::endl;
        std::cout << std::endl;
      }
      return false;
    }
    return true;
  }

 protected:
  std::function<const ScalarSet&(const State&)> scalar_set_accessor_;
  std::function<bool(const NodeState& state)> stable_pred_;
  core::Relation<NodeState> state_graph_;
};

}  // namespace protocol
}  // namespace verc3

#endif /* VERC3_DOMAIN_PROTOCOL_HH_ */

/* vim: set ts=2 sts=2 sw=2 et : */
