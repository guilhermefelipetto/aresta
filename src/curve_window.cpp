#include "curve_window.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include <imgui.h>

#include "app.h"
#include "expr.h"
#include "histogram.h"

namespace {

constexpr int samples = 256;

CurveOp current_op(const CurveWindow& window) {
    CurveOp op;
    std::snprintf(op.expression, sizeof(op.expression), "%s", window.expression);
    op.a = window.a;
    op.b = window.b;
    op.c = window.c;
    op.on_srgb = window.on_srgb;
    return op;
}

void write_back(CurveWindow& window, App& app) {
    const int index = app.chain.index_of(window.editing);
    if (index < 0) {
        window.editing = -1;
        return;
    }
    app.chain.stages[index].params = current_op(window);
    app.evaluate();
}

void create_stage(CurveWindow& window, App& app) {
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

void draw_plot(const CurveWindow& window, const std::vector<float>& curve,
               const Histogram& histogram, float side) {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(side, side));

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 max(origin.x + side, origin.y + side);
    draw->AddRectFilled(origin, max, IM_COL32(18, 18, 20, 255));

    // Histograma da entrada por baixo, pra dar pra ver que parte da faixa a
    // curva está de fato mexendo.
    if (!histogram.empty() && histogram.peak > 0.0f) {
        const float* counts = histogram.channel(histogram.channels - 1);
        const float ceiling = std::log1p(histogram.peak);
        for (int i = 0; i < histogram.bins; ++i) {
            const float share = std::log1p(counts[i]) / ceiling;
            const float x0 = origin.x + side * static_cast<float>(i) / histogram.bins;
            const float x1 = origin.x + side * static_cast<float>(i + 1) / histogram.bins;
            draw->AddRectFilled(ImVec2(x0, max.y - side * share * 0.55f), ImVec2(x1, max.y),
                                IM_COL32(70, 70, 80, 170));
        }
    }

    for (int i = 1; i < 4; ++i) {
        const float t = static_cast<float>(i) / 4.0f;
        draw->AddLine(ImVec2(origin.x + side * t, origin.y), ImVec2(origin.x + side * t, max.y),
                      IM_COL32(45, 45, 52, 255));
        draw->AddLine(ImVec2(origin.x, max.y - side * t), ImVec2(max.x, max.y - side * t),
                      IM_COL32(45, 45, 52, 255));
    }
    draw->AddLine(ImVec2(origin.x, max.y), ImVec2(max.x, origin.y), IM_COL32(80, 80, 90, 255));

    if (window.error.empty() && curve.size() == samples) {
        ImVec2 previous(origin.x, max.y - side * std::clamp(curve[0], 0.0f, 1.0f));
        for (int i = 1; i < samples; ++i) {
            const float t = static_cast<float>(i) / (samples - 1);
            const ImVec2 point(origin.x + side * t,
                               max.y - side * std::clamp(curve[static_cast<std::size_t>(i)], 0.0f,
                                                         1.0f));
            draw->AddLine(previous, point, IM_COL32(120, 190, 255, 255), 2.0f);
            previous = point;
        }
    }

    draw->AddRect(origin, max, IM_COL32(60, 60, 68, 255));
}

}  // namespace

void attach_curve_window(CurveWindow& window, App& app, int stage_id) {
    const int index = app.chain.index_of(stage_id);
    if (index < 0) {
        return;
    }
    const auto* op = std::get_if<CurveOp>(&app.chain.stages[index].params);
    if (!op) {
        return;
    }

    std::snprintf(window.expression, sizeof(window.expression), "%s", op->expression);
    window.a = op->a;
    window.b = op->b;
    window.c = op->c;
    window.on_srgb = op->on_srgb;
    window.editing = stage_id;
    window.open = true;
    window.message.clear();
    app.viewed = index;
    app.upload_view();
}

void draw_curve_window(CurveWindow& window, App& app) {
    if (!window.open) {
        return;
    }
    if (window.editing >= 0 && app.chain.index_of(window.editing) < 0) {
        window.editing = -1;
    }

    ImGui::SetNextWindowSize(ImVec2(940.0f, 560.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Curva", &window.open)) {
        ImGui::End();
        return;
    }

    std::vector<float> curve(samples, 0.0f);
    const Expr parsed = parse_expr(window.expression);
    window.error = parsed.valid() ? std::string() : parsed.error;
    if (parsed.valid()) {
        for (int i = 0; i < samples; ++i) {
            ExprVars vars;
            vars.v = static_cast<float>(i) / (samples - 1);
            vars.a = window.a;
            vars.b = window.b;
            vars.c = window.c;
            curve[static_cast<std::size_t>(i)] = parsed.eval(vars);
        }
    }

    Histogram histogram;
    if (app.viewed >= 0 && app.viewed < static_cast<int>(app.chain.outputs.size()) &&
        !app.chain.outputs[app.viewed].empty()) {
        histogram = histogram_of(app.chain.outputs[app.viewed], 128, window.on_srgb);
    }

    const float side = std::min(ImGui::GetContentRegionAvail().y - 30.0f, 420.0f);
    ImGui::BeginGroup();
    draw_plot(window, curve, histogram, side);
    ImGui::TextDisabled("entrada na horizontal, saída na vertical, ambas de 0 a 1");
    ImGui::EndGroup();

    ImGui::SameLine();
    ImGui::BeginGroup();
    {
        bool changed = false;

        struct Preset {
            const char* label;
            const char* expression;
            float a;
            float b;
        };
        static const Preset presets[] = {
            {"identidade", "v", 1.0f, 1.0f},
            {"negativo", "1 - v", 1.0f, 1.0f},
            {"log", "a * log(1 + b*v) / log(1 + b)", 1.0f, 20.0f},
            {"gama", "a * v^b", 1.0f, 0.45f},
            {"alongamento linear", "clamp((v - a) / (b - a), 0, 1)", 0.2f, 0.8f},
            {"fatiamento binário", "if(v >= a, if(v <= b, 1, 0), 0)", 0.4f, 0.6f},
            {"fatiamento com fundo", "if(v >= a, if(v <= b, 1, v), v)", 0.4f, 0.6f},
            {"solarizar", "if(v < a, v, 1 - v)", 0.5f, 1.0f},
        };

        ImGui::TextDisabled("Prontos");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("##presets", "escolher")) {
            for (const Preset& preset : presets) {
                if (ImGui::Selectable(preset.label)) {
                    std::snprintf(window.expression, sizeof(window.expression), "%s",
                                  preset.expression);
                    window.a = preset.a;
                    window.b = preset.b;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }

        ImGui::Spacing();
        ImGui::TextDisabled("Fórmula em v");
        ImGui::SetNextItemWidth(-1.0f);
        changed |= ImGui::InputText("##formula", window.expression, sizeof(window.expression));
        changed |= ImGui::SliderFloat("a", &window.a, -2.0f, 4.0f, "%.3f");
        changed |= ImGui::SliderFloat("b", &window.b, -2.0f, 24.0f, "%.3f");
        changed |= ImGui::SliderFloat("c", &window.c, -2.0f, 4.0f, "%.3f");
        changed |= ImGui::Checkbox("sobre sRGB", &window.on_srgb);
        ImGui::TextDisabled("desligado, a curva vê o valor linear");

        if (!window.error.empty()) {
            ImGui::TextColored(ImVec4(0.9f, 0.45f, 0.45f, 1.0f), "%s", window.error.c_str());
        }

        ImGui::Spacing();
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
        if (ImGui::Button(bound >= 0 ? "Acrescentar como novo estágio" : "Aplicar na cadeia",
                          ImVec2(-1.0f, 0.0f))) {
            create_stage(window, app);
            window.message = "Estágio de curva acrescentado.";
        }
        if (!window.message.empty()) {
            ImGui::TextWrapped("%s", window.message.c_str());
        }

        if (changed && window.editing >= 0 && window.error.empty()) {
            write_back(window, app);
        }
    }
    ImGui::EndGroup();

    ImGui::End();
}
