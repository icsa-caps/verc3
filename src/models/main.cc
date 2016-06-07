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

#include "main.hh"

#include <cassert>
#include <iostream>
#include <string>

#include <glog/logging.h>

namespace models {

Registry::Map Registry::models_;

bool Registry::Add(const std::string& name, EntryPoint entry_point) {
  assert(entry_point != nullptr);
  models_.emplace(name, entry_point);
  return true;
}

#define REGISTER_MODEL(name)               \
  do {                                     \
    extern int Main_##name(int, char* []); \
    Registry::Add(#name, Main_##name);     \
  } while (0)

int Main(int argc, char* argv[]) {
#include "models/registry.hh"

  if (argc < 3) {
    LOG(ERROR) << "Please specify model (or 'list' to list)!";
    return 1;
  }

  std::string model_name = argv[2];
  if (model_name == "list") {
    std::cout << std::endl << "Available models:" << std::endl;
    for (const auto& model : Registry::models()) {
      std::cout << "    " << model.first << std::endl;
    }
    std::cout << std::endl;
  } else {
    auto model = Registry::models().find(model_name);
    if (model == Registry::models().end()) {
      LOG(ERROR) << "No such model (use 'list' to see all available): "
                 << model_name;
      return 1;
    }

    return model->second(argc - 2, &argv[2]);
  }

  return 0;
}

}  // namespace models

/* vim: set ts=2 sts=2 sw=2 et : */
