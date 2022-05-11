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


## Dependencies
 - C++20
 - fmt

## WASM Resources
 - Spec https://webassembly.github.io/spec/core/index.html
 - Text Format Basics https://developer.mozilla.org/en-US/docs/WebAssembly/Understanding_the_text_format
 - WebAssembly Reference Manual https://github.com/sunfishcode/wasm-reference-manual/blob/master/WebAssembly.md
 - WABT: The WebAssembly Binary Toolkit https://github.com/webassembly/wabt
 - wat2wasm demo https://webassembly.github.io/wabt/demo/wat2wasm/