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
#include "verc3/synthesis/enumerate.hh"

#include <gtest/gtest.h>

using namespace verc3;
using namespace verc3::synthesis;

TEST(SynthesisEnumerate, RangeEnumerateExtend) {
  RangeEnumerate range_enumerate;
  ASSERT_EQ(range_enumerate.combinations(), 0);

  auto foo_id = range_enumerate.Extend(2, "foo");

  try {
    range_enumerate.Extend(2, "foo");
    FAIL();
  } catch (const std::domain_error& e) {
    ASSERT_EQ(std::string(e.what()), "label exists: foo");
  }

  ASSERT_EQ(range_enumerate.GetState(foo_id).label(), "foo");
  ASSERT_EQ(range_enumerate.GetState("foo").label(), "foo");
  ASSERT_EQ(range_enumerate.combinations(), 2);

  // Only one extension
  ASSERT_EQ(range_enumerate[foo_id], 0);
  ASSERT_EQ(range_enumerate.Advance(), true);
  ASSERT_EQ(range_enumerate[foo_id], 1);
  ASSERT_EQ(range_enumerate.Advance(), false);
  ASSERT_EQ(range_enumerate[foo_id], 0);

  // Another extension, but with existing already advanced by 1.
  range_enumerate.Advance();
  auto bar_id = range_enumerate.Extend(5, "bar");
  ASSERT_EQ(range_enumerate.combinations(), 10);

  std::size_t i = range_enumerate[foo_id];
  do {
    ASSERT_EQ(range_enumerate[foo_id], i & 1);
    ASSERT_EQ(range_enumerate[bar_id], i >> 1);
    ++i;
  } while (range_enumerate.Advance());
  ASSERT_EQ(range_enumerate.combinations(), i);

  range_enumerate.Advance();
  range_enumerate.Advance();
  range_enumerate.Advance();

  std::ostringstream oss;
  oss << range_enumerate;
  ASSERT_EQ(oss.str().size(), 27);
}

TEST(SynthesisEnumerate, RangeEnumerateAdvance) {
  RangeEnumerate range_enumerate;

  range_enumerate.Extend(3, "0");
  range_enumerate.Extend(2, "1");
  range_enumerate.Extend(1, "2");
  range_enumerate.Extend(2, "3");
  range_enumerate.Extend(3, "4");
  ASSERT_EQ(range_enumerate.combinations(), 36);

  auto check = [& re = range_enumerate](std::size_t v4, std::size_t v3,
                                        std::size_t v2, std::size_t v1,
                                        std::size_t v0) {
    return re.GetState("4").value() == v4 && re.GetState("3").value() == v3 &&
           re.GetState("2").value() == v2 && re.GetState("1").value() == v1 &&
           re.GetState("0").value() == v0;
  };

  ASSERT_TRUE(range_enumerate.Advance());
  ASSERT_TRUE(check(0, 0, 0, 0, 1));
  ASSERT_TRUE(range_enumerate.Advance(2));
  ASSERT_TRUE(check(0, 0, 0, 1, 0));
  ASSERT_TRUE(range_enumerate.Advance(3));
  ASSERT_TRUE(check(0, 1, 0, 0, 0));
  ASSERT_TRUE(range_enumerate.Advance(4));
  ASSERT_TRUE(check(0, 1, 0, 1, 1));
  ASSERT_TRUE(range_enumerate.Advance(5));
  ASSERT_TRUE(check(1, 0, 0, 1, 0));
  ASSERT_TRUE(range_enumerate.Advance(20));
  ASSERT_TRUE(check(2, 1, 0, 1, 2));
  ASSERT_FALSE(range_enumerate.Advance());
  ASSERT_TRUE(check(0, 0, 0, 0, 0));
}

TEST(SynthesisEnumerate, RangeEnumerateAdvanceFilter) {
  RangeEnumerate range_enumerate;

  range_enumerate.Extend(3, "0");
  range_enumerate.Extend(2, "1");
  range_enumerate.Extend(1, "2");
  range_enumerate.Extend(2, "3");
  range_enumerate.Extend(3, "4");
  ASSERT_EQ(range_enumerate.combinations(), 36);

  auto check = [& re = range_enumerate](std::size_t v4, std::size_t v3,
                                        std::size_t v2, std::size_t v1,
                                        std::size_t v0) {
    return re.GetState("4").value() == v4 && re.GetState("3").value() == v3 &&
           re.GetState("2").value() == v2 && re.GetState("1").value() == v1 &&
           re.GetState("0").value() == v0;
  };

  auto validate = [](const RangeEnumerate& next) -> RangeEnumerate::ID {
    if (next.GetState(1).value() == 1) {
      return 1;
    } else if (next.GetState(5).value() == 1 && next.GetState(2).value() == 1) {
      return 5;
    }
    return RangeEnumerate::kInvalidID;
  };

  ASSERT_TRUE(range_enumerate.Advance(1, validate));
  ASSERT_TRUE(check(0, 0, 0, 0, 2));  // filtered, + 2
  ASSERT_TRUE(range_enumerate.Advance(2, validate));
  ASSERT_TRUE(check(0, 0, 0, 1, 2));  // filtered, + 3
  ASSERT_TRUE(range_enumerate.Advance(9, validate));
  ASSERT_TRUE(check(1, 0, 0, 0, 2));
  ASSERT_TRUE(range_enumerate.Advance(1, validate));
  ASSERT_TRUE(check(2, 0, 0, 1, 0));
  ASSERT_TRUE(range_enumerate.Advance(8, validate));
  ASSERT_TRUE(check(2, 1, 0, 1, 2));
  ASSERT_FALSE(range_enumerate.Advance(2, validate));
  ASSERT_TRUE(check(0, 0, 0, 0, 1));  // filter not applied on overflow
}

TEST(SynthesisEnumerate, RangeEnumerateSetters) {
  RangeEnumerate range_enumerate;
  range_enumerate.Extend(3, "foo");
  range_enumerate.Extend(3, "bar");

  ASSERT_EQ(range_enumerate.states().front().value(), 0);
  ASSERT_EQ(range_enumerate.states().back().value(), 0);
  range_enumerate.SetMax();
  ASSERT_EQ(range_enumerate.states().front().value(), 2);
  ASSERT_EQ(range_enumerate.states().back().value(), 2);

  RangeEnumerate copy(range_enumerate);
  copy.Extend(10, "baz");  // discarded by assignment
  copy = range_enumerate;
  RangeEnumerate copy2 = std::move(copy);  // testing move
  ASSERT_EQ(copy.combinations(), 0);
  copy = std::move(copy2);
  ASSERT_EQ(copy.combinations(), 9);
  ASSERT_EQ(copy2.combinations(), 0);

  ASSERT_EQ(copy, range_enumerate);
  auto id = copy.Extend(3, "baz");
  ASSERT_NE(copy, range_enumerate);

  ASSERT_TRUE(copy.IsValid(id));
  ASSERT_FALSE(range_enumerate.IsValid(id));
  ASSERT_EQ(copy.combinations(), 27);
  ASSERT_EQ(copy[id], 0);
  ASSERT_TRUE(copy.Advance());
  ASSERT_EQ(copy[id], 1);

  ASSERT_FALSE(range_enumerate.Advance());
  range_enumerate.SetFrom(copy);
  ASSERT_EQ(range_enumerate.combinations(), 9);
  ASSERT_EQ(range_enumerate.states().size(), 2);
  ASSERT_TRUE(range_enumerate.Advance());
  ASSERT_EQ(range_enumerate.states().front().value(), 1);
  ASSERT_EQ(range_enumerate.states().back().value(), 0);

  range_enumerate.SetMax();
  copy.SetFrom(range_enumerate);
  ASSERT_TRUE(copy.Advance());
  ASSERT_EQ(copy[id], 2);
}

TEST(SynthesisEnumerate, RangeEnumerateCompare) {
  RangeEnumerate e1;

  e1.Extend(3, "foo");
  RangeEnumerate e2 = e1;
  ASSERT_TRUE(e1 == e2);
  ASSERT_FALSE(e1 != e2);
  ASSERT_FALSE(e1 < e2);
  ASSERT_TRUE(e1 <= e2);
  ASSERT_FALSE(e1 > e2);
  ASSERT_TRUE(e1 >= e2);

  e1.Advance();
  ASSERT_FALSE(e1 == e2);
  ASSERT_TRUE(e1 != e2);
  ASSERT_FALSE(e1 < e2);
  ASSERT_FALSE(e1 <= e2);
  ASSERT_TRUE(e1 > e2);
  ASSERT_TRUE(e1 >= e2);

  e1.Extend(5, "bar");
  e1.Extend(2, "baz");

  e2 = e1;
  e1.Advance(5);
  ASSERT_FALSE(e1 == e2);
  ASSERT_TRUE(e1 != e2);
  ASSERT_FALSE(e1 < e2);
  ASSERT_FALSE(e1 <= e2);
  ASSERT_TRUE(e1 > e2);
  ASSERT_TRUE(e1 >= e2);

  e2.Advance(6);
  ASSERT_FALSE(e1 == e2);
  ASSERT_TRUE(e1 != e2);
  ASSERT_TRUE(e1 < e2);
  ASSERT_TRUE(e1 <= e2);
  ASSERT_FALSE(e1 > e2);
  ASSERT_FALSE(e1 >= e2);
}

TEST(SynthesisEnumerate, LambdaOptions) {
  RangeEnumerate range_enumerate;
  LambdaOptions<bool(int)> lo1(
      "Lambdas1", {[](int i) { return i == 0; }, [](int i) { return i == 1; },
                   [](int i) { return i == 2; }});

  decltype(lo1) lo2("Lambdas2", lo1);

  ASSERT_EQ(range_enumerate.combinations(), 0);
  ASSERT_TRUE(lo1.GetCurrent(range_enumerate, true)(0));
  ASSERT_EQ(range_enumerate.combinations(), 0);

  int i = 0;
  do {
    ASSERT_TRUE(lo1[range_enumerate](i % 3));
    ASSERT_TRUE(lo2[range_enumerate](i / 3));
    ++i;
  } while (range_enumerate.Advance());

  ASSERT_EQ(range_enumerate.combinations(), 9);
  ASSERT_EQ("Lambdas1", range_enumerate.GetState(lo1.id()).label());
  ASSERT_EQ("Lambdas2", range_enumerate.GetState(lo2.id()).label());
}

/* vim: set ts=2 sts=2 sw=2 et : */
