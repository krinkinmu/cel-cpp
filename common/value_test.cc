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

#include "common/value.h"

#include <sstream>

#include "google/protobuf/struct.pb.h"
#include "google/protobuf/type.pb.h"
#include "google/protobuf/descriptor.pb.h"
#include "absl/base/attributes.h"
#include "absl/status/status.h"
#include "absl/types/optional.h"
#include "common/native_type.h"
#include "common/type.h"
#include "common/value_testing.h"
#include "internal/testing.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/generated_enum_reflection.h"

namespace cel {
namespace {

using ::absl_testing::StatusIs;
using ::testing::_;
using ::testing::An;
using ::testing::Eq;
using ::testing::NotNull;
using ::testing::Optional;

TEST(Value, KindDebugDeath) {
  Value value;
  static_cast<void>(value);
  EXPECT_DEBUG_DEATH(static_cast<void>(value.kind()), _);
}

TEST(Value, GetTypeName) {
  Value value;
  static_cast<void>(value);
  EXPECT_DEBUG_DEATH(static_cast<void>(value.GetTypeName()), _);
}

TEST(Value, DebugStringUinitializedValue) {
  Value value;
  static_cast<void>(value);
  std::ostringstream out;
  out << value;
  EXPECT_EQ(out.str(), "default ctor Value");
}

TEST(Value, NativeValueIdDebugDeath) {
  Value value;
  static_cast<void>(value);
  EXPECT_DEBUG_DEATH(static_cast<void>(NativeTypeId::Of(value)), _);
}

TEST(Value, GeneratedEnum) {
  EXPECT_EQ(Value::Enum(google::protobuf::NULL_VALUE), NullValue());
  EXPECT_EQ(Value::Enum(google::protobuf::SYNTAX_EDITIONS), IntValue(2));
}

TEST(Value, DynamicEnum) {
  EXPECT_THAT(
      Value::Enum(google::protobuf::GetEnumDescriptor<google::protobuf::NullValue>(), 0),
      test::IsNullValue());
  EXPECT_THAT(
      Value::Enum(google::protobuf::GetEnumDescriptor<google::protobuf::NullValue>()
                      ->FindValueByNumber(0)),
      test::IsNullValue());
  EXPECT_THAT(
      Value::Enum(google::protobuf::GetEnumDescriptor<google::protobuf::Syntax>(), 2),
      test::IntValueIs(2));
  EXPECT_THAT(Value::Enum(google::protobuf::GetEnumDescriptor<google::protobuf::Syntax>()
                              ->FindValueByNumber(2)),
              test::IntValueIs(2));
}

TEST(Value, DynamicClosedEnum) {
  google::protobuf::FileDescriptorProto file_descriptor;
  file_descriptor.set_name("test/closed_enum.proto");
  file_descriptor.set_package("test");
  file_descriptor.set_syntax("editions");
  file_descriptor.set_edition(google::protobuf::EDITION_2023);
  {
    auto* enum_descriptor = file_descriptor.add_enum_type();
    enum_descriptor->set_name("ClosedEnum");
    enum_descriptor->mutable_options()->mutable_features()->set_enum_type(
        google::protobuf::FeatureSet::CLOSED);
    auto* enum_value_descriptor = enum_descriptor->add_value();
    enum_value_descriptor->set_number(1);
    enum_value_descriptor->set_name("FOO");
    enum_value_descriptor = enum_descriptor->add_value();
    enum_value_descriptor->set_number(2);
    enum_value_descriptor->set_name("BAR");
  }
  google::protobuf::DescriptorPool pool;
  ASSERT_THAT(pool.BuildFile(file_descriptor), NotNull());
  const auto* enum_descriptor = pool.FindEnumTypeByName("test.ClosedEnum");
  ASSERT_THAT(enum_descriptor, NotNull());
  EXPECT_THAT(Value::Enum(enum_descriptor, 0),
              test::ErrorValueIs(StatusIs(absl::StatusCode::kInvalidArgument)));
}

TEST(Value, Is) {
  EXPECT_TRUE(Value(BoolValue()).Is<BoolValue>());

  EXPECT_TRUE(Value(BytesValue()).Is<BytesValue>());

  EXPECT_TRUE(Value(DoubleValue()).Is<DoubleValue>());

  EXPECT_TRUE(Value(DurationValue()).Is<DurationValue>());

  EXPECT_TRUE(Value(ErrorValue()).Is<ErrorValue>());

  EXPECT_TRUE(Value(IntValue()).Is<IntValue>());

  EXPECT_TRUE(Value(ListValue()).Is<ListValue>());

  EXPECT_TRUE(Value(MapValue()).Is<MapValue>());

  EXPECT_TRUE(Value(NullValue()).Is<NullValue>());

  EXPECT_TRUE(Value(OptionalValue()).Is<OpaqueValue>());
  EXPECT_TRUE(Value(OptionalValue()).Is<OptionalValue>());

  EXPECT_TRUE(Value(StringValue()).Is<StringValue>());

  EXPECT_TRUE(Value(TimestampValue()).Is<TimestampValue>());

  EXPECT_TRUE(Value(TypeValue(StringType())).Is<TypeValue>());

  EXPECT_TRUE(Value(UintValue()).Is<UintValue>());

  EXPECT_TRUE(Value(UnknownValue()).Is<UnknownValue>());
}

template <typename T>
constexpr T& AsLValueRef(T& t ABSL_ATTRIBUTE_LIFETIME_BOUND) {
  return t;
}

template <typename T>
constexpr const T& AsConstLValueRef(T& t ABSL_ATTRIBUTE_LIFETIME_BOUND) {
  return t;
}

template <typename T>
constexpr T&& AsRValueRef(T& t ABSL_ATTRIBUTE_LIFETIME_BOUND) {
  return static_cast<T&&>(t);
}

template <typename T>
constexpr const T&& AsConstRValueRef(T& t ABSL_ATTRIBUTE_LIFETIME_BOUND) {
  return static_cast<const T&&>(t);
}

TEST(Value, As) {
  EXPECT_THAT(Value(BoolValue()).As<BoolValue>(), Optional(An<BoolValue>()));
  EXPECT_THAT(Value(BoolValue()).As<ErrorValue>(), Eq(absl::nullopt));

  {
    Value value(BytesValue{});
    Value other_value = value;
    EXPECT_THAT(AsLValueRef<Value>(value).As<BytesValue>(),
                Optional(An<BytesValue>()));
    EXPECT_THAT(AsConstLValueRef<Value>(value).As<BytesValue>(),
                Optional(An<BytesValue>()));
    EXPECT_THAT(AsRValueRef<Value>(value).As<BytesValue>(),
                Optional(An<BytesValue>()));
    EXPECT_THAT(AsConstRValueRef<Value>(other_value).As<BytesValue>(),
                Optional(An<BytesValue>()));
  }

  EXPECT_THAT(Value(DoubleValue()).As<DoubleValue>(),
              Optional(An<DoubleValue>()));
  EXPECT_THAT(Value(DoubleValue()).As<ErrorValue>(), Eq(absl::nullopt));

  EXPECT_THAT(Value(DurationValue()).As<DurationValue>(),
              Optional(An<DurationValue>()));
  EXPECT_THAT(Value(DurationValue()).As<ErrorValue>(), Eq(absl::nullopt));

  {
    Value value(ErrorValue{});
    Value other_value = value;
    EXPECT_THAT(AsLValueRef<Value>(value).As<ErrorValue>(),
                Optional(An<ErrorValue>()));
    EXPECT_THAT(AsConstLValueRef<Value>(value).As<ErrorValue>(),
                Optional(An<ErrorValue>()));
    EXPECT_THAT(AsRValueRef<Value>(value).As<ErrorValue>(),
                Optional(An<ErrorValue>()));
    EXPECT_THAT(AsConstRValueRef<Value>(other_value).As<ErrorValue>(),
                Optional(An<ErrorValue>()));
    EXPECT_THAT(Value(ErrorValue()).As<BoolValue>(), Eq(absl::nullopt));
  }

  EXPECT_THAT(Value(IntValue()).As<IntValue>(), Optional(An<IntValue>()));
  EXPECT_THAT(Value(IntValue()).As<ErrorValue>(), Eq(absl::nullopt));

  {
    Value value(ListValue{});
    Value other_value = value;
    EXPECT_THAT(AsLValueRef<Value>(value).As<ListValue>(),
                Optional(An<ListValue>()));
    EXPECT_THAT(AsConstLValueRef<Value>(value).As<ListValue>(),
                Optional(An<ListValue>()));
    EXPECT_THAT(AsRValueRef<Value>(value).As<ListValue>(),
                Optional(An<ListValue>()));
    EXPECT_THAT(AsConstRValueRef<Value>(other_value).As<ListValue>(),
                Optional(An<ListValue>()));
    EXPECT_THAT(Value(ListValue()).As<ErrorValue>(), Eq(absl::nullopt));
  }

  {
    Value value(MapValue{});
    Value other_value = value;
    EXPECT_THAT(AsLValueRef<Value>(value).As<MapValue>(),
                Optional(An<MapValue>()));
    EXPECT_THAT(AsConstLValueRef<Value>(value).As<MapValue>(),
                Optional(An<MapValue>()));
    EXPECT_THAT(AsRValueRef<Value>(value).As<MapValue>(),
                Optional(An<MapValue>()));
    EXPECT_THAT(AsConstRValueRef<Value>(other_value).As<MapValue>(),
                Optional(An<MapValue>()));
    EXPECT_THAT(Value(MapValue()).As<ErrorValue>(), Eq(absl::nullopt));
  }

  EXPECT_THAT(Value(NullValue()).As<NullValue>(), Optional(An<NullValue>()));
  EXPECT_THAT(Value(NullValue()).As<ErrorValue>(), Eq(absl::nullopt));

  {
    Value value(OptionalValue{});
    Value other_value = value;
    EXPECT_THAT(AsLValueRef<Value>(value).As<OpaqueValue>(),
                Optional(An<OpaqueValue>()));
    EXPECT_THAT(AsConstLValueRef<Value>(value).As<OpaqueValue>(),
                Optional(An<OpaqueValue>()));
    EXPECT_THAT(AsRValueRef<Value>(value).As<OpaqueValue>(),
                Optional(An<OpaqueValue>()));
    EXPECT_THAT(AsConstRValueRef<Value>(other_value).As<OpaqueValue>(),
                Optional(An<OpaqueValue>()));
    EXPECT_THAT(Value(OpaqueValue(OptionalValue())).As<ErrorValue>(),
                Eq(absl::nullopt));
  }

  {
    Value value(OptionalValue{});
    Value other_value = value;
    EXPECT_THAT(AsLValueRef<Value>(value).As<OptionalValue>(),
                Optional(An<OptionalValue>()));
    EXPECT_THAT(AsConstLValueRef<Value>(value).As<OptionalValue>(),
                Optional(An<OptionalValue>()));
    EXPECT_THAT(AsRValueRef<Value>(value).As<OptionalValue>(),
                Optional(An<OptionalValue>()));
    EXPECT_THAT(AsConstRValueRef<Value>(other_value).As<OptionalValue>(),
                Optional(An<OptionalValue>()));
    EXPECT_THAT(Value(OptionalValue()).As<ErrorValue>(), Eq(absl::nullopt));
  }

  {
    OpaqueValue value(OptionalValue{});
    OpaqueValue other_value = value;
    EXPECT_THAT(AsLValueRef<OpaqueValue>(value).As<OptionalValue>(),
                Optional(An<OptionalValue>()));
    EXPECT_THAT(AsConstLValueRef<OpaqueValue>(value).As<OptionalValue>(),
                Optional(An<OptionalValue>()));
    EXPECT_THAT(AsRValueRef<OpaqueValue>(value).As<OptionalValue>(),
                Optional(An<OptionalValue>()));
    EXPECT_THAT(AsConstRValueRef<OpaqueValue>(other_value).As<OptionalValue>(),
                Optional(An<OptionalValue>()));
  }

  {
    Value value(StringValue{});
    Value other_value = value;
    EXPECT_THAT(AsLValueRef<Value>(value).As<StringValue>(),
                Optional(An<StringValue>()));
    EXPECT_THAT(AsConstLValueRef<Value>(value).As<StringValue>(),
                Optional(An<StringValue>()));
    EXPECT_THAT(AsRValueRef<Value>(value).As<StringValue>(),
                Optional(An<StringValue>()));
    EXPECT_THAT(AsConstRValueRef<Value>(other_value).As<StringValue>(),
                Optional(An<StringValue>()));
    EXPECT_THAT(Value(StringValue()).As<ErrorValue>(), Eq(absl::nullopt));
  }

  EXPECT_THAT(Value(TimestampValue()).As<TimestampValue>(),
              Optional(An<TimestampValue>()));
  EXPECT_THAT(Value(TimestampValue()).As<ErrorValue>(), Eq(absl::nullopt));

  {
    Value value(TypeValue(StringType{}));
    Value other_value = value;
    EXPECT_THAT(AsLValueRef<Value>(value).As<TypeValue>(),
                Optional(An<TypeValue>()));
    EXPECT_THAT(AsConstLValueRef<Value>(value).As<TypeValue>(),
                Optional(An<TypeValue>()));
    EXPECT_THAT(AsRValueRef<Value>(value).As<TypeValue>(),
                Optional(An<TypeValue>()));
    EXPECT_THAT(AsConstRValueRef<Value>(other_value).As<TypeValue>(),
                Optional(An<TypeValue>()));
    EXPECT_THAT(Value(TypeValue(StringType())).As<ErrorValue>(),
                Eq(absl::nullopt));
  }

  EXPECT_THAT(Value(UintValue()).As<UintValue>(), Optional(An<UintValue>()));
  EXPECT_THAT(Value(UintValue()).As<ErrorValue>(), Eq(absl::nullopt));

  {
    Value value(UnknownValue{});
    Value other_value = value;
    EXPECT_THAT(AsLValueRef<Value>(value).As<UnknownValue>(),
                Optional(An<UnknownValue>()));
    EXPECT_THAT(AsConstLValueRef<Value>(value).As<UnknownValue>(),
                Optional(An<UnknownValue>()));
    EXPECT_THAT(AsRValueRef<Value>(value).As<UnknownValue>(),
                Optional(An<UnknownValue>()));
    EXPECT_THAT(AsConstRValueRef<Value>(other_value).As<UnknownValue>(),
                Optional(An<UnknownValue>()));
    EXPECT_THAT(Value(UnknownValue()).As<ErrorValue>(), Eq(absl::nullopt));
  }
}

TEST(Value, Cast) {
  EXPECT_THAT(static_cast<BoolValue>(Value(BoolValue())), An<BoolValue>());

  {
    Value value(BytesValue{});
    Value other_value = value;
    EXPECT_THAT(static_cast<BytesValue>(AsLValueRef<Value>(value)),
                An<BytesValue>());
    EXPECT_THAT(static_cast<BytesValue>(AsConstLValueRef<Value>(value)),
                An<BytesValue>());
    EXPECT_THAT(static_cast<BytesValue>(AsRValueRef<Value>(value)),
                An<BytesValue>());
    EXPECT_THAT(static_cast<BytesValue>(AsConstRValueRef<Value>(other_value)),
                An<BytesValue>());
  }

  EXPECT_THAT(static_cast<DoubleValue>(Value(DoubleValue())),
              An<DoubleValue>());

  EXPECT_THAT(static_cast<DurationValue>(Value(DurationValue())),
              An<DurationValue>());

  {
    Value value(ErrorValue{});
    Value other_value = value;
    EXPECT_THAT(static_cast<ErrorValue>(AsLValueRef<Value>(value)),
                An<ErrorValue>());
    EXPECT_THAT(static_cast<ErrorValue>(AsConstLValueRef<Value>(value)),
                An<ErrorValue>());
    EXPECT_THAT(static_cast<ErrorValue>(AsRValueRef<Value>(value)),
                An<ErrorValue>());
    EXPECT_THAT(static_cast<ErrorValue>(AsConstRValueRef<Value>(other_value)),
                An<ErrorValue>());
  }

  EXPECT_THAT(static_cast<IntValue>(Value(IntValue())), An<IntValue>());

  {
    Value value(ListValue{});
    Value other_value = value;
    EXPECT_THAT(static_cast<ListValue>(AsLValueRef<Value>(value)),
                An<ListValue>());
    EXPECT_THAT(static_cast<ListValue>(AsConstLValueRef<Value>(value)),
                An<ListValue>());
    EXPECT_THAT(static_cast<ListValue>(AsRValueRef<Value>(value)),
                An<ListValue>());
    EXPECT_THAT(static_cast<ListValue>(AsConstRValueRef<Value>(other_value)),
                An<ListValue>());
  }

  {
    Value value(MapValue{});
    Value other_value = value;
    EXPECT_THAT(static_cast<MapValue>(AsLValueRef<Value>(value)),
                An<MapValue>());
    EXPECT_THAT(static_cast<MapValue>(AsConstLValueRef<Value>(value)),
                An<MapValue>());
    EXPECT_THAT(static_cast<MapValue>(AsRValueRef<Value>(value)),
                An<MapValue>());
    EXPECT_THAT(static_cast<MapValue>(AsConstRValueRef<Value>(other_value)),
                An<MapValue>());
  }

  EXPECT_THAT(static_cast<NullValue>(Value(NullValue())), An<NullValue>());

  {
    Value value(OptionalValue{});
    Value other_value = value;
    EXPECT_THAT(static_cast<OpaqueValue>(AsLValueRef<Value>(value)),
                An<OpaqueValue>());
    EXPECT_THAT(static_cast<OpaqueValue>(AsConstLValueRef<Value>(value)),
                An<OpaqueValue>());
    EXPECT_THAT(static_cast<OpaqueValue>(AsRValueRef<Value>(value)),
                An<OpaqueValue>());
    EXPECT_THAT(static_cast<OpaqueValue>(AsConstRValueRef<Value>(other_value)),
                An<OpaqueValue>());
  }

  {
    Value value(OptionalValue{});
    Value other_value = value;
    EXPECT_THAT(static_cast<OptionalValue>(AsLValueRef<Value>(value)),
                An<OptionalValue>());
    EXPECT_THAT(static_cast<OptionalValue>(AsConstLValueRef<Value>(value)),
                An<OptionalValue>());
    EXPECT_THAT(static_cast<OptionalValue>(AsRValueRef<Value>(value)),
                An<OptionalValue>());
    EXPECT_THAT(
        static_cast<OptionalValue>(AsConstRValueRef<Value>(other_value)),
        An<OptionalValue>());
  }

  {
    OpaqueValue value(OptionalValue{});
    OpaqueValue other_value = value;
    EXPECT_THAT(static_cast<OptionalValue>(AsLValueRef<OpaqueValue>(value)),
                An<OptionalValue>());
    EXPECT_THAT(
        static_cast<OptionalValue>(AsConstLValueRef<OpaqueValue>(value)),
        An<OptionalValue>());
    EXPECT_THAT(static_cast<OptionalValue>(AsRValueRef<OpaqueValue>(value)),
                An<OptionalValue>());
    EXPECT_THAT(
        static_cast<OptionalValue>(AsConstRValueRef<OpaqueValue>(other_value)),
        An<OptionalValue>());
  }

  {
    Value value(StringValue{});
    Value other_value = value;
    EXPECT_THAT(static_cast<StringValue>(AsLValueRef<Value>(value)),
                An<StringValue>());
    EXPECT_THAT(static_cast<StringValue>(AsConstLValueRef<Value>(value)),
                An<StringValue>());
    EXPECT_THAT(static_cast<StringValue>(AsRValueRef<Value>(value)),
                An<StringValue>());
    EXPECT_THAT(static_cast<StringValue>(AsConstRValueRef<Value>(other_value)),
                An<StringValue>());
  }

  EXPECT_THAT(static_cast<TimestampValue>(Value(TimestampValue())),
              An<TimestampValue>());

  {
    Value value(TypeValue(StringType{}));
    Value other_value = value;
    EXPECT_THAT(static_cast<TypeValue>(AsLValueRef<Value>(value)),
                An<TypeValue>());
    EXPECT_THAT(static_cast<TypeValue>(AsConstLValueRef<Value>(value)),
                An<TypeValue>());
    EXPECT_THAT(static_cast<TypeValue>(AsRValueRef<Value>(value)),
                An<TypeValue>());
    EXPECT_THAT(static_cast<TypeValue>(AsConstRValueRef<Value>(other_value)),
                An<TypeValue>());
  }

  EXPECT_THAT(static_cast<UintValue>(Value(UintValue())), An<UintValue>());

  {
    Value value(UnknownValue{});
    Value other_value = value;
    EXPECT_THAT(static_cast<UnknownValue>(AsLValueRef<Value>(value)),
                An<UnknownValue>());
    EXPECT_THAT(static_cast<UnknownValue>(AsConstLValueRef<Value>(value)),
                An<UnknownValue>());
    EXPECT_THAT(static_cast<UnknownValue>(AsRValueRef<Value>(value)),
                An<UnknownValue>());
    EXPECT_THAT(static_cast<UnknownValue>(AsConstRValueRef<Value>(other_value)),
                An<UnknownValue>());
  }
}

}  // namespace
}  // namespace cel
