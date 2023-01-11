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
        _builtins["memcpy"] = std::bind(&Module::intrinsic_memcpy, this, std::placeholders::_1);
        _builtins["min"] = std::bind(&Module::intrinsic_min, this, std::placeholders::_1);
        _builtins["max"] = std::bind(&Module::intrinsic_max, this, std::placeholders::_1);
        _builtins["abs"] = std::bind(&Module::intrinsic_abs, this, std::placeholders::_1);

        _builtins["pow"] = std::bind(&Module::intrinsic_pow, this, std::placeholders::_1);

        _builtins["sqrt"] = std::bind(&Module::intrinsic_unary, this, llvm::Intrinsic::IndependentIntrinsics::sqrt, std::placeholders::_1);
        _builtins["sin"] = std::bind(&Module::intrinsic_unary, this, llvm::Intrinsic::IndependentIntrinsics::sin, std::placeholders::_1);
        _builtins["cos"] = std::bind(&Module::intrinsic_unary, this, llvm::Intrinsic::IndependentIntrinsics::cos, std::placeholders::_1);
        _builtins["exp"] = std::bind(&Module::intrinsic_unary, this, llvm::Intrinsic::IndependentIntrinsics::exp, std::placeholders::_1);
        _builtins["exp2"] = std::bind(&Module::intrinsic_unary, this, llvm::Intrinsic::IndependentIntrinsics::exp2, std::placeholders::_1);
        _builtins["log"] = std::bind(&Module::intrinsic_unary, this, llvm::Intrinsic::IndependentIntrinsics::log, std::placeholders::_1);
        _builtins["log10"] = std::bind(&Module::intrinsic_unary, this, llvm::Intrinsic::IndependentIntrinsics::log10, std::placeholders::_1);
        _builtins["log2"] = std::bind(&Module::intrinsic_unary, this, llvm::Intrinsic::IndependentIntrinsics::log2, std::placeholders::_1);

        _builtins["floor"] = std::bind(&Module::intrinsic_unary, this, llvm::Intrinsic::IndependentIntrinsics::floor, std::placeholders::_1);
        _builtins["ceil"] = std::bind(&Module::intrinsic_unary, this, llvm::Intrinsic::IndependentIntrinsics::ceil, std::placeholders::_1);
        _builtins["trunc"] = std::bind(&Module::intrinsic_unary, this, llvm::Intrinsic::IndependentIntrinsics::trunc, std::placeholders::_1);
        _builtins["round"] = std::bind(&Module::intrinsic_unary, this, llvm::Intrinsic::IndependentIntrinsics::round, std::placeholders::_1);
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
    void codegen_imports(const std::vector<AST::FunctionDeclaration*>& nodes) {
        for(const auto n : nodes) {
            assert(n->type == AST::Node::Type::FunctionDeclaration);
            codegen(n);
        }
    }

    void codegen_imports(const std::vector<AST::TypeDeclaration*>& nodes) {
        for(const auto n : nodes) {
            assert(n->type == AST::Node::Type::TypeDeclaration);
            codegen(n);
        }
    }

    llvm::Value* codegen(const AST& ast) {
        // TEMP: "Standard Library"
        // Make libc printf available
        std::vector<llvm::Type*> printf_args_types({llvm::Type::getInt8PtrTy(*_llvm_context)});
        llvm::FunctionType*      printf_type = llvm::FunctionType::get(llvm::Type::getInt32Ty(*_llvm_context), printf_args_types, true);
        _llvm_module->getOrInsertFunction("printf", printf_type);
        // put
        std::vector<llvm::Type*> put_args_types({llvm::Type::getInt8Ty(*_llvm_context)});
        llvm::FunctionType*      put_type = llvm::FunctionType::get(llvm::Type::getInt32Ty(*_llvm_context), put_args_types, false);
        _llvm_module->getOrInsertFunction("put", put_type);

        // Register libc malloc and free
        _llvm_module->getOrInsertFunction("malloc", llvm::FunctionType::get(llvm::Type::getInt64Ty(*_llvm_context), {llvm::Type::getInt64Ty(*_llvm_context)}, false));
        _llvm_module->getOrInsertFunction("free", llvm::FunctionType::get(llvm::Type::getVoidTy(*_llvm_context), {llvm::Type::getInt64Ty(*_llvm_context)}, false));

        // Actual codegen
        auto r = codegen(&ast.get_root());
        return r;
    }

  private:
    std::vector<Scope>                                                             _scopes{Scope{}};
    llvm::LLVMContext*                                                             _llvm_context;
    std::unique_ptr<llvm::Module>                                                  _llvm_module;
    llvm::IRBuilder<>                                                              _llvm_ir_builder;
    std::unordered_map<std::string, std::function<llvm::Value*(const AST::Node*)>> _builtins;

    bool _generated_return = false; // Tracks if the last node generated a return statement (FIXME: Remove?)

    Scope&       get_scope() { return _scopes.back(); }
    const Scope& get_scope() const { return _scopes.back(); }

    llvm::Constant* codegen_constant(const AST::Node* val);
    llvm::Value*    codegen(const AST::Node* node);

    llvm::Type* get_llvm_type(TypeID type_id) const;

    llvm::Value* intrinsic_memcpy(const AST::Node* node);
    llvm::Value* intrinsic_min(const AST::Node* node);
    llvm::Value* intrinsic_max(const AST::Node* node);
    llvm::Value* intrinsic_abs(const AST::Node* node);
    llvm::Value* intrinsic_pow(const AST::Node* node);
    llvm::Value* intrinsic_unary(llvm::Intrinsic::IndependentIntrinsics intrinsic, const AST::Node* node);
};
