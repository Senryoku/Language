#include <AST.hpp>

#include <GlobalTypeRegistry.hpp>

AST::Node* AST::Node::add_child(Node* n) {
    assert(n->parent == nullptr);
    children.push_back(n);
    n->parent = this;
    return n;
}

AST::Node* AST::Node::add_child_front(Node* n) {
    assert(n->parent == nullptr);
    children.insert(children.begin(), n);
    n->parent = this;
    return n;
}

AST::Node* AST::Node::add_child_after(Node* n, const Node* prev) {
    assert(n->parent == nullptr);
    children.insert(++std::find(children.begin(), children.end(), prev), n);
    n->parent = this;
    return n;
}

AST::Node* AST::Node::add_child_before(Node* n, const Node* next) {
    assert(n->parent == nullptr);
    children.insert(std::find(children.begin(), children.end(), next), n);
    n->parent = this;
    return n;
}

AST::Node* AST::Node::pop_child() {
    AST::Node* c = children.back();
    children.pop_back();
    c->parent = nullptr;
    return c;
}

// Insert a node between this and its nth child
AST::Node* AST::Node::insert_between(size_t n, Node* node) {
    assert(node->children.size() == 0);
    children[n]->parent = nullptr;
    node->add_child(children[n]);
    children[n] = node;
    return node;
}

[[nodiscard]] AST::Scope* AST::Node::get_scope() {
    auto r = this;
    while(r && r->type != Type::Scope)
        r = r->parent;
    assert(r != nullptr);
    return dynamic_cast<Scope*>(r);
}

[[nodiscard]] const AST::Scope* AST::Node::get_scope() const {
    auto r = this;
    while(r && r->type != Type::Scope)
        r = r->parent;
    assert(r != nullptr);
    return dynamic_cast<const Scope*>(r);
}

[[nodiscard]] AST::Scope* AST::Node::get_root_scope() {
    auto r = get_scope();
    auto it = r;
    while(it) {
        r = it;
        it = it->get_parent_scope();
    }
    assert(r != nullptr);
    return r;
}

[[nodiscard]] const AST::Scope* AST::Node::get_root_scope() const {
    auto r = get_scope();
    auto it = r;
    while(it) {
        r = it;
        it = it->get_parent_scope();
    }
    assert(r != nullptr);
    return r;
}

auto mangle_name(const std::string_view& name, auto arguments, AST::FunctionDeclaration::Flag flags) {
    std::string r{name};
    // FIXME: Correctly handle manged names for variadic functions.
    if((flags & AST::FunctionDeclaration::Flag::Variadic) || (flags & AST::FunctionDeclaration::Flag::Extern) || (flags & AST::FunctionDeclaration::Flag::BuiltIn))
        return r;
    for(auto arg : arguments)
        r += std::string("_") + GlobalTypeRegistry::instance().get_type(arg->type_id)->designation;
    return r;
}

std::string AST::FunctionDeclaration::mangled_name() const {
    return mangle_name(token.value, arguments(), flags);
}

std::string AST::FunctionDeclaration::debug_name() const {
    std::string r(name());
    r += "(";
    for(int i = 0; i < arguments().size(); ++i) {
        r += GlobalTypeRegistry::instance().get_type(arguments()[i]->type_id)->designation;
        if(i < arguments().size() - 1)
            r += ", ";
    }
    r += ")";
    return r;
}

bool AST::FunctionDeclaration::is_templated() const {
    if(type_id != InvalidTypeID && GlobalTypeRegistry::instance().get_type(type_id)->is_placeholder())
        return true;
    for(const auto& arg : arguments())
        if(GlobalTypeRegistry::instance().get_type(arg->type_id)->is_placeholder())
            return true;
    return false;
}

std::string AST::FunctionCall::mangled_name() const {
    return mangle_name(token.value, arguments(), flags);
}

//////////////////////////////////////////////////////////////////
// Scope

AST::Scope* AST::Scope::get_parent_scope() {
    auto it = parent;
    while(it != nullptr && it->type != AST::Node::Type::Scope)
        it = it->parent;
    return dynamic_cast<AST::Scope*>(it);
}

const AST::Scope* AST::Scope::get_parent_scope() const {
    auto it = parent;
    while(it != nullptr && it->type != AST::Node::Type::Scope)
        it = it->parent;
    return dynamic_cast<const AST::Scope*>(it);
}

bool AST::Scope::declare_function(AST::FunctionDeclaration& node) {
    auto sv = node.token.value;
    if(resolve_function(sv, node.arguments()) != nullptr)
        return false;
    // TODO: Check & warn shadowing from other scopes?
    _functions[std::string{sv}].push_back(&node);
    return true;
}

bool AST::Scope::declare_type(AST::TypeDeclaration& node) {
    auto sv = node.token.value;
    if(find_type(sv) != InvalidTypeID)
        return false;
    auto r_type_id = GlobalTypeRegistry::instance().register_type(node);
    node.type_id = r_type_id;
    _types.emplace(std::string{sv}, r_type_id);
    return true;
}

bool AST::Scope::declare_template_placeholder_type(const std::string& name) {
    _template_placeholder_types.push_back(name);
    return true;
}

bool AST::Scope::declare_variable(AST::VariableDeclaration& decNode) {
    const auto& name = decNode.token.value;
    if(is_declared(name))
        return false;
    _variables.emplace(name, &decNode);
    _ordered_variable_declarations.push(&decNode);
    return true;
}

[[nodiscard]] TypeID AST::Scope::find_type(const std::string_view& name) const {
    auto it = _types.find(name);
    if(it != _types.end())
        return it->second;
    auto template_it = std::find(_template_placeholder_types.begin(), _template_placeholder_types.end(), name);
    if(template_it != _template_placeholder_types.end())
        return PlaceholderTypeID_Min + std::distance(_template_placeholder_types.begin(), template_it);
    return InvalidTypeID;
}

static void collect_declarations(AST::Scope* scope, AST::Node* node) {
    if(node->type == AST::Node::Type::VariableDeclaration)
        scope->declare_variable(*dynamic_cast<AST::VariableDeclaration*>(node));
    if(node->type == AST::Node::Type::FunctionDeclaration)
        scope->declare_function(*dynamic_cast<AST::FunctionDeclaration*>(node));

    for(const auto child : node->children)
        if(child->type != AST::Node::Type::Scope)
            collect_declarations(scope, child);
};

[[nodiscard]] AST::Scope* AST::Scope::clone() const {
    auto n = new Scope();
    clone_impl(n);

    // Not calling collect_declarations directly for fear of aliasing shenanigans.
    for(const auto child : n->children)
        if(child->type != Type::Scope)
            collect_declarations(n, child);

    n->_types = _types;
    n->_template_placeholder_types = _template_placeholder_types;

    auto ordered_var_dec = get_ordered_variable_declarations();
    // This is messy, we'll have to unstack it twice to get the same order ~~
    std::vector<AST::VariableDeclaration*> reversed_stack;
    while(!ordered_var_dec.empty()) {
        reversed_stack.push_back(ordered_var_dec.top());
        ordered_var_dec.pop();
    }
    for(auto it = reversed_stack.rbegin(); it != reversed_stack.rend(); ++it)
        n->_ordered_variable_declarations.push(*it);

    return n;
}

[[nodiscard]] const AST::FunctionDeclaration* AST::Scope::resolve_function(const std::string_view& name, const std::span<TypeID>& arguments) const {
    auto candidate_functions = _functions.find(name);
    if(candidate_functions == _functions.end())
        return nullptr;

    for(const auto function : candidate_functions->second) {
        // TODO: Correctly handle vargs functions
        if(!(function->flags & AST::FunctionDeclaration::Flag::Variadic)) {
            // TODO: Handle default values
            if(arguments.size() != function->arguments().size())
                continue;
            bool args_types_match = true;
            for(auto idx = 0; idx < arguments.size(); ++idx) {

                if(arguments[idx] != function->arguments()[idx]->type_id &&
                   // Allow casting to the generic 'pointer' type
                   (function->arguments()[idx]->type_id != PrimitiveType::Pointer || !GlobalTypeRegistry::instance().get_type(arguments[idx])->is_pointer())) {
                    args_types_match = false;
                    break;
                }
            }
            if(!args_types_match)
                continue;
        }
        // We found a match.
        return function;
    }
    return nullptr;
}

[[nodiscard]] const AST::FunctionDeclaration* AST::Scope::resolve_function(const std::string_view& name, const std::span<AST::Node*>& arguments) const {
    std::vector<TypeID> argument_types;
    for(auto c : arguments)
        argument_types.push_back(c->type_id);
    return resolve_function(name, argument_types);
}

[[nodiscard]] const AST::FunctionDeclaration* AST::Scope::get_function(const std::string_view& name, const std::span<TypeID>& arguments) const {
    auto it = this;
    while(it) {
        auto ret = it->resolve_function(name, arguments);
        if(ret)
            return ret;
        it = it->get_parent_scope();
    }
    return nullptr;
}

[[nodiscard]] const AST::FunctionDeclaration* AST::Scope::get_function(const std::string_view& name, const std::span<AST::Node*>& arguments) const {
    std::vector<TypeID> argument_types;
    for(auto c : arguments)
        argument_types.push_back(c->type_id);
    return get_function(name, argument_types);
}

std::vector<const AST::FunctionDeclaration*> AST::Scope::get_functions(const std::string_view& name) const {
    auto                                         it = this;
    std::vector<const AST::FunctionDeclaration*> r;
    while(it) {
        if(it->_functions.contains(name)) {
            const auto& candidates = it->_functions.at(std::string(name));
            r.insert(r.end(), candidates.begin(), candidates.end());
        }
        it = it->get_parent_scope();
    }
    return r;
}

TypeID AST::Scope::get_type(const std::string_view& name) const {
    auto it = this;
    auto r_type_id = it->find_type(name);
    while(it != nullptr && r_type_id == InvalidTypeID) {
        it = it->get_parent_scope();
        if(it)
            r_type_id = it->find_type(name);
    }
    // Search built-ins
    if(r_type_id == InvalidTypeID)
        r_type_id = GlobalTypeRegistry::instance().get_type_id(std::string{name});

    return r_type_id;
}

AST::VariableDeclaration* AST::Scope::get_variable(const std::string_view& name) {
    auto it = this;
    auto val = it->find(name);
    while(it != nullptr && !it->is_valid(val)) {
        it = it->get_parent_scope();
        if(it)
            val = it->find(name);
    }
    return it != nullptr && it->is_valid(val) ? val->second : nullptr;
}

const AST::VariableDeclaration* AST::Scope::get_variable(const std::string_view& name) const {
    auto it = this;
    auto val = it->find(name);
    while(it != nullptr && !it->is_valid(val)) {
        it = it->get_parent_scope();
        if(it != nullptr)
            val = it->find(name);
    }
    return it != nullptr && it->is_valid(val) ? val->second : nullptr;
}

bool AST::Scope::is_type(const std::string_view& name) const {
    return get_type(name) != InvalidTypeID;
}

const AST::VariableDeclaration* AST::Scope::get_this() const {
    auto it = this;
    while(it) {
        if(it->_this)
            return it->_this;
        it = it->get_parent_scope();
    }
    return nullptr;
}

AST::VariableDeclaration* AST::Scope::get_this() {
    auto it = this;
    while(it) {
        if(it->_this)
            return it->_this;
        it = it->get_parent_scope();
    }
    return nullptr;
}