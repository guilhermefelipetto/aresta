#pragma once

#include <memory>
#include <string>

struct ExprNode;

// Uma célula do kernel por vez. x e y são deslocamentos em relação ao centro,
// r e t as mesmas coordenadas em polar, e a/b/c ficam livres pra quem escreve a
// fórmula amarrar num slider.
struct ExprVars {
    float x = 0.0f;
    float y = 0.0f;
    float r = 0.0f;
    float t = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    float a = 0.0f;
    float b = 0.0f;
    float c = 0.0f;
};

struct Expr {
    std::shared_ptr<ExprNode> root;
    std::string error;

    bool valid() const { return root != nullptr; }
    float eval(const ExprVars& vars) const;
};

Expr parse_expr(const std::string& text);
