#include "ui.h"

#include <cstdlib>
#include <filesystem>
#include <cmath>
#include <system_error>

#include <imgui_internal.h>

#include "histogram.h"

namespace {

const char* const font_candidates[] = {
    "/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
};

void load_font() {
    ImGuiIO& io = ImGui::GetIO();
    for (const char* path : font_candidates) {
        if (std::filesystem::exists(path)) {
            io.Fonts->AddFontFromFileTTF(path, 16.0f);
            return;
        }
    }
}

std::filesystem::path config_dir() {
    const char* home = std::getenv("HOME");
    return home ? std::filesystem::path(home) / ".config" / "aresta"
                : std::filesystem::path(".");
}

}  // namespace

std::string layout_path() {
    const std::filesystem::path dir = config_dir();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return (dir / ("layout-" + std::to_string(layout_version) + ".ini")).string();
}

void forget_old_layouts() {
    std::error_code ec;
    const std::filesystem::path keep = std::filesystem::path(layout_path()).filename();
    for (const auto& entry : std::filesystem::directory_iterator(config_dir(), ec)) {
        const std::string name = entry.path().filename().string();
        if (name.rfind("layout-", 0) == 0 && name.size() > 11 &&
            name.compare(name.size() - 4, 4, ".ini") == 0 && entry.path().filename() != keep) {
            std::filesystem::remove(entry.path(), ec);
        }
    }
}

void apply_theme() {
    load_font();

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowPadding = ImVec2(10.0f, 10.0f);
    style.FramePadding = ImVec2(8.0f, 5.0f);
    style.ItemSpacing = ImVec2(8.0f, 7.0f);
    style.WindowBorderSize = 0.0f;
    style.FrameBorderSize = 0.0f;
    style.WindowRounding = 0.0f;
    style.ChildRounding = 3.0f;
    style.FrameRounding = 3.0f;
    style.GrabRounding = 3.0f;
    style.PopupRounding = 3.0f;
    style.TabRounding = 3.0f;
    style.ScrollbarRounding = 3.0f;
    style.ScrollbarSize = 12.0f;

    ImVec4* c = style.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(0.13f, 0.13f, 0.14f, 1.00f);
    c[ImGuiCol_ChildBg] = ImVec4(0.13f, 0.13f, 0.14f, 1.00f);
    c[ImGuiCol_PopupBg] = ImVec4(0.16f, 0.16f, 0.17f, 1.00f);
    c[ImGuiCol_MenuBarBg] = ImVec4(0.16f, 0.16f, 0.17f, 1.00f);
    c[ImGuiCol_TitleBg] = ImVec4(0.11f, 0.11f, 0.12f, 1.00f);
    c[ImGuiCol_TitleBgActive] = ImVec4(0.16f, 0.16f, 0.17f, 1.00f);
    c[ImGuiCol_FrameBg] = ImVec4(0.20f, 0.20f, 0.22f, 1.00f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.25f, 0.25f, 0.27f, 1.00f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.29f, 0.29f, 0.31f, 1.00f);
    c[ImGuiCol_Button] = ImVec4(0.22f, 0.22f, 0.24f, 1.00f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.28f, 0.30f, 0.36f, 1.00f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.33f, 0.37f, 0.46f, 1.00f);
    c[ImGuiCol_Header] = ImVec4(0.24f, 0.26f, 0.32f, 1.00f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.29f, 0.32f, 0.40f, 1.00f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.33f, 0.37f, 0.46f, 1.00f);
    c[ImGuiCol_Separator] = ImVec4(0.24f, 0.24f, 0.26f, 1.00f);
    c[ImGuiCol_SliderGrab] = ImVec4(0.45f, 0.50f, 0.62f, 1.00f);
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.55f, 0.60f, 0.72f, 1.00f);
    c[ImGuiCol_CheckMark] = ImVec4(0.60f, 0.66f, 0.80f, 1.00f);
    c[ImGuiCol_DockingEmptyBg] = ImVec4(0.10f, 0.10f, 0.11f, 1.00f);
}

void build_default_layout(ImGuiID dockspace) {
    ImGui::DockBuilderRemoveNode(dockspace);
    ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace, ImGui::GetMainViewport()->WorkSize);

    ImGuiID center = dockspace;
    const ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.17f, nullptr, &center);
    ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.26f, nullptr, &center);
    const ImGuiID right_bottom =
        ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.55f, nullptr, &right);

    ImGui::DockBuilderDockWindow("Vista", left);
    ImGui::DockBuilderDockWindow("Propriedades", right);
    ImGui::DockBuilderDockWindow("Cadeia", right_bottom);
    ImGui::DockBuilderDockWindow("Imagem", center);
    ImGui::DockBuilderFinish(dockspace);
}

void draw_histogram(const Histogram& histogram, bool log_scale, float height,
                    const bool* channel_visible, float marker) {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    if (width <= 1.0f || height <= 1.0f) {
        return;
    }
    ImGui::Dummy(ImVec2(width, height));

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 max(origin.x + width, origin.y + height);
    draw->AddRectFilled(origin, max, IM_COL32(18, 18, 20, 255));

    if (histogram.empty() || histogram.peak <= 0.0f) {
        return;
    }

    // Contagem de pixel tem cauda longa: sem log, o pico de um fundo liso
    // achata todo o resto contra o eixo.
    const float ceiling = log_scale ? std::log1p(histogram.peak) : histogram.peak;

    static const ImU32 tints[4] = {IM_COL32(230, 90, 90, 190), IM_COL32(90, 210, 120, 190),
                                   IM_COL32(100, 140, 240, 190), IM_COL32(190, 190, 200, 210)};

    for (int c = 0; c < histogram.channels; ++c) {
        if (channel_visible && !channel_visible[c]) {
            continue;
        }
        const ImU32 tint = histogram.channels == 1 ? tints[3] : tints[c];
        const float* counts = histogram.channel(c);
        for (int i = 0; i < histogram.bins; ++i) {
            const float raw = log_scale ? std::log1p(counts[i]) : counts[i];
            const float share = raw / ceiling;
            if (share <= 0.0f) {
                continue;
            }
            const float x0 = origin.x + width * static_cast<float>(i) / histogram.bins;
            const float x1 = origin.x + width * static_cast<float>(i + 1) / histogram.bins;
            draw->AddRectFilled(ImVec2(x0, max.y - height * share), ImVec2(x1, max.y), tint);
        }
    }

    if (std::isfinite(marker) && histogram.hi > histogram.lo) {
        const float share = (marker - histogram.lo) / (histogram.hi - histogram.lo);
        if (share >= 0.0f && share <= 1.0f) {
            const float x = origin.x + width * share;
            draw->AddLine(ImVec2(x, origin.y), ImVec2(x, max.y), IM_COL32(250, 200, 90, 220), 1.5f);
        }
    }

    draw->AddRect(origin, max, IM_COL32(60, 60, 68, 255));
}
