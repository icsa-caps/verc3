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

#include <limits>
#include <mutex>
#include <sstream>
#include <stdexcept>

#include <gflags/gflags.h>
#include <gsl/gsl>

#include "verc3/synthesis/enumerate.hh"

DEFINE_uint64(synth_max_combinations, 10000000000,
              "Maximum permitted combinations in any RangeEnumerate");

namespace verc3 {
namespace synthesis {

// RangeEnumerate --------------------------------------------------------------

constexpr RangeEnumerate::ID RangeEnumerate::kInvalidID;

RangeEnumerate::ID RangeEnumerate::Extend(std::size_t range,
                                          std::string label) {
  Expects(range > 0);
  std::lock_guard<std::mutex> lock(extend_mutex_);

  if (label_map_.find(label) != label_map_.end()) {
    throw std::domain_error("label exists: " + label);
  }

  if (combinations_ == 0) {
    combinations_ = range;
  } else {
    if (std::numeric_limits<decltype(combinations_)>::max() / range <
        combinations_) {
      throw std::overflow_error("RangeEnumerate combinations overflow");
    }

    combinations_ *= range;
  }

  if (combinations_ > FLAGS_synth_max_combinations) {
    throw std::overflow_error("RangeEnumerate combinations global limit");
  }

  values_.emplace_back(0, range, std::move(label));
  // label invalid from here
  label_map_[values_.back().label()] = &values_.back();
  return values_.size() - 1;
}

std::ostream& operator<<(std::ostream& os, const RangeEnumerate& v) {
  os << "{" << std::endl;
  for (std::size_t i = 0; i < v.values().size(); ++i) {
    const auto& s = v.values()[i];
    if (i != 0) os << ", " << std::endl;
    os << "  '" << s.label() << "': " << s.value();
  }
  os << std::endl << "}";
  return os;
}

// RangeEnumerateMatcher -------------------------------------------------------

bool RangeEnumerateMatcher::Insert(const RangeEnumerate& range_enum,
                                   std::size_t max_nonwildcard) {
  std::size_t bit_pattern;
  std::size_t num_nonwildcards =
      AsBitPattern(range_enum, &bit_pattern, nullptr, nullptr);

  if (max_nonwildcard < num_nonwildcards) {
    return false;
  }

  if (bit_pattern == 0) {
    // This is probably unintended usage, so we'll forbid it.
    throw std::logic_error("Only wildcards not supported!");
  }

  std::lock_guard<std::shared_timed_mutex> exclusive_lock(mutex_);

  auto entry = patterns_[bit_pattern].emplace(RangeEnumerateToVector(
      range_enum, bit_pattern, num_nonwildcards, nullptr));

  if (entry.second) {
    ++size_;
  }

  return true;
}

RangeEnumerate::ID RangeEnumerateMatcher::Match(
    const RangeEnumerate& range_enum) const {
  std::size_t bit_nonpattern = 0;
  std::size_t lower_bound = 0;
  std::size_t upper_bound = 0;

  if (!AsBitPattern(range_enum, &bit_nonpattern, &lower_bound, &upper_bound)) {
    // If range_enum only contains wildcards, this is always a non-match.
    return RangeEnumerate::kInvalidID;
  }

  std::shared_lock<std::shared_timed_mutex> shared_lock(mutex_);

  for (auto patterns_it = patterns_.lower_bound(lower_bound);
       patterns_it != patterns_.end() && patterns_it->first <= upper_bound;
       ++patterns_it) {
    std::size_t bit_pattern = patterns_it->first;

    // Disallow matching of range_enum if it has wildcards in positions this
    // pattern has concrete values, or is in fact shorter than the positions
    // this pattern would check.
    if ((bit_pattern & bit_nonpattern) != bit_pattern) continue;

    const auto& sparse_patterns = patterns_it->second;
    std::size_t first_id_set = RangeEnumerate::kInvalidID;
    auto entry =
        RangeEnumerateToVector(range_enum, bit_pattern,
                               sparse_patterns.begin()->size(), &first_id_set);
    auto match = sparse_patterns.find(entry);
    if (match != sparse_patterns.end()) {
      return first_id_set;
    }
  }

  return RangeEnumerate::kInvalidID;
}

// LambdaOptionsBase -----------------------------------------------------------

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
