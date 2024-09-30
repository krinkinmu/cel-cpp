// Copyright 2024 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "checker/internal/type_checker_impl.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/base/no_destructor.h"
#include "absl/base/nullability.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"
#include "absl/types/optional.h"
#include "absl/types/span.h"
#include "base/ast_internal/ast_impl.h"
#include "base/ast_internal/expr.h"
#include "checker/internal/builtins_arena.h"
#include "checker/internal/namespace_generator.h"
#include "checker/internal/type_check_env.h"
#include "checker/internal/type_inference_context.h"
#include "checker/type_check_issue.h"
#include "checker/validation_result.h"
#include "common/ast.h"
#include "common/ast_rewrite.h"
#include "common/ast_traverse.h"
#include "common/ast_visitor.h"
#include "common/ast_visitor_base.h"
#include "common/constant.h"
#include "common/decl.h"
#include "common/expr.h"
#include "common/memory.h"
#include "common/source.h"
#include "common/type.h"
#include "common/type_factory.h"
#include "common/type_kind.h"
#include "extensions/protobuf/memory_manager.h"
#include "internal/status_macros.h"
#include "google/protobuf/arena.h"

namespace cel::checker_internal {
namespace {

class TrivialTypeFactory : public TypeFactory {
 public:
  explicit TrivialTypeFactory(absl::Nonnull<google::protobuf::Arena*> arena)
      : arena_(arena) {}

  MemoryManagerRef GetMemoryManager() const override {
    return extensions::ProtoMemoryManagerRef(arena_);
  }

 private:
  absl::Nonnull<google::protobuf::Arena*> arena_;
};

using cel::ast_internal::AstImpl;

using AstType = cel::ast_internal::Type;
using Severity = TypeCheckIssue::Severity;

Type FreeListType() {
  static absl::NoDestructor<Type> kInstance(
      Type(ListType(BuiltinsArena(), TypeParamType("element_type"))));
  return *kInstance;
}

Type FreeMapType() {
  static absl::NoDestructor<Type> kInstance(
      Type(MapType(BuiltinsArena(), TypeParamType("key_type"),
                   TypeParamType("value_type"))));
  return *kInstance;
}

std::string FormatCandidate(absl::Span<const std::string> qualifiers) {
  return absl::StrJoin(qualifiers, ".");
}

SourceLocation ComputeSourceLocation(const AstImpl& ast, int64_t expr_id) {
  const auto& source_info = ast.source_info();
  auto iter = source_info.positions().find(expr_id);
  if (iter == source_info.positions().end()) {
    return SourceLocation{};
  }
  int32_t absolute_position = iter->second;
  int32_t line_idx = -1;
  for (int32_t offset : source_info.line_offsets()) {
    if (absolute_position >= offset) {
      break;
    }
    ++line_idx;
  }

  if (line_idx < 0 || line_idx >= source_info.line_offsets().size()) {
    return SourceLocation{1, absolute_position};
  }

  int32_t rel_position =
      absolute_position - source_info.line_offsets()[line_idx] + 1;

  return SourceLocation{line_idx + 1, rel_position};
}

// Flatten the type to the AST type representation to remove any lifecycle
// dependency between the type check environment and the AST.
//
// TODO: It may be better to do this at the point of serialization
// in the future, but requires corresponding change for the runtime to correctly
// rehydrate the serialized Ast.
absl::StatusOr<AstType> FlattenType(const Type& type);

absl::StatusOr<AstType> FlattenAbstractType(const OpaqueType& type) {
  std::vector<AstType> parameter_types;
  parameter_types.reserve(type.GetParameters().size());
  for (const auto& param : type.GetParameters()) {
    CEL_ASSIGN_OR_RETURN(auto param_type, FlattenType(param));
    parameter_types.push_back(std::move(param_type));
  }

  return AstType(ast_internal::AbstractType(std::string(type.name()),
                                            std::move(parameter_types)));
}

absl::StatusOr<AstType> FlattenMapType(const MapType& type) {
  CEL_ASSIGN_OR_RETURN(auto key, FlattenType(type.key()));
  CEL_ASSIGN_OR_RETURN(auto value, FlattenType(type.value()));

  return AstType(
      ast_internal::MapType(std::make_unique<AstType>(std::move(key)),
                            std::make_unique<AstType>(std::move(value))));
}

absl::StatusOr<AstType> FlattenListType(const ListType& type) {
  CEL_ASSIGN_OR_RETURN(auto elem, FlattenType(type.element()));

  return AstType(
      ast_internal::ListType(std::make_unique<AstType>(std::move(elem))));
}

absl::StatusOr<AstType> FlattenMessageType(const StructType& type) {
  return AstType(ast_internal::MessageType(std::string(type.name())));
}

absl::StatusOr<AstType> FlattenTypeType(const TypeType& type) {
  if (type.GetParameters().size() > 1) {
    return absl::InternalError(
        absl::StrCat("Unsupported type: ", type.DebugString()));
  }
  if (type.GetParameters().empty()) {
    return AstType(std::make_unique<AstType>());
  }
  CEL_ASSIGN_OR_RETURN(auto param, FlattenType(type.GetParameters()[0]));
  return AstType(std::make_unique<AstType>(std::move(param)));
}

absl::StatusOr<AstType> FlattenType(const Type& type) {
  switch (type.kind()) {
    case TypeKind::kDyn:
      return AstType(ast_internal::DynamicType());
    case TypeKind::kError:
      return AstType(ast_internal::ErrorType());
    case TypeKind::kNull:
      return AstType(ast_internal::NullValue());
    case TypeKind::kBool:
      return AstType(ast_internal::PrimitiveType::kBool);
    case TypeKind::kInt:
      return AstType(ast_internal::PrimitiveType::kInt64);
    case TypeKind::kUint:
      return AstType(ast_internal::PrimitiveType::kUint64);
    case TypeKind::kDouble:
      return AstType(ast_internal::PrimitiveType::kDouble);
    case TypeKind::kString:
      return AstType(ast_internal::PrimitiveType::kString);
    case TypeKind::kBytes:
      return AstType(ast_internal::PrimitiveType::kBytes);
    case TypeKind::kDuration:
      return AstType(ast_internal::WellKnownType::kDuration);
    case TypeKind::kTimestamp:
      return AstType(ast_internal::WellKnownType::kTimestamp);
    case TypeKind::kStruct:
      return FlattenMessageType(type.GetStruct());
    case TypeKind::kList:
      return FlattenListType(type.GetList());
    case TypeKind::kMap:
      return FlattenMapType(type.GetMap());
    case TypeKind::kOpaque:
      return FlattenAbstractType(type.GetOpaque());
    case TypeKind::kBoolWrapper:
      return AstType(ast_internal::PrimitiveTypeWrapper(
          ast_internal::PrimitiveType::kBool));
    case TypeKind::kIntWrapper:
      return AstType(ast_internal::PrimitiveTypeWrapper(
          ast_internal::PrimitiveType::kInt64));
    case TypeKind::kUintWrapper:
      return AstType(ast_internal::PrimitiveTypeWrapper(
          ast_internal::PrimitiveType::kUint64));
    case TypeKind::kDoubleWrapper:
      return AstType(ast_internal::PrimitiveTypeWrapper(
          ast_internal::PrimitiveType::kDouble));
    case TypeKind::kStringWrapper:
      return AstType(ast_internal::PrimitiveTypeWrapper(
          ast_internal::PrimitiveType::kString));
    case TypeKind::kBytesWrapper:
      return AstType(ast_internal::PrimitiveTypeWrapper(
          ast_internal::PrimitiveType::kBytes));
    case TypeKind::kTypeParam:
      // Convert any remaining free type params to dyn.
      return AstType(ast_internal::DynamicType());
    case TypeKind::kType:
      return FlattenTypeType(type.GetType());
    case TypeKind::kAny:
      return AstType(ast_internal::WellKnownType::kAny);
    default:
      return absl::InternalError(
          absl::StrCat("Unsupported type: ", type.DebugString()));
  }
}  // namespace

class ResolveVisitor : public AstVisitorBase {
 public:
  struct FunctionResolution {
    const FunctionDecl* decl;
    bool namespace_rewrite;
  };

  ResolveVisitor(absl::string_view container,
                 NamespaceGenerator namespace_generator,
                 const TypeCheckEnv& env, const AstImpl& ast,
                 TypeInferenceContext& inference_context,
                 std::vector<TypeCheckIssue>& issues,
                 absl::Nonnull<google::protobuf::Arena*> arena, TypeFactory& type_factory)
      : container_(container),
        namespace_generator_(std::move(namespace_generator)),
        env_(&env),
        inference_context_(&inference_context),
        issues_(&issues),
        ast_(&ast),
        root_scope_(env.MakeVariableScope()),
        arena_(arena),
        type_factory_(&type_factory),
        current_scope_(&root_scope_) {}

  void PreVisitExpr(const Expr& expr) override { expr_stack_.push_back(&expr); }

  void PostVisitExpr(const Expr& expr) override {
    if (expr_stack_.empty()) {
      return;
    }
    expr_stack_.pop_back();
  }

  void PostVisitConst(const Expr& expr, const Constant& constant) override;

  void PreVisitComprehension(const Expr& expr,
                             const ComprehensionExpr& comprehension) override;

  void PostVisitComprehension(const Expr& expr,
                              const ComprehensionExpr& comprehension) override;

  void PostVisitMap(const Expr& expr, const MapExpr& map) override;

  void PostVisitList(const Expr& expr, const ListExpr& list) override;

  void PreVisitComprehensionSubexpression(
      const Expr& expr, const ComprehensionExpr& comprehension,
      ComprehensionArg comprehension_arg) override;

  void PostVisitComprehensionSubexpression(
      const Expr& expr, const ComprehensionExpr& comprehension,
      ComprehensionArg comprehension_arg) override;

  void PostVisitIdent(const Expr& expr, const IdentExpr& ident) override;

  void PostVisitSelect(const Expr& expr, const SelectExpr& select) override;

  void PostVisitCall(const Expr& expr, const CallExpr& call) override;

  void PostVisitStruct(const Expr& expr,
                       const StructExpr& create_struct) override;

  // Accessors for resolved values.
  const absl::flat_hash_map<const Expr*, FunctionResolution>& functions()
      const {
    return functions_;
  }

  const absl::flat_hash_map<const Expr*, const VariableDecl*>& attributes()
      const {
    return attributes_;
  }

  const absl::flat_hash_map<const Expr*, std::string>& struct_types() const {
    return struct_types_;
  }

  const absl::flat_hash_map<const Expr*, Type>& types() const { return types_; }

  const absl::Status& status() const { return status_; }

 private:
  struct ComprehensionScope {
    const Expr* comprehension_expr;
    const VariableScope* parent;
    VariableScope* accu_scope;
    VariableScope* iter_scope;
  };

  struct FunctionOverloadMatch {
    // Overall result type.
    // If resolution is incomplete, this will be DynType.
    Type result_type;
    // A new declaration with the narrowed overload candidates.
    // Owned by the Check call scoped arena.
    const FunctionDecl* decl;
  };

  void ResolveSimpleIdentifier(const Expr& expr, absl::string_view name);

  void ResolveQualifiedIdentifier(const Expr& expr,
                                  absl::Span<const std::string> qualifiers);

  // Resolves the function call shape (i.e. the number of arguments and call
  // style) for the given function call.
  const FunctionDecl* ResolveFunctionCallShape(const Expr& expr,
                                               absl::string_view function_name,
                                               int arg_count, bool is_receiver);

  // Resolves the applicable function overloads for the given function call.
  //
  // If found, assigns a new function decl with the resolved overloads.
  void ResolveFunctionOverloads(const Expr& expr, const FunctionDecl& decl,
                                int arg_count, bool is_receiver,
                                bool is_namespaced);

  void ResolveSelectOperation(const Expr& expr, absl::string_view field,
                              const Expr& operand);

  void ReportMissingReference(const Expr& expr, absl::string_view name) {
    issues_->push_back(TypeCheckIssue::CreateError(
        ComputeSourceLocation(*ast_, expr.id()),
        absl::StrCat("undeclared reference to '", name, "' (in container '",
                     container_, "')")));
  }

  void ReportUndefinedField(int64_t expr_id, absl::string_view field_name,
                            absl::string_view struct_name) {
    issues_->push_back(TypeCheckIssue::CreateError(
        ComputeSourceLocation(*ast_, expr_id),
        absl::StrCat("undefined field '", field_name, "' not found in struct '",
                     struct_name, "'")));
  }

  absl::Status CheckFieldAssignments(const Expr& expr,
                                     const StructExpr& create_struct,
                                     Type struct_type,
                                     absl::string_view resolved_name) {
    for (const auto& field : create_struct.fields()) {
      const Expr* value = &field.value();
      Type value_type = GetTypeOrDyn(value);

      // Lookup message type by name to support WellKnownType creation.
      CEL_ASSIGN_OR_RETURN(
          absl::optional<StructTypeField> field_info,
          env_->LookupStructField(*type_factory_, resolved_name, field.name()));
      if (!field_info.has_value()) {
        ReportUndefinedField(field.id(), field.name(), resolved_name);
        continue;
      }
      Type field_type = field_info->GetType();
      if (field.optional()) {
        field_type = OptionalType(arena_, field_type);
      }
      if (!inference_context_->IsAssignable(value_type, field_type)) {
        issues_->push_back(TypeCheckIssue::CreateError(
            ComputeSourceLocation(*ast_, field.id()),
            absl::StrCat("expected type of field '", field_info->name(),
                         "' is '", field_type.DebugString(),
                         "' but provided type is '", value_type.DebugString(),
                         "'")));
        continue;
      }
    }

    return absl::OkStatus();
  }

  // TODO: This should switch to a failing check once all core
  // features are supported. For now, we allow dyn for implementing the
  // typechecker behaviors in isolation.
  Type GetTypeOrDyn(const Expr* expr) {
    auto iter = types_.find(expr);
    return iter != types_.end() ? iter->second : DynType();
  }

  absl::string_view container_;
  NamespaceGenerator namespace_generator_;
  absl::Nonnull<const TypeCheckEnv*> env_;
  absl::Nonnull<TypeInferenceContext*> inference_context_;
  absl::Nonnull<std::vector<TypeCheckIssue>*> issues_;
  absl::Nonnull<const ast_internal::AstImpl*> ast_;
  VariableScope root_scope_;
  absl::Nonnull<google::protobuf::Arena*> arena_;
  absl::Nonnull<TypeFactory*> type_factory_;

  // state tracking for the traversal.
  const VariableScope* current_scope_;
  std::vector<const Expr*> expr_stack_;
  absl::flat_hash_map<const Expr*, std::vector<std::string>>
      maybe_namespaced_functions_;
  // Select operations that need to be resolved outside of the traversal.
  // These are handled separately to disambiguate between namespaces and field
  // accesses
  absl::flat_hash_set<const Expr*> deferred_select_operations_;
  absl::Status status_;
  std::vector<std::unique_ptr<VariableScope>> comprehension_vars_;
  std::vector<ComprehensionScope> comprehension_scopes_;

  // References that were resolved and may require AST rewrites.
  absl::flat_hash_map<const Expr*, FunctionResolution> functions_;
  absl::flat_hash_map<const Expr*, const VariableDecl*> attributes_;
  absl::flat_hash_map<const Expr*, std::string> struct_types_;

  absl::flat_hash_map<const Expr*, Type> types_;
};

void ResolveVisitor::PostVisitIdent(const Expr& expr, const IdentExpr& ident) {
  if (expr_stack_.size() == 1) {
    ResolveSimpleIdentifier(expr, ident.name());
    return;
  }

  // Walk up the stack to find the qualifiers.
  //
  // If the identifier is the target of a receiver call, then note
  // the function so we can disambiguate namespaced functions later.
  int stack_pos = expr_stack_.size() - 1;
  std::vector<std::string> qualifiers;
  qualifiers.push_back(ident.name());
  const Expr* receiver_call = nullptr;
  const Expr* root_candidate = expr_stack_[stack_pos];

  // Try to identify the root of the select chain, possibly as the receiver of
  // a function call.
  while (stack_pos > 0) {
    --stack_pos;
    const Expr* parent = expr_stack_[stack_pos];

    if (parent->has_call_expr() &&
        (&parent->call_expr().target() == root_candidate)) {
      receiver_call = parent;
      break;
    } else if (!parent->has_select_expr()) {
      break;
    }

    qualifiers.push_back(parent->select_expr().field());
    deferred_select_operations_.insert(parent);
    root_candidate = parent;
    if (parent->select_expr().test_only()) {
      break;
    }
  }

  if (receiver_call == nullptr) {
    ResolveQualifiedIdentifier(*root_candidate, qualifiers);
  } else {
    maybe_namespaced_functions_[receiver_call] = std::move(qualifiers);
  }
}

void ResolveVisitor::PostVisitConst(const Expr& expr,
                                    const Constant& constant) {
  switch (constant.kind().index()) {
    case ConstantKindIndexOf<std::nullptr_t>():
      types_[&expr] = NullType();
      break;
    case ConstantKindIndexOf<bool>():
      types_[&expr] = BoolType();
      break;
    case ConstantKindIndexOf<int64_t>():
      types_[&expr] = IntType();
      break;
    case ConstantKindIndexOf<uint64_t>():
      types_[&expr] = UintType();
      break;
    case ConstantKindIndexOf<double>():
      types_[&expr] = DoubleType();
      break;
    case ConstantKindIndexOf<BytesConstant>():
      types_[&expr] = BytesType();
      break;
    case ConstantKindIndexOf<StringConstant>():
      types_[&expr] = StringType();
      break;
    case ConstantKindIndexOf<absl::Duration>():
      types_[&expr] = DurationType();
      break;
    case ConstantKindIndexOf<absl::Time>():
      types_[&expr] = TimestampType();
      break;
    default:
      issues_->push_back(TypeCheckIssue::CreateError(
          ComputeSourceLocation(*ast_, expr.id()),
          absl::StrCat("unsupported constant type: ",
                       constant.kind().index())));
      break;
  }
}

bool IsSupportedKeyType(const Type& type) {
  switch (type.kind()) {
    case TypeKind::kBool:
    case TypeKind::kInt:
    case TypeKind::kUint:
    case TypeKind::kString:
    case TypeKind::kDyn:
      return true;
    default:
      return false;
  }
}

void ResolveVisitor::PostVisitMap(const Expr& expr, const MapExpr& map) {
  // Roughly follows map type inferencing behavior in Go.
  //
  // We try to infer the type of the map if all of the keys or values are
  // homogeneously typed, otherwise assume the type parameter is dyn (defer to
  // runtime for enforcing type compatibility).
  //
  // TODO: Widening behavior is not well documented for map / list
  // construction in the spec and is a bit inconsistent between implementations.
  //
  // In the future, we should probably default enforce homogeneously
  // typed maps unless tagged as JSON (and the values are assignable to
  // the JSON value union type).

  absl::optional<Type> overall_key_type;
  absl::optional<Type> overall_value_type;

  for (const auto& entry : map.entries()) {
    const Expr* key = &entry.key();
    Type key_type = GetTypeOrDyn(key);
    if (!IsSupportedKeyType(key_type)) {
      // The Go type checker implementation can allow any type as a map key, but
      // per the spec this should be limited to the types listed in
      // IsSupportedKeyType.
      //
      // To match the Go implementation, we just warn here, but in the future
      // we should consider making this an error.
      issues_->push_back(TypeCheckIssue(
          Severity::kWarning, ComputeSourceLocation(*ast_, key->id()),
          absl::StrCat("unsupported map key type: ", key_type.DebugString())));
    }
    if (overall_key_type.has_value()) {
      if (key_type != *overall_key_type) {
        overall_key_type = DynType();
      }
    } else {
      overall_key_type = key_type;
    }

    const Expr* value = &entry.value();
    Type value_type = GetTypeOrDyn(value);
    if (entry.optional()) {
      if (value_type.IsOptional()) {
        value_type = value_type.GetOptional().GetParameter();
      }
    }
    if (overall_value_type.has_value()) {
      if (value_type != *overall_value_type) {
        overall_value_type = DynType();
      }
    } else {
      overall_value_type = value_type;
    }
  }

  if (overall_value_type.has_value() && overall_key_type.has_value()) {
    types_[&expr] = MapType(arena_, *overall_key_type, *overall_value_type);
    return;
  } else if (overall_value_type.has_value() != overall_key_type.has_value()) {
    status_.Update(absl::InternalError(
        "Map has mismatched key and value type inference resolution"));
    return;
  }

  types_[&expr] = inference_context_->InstantiateTypeParams(FreeMapType());
}

void ResolveVisitor::PostVisitList(const Expr& expr, const ListExpr& list) {
  // Follows list type inferencing behavior in Go (see map comments above).

  absl::optional<Type> overall_value_type;

  for (const auto& element : list.elements()) {
    const Expr* value = &element.expr();
    Type value_type = GetTypeOrDyn(value);
    if (element.optional()) {
      if (value_type.IsOptional()) {
        value_type = value_type.GetOptional().GetParameter();
      }
    }
    if (overall_value_type.has_value()) {
      if (value_type != *overall_value_type) {
        overall_value_type = DynType();
      }
    } else {
      overall_value_type = value_type;
    }
  }

  if (overall_value_type.has_value()) {
    types_[&expr] = ListType(arena_, *overall_value_type);
    return;
  }

  types_[&expr] = inference_context_->InstantiateTypeParams(FreeListType());
}

void ResolveVisitor::PostVisitStruct(const Expr& expr,
                                     const StructExpr& create_struct) {
  absl::Status status;
  std::string resolved_name;
  Type resolved_type;
  namespace_generator_.GenerateCandidates(
      create_struct.name(), [&](const absl::string_view name) {
        auto type = env_->LookupTypeName(*type_factory_, name);
        if (!type.ok()) {
          status.Update(type.status());
          return false;
        } else if (type->has_value()) {
          resolved_name = name;
          resolved_type = **type;
          return false;
        }
        return true;
      });

  if (!status.ok()) {
    status_.Update(status);
    return;
  }

  if (resolved_name.empty()) {
    ReportMissingReference(expr, create_struct.name());
    return;
  }

  if (resolved_type.kind() != TypeKind::kStruct &&
      !IsWellKnownMessageType(resolved_name)) {
    issues_->push_back(TypeCheckIssue::CreateError(
        ComputeSourceLocation(*ast_, expr.id()),
        absl::StrCat("type '", resolved_name,
                     "' does not support message creation")));
    return;
  }

  types_[&expr] = resolved_type;
  struct_types_[&expr] = resolved_name;

  status_.Update(
      CheckFieldAssignments(expr, create_struct, resolved_type, resolved_name));
}

void ResolveVisitor::PostVisitCall(const Expr& expr, const CallExpr& call) {
  // Handle disambiguation of namespaced functions.
  if (auto iter = maybe_namespaced_functions_.find(&expr);
      iter != maybe_namespaced_functions_.end()) {
    std::string namespaced_name =
        absl::StrCat(FormatCandidate(iter->second), ".", call.function());
    const FunctionDecl* decl =
        ResolveFunctionCallShape(expr, namespaced_name, call.args().size(),
                                 /* is_receiver= */ false);
    if (decl != nullptr) {
      ResolveFunctionOverloads(expr, *decl, call.args().size(),
                               /* is_receiver= */ false,
                               /* is_namespaced= */ true);
      return;
    }
    // Else, resolve the target as an attribute (deferred earlier), then
    // resolve the function call normally.
    ResolveQualifiedIdentifier(call.target(), iter->second);
  }

  int arg_count = call.args().size();
  if (call.has_target()) {
    ++arg_count;
  }

  const FunctionDecl* decl = ResolveFunctionCallShape(
      expr, call.function(), arg_count, call.has_target());

  if (decl != nullptr) {
    ResolveFunctionOverloads(expr, *decl, arg_count, call.has_target(),
                             /* is_namespaced= */ false);
    return;
  }

  ReportMissingReference(expr, call.function());
}

void ResolveVisitor::PreVisitComprehension(
    const Expr& expr, const ComprehensionExpr& comprehension) {
  std::unique_ptr<VariableScope> accu_scope = current_scope_->MakeNestedScope();
  auto* accu_scope_ptr = accu_scope.get();

  std::unique_ptr<VariableScope> iter_scope = accu_scope->MakeNestedScope();
  auto* iter_scope_ptr = iter_scope.get();

  // Keep the temporary decls alive as long as the visitor.
  comprehension_vars_.push_back(std::move(accu_scope));
  comprehension_vars_.push_back(std::move(iter_scope));

  comprehension_scopes_.push_back(
      {&expr, current_scope_, accu_scope_ptr, iter_scope_ptr});
}

void ResolveVisitor::PostVisitComprehension(
    const Expr& expr, const ComprehensionExpr& comprehension) {
  comprehension_scopes_.pop_back();
  types_[&expr] = GetTypeOrDyn(&comprehension.result());
}

void ResolveVisitor::PreVisitComprehensionSubexpression(
    const Expr& expr, const ComprehensionExpr& comprehension,
    ComprehensionArg comprehension_arg) {
  if (comprehension_scopes_.empty()) {
    status_.Update(absl::InternalError(
        "Comprehension scope stack is empty in comprehension"));
    return;
  }
  auto& scope = comprehension_scopes_.back();
  if (scope.comprehension_expr != &expr) {
    status_.Update(absl::InternalError("Comprehension scope stack broken"));
    return;
  }

  switch (comprehension_arg) {
    case ComprehensionArg::LOOP_CONDITION:
      current_scope_ = scope.accu_scope;
      break;
    case ComprehensionArg::LOOP_STEP:
      current_scope_ = scope.iter_scope;
      break;
    case ComprehensionArg::RESULT:
      current_scope_ = scope.accu_scope;
      break;
    default:
      current_scope_ = scope.parent;
      break;
  }
}

void ResolveVisitor::PostVisitComprehensionSubexpression(
    const Expr& expr, const ComprehensionExpr& comprehension,
    ComprehensionArg comprehension_arg) {
  if (comprehension_scopes_.empty()) {
    status_.Update(absl::InternalError(
        "Comprehension scope stack is empty in comprehension"));
    return;
  }
  auto& scope = comprehension_scopes_.back();
  if (scope.comprehension_expr != &expr) {
    status_.Update(absl::InternalError("Comprehension scope stack broken"));
    return;
  }
  current_scope_ = scope.parent;

  // Setting the type depends on the order the visitor is called -- the visitor
  // guarantees iter range and accu init are visited before subexpressions where
  // the corresponding variables can be referenced.
  switch (comprehension_arg) {
    case ComprehensionArg::ACCU_INIT:
      scope.accu_scope->InsertVariableIfAbsent(MakeVariableDecl(
          comprehension.accu_var(), GetTypeOrDyn(&comprehension.accu_init())));
      break;
    case ComprehensionArg::ITER_RANGE: {
      Type range_type = GetTypeOrDyn(&comprehension.iter_range());
      Type iter_type = DynType();
      switch (range_type.kind()) {
        case TypeKind::kList:
          iter_type = range_type.GetList().element();
          break;
        case TypeKind::kMap:
          iter_type = range_type.GetMap().key();
          break;
        case TypeKind::kDyn:
          break;
        default:
          issues_->push_back(TypeCheckIssue::CreateError(
              ComputeSourceLocation(*ast_, expr.id()),
              absl::StrCat("expression of type '", range_type.DebugString(),
                           "' cannot be the range of a comprehension (must be "
                           "list, map, or dynamic)")));
          break;
      }
      scope.iter_scope->InsertVariableIfAbsent(
          MakeVariableDecl(comprehension.iter_var(), iter_type));
      break;
    }
    case ComprehensionArg::RESULT:
      types_[&expr] = types_[&expr];
      break;
    default:
      break;
  }
}

void ResolveVisitor::PostVisitSelect(const Expr& expr,
                                     const SelectExpr& select) {
  if (!deferred_select_operations_.contains(&expr)) {
    ResolveSelectOperation(expr, select.field(), select.operand());
  }
}

const FunctionDecl* ResolveVisitor::ResolveFunctionCallShape(
    const Expr& expr, absl::string_view function_name, int arg_count,
    bool is_receiver) {
  const FunctionDecl* decl = nullptr;
  namespace_generator_.GenerateCandidates(
      function_name, [&, this](absl::string_view candidate) -> bool {
        decl = env_->LookupFunction(candidate);
        if (decl == nullptr) {
          return true;
        }
        for (const auto& ovl : decl->overloads()) {
          if (ovl.member() == is_receiver && ovl.args().size() == arg_count) {
            return false;
          }
        }
        // Name match, but no matching overloads.
        decl = nullptr;
        return true;
      });
  return decl;
}

void ResolveVisitor::ResolveFunctionOverloads(const Expr& expr,
                                              const FunctionDecl& decl,
                                              int arg_count, bool is_receiver,
                                              bool is_namespaced) {
  std::vector<Type> arg_types;
  arg_types.reserve(arg_count);
  if (is_receiver) {
    arg_types.push_back(GetTypeOrDyn(&expr.call_expr().target()));
  }
  for (int i = 0; i < expr.call_expr().args().size(); ++i) {
    arg_types.push_back(GetTypeOrDyn(&expr.call_expr().args()[i]));
  }

  absl::optional<TypeInferenceContext::OverloadResolution> resolution =
      inference_context_->ResolveOverload(decl, arg_types, is_receiver);

  if (!resolution.has_value()) {
    issues_->push_back(TypeCheckIssue::CreateError(
        ComputeSourceLocation(*ast_, expr.id()),
        absl::StrCat("found no matching overload for '", decl.name(),
                     "' applied to (",
                     absl::StrJoin(arg_types, ", ",
                                   [](std::string* out, const Type& type) {
                                     out->append(type.DebugString());
                                   }),
                     ")")));
    return;
  }

  auto* result_decl = google::protobuf::Arena::Create<FunctionDecl>(arena_);
  result_decl->set_name(decl.name());
  for (const auto& ovl : resolution->overloads) {
    absl::Status s = result_decl->AddOverload(ovl);
    if (!s.ok()) {
      // Overloads should be filtered list from the original declaration,
      // so a status means an invariant was broken.
      status_.Update(absl::InternalError(absl::StrCat(
          "failed to add overload to resolved function declaration: ", s)));
    }
  }

  functions_[&expr] = {result_decl, is_namespaced};
  types_[&expr] = resolution->result_type;
}

void ResolveVisitor::ResolveSimpleIdentifier(const Expr& expr,
                                             absl::string_view name) {
  const VariableDecl* decl = nullptr;
  namespace_generator_.GenerateCandidates(
      name, [&decl, this](absl::string_view candidate) {
        decl = current_scope_->LookupVariable(candidate);
        // continue searching.
        return decl == nullptr;
      });

  if (decl == nullptr) {
    ReportMissingReference(expr, name);
    return;
  }

  attributes_[&expr] = decl;
  types_[&expr] = inference_context_->InstantiateTypeParams(decl->type());
}

void ResolveVisitor::ResolveQualifiedIdentifier(
    const Expr& expr, absl::Span<const std::string> qualifiers) {
  if (qualifiers.size() == 1) {
    ResolveSimpleIdentifier(expr, qualifiers[0]);
    return;
  }

  absl::Nullable<const VariableDecl*> decl = nullptr;
  int segment_index_out = -1;
  namespace_generator_.GenerateCandidates(
      qualifiers, [&decl, &segment_index_out, this](absl::string_view candidate,
                                                    int segment_index) {
        decl = current_scope_->LookupVariable(candidate);
        if (decl != nullptr) {
          segment_index_out = segment_index;
          return false;
        }
        return true;
      });

  if (decl == nullptr) {
    ReportMissingReference(expr, FormatCandidate(qualifiers));
    return;
  }

  const int num_select_opts = qualifiers.size() - segment_index_out - 1;
  const Expr* root = &expr;
  std::vector<const Expr*> select_opts;
  select_opts.reserve(num_select_opts);
  for (int i = 0; i < num_select_opts; ++i) {
    select_opts.push_back(root);
    root = &root->select_expr().operand();
  }

  attributes_[root] = decl;
  types_[root] = inference_context_->InstantiateTypeParams(decl->type());

  // fix-up select operations that were deferred.
  for (auto iter = select_opts.rbegin(); iter != select_opts.rend(); ++iter) {
    ResolveSelectOperation(**iter, (*iter)->select_expr().field(),
                           (*iter)->select_expr().operand());
  }
}

void ResolveVisitor::ResolveSelectOperation(const Expr& expr,
                                            absl::string_view field,
                                            const Expr& operand) {
  auto impl = [&](const Type& operand_type) -> absl::optional<Type> {
    if (operand_type.kind() == TypeKind::kDyn ||
        operand_type.kind() == TypeKind::kAny) {
      return DynType();
    }

    if (operand_type.kind() == TypeKind::kStruct) {
      StructType struct_type = operand_type.GetStruct();
      auto field_info =
          env_->LookupStructField(*type_factory_, struct_type.name(), field);
      if (!field_info.ok()) {
        status_.Update(field_info.status());
        return absl::nullopt;
      }
      if (!field_info->has_value()) {
        ReportUndefinedField(expr.id(), field, struct_type.name());
        return absl::nullopt;
      }
      return field_info->value().GetType();
    }

    if (operand_type.kind() == TypeKind::kMap) {
      MapType map_type = operand_type.GetMap();
      if (inference_context_->IsAssignable(StringType(), map_type.GetKey())) {
        return map_type.GetValue();
      }
      // else fall though.
    }

    issues_->push_back(TypeCheckIssue::CreateError(
        ComputeSourceLocation(*ast_, expr.id()),
        absl::StrCat("expression of type '", operand_type.DebugString(),
                     "' cannot be the operand of a select operation")));
    return absl::nullopt;
  };

  const Type& operand_type = GetTypeOrDyn(&operand);

  absl::optional<Type> result_type;
  // Support short-hand optional chaining.
  if (operand_type.IsOptional()) {
    auto optional_type = operand_type.GetOptional();
    Type held_type = optional_type.GetParameter();
    result_type = impl(held_type);
  } else {
    result_type = impl(operand_type);
  }

  if (result_type.has_value()) {
    if (expr.select_expr().test_only()) {
      types_[&expr] = BoolType();
    } else {
      types_[&expr] = *result_type;
    }
  }
}

class ResolveRewriter : public AstRewriterBase {
 public:
  explicit ResolveRewriter(const ResolveVisitor& visitor,
                           const TypeInferenceContext& inference_context,
                           AstImpl::ReferenceMap& references,
                           AstImpl::TypeMap& types)
      : visitor_(visitor),
        inference_context_(inference_context),
        reference_map_(references),
        type_map_(types) {}
  bool PostVisitRewrite(Expr& expr) override {
    bool rewritten = false;
    if (auto iter = visitor_.attributes().find(&expr);
        iter != visitor_.attributes().end()) {
      const VariableDecl* decl = iter->second;
      auto& ast_ref = reference_map_[expr.id()];
      ast_ref.set_name(decl->name());
      expr.mutable_ident_expr().set_name(decl->name());
      rewritten = true;
    } else if (auto iter = visitor_.functions().find(&expr);
               iter != visitor_.functions().end()) {
      const FunctionDecl* decl = iter->second.decl;
      const bool needs_rewrite = iter->second.namespace_rewrite;
      auto& ast_ref = reference_map_[expr.id()];
      ast_ref.set_name(decl->name());
      for (const auto& overload : decl->overloads()) {
        // TODO: narrow based on type inferences and shape.
        ast_ref.mutable_overload_id().push_back(overload.id());
      }
      expr.mutable_call_expr().set_function(decl->name());
      if (needs_rewrite && expr.call_expr().has_target()) {
        expr.mutable_call_expr().set_target(nullptr);
      }
      rewritten = true;
    } else if (auto iter = visitor_.struct_types().find(&expr);
               iter != visitor_.struct_types().end()) {
      auto& ast_ref = reference_map_[expr.id()];
      ast_ref.set_name(iter->second);
      if (expr.has_struct_expr()) {
        expr.mutable_struct_expr().set_name(iter->second);
      }
      rewritten = true;
    }

    if (auto iter = visitor_.types().find(&expr);
        iter != visitor_.types().end()) {
      auto flattened_type =
          FlattenType(inference_context_.FinalizeType(iter->second));

      if (!flattened_type.ok()) {
        status_.Update(flattened_type.status());
        return rewritten;
      }
      type_map_[expr.id()] = *std::move(flattened_type);
      rewritten = true;
    }

    return rewritten;
  }

  const absl::Status& status() const { return status_; }

 private:
  absl::Status status_;
  const ResolveVisitor& visitor_;
  const TypeInferenceContext& inference_context_;
  AstImpl::ReferenceMap& reference_map_;
  AstImpl::TypeMap& type_map_;
};

}  // namespace

absl::StatusOr<ValidationResult> TypeCheckerImpl::Check(
    std::unique_ptr<Ast> ast) const {
  auto& ast_impl = AstImpl::CastFromPublicAst(*ast);
  google::protobuf::Arena type_arena;

  std::vector<TypeCheckIssue> issues;
  CEL_ASSIGN_OR_RETURN(auto generator,
                       NamespaceGenerator::Create(env_.container()));

  TypeInferenceContext type_inference_context(&type_arena);
  TrivialTypeFactory type_factory(&type_arena);
  ResolveVisitor visitor(env_.container(), std::move(generator), env_, ast_impl,
                         type_inference_context, issues, &type_arena,
                         type_factory);

  TraversalOptions opts;
  opts.use_comprehension_callbacks = true;
  AstTraverse(ast_impl.root_expr(), visitor, opts);
  CEL_RETURN_IF_ERROR(visitor.status());

  // If any issues are errors, return without an AST.
  for (const auto& issue : issues) {
    if (issue.severity() == Severity::kError) {
      return ValidationResult(std::move(issues));
    }
  }

  // Apply updates as needed.
  // Happens in a second pass to simplify validating that pointers haven't
  // been invalidated by other updates.
  ResolveRewriter rewriter(visitor, type_inference_context,
                           ast_impl.reference_map(), ast_impl.type_map());
  AstRewrite(ast_impl.root_expr(), rewriter);

  CEL_RETURN_IF_ERROR(rewriter.status());

  ast_impl.set_is_checked(true);

  return ValidationResult(std::move(ast), std::move(issues));
}

}  // namespace cel::checker_internal