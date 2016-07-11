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

#ifndef VERC3_SYNTHESIS_ENUMERATE_HH_
#define VERC3_SYNTHESIS_ENUMERATE_HH_

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
namespace synthesis {

/**
 * Range enumerate.
 *
 * @remark[thread-safety] Not thread-safe, unless explicitly specified (see
 * RangeEnumerate::Extend).
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

  /**
   * State container; deque guarantees that future insertions at the end do not
   * invalidate references/pointers to State from label_map_.
   */
  typedef std::deque<State> States;

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
   * @remark[thread-safety] Multiple functions may call this function
   * concurrently. Calling any other function concurrently with Extend,
   * however, is undefined.
   *
   * @param range The range to be extended by.
   * @param label The associated label.
   * @return A unique ID to refer to the current value.
   * @throw std::domain_error If the label already exists.
   */
  ID Extend(std::size_t range, std::string label) {
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

  bool Advance(std::size_t count = 1) {
    return Advance(count, [](const States& states) { return true; });
  }

  /**
   * Advances the current state to yield the next enumeration.
   *
   * @param count Increase by count.
   * @param filter_states Filters the resulting States, and if filter_states
   *        returns false, advances again with same count. Does not apply
   *        filter_states if overflow occurred (avoid possible non-termination).
   * @return true if no overflow occurred, false if overflow occurred.
   */
  template <class FilterFunc>
  bool Advance(std::size_t count, FilterFunc filter_states) {
    std::size_t cur_count = count;
    for (auto& p : states_) {
      p.value += cur_count;

      if (p.value < p.range()) {
        if (!filter_states(states_)) {
          // current states_ not valid, continue advancing.
          return Advance(count, filter_states);
        }

        // no carry
        return true;
      }

      // carry
      cur_count = p.value / p.range();
      p.value %= p.range();
    }

    return false;
  }

  bool IsValid(ID id) const {
    return id != kInvalidID && id - 1 < states_.size();
  }

  const State& GetState(ID id) const {
    if (!IsValid(id)) {
      throw std::out_of_range("invalid ID");
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
  States states_;

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
 * @remark[thread-safety] See individual function description. Safe for use as
 * function local static (implicit, as C++11 guarantees thread-safe static
 * initialization if constructor does not access other shared data).
 */
template <class T>
class LambdaOptions {
 public:
  typedef std::function<T> Option;

  LambdaOptions(const LambdaOptions& rhs) = delete;

  LambdaOptions(LambdaOptions&& rhs)
      : label_(std::move(rhs.label_)),
        opts_(std::move(rhs.opts_)),
        id_(rhs.id_) {
    rhs.id_ = RangeEnumerate::kInvalidID;
  }

  explicit LambdaOptions(std::string label, const LambdaOptions& copy_from)
      : label_(std::move(label)),
        opts_(copy_from.opts_),
        id_(RangeEnumerate::kInvalidID) {}

  explicit LambdaOptions(std::string label, std::vector<Option> opts)
      : label_(std::move(label)),
        opts_(std::move(opts)),
        id_(RangeEnumerate::kInvalidID) {}

  /**
   * Registers this LambdaOption with a RangeEnumerate, i.e. obtains an ID. May
   * be called multiple times, but obtains an ID only once.
   *
   * @remark[thread-safety] multiple threads may call this function
   * concurrently.
   *
   * @param[in,out] range_enumerate An instance of RangeEnumerate.
   */
  void Register(RangeEnumerate* range_enumerate) {
    if (id_.load(std::memory_order_acquire) == RangeEnumerate::kInvalidID) {
      std::lock_guard<std::mutex> lock(register_mutex_);
      if (id_.load(std::memory_order_relaxed) == RangeEnumerate::kInvalidID) {
        // Extend is thread-safe: it may be called by different LambdaOptions.
        auto new_id = range_enumerate->Extend(opts_.size(), label_);
        id_.store(new_id, std::memory_order_release);
      }
    }
  }

  /**
   * Gets the current Option based on the current state of a RangeEnumerate
   * instance.
   *
   * @remark[thread-safety] Multiple threads may call this function
   * concurrently; however, due to RangeEnumerate only being thread-safe for
   * Extend, it is not possible to use a RangeEnumerate instance that may be
   * modified concurrently. This implies that no other LambdaOption instance
   * may use Register concurrently with the same RangeEnumerate instance.
   *
   * @param range_enumerate Instance of RangeEnumerate registered with.
   * @param permit_invalid (optional) If true and the current ID is invalid
   *        (not registered with range_enumerate or kInvalidID), return first
   *        option.
   * @return Option The current option.
   */
  const Option& GetCurrent(const RangeEnumerate& range_enumerate,
                           bool permit_invalid = false) const {
    auto id = id_.load(std::memory_order_acquire);

    if (permit_invalid && !range_enumerate.IsValid(id)) {
      return opts_.front();
    }

    auto idx = range_enumerate[id];
    assert(idx < opts_.size());
    return opts_[idx];
  }

  /**
   * Index based access, where index is an instance of RangeEnumerate.
   *
   * @remark[thread-safety] Same restrictions as GetCurrent apply. Different
   * LambdaOptions may not use this operator concurrently with the same
   * RangeEnumerate instance.
   *
   * @param range_enumerate Instance of RangeEnumerate.
   * @return The current option.
   */
  const Option& operator[](RangeEnumerate& range_enumerate) {
    Register(&range_enumerate);
    return GetCurrent(range_enumerate);
  }

  const std::string& label() const { return label_; }

  RangeEnumerate::ID id() const { return id_.load(std::memory_order_acquire); }

 private:
  std::string label_;
  std::vector<Option> opts_;
  std::atomic<RangeEnumerate::ID> id_;
  std::mutex register_mutex_;
};

}  // namespace synthesis
}  // namespace verc3

#endif /* VERC3_SYNTHESIS_ENUMERATE_HH_ */

/* vim: set ts=2 sts=2 sw=2 et : */
