#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "canvas.h"
#include "chain.h"
#include "image.h"
#include "texture.h"
#include "value.h"

struct App {
    Image source;
    Chain chain;
    Texture texture;
    Canvas canvas;

    int viewed = 0;  // índice do estágio selecionado pra edição

    // Id do estágio preso na tela, ou -1 pra tela seguir a seleção. Id e não
    // índice, pra fixação sobreviver a apagar estágio do meio.
    int pinned = -1;
    Colormap colormap = Colormap::Gray;
    float view_lo = 0.0f;
    float view_hi = 0.0f;

    // Sobe a cada reavaliação. Quem cacheia coisa derivada da cadeia compara
    // com isso pra saber se precisa refazer.
    int revision = 0;

    // Cadeia comprida vira parede de slider. Com isso ligado, só o estágio
    // selecionado abre os parâmetros.
    bool chain_compact = true;

    // Id do estágio que pediu pra ser editado na janela dele. O painel só
    // levanta a mão; quem sabe qual janela abrir é o laço principal.
    int edit_request = -1;

    std::string path;

    // Erro: fica na barra até alguém trocar, porque erro que some sozinho
    // passa despercebido.
    std::string status;

    // Recado de coisa que deu certo. Some sozinho, e não pode tapar a barra:
    // ela existe pra mostrar o pixel sob o cursor e o estágio preso.
    std::string flash;
    std::chrono::steady_clock::time_point flash_until{};

    // Onde esse documento foi salvo, e o texto do projeto na última vez que
    // ele bateu com o disco. Comparar com o atual é o que diz se tem coisa não
    // salva, sem ninguém precisar marcar sujo na mão.
    std::string project_path;
    std::string saved_state;
    bool unsaved = false;
    long long last_signature = -1;

    int source_channels = 0;
    int source_bits = 0;

    // Nome curto pra aba e pro título: o do projeto quando tem, senão o da
    // imagem.
    std::string label() const;

    // Índice do que está na tela: o fixado quando existe, senão o selecionado.
    int shown() const;

    // Abrir imagem nova zera a cadeia, que é o certo quando você troca de
    // trabalho. Quem está carregando projeto ou religando a imagem que faltou
    // pede pra manter, senão a imagem entra e leva o projeto junto.
    bool open(const std::string& file, bool keep_chain = false);

    void say(const std::string& message);
    bool flashing() const;

    void evaluate();
    void upload_view();
};

// Vários documentos abertos ao mesmo tempo, cada um com sua imagem, sua cadeia
// e seu projeto. As janelas de ferramenta continuam recebendo um App só: elas
// trabalham sempre no documento ativo.
struct Workspace {
    std::vector<std::unique_ptr<App>> docs;
    int active = 0;

    Workspace();

    App& doc();
    const App& doc() const;

    // Documento recém-aberto e intocado é lugar bom pra colocar o próximo
    // arquivo, em vez de deixar aba vazia pra trás.
    bool doc_is_pristine() const;

    App& open_tab();
    void close_tab(int index);
};
