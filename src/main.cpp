#include <cstdio>
#include <filesystem>
#include <string>
#include <utility>

#include <SDL.h>
#include <SDL_opengl.h>

#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_opengl3.h>

// BeginViewportSideBar, que é o jeito de pendurar a barra de status na
// borda da viewport, ainda mora no header interno.
#include <imgui_internal.h>

#include "app.h"
#include "canvas.h"
#include "chain.h"
#include "ui.h"

int main(int argc, char** argv) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init falhou: %s\n", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    SDL_Window* window = SDL_CreateWindow(
        "aresta",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1440, 900,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) {
        std::fprintf(stderr, "SDL_CreateWindow falhou: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_GLContext gl = SDL_GL_CreateContext(window);
    if (!gl) {
        std::fprintf(stderr, "SDL_GL_CreateContext falhou: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_MakeCurrent(window, gl);
    SDL_GL_SetSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    apply_theme();

    ImGui_ImplSDL2_InitForOpenGL(window, gl);
    ImGui_ImplOpenGL3_Init("#version 150");

    // Sem ini gravado, o layout vem do código. Com ini, respeita o que o usuário
    // arrastou da última vez.
    bool layout_pending = !std::filesystem::exists(ImGui::GetIO().IniFilename);

    App app;

    auto open_path = [&](const std::string& file) {
        if (!app.open(file)) {
            return false;
        }
        SDL_SetWindowTitle(window, ("aresta — " + file).c_str());
        return true;
    };

    if (argc > 1) {
        open_path(argv[1]);
    }

    char path_input[1024] = {};
    bool open_requested = false;
    bool rodando = true;

    while (rodando) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            ImGui_ImplSDL2_ProcessEvent(&ev);
            if (ev.type == SDL_QUIT) {
                rodando = false;
            }
            if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_CLOSE
                && ev.window.windowID == SDL_GetWindowID(window)) {
                rodando = false;
            }
            if (ev.type == SDL_DROPFILE) {
                open_path(ev.drop.file);
                SDL_free(ev.drop.file);
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        const ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl && !io.WantTextInput) {
            if (ImGui::IsKeyPressed(ImGuiKey_O)) {
                open_requested = true;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Q)) {
                rodando = false;
            }
        }

        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("Arquivo")) {
                if (ImGui::MenuItem("Abrir...", "Ctrl+O")) {
                    open_requested = true;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Sair", "Ctrl+Q")) {
                    rodando = false;
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Ver")) {
                if (ImGui::MenuItem("Enquadrar", "F", false, app.texture.valid())) {
                    app.canvas.needs_fit = true;
                }
                if (ImGui::MenuItem("Tamanho real", "1", false, app.texture.valid())) {
                    canvas_zoom_to(app.canvas, 1.0f);
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Janela")) {
                if (ImGui::MenuItem("Restaurar layout")) {
                    layout_pending = true;
                }
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        const float bar_height = ImGui::GetFrameHeight();
        if (ImGui::BeginViewportSideBar("##status", ImGui::GetMainViewport(), ImGuiDir_Down,
                                        bar_height, ImGuiWindowFlags_NoSavedSettings |
                                                        ImGuiWindowFlags_MenuBar)) {
            if (ImGui::BeginMenuBar()) {
                if (!app.status.empty()) {
                    ImGui::TextColored(ImVec4(0.9f, 0.45f, 0.45f, 1.0f), "%s", app.status.c_str());
                } else if (app.texture.valid()) {
                    ImGui::Text("%d x %d", app.texture.width, app.texture.height);
                    ImGui::TextDisabled("|");
                    ImGui::Text("%.0f%%", app.canvas.zoom * 100.0f);
                    if (app.canvas.hovering) {
                        ImGui::TextDisabled("|");
                        ImGui::Text("%d, %d", app.canvas.hover_x, app.canvas.hover_y);
                    }
                } else {
                    ImGui::TextDisabled("nenhuma imagem aberta");
                }
                ImGui::EndMenuBar();
            }
            ImGui::End();
        }

        const ImGuiID dockspace = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
        if (layout_pending) {
            build_default_layout(dockspace);
            layout_pending = false;
        }

        if (open_requested) {
            ImGui::OpenPopup("Abrir imagem");
            open_requested = false;
        }
        if (ImGui::BeginPopupModal("Abrir imagem", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Caminho do arquivo:");
            ImGui::SetNextItemWidth(460.0f);
            const bool submitted = ImGui::InputText("##caminho", path_input, sizeof(path_input),
                                                    ImGuiInputTextFlags_EnterReturnsTrue);
            if (ImGui::Button("Abrir") || submitted) {
                if (open_path(path_input)) {
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancelar")) {
                app.status.clear();
                ImGui::CloseCurrentPopup();
            }
            if (!app.status.empty()) {
                ImGui::TextColored(ImVec4(0.9f, 0.45f, 0.45f, 1.0f), "%s", app.status.c_str());
            }
            ImGui::EndPopup();
        }

        ImGui::Begin("Ferramentas");
        if (!app.texture.valid()) {
            ImGui::TextDisabled("Nenhuma imagem.");
        } else {
            if (ImGui::Button("Enquadrar", ImVec2(-1.0f, 0.0f))) {
                app.canvas.needs_fit = true;
            }
            if (ImGui::Button("Tamanho real", ImVec2(-1.0f, 0.0f))) {
                canvas_zoom_to(app.canvas, 1.0f);
            }
            ImGui::Spacing();
            ImGui::TextDisabled("Zoom");
            float percent = app.canvas.zoom * 100.0f;
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::SliderFloat("##zoom", &percent, 2.0f, 6400.0f, "%.0f%%",
                                   ImGuiSliderFlags_Logarithmic)) {
                canvas_zoom_to(app.canvas, percent / 100.0f);
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::TextDisabled("Mapa de cor");
            ImGui::SetNextItemWidth(-1.0f);
            int colormap = static_cast<int>(app.colormap);
            if (ImGui::Combo("##colormap", &colormap, "cinza\0viridis\0")) {
                app.colormap = static_cast<Colormap>(colormap);
                app.upload_view();
            }
            ImGui::TextDisabled("Vale pro estágio escalar.");
        }
        ImGui::End();

        ImGui::Begin("Propriedades");
        if (app.source.empty()) {
            ImGui::TextDisabled("Nenhuma imagem.");
        } else {
            ImGui::TextDisabled("Arquivo");
            ImGui::TextWrapped("%s", app.path.c_str());
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::TextDisabled("Dimensões");
            ImGui::Text("%d x %d", app.source.width, app.source.height);
            ImGui::Spacing();
            ImGui::TextDisabled("Buffer");
            ImGui::TextUnformatted("RGBA float32, linear");
            ImGui::Text("%.1f MB", static_cast<double>(app.source.width) * app.source.height * 4 *
                                       sizeof(float) / (1024.0 * 1024.0));
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::TextDisabled("Estágio exibido");
            if (app.viewed >= 0 && app.viewed < static_cast<int>(app.chain.stages.size())) {
                const OpInfo info = op_info(app.chain.stages[app.viewed].params);
                ImGui::Text("%d  %s", app.viewed, info.name);
                ImGui::Text("tipo   %s", kind_name(info.output));
                if (info.output != ValueKind::Color) {
                    ImGui::Text("faixa  %.4f .. %.4f", app.view_lo, app.view_hi);
                }
            }
        }
        ImGui::End();

        ImGui::Begin("Cadeia");
        if (app.source.empty()) {
            ImGui::TextDisabled("Nenhuma imagem.");
        } else {
            bool dirty = false;

            if (ImGui::Button("Adicionar operação", ImVec2(-1.0f, 0.0f))) {
                ImGui::OpenPopup("adicionar");
            }
            if (ImGui::BeginPopup("adicionar")) {
                if (ImGui::MenuItem("exposição")) { app.chain.add(ExposureOp{}); dirty = true; }
                if (ImGui::MenuItem("contraste")) { app.chain.add(ContrastOp{}); dirty = true; }
                if (ImGui::MenuItem("gama")) { app.chain.add(GammaOp{}); dirty = true; }
                if (ImGui::MenuItem("inverter")) { app.chain.add(InvertOp{}); dirty = true; }
                ImGui::Separator();
                if (ImGui::MenuItem("luminância")) { app.chain.add(LuminanceOp{}); dirty = true; }
                if (ImGui::MenuItem("threshold")) { app.chain.add(ThresholdOp{}); dirty = true; }
                if (ImGui::MenuItem("overlay")) { app.chain.add(OverlayOp{}); dirty = true; }
                ImGui::EndPopup();
            }

            ImGui::Spacing();
            int to_remove = -1;

            for (std::size_t i = 0; i < app.chain.stages.size(); ++i) {
                Stage& stage = app.chain.stages[i];
                const OpInfo info = op_info(stage.params);
                ImGui::PushID(stage.id);

                char label[160];
                std::snprintf(label, sizeof(label), "%zu   %-11s -> %s", i, info.name,
                              kind_name(info.output));
                if (ImGui::Selectable(label, app.viewed == static_cast<int>(i))) {
                    app.viewed = static_cast<int>(i);
                    app.upload_view();
                }

                ImGui::Indent();

                for (int k = 0; k < info.input_count; ++k) {
                    const int current =
                        (k < static_cast<int>(stage.inputs.size())) ? stage.inputs[k] : -1;
                    const int current_idx = app.chain.index_of(current);
                    char preview[96];
                    if (current_idx >= 0) {
                        std::snprintf(preview, sizeof(preview), "entrada: %d %s", current_idx,
                                      op_info(app.chain.stages[current_idx].params).name);
                    } else {
                        std::snprintf(preview, sizeof(preview), "entrada: (nada)");
                    }

                    ImGui::PushID(k);
                    ImGui::SetNextItemWidth(-1.0f);
                    if (ImGui::BeginCombo("##entrada", preview)) {
                        for (std::size_t j = 0; j < i; ++j) {
                            if (op_info(app.chain.stages[j].params).output != info.inputs[k]) {
                                continue;
                            }
                            char option[96];
                            std::snprintf(option, sizeof(option), "%zu %s", j,
                                          op_info(app.chain.stages[j].params).name);
                            if (ImGui::Selectable(option, app.chain.stages[j].id == current)) {
                                if (k < static_cast<int>(stage.inputs.size())) {
                                    stage.inputs[k] = app.chain.stages[j].id;
                                    dirty = true;
                                }
                            }
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::PopID();
                }

                ImGui::SetNextItemWidth(-1.0f);
                if (auto* op = std::get_if<ExposureOp>(&stage.params)) {
                    dirty |= ImGui::SliderFloat("##p", &op->stops, -3.0f, 3.0f, "%.2f EV");
                } else if (auto* op = std::get_if<ContrastOp>(&stage.params)) {
                    dirty |= ImGui::SliderFloat("##p", &op->amount, -0.9f, 2.0f, "%.2f");
                } else if (auto* op = std::get_if<GammaOp>(&stage.params)) {
                    dirty |= ImGui::SliderFloat("##p", &op->gamma, 0.2f, 4.0f, "%.2f");
                } else if (auto* op = std::get_if<ThresholdOp>(&stage.params)) {
                    dirty |= ImGui::SliderFloat("##p", &op->level, 0.0f, 1.0f, "%.3f");
                } else if (auto* op = std::get_if<OverlayOp>(&stage.params)) {
                    dirty |= ImGui::SliderFloat("##p", &op->opacity, 0.0f, 1.0f, "%.2f");
                }

                if (stage.id != 0) {
                    if (ImGui::Checkbox("ativo", &stage.enabled)) {
                        dirty = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("remover")) {
                        to_remove = stage.id;
                    }
                }

                if (!stage.error.empty()) {
                    ImGui::TextColored(ImVec4(0.9f, 0.45f, 0.45f, 1.0f), "%s", stage.error.c_str());
                }

                ImGui::Unindent();
                ImGui::Separator();
                ImGui::PopID();
            }

            if (to_remove >= 0) {
                app.chain.remove(to_remove);
                if (app.viewed >= static_cast<int>(app.chain.stages.size())) {
                    app.viewed = static_cast<int>(app.chain.stages.size()) - 1;
                }
                dirty = true;
            }
            if (dirty) {
                app.evaluate();
            }
        }
        ImGui::End();

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("Imagem");
        draw_canvas(app.canvas, app.texture);
        ImGui::End();
        ImGui::PopStyleVar();

        ImGui::Render();

        int w, h;
        SDL_GetWindowSizeInPixels(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.11f, 0.11f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DeleteContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
