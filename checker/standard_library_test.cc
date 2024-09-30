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

#include "checker/standard_library.h"

#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "base/ast_internal/ast_impl.h"
#include "base/ast_internal/expr.h"
#include "checker/internal/test_ast_helpers.h"
#include "checker/type_checker.h"
#include "checker/type_checker_builder.h"
#include "checker/validation_result.h"
#include "common/ast.h"
#include "common/constant.h"
#include "internal/testing.h"

namespace cel {
namespace {

using ::absl_testing::IsOk;
using ::absl_testing::StatusIs;
using ::cel::ast_internal::AstImpl;
using ::cel::ast_internal::Reference;
using ::testing::IsEmpty;
using ::testing::Pointee;
using ::testing::Property;

TEST(StandardLibraryTest, StandardLibraryAddsDecls) {
  TypeCheckerBuilder builder;
  EXPECT_THAT(builder.AddLibrary(StandardLibrary()), IsOk());
  EXPECT_THAT(std::move(builder).Build(), IsOk());
}

TEST(StandardLibraryTest, StandardLibraryErrorsIfAddedTwice) {
  TypeCheckerBuilder builder;
  EXPECT_THAT(builder.AddLibrary(StandardLibrary()), IsOk());
  EXPECT_THAT(builder.AddLibrary(StandardLibrary()),
              StatusIs(absl::StatusCode::kAlreadyExists));
}

class StandardLibraryDefinitionsTest : public ::testing::Test {
 public:
  void SetUp() override {
    TypeCheckerBuilder builder;
    ASSERT_THAT(builder.AddLibrary(StandardLibrary()), IsOk());
    ASSERT_OK_AND_ASSIGN(stdlib_type_checker_, std::move(builder).Build());
  }

 protected:
  std::unique_ptr<TypeChecker> stdlib_type_checker_;
};

class StdlibTypeVarDefinitionTest
    : public StandardLibraryDefinitionsTest,
      public testing::WithParamInterface<std::string> {};

TEST_P(StdlibTypeVarDefinitionTest, DefinesTypeConstants) {
  auto ast = std::make_unique<AstImpl>();
  ast->root_expr().mutable_ident_expr().set_name(GetParam());
  ast->root_expr().set_id(1);

  ASSERT_OK_AND_ASSIGN(ValidationResult result,
                       stdlib_type_checker_->Check(std::move(ast)));

  EXPECT_THAT(result.GetIssues(), IsEmpty());
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Ast> checked_ast, result.ReleaseAst());
  const auto& checked_impl = AstImpl::CastFromPublicAst(*checked_ast);
  EXPECT_THAT(checked_impl.GetReference(1),
              Pointee(Property(&Reference::name, GetParam())));
}

INSTANTIATE_TEST_SUITE_P(
    StdlibTypeVarDefinitions, StdlibTypeVarDefinitionTest,
    ::testing::Values("bool", "int", "uint", "double", "string", "bytes",
                      "list", "map", "duration", "timestamp", "null_type"),
    [](const auto& info) -> std::string { return info.param; });

TEST_F(StandardLibraryDefinitionsTest, DefinesProtoStructNull) {
  auto ast = std::make_unique<AstImpl>();

  auto& enumerator = ast->root_expr();
  enumerator.set_id(4);
  enumerator.mutable_select_expr().set_field("NULL_VALUE");
  auto& enumeration = enumerator.mutable_select_expr().mutable_operand();
  enumeration.set_id(3);
  enumeration.mutable_select_expr().set_field("NullValue");
  auto& protobuf = enumeration.mutable_select_expr().mutable_operand();
  protobuf.set_id(2);
  protobuf.mutable_select_expr().set_field("protobuf");
  auto& google = protobuf.mutable_select_expr().mutable_operand();
  google.set_id(1);
  google.mutable_ident_expr().set_name("google");

  ASSERT_OK_AND_ASSIGN(ValidationResult result,
                       stdlib_type_checker_->Check(std::move(ast)));

  EXPECT_THAT(result.GetIssues(), IsEmpty());
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Ast> checked_ast, result.ReleaseAst());
  const auto& checked_impl = AstImpl::CastFromPublicAst(*checked_ast);
  EXPECT_THAT(checked_impl.GetReference(4),
              Pointee(Property(&Reference::name,
                               "google.protobuf.NullValue.NULL_VALUE")));
  // TODO: Add support for compiler constants (null value here).
}

struct DefinitionsTestCase {
  std::string expr;
  bool type_check_success = true;
  CheckerOptions options;
};

class StdLibDefinitionsTest
    : public ::testing::TestWithParam<DefinitionsTestCase> {
 public:
  void SetUp() override {
    TypeCheckerBuilder builder;
    ASSERT_THAT(builder.AddLibrary(StandardLibrary()), IsOk());
    ASSERT_OK_AND_ASSIGN(stdlib_type_checker_, std::move(builder).Build());
  }

 protected:
  std::unique_ptr<TypeChecker> stdlib_type_checker_;
};

// Basic coverage that the standard library definitions are defined.
// This is not intended to be exhaustive since it is expected to be covered by
// spec conformance tests.
//
// TODO: Tests are fairly minimal right now -- it's not possible to
// test thoroughly without a more complete implementation of the type checker.
// Type-parameterized functions are not yet checkable.
TEST_P(StdLibDefinitionsTest, Runner) {
  TypeCheckerBuilder builder(GetParam().options);
  ASSERT_THAT(builder.AddLibrary(StandardLibrary()), IsOk());
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<TypeChecker> type_checker,
                       std::move(builder).Build());

  ASSERT_OK_AND_ASSIGN(std::unique_ptr<Ast> ast,
                       checker_internal::MakeTestParsedAst(GetParam().expr));

  ASSERT_OK_AND_ASSIGN(auto result, type_checker->Check(std::move(ast)));
  EXPECT_EQ(result.IsValid(), GetParam().type_check_success);
}

INSTANTIATE_TEST_SUITE_P(
    Strings, StdLibDefinitionsTest,
    ::testing::Values(DefinitionsTestCase{
                          /* .expr = */ "'123'.size()",
                      },
                      DefinitionsTestCase{
                          /* .expr = */ "size('123')",
                      },
                      DefinitionsTestCase{
                          /* .expr = */ "'123' + '123'",
                      },
                      DefinitionsTestCase{
                          /* .expr = */ "'123' + '123'",
                      },
                      DefinitionsTestCase{
                          /* .expr = */ "'123' + '123'",
                      },
                      DefinitionsTestCase{
                          /* .expr = */ "'123'.endsWith('123')",
                      },
                      DefinitionsTestCase{
                          /* .expr = */ "'123'.startsWith('123')",
                      },
                      DefinitionsTestCase{
                          /* .expr = */ "'123'.contains('123')",
                      },
                      DefinitionsTestCase{
                          /* .expr = */ "'123'.matches(r'123')",
                      },
                      DefinitionsTestCase{
                          /* .expr = */ "matches('123', r'123')",
                      }));

INSTANTIATE_TEST_SUITE_P(TypeCasts, StdLibDefinitionsTest,
                         ::testing::Values(DefinitionsTestCase{
                                               /* .expr = */ "int(1)",
                                           },
                                           DefinitionsTestCase{
                                               /* .expr = */ "uint(1)",
                                           },
                                           DefinitionsTestCase{
                                               /* .expr = */ "double(1)",
                                           },
                                           DefinitionsTestCase{
                                               /* .expr = */ "string(1)",
                                           },
                                           DefinitionsTestCase{
                                               /* .expr = */ "bool('true')",
                                           },
                                           DefinitionsTestCase{
                                               /* .expr = */ "timestamp(0)",
                                           },
                                           DefinitionsTestCase{
                                               /* .expr = */ "duration('1s')",
                                           }));

INSTANTIATE_TEST_SUITE_P(Arithmetic, StdLibDefinitionsTest,
                         ::testing::Values(DefinitionsTestCase{
                                               /* .expr = */ "1 + 2",
                                           },
                                           DefinitionsTestCase{
                                               /* .expr = */ "1 - 2",
                                           },
                                           DefinitionsTestCase{
                                               /* .expr = */ "1 / 2",
                                           },
                                           DefinitionsTestCase{
                                               /* .expr = */ "1 * 2",
                                           },
                                           DefinitionsTestCase{
                                               /* .expr = */ "2 % 1",
                                           },
                                           DefinitionsTestCase{
                                               /* .expr = */ "-1",
                                           }));

INSTANTIATE_TEST_SUITE_P(
    TimeArithmetic, StdLibDefinitionsTest,
    ::testing::Values(DefinitionsTestCase{
                          /* .expr = */ "timestamp(0) + duration('1s')",
                      },
                      DefinitionsTestCase{
                          /* .expr = */ "timestamp(0) - duration('1s')",
                      },
                      DefinitionsTestCase{
                          /* .expr = */ "timestamp(0) - timestamp(0)",
                      },
                      DefinitionsTestCase{
                          /* .expr = */ "duration('1s') + duration('1s')",
                      },
                      DefinitionsTestCase{
                          /* .expr = */ "duration('1s') - duration('1s')",
                      }));

INSTANTIATE_TEST_SUITE_P(NumericComparisons, StdLibDefinitionsTest,
                         ::testing::Values(DefinitionsTestCase{
                                               /* .expr = */ "1 > 2",
                                           },
                                           DefinitionsTestCase{
                                               /* .expr = */ "1 < 2",
                                           },
                                           DefinitionsTestCase{
                                               /* .expr = */ "1 >= 2",
                                           },
                                           DefinitionsTestCase{
                                               /* .expr = */ "1 <= 2",
                                           }));

INSTANTIATE_TEST_SUITE_P(
    CrossNumericComparisons, StdLibDefinitionsTest,
    ::testing::Values(
        DefinitionsTestCase{
            /* .expr = */ "1u < 2",
            /* .type_check_success = */ true,
            /* .options = */ {.enable_cross_numeric_comparisons = true}},
        DefinitionsTestCase{
            /* .expr = */ "1u > 2",
            /* .type_check_success = */ true,
            /* .options = */ {.enable_cross_numeric_comparisons = true}},
        DefinitionsTestCase{
            /* .expr = */ "1u <= 2",
            /* .type_check_success = */ true,
            /* .options = */ {.enable_cross_numeric_comparisons = true}},
        DefinitionsTestCase{
            /* .expr = */ "1u >= 2",
            /* .type_check_success = */ true,
            /* .options = */ {.enable_cross_numeric_comparisons = true}}));

INSTANTIATE_TEST_SUITE_P(
    TimeComparisons, StdLibDefinitionsTest,
    ::testing::Values(DefinitionsTestCase{
                          /* .expr = */ "duration('1s') < duration('1s')",
                      },
                      DefinitionsTestCase{
                          /* .expr = */ "duration('1s') > duration('1s')",
                      },
                      DefinitionsTestCase{
                          /* .expr = */ "duration('1s') <= duration('1s')",
                      },
                      DefinitionsTestCase{
                          /* .expr = */ "duration('1s') >= duration('1s')",
                      },
                      DefinitionsTestCase{
                          /* .expr = */ "timestamp(0) < timestamp(0)",
                      },
                      DefinitionsTestCase{
                          /* .expr = */ "timestamp(0) > timestamp(0)",
                      },
                      DefinitionsTestCase{
                          /* .expr = */ "timestamp(0) <= timestamp(0)",
                      },
                      DefinitionsTestCase{
                          /* .expr = */ "timestamp(0) >= timestamp(0)",
                      }));

INSTANTIATE_TEST_SUITE_P(
    TimeAccessors, StdLibDefinitionsTest,
    ::testing::Values(
        DefinitionsTestCase{
            /* .expr = */ "timestamp(0).getFullYear()",
        },
        DefinitionsTestCase{
            /* .expr = */ "timestamp(0).getFullYear('-08:00')",
        },
        DefinitionsTestCase{
            /* .expr = */ "timestamp(0).getMonth()",
        },
        DefinitionsTestCase{
            /* .expr = */ "timestamp(0).getMonth('-08:00')",
        },
        DefinitionsTestCase{
            /* .expr = */ "timestamp(0).getDayOfYear()",
        },
        DefinitionsTestCase{
            /* .expr = */ "timestamp(0).getDayOfYear('-08:00')",
        },
        DefinitionsTestCase{
            /* .expr = */ "timestamp(0).getDate()",
        },
        DefinitionsTestCase{
            /* .expr = */ "timestamp(0).getDate('-08:00')",
        },
        DefinitionsTestCase{
            /* .expr = */ "timestamp(0).getDayOfWeek()",
        },
        DefinitionsTestCase{
            /* .expr = */ "timestamp(0).getDayOfWeek('-08:00')",
        },
        DefinitionsTestCase{
            /* .expr = */ "timestamp(0).getHours()",
        },
        DefinitionsTestCase{
            /* .expr = */ "duration('1s').getHours()",
        },
        DefinitionsTestCase{
            /* .expr = */ "timestamp(0).getHours('-08:00')",
        },
        DefinitionsTestCase{
            /* .expr = */ "timestamp(0).getMinutes()",
        },
        DefinitionsTestCase{
            /* .expr = */ "duration('1s').getMinutes()",
        },
        DefinitionsTestCase{
            /* .expr = */ "timestamp(0).getMinutes('-08:00')",
        },
        DefinitionsTestCase{
            /* .expr = */ "timestamp(0).getSeconds()",
        },
        DefinitionsTestCase{
            /* .expr = */ "duration('1s').getSeconds()",
        },
        DefinitionsTestCase{
            /* .expr = */ "timestamp(0).getSeconds('-08:00')",
        },
        DefinitionsTestCase{
            /* .expr = */ "timestamp(0).getMilliseconds()",
        },
        DefinitionsTestCase{
            /* .expr = */ "duration('1s').getMilliseconds()",
        },
        DefinitionsTestCase{
            /* .expr = */ "timestamp(0).getMilliseconds('-08:00')",
        }));

INSTANTIATE_TEST_SUITE_P(Logic, StdLibDefinitionsTest,
                         ::testing::Values(DefinitionsTestCase{
                                               /* .expr = */ "true || false",
                                           },
                                           DefinitionsTestCase{
                                               /* .expr = */ "true && false",
                                           },
                                           DefinitionsTestCase{
                                               /* .expr = */ "!true",
                                           }));

}  // namespace
}  // namespace cel