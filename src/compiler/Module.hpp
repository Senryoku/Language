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
        std::vector<llvm::Type*> main_param_types; //(1, llvm::Type::getInt32Ty(*_llvm_context));
        auto                     main_return_type = llvm::FunctionType::get(llvm::Type::getInt32Ty(*_llvm_context), main_param_types, false); // Returns void right now
        auto                     main_function = llvm::Function::Create(main_return_type, llvm::Function::ExternalLinkage, "main", _llvm_module.get());
        auto*                    mainblock = llvm::BasicBlock::Create(*_llvm_context, "entrypoint", main_function);
        _llvm_ir_builder.SetInsertPoint(mainblock);
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

    llvm::Value* codegen(const AST& ast) {
        // TEMP: "Standard Library"
        // Make libc printf available
        std::vector<llvm::Type*> printfArgsTypes({llvm::Type::getInt8PtrTy(*_llvm_context)});
        llvm::FunctionType*      printfType = llvm::FunctionType::get(llvm::Type::getInt32Ty(*_llvm_context), printfArgsTypes, true);
        auto                     printfFunc = _llvm_module->getOrInsertFunction("printf", printfType);

        // Actual codegen
        auto r = codegen(&ast.getRoot());
        // Add a return to our generated main (from constructor) (FIXME?)
        _llvm_ir_builder.CreateRet(llvm::ConstantInt::get(*_llvm_context, llvm::APInt(32, 0)));
        return r;
    }

  private:
    std::vector<Scope>            _scopes{Scope{}};
    llvm::LLVMContext*            _llvm_context;
    std::unique_ptr<llvm::Module> _llvm_module;
    llvm::IRBuilder<>             _llvm_ir_builder;

    Scope&       get_scope() { return _scopes.back(); }
    const Scope& get_scope() const { return _scopes.back(); }

    llvm::Value* codegen(const GenericValue& val);
    llvm::Value* codegen(const AST::Node* node);
};
