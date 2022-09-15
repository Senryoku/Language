# Language

Toy Language.
Nothing, including the syntax, is specified. I only have some vague goals in mind.
Features a basic an interpreter that may not be maintained for long.
Working on a compiler using LLVM IR and clang to generate native executables.

## Features

### Syntax
  - Primitive types
    - Integer `int`
    - Float `float`
    - Array (fixed size) of other Primitive Types `int[32]`
  - Variable Declaration
```
let variable_name : type = optional_initial_value;
let a : int = 0;
let b : float = 0.0;
```
  - Branch
```
if(condition)
	single_statement;
else {
	...block
}
```
  - While Loop
```
while(condition_expression) {
	[...while_body]
}
```
  - Function Declaration
```
function name(param_0 : int, param_1 : float) : int {
	[...function body]
}
```
  - Modules
```
import module

export function function_name() {
	...
}
```

## Todo
 - Everything
 - [] and () should be proper binary operators, not weird special cases.
   - Rewrite builtin Arrays with this in mind.
 - String interning for keyword and symbols

## Future Features (Hopefully)
 - String templating
 - Templates (by 'auto'?); Concepts?
 - Reflection/Built in Serialisation

## Build

Only tested using MSVC. Make sure the build type matches the build type of your copy of LLVM (and use LLVM_ROOT if it's not automatically found).
 - `cmake -B ./build -DCMAKE_BUILD_TYPE=Release -DLLVM_ROOT="./LLVM"`
 - `cmake --build ./build --config Release`

## Tests

`cd build && ctest`

## Dependencies
 - C++20
 - fmt 9.1.0 (sources in ext/)
 - [Filewatch](https://github.com/ThomasMonkman/filewatch) (included in ext/)
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
