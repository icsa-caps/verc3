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

#include <string>

#include <gflags/gflags.h>
#include <glog/logging.h>

#include "models/main.hh"
#include "verc3/os.hh"

using namespace verc3;

int main(int argc, char *argv[]) {
  // Override flags default
  FLAGS_logtostderr = true;

  // Initialize flags first.
  gflags::SetUsageMessage("<command> [<args>...]");
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  // Then initialize everything else.
  google::InitGoogleLogging(argv[0]);
  os::ConfigureMemLimit();

  if (argc >= 2) {
    std::string cmd = argv[1];

    if (cmd == "runmodel") {
      return models::Main(argc, argv);
    } else {
      LOG(ERROR) << "Invalid command: " << cmd;
      return 1;
    }
  } else {
    LOG(ERROR) << "No default command available!";
    return 1;
  }

  return 0;
}

/* vim: set ts=2 sts=2 sw=2 et : */
