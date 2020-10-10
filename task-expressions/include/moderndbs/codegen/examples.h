#ifndef INCLUDE_MODERNDBS_EXPRESSION_EXAMPLES_H
#define INCLUDE_MODERNDBS_EXPRESSION_EXAMPLES_H

#include <memory>
#include "llvm/IR/Module.h"

namespace moderndbs {

std::unique_ptr<llvm::Module> buildPrintfExample(llvm::LLVMContext& ctx);

}  // namespace moderndbs

#endif
