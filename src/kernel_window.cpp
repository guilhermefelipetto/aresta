#include "kernel_window.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <imgui.h>

#include "app.h"
#include "expr.h"

namespace {

constexpr int max_side = 21;

void fill_from_expression(KernelWindow& window) {
    const Expr expr = parse_expr(window.expression);
    if (!expr.valid()) {
        window.expr_error = expr.error;
        return;
    }
    window.expr_error.clear();

    const int ax = window.kernel.anchor_x();
    const int ay = window.kernel.anchor_y();
    for (int y = 0; y < window.kernel.height; ++y) {
        for (int x = 0; x < window.kernel.width; ++x) {
            ExprVars vars;
            vars.x = static_cast<float>(x - ax);
            vars.y = static_cast<float>(y - ay);
            vars.r = std::hypot(vars.x, vars.y);
            vars.t = std::atan2(vars.y, vars.x);
            vars.w = static_cast<float>(window.kernel.width);
            vars.h = static_cast<float>(window.kernel.height);
            vars.a = window.pa;
            vars.b = window.pb;
            vars.c = window.pc;
            window.kernel.at(x, y) = expr.eval(vars);
        }
    }
}

void run_generator(KernelWindow& window) {
    const int side = std::max(window.kernel.width, window.kernel.height);
    switch (window.generator) {
        case 0:
            window.kernel = kernel_box(window.kernel.width, window.kernel.height);
            break;
        case 1:
            window.kernel = kernel_gaussian(side, window.sigma);
            break;
        case 2:
            window.kernel = kernel_log(side, window.sigma);
            break;
        case 3:
            window.kernel = kernel_dog(side, window.sigma, window.sigma_far);
            break;
        case 4:
            window.kernel = kernel_gabor(side, window.sigma, window.lambda, window.theta,
                                         window.gamma, window.psi);
            break;
        case 5:
            window.kernel = kernel_disc(side, window.radius);
            break;
        case 6:
            window.kernel = kernel_motion(side, window.angle);
            break;
        default:
            window.kernel.fill(window.constant);
            break;
    }
}

ConvolveOp current_op(const KernelWindow& window) {
    ConvolveOp op;
    op.kernel = window.kernel;
    op.border = window.border;
    op.flip = window.flip;
    op.normalize = window.normalize_on_apply;
    return op;
}

// Empurra o que está na janela pro estágio ligado. Enquanto houver ligação,
// mexer num coeficiente já é mexer na cadeia.
void write_back(KernelWindow& window, App& app) {
    const int index = app.chain.index_of(window.editing);
    if (index < 0) {
        window.editing = -1;
        return;
    }
    app.chain.stages[index].params = current_op(window);
    app.evaluate();
}

void create_stage(KernelWindow& window, App& app) {
    if (app.source.empty()) {
        window.message = "Abra uma imagem primeiro.";
        return;
    }
    const int viewed_id =
        (app.viewed >= 0 && app.viewed < static_cast<int>(app.chain.stages.size()))
            ? app.chain.stages[app.viewed].id
            : -1;
    window.editing = app.chain.add(current_op(window), viewed_id);
    app.viewed = app.chain.index_of(window.editing);
    app.evaluate();
}

void draw_grid(KernelWindow& window) {
    float peak = 0.0f;
    for (float v : window.kernel.values) {
        peak = std::max(peak, std::fabs(v));
    }
    if (peak <= 0.0f) {
        peak = 1.0f;
    }

    // Encolhe a célula pra grade caber sem rolagem enquanto der, e cai pra duas
    // casas quando o número não couber mais em três.
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float available = ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ScrollbarSize;
    const float wanted = (available - spacing * static_cast<float>(window.kernel.width - 1)) /
                         static_cast<float>(window.kernel.width);
    const float cell = std::clamp(wanted, 40.0f, 62.0f);
    const char* format = cell < 54.0f ? "%.2f" : "%.3f";

    ImGui::BeginChild("grade", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_HorizontalScrollbar);
    for (int y = 0; y < window.kernel.height; ++y) {
        for (int x = 0; x < window.kernel.width; ++x) {
            if (x > 0) {
                ImGui::SameLine();
            }
            ImGui::PushID(y * max_side + x);

            const float value = window.kernel.at(x, y);
            const float weight = std::fabs(value) / peak;
            // Positivo puxa pro azul, negativo pro vermelho. Dá pra ver a forma
            // de uma gaussiana 15x15 sem ler número nenhum.
            const ImVec4 tint = value >= 0.0f
                                    ? ImVec4(0.16f + 0.10f * weight, 0.20f + 0.22f * weight,
                                             0.24f + 0.42f * weight, 1.0f)
                                    : ImVec4(0.26f + 0.42f * weight, 0.17f + 0.06f * weight,
                                             0.19f + 0.10f * weight, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, tint);
            ImGui::SetNextItemWidth(cell);
            ImGui::InputFloat("##c", &window.kernel.at(x, y), 0.0f, 0.0f, format);
            ImGui::PopStyleColor();
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
}

}  // namespace

void attach_kernel_window(KernelWindow& window, App& app, int stage_id) {
    const int index = app.chain.index_of(stage_id);
    if (index < 0) {
        return;
    }
    const auto* op = std::get_if<ConvolveOp>(&app.chain.stages[index].params);
    if (!op) {
        return;
    }

    window.kernel = op->kernel;
    window.border = op->border;
    window.flip = op->flip;
    window.normalize_on_apply = op->normalize;
    window.editing = stage_id;
    window.open = true;
    window.message.clear();
    app.viewed = index;
    app.upload_view();
}

void draw_kernel_window(KernelWindow& window, KernelLibrary& library, App& app) {
    if (!window.open) {
        return;
    }
    if (window.editing >= 0 && app.chain.index_of(window.editing) < 0) {
        window.editing = -1;
    }

    ImGui::SetNextWindowSize(ImVec2(1000.0f, 600.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Kernel", &window.open)) {
        ImGui::End();
        return;
    }

    bool changed = false;

    ImGui::BeginChild("esquerda", ImVec2(ImGui::GetContentRegionAvail().x * 0.58f, 0.0f));
    {
        int w = window.kernel.width;
        int h = window.kernel.height;
        ImGui::SetNextItemWidth(120.0f);
        const bool w_changed = ImGui::InputInt("largura", &w);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        const bool h_changed = ImGui::InputInt("altura", &h);
        if (w_changed || h_changed) {
            window.kernel.resize(std::clamp(w, 1, max_side), std::clamp(h, 1, max_side));
            changed = true;
        }

        const float total = window.kernel.sum();
        ImGui::Text("soma %+.4f", total);
        ImGui::SameLine();
        if (std::fabs(total) < 1e-4f) {
            ImGui::TextDisabled("(kernel de borda, soma zero está certo)");
        } else if (std::fabs(total - 1.0f) < 1e-4f) {
            ImGui::TextDisabled("(preserva o brilho)");
        } else {
            ImGui::TextDisabled("(escurece ou clareia a imagem)");
        }

        std::vector<float> row;
        std::vector<float> col;
        if (separable(window.kernel, &row, &col)) {
            ImGui::TextDisabled("separável: dá pra rodar em %d passadas no lugar de %d",
                                window.kernel.width + window.kernel.height,
                                window.kernel.width * window.kernel.height);
        } else {
            ImGui::TextDisabled("não separável");
        }

        ImGui::Spacing();
        draw_grid(window);
    }
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("direita", ImVec2(0.0f, 0.0f));
    {
        if (ImGui::CollapsingHeader("Prontos", ImGuiTreeNodeFlags_DefaultOpen)) {
            std::vector<const char*> names;
            names.reserve(library.items.size());
            for (const NamedKernel& item : library.items) {
                names.push_back(item.name.c_str());
            }
            ImGui::SetNextItemWidth(-1.0f);
            window.preset = std::clamp(window.preset, 0, static_cast<int>(names.size()) - 1);
            if (ImGui::Combo("##preset", &window.preset, names.data(),
                             static_cast<int>(names.size()))) {
                window.kernel = library.items[window.preset].kernel;
                changed = true;
            }

            const NamedKernel& chosen = library.items[window.preset];
            ImGui::BeginDisabled(chosen.builtin);
            if (ImGui::Button("Apagar o selecionado")) {
                window.message = library.erase(chosen.name) ? "Apagado." : "Esse é de fábrica.";
                library.save();
                window.preset = 0;
            }
            ImGui::EndDisabled();
            if (chosen.builtin) {
                ImGui::SameLine();
                ImGui::TextDisabled("de fábrica");
            }

            ImGui::SetNextItemWidth(-90.0f);
            ImGui::InputTextWithHint("##nome", "nome pra salvar", window.save_name,
                                     sizeof(window.save_name));
            ImGui::SameLine();
            if (ImGui::Button("Salvar", ImVec2(-1.0f, 0.0f))) {
                if (window.save_name[0] == '\0') {
                    window.message = "Dá um nome antes.";
                } else if (library.find(window.save_name) &&
                           library.find(window.save_name)->builtin) {
                    window.message = "Já existe um de fábrica com esse nome.";
                } else {
                    library.put(window.save_name, window.kernel);
                    library.save();
                    window.message = std::string("Salvo como ") + window.save_name + ".";
                }
            }
        }

        if (ImGui::CollapsingHeader("Gerador", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::SetNextItemWidth(-1.0f);
            bool regenerate = ImGui::Combo("##gerador", &window.generator,
                                           "média\0gaussiana\0laplaciano da gaussiana\0"
                                           "diferença de gaussianas\0gabor\0disco\0"
                                           "borrado de movimento\0constante\0");

            switch (window.generator) {
                case 1:
                case 2:
                    regenerate |= ImGui::SliderFloat("sigma", &window.sigma, 0.2f, 8.0f, "%.2f");
                    break;
                case 3:
                    regenerate |= ImGui::SliderFloat("sigma perto", &window.sigma, 0.2f, 8.0f);
                    regenerate |= ImGui::SliderFloat("sigma longe", &window.sigma_far, 0.2f, 12.0f);
                    break;
                case 4:
                    regenerate |= ImGui::SliderFloat("sigma", &window.sigma, 0.5f, 8.0f);
                    regenerate |= ImGui::SliderFloat("comprimento", &window.lambda, 1.0f, 16.0f);
                    regenerate |= ImGui::SliderAngle("orientação", &window.theta, 0.0f, 180.0f);
                    regenerate |= ImGui::SliderFloat("achatamento", &window.gamma, 0.1f, 2.0f);
                    regenerate |= ImGui::SliderAngle("fase", &window.psi, -180.0f, 180.0f);
                    break;
                case 5:
                    regenerate |= ImGui::SliderFloat("raio", &window.radius, 0.5f, 10.0f, "%.2f");
                    break;
                case 6:
                    regenerate |= ImGui::SliderAngle("ângulo", &window.angle, 0.0f, 180.0f);
                    break;
                case 7:
                    regenerate |= ImGui::SliderFloat("valor", &window.constant, -4.0f, 4.0f);
                    break;
                default:
                    break;
            }
            if (regenerate) {
                run_generator(window);
                changed = true;
            }
        }

        if (ImGui::CollapsingHeader("Fórmula")) {
            ImGui::TextDisabled("x e y contam do centro, r e t são polares,");
            ImGui::TextDisabled("a, b e c são os parâmetros abaixo.");
            ImGui::SetNextItemWidth(-1.0f);
            bool refill = ImGui::InputText("##formula", window.expression,
                                           sizeof(window.expression));
            refill |= ImGui::SliderFloat("a", &window.pa, -8.0f, 8.0f, "%.3f");
            refill |= ImGui::SliderFloat("b", &window.pb, -8.0f, 8.0f, "%.3f");
            refill |= ImGui::SliderFloat("c", &window.pc, -8.0f, 8.0f, "%.3f");
            if (ImGui::Button("Preencher", ImVec2(-1.0f, 0.0f))) {
                refill = true;
            }
            if (refill) {
                fill_from_expression(window);
                changed = true;
            }
            if (!window.expr_error.empty()) {
                ImGui::TextColored(ImVec4(0.9f, 0.45f, 0.45f, 1.0f), "%s",
                                   window.expr_error.c_str());
            }
        }

        if (ImGui::CollapsingHeader("Transformar")) {
            if (ImGui::Button("Normalizar")) { window.kernel.normalize(); changed = true; }
            ImGui::SameLine();
            if (ImGui::Button("Zerar")) { window.kernel.fill(0.0f); changed = true; }
            if (ImGui::Button("Girar 90")) { window.kernel.rotate90(); changed = true; }
            ImGui::SameLine();
            if (ImGui::Button("Transpor")) { window.kernel.transpose(); changed = true; }
            if (ImGui::Button("Espelhar H")) { window.kernel.flip_horizontal(); changed = true; }
            ImGui::SameLine();
            if (ImGui::Button("Espelhar V")) { window.kernel.flip_vertical(); changed = true; }
        }

        if (ImGui::CollapsingHeader("Aplicar", ImGuiTreeNodeFlags_DefaultOpen)) {
            int border = static_cast<int>(window.border);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::Combo("##borda", &border, "zero\0estender\0espelhar\0circular\0")) {
                window.border = static_cast<Border>(border);
                changed = true;
            }
            changed |= ImGui::Checkbox("espelhar o kernel (convolução estrita)", &window.flip);
            changed |= ImGui::Checkbox("normalizar antes de aplicar", &window.normalize_on_apply);

            const int bound = app.chain.index_of(window.editing);
            if (bound >= 0) {
                ImGui::Text("editando o estágio %d", bound);
                ImGui::SameLine();
                if (ImGui::SmallButton("soltar")) {
                    window.editing = -1;
                    window.message = "Solta. O que você mexer agora não vai pra cadeia.";
                }
            } else {
                ImGui::TextDisabled("solta: nada do que você mexer vai pra cadeia");
            }

            if (ImGui::Button(bound >= 0 ? "Acrescentar como novo estágio"
                                         : "Aplicar na cadeia",
                              ImVec2(-1.0f, 0.0f))) {
                create_stage(window, app);
                window.message = "Estágio de convolução acrescentado.";
            }
        }

        if (!window.message.empty()) {
            ImGui::Spacing();
            ImGui::TextWrapped("%s", window.message.c_str());
        }
    }
    ImGui::EndChild();

    if (changed && window.editing >= 0) {
        write_back(window, app);
    }

    ImGui::End();
}
