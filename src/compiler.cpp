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
#include <fmt/std.h>

#include <Parser.hpp>
#include <Tokenizer.hpp>
#include <compiler/DependencyTree.hpp>
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
std::set<std::filesystem::path> input_files;     // Original files passed to the CLI.
std::set<std::filesystem::path> object_files;    // List of all generated object files for linking.

std::set<std::filesystem::path> processed_files; // Cleared at the start of a run, makes sure we don't end up in a loop. FIXME: Shouldn't be useful anymore.

// Returns true on success
bool handle_file(const std::filesystem::path& path) {
    if(processed_files.contains(path))
        return true;
    if(!std::filesystem::exists(path)) {
        throw Exception(fmt::format("Requested file {} does not exist.", path));
    }
    auto filename = path.stem();

    auto cache_filename = ModuleInterface::get_cache_filename(path);
    auto o_filepath = cache_folder;
    o_filepath += cache_filename.replace_extension(".o");
    if(!args['t'].set && !args['a'].set && !args['i'].set && !args['b'].set) {
        if(!args["bypass-cache"].set && std::filesystem::exists(o_filepath)) {
            const auto o_file_last_write = std::filesystem::last_write_time(o_filepath);
            if(o_file_last_write > std::filesystem::last_write_time(path)) {
                print_subtle(" * Using cached compilation result for {}.\n", path.string());
                // Check if any dependency is newer than our cached result.
                ModuleInterface module_interface;
                module_interface.working_directory = path.parent_path();
                module_interface.import_module(o_filepath.replace_extension(".int"));
                bool updated_deps = false;
                for(const auto& dep : module_interface.dependencies) {
                    auto dep_path = std::filesystem::absolute(module_interface.resolve_dependency(dep));
                    if(o_file_last_write < std::filesystem::last_write_time(dep_path)) {
                        updated_deps = true;
                        break;
                    }
                }
                if(!updated_deps) {
                    object_files.insert(o_filepath);
                    processed_files.insert(path);
                    return true;
                }
                // Some dependencies were re-generated, continue processing anyway.
                print_subtle(" * * Cache for {} is outdated, re-processing...\n", path.string());
            }
        }
    }
    print("Processing {}... \n", path.string());
    const auto total_start = std::chrono::high_resolution_clock::now();

    std ::ifstream input_file(path);
    if(!input_file)
        throw Exception(fmt::format("[compiler::handle_file] Couldn't open file '{}' (Running from {}).\n", path.string(), std::filesystem::current_path().string()));
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
    if(ast.has_value()) {
        parser.write_export_interface(cache_filename.replace_extension(".int"));
        if(args['a'].set) {
            if(args['o'].set && args['o'].has_value()) {
                auto out = fmt::output_file(args['o'].value());
                out.print("{}", *ast);
                fmt::print("AST written to '{}'.\n", args['o'].value());
                std::system(fmt::format("cat \"{}\"", args['o'].value()).c_str());
                return true;
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
                warn("LLVM Codegen returned nullptr. No object file generated for '{}'.\n", path);
                processed_files.insert(path);
                return true;
            }
            if(llvm::verifyModule(new_module.get_llvm_module(), &llvm::errs()))
                throw Exception("\nErrors in LLVM Module.\n");
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
                else
                    throw Exception(fmt::format("Error opening '{}': {}\n", ir_filepath.string(), err));
                success("LLVM IR written to {}.\n", ir_filepath.string());
                if(args['i'].set)
                    return true;
            }
            if(args['l'].set) {
#ifndef NDEBUG
                new_module.get_llvm_module().dump();
#else
                warn("[compiler] LLVM Module dump is only available in debug builds.");
#endif
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
            if(!target)
                throw Exception(fmt::format("Could not lookup target: {}.\n", error_str));
            auto cpu = "generic";
            auto features = "";

            llvm::TargetOptions opt;
            auto                reloc_model = llvm::Optional<llvm::Reloc::Model>();
            auto                target_machine = target->createTargetMachine(target_triple, cpu, features, opt, reloc_model);

            new_module.get_llvm_module().setDataLayout(target_machine->createDataLayout());
            new_module.get_llvm_module().setTargetTriple(target_triple);

            std::error_code      error_code;
            llvm::raw_fd_ostream dest(o_filepath.string(), error_code, llvm::sys::fs::OF_None);

            if(error_code)
                throw Exception(fmt::format("Could not open file '{}': {}.\n", o_filepath.string(), error_code.message()));

            llvm::legacy::PassManager passManager;
            llvm::PassManagerBuilder  passManagerBuilder;
            auto                      file_type = llvm::CGFT_ObjectFile;
            passManagerBuilder.OptLevel = 3;
            passManagerBuilder.PrepareForLTO = true;
            passManagerBuilder.populateModulePassManager(passManager);

            if(target_machine->addPassesToEmitFile(passManager, dest, nullptr, file_type)) {
                error("Target Machine (Target Triple: {}) can't emit a file of this type.\n", target_triple);
                return false;
            }

            passManager.run(new_module.get_llvm_module());
            object_files.insert(o_filepath);
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

            print(" {:<12} | {:<12} | {:<12} | {:<12} | {:<12} | {:<12} \n", fmt::styled("Tokenizer", fmt::fg(fmt::color::aquamarine)),
                  fmt::styled("Parser", fmt::fg(fmt::color::aquamarine)), fmt::styled("LLVMCodegen", fmt::fg(fmt::color::aquamarine)),
                  fmt::styled("IR", fmt::fg(fmt::color::aquamarine)), fmt::styled("ObjectGen", fmt::fg(fmt::color::aquamarine)),
                  fmt::styled("Total", fmt::fg(fmt::color::aquamarine)));
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
        const auto command = fmt::format("clang {} -flto \"{}\" -o \"{}\"", cmd_input_files, LANG_STDLIB_PATH, final_outputfile);
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
    const auto dependency_start = std::chrono::high_resolution_clock::now();

    DependencyTree dependency_tree;
    for(const auto& path : input_files)
        if(!dependency_tree.construct(path))
            return false;

    auto processing_stages_or_error = dependency_tree.generate_processing_stages();
    if(processing_stages_or_error.is_error()) {
        error(processing_stages_or_error.get_error().string());
        return false;
    }
    auto processing_stages = processing_stages_or_error.get();

    const auto dependency_end = std::chrono::high_resolution_clock::now();
    success("Generated dependency tree in {:.2}.\n", std::chrono::duration<double, std::milli>(dependency_end - dependency_start));

    processed_files = {};
    const auto start = std::chrono::high_resolution_clock::now();

    // TODO: We could parallelize here. (GlobalTypeRegister will be a problem.)
    for(const auto& stage : processing_stages)
        for(const auto& file : stage)
            if(!handle_file(file))
                return false;

    const auto clang_start = std::chrono::high_resolution_clock::now();
    auto       final_outputfile = args['o'].set ? args['o'].value() : input_files.size() == 1 ? (*input_files.begin()).filename().replace_extension(".exe").string() : "a.out";

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

    return true;
}

int main(int argc, char* argv[]) {
    info("  █░░  <insert language name> compiler\n");
    info("  █▄▄  v0.0.1\n");

    args.add('o', "out", 1, 1, "Specify the output file.");
    args.add('t', "tokens", 0, 0, "Dump the state after the tokenizing stage.");
    args.add('a', "ast", 0, 0, "Dump the parsed AST to the command line.");
    args.add('l', "llvm-ir", 0, 0, "Dump the LLVM IR to the command line.");
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

    for(const auto& arg : args.get_default_args()) {
        const auto abs_path = std::filesystem::absolute(std::filesystem::path(arg));
        input_files.insert(abs_path);
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
