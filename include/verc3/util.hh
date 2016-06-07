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

#include <functional>

#include "verc3/core/types.hh"

namespace verc3 {
namespace util {

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

}  // namespace util
}  // namespace verc3

#endif /* VERC3_UTIL_HH_ */

/* vim: set ts=2 sts=2 sw=2 et : */
