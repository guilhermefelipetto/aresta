#pragma once

#include <string>

#include <imgui.h>

// Sobe quando o conjunto de painéis muda (nome novo, painel novo, painel que
// sumiu). O arquivo de layout carrega o número no nome, então uma versão antiga
// simplesmente não é lida e o layout de fábrica volta a valer.
constexpr int layout_version = 3;

// ~/.config/aresta/layout-N.ini, com o diretório criado. Guardar no home em vez
// de no diretório de trabalho evita largar imgui.ini onde a pessoa rodou.
std::string layout_path();

void forget_old_layouts();

// Tema escuro e fonte do sistema. A fonte embutida do ImGui é bitmap de 13px e
// denuncia protótipo a três metros de distância.
void apply_theme();

// Monta o layout de fábrica: vista à esquerda, propriedades e cadeia à direita,
// canvas no meio.
void build_default_layout(ImGuiID dockspace);
