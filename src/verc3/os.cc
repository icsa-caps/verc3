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

#include "os.hh"

#include <cstddef>

#include <gflags/gflags.h>
#include <glog/logging.h>
#include <sys/resource.h>

DEFINE_uint64(os_memlimit, 4 * 1024,
              "Set non-zero to configure memory limit, in MiB");

namespace verc3 {
namespace os {

/**
 * Configures defined memory limit.
 *
 * @return True if successful, false otherwise.
 */
bool ConfigureMemLimit() {
  if (FLAGS_os_memlimit == 0) return true;

  std::size_t mem_limit_bytes = FLAGS_os_memlimit * 1024ULL * 1024ULL;

  struct rlimit rl;

  if (getrlimit(RLIMIT_AS, &rl) == 0) {
    if (rl.rlim_cur != static_cast<std::size_t>(-1) &&
        rl.rlim_cur < mem_limit_bytes) {
      LOG(WARNING) << "RLIMIT_AS already configured to less than requested, "
                      "not changing!";
      return false;
    }

    if (rl.rlim_max < mem_limit_bytes) {
      LOG(WARNING) << "RLIMIT_AS maximum less than requested, setting to max!";
      mem_limit_bytes = rl.rlim_max;
    }

    rl.rlim_cur = mem_limit_bytes;

    setrlimit(RLIMIT_AS, &rl);
    getrlimit(RLIMIT_AS, &rl);

    if (rl.rlim_cur == mem_limit_bytes) {
      LOG(INFO) << "Successfully configured memory limit (RLIMIT_AS) to "
                << (rl.rlim_cur / (1024 * 1024)) << " MiB.";
    } else {
      LOG(WARNING) << "Failed to configure memory limit (RLIMIT_AS)!";
    }
  } else {
    LOG(WARNING) << "Could not get current RLIMIT_AS!";
    return false;
  }

  return true;
}

}  // namespace os
}  // namespace verc3

/* vim: set ts=2 sts=2 sw=2 et : */
