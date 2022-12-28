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

#ifndef THIRD_PARTY_CEL_CPP_EVAL_INTERNAL_ADAPTER_ACTIVATION_IMPL_H_
#define THIRD_PARTY_CEL_CPP_EVAL_INTERNAL_ADAPTER_ACTIVATION_IMPL_H_

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "base/memory_manager.h"
#include "base/value.h"
#include "eval/internal/activation_interface.h"
#include "eval/public/base_activation.h"

namespace cel::interop_internal {

// An Activation implementation that adapts the legacy version (based on
// expr::CelValue) to the new cel::Handle based version. This implementation
// must be scoped to an evaluation.
class AdapterActivationImpl : public ActivationInterface {
 public:
  explicit AdapterActivationImpl(
      const google::api::expr::runtime::BaseActivation& legacy_activation)
      : legacy_activation_(legacy_activation) {}

  absl::optional<Handle<Value>> ResolveVariable(
      MemoryManager& manager, absl::string_view name) const override;

 private:
  const google::api::expr::runtime::BaseActivation& legacy_activation_;
};

}  // namespace cel::interop_internal

#endif  // THIRD_PARTY_CEL_CPP_EVAL_INTERNAL_ADAPTER_ACTIVATION_IMPL_H_
