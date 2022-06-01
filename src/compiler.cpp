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
#include <compiler/Module.hpp>
#include <utils/CLIArg.hpp>

#include <het_unordered_map.hpp>

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Verifier.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/Host.h>

#include <jit/LLVMJIT.hpp>

static std::unique_ptr<llvm::LLVMContext> llvm_context(new llvm::LLVMContext());

int main(int argc, char* argv[]) {
    fmt::print("?{0:-^{2}}?\n"
               "¦{1: ^{2}}¦\n"
               "?{0:-^{2}}?\n",
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
        std ::ifstream input_file(filename);
        if(!input_file) {
            error("Couldn't open file '{}' (Running from {}).\n", filename, std::filesystem::current_path().string());
            return 1;
        }
        std::string source{(std::istreambuf_iterator<char>(input_file)), std::istreambuf_iterator<char>()};

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
                auto ir_filepath = std::filesystem::path(filename).stem().replace_extension(".ll");
                if(args['o'].set)
                    ir_filepath = args['o'].value;
                Module new_module{filename, llvm_context.get()};
                auto   result = new_module.codegen(*ast);
                if(!result) {
                    error("LLVM Codegen returned nullptr.\n");
                    return 1;
                }

                // TODO: Remove
                new_module.get_llvm_module().dump();
                if(llvm::verifyModule(new_module.get_llvm_module(), &llvm::errs())) {
                    error("Errors in LLVM Module, exiting...\n");
                    return 1;
                }

                // Output text IR to output file
                std::error_code err;
                auto            file = llvm::raw_fd_ostream(ir_filepath.string(), err);
                if(!err)
                    new_module.get_llvm_module().print(file, nullptr);
                else {
                    error("Error opening '{}': {}\n", ir_filepath.string(), err);
                    return 1;
                }

#if 0
                auto target_triple = llvm::sys::getDefaultTargetTriple();
                llvm::InitializeNativeTarget();
                llvm::InitializeNativeTargetAsmParser();
                llvm::InitializeNativeTargetAsmPrinter();
                std::string error_str;
                auto        target = llvm::TargetRegistry::lookupTarget(target_triple, error_str);
                if(!target) {
                    error("Could not lookup target: {}.\n", error_str);
                    return 1;
                }
                auto cpu = "generic";
                auto features = "";

                llvm::TargetOptions opt;
                auto                reloc_model = llvm::Optional<llvm::Reloc::Model>();
                auto                target_machine = target->createTargetMachine(target_triple, cpu, features, opt, reloc_model);

                new_module.get_llvm_module().setDataLayout(target_machine->createDataLayout());
                new_module.get_llvm_module().setTargetTriple(target_triple);

                auto o_filepath = std::filesystem::path(filename).stem().replace_extension(".o");
                // if(args['o'].set)
                //     o_filepath = args['o'].value;
                std::error_code      error_code;
                llvm::raw_fd_ostream dest(o_filepath.string(), error_code, llvm::sys::fs::OF_None);

                if(error_code) {
                    error("Could not open file '{}': {}.\n", o_filepath.string(), error_code.message());
                    return 1;
                }

                llvm::legacy::PassManager pass;
                auto                      file_type = llvm::CGFT_ObjectFile;

                if(target_machine->addPassesToEmitFile(pass, dest, nullptr, file_type)) {
                    error("Target Machine can't emit a file of this type.\n");
                    return 1;
                }

                pass.run(new_module.get_llvm_module());
                dest.flush();
                success("Wrote object file '{}'.\n", o_filepath.string());
#else
                // Quick Test JIT (TODO: Remove)
                lang::LLVMJIT jit;
                auto          return_value = jit.run(std::move(new_module.get_llvm_module_ptr()), std::move(llvm_context));
                success("JIT main function returned '{}'\n", return_value);
#endif
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
