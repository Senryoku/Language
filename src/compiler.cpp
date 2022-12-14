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
#include <fmt/color.h>
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
#include <llvm/Transforms/IPO/PassManagerBuilder.h>

#include <jit/LLVMJIT.hpp>

#include <string_utils.hpp>

CLIArg args;

const std::filesystem::path     cache_folder("./lang_cache/");
std::set<std::filesystem::path> input_files;
std::set<std::filesystem::path> object_files;

std::set<std::filesystem::path> processed_files; // Cleared at the start of a run, makes sure we don't end up in a loop.

bool handle_file(const std::filesystem::path& path);

// Returns true if some new files were processed
bool handle_file_dependencies(const ModuleInterface& module_interface) {
    bool r = false;
    for(const auto& dep : module_interface.dependencies) {
        auto dep_path = std::filesystem::absolute(module_interface.resolve_dependency(dep));
        // Make sure the dependencies are up-to-date.
        if(!input_files.contains(dep_path) && !processed_files.contains(dep_path)) {
            print("Found new dependency to {} (resolved to {}).\n", dep, dep_path.string());
            if(handle_file(dep_path))
                r = true;
        }
    }
    return r;
}

// Returns if new file were generated.
bool handle_file(const std::filesystem::path& path) {
    if(processed_files.contains(path))
        return false;
    auto filename = path.stem();

    auto cache_filename = ModuleInterface::get_cache_filename(path);
    auto o_filepath = cache_folder;
    o_filepath += cache_filename.replace_extension(".o");
    object_files.insert(o_filepath);
    if(!args['t'].set && !args['a'].set && !args['i'].set && !args['b'].set) {
        if(!args["bypass-cache"].set && std::filesystem::exists(o_filepath) && std::filesystem::last_write_time(o_filepath) > std::filesystem::last_write_time(path)) {
            print_subtle(" * Using cached compilation result for {}.\n", path.string());
            // We still need to check dependencies.
            ModuleInterface module_interface;
            module_interface.working_directory = path.parent_path();
            module_interface.import_module(o_filepath.replace_extension(".int"));
            if(!handle_file_dependencies(module_interface))
                return false;
            // Some dependencies were re-generated, continue processing anyway.
            print_subtle(" * * Cache for {} is outdated, re-process.\n", path.string());
        }
    }
    print("Processing {}... \n", path.string());
    const auto     total_start = std::chrono::high_resolution_clock::now();

    std ::ifstream input_file(path);
    if(!input_file) {
        error("[compiler::handle_file] Couldn't open file '{}' (Running from {}).\n", path.string(), std::filesystem::current_path().string());
        return false;
    }
    std::string source{(std::istreambuf_iterator<char>(input_file)), std::istreambuf_iterator<char>()};

    const auto tokenizing_start = std::chrono::high_resolution_clock::now();

    std::vector<Token> tokens;
    try {
        Tokenizer tokenizer(source);
        while(tokenizer.has_more())
            tokens.push_back(tokenizer.consume());
    } catch(const Exception& e) {
        e.display();
        return false;
    }

    const auto tokenizing_end = std::chrono::high_resolution_clock::now();

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

    const auto parsing_start = std::chrono::high_resolution_clock::now();
    Parser     parser;
    parser.get_module_interface().working_directory = path.parent_path();
    parser.set_source(source);
    parser.set_cache_folder(cache_folder);
    auto       ast = parser.parse(tokens);
    const auto parsing_end = std::chrono::high_resolution_clock::now();
    if(handle_file_dependencies(parser.get_module_interface())) {
        // Dependencies updated, re-process from the start.
        return handle_file(path);
    }
    if(ast.has_value()) {
        parser.write_export_interface(cache_filename.replace_extension(".int"));
        if(args['a'].set) {
            if(args['o'].set && args['o'].has_value()) {
                auto out = fmt::output_file(args['o'].value());
                out.print("{}", *ast);
                fmt::print("AST written to '{}'.\n", args['o'].value());
                std::system(fmt::format("cat \"{}\"", args['o'].value()).c_str());
                return false;
            } else
                fmt::print("{}", *ast);
        }

        try {
            const auto                         codegen_start = std::chrono::high_resolution_clock::now();
            std::unique_ptr<llvm::LLVMContext> llvm_context(new llvm::LLVMContext());
            Module                             new_module{path.string(), llvm_context.get()};
            new_module.codegen_imports(parser.get_module_interface().type_imports);
            new_module.codegen_imports(parser.get_module_interface().imports);
            auto result = new_module.codegen(*ast);
            if(!result) {
                error("LLVM Codegen returned nullptr.\n");
                return false;
            }
            if(llvm::verifyModule(new_module.get_llvm_module(), &llvm::errs())) {
                error("\nErrors in LLVM Module, exiting...\n");
                return false;
            }
            const auto codegen_end = std::chrono::high_resolution_clock::now();

            const auto write_ir_start = std::chrono::high_resolution_clock::now();
            if(args['i'].set) {
                auto ir_filepath = filename.replace_extension(".ll");
                if(args['o'].set)
                    ir_filepath = args['o'].value();
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

            const auto write_ir_end = std::chrono::high_resolution_clock::now();

            const auto object_gen_start = std::chrono::high_resolution_clock::now();

            // Generate Object file
            if(args['b'].set && args['o'].set)
                o_filepath = args['o'].value();

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

            std::error_code      error_code;
            llvm::raw_fd_ostream dest(o_filepath.string(), error_code, llvm::sys::fs::OF_None);

            if(error_code) {
                error("Could not open file '{}': {}.\n", o_filepath.string(), error_code.message());
                return false;
            }

            llvm::legacy::PassManager passManager;
            llvm::PassManagerBuilder  passManagerBuilder;
            auto                      file_type = llvm::CGFT_ObjectFile;
            passManagerBuilder.OptLevel = 3;
            passManagerBuilder.populateModulePassManager(passManager);

            if(target_machine->addPassesToEmitFile(passManager, dest, nullptr, file_type)) {
                error("Target Machine (Target Triple: {}) can't emit a file of this type.\n", target_triple);
                return false;
            }

            passManager.run(new_module.get_llvm_module());
            dest.flush();
            success("Wrote object file '{}' (Target Triple: {}).\n", o_filepath.string(), target_triple);
            if(args['b'].set)
                return true;

            if(args['j'].set) {
                // Quick Test JIT (TODO: Remove?)
                lang::LLVMJIT jit;
                auto          return_value = jit.run(std::move(new_module.get_llvm_module_ptr()), std::move(llvm_context));
                success("JIT main function returned '{}'\n", return_value);
                return true;
            }
            const auto object_gen_end = std::chrono::high_resolution_clock::now();
            const auto total_end = std::chrono::high_resolution_clock::now();

            print(" {:<12} | {:<12} | {:<12} | {:<12} | {:<12} | {:<12} \n", "Tokenizer", "Parser", "LLVMCodegen", "IR", "ObjectGen", "Total");
            print(" {:^12.2} | {:^12.2} | {:^12.2} | {:^12.2} | {:^12.2} | {:^12.2} \n", std::chrono::duration<double, std::milli>(tokenizing_end - tokenizing_start),
                  std::chrono::duration<double, std::milli>(parsing_end - parsing_start), std::chrono::duration<double, std::milli>(codegen_end - codegen_start),
                  std::chrono::duration<double, std::milli>(write_ir_end - write_ir_start), std::chrono::duration<double, std::milli>(object_gen_end - object_gen_start),
                  std::chrono::duration<double, std::milli>(total_end - total_start));
        } catch(const std::exception& e) {
            error("Exception: {}", e.what());
            return false;
        }
        processed_files.insert(path);
        return true;
    }
    return false;
}

// Returns true on success
bool link(const std::string& final_outputfile) {
    try {
        std::string cmd_input_files;
        for(const auto& file : object_files) {
            cmd_input_files += " \"" + file.string() + "\"";
        }
        const auto command = fmt::format("clang {} -flto {} -o \"{}\"", cmd_input_files, LANG_STDLIB_PATH, final_outputfile);
        print("Running '{}'\n", command);
        if(auto retval = std::system(command.c_str()); retval != 0) {
            error("Error running clang: {}.\n", retval);
            return false;
        }

    } catch(const std::exception& e) {
        error("Exception: {}", e.what());
        return false;
    }
    return true;
};

// Returns true on success
bool handle_all() {
    processed_files = {};
    const auto start = std::chrono::high_resolution_clock::now();
    // FIXME: We should generate a dependency tree to
    //  1. Pull all the required dependencies if they're not explicitly passed as argmument
    //  2. Recompile only the necessary files.
    //  3. Recompile in the correct order. (< Most important)
    bool file_generated = false;
    for(const auto& file : input_files)
        file_generated = handle_file(file) || file_generated;
    const auto clang_start = std::chrono::high_resolution_clock::now();
    auto       final_outputfile = args['o'].set ? args['o'].value() : input_files.size() == 1 ? (*input_files.begin()).filename().replace_extension(".exe").string() : "a.out";
    if(file_generated) {
        if(!link(final_outputfile))
            return false;
        const auto clang_end = std::chrono::high_resolution_clock::now();
        const auto end = std::chrono::high_resolution_clock::now();
        success("Compiled successfully to {} in {:.2} (clang: {:.2}).\n", final_outputfile, std::chrono::duration<double, std::milli>(end - start),
                std::chrono::duration<double, std::milli>(clang_end - clang_start));

        // Run the generated program. FIXME: Handy, but dangerous.
        if(args['r'].set) {
            auto run_command = final_outputfile;
            for(const auto& arg : args['r'].values)
                run_command += " " + arg;
            print("Running {}...\n", run_command);
            const auto execution_start = std::chrono::high_resolution_clock::now();
            auto       retval = std::system(run_command.c_str());
            const auto execution_end = std::chrono::high_resolution_clock::now();
            print("\n > {} returned {} after {}.\n", final_outputfile, retval, std::chrono::duration<double, std::milli>(execution_end - execution_start));
        }
    }
    return true;
}

int main(int argc, char* argv[]) {
    info("  █░░  <insert language name> compiler\n");
    info("  █▄▄  v0.0.1\n");

    args.add('o', "out", 1, 1, "Specify the output file.");
    args.add('t', "tokens", 0, 0, "Dump the state after the tokenizing stage.");
    args.add('a', "ast", 0, 0, "Dump the parsed AST to the command line.");
    args.add('i', "ir", 0, 0, "Output LLVM Intermediate Representation.");
    args.add('r', "run", 0, 256, "Run the resulting executable.");
    args.add('b', "object", 0, 0, "Output an object file.");
    args.add('j', "jit", 0, 0, "Run the module using JIT.");
    args.add('w', "watch", 0, 0, "Watch the supplied file and re-run on changes.");
    args.add('n', "bypass-cache", 0, 0, "Ignore the cache generated by previous invocations.");
    args.parse(argc, argv);

    if(!args.has_default_args()) {
        error("No source file provided.\n");
        print("Usage: 'compiler path/to/source.lang'.\n");
        args.print_help();
        return -1;
    }

    if(!std::filesystem::exists(cache_folder))
        std::filesystem::create_directory(cache_folder);

    for(const auto& arg : args.get_default_args())
        input_files.insert(std::filesystem::absolute(std::filesystem::path(arg)));

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
            success("\n[{:%T}] Watching for changes... ", std::chrono::system_clock::now());
            fmt::print("(CTRL+C to exit)\n\n");
        };

        auto file_to_watch = (*input_files.begin()).string();
        // Filewatch doesn't have a simple way to watch multiple files, we'll watch the parent directory for now.
        if(input_files.size() > 1) {
            std::vector<std::string> paths;
            std::transform(input_files.begin(), input_files.end(), std::back_inserter(paths), [](const auto& p) { return p.string(); });
            file_to_watch = longest_common_prefix(paths);
            file_to_watch = std::filesystem::path(file_to_watch).parent_path().string();
        }
        filewatch::FileWatch<std::string> watchers(file_to_watch, rerun);

        success("\n[{:%T}] Watching for changes... ", std::chrono::system_clock::now());
        fmt::print("(CTRL+C to exit)\n\n");
        // TODO: Provide a prompt?
        while(true)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return r ? 0 : 1;
}
