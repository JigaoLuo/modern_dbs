// ---------------------------------------------------------------------------
// MODERNDBS
// ---------------------------------------------------------------------------
#include <gtest/gtest.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/DynamicLibrary.h>
// ---------------------------------------------------------------------------
int main(int argc, char *argv[]) {
    testing::InitGoogleTest(&argc, argv);
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::sys::DynamicLibrary::LoadLibraryPermanently(nullptr);
    return RUN_ALL_TESTS();
}
// ---------------------------------------------------------------------------
