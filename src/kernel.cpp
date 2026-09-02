#include "kernel.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <numbers>

namespace {

Kernel from_values(int w, int h, std::initializer_list<float> values) {
    Kernel k(w, h);
    std::copy(values.begin(), values.end(), k.values.begin());
    return k;
}

}  // namespace

Kernel::Kernel() : values(9, 0.0f) { values[4] = 1.0f; }

Kernel::Kernel(int w, int h)
    : width(std::max(1, w)),
      height(std::max(1, h)),
      values(static_cast<std::size_t>(std::max(1, w)) * std::max(1, h), 0.0f) {}

void Kernel::resize(int w, int h) {
    w = std::clamp(w, 1, 31);
    h = std::clamp(h, 1, 31);
    if (w == width && h == height) {
        return;
    }

    // Mantém o que já estava alinhado pelo centro; crescer um kernel não
    // deveria embaralhar o que a pessoa digitou.
    std::vector<float> grown(static_cast<std::size_t>(w) * h, 0.0f);
    const int dx = w / 2 - width / 2;
    const int dy = h / 2 - height / 2;
    for (int y = 0; y < height; ++y) {
        const int ny = y + dy;
        if (ny < 0 || ny >= h) {
            continue;
        }
        for (int x = 0; x < width; ++x) {
            const int nx = x + dx;
            if (nx < 0 || nx >= w) {
                continue;
            }
            grown[static_cast<std::size_t>(ny) * w + nx] = at(x, y);
        }
    }
    width = w;
    height = h;
    values = std::move(grown);
}

void Kernel::fill(float value) { std::fill(values.begin(), values.end(), value); }

float Kernel::sum() const {
    float total = 0.0f;
    for (float v : values) {
        total += v;
    }
    return total;
}

float Kernel::abs_sum() const {
    float total = 0.0f;
    for (float v : values) {
        total += std::fabs(v);
    }
    return total;
}

void Kernel::normalize() {
    const float total = sum();
    // Kernel de borda soma zero, e dividir por zero não é normalizar. Nesse
    // caso a escala que preserva magnitude é a soma dos absolutos.
    const float divisor = (std::fabs(total) > 1e-6f) ? total : abs_sum();
    if (std::fabs(divisor) < 1e-6f) {
        return;
    }
    for (float& v : values) {
        v /= divisor;
    }
}

void Kernel::rotate90() {
    Kernel out(height, width);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            out.at(height - 1 - y, x) = at(x, y);
        }
    }
    *this = std::move(out);
}

void Kernel::flip_horizontal() {
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width / 2; ++x) {
            std::swap(at(x, y), at(width - 1 - x, y));
        }
    }
}

void Kernel::flip_vertical() {
    for (int y = 0; y < height / 2; ++y) {
        for (int x = 0; x < width; ++x) {
            std::swap(at(x, y), at(x, height - 1 - y));
        }
    }
}

void Kernel::transpose() {
    Kernel out(height, width);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            out.at(y, x) = at(x, y);
        }
    }
    *this = std::move(out);
}

bool separable(const Kernel& kernel, std::vector<float>* row, std::vector<float>* col,
               float tolerance) {
    // Acha o maior elemento e usa a linha e a coluna dele como candidatos. Se a
    // matriz tem posto 1, o produto externo deles dividido pelo pivô reconstrói
    // ela inteira.
    int px = 0;
    int py = 0;
    float pivot = 0.0f;
    for (int y = 0; y < kernel.height; ++y) {
        for (int x = 0; x < kernel.width; ++x) {
            if (std::fabs(kernel.at(x, y)) > std::fabs(pivot)) {
                pivot = kernel.at(x, y);
                px = x;
                py = y;
            }
        }
    }
    if (std::fabs(pivot) < 1e-8f) {
        return false;
    }

    std::vector<float> h(kernel.width);
    std::vector<float> v(kernel.height);
    for (int x = 0; x < kernel.width; ++x) {
        h[x] = kernel.at(x, py);
    }
    for (int y = 0; y < kernel.height; ++y) {
        v[y] = kernel.at(px, y) / pivot;
    }

    float scale = 0.0f;
    for (int y = 0; y < kernel.height; ++y) {
        for (int x = 0; x < kernel.width; ++x) {
            scale = std::max(scale, std::fabs(kernel.at(x, y)));
        }
    }
    for (int y = 0; y < kernel.height; ++y) {
        for (int x = 0; x < kernel.width; ++x) {
            if (std::fabs(h[x] * v[y] - kernel.at(x, y)) > tolerance * scale) {
                return false;
            }
        }
    }

    if (row) {
        *row = h;
    }
    if (col) {
        *col = v;
    }
    return true;
}

Kernel kernel_box(int w, int h) {
    Kernel k(w, h);
    k.fill(1.0f);
    k.normalize();
    return k;
}

Kernel kernel_gaussian(int size, float sigma) {
    Kernel k(size, size);
    const int a = size / 2;
    const float denom = 2.0f * sigma * sigma;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float dx = static_cast<float>(x - a);
            const float dy = static_cast<float>(y - a);
            k.at(x, y) = std::exp(-(dx * dx + dy * dy) / denom);
        }
    }
    k.normalize();
    return k;
}

Kernel kernel_log(int size, float sigma) {
    Kernel k(size, size);
    const int a = size / 2;
    const float s2 = sigma * sigma;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float dx = static_cast<float>(x - a);
            const float dy = static_cast<float>(y - a);
            const float r2 = dx * dx + dy * dy;
            k.at(x, y) = (r2 - 2.0f * s2) / (s2 * s2) * std::exp(-r2 / (2.0f * s2));
        }
    }
    // Soma zero é a propriedade que define o LoG: região constante tem que dar
    // resposta nula. O erro de amostragem tira ela do zero, então volta na mão.
    const float bias = k.sum() / static_cast<float>(k.values.size());
    for (float& v : k.values) {
        v -= bias;
    }
    return k;
}

Kernel kernel_dog(int size, float sigma_near, float sigma_far) {
    Kernel near = kernel_gaussian(size, sigma_near);
    const Kernel far = kernel_gaussian(size, sigma_far);
    for (std::size_t i = 0; i < near.values.size(); ++i) {
        near.values[i] -= far.values[i];
    }
    return near;
}

Kernel kernel_gabor(int size, float sigma, float lambda, float theta, float gamma, float psi) {
    Kernel k(size, size);
    const int a = size / 2;
    const float ct = std::cos(theta);
    const float st = std::sin(theta);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float dx = static_cast<float>(x - a);
            const float dy = static_cast<float>(y - a);
            const float xr = dx * ct + dy * st;
            const float yr = -dx * st + dy * ct;
            const float envelope =
                std::exp(-(xr * xr + gamma * gamma * yr * yr) / (2.0f * sigma * sigma));
            k.at(x, y) = envelope * std::cos(2.0f * std::numbers::pi_v<float> * xr / lambda + psi);
        }
    }
    return k;
}

Kernel kernel_disc(int size, float radius) {
    Kernel k(size, size);
    const int a = size / 2;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float dx = static_cast<float>(x - a);
            const float dy = static_cast<float>(y - a);
            k.at(x, y) = (dx * dx + dy * dy <= radius * radius) ? 1.0f : 0.0f;
        }
    }
    k.normalize();
    return k;
}

Kernel kernel_motion(int size, float degrees) {
    Kernel k(size, size);
    const int a = size / 2;
    const float rad = degrees * std::numbers::pi_v<float> / 180.0f;
    const float ct = std::cos(rad);
    const float st = std::sin(rad);
    for (int t = -a; t <= a; ++t) {
        const int x = a + static_cast<int>(std::lround(ct * static_cast<float>(t)));
        const int y = a + static_cast<int>(std::lround(st * static_cast<float>(t)));
        if (x >= 0 && x < size && y >= 0 && y < size) {
            k.at(x, y) = 1.0f;
        }
    }
    k.normalize();
    return k;
}

KernelLibrary::KernelLibrary() {
    const auto add = [this](const char* name, Kernel k) {
        items.push_back(NamedKernel{name, std::move(k), true});
    };

    add("identidade", from_values(3, 3, {0, 0, 0, 0, 1, 0, 0, 0, 0}));
    add("média 3x3", kernel_box(3, 3));
    add("gaussiana 3x3", kernel_gaussian(3, 0.8f));
    add("gaussiana 5x5", kernel_gaussian(5, 1.2f));
    add("sobel x", from_values(3, 3, {-1, 0, 1, -2, 0, 2, -1, 0, 1}));
    add("sobel y", from_values(3, 3, {-1, -2, -1, 0, 0, 0, 1, 2, 1}));
    add("prewitt x", from_values(3, 3, {-1, 0, 1, -1, 0, 1, -1, 0, 1}));
    add("prewitt y", from_values(3, 3, {-1, -1, -1, 0, 0, 0, 1, 1, 1}));
    add("scharr x", from_values(3, 3, {-3, 0, 3, -10, 0, 10, -3, 0, 3}));
    add("roberts", from_values(2, 2, {1, 0, 0, -1}));
    add("laplaciano 4", from_values(3, 3, {0, 1, 0, 1, -4, 1, 0, 1, 0}));
    add("laplaciano 8", from_values(3, 3, {1, 1, 1, 1, -8, 1, 1, 1, 1}));
    add("realçar", from_values(3, 3, {0, -1, 0, -1, 5, -1, 0, -1, 0}));
    add("relevo", from_values(3, 3, {-2, -1, 0, -1, 1, 1, 0, 1, 2}));
    add("LoG 5x5", kernel_log(5, 1.0f));
}

const NamedKernel* KernelLibrary::find(const std::string& name) const {
    for (const NamedKernel& item : items) {
        if (item.name == name) {
            return &item;
        }
    }
    return nullptr;
}

void KernelLibrary::put(const std::string& name, const Kernel& kernel) {
    for (NamedKernel& item : items) {
        if (item.name == name) {
            if (item.builtin) {
                return;
            }
            item.kernel = kernel;
            return;
        }
    }
    items.push_back(NamedKernel{name, kernel, false});
}

bool KernelLibrary::erase(const std::string& name) {
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (items[i].name == name) {
            if (items[i].builtin) {
                return false;
            }
            items.erase(items.begin() + static_cast<long>(i));
            return true;
        }
    }
    return false;
}

std::string kernel_library_path() {
    const char* home = std::getenv("HOME");
    if (!home) {
        return "kernels.txt";
    }
    return std::string(home) + "/.config/aresta/kernels.txt";
}

void KernelLibrary::load() {
    std::ifstream file(kernel_library_path());
    if (!file) {
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.size() < 3 || line.front() != '[' || line.back() != ']') {
            continue;
        }
        const std::string name = line.substr(1, line.size() - 2);
        int w = 0;
        int h = 0;
        if (!(file >> w >> h) || w < 1 || h < 1 || w > 31 || h > 31) {
            return;
        }
        Kernel k(w, h);
        for (float& v : k.values) {
            if (!(file >> v)) {
                return;
            }
        }
        std::getline(file, line);
        put(name, k);
    }
}

void KernelLibrary::save() const {
    const std::string path = kernel_library_path();
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);

    std::ofstream file(path);
    if (!file) {
        return;
    }
    // Nove dígitos é o que fecha o ida e volta de um float sem perder bit.
    file << std::setprecision(9);
    for (const NamedKernel& item : items) {
        if (item.builtin) {
            continue;
        }
        file << '[' << item.name << "]\n" << item.kernel.width << ' ' << item.kernel.height << '\n';
        for (int y = 0; y < item.kernel.height; ++y) {
            for (int x = 0; x < item.kernel.width; ++x) {
                file << item.kernel.at(x, y) << (x + 1 == item.kernel.width ? '\n' : ' ');
            }
        }
    }
}
