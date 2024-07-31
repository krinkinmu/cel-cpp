// Copyright 2024 Google LLC
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

#include "extensions/protobuf/type.h"

#include "absl/base/attributes.h"
#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "common/type.h"
#include "common/type_factory.h"
#include "common/types/type_cache.h"
#include "internal/status_macros.h"
#include "google/protobuf/descriptor.h"

namespace cel::extensions {

namespace {

absl::StatusOr<Type> ProtoSingularFieldTypeToType(
    TypeFactory& type_factory,
    absl::Nonnull<const google::protobuf::FieldDescriptor*> field_desc) {
  switch (field_desc->type()) {
    case google::protobuf::FieldDescriptor::TYPE_FLOAT:
      ABSL_FALLTHROUGH_INTENDED;
    case google::protobuf::FieldDescriptor::TYPE_DOUBLE:
      return DoubleType{};
    case google::protobuf::FieldDescriptor::TYPE_SFIXED32:
      ABSL_FALLTHROUGH_INTENDED;
    case google::protobuf::FieldDescriptor::TYPE_SINT32:
      ABSL_FALLTHROUGH_INTENDED;
    case google::protobuf::FieldDescriptor::TYPE_INT32:
      ABSL_FALLTHROUGH_INTENDED;
    case google::protobuf::FieldDescriptor::TYPE_SFIXED64:
      ABSL_FALLTHROUGH_INTENDED;
    case google::protobuf::FieldDescriptor::TYPE_SINT64:
      ABSL_FALLTHROUGH_INTENDED;
    case google::protobuf::FieldDescriptor::TYPE_INT64:
      return IntType{};
    case google::protobuf::FieldDescriptor::TYPE_FIXED32:
      ABSL_FALLTHROUGH_INTENDED;
    case google::protobuf::FieldDescriptor::TYPE_UINT32:
      ABSL_FALLTHROUGH_INTENDED;
    case google::protobuf::FieldDescriptor::TYPE_FIXED64:
      ABSL_FALLTHROUGH_INTENDED;
    case google::protobuf::FieldDescriptor::TYPE_UINT64:
      return UintType{};
    case google::protobuf::FieldDescriptor::TYPE_BOOL:
      return BoolType{};
    case google::protobuf::FieldDescriptor::TYPE_STRING:
      return StringType{};
    case google::protobuf::FieldDescriptor::TYPE_GROUP:
      ABSL_FALLTHROUGH_INTENDED;
    case google::protobuf::FieldDescriptor::TYPE_MESSAGE:
      return ProtoTypeToType(type_factory, field_desc->message_type());
    case google::protobuf::FieldDescriptor::TYPE_BYTES:
      return BytesType{};
    case google::protobuf::FieldDescriptor::TYPE_ENUM:
      return ProtoEnumTypeToType(type_factory, field_desc->enum_type());
    default:
      return absl::InvalidArgumentError(
          absl::StrCat("unexpected protocol buffer message field type: ",
                       google::protobuf::FieldDescriptor::TypeName(field_desc->type())));
  }
}

}  // namespace

absl::StatusOr<Type> ProtoTypeToType(
    TypeFactory& type_factory, absl::Nonnull<const google::protobuf::Descriptor*> desc) {
  switch (desc->well_known_type()) {
    case google::protobuf::Descriptor::WELLKNOWNTYPE_FLOATVALUE:
      ABSL_FALLTHROUGH_INTENDED;
    case google::protobuf::Descriptor::WELLKNOWNTYPE_DOUBLEVALUE:
      return DoubleWrapperType{};
    case google::protobuf::Descriptor::WELLKNOWNTYPE_INT32VALUE:
      ABSL_FALLTHROUGH_INTENDED;
    case google::protobuf::Descriptor::WELLKNOWNTYPE_INT64VALUE:
      return IntWrapperType{};
    case google::protobuf::Descriptor::WELLKNOWNTYPE_UINT32VALUE:
      ABSL_FALLTHROUGH_INTENDED;
    case google::protobuf::Descriptor::WELLKNOWNTYPE_UINT64VALUE:
      return UintWrapperType{};
    case google::protobuf::Descriptor::WELLKNOWNTYPE_STRINGVALUE:
      return StringWrapperType{};
    case google::protobuf::Descriptor::WELLKNOWNTYPE_BYTESVALUE:
      return BytesWrapperType{};
    case google::protobuf::Descriptor::WELLKNOWNTYPE_BOOLVALUE:
      return BoolWrapperType{};
    case google::protobuf::Descriptor::WELLKNOWNTYPE_ANY:
      return AnyType{};
    case google::protobuf::Descriptor::WELLKNOWNTYPE_DURATION:
      return DurationType{};
    case google::protobuf::Descriptor::WELLKNOWNTYPE_TIMESTAMP:
      return TimestampType{};
    case google::protobuf::Descriptor::WELLKNOWNTYPE_VALUE:
      return DynType{};
    case google::protobuf::Descriptor::WELLKNOWNTYPE_LISTVALUE:
      return common_internal::ProcessLocalTypeCache::Get()->GetDynListType();
    case google::protobuf::Descriptor::WELLKNOWNTYPE_STRUCT:
      return common_internal::ProcessLocalTypeCache::Get()
          ->GetStringDynMapType();
    default:
      return type_factory.CreateStructType(desc->full_name());
  }
}

absl::StatusOr<Type> ProtoEnumTypeToType(
    TypeFactory&, absl::Nonnull<const google::protobuf::EnumDescriptor*> desc) {
  if (desc->full_name() == "google.protobuf.NullValue") {
    return NullType{};
  }
  return IntType{};
}

absl::StatusOr<Type> ProtoFieldTypeToType(
    TypeFactory& type_factory,
    absl::Nonnull<const google::protobuf::FieldDescriptor*> field_desc) {
  if (field_desc->is_map()) {
    Type map_key_scratch;
    Type map_value_scratch;
    CEL_ASSIGN_OR_RETURN(
        auto key_type,
        ProtoFieldTypeToType(type_factory,
                             field_desc->message_type()->map_key()));
    CEL_ASSIGN_OR_RETURN(
        auto value_type,
        ProtoFieldTypeToType(type_factory,
                             field_desc->message_type()->map_value()));
    return type_factory.CreateMapType(key_type, value_type);
  }
  if (field_desc->is_repeated()) {
    CEL_ASSIGN_OR_RETURN(auto element_type, ProtoSingularFieldTypeToType(
                                                type_factory, field_desc));
    return type_factory.CreateListType(element_type);
  }
  return ProtoSingularFieldTypeToType(type_factory, field_desc);
}

}  // namespace cel::extensions
