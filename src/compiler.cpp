#include <array>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

#include <FileWatch.hpp>
#include <fmt/chrono.h>
#include <fmt/os.h>

#include <Parser.hpp>
#include <Tokenizer.hpp>
#include <asm/wasm.hpp>
#include <utils/CLIArg.hpp>

#include <het_unordered_map.hpp>

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Verifier.h>

#include "llvm/Support/TargetSelect.h"
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>

static std::unique_ptr<llvm::LLVMContext>   llvm_context(new llvm::LLVMContext());
static llvm::IRBuilder<>                    llvm_ir_builder(*llvm_context);
static het_unordered_map<llvm::AllocaInst*> llvm_names_values;

static llvm::AllocaInst* CreateEntryBlockAlloca(llvm::Function* func, auto type, const std::string& name) {
    // llvm::IRBuilder<> tmp_builder(&func->getEntryBlock(), func->getEntryBlock().begin());
    // return tmp_builder.CreateAlloca(type, 0, name.c_str());
    return llvm_ir_builder.CreateAlloca(type, 0, name.c_str());
}

llvm::Value* codegen_constant(const GenericValue& val) {
    switch(val.type) {
        case GenericValue::Type::Float: return llvm::ConstantFP::get(*llvm_context, llvm::APFloat(val.value.as_float));
        case GenericValue::Type::Integer: return llvm::ConstantInt::get(*llvm_context, llvm::APInt(32, val.value.as_int32_t));
        default: warn("LLVM Codegen: Unsupported value type '{}'.\n", val.type);
    }
    return nullptr;
}

llvm::Value* codegen(const AST::Node* node) {
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
            // auto              curent_block = llvm_ir_builder.GetInsertBlock();
            // llvm::Function*   current_function = llvm_ir_builder.GetInsertBlock()->getParent();
            // llvm::BasicBlock* new_block = llvm::BasicBlock::Create(llvm_context, "scope", current_function);
            // llvm_ir_builder.SetInsertPoint(new_block);
            for(auto c : node->children)
                codegen(c);
            // llvm_ir_builder.SetInsertPoint(curent_block);
            return llvm::Constant::getNullValue(llvm::Type::getInt32Ty(*llvm_context));
        }
        case AST::Node::Type::ConstantValue: return codegen_constant(node->value);
        case AST::Node::Type::VariableDeclaration: {
            llvm::AllocaInst* ret = nullptr;
            switch(node->value.type) {
                case GenericValue::Type::Float: ret = CreateEntryBlockAlloca(nullptr, llvm::Type::getFloatTy(*llvm_context), std::string{node->token.value}); break;
                case GenericValue::Type::Integer: ret = CreateEntryBlockAlloca(nullptr, llvm::Type::getInt32Ty(*llvm_context), std::string{node->token.value}); break;
                default: warn("LLVM Codegen: Unsupported variable type '{}'.\n", node->value.type); break;
            }
            llvm_names_values[std::string{node->token.value}] = ret;
            return ret;
        }
        case AST::Node::Type::Variable: {
            auto var = llvm_names_values[std::string{node->token.value}];
            if(!var) {
                error("LLVM Codegen: Undeclared variable '{}'.\n", node->token.value);
                return nullptr;
            }
            return llvm_ir_builder.CreateLoad(var->getAllocatedType(), var, std::string{node->token.value}.c_str());
        }
        case AST::Node::Type::BinaryOperator: {
            auto lhs = codegen(node->children[0]);
            auto rhs = codegen(node->children[1]);
            if(!lhs || !rhs)
                return nullptr;
            if(node->token.value == "+") {
                if(node->value.type == GenericValue::Type::Integer)
                    return llvm_ir_builder.CreateAdd(lhs, rhs, "addtmp");
                else
                    return llvm_ir_builder.CreateFAdd(lhs, rhs, "addftmp");
            } else if(node->token.value == "-") {
                if(node->value.type == GenericValue::Type::Integer)
                    return llvm_ir_builder.CreateSub(lhs, rhs, "subtmp");
                else
                    return llvm_ir_builder.CreateFSub(lhs, rhs, "subftmp");
            } else if(node->token.value == "*") {
                if(node->value.type == GenericValue::Type::Integer)
                    return llvm_ir_builder.CreateMul(lhs, rhs, "multmp");
                else
                    return llvm_ir_builder.CreateFMul(lhs, rhs, "mulftmp");
            } else if(node->token.value == "/") {
                auto div = llvm_ir_builder.CreateFDiv(lhs, rhs, "divftmp");
                if(node->value.type == GenericValue::Type::Integer)
                    return llvm_ir_builder.CreateFPToUI(div, llvm::Type::getInt32Ty(*llvm_context), "intcasttmp");
                else
                    return div;
                // Boolean Operators
            } else if(node->token.value == "<") {
                // TODO: More types
                if(node->children[0]->value.type == GenericValue::Type::Integer && node->children[1]->value.type == GenericValue::Type::Integer)
                    return llvm_ir_builder.CreateCmp(llvm::CmpInst::Predicate::ICMP_SLT, lhs, rhs, "ICMP_SLT");
                else if(node->children[0]->value.type == GenericValue::Type::Float && node->children[1]->value.type == GenericValue::Type::Float)
                    return llvm_ir_builder.CreateFCmp(llvm::CmpInst::Predicate::FCMP_OLT, lhs, rhs, "FCMP_OLT");
            } else if(node->token.value == ">") {
                // TODO: More types
                if(node->children[0]->value.type == GenericValue::Type::Integer && node->children[1]->value.type == GenericValue::Type::Integer)
                    return llvm_ir_builder.CreateCmp(llvm::CmpInst::Predicate::ICMP_SGT, lhs, rhs, "ICMP_SGT");
                else if(node->children[0]->value.type == GenericValue::Type::Float && node->children[1]->value.type == GenericValue::Type::Float)
                    return llvm_ir_builder.CreateFCmp(llvm::CmpInst::Predicate::FCMP_OGT, lhs, rhs, "FCMP_OGT");
            } else if(node->token.value == "=") {
                assert(node->children[0]->type == AST::Node::Type::Variable); // FIXME
                auto variable = llvm_names_values[std::string{node->children[0]->token.value}];
                llvm_ir_builder.CreateStore(rhs, variable);
                return rhs;
            }
            warn("LLVM Code: Unimplemented Binary Operator '{}'.\n", node->token.value);
            break;
        }
        case AST::Node::Type::WhileStatement: {
            llvm::Function* current_function = llvm_ir_builder.GetInsertBlock()->getParent();
            auto            current_block = llvm_ir_builder.GetInsertBlock();

            llvm::BasicBlock* condition_block = llvm::BasicBlock::Create(*llvm_context, "while_condition", current_function);
            llvm::BasicBlock* loop_block = llvm::BasicBlock::Create(*llvm_context, "while_loop", current_function);
            llvm::BasicBlock* after_block = llvm::BasicBlock::Create(*llvm_context, "while_end", current_function);

            llvm_ir_builder.CreateBr(condition_block);

            llvm_ir_builder.SetInsertPoint(condition_block);
            auto condition_label = llvm_ir_builder.GetInsertBlock();
            auto condition = codegen(node->children[0]);
            if(!condition)
                return nullptr;
            llvm_ir_builder.CreateCondBr(condition, loop_block, after_block);

            llvm_ir_builder.SetInsertPoint(loop_block);
            auto loop_code = codegen(node->children[1]);
            if(!loop_code)
                return nullptr;
            llvm_ir_builder.CreateBr(condition_label);

            llvm_ir_builder.SetInsertPoint(after_block);
            return llvm::Constant::getNullValue(llvm::Type::getInt32Ty(*llvm_context));
        }
        case AST::Node::Type::ReturnStatement: return llvm_ir_builder.CreateRet(codegen(node->children[0]));
        default: warn("LLVM Codegen: Unsupported node type '{}'.\n", node->type);
    }
    return nullptr;
}

llvm::Value* codegen(const AST& ast) {
    return codegen(&ast.getRoot());
}

int main(int argc, char* argv[]) {
    fmt::print("╭{0:─^{2}}╮\n"
               "│{1: ^{2}}│\n"
               "╰{0:─^{2}}╯\n",
               "", "<insert language name> compiler.", 80);
    CLIArg args;
    args.add('o', "out", true, "Specify the output file.");
    args.add('t', "tokens", false, "Dump the state after the tokenizing stage.");
    args.add('a', "ast", false, "Dump the parsed AST to the command line.");
    args.add('i', "ir", false, "Output LLVM Intermediate Representation.");
    args.add('w', "watch", false, "Watch the supplied file and re-run on changes.");
    args.add('s', "wasm", false, "Output WASM S-Expression Text format (Currently only works for exporting top-level functions and is extremly limited).");
    args.parse(argc, argv);

    if(args.get_default_arg() == "") {
        error("No source file provided.\n");
        print("Usage: 'compiler path/to/source.lang'.\n");
        args.print_help();
        return -1;
    }

    auto handle_file = [&]() {
        auto           filename = args.get_default_arg();
        std ::ifstream file(filename);
        if(!file) {
            error("Couldn't open file '{}' (Running from {}).\n", filename, std::filesystem::current_path().string());
            return -1;
        }
        std::string source{(std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>()};

        std::vector<Tokenizer::Token> tokens;
        Tokenizer                     tokenizer(source);
        while(tokenizer.has_more())
            tokens.push_back(tokenizer.consume());

        // Print tokens
        if(args['t'].set) {
            int i = 0;
            for(const auto& t : tokens) {
                fmt::print("  {}", t);
                if(++i % 6 == 0)
                    fmt::print("\n");
            }
            fmt::print("\n");
        }

        Parser parser;
        auto   ast = parser.parse(tokens);
        if(ast.has_value()) {
            if(args['a'].set) {
                if(args['o'].set) {
                    auto out = fmt::output_file(args['o'].value);
                    out.print("{}", *ast);
                    fmt::print("AST written to '{}'.\n", args['o'].value);
                    system(fmt::format("cat \"{}\"", args['o'].value).c_str());
                } else
                    fmt::print("{}", *ast);
            }
            if(args['s'].set)
                generate_wasm_s_expression(*ast);
            if(args['i'].set) {
                if(args['o'].set) {
                    std::unique_ptr<llvm::Module> llvm_module{new llvm::Module{filename, *llvm_context}};
                    std::vector<llvm::Type*>      main_param_types; //(1, llvm::Type::getInt32Ty(*llvm_context));
                    auto                          main_return_type = llvm::FunctionType::get(llvm::Type::getInt32Ty(*llvm_context), main_param_types, false);
                    auto                          main_function = llvm::Function::Create(main_return_type, llvm::Function::ExternalLinkage, "main", llvm_module.get());
                    auto*                         mainblock = llvm::BasicBlock::Create(*llvm_context, "entrypoint", main_function);
                    llvm_ir_builder.SetInsertPoint(mainblock);
                    auto result = codegen(*ast);
                    if(!result) {
                        error("LLVM Codegen returned nullptr.\n");
                        return 0;
                    }

                    llvm_module->dump();
                    if(llvm::verifyModule(*llvm_module, &llvm::errs())) {
                        error("Errors in LLVM Module, exiting...\n");
                        return 0;
                    }

                    // Output text IR to output file
                    const auto      filepath = args['o'].value;
                    std::error_code err;
                    auto            file = llvm::raw_fd_ostream(filepath, err);
                    if(!err)
                        llvm_module->print(file, nullptr);
                    else
                        error("Error opening '{}': {}\n", filepath, err);

                    // Test JIT
                    // llvm::InitializeAllTargetInfos();
                    // llvm::InitializeAllTargets();
                    // llvm::InitializeAllTargetMCs();
                    // llvm::InitializeAllAsmPrinters();
                    llvm::InitializeNativeTarget();
                    llvm::InitializeNativeTargetAsmPrinter();

                    llvm::ExitOnError ExitOnErr;
                    // Try to detect the host arch and construct an LLJIT instance.
                    auto JIT = ExitOnErr(llvm::orc::LLJITBuilder().create());
                    // Add the module.
                    ExitOnErr(JIT.get()->addIRModule(llvm::orc::ThreadSafeModule(std::move(llvm_module), std::move(llvm_context))));
                    // Look up the JIT'd code entry point.
                    auto EntrySym = ExitOnErr(JIT.get()->lookup("main"));
                    // Cast the entry point address to a function pointer.
                    auto* Entry = (int (*)())EntrySym.getAddress();
                    // Call into JIT'd code.
                    auto return_value = Entry();
                    success("JIT main function returned '{}'\n", return_value);
                }
            }
        }
        return 0;
    };

    if(args['w'].set) {
        handle_file();
        filewatch::FileWatch<std::string> watcher{args.get_default_arg(), [&](const std::string& path, const filewatch::Event) {
                                                      puts("\033[2J"); // Clear screen
                                                      fmt::print("[{:%T}] <insert lang name> compiler: {} changed, reprocessing...\n", std::chrono::system_clock::now(), path);
                                                      handle_file();
                                                      success("[{:%T}] Watching for changes on {}... ", std::chrono::system_clock::now(), args.get_default_arg());
                                                      fmt::print("(CTRL+C to exit)\n\n");
                                                  }};
        success("[{:%T}] Watching for changes on {}... ", std::chrono::system_clock::now(), args.get_default_arg());
        fmt::print("(CTRL+C to exit)\n");
        while(true) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    } else {
        return handle_file();
    }
}
