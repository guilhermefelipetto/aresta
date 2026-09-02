#include "histogram_window.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <imgui.h>

#include "app.h"
#include "ui.h"

namespace {

void recompute(HistogramWindow& window, const Value& value) {
    window.histogram = histogram_of(value, window.bins, window.srgb);
    window.has_otsu = false;
    window.mean = 0.0f;
    window.median = 0.0f;
    window.deviation = 0.0f;
    window.entropy = 0.0f;

    if (value.kind == ValueKind::Scalar && !value.empty()) {
        window.otsu = otsu_threshold(value.scalar.view());
        window.has_otsu = true;
    }
    if (window.histogram.empty()) {
        return;
    }

    // Estatística sai do último canal: luminância pra cor, o próprio mapa pro
    // resto.
    const Histogram& histogram = window.histogram;
    const float* counts = histogram.channel(histogram.channels - 1);
    const float span = histogram.hi - histogram.lo;

    double total = 0.0;
    for (int i = 0; i < histogram.bins; ++i) {
        total += counts[i];
    }
    if (total <= 0.0) {
        return;
    }

    const auto value_at = [&](int bin) {
        return histogram.lo + (static_cast<float>(bin) + 0.5f) / histogram.bins * span;
    };

    double sum = 0.0;
    double running = 0.0;
    bool found_median = false;
    for (int i = 0; i < histogram.bins; ++i) {
        sum += static_cast<double>(counts[i]) * value_at(i);
        running += counts[i];
        if (!found_median && running >= total * 0.5) {
            window.median = value_at(i);
            found_median = true;
        }
        if (counts[i] > 0.0f) {
            const double p = counts[i] / total;
            window.entropy -= static_cast<float>(p * std::log2(p));
        }
    }
    window.mean = static_cast<float>(sum / total);

    double variance = 0.0;
    for (int i = 0; i < histogram.bins; ++i) {
        const double delta = value_at(i) - window.mean;
        variance += static_cast<double>(counts[i]) * delta * delta;
    }
    window.deviation = static_cast<float>(std::sqrt(variance / total));
}

void add_stage(App& app, OpParams params) {
    const int viewed_id = (app.viewed >= 0 && app.viewed < static_cast<int>(app.chain.stages.size()))
                              ? app.chain.stages[app.viewed].id
                              : 0;
    const int index = app.chain.index_of(app.chain.add(std::move(params), viewed_id));
    app.viewed = index;
    app.evaluate();
}

}  // namespace

void draw_histogram_window(HistogramWindow& window, App& app) {
    if (!window.open) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(820.0f, 660.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Histograma", &window.open)) {
        ImGui::End();
        return;
    }

    const bool has_stage = app.viewed >= 0 &&
                           app.viewed < static_cast<int>(app.chain.outputs.size()) &&
                           !app.chain.outputs[app.viewed].empty();
    if (!has_stage) {
        ImGui::TextDisabled("Nenhum estágio pra medir.");
        ImGui::End();
        return;
    }

    const Value& value = app.chain.outputs[app.viewed];
    if (window.seen_revision != app.revision || window.seen_stage != app.viewed ||
        window.seen_srgb != window.srgb || window.seen_bins != window.bins) {
        recompute(window, value);
        window.seen_revision = app.revision;
        window.seen_stage = app.viewed;
        window.seen_srgb = window.srgb;
        window.seen_bins = window.bins;
    }

    ImGui::Text("estágio %d  %s", app.viewed, op_info(app.chain.stages[app.viewed].params).name);
    ImGui::SameLine();
    ImGui::TextDisabled("(%s, %.4f a %.4f)", kind_name(value.kind), window.histogram.lo,
                        window.histogram.hi);

    // O que vem embaixo do gráfico tem altura fixa; a curva fica com o resto,
    // mas nunca com menos que dá pra ler.
    const float below = 270.0f;
    const float plot = std::max(130.0f, ImGui::GetContentRegionAvail().y - below);
    draw_histogram(window.histogram, window.log_scale, plot, window.visible,
                   window.has_otsu ? window.otsu : NAN);

    ImGui::Spacing();
    ImGui::Checkbox("escala log", &window.log_scale);
    ImGui::SameLine();
    if (value.kind == ValueKind::Color) {
        if (ImGui::Checkbox("faixas em sRGB", &window.srgb)) {
            window.seen_srgb = !window.srgb;
        }
        ImGui::SameLine();
        ImGui::Checkbox("R", &window.visible[0]);
        ImGui::SameLine();
        ImGui::Checkbox("G", &window.visible[1]);
        ImGui::SameLine();
        ImGui::Checkbox("B", &window.visible[2]);
        ImGui::SameLine();
        ImGui::Checkbox("luz", &window.visible[3]);
    } else {
        ImGui::TextDisabled("um canal só");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::BeginTable("stats", 4, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();
        ImGui::TextDisabled("média");
        ImGui::Text("%.4f", window.mean);
        ImGui::TableNextColumn();
        ImGui::TextDisabled("mediana");
        ImGui::Text("%.4f", window.median);
        ImGui::TableNextColumn();
        ImGui::TextDisabled("desvio");
        ImGui::Text("%.4f", window.deviation);
        ImGui::TableNextColumn();
        ImGui::TextDisabled("entropia");
        ImGui::Text("%.3f bits", window.entropy);
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    const bool tone_ok = value.kind != ValueKind::Label;
    ImGui::BeginDisabled(!tone_ok);
    if (ImGui::Button("Equalizar")) {
        add_stage(app, EqualizeOp{});
    }
    ImGui::SameLine();
    ImGui::TextDisabled("espalha pela acumulada, o pico vira rampa");

    ImGui::SetNextItemWidth(160.0f);
    ImGui::DragFloatRange2("percentis", &window.stretch_low, &window.stretch_high, 0.05f, 0.0f,
                           100.0f, "%.2f", "%.2f");
    ImGui::SameLine();
    if (ImGui::Button("Alongar contraste")) {
        add_stage(app, StretchOp{window.stretch_low, window.stretch_high});
    }
    ImGui::EndDisabled();

    ImGui::Spacing();
    if (window.has_otsu) {
        ImGui::Text("Otsu: %.4f", window.otsu);
        ImGui::SameLine();
        if (ImGui::Button("Binarizar aí")) {
            ThresholdOp op;
            op.otsu = true;
            op.level = window.otsu;
            add_stage(app, op);
        }
    } else {
        ImGui::TextDisabled("Otsu precisa de um estágio escalar.");
    }

    ImGui::End();
}
