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
 - fmt 8.1.1 (sources in ext/)
 - LLVM 14.0.0 (for the compiler only)
   - Get the source tarball
   - cd llvm-14.0.0.src
   - mkdir build
   - cd build
   - cmake .. -DLLVM_INCLUDE_BENCHMARKS=OFF
   - cmake --build . --target install
   - CMake should now find LLVM automatically, otherwise set the LLVM_DIR variable (-DLLVM_DIR=path/to/llvm-14.0.0.src/build)
   - Make sure llc and clang are available in PATH

## Notes

### WASM Resources
 - Spec https://webassembly.github.io/spec/core/index.html
 - Text Format Basics https://developer.mozilla.org/en-US/docs/WebAssembly/Understanding_the_text_format
 - WebAssembly Reference Manual https://github.com/sunfishcode/wasm-reference-manual/blob/master/WebAssembly.md
 - WABT: The WebAssembly Binary Toolkit https://github.com/webassembly/wabt
 - wat2wasm demo https://webassembly.github.io/wabt/demo/wat2wasm/