#include <memory>
#include <llvm/IR/Verifier.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/IRBuilder.h>
#include "moderndbs/codegen/examples.h"

/// Build printf example
std::unique_ptr<llvm::Module> moderndbs::buildPrintfExample(llvm::LLVMContext& ctx) {
    /// First to create a LLVM module: "LLVM code container".
    auto mod = std::make_unique<llvm::Module>("printfExample", ctx);
    /// Create a LLVM IR builder.
    llvm::IRBuilder<> builder(ctx);

    /// Declare a LLVM function `printf` with specific type.
    // declare i32 @printf(i8*, ...)
    //                      |    |
    //  pointer to 8 byte int   variable length argument list, as well as in C's printf
    auto printfT = llvm::FunctionType::get(llvm::Type::getInt32Ty(ctx), {llvm::Type::getInt8Ty(ctx)}, true /* accept variable length data type */);
    auto printfFn = llvm::cast<llvm::Function>(mod->getOrInsertFunction("printf", printfT).getCallee());

    /// Implementation of a custom function:
    /// Declare and define a LLVM function `foo` with specific type.
    // define i32 @foo(i32 %x, i32 %y) {
    auto fooT = llvm::FunctionType::get(llvm::Type::getInt32Ty(ctx), {llvm::Type::getInt32Ty(ctx), llvm::Type::getInt32Ty(ctx)}, false /* does not accept variable length parameter list */);
    auto fooFn = llvm::cast<llvm::Function>(mod->getOrInsertFunction("foo", fooT).getCallee());

    /// Write blocks for the LLVM function `foo`'s  definition.
    /// BasicBlock: contains codes and instructions of function.

    // entry:
    // %tmp = mul i32 %x, %y
    llvm::BasicBlock *fooFnEntryBlock = llvm::BasicBlock::Create(ctx, "entry" /* a function label for jumping and branching */, fooFn /* embedd into foo function */);
    builder.SetInsertPoint(fooFnEntryBlock);

    /// function foo's arguments in a std::vector, which can be iterated.
    std::vector<llvm::Value *> fooFnArgs;
    for(llvm::Function::arg_iterator ai = fooFn->arg_begin(), ae = fooFn->arg_end(); ai != ae; ++ai) {
        fooFnArgs.push_back(&*ai);
    }
    if(fooFnArgs.size() < 2) { throw("LLVM: foo() does not have enough arguments"); }

    llvm::Value *fooFnTmp = builder.CreateMul(fooFnArgs[0] /* %x */, fooFnArgs[1] /* %y */);

    /// Include `printf` function call inside of our foo function.
    // call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([4 x i8], [4 x i8]* @f, i32 0, i32 0), i32 %tmp)
    llvm::Value *fooFnStrPtr = builder.CreateGlobalStringPtr("%d\n", "f");  /// create the global string at the top of LLVM.
    std::vector<llvm::Value*> fprintArgs;  /// The arguments for the printf function.
    fprintArgs.push_back(fooFnStrPtr);
    fprintArgs.push_back(fooFnTmp);
    builder.CreateCall(printfFn, llvm::makeArrayRef(fprintArgs));

    /// Create return instruction.
    // ret i32 %tmp
    builder.CreateRet(fooFnTmp);

    /// Dump the module.
    mod->print(llvm::errs(), nullptr);
    return mod /* return a module */;
}
