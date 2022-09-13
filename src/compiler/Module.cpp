#include "Module.hpp"

#include <vector>

// FIXME: We should not use maps here actually. Find another way to manage the nested switch cases.

#define OP(VALUETYPE, FUNC)                                                                         \
    {                                                                                               \
        GenericValue::Type::VALUETYPE, [](llvm::IRBuilder<>& ir_builder, llvm::Value* val) { FUNC } \
    }
static std::unordered_map<Token::Type, std::unordered_map<GenericValue::Type, std::function<llvm::Value*(llvm::IRBuilder<>&, llvm::Value*)>>> unary_ops = {
    {Token::Type::Addition, {OP(Integer, (void)ir_builder; return val;), OP(Float, (void)ir_builder; return val;)}},
    {Token::Type::Substraction, {OP(Integer, return ir_builder.CreateNeg(val, "neg");), OP(Float, return ir_builder.CreateFNeg(val, "fneg");)}},
};
#undef OP

#define OP(VALUETYPE, FUNC)                                                                                           \
    {                                                                                                                 \
        GenericValue::Type::VALUETYPE, [](llvm::IRBuilder<>& ir_builder, llvm::Value* lhs, llvm::Value* rhs) { FUNC } \
    }

static std::unordered_map<Token::Type, std::unordered_map<GenericValue::Type, std::function<llvm::Value*(llvm::IRBuilder<>&, llvm::Value*, llvm::Value*)>>> binary_ops = {
    {Token::Type::Addition, {OP(Integer, return ir_builder.CreateAdd(lhs, rhs, "add");), OP(Float, return ir_builder.CreateFAdd(lhs, rhs, "fadd");)}},
    {Token::Type::Substraction, {OP(Integer, return ir_builder.CreateSub(lhs, rhs, "sub");), OP(Float, return ir_builder.CreateFSub(lhs, rhs, "fsub");)}},
    {Token::Type::Multiplication, {OP(Integer, return ir_builder.CreateMul(lhs, rhs, "mul");), OP(Float, return ir_builder.CreateFMul(lhs, rhs, "fmul");)}},
    {Token::Type::Division, {OP(Integer, return ir_builder.CreateSDiv(lhs, rhs, "div");), OP(Float, return ir_builder.CreateFDiv(lhs, rhs, "fdiv");)}},
    {Token::Type::Modulus, {OP(Integer, return ir_builder.CreateSRem(lhs, rhs, "srem");)}},
    // Comparisons
    {Token::Type::Equal,
     {OP(Integer, return ir_builder.CreateCmp(llvm::CmpInst::Predicate::ICMP_EQ, lhs, rhs, "ICMP_EQ");),
      OP(Float, return ir_builder.CreateFCmp(llvm::CmpInst::Predicate::FCMP_OEQ, lhs, rhs, "FCMP_OEQ");)}},
    {Token::Type::Lesser,
     {OP(Integer, return ir_builder.CreateCmp(llvm::CmpInst::Predicate::ICMP_SLT, lhs, rhs, "ICMP_SLT");),
      OP(Float, return ir_builder.CreateFCmp(llvm::CmpInst::Predicate::FCMP_OLT, lhs, rhs, "FCMP_OLT");)}},
    {Token::Type::LesserOrEqual,
     {OP(Integer, return ir_builder.CreateCmp(llvm::CmpInst::Predicate::ICMP_SLE, lhs, rhs, "ICMP_SLE");),
      OP(Float, return ir_builder.CreateFCmp(llvm::CmpInst::Predicate::FCMP_OLE, lhs, rhs, "FCMP_OLE");)}},
    {Token::Type::Greater,
     {OP(Integer, return ir_builder.CreateCmp(llvm::CmpInst::Predicate::ICMP_SGT, lhs, rhs, "ICMP_SGT");),
      OP(Float, return ir_builder.CreateFCmp(llvm::CmpInst::Predicate::FCMP_OGT, lhs, rhs, "FCMP_OGT");)}},
    {Token::Type::GreaterOrEqual,
     {OP(Integer, return ir_builder.CreateCmp(llvm::CmpInst::Predicate::ICMP_SGE, lhs, rhs, "ICMP_SGE");),
      OP(Float, return ir_builder.CreateFCmp(llvm::CmpInst::Predicate::FCMP_OGE, lhs, rhs, "FCMP_OGE");)}},
};
#undef OP

llvm::Constant* Module::codegen(const GenericValue& val) {
    assert(val.is_constexpr());
    switch(val.type) {
        case GenericValue::Type::Boolean: return llvm::ConstantInt::get(*_llvm_context, llvm::APInt(1, val.value.as_bool));
        case GenericValue::Type::Char: return llvm::ConstantInt::get(*_llvm_context, llvm::APInt(8, val.value.as_char));
        case GenericValue::Type::Float: return llvm::ConstantFP::get(*_llvm_context, llvm::APFloat(val.value.as_float));
        case GenericValue::Type::Integer: return llvm::ConstantInt::get(*_llvm_context, llvm::APInt(32, val.value.as_int32_t));
        case GenericValue::Type::Array: {
            const auto&                  arr = val.value.as_array;
            auto                         itemType = get_llvm_type(arr.type);
            std::vector<llvm::Constant*> values(arr.capacity);
            for(unsigned int i = 0; i < arr.capacity; i++)
                values[i] = codegen(arr.items[i]);

            auto arrayType = llvm::ArrayType::get(itemType, values.size());
            auto globalDeclaration = (llvm::GlobalVariable*)_llvm_module->getOrInsertGlobal("", arrayType);
            globalDeclaration->setInitializer(llvm::ConstantArray::get(arrayType, values));
            globalDeclaration->setConstant(true);
            globalDeclaration->setLinkage(llvm::GlobalValue::LinkageTypes::PrivateLinkage);
            globalDeclaration->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
            return llvm::ConstantExpr::getBitCast(globalDeclaration, arrayType->getPointerTo());
        }
        case GenericValue::Type::String: {
            // FIXME: Take a look at llvm::StringRef and llvm::Twine
            const auto& str = val.value.as_string;
            auto        charType = llvm::IntegerType::get(*_llvm_context, 8);

            // 1. Initialize chars vector
            std::vector<llvm::Constant*> chars(str.size);
            for(unsigned int i = 0; i < str.size; i++)
                chars[i] = llvm::ConstantInt::get(charType, *(str.begin + i));

            // 1b. add a zero terminator too
            // FIXME: Ultimately string will have a built-in length, should we keep the terminating null byte anyway?
            chars.push_back(llvm::ConstantInt::get(charType, 0));

            // 2. Initialize the string from the characters
            auto stringType = llvm::ArrayType::get(charType, chars.size());

            // 3. Create the declaration statement
            auto globalDeclaration = (llvm::GlobalVariable*)_llvm_module->getOrInsertGlobal(val.value.as_string.to_std_string_view().data(), stringType);
            globalDeclaration->setInitializer(llvm::ConstantArray::get(stringType, chars));
            globalDeclaration->setConstant(true);
            globalDeclaration->setLinkage(llvm::GlobalValue::LinkageTypes::PrivateLinkage);
            globalDeclaration->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);

            // 4. Return a cast to an i8*
            return llvm::ConstantExpr::getBitCast(globalDeclaration, charType->getPointerTo());
        }
        default: warn("LLVM Codegen: Unsupported constant value type '{}'.\n", val.type);
    }
    return nullptr;
}

llvm::Value* Module::codegen(const AST::Node* node) {
    _generated_return = false;
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
        case AST::Node::Type::ConstantValue: return codegen(node->value);
        case AST::Node::Type::Cast: {
            assert(node->children.size() == 1);
            auto&& child = codegen(node->children[0]);
            if(!child)
                return nullptr;
            // TODO: Handle child's type.
            switch(node->value.type) {
                case GenericValue::Type::Float: {
                    assert(node->children[0]->value.type == GenericValue::Type::Integer); // TEMP
                    return _llvm_ir_builder.CreateSIToFP(child, llvm::Type::getFloatTy(*_llvm_context), "castSIToFP");
                }
                case GenericValue::Type::Integer: {
                    assert(node->children[0]->value.type == GenericValue::Type::Float); // TEMP
                    return _llvm_ir_builder.CreateFPToSI(child, llvm::Type::getInt32Ty(*_llvm_context), "castFPToSI");
                }
                default: error("[LLVMCodegen] LLVM::Codegen: Cast from {} to {} not supported.", node->children[0]->value.type, node->value.type); return nullptr;
            }
        }
        case AST::Node::Type::TypeDeclaration: {
            std::vector<llvm::Type*> members;
            for(const auto c : node->children)
                members.push_back(get_llvm_type(c->value.type));
            std::string type_name(node->token.value);
            auto        structType = llvm::StructType::create(*_llvm_context, members, type_name);
            break;
        }
        case AST::Node::Type::FunctionDeclaration: {
            auto function_name = std::string{node->token.value};
            auto prev_function = _llvm_module->getFunction(function_name);
            if(prev_function) { // Should be handled by the parser.
                error("Redefinition of function '{}' (line {}).\n", function_name, node->token.line);
                return nullptr;
            }

            auto                     current_block = _llvm_ir_builder.GetInsertBlock();
            std::vector<llvm::Type*> param_types;
            if(node->children.size() > 1)
                for(auto i = 0; i < node->children.size() - 1; ++i)
                    param_types.push_back(get_llvm_type(node->children[i]));
            auto return_type = get_llvm_type(node->value.type);
            auto function_types = llvm::FunctionType::get(return_type, param_types, false);
            auto flags = node->value.value.as_int32_t;
            auto function = llvm::Function::Create(function_types, flags & AST::Node::FunctionFlag::Exported ? llvm::Function::ExternalLinkage : llvm::Function::PrivateLinkage,
                                                   function_name, _llvm_module.get());

            auto* block = llvm::BasicBlock::Create(*_llvm_context, "entrypoint", function);
            _llvm_ir_builder.SetInsertPoint(block);

            push_scope(); // Scope for variable declarations
            auto arg_idx = 0;
            for(auto& arg : function->args()) {
                arg.setName(std::string{node->children[arg_idx]->token.value});
                auto alloca = codegen(node->children[arg_idx]); // Generate variable declarations
                _llvm_ir_builder.CreateStore(&arg, alloca);
                ++arg_idx;
            }
            auto function_body = codegen(node->children.back()); // Generate function body
            if(!_generated_return)
                _llvm_ir_builder.CreateRet(node->value.type == GenericValue::Type::Void ? nullptr : function_body);
            pop_scope();

            // TODO: Correctly handle no return (llvm_ir_builder.CreateRet(RetVal);)
            _llvm_ir_builder.SetInsertPoint(current_block);
            if(verifyFunction(*function, &llvm::errs())) {
                error("\n[LLVMCodegen] Error verifying function '{}'.\n", function_name);
                function->eraseFromParent();
                return nullptr;
            }
            return function;
        }
        case AST::Node::Type::FunctionCall: {
            auto function_name = std::string{node->token.value};
            auto function = _llvm_module->getFunction(function_name);
            if(!function) {
                error("[LLVMCodegen] Call to undeclared function '{}' (line {}).\n", function_name, node->token.line);
                return nullptr;
            }
            // TODO: Handle default values.
            // TODO: Handle vargs functions (variable number of parameters, like printf :^) )
            auto function_flags = node->value.value.as_int32_t;
            if(!(function_flags & AST::Node::FunctionFlag::Variadic) && function->arg_size() != node->children.size() - 1) {
                error("[LLVMCodegen] Unexpected number of parameters in function call '{}' (line {}): Expected {}, got {}.\n", function_name, node->token.line,
                      function->arg_size(), node->children.size() - 1);
                for(auto i = 1u; i < node->children.size(); ++i)
                    print("\tArgument #{}: {}", i, *node->children[i]);
                return nullptr;
            }
            std::vector<llvm::Value*> parameters;
            // Skip the first one, it (will) holds the function name (FIXME: No used yet, we don't support function as result of expression yet)
            for(auto i = 1u; i < node->children.size(); ++i) {
                auto v = codegen(node->children[i]);
                if(!v)
                    return nullptr;
                // C Variadic functions promotes float to double (see https://stackoverflow.com/questions/63144506/printf-doesnt-work-for-floats-in-llvm-ir)
                if(function_flags & AST::Node::FunctionFlag::Variadic && v->getType()->isFloatTy())
                    v = _llvm_ir_builder.CreateFPExt(v, llvm::Type::getDoubleTy(*_llvm_context));
                parameters.push_back(v);
            }
            if(node->value.type == GenericValue::Type::Void)
                return _llvm_ir_builder.CreateCall(function, parameters);
            else
                return _llvm_ir_builder.CreateCall(function, parameters, function_name);
        }
        case AST::Node::Type::VariableDeclaration: {
            llvm::AllocaInst* ret = nullptr;
            auto              parent_function = _llvm_ir_builder.GetInsertBlock()->getParent();
            if(!parent_function)
                warn("TODO: Correctly handle global variables! ({}:{})\n", __FILE__, __LINE__);
            switch(node->value.type) {
                case GenericValue::Type::Float: ret = create_entry_block_alloca(parent_function, llvm::Type::getFloatTy(*_llvm_context), std::string{node->token.value}); break;
                case GenericValue::Type::Integer: ret = create_entry_block_alloca(parent_function, llvm::Type::getInt32Ty(*_llvm_context), std::string{node->token.value}); break;
                case GenericValue::Type::Array: {
                    // FIXME: Handle more types.
                    auto arrayType = llvm::ArrayType::get(llvm::IntegerType::get(*_llvm_context, 32), node->value.value.as_array.capacity);
                    ret = create_entry_block_alloca(parent_function, arrayType, std::string{node->token.value});
                    break;
                }
                case GenericValue::Type::String: {
                    // FIXME: Change this to a struct with size
                    auto charType = llvm::IntegerType::get(*_llvm_context, 8);
                    auto stringType = llvm::PointerType::get(charType, 0);
                    ret = create_entry_block_alloca(parent_function, stringType, std::string{node->token.value});
                    break;
                }
                case GenericValue::Type::Composite: {
                    std::string type_name(node->value.value.as_composite.type_name.to_std_string_view());
                    auto        structType = llvm::StructType::getTypeByName(*_llvm_context, type_name);
                    if(!structType) {
                        error("[LLVMCodegen] Type {} not found.\n", type_name);
                        return nullptr;
                    }
                    ret = create_entry_block_alloca(parent_function, structType, std::string{node->token.value});
                    break;
                }
                default: warn("[LLVMCodegen] Unsupported variable type '{}'.\n", node->value.type); break;
            }
            if(!set(node->token.value, ret)) {
                error("[LLVMCodegen] Variable '{}' already declared (line {}).\n", node->token.value, node->token.line);
                return nullptr;
            }
            return ret;
        }
        case AST::Node::Type::Variable: {
            // FIXME: Quick hack to get a potential 'this'.
            if(node->token.value == "this") {
                auto scope_it = _scopes.rbegin();
                while(scope_it != _scopes.rend() && scope_it->empty())
                    scope_it++;
                return (scope_it == _scopes.rend() || scope_it->empty()) ? nullptr : scope_it->begin()->second;
            }

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
            if(child->type == AST::Node::Type::BinaryOperator && child->token.type == Token::Type::MemberAccess) {
                assert(value->getType()->isPointerTy());
                return _llvm_ir_builder.CreateLoad(get_llvm_type(node->value.type), value, "l-to-rvalue");
            }
            return value;
        }
        case AST::Node::Type::UnaryOperator: {
            auto val = codegen(node->children[0]);
            switch(node->token.type) {
                case Token::Type::Increment: {
                    assert(node->children[0]->type == AST::Node::Type::Variable);
                    // FIXME: Doesn't work
                    auto var = get(node->children[0]->token.value);
                    _llvm_ir_builder.CreateAdd(val, _llvm_ir_builder.getInt32(1), "inc");
                    _llvm_ir_builder.CreateStore(val, var);
                    return var;
                }
                default: {
                    if(unary_ops[node->token.type].find(node->children[0]->value.type) == unary_ops[node->token.type].end()) {
                        error("[LLVMCodegen] Unsupported type {} for unary operator {}.\n", node->children[0]->value.type, node->token.type);
                        return nullptr;
                    }
                    return unary_ops[node->token.type][node->children[0]->value.type](_llvm_ir_builder, val);
                }
            }
        }
        case AST::Node::Type::MemberIdentifier: {
            return llvm::ConstantInt::get(*_llvm_context, llvm::APInt(32, node->value.value.as_int32_t)); // Returns member index
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
                case Token::Type::Lesser: [[fallthrough]];
                case Token::Type::LesserOrEqual: [[fallthrough]];
                case Token::Type::Greater: [[fallthrough]];
                case Token::Type::GreaterOrEqual: {
                    assert(node->children[0]->value.type == node->children[1]->value.type);
                    if(binary_ops[node->token.type].find(node->children[0]->value.type) == binary_ops[node->token.type].end()) {
                        error("[LLVMCodegen] Unsupported types {} and {} for binary operator {}.\n", node->children[0]->value.type, node->children[1]->value.type,
                              node->token.type);
                        return nullptr;
                    }
                    return binary_ops[node->token.type][node->children[0]->value.type](_llvm_ir_builder, lhs, rhs);
                }
                case Token::Type::And: {
                    return _llvm_ir_builder.CreateAnd(lhs, rhs, "and");
                }
                case Token::Type::OpenSubscript: {
                    // FIXME: Remove these checks
                    assert(node->children[0]->type == AST::Node::Type::Variable);
                    assert(node->children[0]->value.type == GenericValue::Type::Array);
                    auto element_type = node->children[0]->value.value.as_array.type;
                    return _llvm_ir_builder.CreateGEP(get_llvm_type(element_type), lhs, {llvm::ConstantInt::get(*_llvm_context, llvm::APInt(32, 0)), rhs},
                                                      "ArrayGEP"); // FIXME: I don't know.
                }
                case Token::Type::Assignment: {
                    _llvm_ir_builder.CreateStore(rhs, lhs);
                    return lhs;
                }
                case Token::Type::MemberAccess: {
                    auto allocaInst = static_cast<llvm::AllocaInst*>(lhs);
                    return _llvm_ir_builder.CreateGEP(allocaInst->getAllocatedType(), lhs, {llvm::ConstantInt::get(*_llvm_context, llvm::APInt(32, 0)), rhs}, "memberptr");
                }
                default: warn("[LLVMCodegen] Unimplemented Binary Operator '{}'.\n", node->token.value); break;
            }
            break;
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
        case AST::Node::Type::IfStatement: {
            auto current_function = _llvm_ir_builder.GetInsertBlock()->getParent();
            auto if_then_block = llvm::BasicBlock::Create(*_llvm_context, "if_then", current_function);
            auto if_else_block = llvm::BasicBlock::Create(*_llvm_context, "if_else");
            auto if_end_block = llvm::BasicBlock::Create(*_llvm_context, "if_end");

            // Condition evaluation and branch
            auto condition = codegen(node->children[0]);
            if(!condition)
                return nullptr;
            _llvm_ir_builder.CreateCondBr(condition, if_then_block, if_else_block);

            // Then
            _llvm_ir_builder.SetInsertPoint(if_then_block);
            auto then_value = codegen(node->children[1]);
            if(!then_value)
                return nullptr;
            if(!_generated_return)
                _llvm_ir_builder.CreateBr(if_end_block);

            // Note: The current block may have changed, this doesn't matter right now, but if the
            // if_then_block is reused in the future (to compute a PHI node holding a return value
            // for the if statement for example), we should update it to the current block:
            //   if_then_block = _llvm_ir_builder.GetInsertBlock();

            // Else
            current_function->getBasicBlockList().push_back(if_else_block);
            _llvm_ir_builder.SetInsertPoint(if_else_block);
            if(node->children.size() > 2) {
                auto else_value = codegen(node->children[2]);
                if(!else_value)
                    return nullptr;
            }
            if(!_generated_return)
                _llvm_ir_builder.CreateBr(if_end_block);

            current_function->getBasicBlockList().push_back(if_end_block);
            _llvm_ir_builder.SetInsertPoint(if_end_block);

            // If statement do not return a value (Should we?)
            return llvm::Constant::getNullValue(llvm::Type::getInt32Ty(*_llvm_context));
        }
        case AST::Node::Type::ReturnStatement: {
            auto val = codegen(node->children[0]);
            _generated_return = true;
            return _llvm_ir_builder.CreateRet(val);
        }
        default: warn("LLVM Codegen: Unsupported node type '{}'.\n", node->type);
    }
    return nullptr;
}
