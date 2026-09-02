#pragma once

#include <string>

// Uma textura RGBA8 na GPU. O id fica como unsigned int pra não arrastar os
// headers de OpenGL pra todo mundo que inclui isso aqui.
struct Texture {
    unsigned int id = 0;
    int width = 0;
    int height = 0;

    Texture() = default;
    ~Texture();

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&& other) noexcept;
    Texture& operator=(Texture&& other) noexcept;

    bool valid() const { return id != 0; }
};

// Devolve uma textura inválida se não conseguir ler o arquivo, e escreve o
// motivo em `error`.
Texture load_image(const std::string& path, std::string& error);
