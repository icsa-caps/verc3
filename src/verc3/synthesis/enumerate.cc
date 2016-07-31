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

#include <mutex>

#include "verc3/synthesis/enumerate.hh"

namespace verc3 {
namespace synthesis {

constexpr std::size_t RangeEnumerate::kInvalidID;
std::vector<LambdaOptionsBase*> LambdaOptionsBase::static_registry_;
std::mutex LambdaOptionsBase::static_registry_mutex_;

void LambdaOptionsBase::StaticRegister(LambdaOptionsBase* instance) {
  std::lock_guard<std::mutex> lock(LambdaOptionsBase::static_registry_mutex_);
  LambdaOptionsBase::static_registry_.push_back(instance);
}

void LambdaOptionsBase::UnregisterAll() {
  for (auto instance : LambdaOptionsBase::static_registry_) {
    instance->id_ = RangeEnumerate::kInvalidID;
  }
}

}  // namespace synthesis
}  // namespace verc3

/* vim: set ts=2 sts=2 sw=2 et : */
