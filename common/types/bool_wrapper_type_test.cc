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

TEST(BoolWrapperType, Kind) {
  EXPECT_EQ(BoolWrapperType().kind(), BoolWrapperType::kKind);
  EXPECT_EQ(Type(BoolWrapperType()).kind(), BoolWrapperType::kKind);
}

TEST(BoolWrapperType, Name) {
  EXPECT_EQ(BoolWrapperType().name(), BoolWrapperType::kName);
  EXPECT_EQ(Type(BoolWrapperType()).name(), BoolWrapperType::kName);
}

TEST(BoolWrapperType, DebugString) {
  {
    std::ostringstream out;
    out << BoolWrapperType();
    EXPECT_EQ(out.str(), BoolWrapperType::kName);
  }
  {
    std::ostringstream out;
    out << Type(BoolWrapperType());
    EXPECT_EQ(out.str(), BoolWrapperType::kName);
  }
}

TEST(BoolWrapperType, Hash) {
  EXPECT_EQ(absl::HashOf(BoolWrapperType()), absl::HashOf(BoolWrapperType()));
}

TEST(BoolWrapperType, Equal) {
  EXPECT_EQ(BoolWrapperType(), BoolWrapperType());
  EXPECT_EQ(Type(BoolWrapperType()), BoolWrapperType());
  EXPECT_EQ(BoolWrapperType(), Type(BoolWrapperType()));
  EXPECT_EQ(Type(BoolWrapperType()), Type(BoolWrapperType()));
}

TEST(BoolWrapperType, NativeTypeId) {
  EXPECT_EQ(NativeTypeId::Of(BoolWrapperType()),
            NativeTypeId::For<BoolWrapperType>());
  EXPECT_EQ(NativeTypeId::Of(Type(BoolWrapperType())),
            NativeTypeId::For<BoolWrapperType>());
}

TEST(BoolWrapperType, InstanceOf) {
  EXPECT_TRUE(InstanceOf<BoolWrapperType>(BoolWrapperType()));
  EXPECT_TRUE(InstanceOf<BoolWrapperType>(Type(BoolWrapperType())));
}

TEST(BoolWrapperType, Cast) {
  EXPECT_THAT(Cast<BoolWrapperType>(BoolWrapperType()), An<BoolWrapperType>());
  EXPECT_THAT(Cast<BoolWrapperType>(Type(BoolWrapperType())),
              An<BoolWrapperType>());
}

TEST(BoolWrapperType, As) {
  EXPECT_THAT(As<BoolWrapperType>(BoolWrapperType()), Ne(absl::nullopt));
  EXPECT_THAT(As<BoolWrapperType>(Type(BoolWrapperType())), Ne(absl::nullopt));
}

}  // namespace
}  // namespace cel
