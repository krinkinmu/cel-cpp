// Copyright 2023 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <sstream>

#include "absl/hash/hash.h"
#include "absl/types/optional.h"
#include "common/casting.h"
#include "common/native_type.h"
#include "common/type.h"
#include "internal/testing.h"

namespace cel {
namespace {

using testing::An;
using testing::Ne;

TEST(BoolType, Kind) {
  EXPECT_EQ(BoolType().kind(), BoolType::kKind);
  EXPECT_EQ(Type(BoolType()).kind(), BoolType::kKind);
}

TEST(BoolType, Name) {
  EXPECT_EQ(BoolType().name(), BoolType::kName);
  EXPECT_EQ(Type(BoolType()).name(), BoolType::kName);
}

TEST(BoolType, DebugString) {
  {
    std::ostringstream out;
    out << BoolType();
    EXPECT_EQ(out.str(), BoolType::kName);
  }
  {
    std::ostringstream out;
    out << Type(BoolType());
    EXPECT_EQ(out.str(), BoolType::kName);
  }
}

TEST(BoolType, Hash) {
  EXPECT_EQ(absl::HashOf(BoolType()), absl::HashOf(BoolType()));
}

TEST(BoolType, Equal) {
  EXPECT_EQ(BoolType(), BoolType());
  EXPECT_EQ(Type(BoolType()), BoolType());
  EXPECT_EQ(BoolType(), Type(BoolType()));
  EXPECT_EQ(Type(BoolType()), Type(BoolType()));
}

TEST(BoolType, NativeTypeId) {
  EXPECT_EQ(NativeTypeId::Of(BoolType()), NativeTypeId::For<BoolType>());
  EXPECT_EQ(NativeTypeId::Of(Type(BoolType())), NativeTypeId::For<BoolType>());
}

TEST(BoolType, InstanceOf) {
  EXPECT_TRUE(InstanceOf<BoolType>(BoolType()));
  EXPECT_TRUE(InstanceOf<BoolType>(Type(BoolType())));
}

TEST(BoolType, Cast) {
  EXPECT_THAT(Cast<BoolType>(BoolType()), An<BoolType>());
  EXPECT_THAT(Cast<BoolType>(Type(BoolType())), An<BoolType>());
}

TEST(BoolType, As) {
  EXPECT_THAT(As<BoolType>(BoolType()), Ne(absl::nullopt));
  EXPECT_THAT(As<BoolType>(Type(BoolType())), Ne(absl::nullopt));
}

}  // namespace
}  // namespace cel
