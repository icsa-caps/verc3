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

#ifndef VERC3_IO_HH_
#define VERC3_IO_HH_

#include <iostream>

namespace verc3 {

constexpr auto kColRst = "\e[0m";
constexpr auto kColRed = "\e[0;31m";
constexpr auto kColGrn = "\e[0;32m";
constexpr auto kColYlw = "\e[0;33m";
constexpr auto kColBlu = "\e[0;34m";
constexpr auto kColPur = "\e[0;35m";
constexpr auto kColCyn = "\e[0;36m";
constexpr auto kColRED = "\e[1;31m";
constexpr auto kColGRN = "\e[1;32m";
constexpr auto kColYLW = "\e[1;33m";
constexpr auto kColBLU = "\e[1;34m";
constexpr auto kColPUR = "\e[1;35m";
constexpr auto kColCYN = "\e[1;36m";

inline auto& InfoOut() { return std::cout << kColPur << "INFO: " << kColRst; }

inline auto& WarnOut() {
  return std::cerr << kColYlw << "WARNING: " << kColRst;
}

inline auto& ErrOut() { return std::cerr << kColRed << "ERROR: " << kColRst; }

}  // namespace verc3

#endif /* VERC3_IO_HH_ */

/* vim: set ts=2 sts=2 sw=2 et : */
