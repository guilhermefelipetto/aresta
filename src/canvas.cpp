#include "canvas.h"

#include "texture.h"

#include <algorithm>
#include <cmath>

#include <imgui.h>

namespace {

constexpr float zoom_min = 0.02f;
constexpr float zoom_max = 64.0f;

void fit_to_area(Canvas& canvas, const Texture& texture, ImVec2 area) {
    const float scale_x = area.x / static_cast<float>(texture.width);
    const float scale_y = area.y / static_cast<float>(texture.height);

    // Teto em 1.0: enquadrar não deveria ampliar imagem pequena.
    canvas.zoom = std::clamp(std::min(scale_x, scale_y), zoom_min, 1.0f);
    canvas.pan_x = (area.x - texture.width * canvas.zoom) * 0.5f;
    canvas.pan_y = (area.y - texture.height * canvas.zoom) * 0.5f;
}

// Muda o zoom deixando parado o ponto da imagem que está sob (focus_x, focus_y),
// que vem em coordenadas do painel.
void zoom_at(Canvas& canvas, float new_zoom, float focus_x, float focus_y) {
    new_zoom = std::clamp(new_zoom, zoom_min, zoom_max);
    const float image_x = (focus_x - canvas.pan_x) / canvas.zoom;
    const float image_y = (focus_y - canvas.pan_y) / canvas.zoom;
    canvas.zoom = new_zoom;
    canvas.pan_x = focus_x - image_x * new_zoom;
    canvas.pan_y = focus_y - image_y * new_zoom;
}

}  // namespace

void canvas_zoom_to(Canvas& canvas, float zoom) {
    zoom_at(canvas, zoom, canvas.view_w * 0.5f, canvas.view_h * 0.5f);
}

void draw_canvas(Canvas& canvas, const Texture& texture) {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 area = ImGui::GetContentRegionAvail();
    canvas.hovering = false;
    if (area.x <= 0.0f || area.y <= 0.0f) {
        return;
    }

    // No quadro em que o dock é montado o painel ainda não tem o tamanho final,
    // e enquadrar contra ele trava o zoom no piso. Espera dois quadros iguais.
    const bool settled = (area.x == canvas.view_w && area.y == canvas.view_h);
    canvas.view_w = area.x;
    canvas.view_h = area.y;

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 canvas_max(origin.x + area.x, origin.y + area.y);
    draw->AddRectFilled(origin, canvas_max, IM_COL32(24, 24, 26, 255));

    if (!texture.valid()) {
        const char* hint = "Arraste uma imagem aqui, ou Arquivo > Abrir.";
        const ImVec2 size = ImGui::CalcTextSize(hint);
        draw->AddText(ImVec2(origin.x + (area.x - size.x) * 0.5f,
                             origin.y + (area.y - size.y) * 0.5f),
                      IM_COL32(110, 110, 118, 255), hint);
        return;
    }

    if (canvas.needs_fit && settled) {
        fit_to_area(canvas, texture, area);
        canvas.needs_fit = false;
    }

    ImGui::InvisibleButton("canvas", area,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    const ImGuiIO& io = ImGui::GetIO();

    if (ImGui::IsItemActive() && (ImGui::IsMouseDragging(ImGuiMouseButton_Left) ||
                                  ImGui::IsMouseDragging(ImGuiMouseButton_Middle))) {
        canvas.pan_x += io.MouseDelta.x;
        canvas.pan_y += io.MouseDelta.y;
    }

    if (hovered) {
        const float focus_x = io.MousePos.x - origin.x;
        const float focus_y = io.MousePos.y - origin.y;

        if (io.MouseWheel != 0.0f) {
            zoom_at(canvas, canvas.zoom * std::pow(1.15f, io.MouseWheel), focus_x, focus_y);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_F)) {
            canvas.needs_fit = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_1)) {
            zoom_at(canvas, 1.0f, focus_x, focus_y);
        }
    }

    const ImVec2 image_min(origin.x + canvas.pan_x, origin.y + canvas.pan_y);
    const ImVec2 image_max(image_min.x + texture.width * canvas.zoom,
                           image_min.y + texture.height * canvas.zoom);

    if (hovered) {
        const int px = static_cast<int>(std::floor((io.MousePos.x - image_min.x) / canvas.zoom));
        const int py = static_cast<int>(std::floor((io.MousePos.y - image_min.y) / canvas.zoom));
        if (px >= 0 && py >= 0 && px < texture.width && py < texture.height) {
            canvas.hovering = true;
            canvas.hover_x = px;
            canvas.hover_y = py;
        }
    }

    draw->PushClipRect(origin, canvas_max, true);
    draw->AddImage(static_cast<ImTextureID>(texture.id), image_min, image_max);

    // Contorno da imagem, pra não sumir contra o fundo quando ela é escura.
    draw->AddRect(image_min, image_max, IM_COL32(70, 70, 78, 255));
    draw->PopClipRect();
}
