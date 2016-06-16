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

#ifndef VERC3_CORE_TYPES_HH_
#define VERC3_CORE_TYPES_HH_

#include <algorithm>
#include <array>
#include <bitset>
#include <cassert>
#include <cstddef>
#include <functional>
#include <iterator>
#include <type_traits>

#include <mc2lib/sets.hpp>

namespace verc3 {
namespace core {

template <class T, class = void>
struct Hasher {
  typedef typename T::Hash type;
};

template <class T>
struct Hasher<T, typename std::enable_if<std::is_fundamental<T>::value>::type> {
  typedef std::hash<T> type;
};

template <class T>
struct Hasher<T, typename std::enable_if<std::is_enum<T>::value>::type> {
  struct type {
    auto operator()(const T& k) const {
      return std::hash<std::size_t>()(static_cast<std::size_t>(k));
    }
  };
};

namespace detail {

template <class ST>
struct SymmetricHash {
  template <class Func>
  auto WithFilter(const ST& k, Func filter) const {
    typename Hasher<typename ST::Element>::type hasher;
    typename std::result_of<decltype(hasher)(const typename ST::Element&)>::type
        h = 42;

    for (auto it = k.Get().begin(); it != k.Get().end(); ++it) {
      if (!filter(it)) continue;

      // Commutative hash
      // TODO(melver): verify sound.
      h += hasher(*it);
    }

    return h;
  }

  auto operator()(const ST& k) const {
    return WithFilter(k, [](decltype(k.Get().begin()) it) { return true; });
  }
};

template <class T, class = void>
struct ArraySetNextBase {
  static constexpr std::size_t value = 1;
};

template <class T>
struct ArraySetNextBase<T, typename std::enable_if<T::kNextBase != 1>::type> {
  static constexpr std::size_t value = T::kNextBase;
};
}  // namespace detail

/**
 * Constant-size array-based set implementation.
 *
 * Union can be defined as follows:
 *
 *    using MyType1 = ArraySet<T1, 4>;
 *    using MyType2 = ArraySet<T2, 1, MyType1>;
 *    union UnionType {
 *      MyType1::ID MyType1_ID;
 *      MyType2::ID MyType2_ID;
 *    };
 */
template <class ElementT, std::size_t size_, class Union = void>
class ArraySet {
 public:
  static_assert(size_ > 0, "Size cannot be 0!");

  typedef ElementT Element;
  typedef std::array<Element, size_> Container;

  enum class ID : std::size_t { kUndefined = 0 };

  struct Hash : detail::SymmetricHash<ArraySet> {
    auto operator()(const ArraySet& k) const {
      return this->WithFilter(
          k, [&k](decltype(k.array_.begin()) it) { return k.IsValid(it); });
    }
  };

  static constexpr std::size_t kBase = detail::ArraySetNextBase<Union>::value;

  static_assert(kBase > static_cast<std::size_t>(ID::kUndefined),
                "Invalid base!");

  static constexpr std::size_t kNextBase = kBase + size_;

  explicit ArraySet(bool all_valid = false) {
    if (all_valid) {
      valid_.set();
    }
  }

  template <typename... Ts>
  explicit ArraySet(Ts&&... ts) : array_{{std::forward<Ts>(ts)...}} {
    for (std::size_t i = 0; i < sizeof...(Ts); ++i) {
      valid_.set(i);
    }
  }

  /**
   * Use with care!
   */
  const Container& Get() const { return array_; }

  bool operator==(const ArraySet& rhs) const {
    if (this == &rhs) return true;
    return std::is_permutation(
        array_.begin(), array_.end(), rhs.array_.begin(),
        [this, &rhs](const Element& e1, const Element& e2) {
          // Have to resort to pointer comparisons as iterators not available.
          // Reimplementing is_permutation would be more complicated.
          //
          std::size_t idx1, idx2;
          const ArraySet *as1, *as2;

          if (&e1 >= &array_.front() && &e1 <= &array_.back()) {
            idx1 = std::distance(array_.data(), &e1);
            as1 = this;
          } else {
            assert(&e1 >= &rhs.array_.front() && &e1 <= &rhs.array_.back());
            idx1 = std::distance(rhs.array_.data(), &e1);
            as1 = &rhs;
          }

          if (&e2 >= &array_.front() && &e2 <= &array_.back()) {
            idx2 = std::distance(array_.data(), &e2);
            as2 = this;
          } else {
            assert(&e2 >= &rhs.array_.front() && &e2 <= &rhs.array_.back());
            idx2 = std::distance(rhs.array_.data(), &e2);
            as2 = &rhs;
          }

          // If one of the elements is invalid, they will only be considered
          // equivalent if both are invalid.
          if (!as1->valid_.test(idx1) || !as2->valid_.test(idx2)) {
            return as1->valid_.test(idx1) == as2->valid_.test(idx2);
          }

          return e1 == e2;
        });
  }

  bool operator!=(const ArraySet& rhs) const { return !((*this) == rhs); }

  // The following functions follow C++ STL naming convention, as they
  // implement the same functionality as the STL counterparts.

  template <class Func>
  Func for_each(Func func) {
    for (auto it = array_.begin(); it != array_.end(); ++it) {
      if (!IsValid(it)) continue;
      func(*it);
    }

    return std::move(func);
  }

  template <class Func>
  Func for_each(Func func) const {
    for (auto it = array_.begin(); it != array_.end(); ++it) {
      if (!IsValid(it)) continue;
      func(*it);
    }

    return std::move(func);
  }

  template <class Func>
  bool all_of(Func func) const {
    for (auto it = array_.begin(); it != array_.end(); ++it) {
      if (!IsValid(it)) continue;
      if (!func(*it)) return false;
    };

    return true;
  }

  template <class Func>
  bool any_of(Func func) const {
    for (auto it = array_.begin(); it != array_.end(); ++it) {
      if (!IsValid(it)) continue;
      if (func(*it)) return true;
    }

    return false;
  }

  template <class Func>
  std::size_t count_if(Func func) const {
    std::size_t result = 0;
    for (auto it = array_.begin(); it != array_.end(); ++it) {
      if (!IsValid(it)) continue;
      if (func(*it)) ++result;
    }

    return result;
  }

  template <class Func>
  Func for_each_ID(Func func) const {
    for (std::size_t i = kBase; i < array_.size() + kBase; ++i) {
      func(static_cast<ID>(i));
    }

    return std::move(func);
  }

  std::size_t size() const { return valid_.count(); }

  bool empty() const { return !valid_.any(); }

  Element* operator[](ID id) {
    const auto idx = IDtoIdx(id);
    valid_.set(idx);
    return &array_[idx];
  }

  const Element* operator[](ID id) const {
    const auto idx = IDtoIdx(id);
    if (!valid_.test(idx)) return nullptr;
    return &array_[idx];
  }

  Element* operator()() {
    if (size() == 1) {
      return (*this)[NextValid()];
    }

    return nullptr;
  }

  const Element* operator()() const {
    if (size() == 1) {
      return (*this)[NextValid()];
    }

    return nullptr;
  }

  constexpr ID GetUndefined() { return ID::kUndefined; }

  bool IsMember(ID id) const {
    return static_cast<std::size_t>(id) >= kBase &&
           static_cast<std::size_t>(id) < array_.size() + kBase;
  }

  ID GetID(const Element& e) const {
    for (auto it = array_.begin(); it != array_.end(); ++it) {
      if (!IsValid(it)) continue;
      if (*it == e) return GetIDFromIterator(it);
    }

    return ID::kUndefined;
  }

  bool Contains(const Element& e) const {
    for (auto it = array_.begin(); it != array_.end(); ++it) {
      if (!IsValid(it)) continue;
      if (*it == e) return true;
    }

    return false;
  }

  void SetAllValid() { valid_.set(); }

  void SetAllInvalid() { valid_.reset(); }

  void Clear() { SetAllInvalid(); }

  bool SetValid(ID id) {
    if (!IsMember(id)) return false;
    valid_.set(IDtoIdx(id));
    return true;
  }

  bool SetInvalid(ID id) {
    if (!IsMember(id)) return false;
    valid_.reset(IDtoIdx(id));
    return true;
  }

  ID Erase(const Element& e) {
    auto id = GetID(e);
    SetInvalid(id);
    return id;
  }

  bool IsValid(ID id) const { return valid_.test(IDtoIdx(id)); }

  ID NextValid() const {
    if (valid_.none()) return ID::kUndefined;
    std::size_t idx = valid_.count() - 1;

    if (!valid_.test(idx)) {
      // Fallback to iterative search
      for (std::size_t i = 0; i < valid_.size(); ++i) {
        if (valid_[i]) {
          idx = i;
          break;
        }
      }
    }

    return IdxToID(idx);
  }

  ID NextInvalid() const {
    if (valid_.all()) return ID::kUndefined;
    std::size_t idx = valid_.count();

    if (valid_.test(idx)) {
      // Fallback to iterative search
      for (std::size_t i = 0; i < valid_.size(); ++i) {
        if (!valid_[i]) {
          idx = i;
          break;
        }
      }
    }

    return IdxToID(idx);
  }

  ID Insert(const Element& e) {
    const auto next_invalid = NextInvalid();
    if (next_invalid != ID::kUndefined) {
      *((*this)[next_invalid]) = e;
    }
    return next_invalid;
  }

  ID Insert(Element&& e) {
    const auto next_invalid = NextInvalid();
    if (next_invalid != ID::kUndefined) {
      *((*this)[next_invalid]) = std::move(e);
    }
    return next_invalid;
  }

 private:
  std::size_t IDtoIdx(ID id) const {
    const auto idx = static_cast<std::size_t>(id) - kBase;
    assert(idx >= 0 && idx < array_.size());
    return idx;
  }

  constexpr ID IdxToID(std::size_t idx) const {
    return static_cast<ID>(kBase + idx);
  }

  ID GetIDFromIterator(typename Container::const_iterator it) const {
    const auto idx = std::distance(array_.begin(), it);
    return IdxToID(idx);
  }

  bool IsValid(typename Container::const_iterator it) const {
    const auto idx = std::distance(array_.begin(), it);
    return valid_.test(idx);
  }

  Container array_;
  std::bitset<size_> valid_;
};

/**
 * Union type that can be used to store arbitrary types of ID without
 * restricting the types.
 */
class WeakUnion {
 public:
  enum class ID : std::size_t { kUndefined = 0 };

  WeakUnion() : id_(ID::kUndefined) {}

  template <class T>
  explicit WeakUnion(T v) : id_(static_cast<decltype(id_)>(v)) {}

  template <class T>
  WeakUnion& operator=(const T& v) {
    id_ = static_cast<decltype(id_)>(v);
    return *this;
  }

  bool operator==(const WeakUnion& rhs) const { return id_ == rhs.id_; }

  template <class T>
  T id_as() const {
    return static_cast<T>(id_);
  }

 private:
  ID id_;
};

template <class T>
class Set : public mc2lib::sets::Set<
                mc2lib::sets::Types<T, typename Hasher<T>::type>> {
 public:
  using typename mc2lib::sets::Set<
      mc2lib::sets::Types<T, typename Hasher<T>::type>>::Element;
  using Hash = detail::SymmetricHash<Set>;
};

template <class T>
using Relation =
    mc2lib::sets::Relation<mc2lib::sets::Types<T, typename Hasher<T>::type>>;

template <class T>
using RelationSeq =
    mc2lib::sets::RelationSeq<mc2lib::sets::Types<T, typename Hasher<T>::type>>;

}  // namespace core
}  // namespace verc3

#endif /* VERC3_CORE_TYPES_HH_ */

/* vim: set ts=2 sts=2 sw=2 et : */
