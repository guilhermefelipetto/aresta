#pragma once

#include <string>

#include "convolve.h"
#include "kernel.h"

struct App;

struct KernelWindow {
    bool open = false;

    Kernel kernel;
    Border border = Border::Clamp;
    bool flip = false;
    bool normalize_on_apply = false;

    bool live = false;
    int live_stage = -1;

    int generator = 0;
    float sigma = 1.2f;
    float sigma_far = 2.4f;
    float lambda = 4.0f;
    float theta = 0.0f;
    float gamma = 0.5f;
    float psi = 0.0f;
    float radius = 2.0f;
    float angle = 0.0f;
    float constant = 1.0f;

    char expression[256] = "gauss(r, a)";
    float pa = 1.2f;
    float pb = 1.0f;
    float pc = 0.0f;
    std::string expr_error;

    char save_name[64] = "";
    int preset = 0;
    std::string message;
};

void draw_kernel_window(KernelWindow& window, KernelLibrary& library, App& app);
