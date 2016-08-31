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
#include <map>
#include <mutex>
#include <shared_mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
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
  // You do not want more than MAX_INT elements anyway. This used to be size_t,
  // but making kInvalidID = 0 was unnecessarily confusing for a use-case that
  // is intractable.
  typedef int ID;
  static constexpr ID kInvalidID = -1;

  class State {
   public:
    explicit State(std::size_t v, std::size_t r, std::string l)
        : value_(v), range_(r), label_(std::move(l)) {}

    bool operator==(const State& rhs) const {
      return value_ == rhs.value_ && range_ == rhs.range_;
    }

    bool operator!=(const State& rhs) const { return !(*this == rhs); }

    std::size_t value() const { return value_; }

    void set_value(std::size_t v) {
      if (v >= range_) {
        throw std::out_of_range("RangeEnumerate::State::set_value");
      }

      value_ = v;
    }

    std::size_t range() const { return range_; }

    const std::string& label() const { return label_; }

   private:
    std::size_t value_;
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

  int Compare(const RangeEnumerate& rhs) const {
    assert(states_.size() == rhs.states_.size());

    for (std::size_t i = states_.size(); i > 0; --i) {
      const auto& lhs_state = states_[i - 1];
      const auto& rhs_state = rhs.states_[i - 1];
      assert(lhs_state.range() == rhs_state.range());
      if (lhs_state.value() < rhs_state.value()) {
        // less-than
        return -1;
      } else if (lhs_state.value() > rhs_state.value()) {
        // greater-than
        return 1;
      }
    }

    // equal
    return 0;
  }

  bool operator<(const RangeEnumerate& rhs) const { return Compare(rhs) == -1; }

  bool operator<=(const RangeEnumerate& rhs) const { return Compare(rhs) <= 0; }

  bool operator>(const RangeEnumerate& rhs) const { return Compare(rhs) == 1; }

  bool operator>=(const RangeEnumerate& rhs) const { return Compare(rhs) >= 0; }

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
  ID Extend(std::size_t range, std::string label);

  void SetMin() {
    for (auto& p : states_) {
      p.set_value(0);
    }
  }

  void SetMax() {
    for (auto& p : states_) {
      p.set_value(p.range() - 1);
    }
  }

  void SetFrom(const RangeEnumerate& other) {
    for (std::size_t i = 0; i < other.states_.size() && i < states_.size();
         ++i) {
      assert(states_[i].range() == other.states_[i].range());
      states_[i].set_value(other.states_[i].value());
    }
  }

  bool Advance(std::size_t count = 1) {
    return Advance(count,
                   [](const RangeEnumerate& next) { return kInvalidID; });
  }

  /**
   * Advances the current state to yield the next enumeration.
   *
   * Advances the current state by the provided count. A user-supplied function
   * validates the next state; if the user function detects a mismatch, the
   * mismatched ID will determine the count by which to incremented again:
   * the increment count will be the range of all states less significant than
   * ID (this implies all states less significant than ID remain unchanged).
   *
   * @param count Increase by count.
   * @param validate Validates the resulting state, which should return
   *        kInvalidID to validate; any valid returned ID should be the first
   *        mismatch.
   * @return true if no overflow occurred, false if overflow occurred.
   */
  template <class ValidateFunc>
  bool Advance(std::size_t count, ValidateFunc validate) {
    for (std::size_t i = 0; i < states_.size();) {
      auto& p = states_[i++];
      std::size_t new_value = p.value() + count;

      if (new_value < p.range()) {
        p.set_value(new_value);

        // validate new state
        ID mismatch = validate(*this);
        if (mismatch != kInvalidID) {
          // validation failed, continue advancing from mismatch.
          if (!IsValid(mismatch)) {
            throw std::out_of_range("RangeEnumerate::Advance: invalid ID");
          }

          count = 1;
          i = mismatch;
          continue;
        }

        // no carry
        return true;
      }

      // carry
      count = new_value / p.range();
      new_value %= p.range();
      p.set_value(new_value);
    }

    return false;
  }

  bool IsValid(ID id) const { return kInvalidID < id && id < states_.size(); }

  const State& GetState(ID id) const {
    if (!IsValid(id)) {
      throw std::out_of_range("RangeEnumerate::GetState: invalid ID");
    }

    return states_[id];
  }

  const State& GetState(const std::string& label) const {
    auto it = label_map_.find(label);

    if (it == label_map_.end()) {
      throw std::domain_error(label);
    }

    return *it->second;
  }

  template <class Func>
  Func for_each_ID(Func func) const {
    for (ID id = 0; id < states_.size(); ++id) {
      if (!func(id)) break;
    }

    return std::move(func);
  }

  std::size_t operator[](ID id) const { return GetState(id).value(); }

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
std::ostream& operator<<(std::ostream& os, const RangeEnumerate& v);

/**
 * Provides the ability to match a set of RangeEnumerate patterns.
 *
 * @remark[thread-safety] Thread-safe.
 */
class RangeEnumerateMatcher {
 public:
  static constexpr std::size_t kMaxStatesSize = sizeof(std::size_t) * 8;

  explicit RangeEnumerateMatcher(std::size_t wildcard) : wildcard_(wildcard) {}

  /**
   * @remark[time-complexity] O( range_enum.size() )
   */
  bool Insert(const RangeEnumerate& range_enum,
              std::size_t max_nonwildcard = kMaxStatesSize);

  /**
   * @remark[time-complexity] O( patterns_.size() * range_enum.size() ),
   *    however significantly reduced average time complexity due to filtering
   *    using pre-computed bit-patterns.
   *
   * @param range_enum The RangeEnumerate which is matched against all patterns.
   * @return The least significant ID of the match.
   */
  RangeEnumerate::ID Match(const RangeEnumerate& range_enum) const;

  void Clear() {
    std::lock_guard<std::shared_timed_mutex> lock(mutex_);
    patterns_.clear();
  }

  /**
   * @remark[time-complexity] O( range_enum.size() )
   *
   * @param range_enum The RangeEnumerate for which to compute the bit pattern.
   * @param bit_pattern[out] Output pattern as bitset.
   * @param lower_bound[out] Output lower bound (minimum overlapping pattern).
   * @param upper_bound[out] Output upper bound (maximum overlapping pattern).
   * @return Number of non-wildcards, i.e. bits set in *bit_pattern.
   */
  std::size_t AsBitPattern(const RangeEnumerate& range_enum,
                           std::size_t* bit_pattern, std::size_t* lower_bound,
                           std::size_t* upper_bound) const {
    if (range_enum.states().size() > kMaxStatesSize) {
      throw std::out_of_range("RangeEnumerateMatcher::AsBitPattern");
    }

    std::size_t result = 0;
    std::size_t num_nonwildcards = 0;
    int first_bit_pos = -1;
    int msb_pos = -1;
    for (std::size_t i = 0; i < range_enum.states().size(); ++i) {
      if (range_enum.states()[i].value() != wildcard_) {
        ++num_nonwildcards;
        result |= 1 << i;

        if (first_bit_pos == -1) {
          first_bit_pos = i;
        }

        msb_pos = i;
      }
    }

    if (bit_pattern != nullptr) {
      *bit_pattern = result;
    }

    if (result > 0) {
      assert(first_bit_pos != -1);
      assert(msb_pos != -1);

      if (lower_bound != nullptr) {
        *lower_bound = 1 << first_bit_pos;
      }

      if (upper_bound != nullptr) {
        *upper_bound = (1 << (msb_pos + 1)) - 1;
      }
    }

    return num_nonwildcards;
  }

 private:
  // We have no need for the timing capabilities of shared_timed_mutex, but
  // C++14 does not yet have shared_mutex (in C++17).
  mutable std::shared_timed_mutex mutex_;

  /**
   * Which value is considered the wildcard.
   */
  std::size_t wildcard_;

  /**
   * A SparsePattern is the datastructure for patterns without wildcards
   * sharing the same wildcard positions.
   *
   * The vector stores the values per location, and per location contains a map
   * from a value to the ones in the next position.
   */
  using SparsePatterns = std::vector<
      std::unordered_map<std::size_t, std::unordered_set<std::size_t>>>;

  /**
   * The WildcardPatterns is the collection of patterns, mapping from the
   * bitvector (1 = no wildcard, 0 = wildcard) representation of the patterns
   * to the sparse concrete patterns.
   */
  using WildcardPatterns = std::map<std::size_t, SparsePatterns>;

  WildcardPatterns patterns_;
};

class LambdaOptionsBase {
 public:
  explicit LambdaOptionsBase(RangeEnumerate::ID id) : id_(id) {}

  /**
   * @remark[thread-safety] Not thread-safe.
   */
  static void UnregisterAll();

 protected:
  /**
   * @remark[thread-safety] multiple threads may call this function
   * concurrently.
   */
  static void StaticRegister(LambdaOptionsBase* instance);

  std::atomic<RangeEnumerate::ID> id_;

  static std::vector<LambdaOptionsBase*> static_registry_;
  static std::mutex static_registry_mutex_;
};

/**
 * Collection of lambdas to be chosen from via RangeEnumerate.
 *
 * @remark[thread-safety] See individual function description. Safe for use as
 * function local static (implicit, as C++11 guarantees thread-safe static
 * initialization if constructor does not access other shared data).
 */
template <class T>
class LambdaOptions : public LambdaOptionsBase {
 public:
  typedef std::function<T> Option;

  LambdaOptions(const LambdaOptions& rhs) = delete;

  LambdaOptions(LambdaOptions&& rhs)
      : LambdaOptionsBase(rhs.id_),
        label_(std::move(rhs.label_)),
        opts_(std::move(rhs.opts_)) {
    rhs.id_ = RangeEnumerate::kInvalidID;
  }

  explicit LambdaOptions(std::string label, const LambdaOptions& copy_from,
                         bool static_duration = false)
      : LambdaOptionsBase(RangeEnumerate::kInvalidID),
        label_(std::move(label)),
        opts_(copy_from.opts_) {
    if (static_duration) {
      StaticRegister(this);
    }
  }

  explicit LambdaOptions(std::string label, std::vector<Option> opts,
                         bool static_duration = false)
      : LambdaOptionsBase(RangeEnumerate::kInvalidID),
        label_(std::move(label)),
        opts_(std::move(opts)) {
    if (static_duration) {
      StaticRegister(this);
    }
  }

  /**
   * Registers this LambdaOptions with a RangeEnumerate, i.e. obtains an ID. May
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
   * modified concurrently. This implies that no other LambdaOptions instance
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
  std::mutex register_mutex_;
};

}  // namespace synthesis
}  // namespace verc3

#endif /* VERC3_SYNTHESIS_ENUMERATE_HH_ */

/* vim: set ts=2 sts=2 sw=2 et : */
