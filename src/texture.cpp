#include "texture.h"

#include <utility>

#include <SDL_opengl.h>

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

void upload_rgba8(Texture& texture, const unsigned char* pixels, int width, int height) {
    if (!pixels || width <= 0 || height <= 0) {
        return;
    }

    const bool resized = (texture.width != width || texture.height != height);
    if (texture.id == 0) {
        glGenTextures(1, &texture.id);
    }
    glBindTexture(GL_TEXTURE_2D, texture.id);

    if (resized) {
        // Nearest na ampliação porque a graça de dar zoom aqui é olhar pixel a
        // pixel.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (resized) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     pixels);
        texture.width = width;
        texture.height = height;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}
