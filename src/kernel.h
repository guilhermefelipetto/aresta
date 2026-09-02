#pragma once

#include <string>
#include <vector>

struct Kernel {
    int width = 3;
    int height = 3;
    std::vector<float> values;

    Kernel();
    Kernel(int w, int h);

    float& at(int x, int y) { return values[static_cast<std::size_t>(y) * width + x]; }
    float at(int x, int y) const { return values[static_cast<std::size_t>(y) * width + x]; }

    // Âncora no centro. Com lado par cai no floor, que é a convenção do
    // OpenCV e evita ter que perguntar toda vez.
    int anchor_x() const { return width / 2; }
    int anchor_y() const { return height / 2; }

    void resize(int w, int h);
    void fill(float value);
    float sum() const;
    float abs_sum() const;
    void normalize();
    void rotate90();
    void flip_horizontal();
    void flip_vertical();
    void transpose();
};

// Posto 1 quer dizer separável, e separável quer dizer O(w+h) por pixel em vez
// de O(w*h). row e col saem preenchidos quando dá.
bool separable(const Kernel& kernel, std::vector<float>* row, std::vector<float>* col,
               float tolerance = 1e-4f);

Kernel kernel_box(int w, int h);
Kernel kernel_gaussian(int size, float sigma);
Kernel kernel_log(int size, float sigma);
Kernel kernel_dog(int size, float sigma_near, float sigma_far);
Kernel kernel_gabor(int size, float sigma, float lambda, float theta, float gamma, float psi);
Kernel kernel_disc(int size, float radius);
Kernel kernel_motion(int size, float degrees);

struct NamedKernel {
    std::string name;
    Kernel kernel;
    bool builtin = false;
};

struct KernelLibrary {
    std::vector<NamedKernel> items;

    KernelLibrary();

    const NamedKernel* find(const std::string& name) const;
    void put(const std::string& name, const Kernel& kernel);
    bool erase(const std::string& name);

    void load();
    void save() const;
};

std::string kernel_library_path();
