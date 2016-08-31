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

#include <cassert>
#include <mutex>
#include <sstream>
#include <stdexcept>

#include "verc3/synthesis/enumerate.hh"

namespace verc3 {
namespace synthesis {

// RangeEnumerate --------------------------------------------------------------

constexpr RangeEnumerate::ID RangeEnumerate::kInvalidID;

RangeEnumerate::ID RangeEnumerate::Extend(std::size_t range,
                                          std::string label) {
  assert(range > 0);
  std::lock_guard<std::mutex> lock(extend_mutex_);

  if (label_map_.find(label) != label_map_.end()) {
    throw std::domain_error("label exists: " + label);
  }

  if (combinations_ == 0) {
    combinations_ = range;
  } else {
    combinations_ *= range;
  }

  states_.emplace_back(0, range, std::move(label));
  // label invalid from here
  label_map_[states_.back().label()] = &states_.back();
  return states_.size() - 1;
}

std::ostream& operator<<(std::ostream& os, const RangeEnumerate& v) {
  os << "{" << std::endl;
  for (std::size_t i = 0; i < v.states().size(); ++i) {
    const auto& s = v.states()[i];
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

  if (patterns_.find(bit_pattern) == patterns_.end()) {
    patterns_[bit_pattern].resize(num_nonwildcards);
  }

  auto& sparse_patterns = patterns_[bit_pattern];
  auto sparse_patterns_it = sparse_patterns.begin();

  // Convert range_enum to sparse representation; avoids accessing each
  // element again by reusing bit_pattern. It goes without saying that
  // bit_pattern should no longer be used to index patterns_.
  std::size_t last_val = wildcard_;
  for (std::size_t i = 0; bit_pattern != 0 && i < range_enum.states().size();
       ++i, bit_pattern >>= 1) {
    if (bit_pattern & 1) {
      auto value = range_enum.states()[i].value();
      assert(value != wildcard_);

      if (last_val != wildcard_) {
        // Not first valid value.
        // Set current position of sparse_patterns and advance iterator.
        (*sparse_patterns_it++)[last_val].insert(value);
        assert(sparse_patterns_it != sparse_patterns.end());
      }

      last_val = value;
    } else {
      assert(range_enum.states()[i].value() == wildcard_);
    }
  }

  // This assert will fail if we permit bit_pattern == 0.
  assert(last_val != wildcard_);

  // Just allocate map entry as last element sequenced before none.
  (*sparse_patterns_it++)[last_val];
  assert(sparse_patterns_it == sparse_patterns.end());

  return true;
}

RangeEnumerate::ID RangeEnumerateMatcher::Match(
    const RangeEnumerate& range_enum) const {
  std::size_t bit_nonpattern, lower_bound, upper_bound;
  AsBitPattern(range_enum, &bit_nonpattern, &lower_bound, &upper_bound);

  std::shared_lock<std::shared_timed_mutex> shared_lock(mutex_);

  for (auto patterns_it = patterns_.lower_bound(lower_bound);
       patterns_it != patterns_.end() && patterns_it->first <= upper_bound;
       ++patterns_it) {
    std::size_t bit_pattern = patterns_it->first;

    // Disallow matching of range_enum if it has wildcards in positions this
    // pattern has concrete values, or is in fact shorter than the positions
    // this pattern would check.
    if ((bit_pattern & bit_nonpattern) != bit_pattern) continue;

    auto& sparse_patterns = patterns_it->second;
    auto sparse_patterns_it = sparse_patterns.begin();

    std::size_t last_val = wildcard_;
    std::size_t least_significant_id = RangeEnumerate::kInvalidID;
    for (std::size_t i = 0; bit_pattern != 0 && i < range_enum.states().size();
         ++i, bit_pattern >>= 1) {
      if (bit_pattern & 1) {
        if (least_significant_id == RangeEnumerate::kInvalidID) {
          least_significant_id = i;
        }

        auto value = range_enum.states()[i].value();
        assert(value != wildcard_);

        if (last_val != wildcard_) {
          auto next_set = sparse_patterns_it->find(last_val);
          if (next_set == sparse_patterns_it->end() ||
              next_set->second.find(value) == next_set->second.end()) {
            // Mismatch
            goto mismatch;
          }

          ++sparse_patterns_it;
          assert(sparse_patterns_it != sparse_patterns.end());
        }

        last_val = value;
      }
    }

    assert(last_val != wildcard_);
    // Have to test last one; this is necessary for cases where there is only
    // 1 concrete value and the rest wildcards in pattern.
    if (sparse_patterns_it->find(last_val) != sparse_patterns_it->end()) {
      // All matched so far: this is a match!
      assert(least_significant_id != RangeEnumerate::kInvalidID);
      return least_significant_id;
    }

    ++sparse_patterns_it;
    assert(sparse_patterns_it == sparse_patterns.end());

  mismatch:;
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
