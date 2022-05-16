#pragma once

#include <llvm/ADT/StringRef.h>
#include <llvm/ExecutionEngine/JITSymbol.h>
#include <llvm/ExecutionEngine/Orc/CompileUtils.h>
#include <llvm/ExecutionEngine/Orc/Core.h>
#include <llvm/ExecutionEngine/Orc/ExecutionUtils.h>
#include <llvm/ExecutionEngine/Orc/IRCompileLayer.h>
#include <llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h>
#include <llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h>
#include <llvm/ExecutionEngine/SectionMemoryManager.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/LLVMContext.h>
/*
namespace lang {

class JIT {
  public:
    JIT(llvm::orc::JITTargetMachineBuilder JTMB, llvm::DataLayout DL)
        : _object_layer(_execution_session, []() { return std::make_unique<llvm::SectionMemoryManager>(); }),
          _compile_layer(_execution_session, _object_layer, llvm::orc::ConcurrentIRCompiler(std::move(JTMB))), _data_layout(std::move(DL)),
          _mangle(_execution_session, this->_data_layout), _context(std::make_unique<llvm::LLVMContext>()) {
        _execution_session.getMainJITDylib().addGenerator(llvm::cantFail(llvm::orc::DynamicLibrarySearchGenerator::GetForCurrentProcess(_data_layout.getGlobalPrefix())));
    }

  private:
    llvm::orc::ExecutionSession         _execution_session;
    llvm::orc::RTDyldObjectLinkingLayer _object_layer;
    llvm::orc::IRCompileLayer           _compile_layer;

    llvm::DataLayout             _data_layout;
    llvm::orc::MangleAndInterner _mangle;
    llvm::orc::ThreadSafeContext _context;
};

} // namespace lang
*/
