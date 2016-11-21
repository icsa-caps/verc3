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

#include <gsl/gsl>

#include "verc3/command.hh"
#include "verc3/io.hh"
#include "verc3/synthesis/enumerate.hh"

namespace verc3 {
namespace synthesis {

extern synthesis::RangeEnumerate g_range_enumerate;
extern thread_local synthesis::RangeEnumerate t_range_enumerate;

namespace detail {

template <class State>
class NoUnknowns : public core::Property<State> {
 public:
  explicit NoUnknowns() : core::Property<State>("NoUnknowns") {}

  typename core::Property<State>::Ptr Clone() const override {
    return std::make_unique<NoUnknowns>(*this);
  }

  void Reset() override { has_unknowns_ = false; }

  bool Invariant(const State& state) const override { return true; }

  void Next(const State& state, const core::StateMap<State>& next_states,
            const core::Unknowns& unknowns) override {
    has_unknowns_ = has_unknowns_ || !unknowns.empty();
  }

  bool IsSatisfied(bool verbose_on_error = true) const override {
    if (has_unknowns_) {
      throw core::Unknown();
    }

    return true;
  }

 private:
  bool has_unknowns_ = false;
};

}  // namespace detail

template <class TransitionSystem>
class ParallelSolver;

template <class TransitionSystem>
class Solver {
 public:
  using ErrorHashTrace =
      typename core::EvalBase<TransitionSystem>::ErrorHashTrace;

  typedef std::function<void(const Solver&, const RangeEnumerate&,
                             const std::exception*)>
      CandidateCallback;

  explicit Solver(TransitionSystem&& ts, std::size_t id = 0)
      : transition_system_(std::move(ts)), id_(id) {
    command_.eval().set_verbose_on_error(false);
    command_.eval().unset_monitor();

    // Require tracking of Unknowns encountered.
    auto no_unknowns = std::make_unique<
        detail::NoUnknowns<typename TransitionSystem::State>>();
    no_unknowns_ = no_unknowns.get();
    transition_system_.Register(std::move(no_unknowns));
  }

  auto operator()(
      const core::StateQueue<typename TransitionSystem::State>& start_states,
      synthesis::RangeEnumerate start, std::size_t num_candidates,
      std::size_t range_stride = 1) {
    // assume t_range_enumerate is thread_local!
    Expects(t_range_enumerate.values().empty());
    Expects(num_candidates > 0);
    Expects(range_stride > 0);
    std::vector<synthesis::RangeEnumerate> result;
    t_range_enumerate = std::move(start);

    // Obtain target RangeEnumerate: since we may skip certain candidates as
    // they are skipped in Advance (via validate_range_enum_), we cannot rely
    // in evaluated_candidates to match num_candidates upon completion.
    auto end = t_range_enumerate;
    if (!end.Advance(num_candidates - 1)) {
      end.SetMax();
    }

    do {
      try {
        if (command_(start_states, &transition_system_) == 0) {
          std::cout << "solution[" << id_ << "][" << evaluated_candidates_
                    << "] = " << t_range_enumerate << std::endl;
          result.push_back(t_range_enumerate);
        }

        if (candidate_callback_) {
          candidate_callback_(*this, t_range_enumerate, nullptr);
        }
      } catch (const ErrorHashTrace& e) {
        // Need to reset NoUnknowns instance, to avoid candidate_callbacks that
        // check for property violations to use it.
        no_unknowns_->Reset();

        if (pruning_patterns_ != nullptr) {
          pruning_patterns_->Insert(t_range_enumerate);
        }

        if (candidate_callback_) {
          candidate_callback_(*this, t_range_enumerate, &e);
        }
      } catch (const core::Unknown& u) {
        no_unknowns_->Reset();
        if (candidate_callback_) {
          candidate_callback_(*this, t_range_enumerate, &u);
        }
      }

      ++evaluated_candidates_;

      // Advance thread-local RangeEnumerate; if overflow, we are done.
      if (validate_range_enum_) {
        if (!t_range_enumerate.Advance(range_stride, validate_range_enum_))
          break;
      } else {
        if (!t_range_enumerate.Advance(range_stride)) break;
      }
    } while (t_range_enumerate <= end);

    // In case threads are allocated from thread-pools, threads would persist,
    // and t_range_enumerate would not be destroyed: clear here for next
    // invocation.
    t_range_enumerate.Clear();
    return result;
  }

  const auto& command() const { return command_; }

  const auto& transition_system() const { return transition_system_; }

  std::size_t id() const { return id_; }

  std::size_t evaluated_candidates() const { return evaluated_candidates_; }

  void set_candidate_callback(CandidateCallback f) {
    candidate_callback_ = std::move(f);
  }

  void set_validate_range_enum(
      std::function<RangeEnumerate::ID(const RangeEnumerate&)> f) {
    validate_range_enum_ = std::move(f);
  }

 private:
  friend class ParallelSolver<TransitionSystem>;

  auto& command() { return command_; }
  auto& transition_system() { return transition_system_; }

  void set_pruning_patterns(RangeEnumerateMatcher* pp) {
    pruning_patterns_ = pp;
  }

  ModelCheckerCommand<TransitionSystem> command_;
  TransitionSystem transition_system_;
  std::size_t id_;

  detail::NoUnknowns<typename TransitionSystem::State>* no_unknowns_;

  CandidateCallback candidate_callback_;
  std::function<RangeEnumerate::ID(const RangeEnumerate&)> validate_range_enum_;

  std::size_t evaluated_candidates_ = 0;

  //! Pruning patterns shared with other threads.
  RangeEnumerateMatcher* pruning_patterns_ = nullptr;
};

template <class TransitionSystem>
class ParallelSolver {
 public:
  template <class TSFactoryFunc>
  std::vector<synthesis::RangeEnumerate> operator()(
      const core::StateQueue<typename TransitionSystem::State>& start_states,
      TSFactoryFunc transition_system_factory,
      std::size_t target_candidates = 0, bool reset_global_state = true) {
    std::vector<Solver<TransitionSystem>> solvers;
    std::vector<std::future<std::vector<synthesis::RangeEnumerate>>> futures;
    std::vector<synthesis::RangeEnumerate> result;

    if (reset_global_state) {
      // Reset any previous state; this implies this function can not be called
      // concurrently.
      g_range_enumerate.Clear();
      LambdaOptionsBase::UnregisterAll();
    }

    for (std::size_t i = 0; i < num_threads_; ++i) {
      solvers.emplace_back(transition_system_factory(start_states.front()), i);

      if (candidate_callback_) {
        solvers.back().set_candidate_callback(candidate_callback_);
      }

      if (validate_range_enum_) {
        Expects(!pruning_enabled_);
        solvers.back().set_validate_range_enum(validate_range_enum_);
      }

      if (pruning_enabled_) {
        solvers.back().set_validate_range_enum(
            [this](const RangeEnumerate& range_enum) {
              for (const auto& v : range_enum.values()) {
                if (v.value() == pruning_patterns_.wildcard()) {
                  // TODO: explain policy
                  return 0;
                }
              }

              return pruning_patterns_.Match(range_enum);
            });

        solvers.back().set_pruning_patterns(&pruning_patterns_);
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

        ++enumerated_candidates;
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
                    << this_from_candidate << ", " << this_to_candidate << ") ";

          std::size_t range_stride = 1;
          if (target_candidates != 0) {
            range_stride = g_range_enumerate.combinations() / target_candidates;
            std::cout << "with stride " << range_stride << " ";
          }

          std::cout << "..." << std::endl;

          // mutable lambda, so that we move start_from, rather than copy.
          futures.emplace_back(std::async(std::launch::async, [
            &solver = solvers[i],
            &start_states,
            start_from = cur_range_enumerate,
            per_thread_variants,
            range_stride
          ]() mutable {
            return solver(start_states, std::move(start_from),
                          per_thread_variants, range_stride);
          }));

          if (!cur_range_enumerate.Advance(per_thread_variants)) break;
        }

        enumerated_candidates = cur_range_enumerate.combinations();

        for (auto& future : futures) {
          auto solns = future.get();
          std::move(solns.begin(), solns.end(), std::back_inserter(result));
        }

        futures.clear();
      }

      std::size_t evaluated_candidates = 0;
      for (const auto& solver : solvers) {
        evaluated_candidates += solver.evaluated_candidates();
      }

      InfoOut() << "Evaluated " << evaluated_candidates << " candidates of "
                << g_range_enumerate.combinations()
                << " discovered possible candidates." << std::endl;

      Ensures(enumerated_candidates <= g_range_enumerate.combinations());
      Ensures(evaluated_candidates <= g_range_enumerate.combinations());
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

  void set_validate_range_enum(
      std::function<RangeEnumerate::ID(const RangeEnumerate&)> f) {
    validate_range_enum_ = std::move(f);
  }

  bool pruning_enabled() const { return pruning_enabled_; }

  void set_pruning_enabled(bool val = true) { pruning_enabled_ = val; }

  const RangeEnumerateMatcher& pruning_patterns() const {
    return pruning_patterns_;
  }

 private:
  std::size_t num_threads_ = 1;
  std::size_t min_per_thread_variants_ = 100;
  typename Solver<TransitionSystem>::CandidateCallback candidate_callback_;
  std::function<RangeEnumerate::ID(const RangeEnumerate&)> validate_range_enum_;

  RangeEnumerateMatcher pruning_patterns_{0};
  bool pruning_enabled_ = false;
};

}  // namespace synthesis
}  // namespace verc3

#define SYNTHESIZE_DECL(opt, local_name)                   \
  static decltype(opt) local_name(#local_name, opt, true); \
  local_name.Register(&verc3::synthesis::g_range_enumerate);

#define SYNTHESIZE_CALL(local_name, ...) \
  local_name.GetCurrent(verc3::synthesis::t_range_enumerate, true)(__VA_ARGS__);

#define SYNTHESIZE(opt, local_name, ...)     \
  do {                                       \
    SYNTHESIZE_DECL(opt, local_name);        \
    SYNTHESIZE_CALL(local_name, __VA_ARGS__) \
  } while (0);

#endif /* VERC3_SYNTHESIS_SOLVER_HH_ */

/* vim: set ts=2 sts=2 sw=2 et : */
