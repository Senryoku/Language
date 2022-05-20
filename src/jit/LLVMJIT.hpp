#pragma once

#include "llvm/Support/TargetSelect.h"
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/Transforms/Utils/Cloning.h>

namespace lang {

class LLVMJIT {
  public:
    LLVMJIT() {
        // Try to detect the host arch and construct an LLJIT instance.
        llvm::InitializeNativeTarget();
        llvm::InitializeNativeTargetAsmPrinter();
        _jit = _exit_on_err(llvm::orc::LLJITBuilder().create());
    }

    int run(std::unique_ptr<llvm::Module> llvm_module, std::unique_ptr<llvm::LLVMContext> llvm_context) {
        // Add the module.
        _exit_on_err(_jit.get()->addIRModule(llvm::orc::ThreadSafeModule(std::move(llvm_module), std::move(llvm_context))));
        // Look up the JIT'd code entry point.
        auto EntrySym = _exit_on_err(_jit.get()->lookup("main"));
        // Cast the entry point address to a function pointer.
        auto* Entry = (int (*)())EntrySym.getAddress();
        // Call into JIT'd code.
        auto return_value = Entry();
        return return_value;
    }

  private:
    std::unique_ptr<llvm::orc::LLJIT> _jit;

    inline static llvm::ExitOnError _exit_on_err;
};

} // namespace lang
