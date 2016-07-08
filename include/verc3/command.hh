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

#ifndef VERC3_COMMAND_HH_
#define VERC3_COMMAND_HH_

#include <iostream>
#include <memory>

#include <gflags/gflags.h>

#include "verc3/core/model_checking.hh"
#include "verc3/debug.hh"
#include "verc3/io.hh"

DECLARE_string(command_eval);

namespace verc3 {

template <class TransitionSystem>
class ModelCheckerCommand {
 public:
  typedef core::EvalBase<TransitionSystem> EvalBase;

  explicit ModelCheckerCommand() {
    if (FLAGS_command_eval == "bfs_hashing") {
      InfoOut() << "Instantiating evaluation backend: Eval_BFS_Hashing"
                << std::endl;
      eval_.reset(new core::Eval_BFS_Hashing<TransitionSystem>());
    } else {  // "bfs"
      InfoOut() << "Instantiating evaluation backend: Eval_BFS" << std::endl;
      eval_.reset(new core::Eval_BFS<TransitionSystem>());
    }

    eval_->set_monitor([count = 0](
        const EvalBase& mc,
        core::StateQueue<typename EvalBase::State>* accept) mutable {
      if (count++ % 10000 == 0) {
        std::cout << "... visited states: " << mc.num_visited_states()
                  << " | queued: " << mc.num_queued_states() << std::endl;
      }
      return true;
    });
  }

  int operator()(
      const core::StateQueue<typename TransitionSystem::State>& start_states,
      TransitionSystem* ts) {
    try {
      eval_->Evaluate(start_states, ts);
    } catch (const typename EvalBase::ExceptionTrace& trace) {
      std::cout << std::endl;
      PrintTraceDiff(trace.trace(),
                     [](const typename EvalBase::Trace::value_type& state_rule,
                        std::ostream& os) { os << state_rule.first; },
                     [](const typename EvalBase::Trace::value_type& state_rule,
                        std::ostream& os) {
                       if (!state_rule.second.empty()) {
                         os << kColGRN << "================( "
                            << state_rule.second << " )===>" << kColRst
                            << std::endl;
                       }
                     },
                     std::cout);

      std::cout << kColRED << "===> VERIFICATION FAILED ("
                << trace.trace().size() << " steps): " << trace.error().what()
                << kColRst << std::endl;
      std::cout << std::endl;
      return 1;
    } catch (const std::bad_alloc& e) {
      ErrOut() << "Out of memory!" << std::endl;
      return 42;
    }

    for (const auto& prop : ts->properties()) {
      if (!prop->IsSatisfied(eval_->trace_on_error())) {
        return 1;
      }
    }

    std::cout << kColBLU << ">> VERIFIED | "
              << "visited states: " << eval_->num_visited_states() << kColRst
              << std::endl;
    return 0;
  }

  EvalBase* eval() { return eval_.get(); }

 private:
  std::unique_ptr<EvalBase> eval_;
};

}  // namespace verc3

#endif /* VERC3_COMMAND_HH_ */

/* vim: set ts=2 sts=2 sw=2 et : */
