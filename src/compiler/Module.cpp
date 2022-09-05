#include "Module.hpp"

#include <vector>

llvm::Value* Module::codegen(const GenericValue& val) {
    switch(val.type) {
        case GenericValue::Type::Float: return llvm::ConstantFP::get(*_llvm_context, llvm::APFloat(val.value.as_float));
        case GenericValue::Type::Integer: return llvm::ConstantInt::get(*_llvm_context, llvm::APInt(32, val.value.as_int32_t));
        case GenericValue::Type::String: {
            // TEMP: Constant String.
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
            auto globalDeclaration = (llvm::GlobalVariable*)_llvm_module->getOrInsertGlobal(".str", stringType);
            globalDeclaration->setInitializer(llvm::ConstantArray::get(stringType, chars));
            globalDeclaration->setConstant(true);
            globalDeclaration->setLinkage(llvm::GlobalValue::LinkageTypes::PrivateLinkage);
            globalDeclaration->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);

            // 4. Return a cast to an i8*
            return llvm::ConstantExpr::getBitCast(globalDeclaration, charType->getPointerTo());
        }
        default: warn("LLVM Codegen: Unsupported value type '{}'.\n", val.type);
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
            if(child)
                return nullptr;
            switch(node->value.type) {
                case GenericValue::Type::Float: return _llvm_ir_builder.CreateSIToFP(child, llvm::Type::getFloatTy(*_llvm_context), "conv");
                default: error("LLVM::Codegen: Cast to {} not supported.", node->value.type); return nullptr;
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
            if(function->arg_size() != node->children.size() - 1) {
                error("Unexpected number of parameters in function call '{}' (line {}): Expected {}, got {}.\n", function_name, node->token.line, function->arg_size(),
                      node->children.size() - 1);
                for(auto i = 1u; i < node->children.size(); ++i)
                    print("\tArgument #{}: {}\n", i, *node->children[i]);
                return nullptr;
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
            if(node->token.value == "+") {
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
            } else if(node->token.value == "-") {
                if(node->value.type == GenericValue::Type::Integer)
                    return _llvm_ir_builder.CreateSub(lhs, rhs, "subtmp");
                else
                    return _llvm_ir_builder.CreateFSub(lhs, rhs, "subftmp");
            } else if(node->token.value == "*") {
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
            } else if(node->token.value == "/") {
                auto div = _llvm_ir_builder.CreateFDiv(lhs, rhs, "divftmp");
                if(node->value.type == GenericValue::Type::Integer)
                    return _llvm_ir_builder.CreateFPToUI(div, llvm::Type::getInt32Ty(*_llvm_context), "intcasttmp");
                else
                    return div;
                // Boolean Operators
            } else if(node->token.value == "<") {
                // TODO: More types
                if(node->children[0]->value.type == GenericValue::Type::Integer && node->children[1]->value.type == GenericValue::Type::Integer)
                    return _llvm_ir_builder.CreateCmp(llvm::CmpInst::Predicate::ICMP_SLT, lhs, rhs, "ICMP_SLT");
                else if(node->children[0]->value.type == GenericValue::Type::Float && node->children[1]->value.type == GenericValue::Type::Float)
                    return _llvm_ir_builder.CreateFCmp(llvm::CmpInst::Predicate::FCMP_OLT, lhs, rhs, "FCMP_OLT");
            } else if(node->token.value == ">") {
                // TODO: More types
                if(node->children[0]->value.type == GenericValue::Type::Integer && node->children[1]->value.type == GenericValue::Type::Integer)
                    return _llvm_ir_builder.CreateCmp(llvm::CmpInst::Predicate::ICMP_SGT, lhs, rhs, "ICMP_SGT");
                else if(node->children[0]->value.type == GenericValue::Type::Float && node->children[1]->value.type == GenericValue::Type::Float)
                    return _llvm_ir_builder.CreateFCmp(llvm::CmpInst::Predicate::FCMP_OGT, lhs, rhs, "FCMP_OGT");
            } else if(node->token.value == "=") {
                assert(node->children[0]->type == AST::Node::Type::Variable); // FIXME
                auto variable = get(node->children[0]->token.value);
                // FIXME
                if(variable->getAllocatedType() != rhs->getType()) {
                    if(variable->getAllocatedType() == llvm::Type::getInt32Ty(*_llvm_context) && rhs->getType() == llvm::Type::getFloatTy(*_llvm_context)) {
                        rhs = _llvm_ir_builder.CreateFPToSI(rhs, llvm::Type::getFloatTy(*_llvm_context), "conv");
                    } else {
                        error("LLVM::Codegen: No automatic conversion from ... to ... .\n");
                    }
                }
                _llvm_ir_builder.CreateStore(rhs, variable);
                return rhs;
            }
            warn("LLVM Code: Unimplemented Binary Operator '{}'.\n", node->token.value);
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
