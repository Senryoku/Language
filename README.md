# Language
Toy Language

## Todo
 - Everything
 - [] and () should be proper binary operators, not weird special cases.
   - Rewrite builtin Arrays with this in mind.
 - Function return type. Part of a function declaration?
 - Boolean operators (== != < > <= >=)
 - String interning for keyword and symbols

## Features

### Syntax
  - Primitive types
  - - Integer
  - - Float
  - - Array of other Primitive Types
  - Variable Declaration
  ```
  type variable_name = optional_initial_value;
  int a = 0;
  float b = 0.0;
  ```
  - While Loop
  ```
  while(condition_expression) {
		[...while_body]
  }
  ```
  - Function Declaration (Still missing return type)
  ```
  function name(int param_0, float param_1) {
		[...function body]
  }
  ```

## Future Features (Hopefully)
 - Easy module import (TBD :])
 - String templating
 - Templates (by 'auto'?); Concepts?
 - Reflection

## Dependencies
 - C++20
 - fmt 9.1.0 (sources in ext/)
 - LLVM 14.0.6 (for the compiler only)
   - On Windows we'll have to compile from source or CMake won't find it.
	   - Get the [source tarball](https://github.com/llvm/llvm-project/releases/download/llvmorg-14.0.6/llvm-14.0.6.src.tar.xz)
	   - `cd llvm-14.0.6.src`
	   - `mkdir build`
	   - `cd build`
	   - `cmake .. -DCMAKE_BUILD_TYPE=RELEASE -DLLVM_INCLUDE_BENCHMARKS=OFF`
	   - `cmake --build . --target install --config Release` (May necessitate a terminal with elevated privileges for installation)
		 - Wait.
	   - CMake should now find LLVM automatically, otherwise set the LLVM_ROOT variable (-DLLVM_ROOT=path/to/llvm-14.0.6.src/build)
   - Also make sure clang is available in PATH for the final compilation step.

## Notes

### WASM Resources
 - Spec https://webassembly.github.io/spec/core/index.html
 - Text Format Basics https://developer.mozilla.org/en-US/docs/WebAssembly/Understanding_the_text_format
 - WebAssembly Reference Manual https://github.com/sunfishcode/wasm-reference-manual/blob/master/WebAssembly.md
 - WABT: The WebAssembly Binary Toolkit https://github.com/webassembly/wabt
 - wat2wasm demo https://webassembly.github.io/wabt/demo/wat2wasm/