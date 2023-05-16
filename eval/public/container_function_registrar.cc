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

#include "eval/public/container_function_registrar.h"

#include <vector>

#include "absl/status/status.h"
#include "base/builtins.h"
#include "base/function_adapter.h"
#include "base/handle.h"
#include "base/value.h"
#include "base/value_factory.h"
#include "base/values/list_value.h"
#include "base/values/map_value.h"
#include "eval/eval/mutable_list_impl.h"
#include "eval/internal/interop.h"
#include "eval/public/cel_function_registry.h"
#include "eval/public/cel_options.h"
#include "eval/public/containers/container_backed_list_impl.h"
#include "eval/public/portable_cel_function_adapter.h"
#include "extensions/protobuf/memory_manager.h"
#include "google/protobuf/arena.h"

namespace google::api::expr::runtime {
namespace {

using ::cel::BinaryFunctionAdapter;
using ::cel::Handle;
using ::cel::ListValue;
using ::cel::MapValue;
using ::cel::UnaryFunctionAdapter;
using ::cel::Value;
using ::cel::ValueFactory;
using ::google::protobuf::Arena;

int64_t MapSizeImpl(ValueFactory&, const MapValue& value) {
  return value.size();
}

int64_t ListSizeImpl(ValueFactory&, const ListValue& value) {
  return value.size();
}

// Concatenation for CelList type.
absl::StatusOr<Handle<ListValue>> ConcatList(ValueFactory& factory,
                                             const Handle<ListValue>& value1,
                                             const Handle<ListValue>& value2) {
  std::vector<CelValue> joined_values;

  int size1 = value1->size();
  if (size1 == 0) {
    return value2;
  }
  int size2 = value2->size();
  if (size2 == 0) {
    return value1;
  }
  joined_values.reserve(size1 + size2);

  google::protobuf::Arena* arena = cel::extensions::ProtoMemoryManager::CastToProtoArena(
      factory.memory_manager());

  ListValue::GetContext context(factory);
  for (int i = 0; i < size1; i++) {
    CEL_ASSIGN_OR_RETURN(Handle<Value> elem, value1->Get(context, i));
    joined_values.push_back(
        cel::interop_internal::ModernValueToLegacyValueOrDie(arena, elem));
  }
  for (int i = 0; i < size2; i++) {
    CEL_ASSIGN_OR_RETURN(Handle<Value> elem, value2->Get(context, i));
    joined_values.push_back(
        cel::interop_internal::ModernValueToLegacyValueOrDie(arena, elem));
  }

  auto concatenated =
      Arena::Create<ContainerBackedListImpl>(arena, joined_values);

  return cel::interop_internal::CreateLegacyListValue(concatenated);
}

// AppendList will append the elements in value2 to value1.
//
// This call will only be invoked within comprehensions where `value1` is an
// intermediate result which cannot be directly assigned or co-mingled with a
// user-provided list.
const CelList* AppendList(Arena* arena, const CelList* value1,
                          const CelList* value2) {
  // The `value1` object cannot be directly addressed and is an intermediate
  // variable. Once the comprehension completes this value will in effect be
  // treated as immutable.
  MutableListImpl* mutable_list = const_cast<MutableListImpl*>(
      cel::internal::down_cast<const MutableListImpl*>(value1));
  for (int i = 0; i < value2->size(); i++) {
    mutable_list->Append((*value2).Get(arena, i));
  }
  return mutable_list;
}
}  // namespace

absl::Status RegisterContainerFunctions(CelFunctionRegistry* registry,
                                        const InterpreterOptions& options) {
  // receiver style = true/false
  // Support both the global and receiver style size() for lists and maps.
  for (bool receiver_style : {true, false}) {
    CEL_RETURN_IF_ERROR(registry->Register(
        cel::UnaryFunctionAdapter<int64_t, const ListValue&>::CreateDescriptor(
            cel::builtin::kSize, receiver_style),
        UnaryFunctionAdapter<int64_t, const ListValue&>::WrapFunction(
            ListSizeImpl)));

    CEL_RETURN_IF_ERROR(registry->Register(
        UnaryFunctionAdapter<int64_t, const MapValue&>::CreateDescriptor(
            cel::builtin::kSize, receiver_style),
        UnaryFunctionAdapter<int64_t, const MapValue&>::WrapFunction(
            MapSizeImpl)));
  }

  if (options.enable_list_concat) {
    CEL_RETURN_IF_ERROR(registry->Register(
        BinaryFunctionAdapter<
            absl::StatusOr<Handle<Value>>, const ListValue&,
            const ListValue&>::CreateDescriptor(cel::builtin::kAdd, false),
        BinaryFunctionAdapter<
            absl::StatusOr<Handle<Value>>, const Handle<ListValue>&,
            const Handle<ListValue>&>::WrapFunction(ConcatList)));
  }

  return registry->Register(
      PortableBinaryFunctionAdapter<
          const CelList*, const CelList*,
          const CelList*>::Create(cel::builtin::kRuntimeListAppend, false,
                                  AppendList));
}

}  // namespace google::api::expr::runtime
