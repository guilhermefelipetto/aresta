#include "export.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <vector>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#pragma GCC diagnostic pop

namespace {

std::ofstream open_binary(const std::string& path, std::string* error) {
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        *error = "não consegui escrever em " + path;
    }
    return file;
}

bool write_pnm(const Value& value, const std::string& path, bool raw, Colormap colormap,
               std::string* error) {
    std::ofstream file = open_binary(path, error);
    if (!file) {
        return false;
    }
    const int w = value.width();
    const int h = value.height();

    if (!raw) {
        float lo = 0.0f;
        float hi = 0.0f;
        const auto rgba = to_display_rgba8(value, colormap, &lo, &hi);
        file << "P6\n" << w << ' ' << h << "\n255\n";
        for (std::size_t i = 0; i < static_cast<std::size_t>(w) * h; ++i) {
            file.write(reinterpret_cast<const char*>(rgba.get() + i * 4), 3);
        }
        return true;
    }

    // PGM de 16 bits, big-endian como manda o formato. Escalar é reescalado
    // pelo próprio intervalo; rótulo vai como índice, sem reescalar.
    file << "P5\n" << w << ' ' << h << "\n65535\n";
    const auto put = [&](int v) {
        const unsigned value16 = static_cast<unsigned>(std::clamp(v, 0, 65535));
        const char bytes[2] = {static_cast<char>((value16 >> 8) & 0xFF),
                               static_cast<char>(value16 & 0xFF)};
        file.write(bytes, 2);
    };

    if (value.kind == ValueKind::Scalar) {
        float lo = std::numeric_limits<float>::max();
        float hi = std::numeric_limits<float>::lowest();
        for (std::size_t i = 0; i < value.scalar.count(); ++i) {
            lo = std::min(lo, value.scalar.data[i]);
            hi = std::max(hi, value.scalar.data[i]);
        }
        const float span = (hi > lo) ? (hi - lo) : 1.0f;
        for (std::size_t i = 0; i < value.scalar.count(); ++i) {
            put(static_cast<int>((value.scalar.data[i] - lo) / span * 65535.0f + 0.5f));
        }
    } else {
        for (std::size_t i = 0; i < value.label.count(); ++i) {
            put(value.label.data[i]);
        }
    }
    return true;
}

bool write_pfm(const Value& value, const std::string& path, std::string* error) {
    std::ofstream file = open_binary(path, error);
    if (!file) {
        return false;
    }
    const int w = value.width();
    const int h = value.height();
    const bool color = value.kind == ValueKind::Color;

    // -1.0 é a escala que declara little-endian, e as linhas do PFM vão de
    // baixo pra cima.
    file << (color ? "PF\n" : "Pf\n") << w << ' ' << h << "\n-1.0\n";
    for (int y = h - 1; y >= 0; --y) {
        if (color) {
            const float* row = value.color.view().row(y);
            for (int x = 0; x < w; ++x) {
                file.write(reinterpret_cast<const char*>(row + x * 4), 3 * sizeof(float));
            }
        } else {
            file.write(reinterpret_cast<const char*>(value.scalar.view().row(y)),
                       static_cast<std::streamsize>(w) * sizeof(float));
        }
    }
    return true;
}

bool write_csv(const Value& value, const std::string& path, std::string* error) {
    std::ofstream file(path);
    if (!file) {
        *error = "não consegui escrever em " + path;
        return false;
    }
    // Nove dígitos fecham o ida e volta de um float sem perder bit, e o CSV só
    // vale a pena se for a cópia exata em texto.
    file << std::setprecision(9);
    for (int y = 0; y < value.height(); ++y) {
        for (int x = 0; x < value.width(); ++x) {
            if (x > 0) {
                file << ',';
            }
            if (value.kind == ValueKind::Scalar) {
                file << value.scalar.view().at(x, y);
            } else {
                file << value.label.view().at(x, y);
            }
        }
        file << '\n';
    }
    return true;
}

bool write_npy(const Value& value, const std::string& path, std::string* error) {
    std::ofstream file = open_binary(path, error);
    if (!file) {
        return false;
    }
    const int w = value.width();
    const int h = value.height();

    char shape[96];
    const char* dtype = value.kind == ValueKind::Label ? "<i4" : "<f4";
    if (value.kind == ValueKind::Color) {
        std::snprintf(shape, sizeof(shape), "(%d, %d, 4)", h, w);
    } else {
        std::snprintf(shape, sizeof(shape), "(%d, %d)", h, w);
    }

    std::string header = std::string("{'descr': '") + dtype +
                         "', 'fortran_order': False, 'shape': " + shape + ", }";
    // O bloco de cabeçalho precisa fechar em múltiplo de 64 contando a magia.
    while ((10 + header.size() + 1) % 64 != 0) {
        header += ' ';
    }
    header += '\n';

    const char magic[8] = {'\x93', 'N', 'U', 'M', 'P', 'Y', 1, 0};
    file.write(magic, 8);
    const std::uint16_t length = static_cast<std::uint16_t>(header.size());
    file.write(reinterpret_cast<const char*>(&length), 2);
    file.write(header.data(), static_cast<std::streamsize>(header.size()));

    switch (value.kind) {
        case ValueKind::Color:
            file.write(reinterpret_cast<const char*>(value.color.data.get()),
                       static_cast<std::streamsize>(w) * h * 4 * sizeof(float));
            break;
        case ValueKind::Scalar:
            file.write(reinterpret_cast<const char*>(value.scalar.data.get()),
                       static_cast<std::streamsize>(w) * h * sizeof(float));
            break;
        case ValueKind::Label:
            file.write(reinterpret_cast<const char*>(value.label.data.get()),
                       static_cast<std::streamsize>(w) * h * sizeof(std::int32_t));
            break;
    }
    return true;
}

}  // namespace

const char* format_name(ExportFormat format) {
    switch (format) {
        case ExportFormat::Png: return "PNG";
        case ExportFormat::Jpeg: return "JPEG";
        case ExportFormat::Bmp: return "BMP";
        case ExportFormat::Tga: return "TGA";
        case ExportFormat::Pnm: return "Netpbm (PPM/PGM)";
        case ExportFormat::Pfm: return "PFM (float32)";
        case ExportFormat::Csv: return "CSV";
        case ExportFormat::Npy: return "NPY (numpy)";
    }
    return "?";
}

const char* format_extension(ExportFormat format) {
    switch (format) {
        case ExportFormat::Png: return "png";
        case ExportFormat::Jpeg: return "jpg";
        case ExportFormat::Bmp: return "bmp";
        case ExportFormat::Tga: return "tga";
        case ExportFormat::Pnm: return "pnm";
        case ExportFormat::Pfm: return "pfm";
        case ExportFormat::Csv: return "csv";
        case ExportFormat::Npy: return "npy";
    }
    return "bin";
}

const char* format_note(ExportFormat format) {
    switch (format) {
        case ExportFormat::Png: return "sem perda, 8 bits por canal";
        case ExportFormat::Jpeg: return "com perda, não serve pra máscara";
        case ExportFormat::Bmp: return "sem perda, sem compressão";
        case ExportFormat::Tga: return "sem perda, aceita alfa";
        case ExportFormat::Pnm: return "cabeçalho em texto; cru vira PGM de 16 bits";
        case ExportFormat::Pfm: return "float32 exato, sem cortar nem reescalar";
        case ExportFormat::Csv: return "número por número, abre em qualquer editor";
        case ExportFormat::Npy: return "numpy.load abre direto";
    }
    return "";
}

bool format_supports(ExportFormat format, ValueKind kind, bool raw) {
    switch (format) {
        case ExportFormat::Png:
        case ExportFormat::Jpeg:
        case ExportFormat::Bmp:
        case ExportFormat::Tga:
            return !raw;
        case ExportFormat::Pnm:
            return !raw || kind != ValueKind::Color;
        case ExportFormat::Pfm:
            return raw && kind != ValueKind::Label;
        case ExportFormat::Csv:
            return raw && kind != ValueKind::Color;
        case ExportFormat::Npy:
            return raw;
    }
    return false;
}

bool export_value(const Value& value, const std::string& path, ExportFormat format, bool raw,
                  int quality, Colormap colormap, std::string* error) {
    error->clear();
    if (value.empty()) {
        *error = "estágio vazio";
        return false;
    }
    if (!format_supports(format, value.kind, raw)) {
        *error = std::string(format_name(format)) + " não guarda isso";
        return false;
    }

    const int w = value.width();
    const int h = value.height();

    if (!raw && format != ExportFormat::Pnm) {
        float lo = 0.0f;
        float hi = 0.0f;
        const auto rgba = to_display_rgba8(value, colormap, &lo, &hi);
        int ok = 0;
        switch (format) {
            case ExportFormat::Png:
                ok = stbi_write_png(path.c_str(), w, h, 4, rgba.get(), w * 4);
                break;
            case ExportFormat::Jpeg:
                ok = stbi_write_jpg(path.c_str(), w, h, 4, rgba.get(), quality);
                break;
            case ExportFormat::Bmp:
                ok = stbi_write_bmp(path.c_str(), w, h, 4, rgba.get());
                break;
            default:
                ok = stbi_write_tga(path.c_str(), w, h, 4, rgba.get());
                break;
        }
        if (!ok) {
            *error = "não consegui escrever em " + path;
        }
        return ok != 0;
    }

    switch (format) {
        case ExportFormat::Pnm:
            return write_pnm(value, path, raw, colormap, error);
        case ExportFormat::Pfm:
            return write_pfm(value, path, error);
        case ExportFormat::Csv:
            return write_csv(value, path, error);
        default:
            return write_npy(value, path, error);
    }
}
