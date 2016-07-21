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

#include "verc3/command.hh"
#include "verc3/io.hh"
#include "verc3/synthesis/enumerate.hh"

namespace verc3 {
namespace synthesis {

extern synthesis::RangeEnumerate g_range_enumerate;
extern thread_local synthesis::RangeEnumerate t_range_enumerate;

template <class TransitionSystem>
class ParallelSolver;

template <class TransitionSystem>
class Solver {
 public:
  using ErrorHashTrace =
      typename core::EvalBase<TransitionSystem>::ErrorHashTrace;

  typedef std::function<void(const Solver&, const RangeEnumerate&,
                             const ErrorHashTrace*)>
      CandidateCallback;

  explicit Solver(TransitionSystem&& ts, std::size_t id = 0)
      : transition_system_(std::move(ts)), id_(id) {
    command_.eval().set_verbose_on_error(false);
    command_.eval().unset_monitor();
  }

  auto operator()(
      const core::StateQueue<typename TransitionSystem::State>& start_states,
      synthesis::RangeEnumerate start, std::size_t num_candidates) {
    // assume t_range_enumerate is thread_local!
    assert(t_range_enumerate.states().empty());
    assert(num_candidates > 0);
    std::vector<synthesis::RangeEnumerate> result;
    t_range_enumerate = std::move(start);

    do {
      try {
        if (command_(start_states, &transition_system_) == 0) {
          std::cout << "solution[" << id_ << "][" << enumerated_candidates_
                    << "] = " << t_range_enumerate << std::endl;
          result.push_back(t_range_enumerate);
        }

        if (candidate_callback_) {
          candidate_callback_(*this, t_range_enumerate, nullptr);
        }
      } catch (const ErrorHashTrace& e) {
        if (candidate_callback_) {
          candidate_callback_(*this, t_range_enumerate, &e);
        }
      }
      ++enumerated_candidates_;

      // Advance thread-local RangeEnumerate; if overflow, we are done.
      if (filter_states_) {
        if (!t_range_enumerate.Advance(1, filter_states_)) break;
      } else {
        if (!t_range_enumerate.Advance()) break;
      }
    } while (--num_candidates != 0);

    // In case threads are allocated from thread-pools, threads would persist,
    // and t_range_enumerate would not be destroyed: clear here for next
    // invocation.
    t_range_enumerate.Clear();
    return result;
  }

  const auto& command() const { return command_; }

  const auto& transition_system() const { return transition_system_; }

  std::size_t id() const { return id_; }

  std::size_t enumerated_candidates() const { return enumerated_candidates_; }

  void set_candidate_callback(CandidateCallback f) {
    candidate_callback_ = std::move(f);
  }

  void set_filter_states(std::function<bool(const RangeEnumerate::States&)> f) {
    filter_states_ = std::move(f);
  }

 private:
  friend class ParallelSolver<TransitionSystem>;

  auto& command() { return command_; }
  auto& transition_system() { return transition_system_; }

  ModelCheckerCommand<TransitionSystem> command_;
  TransitionSystem transition_system_;
  std::size_t id_;
  CandidateCallback candidate_callback_;
  std::function<bool(const RangeEnumerate::States&)> filter_states_;
  std::size_t enumerated_candidates_ = 0;
};

template <class TransitionSystem>
class ParallelSolver {
 public:
  template <class TSFactoryFunc>
  std::vector<synthesis::RangeEnumerate> operator()(
      const core::StateQueue<typename TransitionSystem::State>& start_states,
      TSFactoryFunc transition_system_factory) {
    std::vector<Solver<TransitionSystem>> solvers;
    std::vector<std::future<std::vector<synthesis::RangeEnumerate>>> futures;
    std::vector<synthesis::RangeEnumerate> result;

    // Reset any previous state; this implies this function can not be called
    // concurrently.
    g_range_enumerate.Clear();

    for (std::size_t i = 0; i < num_threads_; ++i) {
      solvers.emplace_back(transition_system_factory(start_states.front()), i);

      if (candidate_callback_) {
        solvers.back().set_candidate_callback(candidate_callback_);
      }

      if (filter_states_) {
        solvers.back().set_filter_states(filter_states_);
      }
    }

    std::size_t enumerated_candidates = 0;
    do {
      // Get copy of current g_range_enumerate, as other threads might start
      // modifying it while we launch new threads.
      auto cur_range_enumerate = g_range_enumerate;

      // Set all existing elements to max before new ones are discovered by
      // starting the threads.
      g_range_enumerate.SetMax();

      if (g_range_enumerate.combinations() == 0) {
        auto solns =
            solvers.front()(start_states, std::move(cur_range_enumerate), 1);
        std::move(solns.begin(), solns.end(), std::back_inserter(result));

        if (g_range_enumerate.combinations() <= 1) {
          // Fall back to just model check and show an error trace if the
          // current model is faulty.
          if (result.empty()) {
            InfoOut() << "Model is unique and faulty." << std::endl;
            solvers.front().command().eval().set_verbose_on_error(true);
            solvers.front().command()(start_states,
                                      &solvers.front().transition_system());
          } else {
            InfoOut() << "Model is unique and correct." << std::endl;
          }
          break;
        }
      } else {
        std::size_t per_thread_variants =
            (cur_range_enumerate.combinations() - enumerated_candidates) /
            num_threads_;
        if (per_thread_variants < min_per_thread_variants_) {
          per_thread_variants = min_per_thread_variants_;
        }

        for (std::size_t i = 0; i < solvers.size(); ++i) {
          std::size_t this_from_candidate =
              (enumerated_candidates + per_thread_variants * i);
          std::size_t this_to_candidate =
              this_from_candidate + per_thread_variants >
                      cur_range_enumerate.combinations()
                  ? cur_range_enumerate.combinations()
                  : this_from_candidate + per_thread_variants;
          InfoOut() << "Dispatching thread for candidates ["
                    << this_from_candidate << ", " << this_to_candidate
                    << ") ..." << std::endl;

          // mutable lambda, so that we move start_from, rather than copy.
          futures.emplace_back(std::async(std::launch::async, [
            &solver = solvers[i],
            &start_states,
            start_from = cur_range_enumerate,
            per_thread_variants
          ]() mutable {
            return solver(start_states, std::move(start_from),
                          per_thread_variants);
          }));

          if (!cur_range_enumerate.Advance(per_thread_variants)) break;
        }

        for (auto& future : futures) {
          auto solns = future.get();
          std::move(solns.begin(), solns.end(), std::back_inserter(result));
        }

        futures.clear();
      }

      enumerated_candidates = 0;
      for (const auto& solver : solvers) {
        enumerated_candidates += solver.enumerated_candidates();
      }

      InfoOut() << "Enumerated " << enumerated_candidates << " candidates of "
                << g_range_enumerate.combinations()
                << " discovered possible candidates." << std::endl;

      assert(enumerated_candidates <= g_range_enumerate.combinations());
    } while (g_range_enumerate.Advance());

    return result;
  }

  std::size_t num_threads() const { return num_threads_; }

  void set_num_threads(std::size_t val) { num_threads_ = val; }

  std::size_t min_per_thread_variants() const {
    return min_per_thread_variants_;
  }

  void set_min_per_thread_variants(std::size_t val) {
    min_per_thread_variants_ = val;
  }

  void set_candidate_callback(
      typename Solver<TransitionSystem>::CandidateCallback f) {
    candidate_callback_ = std::move(f);
  }

  void set_filter_states(std::function<bool(const RangeEnumerate::States&)> f) {
    filter_states_ = std::move(f);
  }

 private:
  std::size_t num_threads_ = 1;
  std::size_t min_per_thread_variants_ = 100;
  typename Solver<TransitionSystem>::CandidateCallback candidate_callback_;
  std::function<bool(const RangeEnumerate::States&)> filter_states_;
};

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
