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
#include "verc3/core/types.hh"

#include <gtest/gtest.h>

using namespace verc3::core;

TEST(CoreTypes, ArraySetCompare) {
  ArraySet<int, 3> x{0, 1, 2};
  ArraySet<int, 3> y{2, 1};

  ASSERT_NE(x, y);  // testing for valid_ working properly on init.

  ASSERT_EQ(x.NextInvalid(), y.GetUndefined());
  ASSERT_EQ(x.NextValid(), x.GetID(2));

  auto next_id = y.NextInvalid();
  ASSERT_NE(y.GetUndefined(), next_id);

  // Accessing const does not set valid and returns nullptr
  const auto& y_const = y;
  ASSERT_FALSE(y_const.IsValid(next_id));
  ASSERT_EQ(y_const[next_id], nullptr);
  ASSERT_NE(y_const, x);

  *y[next_id] = 0;
  ASSERT_EQ(y, x);

  ASSERT_FALSE(x.IsMember(x.GetUndefined()));
}

TEST(CoreTypes, ArraySetUnion) {
  ArraySet<int, 3> x;
  ArraySet<int, 3, decltype(x)> y;

  static_assert(x.kBase == 1, "Wrong base: x");
  static_assert(y.kBase == 4, "Wrong base: y");
  static_assert(static_cast<int>(decltype(x)::ID::kUndefined) ==
                    static_cast<int>(decltype(y)::ID::kUndefined),
                "Unexpected Undefined value!");

  union Union {
    decltype(x)::ID ID_1;
    decltype(y)::ID ID_2;
  };

  Union un1;
  un1.ID_1 = x.NextInvalid();

  ASSERT_TRUE(x.IsMember(un1.ID_1));
  ASSERT_FALSE(y.IsMember(un1.ID_2));

  // Same with WeakUnion
  WeakUnion un2(x.NextInvalid());
  ASSERT_TRUE(x.IsMember(un2.id_as<decltype(x)::ID>()));
  ASSERT_FALSE(y.IsMember(un2.id_as<decltype(y)::ID>()));
}

TEST(CoreTypes, ArraySetIteration) {
  ArraySet<int, 5> x{1, 2};
  ASSERT_EQ(x.size(), 2);

  const auto next_invalid = x.NextInvalid();
  ASSERT_EQ(x.Insert(3), next_invalid);
  ASSERT_EQ(x.size(), 3);

  x.for_each([](int v) { ASSERT_TRUE(v == 1 || v == 2 || v == 3); });

  const auto& xc = x;
  xc.for_each([](int v) {});

  ASSERT_FALSE(x.all_of([](const int& v) { return v > 2; }));
  ASSERT_TRUE(x.any_of([](const int& v) { return v > 2; }));
  ASSERT_EQ(2, x.count_if([](int v) { return v >= 2; }));

  std::size_t valid = 0;
  std::size_t total = 0;
  x.for_each_ID([&x, &valid, &total](decltype(x)::ID id) {
    if (x.IsValid(id)) ++valid;
    ++total;
  });
  ASSERT_EQ(valid, 3);
  ASSERT_EQ(total, 5);

  ASSERT_NE(x.Insert(0), x.GetUndefined());
  ASSERT_NE(x.Insert(0), x.GetUndefined());
  ASSERT_EQ(x.Insert(0), x.GetUndefined());

  ASSERT_EQ(x.size(), 5);
  ASSERT_NE(x.Erase(2), x.GetUndefined());
  ASSERT_EQ(x.size(), 4);
  ASSERT_EQ(x.Erase(2), x.GetUndefined());
  ASSERT_EQ(x.Erase(42), x.GetUndefined());
}

struct WithCustomHash {
  struct Hash {
    auto operator()(const WithCustomHash& k) const { return k.i; }
  };

  bool operator==(const WithCustomHash& rhs) const { return i == rhs.i; }

  int i = 0;
};

TEST(CoreTypes, ArraySetCustomHash) {
  ArraySet<WithCustomHash, 3> x(true);
  ASSERT_EQ(x.size(), 3);

  WithCustomHash some;

  x[x.NextValid()]->i = 1;
  x[x.GetID(some)]->i = 2;

  decltype(x)::Hash hasher;
  ASSERT_EQ(hasher(x), 42 + 3);
}

TEST(CoreTypes, ArraySetEnumClassHash) {
  ArraySet<int, 3> x;
  ArraySet<decltype(x)::ID, 2> y;

  decltype(y)::Hash hasher;
  ASSERT_EQ(hasher(y), 42);
}

TEST(CoreTypes, ArraySetOne) {
  ArraySet<int, 1> x;
  ArraySet<int, 1> y(true);
  ArraySet<int, 2> z;

  ASSERT_EQ(nullptr, x.One());
  ASSERT_NE(nullptr, y.One());
  ASSERT_EQ(nullptr, z.One());

  *z[z.NextInvalid()] = 1;
  ASSERT_NE(nullptr, z.One());
  ASSERT_EQ(1, *z.One());
  *z[z.NextInvalid()] = 2;
  ASSERT_EQ(nullptr, z.One());
}

TEST(CoreTypes, SetHash) {
  Set<int> int_set;
  Set<WithCustomHash> complex_set;

  decltype(int_set)::Hash int_set_hasher;
  ASSERT_EQ(int_set_hasher(int_set), 42);

  decltype(complex_set)::Hash complex_set_hasher;
  ASSERT_EQ(complex_set_hasher(complex_set), 42);

  WithCustomHash some;
  some.i = 3;
  complex_set.Insert(some);
  ASSERT_EQ(complex_set_hasher(complex_set), 42 + 3);

  // Test if Set of IDs.
  ArraySet<int, 1> as;
  Set<decltype(as)::ID> id_set;
  id_set.Insert(as.GetUndefined());
  ASSERT_EQ(id_set.size(), 1);
}

/* vim: set ts=2 sts=2 sw=2 et : */
