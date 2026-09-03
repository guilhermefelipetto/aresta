#include "dialog.h"

#include <array>
#include <cstdio>
#include <cstdlib>

namespace {

// Aspas simples de shell: só o próprio apóstrofo precisa de cuidado. Sem isso
// um caminho com aspas viraria comando.
std::string entre_aspas(const std::string& texto) {
    std::string saida = "'";
    for (char c : texto) {
        if (c == '\'') {
            saida += "'\\''";
        } else {
            saida += c;
        }
    }
    saida += "'";
    return saida;
}

bool tem_zenity() {
    static const bool existe = std::system("command -v zenity >/dev/null 2>&1") == 0;
    return existe;
}

PickResult roda(const std::string& comando, std::string* chosen) {
    if (!tem_zenity()) {
        return PickResult::Unavailable;
    }
    std::FILE* tubo = popen(comando.c_str(), "r");
    if (!tubo) {
        return PickResult::Unavailable;
    }
    std::string saida;
    std::array<char, 512> buffer{};
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), tubo)) {
        saida += buffer.data();
    }
    const int status = pclose(tubo);

    while (!saida.empty() && (saida.back() == '\n' || saida.back() == '\r')) {
        saida.pop_back();
    }
    if (status != 0 || saida.empty()) {
        return PickResult::Canceled;
    }
    *chosen = saida;
    return PickResult::Chose;
}

std::string filtros_em(const std::vector<FileFilter>& filters) {
    std::string texto;
    for (const FileFilter& f : filters) {
        std::string regra = f.label + " |";
        for (const std::string& g : f.globs) {
            regra += " " + g;
        }
        texto += " --file-filter=" + entre_aspas(regra);
    }
    if (!filters.empty()) {
        texto += " --file-filter=" + entre_aspas("Todos | *");
    }
    return texto;
}

}  // namespace

PickResult pick_open_file(const std::string& title, const std::string& start,
                          const std::vector<FileFilter>& filters, std::string* chosen) {
    std::string comando = "zenity --file-selection --title=" + entre_aspas(title);
    if (!start.empty()) {
        comando += " --filename=" + entre_aspas(start);
    }
    comando += filtros_em(filters) + " 2>/dev/null";
    return roda(comando, chosen);
}

PickResult pick_save_file(const std::string& title, const std::string& suggested,
                          const std::vector<FileFilter>& filters, std::string* chosen) {
    std::string comando =
        "zenity --file-selection --save --confirm-overwrite --title=" + entre_aspas(title);
    if (!suggested.empty()) {
        comando += " --filename=" + entre_aspas(suggested);
    }
    comando += filtros_em(filters) + " 2>/dev/null";
    return roda(comando, chosen);
}
