# Language

Toy Language.

Nothing, including the syntax, is specified. I only have some vague goals in mind (Statically typed, object oriented, close to C++ in spirit). Current syntax looks a lot like typescript, mainly because it's easy to parse :)

Working on a compiler using LLVM IR and clang to generate native executables.

## Features

### Syntax
  Examples in [test/compiler/](test/compiler/)

  - Primitive types
    - Integer `int` (`u8`...`u64`, `i8`...`i64`)
    - Float `float`, `double`
    - Array (fixed size) of other Primitive Types `int[32]`. Will be reworked.
    - C-String `cstr`, null-terminated to interface with C/C++
  - Variable Declaration
```c
let variable_name : type = optional_initial_value;
let a : int = 0;
const b : float = 0.0;
const c = 0.0; // Type derived from the initial value
```
  - Branch
```c
if(condition)
	single_statement;
else {
	...block
}
```
  - While Loop
```c
while(condition_expression) {
	[...while_body]
}
```
  - Function Declaration
```c
function name(param_0 : int, param_1 : float) : int {
	[...function body]
}
```
  - Type Declaration
```c
type Complex {
	let i : float = 0;
	let j : float = 0;
}

// Uniform Function Call Syntax (UFCS)
function magnitude(this: Complex*) {
	return sqrt(.i * .i + .j * .j); // Actually, there's no sqrt() yet
}

function main() : int {
	let z : Complex;
	z.i = 2;
	z.j = 3;
	let ret_val : int = z.magnitude();
	return ret_val;
}
```
  - Modules
```c
import "std/String"
import "module"

export function function_name() : cstr {
	...
}
```

## Todo
 - Everything
 - Design
   - Handle const-ness: Default to const for struct passed to functions, add a way to opt-in for mutability (espacially for 'member functions').
   - Should we have constructors (other than the default one that is currently not implemented :)) )? And copy/move constructors? Can we get away with some sort of 'always move', and providing standardized clone/copy functions?
   - Heap allocation?
 - Implementation
   - Generate default constructors (and call them) for types with default values.
   - Re-think destructors calls (They're called as soon as the declared variable is out of scope, even if it's returned from a function, for example.)
   - Handle returning structs from a function in general.
   - [] and () should be proper binary operators, not weird special cases.
     - Rewrite builtin Arrays with this in mind.
   - String interning for keyword and symbols

## Future Features (Far, far away)
 - Template Strings
   ```c
   // Not sure of the format. I kinda like python's format strings.
   const str = f"Key: {key}, Value: {value}\n";
   ```
 - Language support for array / dictionnary
   ```c
   const array = [0, 1, 2, 3]; // Defaults to some kind of StaticArray<int>
   let dyn_array : Array<String> = ["A String", "Another One"];
   const dict = {key0: value0, key1: value1}; // Defaults to some kind of Dict<KeyType, ValueType>
   // Both keys and values of any type (hashable for the key), but of a single on for each.
   ```
 - Templates / Concepts?
 - Reflection/Built in Serialisation

## Build

Only tested using MSVC. Make sure the build type matches the build type of your copy of LLVM (and use LLVM_ROOT if it's not automatically found).
 - `cmake -B ./build -DCMAKE_BUILD_TYPE=Release -DLLVM_ROOT="./LLVM"`
 - `cmake --build ./build --config Release`

## Tests

`cd build && ctest -C Debug`

## Dependencies
 - C++20
 - [fmt 9.1.0](https://fmt.dev/9.1.0/) (sources in ext/)
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
