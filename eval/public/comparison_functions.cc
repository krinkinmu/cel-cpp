// Copyright 2021 Google LLC
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

#include "eval/public/comparison_functions.h"

#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <type_traits>
#include <vector>

#include "absl/status/status.h"
#include "absl/time/time.h"
#include "absl/types/optional.h"
#include "base/function_adapter.h"
#include "base/handle.h"
#include "base/value_factory.h"
#include "base/values/bytes_value.h"
#include "base/values/string_value.h"
#include "eval/public/cel_builtins.h"
#include "eval/public/cel_function_registry.h"
#include "eval/public/cel_number.h"
#include "eval/public/cel_options.h"
#include "internal/status_macros.h"

namespace google::api::expr::runtime {

namespace {

using ::cel::BinaryFunctionAdapter;
using ::cel::BytesValue;
using ::cel::Handle;
using ::cel::StringValue;
using ::cel::ValueFactory;

// Comparison template functions
template <class Type>
bool LessThan(ValueFactory&, Type t1, Type t2) {
  return (t1 < t2);
}

template <class Type>
bool LessThanOrEqual(ValueFactory&, Type t1, Type t2) {
  return (t1 <= t2);
}

template <class Type>
bool GreaterThan(ValueFactory& factory, Type t1, Type t2) {
  return LessThan(factory, t2, t1);
}

template <class Type>
bool GreaterThanOrEqual(ValueFactory& factory, Type t1, Type t2) {
  return LessThanOrEqual(factory, t2, t1);
}

// String value comparions specializations
template <>
bool LessThan(ValueFactory&, const Handle<StringValue>& t1,
              const Handle<StringValue>& t2) {
  return t1->Compare(*t2) < 0;
}

template <>
bool LessThanOrEqual(ValueFactory&, const Handle<StringValue>& t1,
                     const Handle<StringValue>& t2) {
  return t1->Compare(*t2) <= 0;
}

template <>
bool GreaterThan(ValueFactory&, const Handle<StringValue>& t1,
                 const Handle<StringValue>& t2) {
  return t1->Compare(*t2) > 0;
}

template <>
bool GreaterThanOrEqual(ValueFactory&, const Handle<StringValue>& t1,
                        const Handle<StringValue>& t2) {
  return t1->Compare(*t2) >= 0;
}

// bytes value comparions specializations
template <>
bool LessThan(ValueFactory&, const Handle<BytesValue>& t1,
              const Handle<BytesValue>& t2) {
  return t1->Compare(*t2) < 0;
}

template <>
bool LessThanOrEqual(ValueFactory&, const Handle<BytesValue>& t1,
                     const Handle<BytesValue>& t2) {
  return t1->Compare(*t2) <= 0;
}

template <>
bool GreaterThan(ValueFactory&, const Handle<BytesValue>& t1,
                 const Handle<BytesValue>& t2) {
  return t1->Compare(*t2) > 0;
}

template <>
bool GreaterThanOrEqual(ValueFactory&, const Handle<BytesValue>& t1,
                        const Handle<BytesValue>& t2) {
  return t1->Compare(*t2) >= 0;
}

// Duration comparison specializations
template <>
bool LessThan(ValueFactory&, absl::Duration t1, absl::Duration t2) {
  return absl::operator<(t1, t2);
}

template <>
bool LessThanOrEqual(ValueFactory&, absl::Duration t1, absl::Duration t2) {
  return absl::operator<=(t1, t2);
}

template <>
bool GreaterThan(ValueFactory&, absl::Duration t1, absl::Duration t2) {
  return absl::operator>(t1, t2);
}

template <>
bool GreaterThanOrEqual(ValueFactory&, absl::Duration t1, absl::Duration t2) {
  return absl::operator>=(t1, t2);
}

// Timestamp comparison specializations
template <>
bool LessThan(ValueFactory&, absl::Time t1, absl::Time t2) {
  return absl::operator<(t1, t2);
}

template <>
bool LessThanOrEqual(ValueFactory&, absl::Time t1, absl::Time t2) {
  return absl::operator<=(t1, t2);
}

template <>
bool GreaterThan(ValueFactory&, absl::Time t1, absl::Time t2) {
  return absl::operator>(t1, t2);
}

template <>
bool GreaterThanOrEqual(ValueFactory&, absl::Time t1, absl::Time t2) {
  return absl::operator>=(t1, t2);
}

template <typename T, typename U>
bool CrossNumericLessThan(ValueFactory&, T t, U u) {
  return CelNumber(t) < CelNumber(u);
}

template <typename T, typename U>
bool CrossNumericGreaterThan(ValueFactory&, T t, U u) {
  return CelNumber(t) > CelNumber(u);
}

template <typename T, typename U>
bool CrossNumericLessOrEqualTo(ValueFactory&, T t, U u) {
  return CelNumber(t) <= CelNumber(u);
}

template <typename T, typename U>
bool CrossNumericGreaterOrEqualTo(ValueFactory&, T t, U u) {
  return CelNumber(t) >= CelNumber(u);
}

template <class Type>
absl::Status RegisterComparisonFunctionsForType(CelFunctionRegistry* registry) {
  using FunctionAdapter = BinaryFunctionAdapter<bool, Type, Type>;
  CEL_RETURN_IF_ERROR(registry->Register(
      FunctionAdapter::CreateDescriptor(builtin::kLess, false),
      FunctionAdapter::WrapFunction(LessThan<Type>)));

  CEL_RETURN_IF_ERROR(registry->Register(
      FunctionAdapter::CreateDescriptor(builtin::kLessOrEqual, false),
      FunctionAdapter::WrapFunction(LessThanOrEqual<Type>)));

  CEL_RETURN_IF_ERROR(registry->Register(
      FunctionAdapter::CreateDescriptor(builtin::kGreater, false),
      FunctionAdapter::WrapFunction(GreaterThan<Type>)));

  CEL_RETURN_IF_ERROR(registry->Register(
      FunctionAdapter::CreateDescriptor(builtin::kGreaterOrEqual, false),
      FunctionAdapter::WrapFunction(GreaterThanOrEqual<Type>)));

  return absl::OkStatus();
}

absl::Status RegisterHomogenousComparisonFunctions(
    CelFunctionRegistry* registry) {
  CEL_RETURN_IF_ERROR(RegisterComparisonFunctionsForType<bool>(registry));

  CEL_RETURN_IF_ERROR(RegisterComparisonFunctionsForType<int64_t>(registry));

  CEL_RETURN_IF_ERROR(RegisterComparisonFunctionsForType<uint64_t>(registry));

  CEL_RETURN_IF_ERROR(RegisterComparisonFunctionsForType<double>(registry));

  CEL_RETURN_IF_ERROR(
      RegisterComparisonFunctionsForType<const Handle<StringValue>&>(registry));

  CEL_RETURN_IF_ERROR(
      RegisterComparisonFunctionsForType<const Handle<BytesValue>&>(registry));

  CEL_RETURN_IF_ERROR(
      RegisterComparisonFunctionsForType<absl::Duration>(registry));

  CEL_RETURN_IF_ERROR(RegisterComparisonFunctionsForType<absl::Time>(registry));

  return absl::OkStatus();
}

template <typename T, typename U>
absl::Status RegisterCrossNumericComparisons(CelFunctionRegistry* registry) {
  using FunctionAdapter = BinaryFunctionAdapter<bool, T, U>;
  CEL_RETURN_IF_ERROR(registry->Register(
      FunctionAdapter::CreateDescriptor(builtin::kLess,
                                        /*receiver_style=*/false),
      FunctionAdapter::WrapFunction(&CrossNumericLessThan<T, U>)));
  CEL_RETURN_IF_ERROR(registry->Register(
      FunctionAdapter::CreateDescriptor(builtin::kGreater,
                                        /*receiver_style=*/false),
      FunctionAdapter::WrapFunction(&CrossNumericGreaterThan<T, U>)));
  CEL_RETURN_IF_ERROR(registry->Register(
      FunctionAdapter::CreateDescriptor(builtin::kGreaterOrEqual,
                                        /*receiver_style=*/false),
      FunctionAdapter::WrapFunction(&CrossNumericGreaterOrEqualTo<T, U>)));
  CEL_RETURN_IF_ERROR(registry->Register(
      FunctionAdapter::CreateDescriptor(builtin::kLessOrEqual,
                                        /*receiver_style=*/false),
      FunctionAdapter::WrapFunction(&CrossNumericLessOrEqualTo<T, U>)));
  return absl::OkStatus();
}

absl::Status RegisterHeterogeneousComparisonFunctions(
    CelFunctionRegistry* registry) {

  CEL_RETURN_IF_ERROR(
      (RegisterCrossNumericComparisons<double, int64_t>(registry)));
  CEL_RETURN_IF_ERROR(
      (RegisterCrossNumericComparisons<double, uint64_t>(registry)));

  CEL_RETURN_IF_ERROR(
      (RegisterCrossNumericComparisons<uint64_t, double>(registry)));
  CEL_RETURN_IF_ERROR(
      (RegisterCrossNumericComparisons<uint64_t, int64_t>(registry)));

  CEL_RETURN_IF_ERROR(
      (RegisterCrossNumericComparisons<int64_t, double>(registry)));
  CEL_RETURN_IF_ERROR(
      (RegisterCrossNumericComparisons<int64_t, uint64_t>(registry)));

  CEL_RETURN_IF_ERROR(RegisterComparisonFunctionsForType<bool>(registry));
  CEL_RETURN_IF_ERROR(RegisterComparisonFunctionsForType<int64_t>(registry));
  CEL_RETURN_IF_ERROR(RegisterComparisonFunctionsForType<uint64_t>(registry));
  CEL_RETURN_IF_ERROR(RegisterComparisonFunctionsForType<double>(registry));
  CEL_RETURN_IF_ERROR(
      RegisterComparisonFunctionsForType<const Handle<StringValue>&>(registry));
  CEL_RETURN_IF_ERROR(
      RegisterComparisonFunctionsForType<const Handle<BytesValue>&>(registry));
  CEL_RETURN_IF_ERROR(
      RegisterComparisonFunctionsForType<absl::Duration>(registry));
  CEL_RETURN_IF_ERROR(RegisterComparisonFunctionsForType<absl::Time>(registry));

  return absl::OkStatus();
}
}  // namespace


absl::Status RegisterComparisonFunctions(CelFunctionRegistry* registry,
                                         const InterpreterOptions& options) {
  if (options.enable_heterogeneous_equality) {
    CEL_RETURN_IF_ERROR(RegisterHeterogeneousComparisonFunctions(registry));
  } else {
    CEL_RETURN_IF_ERROR(RegisterHomogenousComparisonFunctions(registry));
  }
  return absl::OkStatus();
}

}  // namespace google::api::expr::runtime
