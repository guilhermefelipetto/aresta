#pragma once

#include <memory>
#include <string>

#include "map.h"

// RGBA intercalado, float32, espaço linear. O stride conta floats, não pixels nem bytes.
struct ImageView {
    float* data = nullptr;
    int width = 0;
    int height = 0;
    int stride = 0;

    bool empty() const { return data == nullptr; }
    float* row(int y) const { return data + static_cast<std::size_t>(y) * stride; }
    float* at(int x, int y) const { return row(y) + x * 4; }
};

struct Image {
    int width = 0;
    int height = 0;
    std::unique_ptr<float[]> data;

    Image() = default;
    Image(int w, int h);

    bool empty() const { return data == nullptr; }

    ImageView view() const;
    ImageView view(int x, int y, int w, int h) const;

    Image clone() const;
    Map<float> luma() const;
};

// Lê só o cabeçalho: quantos canais e quantos bits o arquivo tem de fato. O
// buffer não guarda isso, porque ele é sempre float32 de quatro canais.
bool probe_image(const std::string& path, int* channels, int* bits);

// Lê o arquivo com stb e converte de sRGB pra linear já na entrada.
Image load_image(const std::string& path, std::string& error);

// Volta pra sRGB de 8 bits, que é o que a textura quer. Valores fora de 0..1
// são cortados aqui, e só aqui.
std::unique_ptr<unsigned char[]> to_srgb8(const Image& image);
