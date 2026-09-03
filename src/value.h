#pragma once

#include <cstdint>
#include <memory>

#include "image.h"
#include "map.h"

enum class ValueKind { Color, Scalar, Label };

const char* kind_name(ValueKind kind);

// O que trafega entre os estágios da cadeia. Só um dos três está preenchido; a
// alternativa era std::variant, que apareceria em toda assinatura sem pagar
// nada em troca.
struct Value {
    ValueKind kind = ValueKind::Color;
    Image color;
    Map<float> scalar;
    Map<int32_t> label;

    int width() const;
    int height() const;
    bool empty() const;
    Value clone() const;
};

Value make_color(Image image);
Value make_scalar(Map<float> map);
Value make_label(Map<int32_t> map);

enum class Colormap { Gray, Viridis, Magma, Turbo, Hot };

const char* colormap_name(Colormap map);

// Escalar é normalizado pelo próprio intervalo, que volta em lo/hi pra UI poder
// mostrar contra o que a imagem foi normalizada.
std::unique_ptr<unsigned char[]> to_display_rgba8(const Value& value, Colormap colormap,
                                                  float* lo, float* hi);

// Escalar vira cor pelo mapa escolhido, virando estágio da cadeia em vez de só
// jeito de exibir.
Image pseudo_color(MapView<float> scalar, Colormap map);
