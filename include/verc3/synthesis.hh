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

#ifndef VERC3_SYNTHESIS_HH_
#define VERC3_SYNTHESIS_HH_

#include <cassert>
#include <cstddef>
#include <functional>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace verc3 {

class RangeEnumerate {
 public:
  typedef std::size_t ID;
  static constexpr std::size_t kInvalidID = 0;

  struct State {
    explicit State(std::size_t v, std::size_t r, std::string l)
        : value(v), range(r), label(std::move(l)) {}

    std::size_t value;
    std::size_t range;
    std::string label;
  };

  ID Extend(std::size_t range, std::string label) {
    assert(label_map_.find(label) == label_map_.end());

    if (combinations_ == 0) {
      combinations_ = range;
    } else {
      combinations_ *= range;
    }

    states_.emplace_back(new State(0, range, std::move(label)));
    // label invalid from here
    label_map_[states_.back()->label] = states_.back().get();
    return states_.size();
  }

  void Reset() {
    for (auto& p : states_) {
      p->value = 0;
    }
  }

  bool Next() {
    for (auto& p : states_) {
      if (++p->value < p->range) {
        return true;
      } else {
        p->value = 0;
      }
    }

    return false;
  }

  const State& GetState(ID id) const {
    assert(id != kInvalidID);
    assert(id - 1 < states_.size());
    return *states_[id - 1];
  }

  const State& GetState(const std::string& label) const {
    auto it = label_map_.find(label);

    if (it == label_map_.end()) {
      throw std::domain_error(label);
    }

    return *it->second;
  }

  std::size_t GetValue(ID id) const {
    assert(id != kInvalidID);
    assert(id - 1 < states_.size());
    return states_[id - 1]->value;
  }

  auto& states() const { return states_; }

  std::size_t combinations() const { return combinations_; }

 private:
  /**
   * State container; need to use unique_ptr, so that future insertions do not
   * invalidate references to State from label_map_.
   */
  std::vector<std::unique_ptr<State>> states_;

  /**
   * Map of labels to state.
   */
  std::unordered_map<std::string, const State*> label_map_;

  /**
   * Current maximum combinations.
   */
  std::size_t combinations_ = 0;
};

/**
 * Writes RangeEnumerate current state in JSON format.
 */
inline std::ostream& operator<<(std::ostream& os, const RangeEnumerate& v) {
  os << "{" << std::endl;
  for (std::size_t i = 0; i < v.states().size(); ++i) {
    const auto& s = *v.states()[i];
    if (i != 0) os << ", " << std::endl;
    os << "  '" << s.label << "': " << s.value;
  }
  os << std::endl << "}";
  return os;
}

// static Options options{ ... };
template <class T>
class LambdaOptions {
 public:
  typedef std::function<T> Option;

  template <typename... Ts>
  explicit LambdaOptions(std::string label, Ts&&... ts)
      : label_(std::move(label)), opts_{std::forward<Ts>(ts)...} {}

  auto& operator[](RangeEnumerate& range_enumerate) {
    if (id_ == RangeEnumerate::kInvalidID) {
      id_ = range_enumerate.Extend(opts_.size(), label_);
    }

    return opts_[range_enumerate.GetValue(id_)];
  }

  const std::string& label() const { return label_; }

  RangeEnumerate::ID id() const { return id_; }

 private:
  std::string label_;
  std::vector<Option> opts_;
  RangeEnumerate::ID id_ = RangeEnumerate::kInvalidID;
};

}  // namespace verc3

#endif /* VERC3_SYNTHESIS_HH_ */

/* vim: set ts=2 sts=2 sw=2 et : */
