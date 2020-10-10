// ---------------------------------------------------------------------------------------------------
// MODERNDBS
// ---------------------------------------------------------------------------------------------------
#include <random>
#include "moderndbs/codegen/expression.h"
#include "benchmark/benchmark.h"
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/DynamicLibrary.h>
// ---------------------------------------------------------------------------------------------------
using AddExpression = moderndbs::AddExpression;
using Argument = moderndbs::Argument;
using BinaryExpression = moderndbs::BinaryExpression;
using Constant = moderndbs::Constant;
using DivExpression = moderndbs::DivExpression;
using Expression = moderndbs::Expression;
using ExpressionCompiler = moderndbs::ExpressionCompiler;
using JIT = moderndbs::JIT;
using MulExpression = moderndbs::MulExpression;
using SubExpression = moderndbs::SubExpression;
using data64_t = moderndbs::data64_t;
// ---------------------------------------------------------------------------------------------------
namespace {
// ---------------------------------------------------------------------------------------------------
struct TestExpression {
    /// The number of arguments
    unsigned arg_count;
    /// The number of operations
    unsigned op_count;
    /// The (sub-) expressions.
    std::vector<std::unique_ptr<Expression>> expressions;
    /// The root.
    Expression* root;

    /// The constructor.
    TestExpression(int arg_count, int op_count);
};
// ---------------------------------------------------------------------------------------------------
TestExpression::TestExpression(int arg_count, int op_count)
    : arg_count(arg_count), op_count(op_count), expressions() {

    // Allocate n integer arguments
    for (int i = 0; i < arg_count; ++i) {
        expressions.push_back(std::make_unique<Argument>(i, Expression::ValueType::INT64));
    }

    std::mt19937_64 rng(0);
    std::uniform_int_distribution<std::mt19937_64::result_type> op_dis(0, 2);
    std::uniform_int_distribution<std::mt19937_64::result_type> arg_dis(0, arg_count - 1);

    // Now build a left-deep expression tree with all operations.
    for (int i = 0; i < op_count; ++i) {
        auto& last = *expressions.back();
        auto& arg = *expressions[arg_dis(rng)];
        switch (op_dis(rng)) {
            case 0:
                expressions.push_back(std::make_unique<AddExpression>(last, arg));
                break;
            case 1:
                expressions.push_back(std::make_unique<SubExpression>(last, arg));
                break;
            case 2:
                expressions.push_back(std::make_unique<MulExpression>(last, arg));
                break;
            default:
                assert(false);
        }
    }

    // Store the root.
    root = expressions.back().get();
}
// ---------------------------------------------------------------------------------------------------
void I64_AddSubMul_LeftDeep_Compiled(benchmark::State &state) {
    TestExpression t(state.range(0), state.range(1));

    llvm::orc::ThreadSafeContext context{std::make_unique<llvm::LLVMContext>()};
    ExpressionCompiler compiler(context);
    compiler.compile(*t.root);

    std::mt19937_64 rng(0);
    std::uniform_int_distribution<std::mt19937_64::result_type> dis;
    std::vector<int64_t> d(t.arg_count);

    for (unsigned i = 0; i < t.arg_count; ++i) {
        d[i] = dis(rng);
    }

    for (auto _ : state) {
        benchmark::DoNotOptimize(compiler.run(reinterpret_cast<data64_t*>(d.data())));
    }


    state.SetItemsProcessed(state.iterations());
}
// ---------------------------------------------------------------------------------------------------
void I64_AddSubMul_LeftDeep_Interpreted(benchmark::State &state) {
    TestExpression t(state.range(0), state.range(1));

    std::mt19937_64 rng(0);
    std::uniform_int_distribution<std::mt19937_64::result_type> dis;
    std::vector<int64_t> d(t.arg_count);

    for (unsigned i = 0; i < t.arg_count; ++i) {
        d[i] = dis(rng);
    }

    for (auto _ : state) {
        benchmark::DoNotOptimize(t.root->evaluate(reinterpret_cast<data64_t*>(d.data())));
    }

    state.SetItemsProcessed(state.iterations());
}
// ---------------------------------------------------------------------------------------------------
}  // namespace
// ---------------------------------------------------------------------------------------------------
BENCHMARK(I64_AddSubMul_LeftDeep_Compiled)
    ->Args({10, 1000})
    ->Args({100, 1000})
    ->Args({1000, 1000});
BENCHMARK(I64_AddSubMul_LeftDeep_Interpreted)
    ->Args({10, 1000})
    ->Args({100, 1000})
    ->Args({1000, 1000});
// ---------------------------------------------------------------------------------------------------
int main(int argc, char** argv) {
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::sys::DynamicLibrary::LoadLibraryPermanently(nullptr);
    benchmark::Initialize(&argc, argv);
    benchmark::RunSpecifiedBenchmarks();
}
// ---------------------------------------------------------------------------------------------------
