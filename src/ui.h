#pragma once

#include <imgui.h>

// Tema escuro e fonte do sistema. A fonte embutida do ImGui é bitmap de 13px e
// denuncia protótipo a três metros de distância.
void apply_theme();

// Monta o layout de fábrica: ferramentas à esquerda, propriedades e cadeia à
// direita, canvas no meio.
void build_default_layout(ImGuiID dockspace);
