#ifndef INCLUDE_MODERNDBS_CODEGEN_EXPRESSION_H
#define INCLUDE_MODERNDBS_CODEGEN_EXPRESSION_H

#include <memory>
#include "moderndbs/codegen/jit.h"
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

namespace moderndbs {
   /// A value (can either be a signed (!) 64 bit integer or a double).
   using data64_t = uint64_t;

   struct Expression {
       /// A value type.
       enum class ValueType {
           INT64,
           DOUBLE
       };

       /// The constant type.
       ValueType type;

       /// Constructor.
       explicit Expression(ValueType type): type(type) {}
       /// Destructor
       virtual ~Expression() = default;

       /// Get the expression type.
       [[nodiscard]] ValueType getType() const { return type; }
       /// Interpretation. Evaluate the expression.
       virtual data64_t evaluate(const data64_t* args) = 0;
       /// Code Generation. Build the expression code.
       virtual llvm::Value* build(llvm::IRBuilder<>& builder, llvm::Value* args) = 0;
   };

   struct Constant: public Expression {
       /// The constant value.
       data64_t value;

       /// Constructor.
       explicit Constant(long long value) : Expression(ValueType::INT64), value(*reinterpret_cast<data64_t*>(&value)) {}  // NOLINT
       /// Constructor.
       explicit Constant(double value) : Expression(ValueType::DOUBLE), value(*reinterpret_cast<data64_t*>(&value)) {}

       data64_t evaluate(const data64_t* /* args */ ) override { return value; }

       llvm::Value* build(llvm::IRBuilder<>& builder, llvm::Value* /* args */ ) override {
            /// Solution Idea.
            if (type == ValueType::INT64) {
                 return llvm::ConstantInt::getSigned(builder.getInt64Ty(), *reinterpret_cast<int64_t*>(&value));
            } else {
               assert(type == ValueType::DOUBLE);
               return llvm::ConstantFP::get(builder.getDoubleTy(), *reinterpret_cast<double*>(&value));
            }

//           // Just treat the bytes inside of Int64Ty. Do not care the type.
//           return llvm::ConstantInt::get(llvm::IntegerType::getInt64Ty(builder.getContext()), value);

           // The following also works.
//          if (type == ValueType::INT64) {
//               // The following line does work.
//               return llvm::ConstantInt::get(builder.getContext(), llvm::APInt(/*nbits*/64, value, /*bool*/false));
//           } else {
//               assert(type == ValueType::DOUBLE);
//               // The following line does work!
//               llvm::Value* double_val = llvm::ConstantFP::get(builder.getContext(), llvm::APFloat(static_cast<double>(value)));
//               return builder.CreateFPToSI(double_val, llvm::Type::getInt64Ty(builder.getContext()));
//           }
       }
   };

   struct Argument: public Expression {
       /// The argument index.
       uint64_t index;

       /// Constructor.
       Argument(uint64_t index, ValueType type) : Expression(type), index(index) {}

       data64_t evaluate(const data64_t* args) override { return args[index]; }

       llvm::Value* build(llvm::IRBuilder<>& builder, llvm::Value* args) override {
            /// Solution Idea.
            auto gepi = builder.CreateConstGEP1_64(args, index);
            auto load = builder.CreateLoad(gepi);
            if (type == ValueType::INT64) {
                return builder.CreateBitCast(load, builder.getInt64Ty());
            } else {
                assert(type == ValueType::DOUBLE);
                return builder.CreateBitCast(load, builder.getDoubleTy());
            }

//           // First create an getelementptr. Using LLVM's poionter arthmetic.
//           llvm::Value* argument = builder.CreateConstGEP1_64(args, index);
//           // then dereference that pointer.
//           return builder.CreateLoad(argument);
       }
   };

   struct Cast: public Expression {
       /// The child.
       Expression& child;
       /// The child type.
       ValueType childType;

       /// Constructor.
       Cast(Expression& child, ValueType type) : Expression(type), child(child) { childType = child.getType(); }

       data64_t evaluate(const data64_t* args) override {
           data64_t child_eval = child.evaluate(args);
           if (childType == getType()) {
               /// No need to cast.
               return child_eval;
           }
           if (getType() == ValueType::INT64) {
               assert(childType  == ValueType::DOUBLE);
               /// double -> int.
               int64_t res = static_cast<int64_t>(*reinterpret_cast<double*>(&child_eval));  // NOLINT
               return *reinterpret_cast<data64_t*>(&res);
           } else {
               assert(childType == ValueType::INT64);
               /// int -> double.
               double res = static_cast<double>(*reinterpret_cast<int64_t*>(&child_eval));  // NOLINT
               return *reinterpret_cast<data64_t*>(&res);
           }
       }

       llvm::Value* build(llvm::IRBuilder<>& builder, llvm::Value* args) override {
           llvm::Value *child_val = child.build(builder, args);
           if (childType == getType()) {
               /// No need to cast.
               return child_val;
           }

           /// Solution Idea.
           if (getType() == ValueType::INT64) {
               assert(childType  == ValueType::DOUBLE);
               /// double -> int.
               return builder.CreateFPToSI(child_val, builder.getInt64Ty());
           } else {
               assert(childType == ValueType::INT64);
               /// int -> double.
               return builder.CreateSIToFP(child_val, builder.getDoubleTy());
           }

//           if (getType() == ValueType::INT64) {
//               assert(childType  == ValueType::DOUBLE);
//               /// double -> int.
//               llvm::Value* double_val = builder.CreateBitCast(child_val, llvm::Type::getDoubleTy(builder.getContext()));
//               return builder.CreateFPToSI(double_val, llvm::IntegerType::getInt64Ty(builder.getContext()));
//           } else {
//               assert(childType == ValueType::INT64);
//               /// int -> double.
//               return builder.CreateSIToFP(child_val, llvm::Type::getDoubleTy(builder.getContext()));
//           }
       }
   };

   struct BinaryExpression: public Expression {
       /// The left child.
       Expression& left;
       /// The right child.
       Expression& right;

       /// Constructor.
       BinaryExpression(Expression& left, Expression& right) : Expression(ValueType::INT64), left(left), right(right) {
           assert(left.getType() == right.getType() && "the left and right type must consistent");
           type = left.getType();
       }
   };

   struct AddExpression: public BinaryExpression {
       /// Constructor
       AddExpression(Expression& left, Expression& right) : BinaryExpression(left, right) {}

       data64_t evaluate(const data64_t* args) override {
           data64_t l_eval = left.evaluate(args);
           data64_t r_eval = right.evaluate(args);
           if (left.getType() == ValueType::INT64) {
               const int64_t l_val = *reinterpret_cast<int64_t*>(&l_eval);
               const int64_t r_val = *reinterpret_cast<int64_t*>(&r_eval);
               int64_t add = l_val + r_val;
               return *reinterpret_cast<data64_t*>(&add);
           } else {
               assert(left.getType() == ValueType::DOUBLE);
               const double l_val = *reinterpret_cast<double*>(&l_eval);
               const double r_val = *reinterpret_cast<double*>(&r_eval);
               double add = l_val + r_val;
               return *reinterpret_cast<data64_t*>(&add);
           }
        }

       llvm::Value* build(llvm::IRBuilder<>& builder, llvm::Value* args) override {
           llvm::Value *l_val = left.build(builder, args);
           llvm::Value *r_val = right.build(builder, args);

           /// Solution Idea.
           if (left.getType() == ValueType::INT64) {
               return builder.CreateAdd(l_val /* %x */, r_val /* %y */);
           } else {
               return builder.CreateFAdd(l_val /* %x */, r_val /* %y */);
           }

//           if (left.getType() == ValueType::INT64) {
//               return builder.CreateAdd(l_val /* %x */, r_val /* %y */);
//           } else {
//               assert(left.getType() == ValueType::DOUBLE);
//               llvm::Value* double_val = builder.CreateFAdd(builder.CreateBitCast(l_val, llvm::Type::getDoubleTy(builder.getContext())) /* %x */,
//                                                           builder.CreateBitCast(r_val, llvm::Type::getDoubleTy(builder.getContext())) /* %y */);
//               return builder.CreateBitCast(double_val, llvm::IntegerType::getInt64Ty(builder.getContext()));
//           }
       }
   };

   struct SubExpression: public BinaryExpression {
       /// Constructor
       SubExpression(Expression& left, Expression& right) : BinaryExpression(left, right) {}

       data64_t evaluate(const data64_t* args) override {
           data64_t l_eval = left.evaluate(args);
           data64_t r_eval = right.evaluate(args);
           if (left.getType() == ValueType::INT64) {
               const int64_t l_val = *reinterpret_cast<int64_t*>(&l_eval);
               const int64_t r_val = *reinterpret_cast<int64_t*>(&r_eval);
               int64_t add = l_val - r_val;
               return *reinterpret_cast<data64_t*>(&add);
           } else {
               assert(left.getType() == ValueType::DOUBLE);
               const double l_val = *reinterpret_cast<double*>(&l_eval);
               const double r_val = *reinterpret_cast<double*>(&r_eval);
               double add = l_val - r_val;
               return *reinterpret_cast<data64_t*>(&add);
           }
       }

       llvm::Value* build(llvm::IRBuilder<>& builder, llvm::Value* args) override {
           llvm::Value *l_val = left.build(builder, args);
           llvm::Value *r_val = right.build(builder, args);

           /// Solution Idea.
           if (left.getType() == ValueType::INT64) {
               return builder.CreateSub(l_val /* %x */, r_val /* %y */);
           } else {
               return builder.CreateFSub(l_val /* %x */, r_val /* %y */);
           }

//           if (left.getType() == ValueType::INT64) {
//               return builder.CreateSub(l_val /* %x */, r_val /* %y */);
//           } else {
//               assert(left.getType() == ValueType::DOUBLE);
//               llvm::Value* double_val = builder.CreateFSub(builder.CreateBitCast(l_val, llvm::Type::getDoubleTy(builder.getContext())) /* %x */,
//                                                            builder.CreateBitCast(r_val, llvm::Type::getDoubleTy(builder.getContext())) /* %y */);
//               return builder.CreateBitCast(double_val, llvm::IntegerType::getInt64Ty(builder.getContext()));
//           }
       }
   };

   struct MulExpression: public BinaryExpression {
       /// Constructor
       MulExpression(Expression& left, Expression& right) : BinaryExpression(left, right) {}

       data64_t evaluate(const data64_t* args) override {
           data64_t l_eval = left.evaluate(args);
           data64_t r_eval = right.evaluate(args);
           if (left.getType() == ValueType::INT64) {
               const int64_t l_val = *reinterpret_cast<int64_t*>(&l_eval);
               const int64_t r_val = *reinterpret_cast<int64_t*>(&r_eval);
               int64_t add = l_val * r_val;
               return *reinterpret_cast<data64_t*>(&add);
           } else {
               assert(left.getType() == ValueType::DOUBLE);
               const double l_val = *reinterpret_cast<double*>(&l_eval);
               const double r_val = *reinterpret_cast<double*>(&r_eval);
               double add = l_val * r_val;
               return *reinterpret_cast<data64_t*>(&add);
           }
       }

       llvm::Value* build(llvm::IRBuilder<>& builder, llvm::Value* args) override {
           llvm::Value *l_val = left.build(builder, args);
           llvm::Value *r_val = right.build(builder, args);

           /// Solution Idea.
           if (left.getType() == ValueType::INT64) {
               return builder.CreateMul(l_val /* %x */, r_val /* %y */);
           } else {
               return builder.CreateFMul(l_val /* %x */, r_val /* %y */);
           }

//           if (left.getType() == ValueType::INT64) {
//               return builder.CreateMul(l_val /* %x */, r_val /* %y */);
//           } else {
//               assert(left.getType() == ValueType::DOUBLE);
//               llvm::Value* double_val = builder.CreateFMul(builder.CreateBitCast(l_val, llvm::Type::getDoubleTy(builder.getContext())) /* %x */,
//                                                            builder.CreateBitCast(r_val, llvm::Type::getDoubleTy(builder.getContext())) /* %y */);
//               return builder.CreateBitCast(double_val, llvm::IntegerType::getInt64Ty(builder.getContext()));
//           }
       }
   };

   struct DivExpression: public BinaryExpression {
       /// Constructor
       DivExpression(Expression& left, Expression& right) : BinaryExpression(left, right) {}

       data64_t evaluate(const data64_t* args) override {
       data64_t l_eval = left.evaluate(args);
           data64_t r_eval = right.evaluate(args);
           if (left.getType() == ValueType::INT64) {
               const int64_t l_val = *reinterpret_cast<int64_t*>(&l_eval);
               const int64_t r_val = *reinterpret_cast<int64_t*>(&r_eval);
               int64_t add = l_val / r_val;
               return *reinterpret_cast<data64_t *>(&add);
           } else {
               assert(left.getType() == ValueType::DOUBLE);
               const double l_val = *reinterpret_cast<double*>(&l_eval);
               const double r_val = *reinterpret_cast<double*>(&r_eval);
               double add = l_val / r_val;
               return *reinterpret_cast<data64_t *>(&add);
           }
       }

       llvm::Value* build(llvm::IRBuilder<>& builder, llvm::Value* args) override {
           llvm::Value *l_val = left.build(builder, args);
           llvm::Value *r_val = right.build(builder, args);

           /// Solution Idea.
           if (left.getType() == ValueType::INT64) {
               return builder.CreateSDiv(l_val /* %x */, r_val /* %y */);
           } else {
               return builder.CreateFDiv(l_val /* %x */, r_val /* %y */);
            }

//           if (left.getType() == ValueType::INT64) {
//               // SDiv: signed division.
//               return builder.CreateSDiv(l_val /* %x */, r_val /* %y */);
//           } else {
//               assert(left.getType() == ValueType::DOUBLE);
//               llvm::Value* double_val = builder.CreateFDiv(builder.CreateBitCast(l_val, llvm::Type::getDoubleTy(builder.getContext())) /* %x */,
//                                                            builder.CreateBitCast(r_val, llvm::Type::getDoubleTy(builder.getContext())) /* %y */);
//               return builder.CreateBitCast(double_val, llvm::IntegerType::getInt64Ty(builder.getContext()));;
//           }
       }
   };

   struct ExpressionCompiler {
       /// The llvm context.
       llvm::orc::ThreadSafeContext& context;
       /// The llvm module.
       std::unique_ptr<llvm::Module> module;
       /// The jit.
       /// To generate a C++ function pointer from the compiled function LLVM code.
       JIT jit;
       /// The compiled function.
       data64_t (*fnPtr)(data64_t* args);

       /// Constructor.
       explicit ExpressionCompiler(llvm::orc::ThreadSafeContext& context);

       /// Compile an expression.
       void compile(Expression& expression, bool verbose = false);
       /// Run a previously compiled expression
       data64_t run(data64_t* args);
   };

}  // namespace moderndbs

#endif
