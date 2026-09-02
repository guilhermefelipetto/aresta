#include "texture.h"

#include <utility>

#include <SDL_opengl.h>

// O stb não compila limpo com os avisos que o projeto liga, e não é código meu
// pra sair consertando.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#pragma GCC diagnostic pop

Texture::~Texture() {
    if (id != 0) {
        glDeleteTextures(1, &id);
    }
}

Texture::Texture(Texture&& other) noexcept
    : id(other.id), width(other.width), height(other.height) {
    other.id = 0;
    other.width = 0;
    other.height = 0;
}

Texture& Texture::operator=(Texture&& other) noexcept {
    if (this != &other) {
        if (id != 0) {
            glDeleteTextures(1, &id);
        }
        id = std::exchange(other.id, 0);
        width = std::exchange(other.width, 0);
        height = std::exchange(other.height, 0);
    }
    return *this;
}

Texture load_image(const std::string& path, std::string& error) {
    int w = 0;
    int h = 0;
    int channels = 0;
    unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (!pixels) {
        error = stbi_failure_reason() ? stbi_failure_reason() : "motivo desconhecido";
        return Texture{};
    }

    Texture texture;
    texture.width = w;
    texture.height = h;
    glGenTextures(1, &texture.id);
    glBindTexture(GL_TEXTURE_2D, texture.id);

    // Nearest na ampliação porque a graça de dar zoom aqui é olhar pixel a pixel.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(pixels);
    error.clear();
    return texture;
}
