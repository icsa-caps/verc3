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

// Include tested header first, to assert it includes required headers itself!
#include "verc3/util.hh"

#include <sstream>

#include <gtest/gtest.h>

using namespace verc3;

PRINTABLE_ENUM_CLASS(TestEnum, inline, kFirst, kSecond, kThird);

TEST(Util, PrintableEnumClass) {
  std::ostringstream oss;
  oss << TestEnum::kFirst << TestEnum::kSecond << TestEnum::kThird;
  ASSERT_EQ(oss.str(), "kFirstkSecondkThird");
}

/* vim: set ts=2 sts=2 sw=2 et : */
