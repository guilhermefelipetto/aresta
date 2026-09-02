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

#include "canvas.h"
#include "texture.h"
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

    Texture texture;
    Canvas canvas;
    std::string current_path;
    std::string status;

    auto open_path = [&](const std::string& path) {
        std::string error;
        Texture loaded = load_image(path, error);
        if (!loaded.valid()) {
            status = "Não abriu " + path + ": " + error;
            return false;
        }
        texture = std::move(loaded);
        canvas.needs_fit = true;
        current_path = path;
        status.clear();
        SDL_SetWindowTitle(window, ("aresta — " + path).c_str());
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
                if (ImGui::MenuItem("Enquadrar", "F", false, texture.valid())) {
                    canvas.needs_fit = true;
                }
                if (ImGui::MenuItem("Tamanho real", "1", false, texture.valid())) {
                    canvas_zoom_to(canvas, 1.0f);
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
                if (!status.empty()) {
                    ImGui::TextColored(ImVec4(0.9f, 0.45f, 0.45f, 1.0f), "%s", status.c_str());
                } else if (texture.valid()) {
                    ImGui::Text("%d x %d", texture.width, texture.height);
                    ImGui::TextDisabled("|");
                    ImGui::Text("%.0f%%", canvas.zoom * 100.0f);
                    if (canvas.hovering) {
                        ImGui::TextDisabled("|");
                        ImGui::Text("%d, %d", canvas.hover_x, canvas.hover_y);
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
                status.clear();
                ImGui::CloseCurrentPopup();
            }
            if (!status.empty()) {
                ImGui::TextColored(ImVec4(0.9f, 0.45f, 0.45f, 1.0f), "%s", status.c_str());
            }
            ImGui::EndPopup();
        }

        ImGui::Begin("Ferramentas");
        if (!texture.valid()) {
            ImGui::TextDisabled("Nenhuma imagem.");
        } else {
            if (ImGui::Button("Enquadrar", ImVec2(-1.0f, 0.0f))) {
                canvas.needs_fit = true;
            }
            if (ImGui::Button("Tamanho real", ImVec2(-1.0f, 0.0f))) {
                canvas_zoom_to(canvas, 1.0f);
            }
            ImGui::Spacing();
            ImGui::TextUnformatted("Zoom");
            float percent = canvas.zoom * 100.0f;
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::SliderFloat("##zoom", &percent, 2.0f, 6400.0f, "%.0f%%",
                                   ImGuiSliderFlags_Logarithmic)) {
                canvas_zoom_to(canvas, percent / 100.0f);
            }
        }
        ImGui::End();

        ImGui::Begin("Propriedades");
        if (!texture.valid()) {
            ImGui::TextDisabled("Nenhuma imagem.");
        } else {
            ImGui::TextDisabled("Arquivo");
            ImGui::TextWrapped("%s", current_path.c_str());
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::TextDisabled("Dimensões");
            ImGui::Text("%d x %d", texture.width, texture.height);
            ImGui::Spacing();
            ImGui::TextDisabled("Formato na GPU");
            ImGui::TextUnformatted("RGBA, 8 bits por canal");
        }
        ImGui::End();

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("Imagem");
        draw_canvas(canvas, texture);
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
