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

#include <glog/logging.h>

#include "verc3/core/model_checking.hh"
#include "verc3/debug.hh"

namespace verc3 {

template <class TransitionSystem>
class ModelCheckerCommand {
 public:
  typedef core::EvalBase<TransitionSystem> EvalBase;

  explicit ModelCheckerCommand(int argc, char* argv[]) {
    if (argc > 1 && std::string(argv[1]) == "hashing") {
      LOG(INFO) << "Using evaluation backend: Eval_BFS_Hashing";
      eval_.reset(new core::Eval_BFS_Hashing<TransitionSystem>());
    } else {
      LOG(INFO) << "Using evaluation backend: Eval_BFS";
      eval_.reset(new core::Eval_BFS<TransitionSystem>());
    }

    eval_->set_monitor([](const EvalBase& mc,
                          core::StateQueue<typename EvalBase::State>* accept) {
      VLOG_EVERY_N(0, 10000)
          << "... visited states: " << mc.num_visited_states()
          << " | queued: " << mc.num_queued_states();
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
                         os << "\e[1;32m================( " << state_rule.second
                            << " )===>\e[0m" << std::endl;
                       }
                     },
                     std::cout);

      std::cout << "\e[1;31m===> VERIFICATION FAILED (" << trace.trace().size()
                << " steps): " << trace.error().what() << "\e[0m" << std::endl;
      std::cout << std::endl;
      return 1;
    } catch (const std::bad_alloc& e) {
      LOG(ERROR) << "Out of memory!";
      return 42;
    }

    for (const auto& prop : ts->properties()) {
      if (!prop->IsSatisfied()) {
        return 1;
      }
    }

    VLOG(0) << "DONE @ "
            << "visited states: " << eval_->num_visited_states();
    return 0;
  }

  EvalBase* eval() { return eval_.get(); }

 private:
  std::unique_ptr<EvalBase> eval_;
};

}  // namespace verc3

#endif /* VERC3_COMMAND_HH_ */

/* vim: set ts=2 sts=2 sw=2 et : */
