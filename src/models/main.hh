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

#ifndef VERC3_MODELS_MAIN_HH_
#define VERC3_MODELS_MAIN_HH_

#include <map>
#include <string>

namespace models {

class Registry {
 public:
  typedef int (*EntryPoint)(int, char* []);
  using Map = std::map<std::string, EntryPoint>;

  static bool Add(const std::string& name, EntryPoint entry_point);

  static const Map& models() { return models_; }

 private:
  static Map models_;
};

int Main(int argc, char* argv[]);

}  // namespace models

#endif /* VERC3_MODELS_MAIN_HH_ */

/* vim: set ts=2 sts=2 sw=2 et : */
