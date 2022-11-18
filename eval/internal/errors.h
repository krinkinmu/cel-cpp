// Copyright 2022 Google LLC
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

#ifndef THIRD_PARTY_CEL_CPP_EVAL_INTERNAL_ERRORS_H_
#define THIRD_PARTY_CEL_CPP_EVAL_INTERNAL_ERRORS_H_

#include "google/protobuf/arena.h"
#include "absl/status/status.h"
#include "base/memory_manager.h"

namespace cel::interop_internal {

constexpr absl::string_view kErrNoMatchingOverload =
    "No matching overloads found";
constexpr absl::string_view kErrNoSuchField = "no_such_field";
constexpr absl::string_view kErrNoSuchKey = "Key not found in map";
// Error name for MissingAttributeError indicating that evaluation has
// accessed an attribute whose value is undefined. go/terminal-unknown
constexpr absl::string_view kErrMissingAttribute = "MissingAttributeError: ";
constexpr absl::string_view kPayloadUrlMissingAttributePath =
    "missing_attribute_path";
constexpr absl::string_view kPayloadUrlUnknownFunctionResult =
    "cel_is_unknown_function_result";

const absl::Status* DurationOverflowError();

// Exclusive bounds for valid duration values.
constexpr absl::Duration kDurationHigh = absl::Seconds(315576000001);
constexpr absl::Duration kDurationLow = absl::Seconds(-315576000001);

// Factories for absl::Status values for well-known CEL errors.
// Results are arena allocated raw pointers to support interop with cel::Handle
// and expr::runtime::CelValue.
// Memory manager implementation is assumed to be google::protobuf::Arena.
const absl::Status* CreateNoMatchingOverloadError(cel::MemoryManager& manager,
                                                  absl::string_view fn);

const absl::Status* CreateNoMatchingOverloadError(google::protobuf::Arena* arena,
                                                  absl::string_view fn);

const absl::Status* CreateNoSuchFieldError(cel::MemoryManager& manager,
                                           absl::string_view field);

const absl::Status* CreateNoSuchFieldError(google::protobuf::Arena* arena,
                                           absl::string_view field);

const absl::Status* CreateNoSuchKeyError(cel::MemoryManager& manager,
                                         absl::string_view key);

const absl::Status* CreateNoSuchKeyError(google::protobuf::Arena* arena,
                                         absl::string_view key);

const absl::Status* CreateUnknownValueError(google::protobuf::Arena* arena,
                                            absl::string_view unknown_path);

const absl::Status* CreateMissingAttributeError(
    google::protobuf::Arena* arena, absl::string_view missing_attribute_path);

const absl::Status* CreateMissingAttributeError(
    cel::MemoryManager& manager, absl::string_view missing_attribute_path);

const absl::Status* CreateUnknownFunctionResultError(
    cel::MemoryManager& manager, absl::string_view help_message);

const absl::Status* CreateUnknownFunctionResultError(
    google::protobuf::Arena* arena, absl::string_view help_message);

}  // namespace cel::interop_internal

#endif  // THIRD_PARTY_CEL_CPP_EVAL_INTERNAL_ERRORS_H_
