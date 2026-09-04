#include "export_window.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include <imgui.h>

#include "app.h"
#include "chain.h"
#include "dialog.h"

namespace {

std::string slug(const std::string& text) {
    std::string out;
    for (char c : text) {
        out += (c == ' ' || c == '/') ? '-' : c;
    }
    return out;
}

void pick_defaults(ExportWindow& window, App& app) {
    window.stage = app.shown();
    window.raw = false;
    window.format = ExportFormat::Png;
    window.message.clear();
    window.failed = false;

    std::snprintf(window.folder, sizeof(window.folder), "%s", downloads_folder().c_str());

    std::string base = std::filesystem::path(app.path).stem().string();
    if (base.empty()) {
        base = "aresta";
    }
    std::string suffix;
    if (window.stage > 0 && window.stage < static_cast<int>(app.chain.stages.size())) {
        suffix = "-" + slug(op_info(app.chain.stages[window.stage].params).name);
    }
    std::snprintf(window.name, sizeof(window.name), "%s%s", base.c_str(), suffix.c_str());
}

}  // namespace

void open_export_window(ExportWindow& window, App& app) {
    pick_defaults(window, app);
    window.open = true;
}

void draw_export_window(ExportWindow& window, App& app) {
    if (!window.open) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(620.0f, 0.0f), ImGuiCond_Always);
    if (!ImGui::Begin("Exportar", &window.open, ImGuiWindowFlags_NoResize)) {
        ImGui::End();
        return;
    }

    if (app.chain.outputs.empty()) {
        ImGui::TextDisabled("Nada pra exportar.");
        ImGui::End();
        return;
    }

    window.stage = std::clamp(window.stage, 0, static_cast<int>(app.chain.stages.size()) - 1);

    char preview[128];
    std::snprintf(preview, sizeof(preview), "%d  %s", window.stage,
                  op_info(app.chain.stages[window.stage].params).name);
    ImGui::SetNextItemWidth(-160.0f);
    if (ImGui::BeginCombo("estágio", preview)) {
        for (std::size_t i = 0; i < app.chain.stages.size(); ++i) {
            char option[128];
            std::snprintf(option, sizeof(option), "%zu  %-12s (%s)", i,
                          op_info(app.chain.stages[i].params).name,
                          kind_name(app.chain.kind_of(static_cast<int>(i))));
            if (ImGui::Selectable(option, window.stage == static_cast<int>(i))) {
                window.stage = static_cast<int>(i);
            }
        }
        ImGui::EndCombo();
    }

    const Value& value = app.chain.outputs[static_cast<std::size_t>(window.stage)];
    const ValueKind kind = value.empty() ? ValueKind::Color : value.kind;

    ImGui::Spacing();
    int content = window.raw ? 1 : 0;
    ImGui::RadioButton("como aparece na tela", &content, 0);
    ImGui::SameLine();
    ImGui::RadioButton("valores crus", &content, 1);
    window.raw = content == 1;
    ImGui::TextDisabled(window.raw
                            ? "o número que o estágio carrega, sem colormap e sem cortar"
                            : "RGBA de 8 bits igual ao canvas, com colormap se for escalar");

    ImGui::Spacing();
    static const ExportFormat todos[] = {ExportFormat::Png,  ExportFormat::Jpeg,
                                         ExportFormat::Bmp,  ExportFormat::Tga,
                                         ExportFormat::Pnm,  ExportFormat::Pfm,
                                         ExportFormat::Csv,  ExportFormat::Npy};
    if (!format_supports(window.format, kind, window.raw)) {
        for (ExportFormat candidate : todos) {
            if (format_supports(candidate, kind, window.raw)) {
                window.format = candidate;
                break;
            }
        }
    }
    ImGui::SetNextItemWidth(-160.0f);
    if (ImGui::BeginCombo("formato", format_name(window.format))) {
        for (ExportFormat candidate : todos) {
            if (!format_supports(candidate, kind, window.raw)) {
                continue;
            }
            if (ImGui::Selectable(format_name(candidate), candidate == window.format)) {
                window.format = candidate;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::TextDisabled("%s", format_note(window.format));

    if (window.format == ExportFormat::Jpeg) {
        ImGui::SetNextItemWidth(-160.0f);
        ImGui::SliderInt("qualidade", &window.quality, 20, 100);
    }

    ImGui::Spacing();
    ImGui::SetNextItemWidth(-160.0f);
    ImGui::InputText("pasta", window.folder, sizeof(window.folder));
    ImGui::SetNextItemWidth(-160.0f);
    ImGui::InputText("nome", window.name, sizeof(window.name));

    const std::string path = std::string(window.folder) + "/" + window.name + "." +
                             format_extension(window.format);
    ImGui::Spacing();
    ImGui::TextDisabled("%s", path.c_str());

    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
        ImGui::TextColored(ImVec4(0.9f, 0.75f, 0.4f, 1.0f), "já existe, vai por cima");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Exportar", ImVec2(140.0f, 0.0f))) {
        std::string error;
        const bool ok = export_value(value, path, window.format, window.raw, window.quality,
                                     app.colormap, &error);
        window.failed = !ok;
        window.message = ok ? ("Salvou em " + path) : error;
        if (ok) {
            window.open = false;
            app.status = "Exportado: " + path;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancelar", ImVec2(140.0f, 0.0f))) {
        window.open = false;
    }

    if (!window.message.empty()) {
        ImGui::TextColored(window.failed ? ImVec4(0.9f, 0.45f, 0.45f, 1.0f)
                                         : ImVec4(0.5f, 0.8f, 0.5f, 1.0f),
                           "%s", window.message.c_str());
    }

    ImGui::End();
}
