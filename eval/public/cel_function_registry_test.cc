#include "eval/public/cel_function_registry.h"

#include <memory>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "base/kind.h"
#include "eval/public/activation.h"
#include "eval/public/cel_function.h"
#include "eval/public/cel_function_provider.h"
#include "internal/testing.h"

namespace google::api::expr::runtime {

namespace {

using testing::ElementsAre;
using testing::Eq;
using testing::HasSubstr;
using testing::Property;
using testing::SizeIs;
using testing::Truly;
using cel::internal::StatusIs;

class ConstCelFunction : public CelFunction {
 public:
  ConstCelFunction() : CelFunction(MakeDescriptor()) {}
  explicit ConstCelFunction(const CelFunctionDescriptor& desc)
      : CelFunction(desc) {}

  static CelFunctionDescriptor MakeDescriptor() {
    return {"ConstFunction", false, {}};
  }

  absl::Status Evaluate(absl::Span<const CelValue> args, CelValue* output,
                        google::protobuf::Arena* arena) const override {
    *output = CelValue::CreateInt64(42);

    return absl::OkStatus();
  }
};

TEST(CelFunctionRegistryTest, InsertAndRetrieveLazyFunction) {
  CelFunctionDescriptor lazy_function_desc{"LazyFunction", false, {}};
  CelFunctionRegistry registry;
  Activation activation;
  ASSERT_OK(registry.RegisterLazyFunction(lazy_function_desc));

  const auto descriptors =
      registry.FindLazyOverloads("LazyFunction", false, {});
  EXPECT_THAT(descriptors, testing::SizeIs(1));
}

// Confirm that lazy and static functions share the same descriptor space:
// i.e. you can't insert both a lazy function and a static function for the same
// descriptors.
TEST(CelFunctionRegistryTest, LazyAndStaticFunctionShareDescriptorSpace) {
  CelFunctionRegistry registry;
  CelFunctionDescriptor desc = ConstCelFunction::MakeDescriptor();
  ASSERT_OK(registry.RegisterLazyFunction(desc));

  absl::Status status = registry.Register(ConstCelFunction::MakeDescriptor(),
                                          std::make_unique<ConstCelFunction>());
  EXPECT_FALSE(status.ok());
}

// Confirm that lazy and static functions share the same descriptor space:
// i.e. you can't insert both a lazy function and a static function for the same
// descriptors.
TEST(CelFunctionRegistryTest, FindStaticOverloadsReturns) {
  CelFunctionRegistry registry;
  CelFunctionDescriptor desc = ConstCelFunction::MakeDescriptor();
  ASSERT_OK(registry.Register(desc, std::make_unique<ConstCelFunction>(desc)));

  std::vector<CelFunctionRegistry::StaticOverload> overloads =
      registry.FindStaticOverloads(desc.name(), false, {});

  EXPECT_THAT(
      overloads,
      ElementsAre(
          Truly([](const CelFunctionRegistry::StaticOverload overload) -> bool {
            return overload.descriptor->name() == "ConstFunction" &&
                   overload.implementation != nullptr;
          })))
      << "Expected single ConstFunction()";
}

TEST(CelFunctionRegistryTest, ListFunctions) {
  CelFunctionDescriptor lazy_function_desc{"LazyFunction", false, {}};
  CelFunctionRegistry registry;

  ASSERT_OK(registry.RegisterLazyFunction(lazy_function_desc));
  EXPECT_OK(registry.Register(ConstCelFunction::MakeDescriptor(),
                              std::make_unique<ConstCelFunction>()));

  auto registered_functions = registry.ListFunctions();

  EXPECT_THAT(registered_functions, SizeIs(2));
  EXPECT_THAT(registered_functions["LazyFunction"], SizeIs(1));
  EXPECT_THAT(registered_functions["ConstFunction"], SizeIs(1));
}

TEST(CelFunctionRegistryTest, DefaultLazyProvider) {
  CelFunctionDescriptor lazy_function_desc{"LazyFunction", false, {}};
  CelFunctionRegistry registry;
  Activation activation;
  EXPECT_OK(registry.RegisterLazyFunction(lazy_function_desc));
  EXPECT_OK(activation.InsertFunction(
      std::make_unique<ConstCelFunction>(lazy_function_desc)));

  const auto providers =
      registry.ModernFindLazyOverloads("LazyFunction", false, {});
  EXPECT_THAT(providers, testing::SizeIs(1));
  ASSERT_OK_AND_ASSIGN(auto func, providers[0].provider->GetFunction(
                                      lazy_function_desc, activation));
  EXPECT_THAT(func, Property(&CelFunction::descriptor,
                             Property(&CelFunctionDescriptor::name,
                                      Eq("LazyFunction"))));
}

TEST(CelFunctionRegistryTest, DefaultLazyProviderNoOverloadFound) {
  CelFunctionRegistry registry;
  Activation activation;
  CelFunctionDescriptor lazy_function_desc{"LazyFunction", false, {}};
  EXPECT_OK(registry.RegisterLazyFunction(lazy_function_desc));
  EXPECT_OK(activation.InsertFunction(
      std::make_unique<ConstCelFunction>(lazy_function_desc)));

  const auto providers =
      registry.ModernFindLazyOverloads("LazyFunction", false, {});
  ASSERT_THAT(providers, testing::SizeIs(1));
  auto provider = providers[0].provider;
  auto func = provider->GetFunction({"LazyFunc", false, {cel::Kind::kInt64}},
                                    activation);

  ASSERT_OK(func);
  EXPECT_THAT(*func, Eq(nullptr));
}

TEST(CelFunctionRegistryTest, DefaultLazyProviderAmbiguousLookup) {
  CelFunctionRegistry registry;
  Activation activation;
  CelFunctionDescriptor desc1{"LazyFunc", false, {CelValue::Type::kInt64}};
  CelFunctionDescriptor desc2{"LazyFunc", false, {CelValue::Type::kUint64}};
  CelFunctionDescriptor match_desc{"LazyFunc", false, {CelValue::Type::kAny}};
  ASSERT_OK(registry.RegisterLazyFunction(match_desc));
  ASSERT_OK(
      activation.InsertFunction(std::make_unique<ConstCelFunction>(desc1)));
  ASSERT_OK(
      activation.InsertFunction(std::make_unique<ConstCelFunction>(desc2)));

  auto providers =
      registry.ModernFindLazyOverloads("LazyFunc", false, {cel::Kind::kAny});
  ASSERT_THAT(providers, testing::SizeIs(1));
  auto provider = providers[0].provider;
  auto func = provider->GetFunction(match_desc, activation);

  EXPECT_THAT(std::string(func.status().message()),
              HasSubstr("Couldn't resolve function"));
}

TEST(CelFunctionRegistryTest, CanRegisterNonStrictFunction) {
  {
    CelFunctionRegistry registry;
    CelFunctionDescriptor descriptor("NonStrictFunction",
                                     /*receiver_style=*/false,
                                     {CelValue::Type::kAny},
                                     /*is_strict=*/false);
    ASSERT_OK(registry.Register(
        descriptor, std::make_unique<ConstCelFunction>(descriptor)));
    EXPECT_THAT(registry.FindStaticOverloads("NonStrictFunction", false,
                                             {CelValue::Type::kAny}),
                SizeIs(1));
  }
  {
    CelFunctionRegistry registry;
    CelFunctionDescriptor descriptor("NonStrictLazyFunction",
                                     /*receiver_style=*/false,
                                     {CelValue::Type::kAny},
                                     /*is_strict=*/false);
    EXPECT_OK(registry.RegisterLazyFunction(descriptor));
    EXPECT_THAT(registry.FindLazyOverloads("NonStrictLazyFunction", false,
                                           {CelValue::Type::kAny}),
                SizeIs(1));
  }
}

using NonStrictTestCase = std::tuple<bool, bool>;
using NonStrictRegistrationFailTest = testing::TestWithParam<NonStrictTestCase>;

TEST_P(NonStrictRegistrationFailTest,
       IfOtherOverloadExistsRegisteringNonStrictFails) {
  bool existing_function_is_lazy, new_function_is_lazy;
  std::tie(existing_function_is_lazy, new_function_is_lazy) = GetParam();
  CelFunctionRegistry registry;
  CelFunctionDescriptor descriptor("OverloadedFunction",
                                   /*receiver_style=*/false,
                                   {CelValue::Type::kAny},
                                   /*is_strict=*/true);
  if (existing_function_is_lazy) {
    ASSERT_OK(registry.RegisterLazyFunction(descriptor));
  } else {
    ASSERT_OK(registry.Register(
        descriptor, std::make_unique<ConstCelFunction>(descriptor)));
  }
  CelFunctionDescriptor new_descriptor(
      "OverloadedFunction",
      /*receiver_style=*/false, {CelValue::Type::kAny, CelValue::Type::kAny},
      /*is_strict=*/false);
  absl::Status status;
  if (new_function_is_lazy) {
    status = registry.RegisterLazyFunction(new_descriptor);
  } else {
    status = registry.Register(
        new_descriptor, std::make_unique<ConstCelFunction>(new_descriptor));
  }
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kAlreadyExists,
                               HasSubstr("Only one overload")));
}

TEST_P(NonStrictRegistrationFailTest,
       IfOtherNonStrictExistsRegisteringStrictFails) {
  bool existing_function_is_lazy, new_function_is_lazy;
  std::tie(existing_function_is_lazy, new_function_is_lazy) = GetParam();
  CelFunctionRegistry registry;
  CelFunctionDescriptor descriptor("OverloadedFunction",
                                   /*receiver_style=*/false,
                                   {CelValue::Type::kAny},
                                   /*is_strict=*/false);
  if (existing_function_is_lazy) {
    ASSERT_OK(registry.RegisterLazyFunction(descriptor));
  } else {
    ASSERT_OK(registry.Register(
        descriptor, std::make_unique<ConstCelFunction>(descriptor)));
  }
  CelFunctionDescriptor new_descriptor(
      "OverloadedFunction",
      /*receiver_style=*/false, {CelValue::Type::kAny, CelValue::Type::kAny},
      /*is_strict=*/true);
  absl::Status status;
  if (new_function_is_lazy) {
    status = registry.RegisterLazyFunction(new_descriptor);
  } else {
    status = registry.Register(
        new_descriptor, std::make_unique<ConstCelFunction>(new_descriptor));
  }
  EXPECT_THAT(status, StatusIs(absl::StatusCode::kAlreadyExists,
                               HasSubstr("Only one overload")));
}

TEST_P(NonStrictRegistrationFailTest, CanRegisterStrictFunctionsWithoutLimit) {
  bool existing_function_is_lazy, new_function_is_lazy;
  std::tie(existing_function_is_lazy, new_function_is_lazy) = GetParam();
  CelFunctionRegistry registry;
  CelFunctionDescriptor descriptor("OverloadedFunction",
                                   /*receiver_style=*/false,
                                   {CelValue::Type::kAny},
                                   /*is_strict=*/true);
  if (existing_function_is_lazy) {
    ASSERT_OK(registry.RegisterLazyFunction(descriptor));
  } else {
    ASSERT_OK(registry.Register(
        descriptor, std::make_unique<ConstCelFunction>(descriptor)));
  }
  CelFunctionDescriptor new_descriptor(
      "OverloadedFunction",
      /*receiver_style=*/false, {CelValue::Type::kAny, CelValue::Type::kAny},
      /*is_strict=*/true);
  absl::Status status;
  if (new_function_is_lazy) {
    status = registry.RegisterLazyFunction(new_descriptor);
  } else {
    status = registry.Register(
        new_descriptor, std::make_unique<ConstCelFunction>(new_descriptor));
  }
  EXPECT_OK(status);
}

INSTANTIATE_TEST_SUITE_P(NonStrictRegistrationFailTest,
                         NonStrictRegistrationFailTest,
                         testing::Combine(testing::Bool(), testing::Bool()));

}  // namespace

}  // namespace google::api::expr::runtime
