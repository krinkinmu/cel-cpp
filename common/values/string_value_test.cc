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
#include <string>

#include "absl/hash/hash.h"
#include "absl/strings/cord.h"
#include "absl/strings/cord_test_helpers.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "common/any.h"
#include "common/casting.h"
#include "common/json.h"
#include "common/native_type.h"
#include "common/type.h"
#include "common/value.h"
#include "common/value_testing.h"
#include "internal/testing.h"

namespace cel {
namespace {

using testing::An;
using testing::Ne;
using cel::internal::IsOkAndHolds;

using StringValueTest = common_internal::ThreadCompatibleValueTest<>;

TEST_P(StringValueTest, Kind) {
  EXPECT_EQ(StringValue("foo").kind(), StringValue::kKind);
  EXPECT_EQ(Value(StringValue(absl::Cord("foo"))).kind(), StringValue::kKind);
}

TEST_P(StringValueTest, DebugString) {
  {
    std::ostringstream out;
    out << StringValue("foo");
    EXPECT_EQ(out.str(), "\"foo\"");
  }
  {
    std::ostringstream out;
    out << StringValue(absl::MakeFragmentedCord({"f", "o", "o"}));
    EXPECT_EQ(out.str(), "\"foo\"");
  }
  {
    std::ostringstream out;
    out << Value(StringValue(absl::Cord("foo")));
    EXPECT_EQ(out.str(), "\"foo\"");
  }
}

TEST_P(StringValueTest, GetSerializedSize) {
  EXPECT_THAT(StringValue().GetSerializedSize(value_manager()),
              IsOkAndHolds(0));
}

TEST_P(StringValueTest, ConvertToAny) {
  EXPECT_THAT(StringValue().ConvertToAny(value_manager()),
              IsOkAndHolds(MakeAny(MakeTypeUrl("google.protobuf.StringValue"),
                                   absl::Cord())));
}

TEST_P(StringValueTest, ConvertToJson) {
  EXPECT_THAT(StringValue("foo").ConvertToJson(value_manager()),
              IsOkAndHolds(Json(JsonString("foo"))));
}

TEST_P(StringValueTest, NativeValue) {
  std::string scratch;
  EXPECT_EQ(StringValue("foo").NativeString(), "foo");
  EXPECT_EQ(StringValue("foo").NativeString(scratch), "foo");
  EXPECT_EQ(StringValue("foo").NativeCord(), "foo");
}

TEST_P(StringValueTest, NativeTypeId) {
  EXPECT_EQ(NativeTypeId::Of(StringValue("foo")),
            NativeTypeId::For<StringValue>());
  EXPECT_EQ(NativeTypeId::Of(Value(StringValue(absl::Cord("foo")))),
            NativeTypeId::For<StringValue>());
}

TEST_P(StringValueTest, InstanceOf) {
  EXPECT_TRUE(InstanceOf<StringValue>(StringValue("foo")));
  EXPECT_TRUE(InstanceOf<StringValue>(Value(StringValue(absl::Cord("foo")))));
}

TEST_P(StringValueTest, Cast) {
  EXPECT_THAT(Cast<StringValue>(StringValue("foo")), An<StringValue>());
  EXPECT_THAT(Cast<StringValue>(Value(StringValue(absl::Cord("foo")))),
              An<StringValue>());
}

TEST_P(StringValueTest, As) {
  EXPECT_THAT(As<StringValue>(StringValue("foo")), Ne(absl::nullopt));
  EXPECT_THAT(As<StringValue>(Value(StringValue(absl::Cord("foo")))),
              Ne(absl::nullopt));
}

TEST_P(StringValueTest, HashValue) {
  EXPECT_EQ(absl::HashOf(StringValue("foo")),
            absl::HashOf(absl::string_view("foo")));
  EXPECT_EQ(absl::HashOf(StringValue(absl::string_view("foo"))),
            absl::HashOf(absl::string_view("foo")));
  EXPECT_EQ(absl::HashOf(StringValue(absl::Cord("foo"))),
            absl::HashOf(absl::string_view("foo")));
}

TEST_P(StringValueTest, Equality) {
  EXPECT_NE(StringValue("foo"), "bar");
  EXPECT_NE("bar", StringValue("foo"));
  EXPECT_NE(StringValue("foo"), StringValue("bar"));
  EXPECT_NE(StringValue("foo"), absl::Cord("bar"));
  EXPECT_NE(absl::Cord("bar"), StringValue("foo"));
}

TEST_P(StringValueTest, LessThan) {
  EXPECT_LT(StringValue("bar"), "foo");
  EXPECT_LT("bar", StringValue("foo"));
  EXPECT_LT(StringValue("bar"), StringValue("foo"));
  EXPECT_LT(StringValue("bar"), absl::Cord("foo"));
  EXPECT_LT(absl::Cord("bar"), StringValue("foo"));
}

INSTANTIATE_TEST_SUITE_P(
    StringValueTest, StringValueTest,
    ::testing::Combine(::testing::Values(MemoryManagement::kPooling,
                                         MemoryManagement::kReferenceCounting)),
    StringValueTest::ToString);

using StringValueViewTest = common_internal::ThreadCompatibleValueTest<>;

TEST_P(StringValueViewTest, Kind) {
  EXPECT_EQ(StringValueView("foo").kind(), StringValueView::kKind);
  EXPECT_EQ(ValueView(StringValueView("foo")).kind(), StringValueView::kKind);
}

TEST_P(StringValueViewTest, DebugString) {
  {
    std::ostringstream out;
    out << StringValueView("foo");
    EXPECT_EQ(out.str(), "\"foo\"");
  }
  {
    std::ostringstream out;
    out << ValueView(StringValueView("foo"));
    EXPECT_EQ(out.str(), "\"foo\"");
  }
}

TEST_P(StringValueViewTest, GetSerializedSize) {
  EXPECT_THAT(StringValueView().GetSerializedSize(value_manager()),
              IsOkAndHolds(0));
}

TEST_P(StringValueViewTest, ConvertToAny) {
  EXPECT_THAT(StringValueView().ConvertToAny(value_manager()),
              IsOkAndHolds(MakeAny(MakeTypeUrl("google.protobuf.StringValue"),
                                   absl::Cord())));
}

TEST_P(StringValueViewTest, ConvertToJson) {
  EXPECT_THAT(StringValueView("foo").ConvertToJson(value_manager()),
              IsOkAndHolds(Json(JsonString("foo"))));
}

TEST_P(StringValueViewTest, NativeValue) {
  std::string scratch;
  EXPECT_EQ(StringValueView(StringValue("foo")).NativeString(), "foo");
  EXPECT_EQ(StringValueView(StringValue("foo")).NativeString(scratch), "foo");
  EXPECT_EQ(StringValueView(StringValue("foo")).NativeCord(), "foo");
}

TEST_P(StringValueViewTest, NativeTypeId) {
  EXPECT_EQ(NativeTypeId::Of(StringValueView("foo")),
            NativeTypeId::For<StringValueView>());
  EXPECT_EQ(NativeTypeId::Of(ValueView(StringValueView("foo"))),
            NativeTypeId::For<StringValueView>());
}

TEST_P(StringValueViewTest, InstanceOf) {
  EXPECT_TRUE(InstanceOf<StringValueView>(StringValueView("foo")));
  EXPECT_TRUE(InstanceOf<StringValueView>(ValueView(StringValueView("foo"))));
}

TEST_P(StringValueViewTest, Cast) {
  EXPECT_THAT(Cast<StringValueView>(StringValueView("foo")),
              An<StringValueView>());
  EXPECT_THAT(Cast<StringValueView>(ValueView(StringValueView("foo"))),
              An<StringValueView>());
}

TEST_P(StringValueViewTest, As) {
  EXPECT_THAT(As<StringValueView>(StringValueView("foo")), Ne(absl::nullopt));
  EXPECT_THAT(As<StringValueView>(ValueView(StringValueView("foo"))),
              Ne(absl::nullopt));
}

TEST_P(StringValueViewTest, HashValue) {
  EXPECT_EQ(absl::HashOf(StringValueView("foo")),
            absl::HashOf(absl::string_view("foo")));
  EXPECT_EQ(absl::HashOf(StringValueView(absl::string_view("foo"))),
            absl::HashOf(absl::string_view("foo")));
  EXPECT_EQ(absl::HashOf(StringValueView(absl::Cord("foo"))),
            absl::HashOf(absl::string_view("foo")));
}

TEST_P(StringValueViewTest, Equality) {
  EXPECT_NE(StringValueView("foo"), "bar");
  EXPECT_NE("bar", StringValueView("foo"));
  EXPECT_NE(StringValueView("foo"), StringValueView("bar"));
  EXPECT_NE(StringValueView("foo"), absl::Cord("bar"));
  EXPECT_NE(absl::Cord("bar"), StringValueView("foo"));
  EXPECT_NE(StringValueView("foo"), StringValue("bar"));
  EXPECT_NE(StringValue("bar"), StringValueView("foo"));
}

TEST_P(StringValueViewTest, LessThan) {
  EXPECT_LT(StringValueView("bar"), "foo");
  EXPECT_LT("bar", StringValueView("foo"));
  EXPECT_LT(StringValueView("bar"), StringValueView("foo"));
  EXPECT_LT(StringValueView("bar"), absl::Cord("foo"));
  EXPECT_LT(absl::Cord("bar"), StringValueView("foo"));
  EXPECT_LT(StringValueView("bar"), StringValue("foo"));
  EXPECT_LT(StringValue("bar"), StringValueView("foo"));
}

INSTANTIATE_TEST_SUITE_P(
    StringValueViewTest, StringValueViewTest,
    ::testing::Combine(::testing::Values(MemoryManagement::kPooling,
                                         MemoryManagement::kReferenceCounting)),
    StringValueViewTest::ToString);

}  // namespace
}  // namespace cel
