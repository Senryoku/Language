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

#include <string_utils.hpp>

CLIArg args;

bool handle_file(const std::string& path) {
    auto           filename = std::filesystem::path(path).stem();
    auto           total_start = std::chrono::high_resolution_clock::now();
    std ::ifstream input_file(path);
    if(!input_file) {
        error("Couldn't open file '{}' (Running from {}).\n", path, std::filesystem::current_path().string());
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
        return false;
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
        return true;
    }

    auto   parsing_start = std::chrono::high_resolution_clock::now();
    Parser parser;
    auto   ast = parser.parse(tokens);
    parser.write_export_interface(filename.replace_extension(".int"));
    auto   parsing_end = std::chrono::high_resolution_clock::now();
    if(ast.has_value()) {
        if(args['a'].set) {
            if(args['o'].set && args['o'].has_value()) {
                auto out = fmt::output_file(args['o'].value());
                out.print("{}", *ast);
                fmt::print("AST written to '{}'.\n", args['o'].value());
                std::system(fmt::format("cat \"{}\"", args['o'].value()).c_str());
            } else
                fmt::print("{}", *ast);
            return 0;
        }

        auto ir_filepath = filename.replace_extension(".ll");
        if(args['o'].set)
            ir_filepath = args['o'].value();

        try {
            auto                               codegen_start = std::chrono::high_resolution_clock::now();
            std::unique_ptr<llvm::LLVMContext> llvm_context(new llvm::LLVMContext());
            Module                             new_module{path, llvm_context.get()};
            auto                               result = new_module.codegen(*ast);
            if(!result) {
                error("LLVM Codegen returned nullptr.\n");
                return false;
            }
            if(llvm::verifyModule(new_module.get_llvm_module(), &llvm::errs())) {
                error("\nErrors in LLVM Module, exiting...\n");
                return false;
            }
            auto codegen_end = std::chrono::high_resolution_clock::now();

            auto write_ir_start = std::chrono::high_resolution_clock::now();
            if(args['i'].set) {
                // Output text IR to output file
                std::error_code err;
                auto            file = llvm::raw_fd_ostream(ir_filepath.string(), err);
                if(!err)
                    new_module.get_llvm_module().print(file, nullptr);
                else {
                    error("Error opening '{}': {}\n", ir_filepath.string(), err);
                    return false;
                }
                success("LLVM IR written to {}.\n", ir_filepath.string());
                if(args['i'].set)
                    return true;
            }

            auto write_ir_end = std::chrono::high_resolution_clock::now();

            auto object_gen_start = std::chrono::high_resolution_clock::now();

            // Generate Object file
            auto o_filepath = filename.replace_extension(".o");
            auto target_triple = llvm::sys::getDefaultTargetTriple();
            llvm::InitializeNativeTarget();
            llvm::InitializeNativeTargetAsmParser();
            llvm::InitializeNativeTargetAsmPrinter();
            std::string error_str;
            auto        target = llvm::TargetRegistry::lookupTarget(target_triple, error_str);
            if(!target) {
                error("Could not lookup target: {}.\n", error_str);
                return false;
            }
            auto cpu = "generic";
            auto features = "";

            llvm::TargetOptions opt;
            auto                reloc_model = llvm::Optional<llvm::Reloc::Model>();
            auto                target_machine = target->createTargetMachine(target_triple, cpu, features, opt, reloc_model);

            new_module.get_llvm_module().setDataLayout(target_machine->createDataLayout());
            new_module.get_llvm_module().setTargetTriple(target_triple);

            if(args['b'].set && args['o'].set)
                o_filepath = args['o'].value();
            std::error_code      error_code;
            llvm::raw_fd_ostream dest(o_filepath.string(), error_code, llvm::sys::fs::OF_None);

            if(error_code) {
                error("Could not open file '{}': {}.\n", o_filepath.string(), error_code.message());
                return 1;
            }

            llvm::legacy::PassManager pass;
            auto                      file_type = llvm::CGFT_ObjectFile;

            if(target_machine->addPassesToEmitFile(pass, dest, nullptr, file_type)) {
                error("Target Machine (Target Triple: {}) can't emit a file of this type.\n", target_triple);
                return false;
            }

            pass.run(new_module.get_llvm_module());
            dest.flush();
            success("Wrote object file '{}' (Target Triple: {}).\n", o_filepath.string(), target_triple);
            if(args['b'].set)
                return 0;

            if(args['j'].set) {
                // Quick Test JIT (TODO: Remove?)
                lang::LLVMJIT jit;
                auto          return_value = jit.run(std::move(new_module.get_llvm_module_ptr()), std::move(llvm_context));
                success("JIT main function returned '{}'\n", return_value);
                return 0;
            }
            auto object_gen_end = std::chrono::high_resolution_clock::now();
            auto total_end = std::chrono::high_resolution_clock::now();

            print(" {:<12} | {:<12} | {:<12} | {:<12} | {:<12} | {:<12} \n", "Tokenizer", "Parser", "LLVMCodegen", "IR", "ObjectGen", "Total");
            print(" {:^12.2} | {:^12.2} | {:^12.2} | {:^12.2} | {:^12.2} | {:^12.2} \n", std::chrono::duration<double, std::milli>(tokenizing_end - tokenizing_start),
                  std::chrono::duration<double, std::milli>(parsing_end - parsing_start), std::chrono::duration<double, std::milli>(codegen_end - codegen_start),
                  std::chrono::duration<double, std::milli>(write_ir_end - write_ir_start), std::chrono::duration<double, std::milli>(object_gen_end - object_gen_start),
                  std::chrono::duration<double, std::milli>(total_end - total_start));
        } catch(const std::exception& e) {
            error("Exception: {}", e.what());
            return false;
        }
        return true;
    }
    return false;
}

bool link() {
    try {
        auto        clang_start = std::chrono::high_resolution_clock::now();
        auto        final_outputfile = args['o'].set                         ? args['o'].value()
                                       : args.get_default_args().size() == 1 ? std::filesystem::path(args.get_default_arg()).filename().replace_extension(".exe").string()
                                                                             : "a.out";
        std::string input_files;
        for(const auto& file : args.get_default_args())
            input_files += " \"" + std::filesystem::path(file).filename().replace_extension(".o").string() + "\"";
        auto command = fmt::format("clang {} -o \"{}\"", input_files, final_outputfile);
        print("Running '{}'\n", command);
        if(auto retval = std::system(command.c_str()); retval != 0) {
            error("Error running clang: {}.\n", retval);
            return false;
        }
        auto clang_end = std::chrono::high_resolution_clock::now();
        success("Compiled successfully to {} in {:^12.2}\n", final_outputfile, std::chrono::duration<double, std::milli>(clang_end - clang_start));

        // Run the generated program. FIXME: Handy, but dangerous.
        if(args['r'].set) {
            auto run_command = final_outputfile;
            for(const auto& arg : args['r'].values)
                run_command += " " + arg;
            print("Running {}...\n", run_command);
            auto retval = std::system(run_command.c_str());
            print(" > {} returned {}.\n", final_outputfile, retval);
        }
    } catch(const std::exception& e) {
        error("Exception: {}", e.what());
        return false;
    }
    return true;
};

bool handle_all() {
    // FIXME: We should generate a dependency tree to 
    //  1. Pull all the required dependencies if they're not explicitly passed as argmument
    //  2. Recompile only the necessary files.
    //  3. Recompile in the correct order.
    for(const auto& file : args.get_default_args()) {
        if(!handle_file(file))
            return false;
    }
    return link();
}

int main(int argc, char* argv[]) {
    fmt::print("┌{0:─^{2}}┐\n"
               "│{1: ^{2}}│\n"
               "└{0:─^{2}}┘\n",
               "", "<insert language name> compiler.", 80);

    args.add('o', "out", 1, 1, "Specify the output file.");
    args.add('t', "tokens", 0, 0, "Dump the state after the tokenizing stage.");
    args.add('a', "ast", 0, 0, "Dump the parsed AST to the command line.");
    args.add('i', "ir", 0, 0, "Output LLVM Intermediate Representation.");
    args.add('r', "run", 0, 256, "Run the resulting executable.");
    args.add('b', "object", 0, 0, "Output an object file.");
    args.add('j', "jit", 0, 0, "Run the module using JIT.");
    args.add('w', "watch", 0, 0, "Watch the supplied file and re-run on changes.");
    args.parse(argc, argv);

    if(!args.has_default_args()) {
        error("No source file provided.\n");
        print("Usage: 'compiler path/to/source.lang'.\n");
        args.print_help();
        return -1;
    }

    auto r = handle_all();
    if(args['w'].set) {
        auto last_run = std::chrono::system_clock::now();

        auto rerun = [&](const std::string& path, const filewatch::Event) {
            if(std::chrono::system_clock::now() - last_run < std::chrono::seconds(1)) // Ignore double events
                return;
            fmt::print("[{:%T}] <insert lang name> compiler: {} changed, reprocessing...\n", std::chrono::system_clock::now(), path);
            // Wait a bit, at least on Windows it looks like the file read will fail if attempted right after the modification event.
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            r = handle_all(); // TODO: Recompile only modified files.
            last_run = std::chrono::system_clock::now();
            success("\n[{:%T}] Watching for changes... ", std::chrono::system_clock::now(), args.get_default_arg());
            fmt::print("(CTRL+C to exit)\n\n");
        };

        auto file_to_watch = args.get_default_arg();
        // Filewatch doesn't have a simple way to watch multiple files, we'll watch the parent directory for now.
        if(args.get_default_args().size() > 1) {
            file_to_watch = longest_common_prefix(args.get_default_args());
            file_to_watch = std::filesystem::path(file_to_watch).parent_path().string();
        }
        filewatch::FileWatch<std::string> watchers(file_to_watch, rerun);

        success("\n[{:%T}] Watching for changes... ", std::chrono::system_clock::now());
        fmt::print("(CTRL+C to exit)\n\n");
        // TODO: Provide a prompt?
        while(true)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return r;
}
