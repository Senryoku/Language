#pragma once

#include <Logger.hpp>
#include <AST.hpp>

Indenter wasm_out{2};

std::string to_wasm_type(GenericValue::Type type) {
    switch(type) {
        case GenericValue::Type::Integer: return "i32";
        case GenericValue::Type::Boolean: return "i32";
        // case GenericValue::Type::Float: return "f32";
        default: {
            error("[WASMCompiler] Unimplemented GenericType:{}\n", type);
            assert(false);
        }
    }
    return "[InvalidType]";
}

void generate_wasm_s_expression(const AST::Node& n) {
    switch(n.type) {
        using enum AST::Node::Type;
        case Root: {
            // TODO: Top-level declaration should be global?
            //       Nested function doesn't exist in WASM afaik
            //       Global initialization in IIFE?

            wasm_out.print("(module\n");
            wasm_out.group();
            wasm_out.print(";;(import \"console\" \"log\" (func $print(param i32)))\n"); // FIXME
            // Some help function for arrays managment (See https://openhome.cc/eGossip/WebAssembly/Array.html)
            // First i32 points to the first free bytes, then each array starts with its size (i32) immediatly followed by its elements.
            wasm_out.print(R"(
;; ----------------------------------------------------------------------------
(memory 1) ;; Allocate 64 KiB
(data (i32.const 0) "\04") ;; Reserve The first i32 as a pointer to available memory

(func $create_array (param $len i32) (result i32)
    (local $offset i32)                              ;; offset
    (set_local $offset (i32.load (i32.const 0)))     ;; load offset from the first i32

    (i32.store (get_local $offset)                   ;; load the length
               (get_local $len)
    ) 

    (i32.store (i32.const 0)                         ;; store offset of available space                   
               (i32.add 
                   (i32.add
                       (get_local $offset)
                       (i32.mul 
                           (get_local $len) 
                           (i32.const 4)
                       )
                   )
                   (i32.const 4)                     ;; the first i32 is the length
               )
    )
    (get_local $offset)                              ;; (return) the beginning offset of the array.
)

(func $length (param $arr i32) (result i32)
    (i32.load (get_local $arr))
)

(func $offset (param $arr i32) (param $i i32) (result i32)
    (i32.add
         (i32.add (get_local $arr) (i32.const 4))    ;; The first i32 is the array length 
         (i32.mul (i32.const 4) (get_local $i))      ;; one i32 is 4 bytes
    )
)

;; set a value at the index 
(func $set (param $arr i32) (param $i i32) (param $value i32)
    (i32.store 
        (call $offset (get_local $arr) (get_local $i)) 
        (get_local $value)
    ) 
)
;; get a value at the index 
(func $get (param $arr i32) (param $i i32) (result i32)
    (i32.load 
        (call $offset (get_local $arr) (get_local $i)) 
    )
)
;; ----------------------------------------------------------------------------
)");
            for(auto c : n.children)
                generate_wasm_s_expression(*c);
            wasm_out.end();
            // Shove global init here?
            // wasm_out.print("(func $main) (start $main))\n");
            wasm_out.print(")\n");
            break;
        }
        case Scope: {
            // FIXME?
            for(auto c : n.children)
                generate_wasm_s_expression(*c);
            break;
        }
        case IfStatement: {
            wasm_out.print("(if \n");
            wasm_out.group();
            generate_wasm_s_expression(*n.children[0]);
            wasm_out.print("(then \n");
            wasm_out.group();
            generate_wasm_s_expression(*n.children[1]);
            wasm_out.end();
            wasm_out.print(")\n");
            wasm_out.end();
            wasm_out.print(")\n");
            break;
        }
        case WhileStatement: {
            wasm_out.print("(block\n");
            wasm_out.group();
            wasm_out.print("(loop ;; While\n");
            wasm_out.group();
            // Branch out if the condition is NOT met
            wasm_out.print("(br_if 1 (i32.eqz\n");
            wasm_out.group();
            generate_wasm_s_expression(*n.children[0]);
            wasm_out.end();
            wasm_out.print("))\n");
            generate_wasm_s_expression(*n.children[1]);
            wasm_out.print("(br 0) ;; Jump to While\n");
            wasm_out.end();
            wasm_out.print(")\n");
            wasm_out.end();
            wasm_out.print(")\n");
            break;
        }
        case ReturnStatement: {
            // TODO: Some static check for a single value on the stack?
            wasm_out.print("(return\n");
            wasm_out.group();
            for(auto c : n.children)
                generate_wasm_s_expression(*c);
            wasm_out.end();
            wasm_out.print(")\n");
            break;
        }
        case ConstantValue: {
            wasm_out.print("({}.const {})\n", to_wasm_type(n.value.type), n.value.value.as_int32_t); // FIXME: Switch on type.
            break;
        }
        case FunctionDeclaration: {
            wasm_out.print("(func ${}", n.token.value);
            for(size_t i = 0; i < n.children.size() - 1; ++i)
                wasm_out.print_same_line(" (param ${} {})", n.children[i]->token.value, to_wasm_type(n.children[i]->value.type));
            if(n.value.type != GenericValue::Type::Undefined)
                wasm_out.print_same_line(" (result {})", to_wasm_type(n.value.type));
            wasm_out.print_same_line("\n");
            wasm_out.group();
            generate_wasm_s_expression(*n.children.back());
            wasm_out.end();
            wasm_out.print(") (export \"{}\" (func ${}))\n", n.token.value, n.token.value);
            break;
        }
        case FunctionCall: {
            wasm_out.print("(call ${}\n", n.token.value);
            wasm_out.group();
            for(auto c : n.children)
                generate_wasm_s_expression(*c);
            wasm_out.end();
            wasm_out.print(")\n");
            break;
        }
        case BinaryOperator: {
            auto simple_op = [&](const std::string_view& op) {
                wasm_out.print("({}.{}\n", to_wasm_type(n.value.type), op);
                wasm_out.group();
                for(auto c : n.children)
                    generate_wasm_s_expression(*c);
                wasm_out.end();
                wasm_out.print(")\n");
            };
            if(n.token.value.length() == 1) {
                switch(n.token.value[0]) {
                    case '=': {
                        // TODO: We assume lhs is just a variable here.
                        if(n.children[0]->value.type == GenericValue::Type::Array) {
                            wasm_out.print("(call $set (local.get ${}) \n", n.children[0]->token.value);
                            wasm_out.group();
                            generate_wasm_s_expression(*n.children[0]->children[0]); // Index
                            generate_wasm_s_expression(*n.children[1]);              // Value
                            wasm_out.end();
                            wasm_out.print(")\n");
                        } else {
                            wasm_out.print("(local.set ${}\n", n.children[0]->token.value);
                            wasm_out.group();
                            generate_wasm_s_expression(*n.children[1]);
                            wasm_out.end();
                            wasm_out.print(")\n");
                        }
                        break;
                    }
                    case '+': simple_op("add"); break;
                    case '-': simple_op("sub"); break;
                    case '*': simple_op("mul"); break;
                    case '/': simple_op("div_s"); break;
                    case '<': simple_op("lt_s"); break;
                    case '>': simple_op("gt_s"); break;
                    default: {
                        error("[WASMCompiler] Unimplemented BinaryOperator {}.\n", n.token);
                        return;
                    }
                }
            } else if(n.token.value == "==") {
                simple_op("eq");
                break;
            } else {
                error("[WASMCompiler] Unimplemented BinaryOperator {}.\n", n.token);
                return;
            }
            break;
        }
        case VariableDeclaration: {
            // TODO: Reorganize local variable declaration to be on top of function
            if(n.value.type == GenericValue::Type::Array) {
                wasm_out.print("(local ${} i32)\n", n.token.value);
                wasm_out.print("(local.set ${} (call $create_array ({}.const {})))\n", n.token.value, to_wasm_type(n.value.value.as_array.type), n.value.value.as_array.capacity);
            } else {
                wasm_out.print("(local ${} {})\n", n.token.value, to_wasm_type(n.value.type));
            }
            break;
        }
        case Variable: {
            if(n.value.type == GenericValue::Type::Array) {
                if(n.children.size() == 0) {
                    error("[WASMCompiler] TODO! Direct access to Array (non-indexed).");
                    return;
                }
                wasm_out.print("(call $get (local.get ${}", n.token.value);
                wasm_out.group();
                generate_wasm_s_expression(*n.children[0]);
                wasm_out.end();
                wasm_out.print(")\n ");
            } else {
                wasm_out.print("(local.get ${})\n", n.token.value);
            }
            break;
        }
        default: {
            error("[WASMCompiler] Node type {} unimplemented.\n", n.type);
            return;
        }
    }
}

void generate_wasm_s_expression(const AST& ast) {
    generate_wasm_s_expression(ast.getRoot());
}