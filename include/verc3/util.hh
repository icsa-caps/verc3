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

#ifndef VERC3_UTIL_HH_
#define VERC3_UTIL_HH_

#include <array>
#include <functional>
#include <sstream>
#include <string>

#include "verc3/core/types.hh"

namespace verc3 {

/**
 * Combine hash with hash of another value.
 *
 * Magic numbers taken from boost's hash_combine.
 */
template <class InT, class OutT, class Hash = typename core::Hasher<InT>::type>
inline void CombineHash(const InT& in, OutT* seed) {
  Hash hasher;
  *seed ^= hasher(in) + 0x9e3779b9 + (*seed << 6) + (*seed >> 2);
}

}  // namespace verc3

/**
 * Define enum class with stream operator to convert to string. Does not work
 * with explicitly specified enum values.
 *
 * func_attr should be either inline or friend (if nested inside class).
 */
#define PRINTABLE_ENUM_CLASS(T, func_attr, ...)                              \
  enum class T { __VA_ARGS__ };                                              \
  func_attr std::ostream& operator<<(std::ostream& os, const T& v) {         \
    struct EnumToStr {                                                       \
      enum class T##__{__VA_ARGS__, kSize__};                                \
      explicit EnumToStr() {                                                 \
        std::istringstream iss(#__VA_ARGS__);                                \
        std::string s;                                                       \
        for (std::size_t i = 0; std::getline(iss, s, ','); ++i) {            \
          if (s[0] == ' ') {                                                 \
            s.erase(0, 1);                                                   \
          }                                                                  \
          vs_[i] = std::move(s);                                             \
        }                                                                    \
      }                                                                      \
      std::array<std::string, static_cast<std::size_t>(T##__::kSize__)> vs_; \
    };                                                                       \
    static EnumToStr enum_to_str;                                            \
    os << enum_to_str.vs_[static_cast<std::size_t>(v)];                      \
    return os;                                                               \
  }

#endif /* VERC3_UTIL_HH_ */

/* vim: set ts=2 sts=2 sw=2 et : */
