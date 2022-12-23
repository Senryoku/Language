#include "Module.hpp"

#include <vector>

#include <GlobalTypeRegistry.hpp>

static void dump(auto llvm_object) {
#ifndef NDEBUG // dump is not available in release builds of LLVM
    llvm_object->dump();
#endif
}

// FIXME: We should not use maps here actually. Find another way to manage the nested switch cases.

#define OP(VALUETYPE, FUNC)                                                                    \
    {                                                                                          \
        PrimitiveType::VALUETYPE, [](llvm::IRBuilder<>& ir_builder, llvm::Value* val) { FUNC } \
    }
static std::unordered_map<Token::Type, std::unordered_map<PrimitiveType, std::function<llvm::Value*(llvm::IRBuilder<>&, llvm::Value*)>>> unary_ops = {
    {Token::Type::Addition, {OP(Integer, (void)ir_builder; return val;), OP(Float, (void)ir_builder; return val;)}},
    {Token::Type::Substraction, {OP(Integer, return ir_builder.CreateNeg(val, "neg");), OP(Float, return ir_builder.CreateFNeg(val, "fneg");)}},
};
#undef OP

#define OP(VALUETYPE, FUNC)                                                                                      \
    {                                                                                                            \
        PrimitiveType::VALUETYPE, [](llvm::IRBuilder<>& ir_builder, llvm::Value* lhs, llvm::Value* rhs) { FUNC } \
    }

static std::unordered_map<Token::Type, std::unordered_map<PrimitiveType, std::function<llvm::Value*(llvm::IRBuilder<>&, llvm::Value*, llvm::Value*)>>> binary_ops = {
    {Token::Type::Addition,
     {OP(Integer, return ir_builder.CreateAdd(lhs, rhs, "add");), OP(U8, return ir_builder.CreateAdd(lhs, rhs, "add");), OP(U16, return ir_builder.CreateAdd(lhs, rhs, "add");),
      OP(U32, return ir_builder.CreateAdd(lhs, rhs, "add");), OP(U64, return ir_builder.CreateAdd(lhs, rhs, "add");), OP(I8, return ir_builder.CreateAdd(lhs, rhs, "add");),
      OP(I16, return ir_builder.CreateAdd(lhs, rhs, "add");), OP(I32, return ir_builder.CreateAdd(lhs, rhs, "add");), OP(I64, return ir_builder.CreateAdd(lhs, rhs, "add");),
      OP(Float, return ir_builder.CreateFAdd(lhs, rhs, "fadd");)}},
    {Token::Type::Substraction,
     {OP(Integer, return ir_builder.CreateSub(lhs, rhs, "sub");), OP(U8, return ir_builder.CreateSub(lhs, rhs, "sub");), OP(U16, return ir_builder.CreateSub(lhs, rhs, "sub");),
      OP(U32, return ir_builder.CreateSub(lhs, rhs, "sub");), OP(U64, return ir_builder.CreateSub(lhs, rhs, "sub");), OP(I8, return ir_builder.CreateSub(lhs, rhs, "sub");),
      OP(I16, return ir_builder.CreateSub(lhs, rhs, "sub");), OP(I32, return ir_builder.CreateSub(lhs, rhs, "sub");), OP(I64, return ir_builder.CreateSub(lhs, rhs, "sub");),
      OP(Float, return ir_builder.CreateFSub(lhs, rhs, "fsub");)}},
    {Token::Type::Multiplication,
     {OP(Integer, return ir_builder.CreateMul(lhs, rhs, "mul");), OP(U8, return ir_builder.CreateMul(lhs, rhs, "mul");), OP(U16, return ir_builder.CreateMul(lhs, rhs, "mul");),
      OP(U32, return ir_builder.CreateMul(lhs, rhs, "mul");), OP(U64, return ir_builder.CreateMul(lhs, rhs, "mul");), OP(I8, return ir_builder.CreateMul(lhs, rhs, "mul");),
      OP(I16, return ir_builder.CreateMul(lhs, rhs, "mul");), OP(I32, return ir_builder.CreateMul(lhs, rhs, "mul");), OP(I64, return ir_builder.CreateMul(lhs, rhs, "mul");),
      OP(Float, return ir_builder.CreateFMul(lhs, rhs, "fmul");)}},
    {Token::Type::Division, {OP(Integer, return ir_builder.CreateSDiv(lhs, rhs, "div");), OP(Float, return ir_builder.CreateFDiv(lhs, rhs, "fdiv");)}},
    {Token::Type::Modulus, {OP(Integer, return ir_builder.CreateSRem(lhs, rhs, "srem");)}},
    // Comparisons
    {Token::Type::Equal,
     {OP(Integer, return ir_builder.CreateCmp(llvm::CmpInst::Predicate::ICMP_EQ, lhs, rhs, "ICMP_EQ");),
      OP(Float, return ir_builder.CreateFCmp(llvm::CmpInst::Predicate::FCMP_OEQ, lhs, rhs, "FCMP_OEQ");)}},
    {Token::Type::Different,
     {OP(Integer, return ir_builder.CreateCmp(llvm::CmpInst::Predicate::ICMP_NE, lhs, rhs, "ICMP_NE");),
      OP(Char, return ir_builder.CreateCmp(llvm::CmpInst::Predicate::ICMP_NE, lhs, rhs, "ICMP_NE");),
      OP(Float, return ir_builder.CreateFCmp(llvm::CmpInst::Predicate::FCMP_ONE, lhs, rhs, "FCMP_ONE");)}},
    {Token::Type::Lesser,
     {OP(Integer, return ir_builder.CreateCmp(llvm::CmpInst::Predicate::ICMP_SLT, lhs, rhs, "ICMP_SLT");),
      OP(U8, return ir_builder.CreateCmp(llvm::CmpInst::Predicate::ICMP_ULT, lhs, rhs, "ICMP_ULT");),
      OP(U16, return ir_builder.CreateCmp(llvm::CmpInst::Predicate::ICMP_ULT, lhs, rhs, "ICMP_ULT");),
      OP(U32, return ir_builder.CreateCmp(llvm::CmpInst::Predicate::ICMP_ULT, lhs, rhs, "ICMP_ULT");),
      OP(U64, return ir_builder.CreateCmp(llvm::CmpInst::Predicate::ICMP_ULT, lhs, rhs, "ICMP_ULT");),
      OP(I8, return ir_builder.CreateCmp(llvm::CmpInst::Predicate::ICMP_SLT, lhs, rhs, "ICMP_SLT");),
      OP(I16, return ir_builder.CreateCmp(llvm::CmpInst::Predicate::ICMP_SLT, lhs, rhs, "ICMP_SLT");),
      OP(I32, return ir_builder.CreateCmp(llvm::CmpInst::Predicate::ICMP_SLT, lhs, rhs, "ICMP_SLT");),
      OP(I64, return ir_builder.CreateCmp(llvm::CmpInst::Predicate::ICMP_SLT, lhs, rhs, "ICMP_SLT");),
      OP(Float, return ir_builder.CreateFCmp(llvm::CmpInst::Predicate::FCMP_OLT, lhs, rhs, "FCMP_OLT");)}},
    {Token::Type::LesserOrEqual,
     {OP(Integer, return ir_builder.CreateCmp(llvm::CmpInst::Predicate::ICMP_SLE, lhs, rhs, "ICMP_SLE");),
      OP(U8, return ir_builder.CreateCmp(llvm::CmpInst::Predicate::ICMP_ULE, lhs, rhs, "ICMP_ULE");),
      OP(U16, return ir_builder.CreateCmp(llvm::CmpInst::Predicate::ICMP_ULE, lhs, rhs, "ICMP_ULE");),
      OP(U32, return ir_builder.CreateCmp(llvm::CmpInst::Predicate::ICMP_ULE, lhs, rhs, "ICMP_ULE");),
      OP(U64, return ir_builder.CreateCmp(llvm::CmpInst::Predicate::ICMP_ULE, lhs, rhs, "ICMP_ULE");),
      OP(I8, return ir_builder.CreateCmp(llvm::CmpInst::Predicate::ICMP_SLE, lhs, rhs, "ICMP_SLE");),
      OP(I16, return ir_builder.CreateCmp(llvm::CmpInst::Predicate::ICMP_SLE, lhs, rhs, "ICMP_SLE");),
      OP(I32, return ir_builder.CreateCmp(llvm::CmpInst::Predicate::ICMP_SLE, lhs, rhs, "ICMP_SLE");),
      OP(I64, return ir_builder.CreateCmp(llvm::CmpInst::Predicate::ICMP_SLE, lhs, rhs, "ICMP_SLE");),
      OP(Float, return ir_builder.CreateFCmp(llvm::CmpInst::Predicate::FCMP_OLE, lhs, rhs, "FCMP_OLE");)}},
    {Token::Type::Greater,
     {OP(Integer, return ir_builder.CreateCmp(llvm::CmpInst::Predicate::ICMP_SGT, lhs, rhs, "ICMP_SGT");),
      OP(U8, return ir_builder.CreateCmp(llvm::CmpInst::Predicate::ICMP_UGT, lhs, rhs, "ICMP_UGT");),
      OP(U16, return ir_builder.CreateCmp(llvm::CmpInst::Predicate::ICMP_UGT, lhs, rhs, "ICMP_UGT");),
      OP(U32, return ir_builder.CreateCmp(llvm::CmpInst::Predicate::ICMP_UGT, lhs, rhs, "ICMP_UGT");),
      OP(U64, return ir_builder.CreateCmp(llvm::CmpInst::Predicate::ICMP_UGT, lhs, rhs, "ICMP_UGT");),
      OP(I8, return ir_builder.CreateCmp(llvm::CmpInst::Predicate::ICMP_SGT, lhs, rhs, "ICMP_SGT");),
      OP(I16, return ir_builder.CreateCmp(llvm::CmpInst::Predicate::ICMP_SGT, lhs, rhs, "ICMP_SGT");),
      OP(I32, return ir_builder.CreateCmp(llvm::CmpInst::Predicate::ICMP_SGT, lhs, rhs, "ICMP_SGT");),
      OP(I64, return ir_builder.CreateCmp(llvm::CmpInst::Predicate::ICMP_SGT, lhs, rhs, "ICMP_SGT");),
      OP(Float, return ir_builder.CreateFCmp(llvm::CmpInst::Predicate::FCMP_OGT, lhs, rhs, "FCMP_OGT");)}},
    {Token::Type::GreaterOrEqual,
     {OP(Integer, return ir_builder.CreateCmp(llvm::CmpInst::Predicate::ICMP_SGE, lhs, rhs, "ICMP_SGE");),
      OP(Float, return ir_builder.CreateFCmp(llvm::CmpInst::Predicate::FCMP_OGE, lhs, rhs, "FCMP_OGE");)}},
};
#undef OP

llvm::Constant* Module::codegen_constant(const AST::Node* val) {
    auto type = GlobalTypeRegistry::instance().get_type(val->type_id);
    assert(type);
    if(type->is_array()) {
        const auto*                  type_arr = dynamic_cast<const ArrayType*>(type);
        auto                         element_type = get_llvm_type(type_arr->element_type);
        std::vector<llvm::Constant*> values(type_arr->capacity);
        for(unsigned int i = 0; i < type_arr->capacity; i++)
            values[i] = codegen_constant(val->children[i]);

        auto array_type = llvm::ArrayType::get(element_type, values.size());
        auto global_declaration = (llvm::GlobalVariable*)_llvm_module->getOrInsertGlobal("", array_type);
        global_declaration->setInitializer(llvm::ConstantArray::get(array_type, values));
        global_declaration->setConstant(true);
        global_declaration->setLinkage(llvm::GlobalValue::LinkageTypes::PrivateLinkage);
        global_declaration->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
        return llvm::ConstantExpr::getBitCast(global_declaration, array_type->getPointerTo());
    }
    if(type->is_pointer() && val->type_id != PrimitiveType::CString) {
        // Doesn't make sense, does it?
        throw Exception("Literal pointer? What?");
    }
    switch(val->type_id) {
        using enum PrimitiveType;
        case Boolean: return llvm::ConstantInt::get(*_llvm_context, llvm::APInt(1, dynamic_cast<const AST::BoolLiteral*>(val)->value));
        case Char: return llvm::ConstantInt::get(*_llvm_context, llvm::APInt(8, dynamic_cast<const AST::CharLiteral*>(val)->value));
        case Float: return llvm::ConstantFP::get(*_llvm_context, llvm::APFloat(dynamic_cast<const AST::FloatLiteral*>(val)->value));
        case I32: [[fallthrough]];
        case Integer: return llvm::ConstantInt::get(*_llvm_context, llvm::APInt(32, dynamic_cast<const AST::IntegerLiteral*>(val)->value));
        case CString: {
            // FIXME: Take a look at llvm::StringRef and llvm::Twine
            auto        str_node = dynamic_cast<const AST::StringLiteral*>(val);
            const auto& str = str_node->value;
            auto        charType = llvm::IntegerType::get(*_llvm_context, 8);

            // 1. Initialize chars vector
            std::vector<llvm::Constant*> chars(str.size());
            for(unsigned int i = 0; i < str.size(); i++)
                chars[i] = llvm::ConstantInt::get(charType, *(str.begin() + i));

            // 1b. add a zero terminator too
            // FIXME: Ultimately string will have a built-in length, should we keep the terminating null byte anyway?
            chars.push_back(llvm::ConstantInt::get(charType, 0));

            // 2. Initialize the string from the characters
            auto stringType = llvm::ArrayType::get(charType, chars.size());

            // 3. Create the declaration statement
            auto globalDeclaration = (llvm::GlobalVariable*)_llvm_module->getOrInsertGlobal(str.data(), stringType);
            globalDeclaration->setInitializer(llvm::ConstantArray::get(stringType, chars));
            globalDeclaration->setConstant(true);
            globalDeclaration->setLinkage(llvm::GlobalValue::LinkageTypes::PrivateLinkage);
            globalDeclaration->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);

            // 4. Return a cast to an i8*
            return llvm::ConstantExpr::getBitCast(globalDeclaration, charType->getPointerTo());
        }
        default: warn("LLVM Codegen: Unsupported constant value type '{}'.\n", *type);
    }
    return nullptr;
}

llvm::Value* Module::codegen(const AST::Node* node) {
    switch(node->type) {
        case AST::Node::Type::Root: [[fallthrough]];
        case AST::Node::Type::Statement: {
            llvm::Value* ret = nullptr;
            for(auto c : node->children) {
                ret = codegen(c);
            }
            return ret;
        }
        case AST::Node::Type::Scope: {
            push_scope();
            llvm::Value* ret = nullptr;
            for(auto c : node->children)
                ret = codegen(c);
            pop_scope();
            return ret;
        }
        case AST::Node::Type::Defer: {
            assert(false); // Defer nodes should not be in the basic AST.
            return nullptr;
        }
        case AST::Node::Type::ConstantValue: return codegen_constant(node);
        case AST::Node::Type::Cast: {
            assert(node->children.size() == 1);
            auto child = codegen(node->children[0]);
            if(!child)
                return nullptr;
            // TODO: Handle child's type.
            if(is_primitive(node->type_id)) {
                switch(node->type_id) {
                    case PrimitiveType::Float: {
                        assert(node->children[0]->type_id == PrimitiveType::Integer); // TEMP
                        return _llvm_ir_builder.CreateSIToFP(child, llvm::Type::getFloatTy(*_llvm_context), "castSIToFP");
                    }
                    case PrimitiveType::U8: [[fallthrough]];
                    case PrimitiveType::U16: [[fallthrough]];
                    case PrimitiveType::U32: [[fallthrough]];
                    case PrimitiveType::U64: {
                        return _llvm_ir_builder.CreateCast(llvm::Instruction::ZExt, child, get_llvm_type(node->type_id), "castZeroExt");
                    }
                    case PrimitiveType::Integer: {
                        if(node->children[0]->type_id == PrimitiveType::Float || node->children[0]->type_id == PrimitiveType::Double)
                            return _llvm_ir_builder.CreateFPToSI(child, llvm::Type::getInt32Ty(*_llvm_context), "castFPToSI");
                        [[fallthrough]];
                    }
                    case PrimitiveType::I8: [[fallthrough]];
                    case PrimitiveType::I16: [[fallthrough]];
                    case PrimitiveType::I32: [[fallthrough]];
                    case PrimitiveType::I64: {
                        return _llvm_ir_builder.CreateCast(llvm::Instruction::SExt, child, get_llvm_type(node->type_id), "castSignExt");
                    }
                    case PrimitiveType::Pointer: {
                        auto type = GlobalTypeRegistry::instance().get_type(node->children[0]->type_id);
                        assert(type->is_pointer());
                        auto as_int = _llvm_ir_builder.CreatePtrToInt(child, llvm::Type::getInt64Ty(*_llvm_context), "castToU64");
                        return _llvm_ir_builder.CreateIntToPtr(as_int, get_llvm_type(node->type_id), "castToVoidPtr");
                    }
                    case PrimitiveType::CString: {
                        if(node->children[0]->type_id == PrimitiveType::Pointer) {
                            auto as_int = _llvm_ir_builder.CreatePtrToInt(child, llvm::Type::getInt64Ty(*_llvm_context), "castToU64");
                            return _llvm_ir_builder.CreateIntToPtr(as_int, get_llvm_type(node->type_id), "castToCStr");
                        }
                        [[fallthrough]];
                    }
                    default:
                        error("[LLVMCodegen] LLVM::Codegen: Cast from {} to {} not supported.\n", type_id_to_string(node->children[0]->type_id), type_id_to_string(node->type_id));
                        return nullptr;
                }
            } else {
                // Generic Pointer type to Typed Ptr
                auto type = GlobalTypeRegistry::instance().get_type(node->type_id);
                if(type->is_pointer() && node->children[0]->type_id == PrimitiveType::Pointer) {
                    auto as_int = _llvm_ir_builder.CreatePtrToInt(child, llvm::Type::getInt64Ty(*_llvm_context), "castToU64");
                    return _llvm_ir_builder.CreateIntToPtr(as_int, get_llvm_type(node->type_id), "castToTypedPtr");
                }
            }
        }
        case AST::Node::Type::TypeDeclaration: {
            auto type = GlobalTypeRegistry::instance().get_type(node->type_id);
            // Ignore Template definitions, we only care about actual instanciations.
            if(type->is_placeholder())
                break;
            std::vector<llvm::Type*> members;
            for(const auto c : node->children)
                members.push_back(get_llvm_type(c->type_id));
            std::string type_name(node->token.value);
            llvm::StructType::create(*_llvm_context, members, type_name);
            break;
        }
        case AST::Node::Type::FunctionDeclaration: {
            auto function_declaration_node = dynamic_cast<const AST::FunctionDeclaration*>(node);
            // Ignore Template definitions, we only care about actual instanciations.
            if(function_declaration_node->is_templated())
                break;
            auto function_name = function_declaration_node->mangled_name();
            auto prev_function = _llvm_module->getFunction(function_name);
            if(prev_function) { // Should be handled by the parser.
                warn("[Module] Redefinition of function '{}' (line {}).\n", function_name, function_declaration_node->token.line);
                return prev_function;
            }

            std::vector<llvm::Type*> param_types;
            for(auto arg : function_declaration_node->arguments()) {
                auto type = get_llvm_type(arg->type_id);
                assert(type);
                param_types.push_back(type);
            }
            auto return_type = get_llvm_type(function_declaration_node->type_id);
            auto function_types = llvm::FunctionType::get(return_type, param_types, false);
            auto flags = function_declaration_node->flags;

            if(function_declaration_node->body()) {
                // ExternalLinkage: Externally visible function.
                // InternalLinkage: Rename collisions when linking(static functions)
                // PrivateLinkage:  Like Internal, but omit from symbol table.
                auto function =
                    llvm::Function::Create(function_types, flags & AST::FunctionDeclaration::Flag::Exported ? llvm::Function::ExternalLinkage : llvm::Function::PrivateLinkage,
                                           function_name, _llvm_module.get());
                auto  current_block = _llvm_ir_builder.GetInsertBlock();
                auto* block = llvm::BasicBlock::Create(*_llvm_context, "entrypoint", function);
                _llvm_ir_builder.SetInsertPoint(block);

                push_scope(); // Scope for variable declarations
                auto arg_idx = 0;
                for(auto& arg : function->args()) {
                    arg.setName(std::string{function_declaration_node->arguments()[arg_idx]->token.value});
                    auto alloca = codegen(function_declaration_node->arguments()[arg_idx]); // Generate variable declarations
                    _llvm_ir_builder.CreateStore(&arg, alloca);
                    ++arg_idx;
                }

                _generated_return = false;
                auto function_body = codegen(function_declaration_node->body()); // Generate function body
                if(!_generated_return) {
                    _llvm_ir_builder.CreateRet(node->type_id == PrimitiveType::Void ? nullptr : function_body);
                    _generated_return = false;
                }
                pop_scope();

                _llvm_ir_builder.SetInsertPoint(current_block);
                if(verifyFunction(*function, &llvm::errs())) {
                    error("\n[LLVMCodegen] Error verifying function '{}'.\n", function_name);
                    dump(function);
                    function->eraseFromParent();
                    return nullptr;
                }
                return function;
            } else {
                assert(((flags & AST::FunctionDeclaration::Flag::Extern) || (flags & AST::FunctionDeclaration::Flag::Imported)) &&
                       "Functions without a body should be marked as 'extern' or imported.");
                _llvm_module->getOrInsertFunction(function_name, function_types);
                return nullptr;
            }
            return nullptr;
        }
        case AST::Node::Type::FunctionCall: {
            auto function_call_node = dynamic_cast<const AST::FunctionCall*>(node);
            auto mangled_function_name = function_call_node->mangled_name();
            auto function = _llvm_module->getFunction(mangled_function_name);
            if(!function) {
                error("[LLVMCodegen] Call to undeclared function '{}' (line {}).\n", mangled_function_name, function_call_node->token.line);
                return nullptr;
            }
            // TODO: Handle default values.
            // TODO: Handle vargs functions (variable number of parameters, like printf :^) )
            auto function_flags = function_call_node->flags;
            if(!(function_flags & AST::FunctionDeclaration::Flag::Variadic) && function->arg_size() != function_call_node->arguments().size()) {
                error("[LLVMCodegen] Unexpected number of parameters in function call '{}' (line {}): Expected {}, got {}.\n", mangled_function_name, node->token.line,
                      function->arg_size(), function_call_node->arguments().size());
                print("Argument from function call (AST):\n");
                for(auto i = 0; i < function_call_node->arguments().size(); ++i)
                    print("\tArgument #{}: {}", i, *function_call_node->arguments()[i]);
#ifndef NDEBUG
                print("Arguments from registered function in LLVM:\n");
                for(llvm::Function::const_arg_iterator it = function->arg_begin(); it != function->arg_end(); ++it) {
                    print("Argument #{}: ", it->getArgNo());
                    it->dump();
                    print("\n");
                }
#endif
                return nullptr;
            }
            std::vector<llvm::Value*> parameters;
            for(auto arg_node : function_call_node->arguments()) {
                auto v = codegen(arg_node);
                if(!v)
                    return nullptr;
                // C Variadic functions promotes float to double (see https://stackoverflow.com/questions/63144506/printf-doesnt-work-for-floats-in-llvm-ir)
                if(function_flags & AST::FunctionDeclaration::Flag::Variadic && v->getType()->isFloatTy())
                    v = _llvm_ir_builder.CreateFPExt(v, llvm::Type::getDoubleTy(*_llvm_context));
                parameters.push_back(v);
            }
            if(function_call_node->type_id == PrimitiveType::Void) // "Cannot assign a name to void values!"
                return _llvm_ir_builder.CreateCall(function, parameters);
            else
                return _llvm_ir_builder.CreateCall(function, parameters, mangled_function_name);
        }
        case AST::Node::Type::VariableDeclaration: {
            llvm::AllocaInst* ret = nullptr;
            auto              parent_function = _llvm_ir_builder.GetInsertBlock()->getParent();
            if(!parent_function)
                warn("TODO: Correctly handle global variables! ({}:{})\n", __FILE__, __LINE__);
            auto type = get_llvm_type(node->type_id);
            ret = create_entry_block_alloca(parent_function, type, std::string{node->token.value});
            if(!set(node->token.value, ret))
                throw Exception(fmt::format("[LLVMCodegen] Variable '{}' already declared (line {}).\n", node->token.value, node->token.line));
            return ret;
        }
        case AST::Node::Type::Variable: {
            auto var = get(node->token.value);
            if(!var) {
                error("[LLVMCodegen] Undeclared variable '{}'.\n", node->token.value);
                assert(false);
            }
            return var;
        }
        case AST::Node::Type::LValueToRValue: {
            assert(node->children.size() == 1);
            auto child = node->children[0];
            auto value = codegen(child);
            if(child->type == AST::Node::Type::Variable) {
                auto allocaInst = static_cast<llvm::AllocaInst*>(value);
                return _llvm_ir_builder.CreateLoad(allocaInst->getAllocatedType(), allocaInst, "l-to-rvalue");
            }
            // FIXME: I don't know what I'm doing.
            //        These special cases should not be needed. I'm pretty sure.
            if(child->type == AST::Node::Type::BinaryOperator && child->token.type == Token::Type::MemberAccess) {
                assert(value->getType()->isPointerTy());
                return _llvm_ir_builder.CreateLoad(get_llvm_type(node->type_id), value, "l-to-rvalue");
            }
            if(child->type == AST::Node::Type::BinaryOperator && child->token.type == Token::Type::OpenSubscript) {
                auto type = GlobalTypeRegistry::instance().get_type(child->children[0]->type_id);
                if(type->is_array()) {
                    assert(value->getType()->isPointerTy());
                    auto arr_type = dynamic_cast<const ArrayType*>(type);
                    auto element_type = get_llvm_type(arr_type->element_type);
                    return _llvm_ir_builder.CreateLoad(element_type, value, "l-to-rvalue");
                }
                if(type->is_pointer()) {
                    assert(value->getType()->isPointerTy());
                    auto pointee_type = get_llvm_type(dynamic_cast<const PointerType*>(type)->pointee_type);
                    return _llvm_ir_builder.CreateLoad(pointee_type, value, "l-to-rvalue");
                }
            }
            // warn("[LLVMCodegen] LValueToRValue without effect:\n{}", *node);
            return value;
        }
        case AST::Node::Type::GetPointer: {
            // FIXME: Our only case is actually already handled by the MemberAccess node.
            return codegen(node->children[0]);
        }
        case AST::Node::Type::UnaryOperator: {
            auto val = codegen(node->children[0]);
            switch(node->token.type) {
                case Token::Type::Increment: {
                    auto value = _llvm_ir_builder.CreateLoad(get_llvm_type(node->children[0]->type_id), val, "l-to-rvalue");
                    // FIXME: Correctly support all types.
                    auto one = node->children[0]->type_id == PrimitiveType::U64 ? _llvm_ir_builder.getInt64(1) : _llvm_ir_builder.getInt32(1);
                    auto res = _llvm_ir_builder.CreateAdd(value, one, "inc");
                    _llvm_ir_builder.CreateStore(res, val);
                    return val;
                }
                default: {
                    assert(is_primitive(node->children[0]->type_id));
                    auto primitive_type = static_cast<PrimitiveType>(node->children[0]->type_id);
                    if(unary_ops[node->token.type].find(primitive_type) == unary_ops[node->token.type].end()) {
                        error("[LLVMCodegen] Unsupported type {} for unary operator {}.\n", type_id_to_string(primitive_type), node->token.type);
                        return nullptr;
                    }
                    return unary_ops[node->token.type][primitive_type](_llvm_ir_builder, val);
                }
            }
        }
        case AST::Node::Type::MemberIdentifier: {
            auto member_identifier = dynamic_cast<const AST::MemberIdentifier*>(node);
            return llvm::ConstantInt::get(*_llvm_context, llvm::APInt(32, member_identifier->index)); // Returns member index
        }
        case AST::Node::Type::BinaryOperator: {
            auto lhs = codegen(node->children[0]);
            auto rhs = codegen(node->children[1]);
            if(!lhs || !rhs)
                return nullptr;
            // TODO: Overloads.
            switch(node->token.type) {
                case Token::Type::Addition: [[fallthrough]];
                case Token::Type::Substraction: [[fallthrough]];
                case Token::Type::Multiplication: [[fallthrough]];
                case Token::Type::Division: [[fallthrough]];
                case Token::Type::Modulus: [[fallthrough]];
                case Token::Type::Equal: [[fallthrough]];
                case Token::Type::Different: [[fallthrough]];
                case Token::Type::Lesser: [[fallthrough]];
                case Token::Type::LesserOrEqual: [[fallthrough]];
                case Token::Type::Greater: [[fallthrough]];
                case Token::Type::GreaterOrEqual: {
                    // assert(node->children[0]->type_id == node->children[1]->type_id);
                    assert(is_primitive(node->children[0]->type_id));
                    auto primitive_type = static_cast<PrimitiveType>(node->children[0]->type_id);
                    if(binary_ops[node->token.type].find(primitive_type) == binary_ops[node->token.type].end()) {
                        error("[LLVMCodegen] Unsupported types {} and {} for binary operator {}.\n", type_id_to_string(node->children[0]->type_id),
                              type_id_to_string(node->children[1]->type_id), node->token.type);
                        return nullptr;
                    }
                    return binary_ops[node->token.type][primitive_type](_llvm_ir_builder, lhs, rhs);
                }
                case Token::Type::And: {
                    return _llvm_ir_builder.CreateAnd(lhs, rhs, "and");
                }
                case Token::Type::OpenSubscript: {
                    auto type = GlobalTypeRegistry::instance().get_type(node->children[0]->type_id);
                    assert(type->is_array() || type->is_pointer());
                    if(type->is_array()) {
                        auto llvm_type = get_llvm_type(node->children[0]->type_id);
                        return _llvm_ir_builder.CreateGEP(llvm_type, lhs, {llvm::ConstantInt::get(*_llvm_context, llvm::APInt(32, 0)), rhs}, "ArrayGEP");
                    } else if(type->is_pointer()) {
                        auto pointer_type = dynamic_cast<const PointerType*>(type);
                        auto pointee_type = get_llvm_type(pointer_type->pointee_type);
                        // FIXME: This is extremely hackish... I guess we should just make sure to insert a LValueToRValue node when necessary.
                        if(node->children[0]->type == AST::Node::Type::LValueToRValue) {
                            return _llvm_ir_builder.CreateGEP(pointee_type, lhs, {rhs}, "PointerGEP");
                        } else {
                            auto load = _llvm_ir_builder.CreateLoad(pointee_type->getPointerTo(), lhs);
                            return _llvm_ir_builder.CreateGEP(pointee_type, load, {rhs}, "PointerGEP");
                        }
                    }
                    assert(false);
                }
                case Token::Type::Assignment: {
                    _llvm_ir_builder.CreateStore(rhs, lhs);
                    return lhs;
                }
                case Token::Type::MemberAccess: {
                    return _llvm_ir_builder.CreateGEP(get_llvm_type(node->children[0]->type_id), lhs, {llvm::ConstantInt::get(*_llvm_context, llvm::APInt(32, 0)), rhs},
                                                      "memberptr");
                }
                default: warn("[LLVMCodegen] Unimplemented Binary Operator '{}'.\n", node->token.value); break;
            }
            break;
        }
        case AST::Node::Type::Dereference: {
            auto lhs = codegen(node->children[0]);
            auto allocaInst = static_cast<llvm::AllocaInst*>(lhs);
            auto type = GlobalTypeRegistry::instance().get_type(node->children[0]->type_id);
            assert(type->is_pointer());
            return _llvm_ir_builder.CreateLoad(allocaInst->getAllocatedType(), lhs);
        }
        case AST::Node::Type::WhileStatement: {
            llvm::Function* current_function = _llvm_ir_builder.GetInsertBlock()->getParent();
            // auto            current_block = _llvm_ir_builder.GetInsertBlock();

            llvm::BasicBlock* condition_block = llvm::BasicBlock::Create(*_llvm_context, "while_condition", current_function);
            llvm::BasicBlock* loop_block = llvm::BasicBlock::Create(*_llvm_context, "while_loop", current_function);
            llvm::BasicBlock* after_block = llvm::BasicBlock::Create(*_llvm_context, "while_end", current_function);

            _llvm_ir_builder.CreateBr(condition_block);

            _llvm_ir_builder.SetInsertPoint(condition_block);
            auto condition_label = _llvm_ir_builder.GetInsertBlock();
            auto condition = codegen(node->children[0]);
            if(!condition)
                return nullptr;
            _llvm_ir_builder.CreateCondBr(condition, loop_block, after_block);

            _llvm_ir_builder.SetInsertPoint(loop_block);
            auto loop_code = codegen(node->children[1]);
            if(!loop_code)
                return nullptr;
            _llvm_ir_builder.CreateBr(condition_label);

            _llvm_ir_builder.SetInsertPoint(after_block);
            return llvm::Constant::getNullValue(llvm::Type::getInt32Ty(*_llvm_context));
        }
        case AST::Node::Type::ForStatement: {
            // For Init
            if(!codegen(node->children[0]))
                return nullptr;

            llvm::Function* current_function = _llvm_ir_builder.GetInsertBlock()->getParent();

            llvm::BasicBlock* condition_block = llvm::BasicBlock::Create(*_llvm_context, "for_condition", current_function);
            llvm::BasicBlock* loop_block = llvm::BasicBlock::Create(*_llvm_context, "for_loop", current_function);
            llvm::BasicBlock* after_block = llvm::BasicBlock::Create(*_llvm_context, "for_end", current_function);

            _llvm_ir_builder.CreateBr(condition_block);

            _llvm_ir_builder.SetInsertPoint(condition_block);
            auto condition_label = _llvm_ir_builder.GetInsertBlock();
            auto condition = codegen(node->children[1]);
            if(!condition)
                return nullptr;
            _llvm_ir_builder.CreateCondBr(condition, loop_block, after_block);

            _llvm_ir_builder.SetInsertPoint(loop_block);
            // Loop block
            if(!codegen(node->children[3]))
                return nullptr;
            // Iterator advance
            if(!codegen(node->children[2]))
                return nullptr;
            _llvm_ir_builder.CreateBr(condition_label);

            _llvm_ir_builder.SetInsertPoint(after_block);
            return llvm::Constant::getNullValue(llvm::Type::getInt32Ty(*_llvm_context));
        }
        case AST::Node::Type::IfStatement: {
            auto              current_function = _llvm_ir_builder.GetInsertBlock()->getParent();
            auto              if_then_block = llvm::BasicBlock::Create(*_llvm_context, "if_then", current_function);
            llvm::BasicBlock* if_else_block = nullptr;
            auto              if_end_block = llvm::BasicBlock::Create(*_llvm_context, "if_end", current_function);

            // Condition evaluation and branch
            auto condition = codegen(node->children[0]);
            if(!condition)
                return nullptr;

            if(node->children.size() > 2) {
                if_else_block = llvm::BasicBlock::Create(*_llvm_context, "if_else", current_function);
                _llvm_ir_builder.CreateCondBr(condition, if_then_block, if_else_block);
            } else {
                _llvm_ir_builder.CreateCondBr(condition, if_then_block, if_end_block);
            }

            // Then
            _llvm_ir_builder.SetInsertPoint(if_then_block);
            _generated_return = false;
            auto then_value = codegen(node->children[1]);
            if(!then_value)
                return nullptr;
            if(!_generated_return) {
                _llvm_ir_builder.CreateBr(if_end_block);
                _generated_return = false;
            }

            // Note: The current block may have changed, this doesn't matter right now, but if the
            // if_then_block is reused in the future (to compute a PHI node holding a return value
            // for the if statement for example), we should update it to the current block:
            //   if_then_block = _llvm_ir_builder.GetInsertBlock();

            // Else
            if(if_else_block) {
                _llvm_ir_builder.SetInsertPoint(if_else_block);
                _generated_return = false;
                auto else_value = codegen(node->children[2]);
                if(!else_value)
                    return nullptr;
                if(!_generated_return) {
                    _llvm_ir_builder.CreateBr(if_end_block);
                    _generated_return = false;
                }
            }

            _llvm_ir_builder.SetInsertPoint(if_end_block);

            // If statements do not return a value (Should we?)
            return llvm::Constant::getNullValue(llvm::Type::getInt32Ty(*_llvm_context));
        }
        case AST::Node::Type::ReturnStatement: {
            _generated_return = true;
            if(node->children.empty()) {
                assert(node->type_id == PrimitiveType::Void);
                return _llvm_ir_builder.CreateRetVoid();
            } else {
                return _llvm_ir_builder.CreateRet(codegen(node->children[0]));
            }
        }
        default: warn("LLVM Codegen: Unsupported node type '{}'.\n", node->type);
    }
    return nullptr;
}

llvm::Type* Module::get_llvm_type(TypeID type_id) const {
    auto type = GlobalTypeRegistry::instance().get_type(type_id);
    if(type->is_pointer()) {
        auto llvm_type = get_llvm_type(dynamic_cast<const PointerType*>(type)->pointee_type);
        return llvm_type->getPointerTo(0);
    }
    if(type->is_array()) {
        auto arr_type = dynamic_cast<const ArrayType*>(type);
        auto llvm_type = get_llvm_type(arr_type->element_type);
        return llvm::ArrayType::get(llvm_type, arr_type->capacity);
    }
    if(is_primitive(type_id)) {
        switch(type_id) {
            case Void: return llvm::Type::getVoidTy(*_llvm_context);
            case Char: return llvm::Type::getInt8Ty(*_llvm_context);
            case Boolean: return llvm::Type::getInt1Ty(*_llvm_context);
            case U8: return llvm::Type::getInt8Ty(*_llvm_context);
            case U16: return llvm::Type::getInt16Ty(*_llvm_context);
            case U32: return llvm::Type::getInt32Ty(*_llvm_context);
            case U64: return llvm::Type::getInt64Ty(*_llvm_context);
            case I8: return llvm::Type::getInt8Ty(*_llvm_context);
            case I16: return llvm::Type::getInt16Ty(*_llvm_context);
            case I32: return llvm::Type::getInt32Ty(*_llvm_context);
            case I64: return llvm::Type::getInt64Ty(*_llvm_context);
            case Integer: return llvm::Type::getInt32Ty(*_llvm_context);
            case Pointer: return llvm::Type::getInt64Ty(*_llvm_context); // FIXME ?
            case Float: return llvm::Type::getFloatTy(*_llvm_context);
            case Double: return llvm::Type::getDoubleTy(*_llvm_context);
            case CString: return llvm::Type::getInt8PtrTy(*_llvm_context);
            default: throw(fmt::format("[Module] get_llvm_type: Unhandled primitive type '{}'.", type_id));
        }
    }

    auto structType = llvm::StructType::getTypeByName(*_llvm_context, type->designation);
    if(!structType)
        throw Exception(fmt::format("[LLVMCodegen] Could not find struct with name '{}'.\n", type->designation));
    return structType;
}
