#pragma once

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

// Realoca só quando o tamanho muda; mexer num slider não deveria recriar
// textura a cada quadro.
void upload_rgba8(Texture& texture, const unsigned char* pixels, int width, int height);
