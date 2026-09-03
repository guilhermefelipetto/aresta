#include "image.h"

#include <algorithm>
#include <array>
#include <cstring>

#include "color.h"

// O stb não compila limpo com os avisos que o projeto liga, e não é código meu
// pra sair consertando.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#pragma GCC diagnostic pop

namespace {

// A entrada é sempre 8 bits, então a curva cabe inteira numa tabela e não sobra
// erro nenhum de aproximação.
const float* srgb8_table() {
    static const std::array<float, 256> table = [] {
        std::array<float, 256> t{};
        for (int i = 0; i < 256; ++i) {
            t[i] = srgb_to_linear(static_cast<float>(i) / 255.0f);
        }
        return t;
    }();
    return table.data();
}

}  // namespace

Image::Image(int w, int h)
    : width(w), height(h), data(new float[static_cast<std::size_t>(w) * h * 4]) {}

ImageView Image::view() const {
    return ImageView{data.get(), width, height, width * 4};
}

ImageView Image::view(int x, int y, int w, int h) const {
    float* start = data.get() + (static_cast<std::size_t>(y) * width + x) * 4;
    return ImageView{start, w, h, width * 4};
}

Image Image::clone() const {
    Image copy;
    if (empty()) {
        return copy;
    }
    copy = Image(width, height);
    std::memcpy(copy.data.get(), data.get(),
                static_cast<std::size_t>(width) * height * 4 * sizeof(float));
    return copy;
}

Map<float> Image::luma() const {
    if (empty()) {
        return Map<float>{};
    }
    Map<float> result(width, height);
    const ImageView src = view();
    for (int y = 0; y < height; ++y) {
        const float* p = src.row(y);
        float* out = result.view().row(y);
        for (int x = 0; x < width; ++x, p += 4) {
            out[x] = luminance(p[0], p[1], p[2]);
        }
    }
    return result;
}

bool probe_image(const std::string& path, int* channels, int* bits) {
    int w = 0;
    int h = 0;
    int n = 0;
    if (!stbi_info(path.c_str(), &w, &h, &n)) {
        return false;
    }
    *channels = n;
    // O stb expande 1, 2 e 4 bits pra 8 sem avisar, então isso é o que o
    // decodificador entrega, não necessariamente o que o arquivo guarda.
    *bits = stbi_is_16_bit(path.c_str()) ? 16 : 8;
    return true;
}

Image load_image(const std::string& path, std::string& error) {
    int w = 0;
    int h = 0;
    int channels = 0;
    unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (!pixels) {
        const char* reason = stbi_failure_reason();
        error = reason ? reason : "motivo desconhecido";
        return Image{};
    }

    Image image(w, h);
    const float* table = srgb8_table();
    const unsigned char* src = pixels;
    float* dst = image.data.get();
    const std::size_t total = static_cast<std::size_t>(w) * h;
    for (std::size_t i = 0; i < total; ++i, src += 4, dst += 4) {
        dst[0] = table[src[0]];
        dst[1] = table[src[1]];
        dst[2] = table[src[2]];
        dst[3] = static_cast<float>(src[3]) / 255.0f;
    }

    stbi_image_free(pixels);
    error.clear();
    return image;
}

std::unique_ptr<unsigned char[]> to_srgb8(const Image& image) {
    if (image.empty()) {
        return nullptr;
    }
    const std::size_t total = static_cast<std::size_t>(image.width) * image.height;
    std::unique_ptr<unsigned char[]> out(new unsigned char[total * 4]);

    const float* src = image.data.get();
    unsigned char* dst = out.get();
    for (std::size_t i = 0; i < total; ++i, src += 4, dst += 4) {
        for (int c = 0; c < 3; ++c) {
            const float encoded = linear_to_srgb(std::clamp(src[c], 0.0f, 1.0f));
            dst[c] = static_cast<unsigned char>(encoded * 255.0f + 0.5f);
        }
        dst[3] = static_cast<unsigned char>(std::clamp(src[3], 0.0f, 1.0f) * 255.0f + 0.5f);
    }
    return out;
}
