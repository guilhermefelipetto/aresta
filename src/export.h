#pragma once

#include <string>

#include "value.h"

enum class ExportFormat { Png, Jpeg, Bmp, Tga, Pnm, Pfm, Csv, Npy };

const char* format_name(ExportFormat format);
const char* format_extension(ExportFormat format);

// `raw` distingue exportar o que está na tela, já com colormap e cortado em 8
// bits, de exportar o valor que o estágio realmente carrega. Nem todo formato
// aguenta as duas coisas.
bool format_supports(ExportFormat format, ValueKind kind, bool raw);

const char* format_note(ExportFormat format);

bool export_value(const Value& value, const std::string& path, ExportFormat format, bool raw,
                  int quality, Colormap colormap, std::string* error);
