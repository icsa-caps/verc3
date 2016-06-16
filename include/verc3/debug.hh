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

#ifndef VERC3_DEBUG_HH_
#define VERC3_DEBUG_HH_

#include <sstream>
#include <vector>

namespace verc3 {

/**
 * Prints a trace given in some container, with each those lines highlighted
 * that are different from the previous iteration, i.e. the changed lines.
 *
 * This can be used for printing traces of EvalBase::Trace (although note that
 * this function does not depend on EvalBase and is generic). The head and tail
 * functions are user supplied and decide how each element is supposed to be
 * printed.
 *
 * @param container Sequential container which contains the trace.
 * @param head Function being called before diffing.
 * @param tail Function being called after printing diffed strings.
 * @param[out] os The output stream.
 */
template <class T, class HeadFunc, class TailFunc>
void PrintTraceDiff(const T& container, HeadFunc head, TailFunc tail,
                    std::ostream& os) {
  std::vector<std::string> last;
  for (const auto& v : container) {
    std::vector<std::string> current;
    std::stringstream ss;

    head(v, ss);

    std::string line;
    while (std::getline(ss, line, '\n')) {
      current.push_back(std::move(line));
    }

    for (std::size_t i = 0; i < current.size(); ++i) {
      if (last.size() != current.size() || current[i] != last[i]) {
        os << "\e[1;36m" << current[i] << "\e[0m" << std::endl;
      } else {
        os << current[i] << std::endl;
      }
    }

    last = std::move(current);

    tail(v, os);
  }
}

}  // namespace verc3

#endif /* VERC3_DEBUG_HH_ */

/* vim: set ts=2 sts=2 sw=2 et : */
