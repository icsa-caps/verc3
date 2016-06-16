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
#include "verc3/synthesis.hh"

#include <gtest/gtest.h>

using namespace verc3;

TEST(Synthesis, RangeEnumerate) {
  RangeEnumerate range_enumerate;
  ASSERT_EQ(range_enumerate.combinations(), 0);

  auto foo_id = range_enumerate.Extend(2, "foo");
  ASSERT_EQ(range_enumerate.GetState(foo_id).label, "foo");
  ASSERT_EQ(range_enumerate.GetState("foo").label, "foo");
  ASSERT_EQ(range_enumerate.combinations(), 2);

  // Only one extension
  ASSERT_EQ(range_enumerate.GetValue(foo_id), 0);
  ASSERT_EQ(range_enumerate.Next(), true);
  ASSERT_EQ(range_enumerate.GetValue(foo_id), 1);
  ASSERT_EQ(range_enumerate.Next(), false);
  ASSERT_EQ(range_enumerate.GetValue(foo_id), 0);

  // Another extension, but with existing already advanced by 1.
  range_enumerate.Next();
  auto bar_id = range_enumerate.Extend(5, "bar");
  ASSERT_EQ(range_enumerate.combinations(), 10);

  std::size_t i = range_enumerate.GetValue(foo_id);
  do {
    ASSERT_EQ(range_enumerate.GetValue(foo_id), i & 1);
    ASSERT_EQ(range_enumerate.GetValue(bar_id), i >> 1);
    ++i;
  } while (range_enumerate.Next());
  ASSERT_EQ(range_enumerate.combinations(), i);

  range_enumerate.Next();
  range_enumerate.Next();
  range_enumerate.Next();

  std::ostringstream oss;
  oss << range_enumerate;
  ASSERT_EQ(oss.str().size(), 27);
}

TEST(Synthesis, LambdaOptions) {
  RangeEnumerate range_enumerate;
  LambdaOptions<bool(int)> lo{"Lambdas", [](int i) { return i == 0; },
                              [](int i) { return i == 1; },
                              [](int i) { return i == 2; }};

  ASSERT_EQ(range_enumerate.combinations(), 0);

  int i = 0;
  do {
    ASSERT_TRUE(lo[range_enumerate](i++));
  } while (range_enumerate.Next());

  ASSERT_EQ("Lambdas", range_enumerate.GetState(lo.id()).label);
}

/* vim: set ts=2 sts=2 sw=2 et : */
