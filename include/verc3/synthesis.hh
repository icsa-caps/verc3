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

#include <atomic>
#include <cassert>
#include <deque>
#include <functional>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace verc3 {

/**
 * Range enumerate.
 *
 * Not thread-safe, unless explicitly specified (see Extend).
 */
class RangeEnumerate {
 public:
  typedef std::size_t ID;
  static constexpr std::size_t kInvalidID = 0;

  class State {
   public:
    explicit State(std::size_t v, std::size_t r, std::string l)
        : value(v), range_(r), label_(std::move(l)) {}

    auto range() const { return range_; }

    auto label() const { return label_; }

    bool operator==(const State& rhs) const {
      return value == rhs.value && range_ == rhs.range_;
    }

    bool operator!=(const State& rhs) const { return !(*this == rhs); }

   public:
    std::size_t value;

   private:
    std::size_t range_;
    std::string label_;
  };

  RangeEnumerate() : combinations_(0) {}

  RangeEnumerate(const RangeEnumerate& rhs)
      : states_(rhs.states_), combinations_(rhs.combinations_) {
    // Need to rebuild label_map_, as the pointers need to be owned by this.
    for (const auto& state : states_) {
      label_map_[state.label()] = &state;
    }
  }

  RangeEnumerate(RangeEnumerate&& rhs)
      : states_(std::move(rhs.states_)),
        label_map_(std::move(rhs.label_map_)),
        combinations_(rhs.combinations_) {
    rhs.combinations_ = 0;
    assert(rhs.states_.empty());
    assert(rhs.label_map_.empty());
    assert(states_.empty() ||
           label_map_[states_.front().label()] == &states_.front());
  }

  RangeEnumerate& operator=(const RangeEnumerate& rhs) {
    states_ = rhs.states_;
    combinations_ = rhs.combinations_;
    label_map_.clear();
    // Need to rebuild label_map_, as the pointers need to be owned by this.
    for (const auto& state : states_) {
      label_map_[state.label()] = &state;
    }
    return *this;
  }

  RangeEnumerate& operator=(RangeEnumerate&& rhs) {
    states_ = std::move(rhs.states_);
    label_map_ = std::move(rhs.label_map_);
    combinations_ = rhs.combinations_;
    rhs.combinations_ = 0;
    assert(rhs.states_.empty());
    assert(rhs.label_map_.empty());
    assert(states_.empty() ||
           label_map_[states_.front().label()] == &states_.front());
    return *this;
  }

  bool operator==(const RangeEnumerate& rhs) const {
    return states_ == rhs.states_;
  }

  bool operator!=(const RangeEnumerate& rhs) const { return !(*this == rhs); }

  void Clear() {
    states_.clear();
    label_map_.clear();
    combinations_ = 0;
  }

  /**
   * Extends the range.
   *
   * Is thread-safe, i.e. concurrent extensions are permitted; calling any
   * other functions concurrently with Extend, however, is undefined.
   */
  ID Extend(std::size_t range, std::string label) {
    assert(range > 0);
    std::lock_guard<std::mutex> lock(extend_mutex_);

    if (label_map_.find(label) != label_map_.end()) {
      return kInvalidID;
    }

    if (combinations_ == 0) {
      combinations_ = range;
    } else {
      combinations_ *= range;
    }

    states_.emplace_back(0, range, std::move(label));
    // label invalid from here
    label_map_[states_.back().label()] = &states_.back();
    return states_.size();
  }

  void SetMin() {
    for (auto& p : states_) {
      p.value = 0;
    }
  }

  void SetMax() {
    for (auto& p : states_) {
      p.value = p.range() - 1;
    }
  }

  void SetFrom(const RangeEnumerate& other) {
    for (std::size_t i = 0; i < other.states_.size() && i < states_.size();
         ++i) {
      assert(states_[i].range() == other.states_[i].range());
      states_[i].value = other.states_[i].value;
    }
  }

  State* GetMostSignificant() {
    if (states_.empty()) {
      return nullptr;
    }

    return &states_.back();
  }

  bool Next() {
    for (auto& p : states_) {
      if (++p.value < p.range()) {
        return true;
      } else {
        p.value = 0;
      }
    }

    return false;
  }

  bool IsValid(ID id) const {
    return id != kInvalidID && id - 1 < states_.size();
  }

  const State& GetState(ID id) const {
    if (!IsValid(id)) {
      throw std::domain_error("invalid ID");
    }

    return states_[id - 1];
  }

  const State& GetState(const std::string& label) const {
    auto it = label_map_.find(label);

    if (it == label_map_.end()) {
      throw std::domain_error(label);
    }

    return *it->second;
  }

  std::size_t operator[](ID id) const { return GetState(id).value; }

  const auto& states() const { return states_; }

  std::size_t combinations() const { return combinations_; }

 private:
  /**
   * State container; deque guarantees that future insertions at the end do not
   * invalidate references/pointers to State from label_map_.
   */
  std::deque<State> states_;

  /**
   * Map of labels to state.
   */
  std::unordered_map<std::string, const State*> label_map_;

  /**
   * Current maximum combinations.
   */
  std::size_t combinations_;

  /**
   * Mutex for Extend.
   */
  std::mutex extend_mutex_;
};

/**
 * Writes RangeEnumerate current state in JSON format.
 */
inline std::ostream& operator<<(std::ostream& os, const RangeEnumerate& v) {
  os << "{" << std::endl;
  for (std::size_t i = 0; i < v.states().size(); ++i) {
    const auto& s = v.states()[i];
    if (i != 0) os << ", " << std::endl;
    os << "  '" << s.label() << "': " << s.value;
  }
  os << std::endl << "}";
  return os;
}

/**
 * Collection of lambdas to be chosen from via RangeEnumerate.
 *
 * Provides thread-safe interface as well as safe for use as function local
 * static (implicit, as C++11 guarantees thread-safe static initialization if
 * constructor does not access other shared data).
 */
template <class T>
class LambdaOptions {
 public:
  typedef std::function<T> Option;

  LambdaOptions(const LambdaOptions& rhs) = delete;

  LambdaOptions(LambdaOptions&& rhs) = default;

  explicit LambdaOptions(std::string label, const LambdaOptions& copy_from)
      : label_(std::move(label)),
        opts_(copy_from.opts_),
        id_(RangeEnumerate::kInvalidID) {}

  explicit LambdaOptions(std::string label, std::vector<Option> opts)
      : label_(std::move(label)),
        opts_(std::move(opts)),
        id_(RangeEnumerate::kInvalidID) {}

  void Register(RangeEnumerate* range_enumerate) {
    if (id_.load(std::memory_order_acquire) == RangeEnumerate::kInvalidID) {
      // Extend is thread-safe.
      auto new_id = range_enumerate->Extend(opts_.size(), label_);
      if (new_id != RangeEnumerate::kInvalidID) {
        id_.store(new_id, std::memory_order_release);
      }
    }
  }

  auto& GetCurrent(const RangeEnumerate& range_enumerate,
                   bool permit_invalid = false) const {
    if (permit_invalid &&
        !range_enumerate.IsValid(id_.load(std::memory_order_acquire))) {
      return opts_.front();
    }

    return opts_[range_enumerate[id_.load(std::memory_order_acquire)]];
  }

  auto& operator[](RangeEnumerate& range_enumerate) {
    Register(&range_enumerate);
    return GetCurrent(range_enumerate);
  }

  const std::string& label() const { return label_; }

  RangeEnumerate::ID id() const { return id_; }

 private:
  std::string label_;
  std::vector<Option> opts_;
  std::atomic<RangeEnumerate::ID> id_;
};

}  // namespace verc3

#endif /* VERC3_SYNTHESIS_HH_ */

/* vim: set ts=2 sts=2 sw=2 et : */
