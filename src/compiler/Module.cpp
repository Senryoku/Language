#include "Module.hpp"

#include <vector>

llvm::Constant* Module::codegen(const GenericValue& val) {
    assert(val.is_constexpr());
    switch(val.type) {
        case GenericValue::Type::Float: return llvm::ConstantFP::get(*_llvm_context, llvm::APFloat(val.value.as_float));
        case GenericValue::Type::Integer: return llvm::ConstantInt::get(*_llvm_context, llvm::APInt(32, val.value.as_int32_t));
        case GenericValue::Type::Array: {
            const auto& arr = val.value.as_array;
            auto        itemType = llvm::IntegerType::get(*_llvm_context, 32);
            // FIXME: Determine the item type:
            /*
            switch(arr.type) {
                case GenericValue::Type::Integer:
                    ...
                    break;
                ...
            }
            */
            std::vector<llvm::Constant*> values(arr.capacity);
            for(unsigned int i = 0; i < arr.capacity; i++)
                values[i] = llvm::ConstantInt::get(itemType, arr.items[i].value.as_int32_t); // FIXME: Depends on the item type

            auto arrayType = llvm::ArrayType::get(itemType, values.size());
            auto globalDeclaration = (llvm::GlobalVariable*)_llvm_module->getOrInsertGlobal("", arrayType);
            globalDeclaration->setInitializer(llvm::ConstantArray::get(arrayType, values));
            globalDeclaration->setConstant(true);
            globalDeclaration->setLinkage(llvm::GlobalValue::LinkageTypes::PrivateLinkage);
            globalDeclaration->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
            return llvm::ConstantExpr::getBitCast(globalDeclaration, arrayType->getPointerTo());
        }
        case GenericValue::Type::String: {
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
                    return _llvm_ir_builder.CreateSIToFP(child, llvm::Type::getFloatTy(*_llvm_context), "conv");
                }
                case GenericValue::Type::Integer: {
                    assert(node->children[0]->value.type == GenericValue::Type::Float); // TEMP
                    return _llvm_ir_builder.CreateFPToSI(child, llvm::Type::getInt32Ty(*_llvm_context), "conv");
                }
                default: error("LLVM::Codegen: Cast from {} to {} not supported.", node->children[0]->value.type, node->value.type); return nullptr;
            }
        }
        case AST::Node::Type::FunctionDeclaration: {
            auto function_name = std::string{node->token.value};
            auto prev_function = _llvm_module->getFunction(function_name);
            if(prev_function) {
                error("Redefinition of function '{}' (line {}, already defined on line ??[TODO]).\n", function_name, node->token.line);
                return nullptr;
            }

            auto                     current_block = _llvm_ir_builder.GetInsertBlock();
            std::vector<llvm::Type*> param_types(1, llvm::Type::getInt32Ty(*_llvm_context));                                               // TODO
            auto                     function_types = llvm::FunctionType::get(llvm::Type::getInt32Ty(*_llvm_context), param_types, false); // TODO
            auto                     function = llvm::Function::Create(function_types, llvm::Function::ExternalLinkage, function_name, _llvm_module.get());
            auto*                    block = llvm::BasicBlock::Create(*_llvm_context, "entrypoint", function);
            auto                     arg_idx = 0;
            _llvm_ir_builder.SetInsertPoint(block);
            push_scope(); // Scope for variable declarations
            for(auto& arg : function->args()) {
                const auto& argName = node->children[arg_idx]->token.value;
                arg.setName(std::string{argName});
                auto alloca = codegen(node->children[arg_idx]); // Generate variable declarations
                _llvm_ir_builder.CreateStore(&arg, alloca);
                ++arg_idx;
            }
            codegen(node->children.back()); // Generate function body
            pop_scope();
            // TODO: Correctly handle no return (llvm_ir_builder.CreateRet(RetVal);)
            _llvm_ir_builder.SetInsertPoint(current_block);
            if(verifyFunction(*function)) {
                function->eraseFromParent();
                return nullptr;
            }
            return function;
        }
        case AST::Node::Type::FunctionCall: {
            auto function_name = std::string{node->token.value};
            auto function = _llvm_module->getFunction(function_name);
            if(!function) {
                error("Call to undeclared function '{}' (line {}).\n", function_name, node->token.line);
                return nullptr;
            }
            // TODO: Handle default values.
            // TODO: Handle vargs functions (variable number of parameters, like printf :^) )
            if(function->arg_size() != node->children.size() - 1) {
                error("Unexpected number of parameters in function call '{}' (line {}): Expected {}, got {}.\n", function_name, node->token.line, function->arg_size(),
                      node->children.size() - 1);
                for(auto i = 1u; i < node->children.size(); ++i)
                    print("\tArgument #{}: {}", i, *node->children[i]);
                // return nullptr; // FIXME: Disabled to 'support' vargs, should return an error
            }
            std::vector<llvm::Value*> parameters;
            // Skip the first one, it (will) holds the function name (FIXME: No used yet, we don't support function as result of expression yet)
            for(auto i = 1u; i < node->children.size(); ++i) {
                auto v = codegen(node->children[i]);
                if(!v)
                    return nullptr;
                parameters.push_back(v);
            }
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
                default: warn("LLVM Codegen: Unsupported variable type '{}'.\n", node->value.type); break;
            }
            if(!set(node->token.value, ret)) {
                error("Variable '{}' already declared (line {}).\n", node->token.value, node->token.line);
                return nullptr;
            }
            return ret;
        }
        case AST::Node::Type::Variable: {
            auto var = get(node->token.value);
            if(!var) {
                error("LLVM Codegen: Undeclared variable '{}'.\n", node->token.value);
                return nullptr;
            }
            return _llvm_ir_builder.CreateLoad(var->getAllocatedType(), var, std::string{node->token.value}.c_str());
        }
        case AST::Node::Type::BinaryOperator: {
            auto lhs = codegen(node->children[0]);
            auto rhs = codegen(node->children[1]);
            if(!lhs || !rhs)
                return nullptr;
            switch(node->token.type) {
                case Tokenizer::Token::Type::Addition: {
                    switch(node->value.type) {
                        case GenericValue::Type::Integer: return _llvm_ir_builder.CreateAdd(lhs, rhs, "addtmp");
                        case GenericValue::Type::Float: {
                            // TODO: Temp, tidy up.
                            if(lhs->getType() == llvm::Type::getInt32Ty(*_llvm_context)) {
                                assert(rhs->getType() != llvm::Type::getInt32Ty(*_llvm_context));
                                lhs = _llvm_ir_builder.CreateSIToFP(lhs, llvm::Type::getFloatTy(*_llvm_context), "conv");
                            } else if(rhs->getType() == llvm::Type::getInt32Ty(*_llvm_context)) {
                                assert(lhs->getType() != llvm::Type::getInt32Ty(*_llvm_context));
                                rhs = _llvm_ir_builder.CreateSIToFP(rhs, llvm::Type::getFloatTy(*_llvm_context), "conv");
                            }
                            return _llvm_ir_builder.CreateFAdd(lhs, rhs, "addftmp");
                        }
                        default: error("LLVM::Codegen: Binary operator '{}' does not support type '{}'.", node->token.value, node->value.type); return nullptr;
                    }
                }
                case Tokenizer::Token::Type::Substraction: {
                    if(node->value.type == GenericValue::Type::Integer)
                        return _llvm_ir_builder.CreateSub(lhs, rhs, "subtmp");
                    else
                        return _llvm_ir_builder.CreateFSub(lhs, rhs, "subftmp");
                }
                case Tokenizer::Token::Type::Multiplication: {
                    switch(node->value.type) {
                        case GenericValue::Type::Integer: return _llvm_ir_builder.CreateMul(lhs, rhs, "multmp");
                        case GenericValue::Type::Float: {
                            // TODO: Temp, tidy up.
                            if(lhs->getType() == llvm::Type::getInt32Ty(*_llvm_context)) {
                                assert(rhs->getType() != llvm::Type::getInt32Ty(*_llvm_context));
                                lhs = _llvm_ir_builder.CreateSIToFP(lhs, llvm::Type::getFloatTy(*_llvm_context), "conv");
                            } else if(rhs->getType() == llvm::Type::getInt32Ty(*_llvm_context)) {
                                assert(lhs->getType() != llvm::Type::getInt32Ty(*_llvm_context));
                                rhs = _llvm_ir_builder.CreateSIToFP(rhs, llvm::Type::getFloatTy(*_llvm_context), "conv");
                            }
                            return _llvm_ir_builder.CreateFMul(lhs, rhs, "mulftmp");
                        }
                        default: error("LLVM::Codegen: Binary operator '{}' does not support type '{}'.", node->token.value, node->value.type); return nullptr;
                    }
                }
                case Tokenizer::Token::Type::Division: {
                    auto div = _llvm_ir_builder.CreateFDiv(lhs, rhs, "divftmp");
                    if(node->value.type == GenericValue::Type::Integer)
                        return _llvm_ir_builder.CreateFPToUI(div, llvm::Type::getInt32Ty(*_llvm_context), "intcasttmp");
                    else
                        return div;
                }
                case Tokenizer::Token::Type::Lesser: {
                    // TODO: More types
                    if(node->children[0]->value.type == GenericValue::Type::Integer && node->children[1]->value.type == GenericValue::Type::Integer)
                        return _llvm_ir_builder.CreateCmp(llvm::CmpInst::Predicate::ICMP_SLT, lhs, rhs, "ICMP_SLT");
                    else if(node->children[0]->value.type == GenericValue::Type::Float && node->children[1]->value.type == GenericValue::Type::Float)
                        return _llvm_ir_builder.CreateFCmp(llvm::CmpInst::Predicate::FCMP_OLT, lhs, rhs, "FCMP_OLT");
                }
                case Tokenizer::Token::Type::Greater: {
                    // TODO: More types
                    if(node->children[0]->value.type == GenericValue::Type::Integer && node->children[1]->value.type == GenericValue::Type::Integer)
                        return _llvm_ir_builder.CreateCmp(llvm::CmpInst::Predicate::ICMP_SGT, lhs, rhs, "ICMP_SGT");
                    else if(node->children[0]->value.type == GenericValue::Type::Float && node->children[1]->value.type == GenericValue::Type::Float)
                        return _llvm_ir_builder.CreateFCmp(llvm::CmpInst::Predicate::FCMP_OGT, lhs, rhs, "FCMP_OGT");
                }
                case Tokenizer::Token::Type::Assignment: {
                    assert(node->children[0]->type == AST::Node::Type::Variable); // FIXME
                    auto variable = get(node->children[0]->token.value);
                    if(!variable) {
                        error("LLVM Codegen: Undeclared variable '{}'.\n", node->children[0]->token.value);
                        return nullptr;
                    }
                    // FIXME: Define rules for automatic conversion and implement them. (But not here?)
                    if(variable->getAllocatedType() != rhs->getType()) {
                        std::string              rhs_type_str, lhs_type_str;
                        llvm::raw_string_ostream rhs_type_rso(rhs_type_str), lhs_type_rso(lhs_type_str);
                        rhs->getType()->print(rhs_type_rso);
                        variable->getAllocatedType()->print(lhs_type_rso);
                        error("LLVM::Codegen: No automatic conversion from {} to {} .\n", rhs_type_rso.str(), lhs_type_rso.str());
                    }
                    _llvm_ir_builder.CreateStore(rhs, variable);
                    return variable;
                }
                default: warn("LLVM Codegen: Unimplemented Binary Operator '{}'.\n", node->token.value); break;
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
        case AST::Node::Type::ReturnStatement: {
            auto val = codegen(node->children[0]);
            _generated_return = true;
            return _llvm_ir_builder.CreateRet(val);
        }
        default: warn("LLVM Codegen: Unsupported node type '{}'.\n", node->type);
    }
    return nullptr;
}
