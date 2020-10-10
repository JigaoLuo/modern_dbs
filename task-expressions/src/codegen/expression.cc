#include "moderndbs/codegen/expression.h"
#include "moderndbs/error.h"
#include <iostream>

using JIT = moderndbs::JIT;
using Expression = moderndbs::Expression;
using ExpressionCompiler = moderndbs::ExpressionCompiler;
using data64_t = moderndbs::data64_t;

/// Evaluate the expresion.
data64_t Expression::evaluate(const data64_t* /*args*/) { throw NotImplementedException(); }

/// Build the expression
llvm::Value* Expression::build(llvm::IRBuilder<>& /*builder*/, llvm::Value* /*args*/) { throw NotImplementedException(); }

/// Constructor.
ExpressionCompiler::ExpressionCompiler(llvm::orc::ThreadSafeContext& context) : context(context), module(std::make_unique<llvm::Module>("module_name", *context.getContext())), jit(context), fnPtr(nullptr) {}

/// Compile an expression.
void ExpressionCompiler::compile(Expression& expression, bool verbose) {
    /// Create a LLVM IR builder.
    auto& ctx = *context.getContext();

    /// Solution Idea.
    /// Build the type of the expression function.
    auto FnType = llvm::FunctionType::get(llvm::Type::getInt64Ty(ctx), {llvm::Type::getInt64PtrTy(ctx)->getPointerTo()}, false /* not accept variable length data type */);

    /// Insert the function "fn" into the module.
    auto Fn = llvm::cast<llvm::Function>(module->getOrInsertFunction("fnPtr", FnType).getCallee());

    /// Create a basic block for the function.
    llvm::BasicBlock *FnEntryBlock = llvm::BasicBlock::Create(ctx, "entry", Fn);

    /// Generate the code for the expression tree.
    llvm::IRBuilder<> builder(FnEntryBlock);
    llvm::Value* val = expression.build(builder, &*Fn->arg_begin());
    auto cv = builder.CreateBitCast(val, builder.getInt64Ty());
    builder.CreateRet(cv);

    /// Dump the module if verbose model.
    if (verbose) {
        module->print(llvm::errs(), nullptr);
    }

    /// Add the module to the JIT.
    auto err = jit.addModule(std::move(module));
    if (err) {
        throw std::runtime_error("module compilation failed");
    }

    /// Store the function pointer.
    fnPtr = reinterpret_cast<data64_t (*)(data64_t *)>(jit.getPointerToFunction("fnPtr"));


//    /// Create a LLVM IR builder.
//    auto& ctx = *context.getContext();
//    llvm::IRBuilder<> builder(ctx);
//
//    /// Implementation of a custom function:
//    /// Declare and define a LLVM function `fnPtr` with specific type.
//    /// Use a function type builder to create / declare a function type.
//    auto FnType = llvm::FunctionType::get(llvm::Type::getInt64Ty(ctx), {llvm::Type::getInt64PtrTy(ctx)}, false /* not accept variable length data type */);
//
//    /// Create / insert a function into the module.
//    auto Fn = llvm::cast<llvm::Function>(module->getOrInsertFunction("fnPtr", FnType).getCallee());
//
//    /// Create a basic block.
//    /// Write blocks for the LLVM function `fnPtr`'s  definition.
//    /// BasicBlock: contains codes and instructions of function.
//    llvm::BasicBlock *FnEntryBlock = llvm::BasicBlock::Create(ctx, "entry", Fn);
//    builder.SetInsertPoint(FnEntryBlock);
//
//    /// function fnPtr's arguments in a std::vector, which can be iterated.
//    /// Use your expression tree to roll out code into this block.
//    std::vector<llvm::Value *> FnArgs;
//    for(llvm::Function::arg_iterator ai = Fn->arg_begin(), ae = Fn->arg_end(); ai != ae; ++ai) {
//        FnArgs.push_back(&*ai);
//    }
//
//    /// Codegen: build the code for evaluation of expression tree.
//    llvm::Value* val = expression.build(builder, Fn->arg_begin());
//
//    /// Create a return instruction to return the value that was produced by the expression tree.
//    builder.CreateRet(val);
//
//    /// Dump the module if verbose model.
//    if (verbose) {
//        module->print(llvm::errs(), nullptr);
//    }
//
//    /// Finally compile the module using JIT.
//    /// LLVM module / function is passed to JIT, handing over the ownership to JIT.
//    /// Use JIT `addModule` to generate a function with `data64_t (*)(data64_t *)` signature to evaluate the expression tree.
//    auto err = jit.addModule(std::move(module));
//    if (err) {
//        throw std::runtime_error("module compilation failed");
//    }
//    /// Cast the function into C++ function pointer.
//    fnPtr = reinterpret_cast<data64_t (*)(data64_t *)>(jit.getPointerToFunction("fnPtr"));
}

/// Run an expression.
data64_t ExpressionCompiler::run(data64_t* args) { return fnPtr(args); }
