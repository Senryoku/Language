# Language

Toy Language.

Nothing, including the syntax, is specified. I only have some vague goals in mind (Statically typed, object oriented, close to C++ in spirit). Current syntax looks a lot like typescript, mainly because it's easy to parse :)

Working on a compiler using LLVM IR and clang to generate native executables.

## Features

### Syntax
  Examples in [test/compiler/](test/compiler/)

  - Primitive types
    - Integer `u8`...`u64`, `i8`...`i64`
    - Float `float`, `double`
    - Array (fixed size) of other Primitive Types `i32[32]`. Will be reworked.
    - C-String `cstr`, null-terminated to interface with C/C++
  - Basic Control Flow
<table>
<tr>
<td> Condition </td> <td> While Loop </td><td> For Loop </td>
</tr>
<tr>
<td>
	
```c
if(condition)
	single_statement;
else {
	...block
}
```
	
</td>
<td>
	
```c
while(condition_expression) {
	[...while_body]
}
```
	
</td>
<td>
	
```c
for(let i : i32 = 0; i < length; ++i) {
	[...for_body]
}
```
	
</td>
</tr>
</table>

  - Variable Declaration
```c
let variable_name : type = optional_initial_value;
let a : i32 = 0;
const b : float = 0.0;
const c = 0.0; // Type derived from the initial value
```

  - Function Declaration
```c
function name(param_0 : i32, param_1 : float) : i32 {
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

function main() : i32 {
	let z : Complex;
	z.i = 2;
	z.j = 3;
	let ret_val : i32 = z.magnitude();
	return ret_val;
}
```
  - Modules
```c
import "std/String"
import "other_module"

export type MyType {
	...
}

export function function_name() : MyType {
	...
}

// Exporting variables/values is not supported yet.
```
  - Templates (Generics)
```c
// On types
type Pair<T0, T1> {
    let lhs: T0;
    let rhs: T1;
}
// On functions
function Pair<T0, T1>(key: T0, value: T1) {
    let pair: Pair<T0, T1>;
    pair.lhs = key;
    pair.rhs = value;
    return pair;
}
```

## Todo
 - Everything
 - Design
   - Handle const-ness: Default to const for struct passed to functions, add a way to opt-in for mutability (espacially for 'member functions').
     Should it be part of the type system, C++ style? I'm used to it, but it seems pretty hard to implement (at least given the current type system implementation, which is probably far from optimal). I'm leaning to "Yes", just have to do it...
   - Rework function arguments and return values:
     Structs should be passed as const reference (i.e. a simple pointer) by default instead of "moved", unless they're explicitly marked as mutable (so this is kinda dependent on solving const-correctness first...). This will also remove the need of passing pointers for "this", it should 
     just be a reference, like all other arguments (no need for a syntatic marker, let the compiler handle this transparently), optionally mutable.
     Structs returned from functions are currently broken. I have to flip the responsability (in the LLVM codegen): Let the caller allocate memory on the stack and pass an additional, hidden, pointer to it to the function which will use it to store its return value. 
     This will simplify a lot of lifetime management.
   - Should we have constructors (other than the default one that is currently not implemented :))? Currently I use function with the same new as the type as a convention. And copy/move constructors? Can we get away with some sort of 'always move', and providing standardized clone/copy functions?
     - Currently variables are marked as 'moved' when returned from a function to avoid calling their destructors. This is obviously not enough, we'll need a more general mechanism to handle marking object as moved. There is also a problem with actually retrieving value returned by functions: Nothing prevents the called to ignore the return value and currently I'm pretty sure the destructor will never be called in this case. We need a better model for this, but I think I'm not the right track (It looks like the idea of 'always move unless explicitly cloning' is pretty much what Rust does, but I need to read more about that.)
     - In summary: I have to come up with a good ownership/lifetime model.
   - Heap allocation?
   - Think about UTF-8. Both in the language itself (the String class should probably be utf-8 by default), and the compiler implementation (inputs). 
 - Implementation
   - Generate default constructors (and call them) for types with default values.
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
   const array = [0, 1, 2, 3]; // Defaults to some kind of StaticArray<i32>
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
