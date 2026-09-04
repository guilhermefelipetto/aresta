#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

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
#include "chain_panel.h"
#include "components_window.h"
#include "curve_window.h"
#include "dialog.h"
#include "export_window.h"
#include "histogram_window.h"
#include "kernel_window.h"
#include "project.h"
#include "sheet_window.h"
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

    // O ImGui guarda o ponteiro, não a string, então ela precisa viver o
    // programa inteiro.
    const std::string ini = layout_path();
    ImGui::GetIO().IniFilename = ini.c_str();
    forget_old_layouts();

    ImGui_ImplSDL2_InitForOpenGL(window, gl);
    ImGui_ImplOpenGL3_Init("#version 150");

    // Sem ini gravado, o layout vem do código. Com ini, respeita o que o usuário
    // arrastou da última vez.
    bool layout_pending = !std::filesystem::exists(ini);

    Workspace ws;
    KernelLibrary kernel_library;
    kernel_library.load();
    KernelWindow kernel_window;
    HistogramWindow histogram_window;
    CurveWindow curve_window;
    ComponentsWindow components_window;
    ExportWindow export_window;
    SheetWindow sheet_window;

    bool chain_dirty = false;
    int fechar_aba = -1;
    int fechar_confirmado = -1;
    bool sair_pedido = false;
    char project_input[1024] = {};
    char attach_input[1024] = {};
    std::string missing_image;

    const std::vector<FileFilter> filtro_imagem = {
        {"Imagens", {"*.png", "*.jpg", "*.jpeg", "*.bmp", "*.tga", "*.gif", "*.psd", "*.hdr",
                     "*.pgm", "*.ppm", "*.pnm"}}};
    const std::vector<FileFilter> filtro_projeto = {{"Projeto do aresta", {"*.aresta"}}};

    auto set_title = [&] {
        const App& d = ws.doc();
        std::string titulo = d.label() + " - aresta";
        if (d.unsaved) {
            titulo += "*";
        }
        SDL_SetWindowTitle(window, titulo.c_str());
    };

    auto mark_saved = [&] {
        App& d = ws.doc();
        d.saved_state = project_text(d);
        d.unsaved = false;
        set_title();
    };

    // Janela de ferramenta guarda o id do estágio que está dirigindo, e id de
    // outro documento apontaria pra cadeia errada. Trocar de aba solta todas.
    auto detach_tools = [&] {
        kernel_window.editing = -1;
        curve_window.editing = -1;
        components_window.editing = -1;
        histogram_window.seen_revision = -1;
        components_window.seen_revision = -1;
        sheet_window.items.clear();
    };

    auto open_path = [&](const std::string& file, bool nova_aba) {
        App& d = nova_aba ? ws.open_tab() : ws.doc();
        if (!d.open(file)) {
            return false;
        }
        d.project_path.clear();
        detach_tools();
        mark_saved();
        return true;
    };

    auto swap_image = [&](const std::string& file) {
        App& d = ws.doc();
        if (!d.open(file, true)) {
            return false;
        }
        d.evaluate();
        d.upload_view();
        set_title();
        d.say("Imagem trocada, cadeia mantida.");
        return true;
    };

    auto open_project = [&](const std::string& file, bool nova_aba) {
        App& d = nova_aba ? ws.open_tab() : ws.doc();
        const ProjectLoad r = load_project(d, file);
        if (!r.ok) {
            d.status = r.error;
            return false;
        }
        d.project_path = file;
        chain_dirty = false;
        missing_image = r.missing_image ? r.wanted_image : std::string();
        std::snprintf(attach_input, sizeof(attach_input), "%s", r.wanted_image.c_str());
        detach_tools();
        mark_saved();
        if (!r.missing_image) {
            d.say("Projeto carregado.");
        }
        return true;
    };

    auto save_project_to = [&](const std::string& file) {
        App& d = ws.doc();
        std::string erro;
        if (!save_project(d, file, &erro)) {
            d.status = erro;
            return false;
        }
        d.project_path = file;
        mark_saved();
        d.say("Projeto salvo em " + file);
        return true;
    };

    // Abrir arquivo solto: projeto ou imagem, decidido pelo sufixo.
    auto open_any = [&](const std::string& file, bool nova_aba) {
        const std::size_t n = std::strlen(project_suffix());
        const bool eh_projeto =
            file.size() > n
            && file.compare(file.size() - n, std::string::npos, project_suffix()) == 0;
        return eh_projeto ? open_project(file, nova_aba) : open_path(file, nova_aba);
    };

    // Antes de abrir qualquer coisa: sem isso o app vazio já nasceria com
    // asterisco, e pior, isso aqui embaixo do argv apagaria o estado sujo que
    // trocar a imagem pela linha de comando cria de propósito.
    mark_saved();

    if (argc > 1) {
        const std::string arg = argv[1];
        const bool eh_projeto =
            arg.size() > std::strlen(project_suffix())
            && arg.compare(arg.size() - std::strlen(project_suffix()),
                           std::string::npos, project_suffix()) == 0;
        if (eh_projeto) {
            open_project(arg, true);
            // ./aresta projeto.aresta outra.png roda a mesma cadeia noutra
            // imagem, sem precisar salvar um projeto novo pra isso.
            if (argc > 2 && swap_image(argv[2])) {
                missing_image.clear();
            }
        } else {
            open_path(arg, true);
        }
    }

    char path_input[1024] = {};
    char image_input[1024] = {};
    std::string image_input_from;
    bool open_requested = false;
    bool open_project_requested = false;
    bool save_as_requested = false;
    bool rodando = true;

    while (rodando) {
        if (fechar_confirmado >= 0) {
            ws.close_tab(fechar_confirmado);
            fechar_confirmado = -1;
            detach_tools();
            set_title();
        }

        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            ImGui_ImplSDL2_ProcessEvent(&ev);
            if (ev.type == SDL_QUIT) {
                sair_pedido = true;
            }
            if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_CLOSE
                && ev.window.windowID == SDL_GetWindowID(window)) {
                sair_pedido = true;
            }
            if (ev.type == SDL_DROPFILE) {
                open_any(ev.drop.file, true);
                SDL_free(ev.drop.file);
            }
        }

        App& app = ws.doc();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        const ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl && !io.WantTextInput) {
            if (ImGui::IsKeyPressed(ImGuiKey_O)) {
                open_requested = true;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_E) && !app.source.empty()) {
                open_export_window(export_window, app);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Q)) {
                sair_pedido = true;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_S)) {
                if (io.KeyShift || app.project_path.empty()) {
                    save_as_requested = true;
                } else {
                    save_project_to(app.project_path);
                }
            }
        }

        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("Arquivo")) {
                if (ImGui::MenuItem("Abrir...", "Ctrl+O")) {
                    open_requested = true;
                }
                if (ImGui::MenuItem("Exportar como...", "Ctrl+E", false, !app.source.empty())) {
                    open_export_window(export_window, app);
                }
                if (ImGui::MenuItem("Exportar pipeline...", nullptr, false, !app.source.empty())) {
                    open_sheet_window(sheet_window, app);
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Abrir projeto...")) {
                    open_project_requested = true;
                }
                if (ImGui::MenuItem("Salvar projeto", "Ctrl+S")) {
                    if (app.project_path.empty()) {
                        save_as_requested = true;
                    } else {
                        save_project_to(app.project_path);
                    }
                }
                if (ImGui::MenuItem("Salvar projeto como...", "Ctrl+Shift+S")) {
                    save_as_requested = true;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Sair", "Ctrl+Q")) {
                    sair_pedido = true;
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
            if (ImGui::BeginMenu("Operações")) {
                ImGui::BeginDisabled(app.source.empty());
                if (draw_operation_items(app)) {
                    chain_dirty = true;
                }
                ImGui::EndDisabled();
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Ferramentas")) {
                ImGui::MenuItem("Kernel...", nullptr, &kernel_window.open);
                ImGui::MenuItem("Histograma...", nullptr, &histogram_window.open);
                ImGui::MenuItem("Curva...", nullptr, &curve_window.open);
                ImGui::MenuItem("Componentes...", nullptr, &components_window.open);
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
                    const int fixed = app.chain.index_of(app.pinned);
                    if (fixed >= 0) {
                        ImGui::TextDisabled("|");
                        ImGui::Text("preso em %d %s", fixed,
                                    op_info(app.chain.stages[fixed].params).name);
                    }
                    if (app.flashing()) {
                        ImGui::TextDisabled("|");
                        ImGui::TextDisabled("%s", app.flash.c_str());
                    }
                } else if (app.flashing()) {
                    ImGui::TextDisabled("%s", app.flash.c_str());
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
            open_requested = false;
            std::string escolhido;
            const PickResult r =
                pick_open_file("Abrir imagem", app.path, filtro_imagem, &escolhido);
            if (r == PickResult::Chose) {
                open_path(escolhido, true);
            } else if (r == PickResult::Unavailable) {
                ImGui::OpenPopup("Abrir imagem");
            }
        }
        if (ImGui::BeginPopupModal("Abrir imagem", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Caminho do arquivo:");
            ImGui::SetNextItemWidth(460.0f);
            const bool submitted = ImGui::InputText("##caminho", path_input, sizeof(path_input),
                                                    ImGuiInputTextFlags_EnterReturnsTrue);
            if (ImGui::Button("Abrir") || submitted) {
                if (open_path(path_input, true)) {
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

        if (open_project_requested) {
            open_project_requested = false;
            std::string escolhido;
            const PickResult r =
                pick_open_file("Abrir projeto", app.project_path, filtro_projeto, &escolhido);
            if (r == PickResult::Chose) {
                open_project(escolhido, true);
            } else if (r == PickResult::Unavailable) {
                std::snprintf(project_input, sizeof(project_input), "%s", app.project_path.c_str());
                ImGui::OpenPopup("Abrir projeto");
            }
        }
        if (ImGui::BeginPopupModal("Abrir projeto", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Caminho do projeto:");
            ImGui::SetNextItemWidth(460.0f);
            const bool enviou = ImGui::InputText("##projeto", project_input, sizeof(project_input),
                                                 ImGuiInputTextFlags_EnterReturnsTrue);
            if (ImGui::Button("Abrir") || enviou) {
                if (open_project(project_input, true)) {
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

        if (save_as_requested) {
            if (app.project_path.empty()) {
                // Sem projeto ainda: propõe o nome da imagem, na pasta dela.
                std::filesystem::path sugestao =
                    app.path.empty() ? std::filesystem::path("projeto")
                                     : std::filesystem::path(app.path).replace_extension();
                sugestao += project_suffix();
                std::snprintf(project_input, sizeof(project_input), "%s", sugestao.c_str());
            } else {
                std::snprintf(project_input, sizeof(project_input), "%s", app.project_path.c_str());
            }
            save_as_requested = false;
            std::string escolhido;
            const PickResult r =
                pick_save_file("Salvar projeto", project_input, filtro_projeto, &escolhido);
            if (r == PickResult::Chose) {
                // Sem sufixo o arquivo não volta pelo filtro do seletor.
                const std::size_t n = std::strlen(project_suffix());
                if (escolhido.size() <= n
                    || escolhido.compare(escolhido.size() - n, std::string::npos,
                                         project_suffix()) != 0) {
                    escolhido += project_suffix();
                }
                save_project_to(escolhido);
            } else if (r == PickResult::Unavailable) {
                ImGui::OpenPopup("Salvar projeto");
            }
        }
        if (ImGui::BeginPopupModal("Salvar projeto", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Salvar em:");
            ImGui::SetNextItemWidth(460.0f);
            const bool enviou = ImGui::InputText("##salvar", project_input, sizeof(project_input),
                                                 ImGuiInputTextFlags_EnterReturnsTrue);
            if (ImGui::Button("Salvar") || enviou) {
                if (save_project_to(project_input)) {
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

        if (!missing_image.empty() && !ImGui::IsPopupOpen("Imagem não encontrada")) {
            ImGui::OpenPopup("Imagem não encontrada");
        }
        if (ImGui::BeginPopupModal("Imagem não encontrada", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("O projeto abriu, mas a imagem que ele aponta não está lá:");
            ImGui::TextColored(ImVec4(0.9f, 0.45f, 0.45f, 1.0f), "%s", missing_image.c_str());
            ImGui::Spacing();
            ImGui::TextUnformatted("Anexar outra imagem:");
            if (ImGui::Button("Procurar...")) {
                std::string escolhido;
                if (pick_open_file("Anexar imagem", missing_image, filtro_imagem, &escolhido)
                    == PickResult::Chose) {
                    std::snprintf(attach_input, sizeof(attach_input), "%s", escolhido.c_str());
                }
            }
            ImGui::SetNextItemWidth(460.0f);
            const bool enviou = ImGui::InputText("##anexar", attach_input, sizeof(attach_input),
                                                 ImGuiInputTextFlags_EnterReturnsTrue);
            if (ImGui::Button("Anexar") || enviou) {
                if (app.open(attach_input, true)) {
                    app.evaluate();
                    app.upload_view();
                    missing_image.clear();
                    chain_dirty = false;
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Seguir sem imagem")) {
                app.status.clear();
                missing_image.clear();
                ImGui::CloseCurrentPopup();
            }
            if (!app.status.empty()) {
                ImGui::TextColored(ImVec4(0.9f, 0.45f, 0.45f, 1.0f), "%s", app.status.c_str());
            }
            ImGui::EndPopup();
        }

        ImGui::Begin("Vista");
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
            if (ImGui::Combo("##colormap", &colormap, "cinza\0viridis\0magma\0turbo\0quente\0")) {
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
            // O campo segue app.path, menos enquanto está sendo digitado.
            if (image_input_from != app.path && !ImGui::IsAnyItemActive()) {
                image_input_from = app.path;
                std::snprintf(image_input, sizeof(image_input), "%s", app.path.c_str());
            }
            ImGui::SetNextItemWidth(-70.0f);
            const bool trocou = ImGui::InputText("##arquivo", image_input, sizeof(image_input),
                                                 ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::SameLine();
            if (ImGui::Button("trocar")) {
                std::string escolhido;
                if (pick_open_file("Trocar imagem", app.path, filtro_imagem, &escolhido)
                    == PickResult::Chose) {
                    swap_image(escolhido);
                } else {
                    // Sem seletor, o campo do lado já resolve.
                    ImGui::SetKeyboardFocusHere(-2);
                }
            }
            if (trocou) {
                swap_image(image_input);
            }
            ImGui::TextDisabled("a cadeia continua a mesma");
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::TextDisabled("Dimensões");
            ImGui::Text("%d x %d", app.source.width, app.source.height);
            ImGui::Spacing();
            ImGui::TextDisabled("Origem");
            if (app.source_bits > 0) {
                static const char* arranjo[5] = {"?", "cinza", "cinza e alfa", "RGB", "RGBA"};
                ImGui::Text("%s, %d bits por canal",
                            arranjo[std::clamp(app.source_channels, 0, 4)], app.source_bits);
            } else {
                ImGui::TextDisabled("cabeçalho não lido");
            }
            ImGui::Spacing();
            ImGui::TextDisabled("Buffer");
            ImGui::TextUnformatted("RGBA float32, linear");
            ImGui::Text("%.1f MB", static_cast<double>(app.source.width) * app.source.height * 4 *
                                       sizeof(float) / (1024.0 * 1024.0));
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            const int shown = app.shown();
            if (app.viewed >= 0 && app.viewed < static_cast<int>(app.chain.stages.size())) {
                ImGui::TextDisabled("Estágio selecionado");
                ImGui::Text("%d  %s", app.viewed,
                            op_info(app.chain.stages[app.viewed].params).name);
                ImGui::Text("tipo   %s", kind_name(app.chain.kind_of(app.viewed)));
            }
            if (shown >= 0 && shown < static_cast<int>(app.chain.stages.size())) {
                if (shown != app.viewed) {
                    ImGui::Spacing();
                    ImGui::TextDisabled("Preso na tela");
                    ImGui::Text("%d  %s", shown, op_info(app.chain.stages[shown].params).name);
                }
                if (app.chain.kind_of(shown) != ValueKind::Color) {
                    ImGui::Text("faixa  %.4f .. %.4f", app.view_lo, app.view_hi);
                }
            }
        }
        ImGui::End();

        draw_chain_panel(app, chain_dirty);
        chain_dirty = false;

        if (app.edit_request >= 0) {
            const int index = app.chain.index_of(app.edit_request);
            if (index >= 0) {
                const OpParams& params = app.chain.stages[index].params;
                if (std::get_if<ConvolveOp>(&params)) {
                    attach_kernel_window(kernel_window, app, app.edit_request);
                } else if (std::get_if<CurveOp>(&params)) {
                    attach_curve_window(curve_window, app, app.edit_request);
                } else if (std::get_if<ComponentsOp>(&params)) {
                    attach_components_window(components_window, app, app.edit_request);
                }
            }
            app.edit_request = -1;
        }

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("Imagem");
        if (ws.docs.size() > 1 || !ws.doc().path.empty()) {
            if (ImGui::BeginTabBar("abas", ImGuiTabBarFlags_AutoSelectNewTabs
                                               | ImGuiTabBarFlags_Reorderable
                                               | ImGuiTabBarFlags_FittingPolicyScroll)) {
                for (int i = 0; i < static_cast<int>(ws.docs.size()); ++i) {
                    App& d = *ws.docs[i];
                    std::string nome = d.label();
                    if (d.unsaved) {
                        nome += "*";
                    }
                    // Id separado do rótulo, senão dois arquivos de mesmo nome
                    // viram a mesma aba.
                    nome += "###aba" + std::to_string(i);

                    bool viva = true;
                    if (ImGui::BeginTabItem(nome.c_str(), &viva)) {
                        if (ws.active != i) {
                            ws.active = i;
                            detach_tools();
                            set_title();
                        }
                        ImGui::EndTabItem();
                    }
                    if (!viva) {
                        fechar_aba = i;
                    }
                }
                ImGui::EndTabBar();
            }
        }
        draw_canvas(app.canvas, app.texture);
        ImGui::End();
        ImGui::PopStyleVar();

        if (fechar_aba >= 0 && !ImGui::IsPopupOpen("Fechar sem salvar")) {
            if (ws.docs[fechar_aba]->unsaved) {
                ImGui::OpenPopup("Fechar sem salvar");
            } else {
                fechar_confirmado = fechar_aba;
                fechar_aba = -1;
            }
        }
        if (fechar_aba >= 0
            && ImGui::BeginPopupModal("Fechar sem salvar", nullptr,
                                      ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("%s tem alteração não salva.", ws.docs[fechar_aba]->label().c_str());
            if (ImGui::Button("Fechar mesmo assim")) {
                fechar_confirmado = fechar_aba;
                fechar_aba = -1;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancelar")) {
                fechar_aba = -1;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        draw_kernel_window(kernel_window, kernel_library, app);
        draw_histogram_window(histogram_window, app);
        draw_curve_window(curve_window, app);
        draw_components_window(components_window, app);
        draw_export_window(export_window, app);
        draw_sheet_window(sheet_window, app);

        // Serializar o projeto todo frame seria desperdício, e marcar sujo na
        // mão esquece campo. Essa assinatura barata muda em toda alteração que
        // entra no arquivo, e só então vale reserializar pra comparar.
        {
            const long long assinatura =
                app.revision * 1000003LL + app.viewed * 1009LL + app.pinned * 101LL
                + static_cast<int>(app.colormap) * 7LL + (app.chain_compact ? 1LL : 0LL);
            if (assinatura != app.last_signature) {
                app.last_signature = assinatura;
                const bool agora = project_text(app) != app.saved_state;
                if (agora != app.unsaved) {
                    app.unsaved = agora;
                    set_title();
                }
            }
        }

        if (sair_pedido) {
            int sujos = 0;
            for (const auto& d : ws.docs) {
                sujos += d->unsaved ? 1 : 0;
            }
            if (sujos == 0) {
                rodando = false;
            } else if (!ImGui::IsPopupOpen("Sair sem salvar")) {
                ImGui::OpenPopup("Sair sem salvar");
            }
        }
        if (ImGui::BeginPopupModal("Sair sem salvar", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Tem alteração não salva:");
            for (const auto& d : ws.docs) {
                if (d->unsaved) {
                    ImGui::BulletText("%s", d->label().c_str());
                }
            }
            ImGui::Spacing();
            if (ImGui::Button("Sair mesmo assim")) {
                rodando = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancelar")) {
                sair_pedido = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

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
