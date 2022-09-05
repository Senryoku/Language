#include <array>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

#include <FileWatch.hpp>
#include <fmt/chrono.h>
#include <fmt/format.h>
#include <fmt/os.h>

#include <Parser.hpp>
#include <Tokenizer.hpp>
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

int main(int argc, char* argv[]) {
    fmt::print("┌{0:─^{2}}┐\n"
               "│{1: ^{2}}│\n"
               "└{0:─^{2}}┘\n",
               "", "<insert language name> compiler.", 80);
    CLIArg args;
    args.add('o', "out", true, "Specify the output file.");
    args.add('t', "tokens", false, "Dump the state after the tokenizing stage.");
    args.add('a', "ast", false, "Dump the parsed AST to the command line.");
    args.add('i', "ir", false, "Output LLVM Intermediate Representation.");
    args.add('r', "run", false, "Run the resulting executable.");
    args.add('b', "object", false, "Output an object file.");
    args.add('j', "jit", false, "Run the module using JIT.");
    args.add('w', "watch", false, "Watch the supplied file and re-run on changes.");
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

        auto tokenizing_start = std::chrono::high_resolution_clock::now();

        std::vector<Tokenizer::Token> tokens;
        try {
            Tokenizer tokenizer(source);
            while(tokenizer.has_more())
                tokens.push_back(tokenizer.consume());
        } catch(const Exception& e) {
            e.display();
            return 1;
        }

        auto tokenizing_end = std::chrono::high_resolution_clock::now();

        // Print tokens
        if(args['t'].set) {
            int i = 0;
            for(const auto& t : tokens) {
                fmt::print("  {}", t);
                if(++i % 6 == 0)
                    fmt::print("\n");
            }
            fmt::print("\n");
            return 0;
        }

        auto   parsing_start = std::chrono::high_resolution_clock::now();
        Parser parser;
        auto   ast = parser.parse(tokens);
        auto   parsing_end = std::chrono::high_resolution_clock::now();
        if(ast.has_value()) {
            if(args['a'].set) {
                if(args['o'].set) {
                    auto out = fmt::output_file(args['o'].value);
                    out.print("{}", *ast);
                    fmt::print("AST written to '{}'.\n", args['o'].value);
                    std::system(fmt::format("cat \"{}\"", args['o'].value).c_str());
                } else
                    fmt::print("{}", *ast);
                return 0;
            }

            auto ir_filepath = std::filesystem::path(filename).stem().replace_extension(".ll");
            if(args['o'].set)
                ir_filepath = args['o'].value;

            try {
                auto                               codegen_start = std::chrono::high_resolution_clock::now();
                std::unique_ptr<llvm::LLVMContext> llvm_context(new llvm::LLVMContext());
                Module                             new_module{filename, llvm_context.get()};
                auto                               result = new_module.codegen(*ast);
                if(!result) {
                    error("LLVM Codegen returned nullptr.\n");
                    return 1;
                }
                if(llvm::verifyModule(new_module.get_llvm_module(), &llvm::errs())) {
                    error("Errors in LLVM Module, exiting...\n");
                    return 1;
                }
                auto codegen_end = std::chrono::high_resolution_clock::now();

// Will use assembly on disk as an intermediary step if set to 1
#define USING_LLC 0

                auto write_ir_start = std::chrono::high_resolution_clock::now();
#if !USING_LLC
                if(args['i'].set)
#endif
                {
                    // Output text IR to output file
                    std::error_code err;
                    auto            file = llvm::raw_fd_ostream(ir_filepath.string(), err);
                    if(!err)
                        new_module.get_llvm_module().print(file, nullptr);
                    else {
                        error("Error opening '{}': {}\n", ir_filepath.string(), err);
                        return 1;
                    }
                    success("LLVM IR written to {}.\n", ir_filepath.string());
                    if(args['i'].set)
                        return 0;
                }

                auto write_ir_end = std::chrono::high_resolution_clock::now();

                auto object_gen_start = std::chrono::high_resolution_clock::now();
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

                // Generate Object file
                auto o_filepath = std::filesystem::path(filename).stem().replace_extension(".o");
#if USING_LLC
                if(args['b'].set)
#endif
                {
                    if(args['b'].set && args['o'].set)
                        o_filepath = args['o'].value;
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
                    if(args['b'].set)
                        return 0;
                }

                if(args['j'].set) {
                    // Quick Test JIT (TODO: Remove?)
                    lang::LLVMJIT jit;
                    auto          return_value = jit.run(std::move(new_module.get_llvm_module_ptr()), std::move(llvm_context));
                    success("JIT main function returned '{}'\n", return_value);
                    return 0;
                }
                auto object_gen_end = std::chrono::high_resolution_clock::now();

                auto llc_start = std::chrono::high_resolution_clock::now();
#if USING_LLC
                if(auto retval = std::system(fmt::format("llc \"{}\"", ir_filepath.string()).c_str()); retval != 0) {
                    error("Error running llc: {}.\n", retval);
                    return 1;
                }
#endif
                auto llc_end = std::chrono::high_resolution_clock::now();

                auto clang_start = std::chrono::high_resolution_clock::now();
                auto final_outputfile = args['o'].set ? args['o'].value : ir_filepath.replace_extension(".exe").string();
#if USING_LLC
                if(auto retval = std::system(fmt::format("clang -O3 \"{}\" -o \"{}\"", ir_filepath.replace_extension(".ll").string(), final_outputfile).c_str()); retval != 0)
#else
                // From object file
                if(auto retval = std::system(fmt::format("clang \"{}\" -o \"{}\"", std::filesystem::absolute(o_filepath).string(), final_outputfile).c_str()); retval != 0)
#endif
                {
                    error("Error running clang: {}.\n", retval);
                    return 1;
                }
                auto clang_end = std::chrono::high_resolution_clock::now();
                success("Compiled successfully to {}\n", final_outputfile);
                print(" {:<12} | {:<12} | {:<12} | {:<12} | {:<12} | {:<12} | {:<12} \n", "Tokenizer", "Parser", "LLVMCodegen", "IR", "ObjectGen", "LLC", "Clang");
                print(" {:^12.2} | {:^12.2} | {:^12.2} | {:^12.2} | {:^12.2} | {:^12.2} | {:^12.2} \n",
                      std::chrono::duration<double, std::milli>(tokenizing_end - tokenizing_start), std::chrono::duration<double, std::milli>(parsing_end - parsing_start),
                      std::chrono::duration<double, std::milli>(write_ir_end - write_ir_start), std::chrono::duration<double, std::milli>(object_gen_end - object_gen_start),
                      std::chrono::duration<double, std::milli>(codegen_end - codegen_start), std::chrono::duration<double, std::milli>(llc_end - llc_start),
                      std::chrono::duration<double, std::milli>(clang_end - clang_start));

                // Run the generated program. FIXME: Handy, but dangerous.
                if(args['r'].set) {
                    print("Running {}...\n", final_outputfile);
                    auto retval = std::system(final_outputfile.c_str());
                    print(" > {} returned {}.\n", final_outputfile, retval);
                }
            } catch(const std::exception& e) {
                error("Exception: {}", e.what());
                return 1;
            }
        }
        return 0;
    };

    if(args['w'].set) {
        handle_file();
        auto                              last_run = std::chrono::system_clock::now();
        filewatch::FileWatch<std::string> watcher{args.get_default_arg(), [&](const std::string& path, const filewatch::Event) {
                                                      if(std::chrono::system_clock::now() - last_run < std::chrono::seconds(1)) // Ignore double events
                                                          return;
                                                      fmt::print("[{:%T}] <insert lang name> compiler: {} changed, reprocessing...\n", std::chrono::system_clock::now(), path);
                                                      // Wait a bit, at least on Windows it looks like the file read will fail if attempted right after the modification event.
                                                      std::this_thread::sleep_for(std::chrono::milliseconds(100));
                                                      handle_file();
                                                      last_run = std::chrono::system_clock::now();
                                                      success("\n[{:%T}] Watching for changes on {}... ", std::chrono::system_clock::now(), args.get_default_arg());
                                                      fmt::print("(CTRL+C to exit)\n\n");
                                                  }};
        success("\n[{:%T}] Watching for changes on {}... ", std::chrono::system_clock::now(), args.get_default_arg());
        fmt::print("(CTRL+C to exit)\n\n");
        // TODO: Provide a prompt?
        while(true) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    } else {
        return handle_file();
    }
}
