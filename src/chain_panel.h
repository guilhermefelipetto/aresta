#pragma once

struct App;

// As entradas de operação do menu, usadas pelo menu do topo e pelo botão do
// painel. Devolve true se acrescentou alguma.
bool draw_operation_items(App& app);

// `dirty_from_outside` entra true quando o menu do topo já mexeu na cadeia
// neste quadro, pra reavaliação acontecer uma vez só.
void draw_chain_panel(App& app, bool dirty_from_outside);
