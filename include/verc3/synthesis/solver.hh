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

#ifndef VERC3_SYNTHESIS_SOLVER_HH_
#define VERC3_SYNTHESIS_SOLVER_HH_

#include <future>
#include <utility>
#include <vector>

#include <glog/logging.h>

#include "verc3/command.hh"
#include "verc3/synthesis/enumerate.hh"

namespace verc3 {
namespace synthesis {

extern synthesis::RangeEnumerate g_range_enumerate;
extern thread_local synthesis::RangeEnumerate t_range_enumerate;

template <class TransitionSystem>
class Solver {
 public:
  explicit Solver(TransitionSystem&& ts, bool verbose = true)
      : ts_(std::move(ts)) {
    command_.eval()->set_trace_on_error(false);
    command_.eval()->unset_monitor();
  }

  auto operator()(
      const core::StateQueue<typename TransitionSystem::State>& start_states,
      synthesis::RangeEnumerate start, std::size_t msto) {
    // assume t_range_enumerate is thread_local!
    assert(t_range_enumerate.states().empty());
    std::vector<synthesis::RangeEnumerate> result;
    t_range_enumerate = std::move(start);

    do {
      ++steps_;
      try {
        command_(start_states, &ts_);
        VLOG(0) << "SOLUTION @ total discovered "
                << t_range_enumerate.combinations()
                << " combinations | visited states: "
                << command_.eval()->num_visited_states() << " | "
                << t_range_enumerate;
        result.push_back(t_range_enumerate);
      } catch (const core::Error& e) {
      }
    } while (
        t_range_enumerate.Next() &&
        (msto == 0 || t_range_enumerate.GetMostSignificant()->value < msto));

    t_range_enumerate.Clear();
    return result;
  }

  std::size_t steps() const { return steps_; }

 private:
  ModelCheckerCommand<TransitionSystem> command_;
  TransitionSystem ts_;
  std::size_t steps_ = 0;
};

template <class TransitionSystem, class Func>
inline std::vector<synthesis::RangeEnumerate> ParallelSolve(
    const core::StateQueue<typename TransitionSystem::State>& start_states,
    Func transition_system_factory, std::size_t num_threads = 1) {
  assert(g_range_enumerate.states().empty());
  std::vector<Solver<TransitionSystem>> solvers;
  std::vector<std::future<std::vector<synthesis::RangeEnumerate>>> futures;
  std::vector<synthesis::RangeEnumerate> result;

  for (std::size_t i = 0; i < num_threads; ++i) {
    solvers.emplace_back(transition_system_factory(start_states.front()));
  }

  do {
    // Get copy of current g_range_enumerate, as other threads might start
    // modifying it while we launch new threads.
    auto cur_g_range_enumerate = g_range_enumerate;
    auto cur_most_significant = cur_g_range_enumerate.GetMostSignificant();

    // Set all existing elements to max before new ones are discovered by
    // starting the threads.
    g_range_enumerate.SetMax();

    if (cur_most_significant == nullptr) {
      auto solns = solvers.front()(start_states, cur_g_range_enumerate, 0);
      std::move(solns.begin(), solns.end(), std::back_inserter(result));
    } else {
      std::size_t ms_base = cur_most_significant->value;
      std::size_t ms_offset =
          (cur_most_significant->range() - ms_base + 1) / num_threads;
      if (ms_offset == 0) ms_offset = 1;
      assert(ms_base + num_threads * ms_offset >=
             cur_most_significant->range());

      for (auto& solver : solvers) {
        assert(cur_most_significant->value < cur_most_significant->range());
        auto next_value = (cur_most_significant->value + ms_offset);
        if (next_value > cur_most_significant->range()) {
          next_value = cur_most_significant->range();
        }

        VLOG(0) << "Dispatching thread for [" << cur_most_significant->value
                << "," << next_value << ")/" << cur_most_significant->range()
                << " ...";
        futures.emplace_back(std::async(std::launch::async, [
          &solver, &start_states, tre = cur_g_range_enumerate, next_value
        ]() { return solver(start_states, std::move(tre), next_value); }));

        cur_g_range_enumerate.SetMin();
        cur_most_significant->value = next_value;

        if (cur_most_significant->value >= cur_most_significant->range()) break;
      }

      for (auto& future : futures) {
        auto solns = future.get();
        std::move(solns.begin(), solns.end(), std::back_inserter(result));
      }

      futures.clear();
    }

    std::size_t current_steps = 0;
    for (const auto& solver : solvers) {
      current_steps += solver.steps();
    }
    assert(current_steps <= g_range_enumerate.combinations());
    VLOG(0) << "Enumerated " << current_steps << " variants out of "
            << g_range_enumerate.combinations();
  } while (g_range_enumerate.Next());

  return result;
}

}  // namespace synthesis
}  // namespace verc3

#define SYNTHESIZE(opt, local_name, ...)                       \
  do {                                                         \
    static decltype(opt) local_name(#local_name, opt);         \
    local_name.Register(&verc3::synthesis::g_range_enumerate); \
    local_name.GetCurrent(verc3::synthesis::t_range_enumerate, \
                          true)(__VA_ARGS__);                  \
  } while (0);

#endif /* VERC3_SYNTHESIS_SOLVER_HH_ */

/* vim: set ts=2 sts=2 sw=2 et : */
