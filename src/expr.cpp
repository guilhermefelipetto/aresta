#include "expr.h"

#include <cctype>
#include <cmath>
#include <numbers>
#include <vector>

struct ExprNode {
    enum Kind { Num, Var, Op, Call };

    Kind kind = Num;
    float value = 0.0f;
    int var_index = -1;
    std::string text;
    std::vector<std::shared_ptr<ExprNode>> kids;
};

namespace {

using Node = std::shared_ptr<ExprNode>;

int var_index_of(const std::string& name) {
    static const char* names[] = {"x", "y", "r", "t", "w", "h", "a", "b", "c", "v"};
    for (int i = 0; i < 10; ++i) {
        if (name == names[i]) {
            return i;
        }
    }
    return -1;
}

float var_value(int index, const ExprVars& v) {
    switch (index) {
        case 0: return v.x;
        case 1: return v.y;
        case 2: return v.r;
        case 3: return v.t;
        case 4: return v.w;
        case 5: return v.h;
        case 6: return v.a;
        case 7: return v.b;
        case 8: return v.c;
        default: return v.v;
    }
}

struct Parser {
    const std::string& text;
    std::size_t pos = 0;
    std::string error;

    explicit Parser(const std::string& source) : text(source) {}

    void skip() {
        while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
            ++pos;
        }
    }

    bool eat(const char* token) {
        skip();
        const std::size_t n = std::char_traits<char>::length(token);
        if (text.compare(pos, n, token) == 0) {
            pos += n;
            return true;
        }
        return false;
    }

    char peek() {
        skip();
        return pos < text.size() ? text[pos] : '\0';
    }

    Node make(ExprNode::Kind kind) {
        auto node = std::make_shared<ExprNode>();
        node->kind = kind;
        return node;
    }

    Node parse_expression() { return parse_comparison(); }

    Node parse_comparison() {
        Node left = parse_sum();
        while (left && !error.empty() == false) {
            const char* found = nullptr;
            for (const char* op : {"<=", ">=", "==", "!=", "<", ">"}) {
                if (eat(op)) {
                    found = op;
                    break;
                }
            }
            if (!found) {
                break;
            }
            Node right = parse_sum();
            if (!right) {
                return nullptr;
            }
            Node node = make(ExprNode::Op);
            node->text = found;
            node->kids = {left, right};
            left = node;
        }
        return left;
    }

    Node parse_sum() {
        Node left = parse_product();
        while (left) {
            const char c = peek();
            if (c != '+' && c != '-') {
                break;
            }
            ++pos;
            Node right = parse_product();
            if (!right) {
                return nullptr;
            }
            Node node = make(ExprNode::Op);
            node->text = std::string(1, c);
            node->kids = {left, right};
            left = node;
        }
        return left;
    }

    Node parse_product() {
        Node left = parse_unary();
        while (left) {
            const char c = peek();
            if (c != '*' && c != '/' && c != '%') {
                break;
            }
            ++pos;
            Node right = parse_unary();
            if (!right) {
                return nullptr;
            }
            Node node = make(ExprNode::Op);
            node->text = std::string(1, c);
            node->kids = {left, right};
            left = node;
        }
        return left;
    }

    Node parse_unary() {
        const char c = peek();
        if (c == '-' || c == '+') {
            ++pos;
            Node operand = parse_unary();
            if (!operand) {
                return nullptr;
            }
            if (c == '+') {
                return operand;
            }
            Node node = make(ExprNode::Op);
            node->text = "neg";
            node->kids = {operand};
            return node;
        }
        return parse_power();
    }

    Node parse_power() {
        Node base = parse_atom();
        if (!base) {
            return nullptr;
        }
        if (peek() == '^') {
            ++pos;
            // Direita pra esquerda: 2^3^2 é 2^(3^2).
            Node exponent = parse_unary();
            if (!exponent) {
                return nullptr;
            }
            Node node = make(ExprNode::Op);
            node->text = "^";
            node->kids = {base, exponent};
            return node;
        }
        return base;
    }

    Node parse_atom() {
        skip();
        if (pos >= text.size()) {
            error = "expressão termina cedo demais";
            return nullptr;
        }

        if (text[pos] == '(') {
            ++pos;
            Node inner = parse_expression();
            if (!inner) {
                return nullptr;
            }
            if (!eat(")")) {
                error = "falta fechar parêntese";
                return nullptr;
            }
            return inner;
        }

        if (std::isdigit(static_cast<unsigned char>(text[pos])) || text[pos] == '.') {
            std::size_t used = 0;
            const float value = std::stof(text.substr(pos), &used);
            pos += used;
            Node node = make(ExprNode::Num);
            node->value = value;
            return node;
        }

        if (std::isalpha(static_cast<unsigned char>(text[pos])) || text[pos] == '_') {
            const std::size_t start = pos;
            while (pos < text.size() &&
                   (std::isalnum(static_cast<unsigned char>(text[pos])) || text[pos] == '_')) {
                ++pos;
            }
            const std::string name = text.substr(start, pos - start);

            if (peek() == '(') {
                ++pos;
                Node node = make(ExprNode::Call);
                node->text = name;
                if (peek() != ')') {
                    while (true) {
                        Node arg = parse_expression();
                        if (!arg) {
                            return nullptr;
                        }
                        node->kids.push_back(arg);
                        if (peek() == ',') {
                            ++pos;
                            continue;
                        }
                        break;
                    }
                }
                if (!eat(")")) {
                    error = "falta fechar parêntese de " + name;
                    return nullptr;
                }
                return node;
            }

            if (name == "pi") {
                Node node = make(ExprNode::Num);
                node->value = std::numbers::pi_v<float>;
                return node;
            }
            if (name == "e") {
                Node node = make(ExprNode::Num);
                node->value = std::numbers::e_v<float>;
                return node;
            }

            const int index = var_index_of(name);
            if (index < 0) {
                error = "não conheço '" + name + "'";
                return nullptr;
            }
            Node node = make(ExprNode::Var);
            node->var_index = index;
            return node;
        }

        error = std::string("não esperava '") + text[pos] + "'";
        return nullptr;
    }
};

float eval_node(const ExprNode& node, const ExprVars& v) {
    switch (node.kind) {
        case ExprNode::Num:
            return node.value;
        case ExprNode::Var:
            return var_value(node.var_index, v);
        case ExprNode::Op: {
            if (node.text == "neg") {
                return -eval_node(*node.kids[0], v);
            }
            const float a = eval_node(*node.kids[0], v);
            const float b = eval_node(*node.kids[1], v);
            if (node.text == "+") return a + b;
            if (node.text == "-") return a - b;
            if (node.text == "*") return a * b;
            if (node.text == "/") return (b == 0.0f) ? 0.0f : a / b;
            if (node.text == "%") return (b == 0.0f) ? 0.0f : std::fmod(a, b);
            if (node.text == "^") return std::pow(a, b);
            if (node.text == "<") return a < b ? 1.0f : 0.0f;
            if (node.text == ">") return a > b ? 1.0f : 0.0f;
            if (node.text == "<=") return a <= b ? 1.0f : 0.0f;
            if (node.text == ">=") return a >= b ? 1.0f : 0.0f;
            if (node.text == "==") return a == b ? 1.0f : 0.0f;
            return a != b ? 1.0f : 0.0f;
        }
        case ExprNode::Call: {
            const std::size_t n = node.kids.size();
            const float a = n > 0 ? eval_node(*node.kids[0], v) : 0.0f;
            const float b = n > 1 ? eval_node(*node.kids[1], v) : 0.0f;
            const float c = n > 2 ? eval_node(*node.kids[2], v) : 0.0f;
            const std::string& f = node.text;
            if (f == "sin") return std::sin(a);
            if (f == "cos") return std::cos(a);
            if (f == "tan") return std::tan(a);
            if (f == "asin") return std::asin(a);
            if (f == "acos") return std::acos(a);
            if (f == "atan") return std::atan(a);
            if (f == "atan2") return std::atan2(a, b);
            if (f == "exp") return std::exp(a);
            if (f == "log") return a > 0.0f ? std::log(a) : 0.0f;
            if (f == "log2") return a > 0.0f ? std::log2(a) : 0.0f;
            if (f == "log10") return a > 0.0f ? std::log10(a) : 0.0f;
            if (f == "sqrt") return a > 0.0f ? std::sqrt(a) : 0.0f;
            if (f == "abs") return std::fabs(a);
            if (f == "floor") return std::floor(a);
            if (f == "ceil") return std::ceil(a);
            if (f == "round") return std::round(a);
            if (f == "sign") return (a > 0.0f) - (a < 0.0f);
            if (f == "min") return std::fmin(a, b);
            if (f == "max") return std::fmax(a, b);
            if (f == "pow") return std::pow(a, b);
            if (f == "hypot") return std::hypot(a, b);
            if (f == "step") return b >= a ? 1.0f : 0.0f;
            if (f == "clamp") return std::fmin(std::fmax(a, b), c);
            if (f == "if") return a != 0.0f ? b : c;
            if (f == "gauss") return b == 0.0f ? 0.0f : std::exp(-a * a / (2.0f * b * b));
            return 0.0f;
        }
    }
    return 0.0f;
}

const char* known_function(const std::string& name) {
    static const char* names[] = {"sin",  "cos",   "tan",  "asin", "acos",  "atan", "atan2",
                                  "exp",  "log",   "log2", "log10", "sqrt", "abs",  "floor",
                                  "ceil", "round", "sign", "min",  "max",   "pow",  "hypot",
                                  "step", "clamp", "if",   "gauss"};
    for (const char* candidate : names) {
        if (name == candidate) {
            return candidate;
        }
    }
    return nullptr;
}

bool check_calls(const ExprNode& node, std::string* error) {
    if (node.kind == ExprNode::Call && !known_function(node.text)) {
        *error = "não conheço a função '" + node.text + "'";
        return false;
    }
    for (const auto& kid : node.kids) {
        if (!check_calls(*kid, error)) {
            return false;
        }
    }
    return true;
}

}  // namespace

float Expr::eval(const ExprVars& vars) const {
    if (!root) {
        return 0.0f;
    }
    const float value = eval_node(*root, vars);
    return std::isfinite(value) ? value : 0.0f;
}

Expr parse_expr(const std::string& text) {
    Expr result;
    if (text.empty()) {
        result.error = "expressão vazia";
        return result;
    }

    Parser parser(text);
    Node root = parser.parse_expression();
    if (!root) {
        result.error = parser.error.empty() ? "expressão inválida" : parser.error;
        return result;
    }
    parser.skip();
    if (parser.pos != text.size()) {
        result.error = "sobrou '" + text.substr(parser.pos) + "'";
        return result;
    }
    std::string error;
    if (!check_calls(*root, &error)) {
        result.error = error;
        return result;
    }

    result.root = root;
    return result;
}
