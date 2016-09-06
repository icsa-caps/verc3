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
  RangeEnumerate range_enum;
  ASSERT_EQ(range_enum.combinations(), 0);

  auto foo_id = range_enum.Extend(2, "foo");

  try {
    range_enum.Extend(2, "foo");
    FAIL();
  } catch (const std::domain_error& e) {
    ASSERT_EQ(std::string(e.what()), "label exists: foo");
  }

  ASSERT_EQ(range_enum.GetState(foo_id).label(), "foo");
  ASSERT_EQ(range_enum.GetState("foo").label(), "foo");
  ASSERT_EQ(range_enum.combinations(), 2);

  // Only one extension
  ASSERT_EQ(range_enum[foo_id], 0);
  ASSERT_EQ(range_enum.Advance(), true);
  ASSERT_EQ(range_enum[foo_id], 1);
  ASSERT_EQ(range_enum.Advance(), false);
  ASSERT_EQ(range_enum[foo_id], 0);

  // Another extension, but with existing already advanced by 1.
  range_enum.Advance();
  auto bar_id = range_enum.Extend(5, "bar");
  ASSERT_EQ(range_enum.combinations(), 10);

  std::size_t i = range_enum[foo_id];
  do {
    ASSERT_EQ(range_enum[foo_id], i & 1);
    ASSERT_EQ(range_enum[bar_id], i >> 1);
    ++i;
  } while (range_enum.Advance());
  ASSERT_EQ(range_enum.combinations(), i);

  range_enum.Advance();
  range_enum.Advance();
  range_enum.Advance();

  std::ostringstream oss;
  oss << range_enum;
  ASSERT_EQ(oss.str().size(), 27);
}

TEST(SynthesisEnumerate, RangeEnumerateAdvance) {
  RangeEnumerate range_enum;

  range_enum.Extend(3, "0");
  range_enum.Extend(2, "1");
  range_enum.Extend(1, "2");
  range_enum.Extend(2, "3");
  range_enum.Extend(3, "4");
  ASSERT_EQ(range_enum.combinations(), 36);

  auto check = [& re = range_enum](std::size_t v4, std::size_t v3,
                                   std::size_t v2, std::size_t v1,
                                   std::size_t v0) {
    return re.GetState("4").value() == v4 && re.GetState("3").value() == v3 &&
           re.GetState("2").value() == v2 && re.GetState("1").value() == v1 &&
           re.GetState("0").value() == v0;
  };

  ASSERT_TRUE(range_enum.Advance());
  ASSERT_TRUE(check(0, 0, 0, 0, 1));
  ASSERT_TRUE(range_enum.Advance(2));
  ASSERT_TRUE(check(0, 0, 0, 1, 0));
  ASSERT_TRUE(range_enum.Advance(3));
  ASSERT_TRUE(check(0, 1, 0, 0, 0));
  ASSERT_TRUE(range_enum.Advance(4));
  ASSERT_TRUE(check(0, 1, 0, 1, 1));
  ASSERT_TRUE(range_enum.Advance(5));
  ASSERT_TRUE(check(1, 0, 0, 1, 0));
  ASSERT_TRUE(range_enum.Advance(20));
  ASSERT_TRUE(check(2, 1, 0, 1, 2));
  ASSERT_FALSE(range_enum.Advance());
  ASSERT_TRUE(check(0, 0, 0, 0, 0));
}

TEST(SynthesisEnumerate, RangeEnumerateAdvanceFilter) {
  RangeEnumerate range_enum;

  range_enum.Extend(3, "0");
  range_enum.Extend(2, "1");
  range_enum.Extend(1, "2");
  range_enum.Extend(2, "3");
  range_enum.Extend(3, "4");
  ASSERT_EQ(range_enum.combinations(), 36);

  auto check = [& re = range_enum](std::size_t v4, std::size_t v3,
                                   std::size_t v2, std::size_t v1,
                                   std::size_t v0) {
    return re.GetState("4").value() == v4 && re.GetState("3").value() == v3 &&
           re.GetState("2").value() == v2 && re.GetState("1").value() == v1 &&
           re.GetState("0").value() == v0;
  };

  auto validate = [](const RangeEnumerate& next) -> RangeEnumerate::ID {
    if (next.GetState(0).value() == 1) {
      return 0;
    } else if (next.GetState(4).value() == 1 && next.GetState(1).value() == 1) {
      return 4;
    }
    return RangeEnumerate::kInvalidID;
  };

  ASSERT_TRUE(range_enum.Advance(1, validate));
  ASSERT_TRUE(check(0, 0, 0, 0, 2));  // filtered, + 2
  ASSERT_TRUE(range_enum.Advance(2, validate));
  ASSERT_TRUE(check(0, 0, 0, 1, 2));  // filtered, + 3
  ASSERT_TRUE(range_enum.Advance(9, validate));
  ASSERT_TRUE(check(1, 0, 0, 0, 2));
  ASSERT_TRUE(range_enum.Advance(1, validate));
  ASSERT_TRUE(check(2, 0, 0, 1, 0));
  ASSERT_TRUE(range_enum.Advance(8, validate));
  ASSERT_TRUE(check(2, 1, 0, 1, 2));
  ASSERT_FALSE(range_enum.Advance(2, validate));
  ASSERT_TRUE(check(0, 0, 0, 0, 1));  // filter not applied on overflow
}

TEST(SynthesisEnumerate, RangeEnumerateSetters) {
  RangeEnumerate range_enum;
  range_enum.Extend(3, "foo");
  range_enum.Extend(3, "bar");

  ASSERT_EQ(range_enum.states().front().value(), 0);
  ASSERT_EQ(range_enum.states().back().value(), 0);
  range_enum.SetMax();
  ASSERT_EQ(range_enum.states().front().value(), 2);
  ASSERT_EQ(range_enum.states().back().value(), 2);

  RangeEnumerate copy(range_enum);
  copy.Extend(10, "baz");  // discarded by assignment
  copy = range_enum;
  RangeEnumerate copy2 = std::move(copy);  // testing move
  ASSERT_EQ(copy.combinations(), 0);
  copy = std::move(copy2);
  ASSERT_EQ(copy.combinations(), 9);
  ASSERT_EQ(copy2.combinations(), 0);

  ASSERT_EQ(copy, range_enum);
  auto id = copy.Extend(3, "baz");
  ASSERT_NE(copy, range_enum);

  ASSERT_TRUE(copy.IsValid(id));
  ASSERT_FALSE(range_enum.IsValid(id));
  ASSERT_EQ(copy.combinations(), 27);
  ASSERT_EQ(copy[id], 0);
  ASSERT_TRUE(copy.Advance());
  ASSERT_EQ(copy[id], 1);

  ASSERT_FALSE(range_enum.Advance());
  range_enum.SetFrom(copy);
  ASSERT_EQ(range_enum.combinations(), 9);
  ASSERT_EQ(range_enum.states().size(), 2);
  ASSERT_TRUE(range_enum.Advance());
  ASSERT_EQ(range_enum.states().front().value(), 1);
  ASSERT_EQ(range_enum.states().back().value(), 0);

  range_enum.SetMax();
  copy.SetFrom(range_enum);
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

TEST(SynthesisEnumerate, RangeEnumerateMatcherWildcards) {
  RangeEnumerate range_enum;

  range_enum.Extend(3, "0");
  range_enum.Extend(3, "1");
  range_enum.Extend(3, "2");
  range_enum.Extend(3, "3");
  ASSERT_EQ(range_enum.combinations(), 81);

  auto check = [& re = range_enum](std::size_t v3, std::size_t v2,
                                   std::size_t v1, std::size_t v0) {
    return re.GetState("3").value() == v3 && re.GetState("2").value() == v2 &&
           re.GetState("1").value() == v1 && re.GetState("0").value() == v0;
  };

  ASSERT_TRUE(check(0, 0, 0, 0));

  RangeEnumerateMatcher matcher(0);  // wildcard = 0

  // Only wildcards not permitted.
  try {
    matcher.Insert(range_enum);
    FAIL();
  } catch (const std::logic_error& e) {
  }

  range_enum.Advance();
  ASSERT_TRUE(check(0, 0, 0, 1));

  // No patterns in matcher:
  ASSERT_EQ(matcher.Match(range_enum), RangeEnumerate::kInvalidID);

  // Only one concrete value in patterns:
  matcher.Insert(range_enum);
  matcher.Insert(range_enum);               // idempotent
  ASSERT_EQ(matcher.Match(range_enum), 0);  // same as pattern

  range_enum.Advance(3);
  ASSERT_TRUE(check(0, 0, 1, 1));
  ASSERT_EQ(matcher.Match(range_enum), 0);  // wildcard + concrete

  // No overlapping concrete values:
  range_enum.Advance(2);
  ASSERT_TRUE(check(0, 0, 2, 0));
  ASSERT_EQ(matcher.Match(range_enum), RangeEnumerate::kInvalidID);

  // Multiple patterns:
  matcher.Insert(range_enum);
  //  now have patterns:
  //    [*, *, *, 1]
  //    [*, *, 2, *]

  ASSERT_EQ(matcher.Match(range_enum), 1);

  range_enum.Advance();
  ASSERT_TRUE(check(0, 0, 2, 1));
  // There are several matches now, but we want the least significant matching
  // position returned on a match:
  ASSERT_EQ(matcher.Match(range_enum), 0);

  range_enum.Advance();
  ASSERT_TRUE(check(0, 0, 2, 2));
  ASSERT_EQ(matcher.Match(range_enum), 1);

  range_enum.Advance();
  ASSERT_TRUE(check(0, 1, 0, 0));
  ASSERT_EQ(matcher.Match(range_enum), RangeEnumerate::kInvalidID);

  range_enum.Advance();
  ASSERT_TRUE(check(0, 1, 0, 1));
  ASSERT_EQ(matcher.Match(range_enum), 0);
  matcher.Insert(range_enum);  // useless pattern

  range_enum.Advance();
  ASSERT_TRUE(check(0, 1, 0, 2));
  ASSERT_EQ(matcher.Match(range_enum), RangeEnumerate::kInvalidID);
  matcher.Insert(range_enum);
  ASSERT_EQ(matcher.Match(range_enum), 0);

  //  now have patterns:
  //    [*, *, *, 1]
  //    [*, *, 2, *]
  //    [*, 1, *, 1]
  //    [*, 1, *, 2]

  range_enum.Advance();
  ASSERT_TRUE(check(0, 1, 1, 0));
  ASSERT_EQ(matcher.Match(range_enum), RangeEnumerate::kInvalidID);

  range_enum.Advance(2);
  ASSERT_TRUE(check(0, 1, 1, 2));
  ASSERT_EQ(matcher.Match(range_enum), 0);

  range_enum.Advance(34);
  ASSERT_TRUE(check(1, 2, 1, 0));
  ASSERT_EQ(matcher.Match(range_enum), RangeEnumerate::kInvalidID);
  matcher.Insert(range_enum);
  ASSERT_EQ(matcher.Match(range_enum), 1);

  //  now have patterns:
  //    [*, *, *, 1]
  //    [*, *, 2, *]
  //    [*, 1, *, 1]
  //    [*, 1, *, 2]
  //    [1, 2, 1, *]

  range_enum.Advance(2);
  ASSERT_TRUE(check(1, 2, 1, 2));
  ASSERT_EQ(matcher.Match(range_enum), 1);

  range_enum.Advance();
  ASSERT_TRUE(check(1, 2, 2, 0));
  ASSERT_EQ(matcher.Match(range_enum), 1);

  range_enum.Advance(24);
  ASSERT_TRUE(check(2, 2, 1, 0));
  ASSERT_EQ(matcher.Match(range_enum), RangeEnumerate::kInvalidID);
}

TEST(SynthesisEnumerate, RangeEnumerateMatcherExact) {
  RangeEnumerate range_enum;

  range_enum.Extend(5, "0");
  range_enum.Extend(5, "1");
  range_enum.Extend(5, "2");

  RangeEnumerateMatcher matcher(0);

  auto& states = *const_cast<RangeEnumerate::States*>(&range_enum.states());
  ASSERT_EQ(states[0].label(), "0");
  ASSERT_EQ(states[1].label(), "1");
  ASSERT_EQ(states[2].label(), "2");

  states[0].set_value(3);
  states[1].set_value(1);
  states[2].set_value(1);
  matcher.Insert(range_enum);

  states[0].set_value(2);
  states[1].set_value(1);
  states[2].set_value(2);
  matcher.Insert(range_enum);

  states[0].set_value(3);
  states[1].set_value(1);
  states[2].set_value(2);
  ASSERT_EQ(matcher.Match(range_enum), RangeEnumerate::kInvalidID);
}

TEST(SynthesisEnumerate, LambdaOptions) {
  RangeEnumerate range_enum;
  LambdaOptions<bool(int)> lo1(
      "Lambdas1", {[](int i) { return i == 0; }, [](int i) { return i == 1; },
                   [](int i) { return i == 2; }});

  decltype(lo1) lo2("Lambdas2", lo1);

  ASSERT_EQ(range_enum.combinations(), 0);
  ASSERT_TRUE(lo1.GetCurrent(range_enum, true)(0));
  ASSERT_EQ(range_enum.combinations(), 0);

  int i = 0;
  do {
    ASSERT_TRUE(lo1[range_enum](i % 3));
    ASSERT_TRUE(lo2[range_enum](i / 3));
    ++i;
  } while (range_enum.Advance());

  ASSERT_EQ(range_enum.combinations(), 9);
  ASSERT_EQ("Lambdas1", range_enum.GetState(lo1.id()).label());
  ASSERT_EQ("Lambdas2", range_enum.GetState(lo2.id()).label());
}

/* vim: set ts=2 sts=2 sw=2 et : */
