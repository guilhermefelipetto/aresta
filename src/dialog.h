#pragma once

#include <string>
#include <vector>

// Seletor de arquivo do sistema. Sai por zenity, que já vem no Ubuntu: dá a
// janela do GNOME de graça e não custa dependência de build. Onde ele não
// existir, devolve Indisponivel e quem chamou volta pro campo de digitar.
enum class PickResult { Chose, Canceled, Unavailable };

struct FileFilter {
    std::string label;              // "Imagens"
    std::vector<std::string> globs; // {"*.png", "*.jpg"}
};

PickResult pick_open_file(const std::string& title, const std::string& start,
                          const std::vector<FileFilter>& filters, std::string* chosen);

PickResult pick_save_file(const std::string& title, const std::string& suggested,
                          const std::vector<FileFilter>& filters, std::string* chosen);
