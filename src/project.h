#pragma once

#include <string>

struct App;

// O projeto abre mesmo sem a imagem: a cadeia é o trabalho, o arquivo de
// entrada é só onde ela estava apontando. Quem chamou decide o que fazer com
// o caminho que faltou.
struct ProjectLoad {
    bool ok = false;
    std::string error;
    bool missing_image = false;
    std::string wanted_image;
};

// O projeto como texto, que é o que save_project grava. Serve também pra saber
// se tem coisa não salva: comparar com o texto do disco não deixa campo de
// fora, e ninguém precisa lembrar de marcar sujo quando acrescenta parâmetro.
std::string project_text(const App& app);

bool save_project(const App& app, const std::string& file, std::string* error);
ProjectLoad load_project(App& app, const std::string& file);

// Sufixo do arquivo de projeto, num lugar só.
inline const char* project_suffix() { return ".aresta"; }
