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

#include "verc3/io.hh"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <ostream>
#include <string>

namespace {

std::chrono::time_point<std::chrono::steady_clock> start_time;

}  // namespace

namespace verc3 {
namespace detail {

void TimeSinceStart(std::ostream* os) {
  if (!start_time.time_since_epoch().count()) {
    start_time = std::chrono::steady_clock::now();
    auto now =
        std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::string now_str(std::ctime(&now));
    *os << now_str.substr(0, now_str.length() - 1);
    return;
  }

  std::chrono::duration<double> elapsed =
      std::chrono::steady_clock::now() - start_time;
  *os << std::fixed << elapsed.count() << "s";
}

}  // namespace detail
}  // namespace verc3

/* vim: set ts=2 sts=2 sw=2 et : */
