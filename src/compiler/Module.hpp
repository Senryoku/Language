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
    void codegen_imports(const std::vector<AST::FunctionDeclaration*>& nodes) {
        for(const auto& n : nodes) {
            assert(n->type == AST::Node::Type::FunctionDeclaration);
            auto                     name = n->token.value.data();
            auto                     return_type = get_llvm_type(n->value_type);
            std::vector<llvm::Type*> func_args_types;
            // These function nodes should not have bodies
            for(const auto& c : n->children) {
                assert(c->type == AST::Node::Type::VariableDeclaration);
                func_args_types.push_back(get_llvm_type(c->value_type));
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
        // put
        std::vector<llvm::Type*> put_args_types({llvm::Type::getInt8Ty(*_llvm_context)});
        llvm::FunctionType*      put_type = llvm::FunctionType::get(llvm::Type::getInt32Ty(*_llvm_context), put_args_types, false);
        auto                     put_func = _llvm_module->getOrInsertFunction("put", put_type);

        const auto integer_t = llvm::Type::getInt32Ty(*_llvm_context);
        const auto void_t = llvm::Type::getVoidTy(*_llvm_context);
        const auto str_t = llvm::Type::getInt8PtrTy(*_llvm_context);

        _llvm_module->getOrInsertFunction("__socket_init", llvm::FunctionType::get(void_t, {}, false));
        _llvm_module->getOrInsertFunction("__socket_create", llvm::FunctionType::get(integer_t, {}, false));
        _llvm_module->getOrInsertFunction("__socket_connect", llvm::FunctionType::get(integer_t, {integer_t, str_t, integer_t}, false));
        _llvm_module->getOrInsertFunction("__socket_send", llvm::FunctionType::get(integer_t, {integer_t, str_t/*, str_t, integer_t*/}, false));
        _llvm_module->getOrInsertFunction("__socket_recv", llvm::FunctionType::get(str_t, {integer_t}, false));
        _llvm_module->getOrInsertFunction("__socket_close", llvm::FunctionType::get(integer_t, {integer_t}, false));

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

    llvm::Constant* codegen_constant(const AST::Node* val);
    llvm::Value*    codegen(const AST::Node* node);

    llvm::Type* get_llvm_type(PrimitiveType type) const {
        switch(type) {
            using enum PrimitiveType;
            case Integer: return llvm::Type::getInt32Ty(*_llvm_context);
            case Float: return llvm::Type::getFloatTy(*_llvm_context);
            case Char: return llvm::Type::getInt8Ty(*_llvm_context);
            case String: return llvm::Type::getInt8PtrTy(*_llvm_context); // FIXME
            case Void: return llvm::Type::getVoidTy(*_llvm_context);
            default: error("[Module::get_llvm_type] GenericValue Type '{}' not mapped to a LLVM Type.\n", type); assert(false);
        }
        return nullptr;
    }

    llvm::Type* get_llvm_type(const ValueType& type) const {
        if(type.is_pointer || type.is_reference) {
            auto llvm_type = get_llvm_type(type.get_pointed_type());
            return llvm_type->getPointerTo(0);
        }
        if(type.is_array) {
            auto llvm_type = get_llvm_type(type.get_element_type());
            return llvm::ArrayType::get(llvm_type, type.capacity);
        }
        if(type.is_composite()) {
            std::string type_name(GlobalTypeRegistry::instance().get_type(type.type_id)->name()); // FIXME: Internalize the string and remove this
            auto        structType = llvm::StructType::getTypeByName(*_llvm_context, type_name);
            if(!structType)
                throw Exception(fmt::format("[LLVMCodegen] Could not find struct with name '{}'.\n", type_name));
            return structType;
        }
        return get_llvm_type(type.primitive);
    }

    void insert_defer_block(const AST::Node* node);
};
