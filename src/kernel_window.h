#pragma once

#include <string>

#include "chain.h"
#include "convolve.h"
#include "kernel.h"

struct App;

struct KernelWindow {
    bool open = false;

    Kernel kernel;
    Border border = Border::Zero;
    bool flip = false;
    bool normalize_on_apply = false;
    ConvPath path = ConvPath::Auto;

    // O que a janela e o estágio tinham na última vez que bateram. Sem isso
    // não dá pra saber quem mexeu: se foi a janela, empurra pro estágio; se
    // foi a cadeia, puxa de volta.
    ConvolveOp synced;

    // Id do estágio que a janela está dirigindo, ou -1 pra ela estar só
    // montando um kernel solto.
    int editing = -1;

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

// Liga a janela num estágio de convolução que já existe, trazendo os
// parâmetros dele pra dentro.
void attach_kernel_window(KernelWindow& window, App& app, int stage_id);

void draw_kernel_window(KernelWindow& window, KernelLibrary& library, App& app);
