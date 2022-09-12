#pragma once

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Verifier.h>

#include <het_unordered_map.hpp>

#include <AST.hpp>

class Module {
  public:
    using Scope = het_unordered_map<llvm::AllocaInst*>;

    Module(const std::string& name, llvm::LLVMContext* context) : _llvm_context(context), _llvm_module(new llvm::Module{name, *context}), _llvm_ir_builder{*context} {
        // Creates an implicit main function
        //   Maybe we should turn this into an implicit "__init" function, for globals and module initialisation, and allow the declaration of an actual main function?
        //   One consideration is: How to handle repl and most importantly the return type?
        // std::vector<llvm::Type*> main_param_types; //(1, llvm::Type::getInt32Ty(*_llvm_context));
        // auto                     main_return_type = llvm::FunctionType::get(llvm::Type::getInt32Ty(*_llvm_context), main_param_types, false); // Returns void right now
        // auto                     main_function = llvm::Function::Create(main_return_type, llvm::Function::ExternalLinkage, "main", _llvm_module.get());
        // auto*                    mainblock = llvm::BasicBlock::Create(*_llvm_context, "entrypoint", main_function);
        //_llvm_ir_builder.SetInsertPoint(mainblock);
    }

    Scope& push_scope() {
        _scopes.push_back(Scope{});
        return get_scope();
    }
    inline void pop_scope() { _scopes.pop_back(); }

    inline llvm::Module&                 get_llvm_module() { return *_llvm_module; }
    inline const llvm::Module&           get_llvm_module() const { return *_llvm_module; }
    inline std::unique_ptr<llvm::Module> get_llvm_module_ptr() { return std::move(_llvm_module); }

    bool set(const std::string_view& name, llvm::AllocaInst* alloca) {
        if(get_scope().find(name) != get_scope().end()) {
            return false;
        }
        get_scope().emplace(name, alloca);
        return true;
    }

    llvm::AllocaInst* get(const std::string_view& name) {
        auto scope_it = _scopes.rbegin();
        auto alloca_it = scope_it->find(name);
        while(scope_it != _scopes.rend() && (alloca_it = scope_it->find(name)) == scope_it->end())
            scope_it++;
        return scope_it != _scopes.rend() && alloca_it != scope_it->end() ? alloca_it->second : nullptr;
    }

    const llvm::AllocaInst* get(const std::string_view& name) const {
        auto scope_it = _scopes.rbegin();
        auto alloca_it = scope_it->find(name);
        while(scope_it != _scopes.rend() && (alloca_it = scope_it->find(name)) == scope_it->end())
            scope_it++;
        return scope_it != _scopes.rend() && alloca_it != scope_it->end() ? alloca_it->second : nullptr;
    }

    bool is_declared(const std::string_view& name) const { return get(name) != nullptr; }

    llvm::AllocaInst* create_entry_block_alloca(llvm::Function* func, auto type, const std::string& name) {
        if(func) {
            llvm::IRBuilder<> tmp_builder(&func->getEntryBlock(), func->getEntryBlock().begin());
            return tmp_builder.CreateAlloca(type, 0, name.c_str());
        } else
            return _llvm_ir_builder.CreateAlloca(type, 0, name.c_str());
    }

    // Create declarations for imported external functions/variables
    void codegen_imports(const std::vector<AST::Node*>& nodes) {
        for(const auto& n : nodes) {
            assert(n->type == AST::Node::Type::FunctionDeclaration);
            auto                     name = n->token.value.data();
            auto                     return_type = get_llvm_type(n->value.type);
            std::vector<llvm::Type*> func_args_types;
            // These function nodes should not have bodies
            for(const auto& c : n->children) {
                assert(c->type == AST::Node::Type::VariableDeclaration);
                func_args_types.push_back(get_llvm_type(c->value.type));
            }
            llvm::FunctionType* func_type = llvm::FunctionType::get(return_type, func_args_types, true);
            auto                func_func = _llvm_module->getOrInsertFunction(name, func_type);
        }
    }

    llvm::Value* codegen(const AST& ast) {
        // TEMP: "Standard Library"
        // Make libc printf available
        std::vector<llvm::Type*> printf_args_types({llvm::Type::getInt8PtrTy(*_llvm_context)});
        llvm::FunctionType*      printf_type = llvm::FunctionType::get(llvm::Type::getInt32Ty(*_llvm_context), printf_args_types, true);
        auto                     printf_func = _llvm_module->getOrInsertFunction("printf", printf_type);

        // Actual codegen
        auto r = codegen(&ast.getRoot());
        // Add a return to our generated main (from constructor) if needed (FIXME?)
        // if(!_generated_return)
        //    _llvm_ir_builder.CreateRet(llvm::ConstantInt::get(*_llvm_context, llvm::APInt(32, 0)));
        return r;
    }

  private:
    std::vector<Scope>            _scopes{Scope{}};
    llvm::LLVMContext*            _llvm_context;
    std::unique_ptr<llvm::Module> _llvm_module;
    llvm::IRBuilder<>             _llvm_ir_builder;

    bool _generated_return = false; // Tracks if the last node generated a return statement (FIXME: Remove?)

    Scope&       get_scope() { return _scopes.back(); }
    const Scope& get_scope() const { return _scopes.back(); }

    llvm::Constant* codegen(const GenericValue& val);
    llvm::Value*    codegen(const AST::Node* node);

    llvm::Type* get_llvm_type(GenericValue::Type type) const {
        switch(type) {
            case GenericValue::Type::Integer: return llvm::Type::getInt32Ty(*_llvm_context);
            case GenericValue::Type::Float: return llvm::Type::getFloatTy(*_llvm_context);
            default: error("[Module::get_llvm_type] GenericValue Type '{}' not mapped to a LLVM Type.\n", type); assert(false);
        }
        return nullptr;
    }

    llvm::Type* get_llvm_type(const AST::Node* node) const {
        switch(node->value.type) {
            case GenericValue::Type::Composite: {
                std::string type_name(node->value.value.as_composite.type_name.to_std_string_view()); // FIXME
                return llvm::StructType::getTypeByName(*_llvm_context, type_name);
            }
            default: return get_llvm_type(node->value.type);
        }
        return nullptr;
    }
};
