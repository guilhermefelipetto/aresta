#include "descriptors.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>

namespace {

// Vizinhos em oito, no sentido anti-horário a partir da direita. A ordem é o
// que define o código de cadeia, então mexer aqui muda o número.
constexpr int kDx[8] = {1, 1, 0, -1, -1, -1, 0, 1};
constexpr int kDy[8] = {0, -1, -1, -1, 0, 1, 1, 1};

bool dentro(MapView<int32_t> labels, int x, int y) {
    return x >= 0 && y >= 0 && x < labels.width && y < labels.height;
}

bool pertence(MapView<int32_t> labels, int32_t label, int x, int y) {
    return dentro(labels, x, y) && labels.at(x, y) == label;
}

int direcao_entre(Point de, Point para) {
    const int dx = para.x - de.x;
    const int dy = para.y - de.y;
    for (int d = 0; d < 8; ++d) {
        if (kDx[d] == dx && kDy[d] == dy) {
            return d;
        }
    }
    return -1;
}

long long cruzado(Point o, Point a, Point b) {
    return static_cast<long long>(a.x - o.x) * (b.y - o.y)
           - static_cast<long long>(a.y - o.y) * (b.x - o.x);
}

}  // namespace

std::vector<Point> trace_boundary(MapView<int32_t> labels, int32_t label, Point start) {
    std::vector<Point> fronteira;
    if (!pertence(labels, label, start.x, start.y)) {
        return fronteira;
    }

    // Pixel sozinho não tem volta pra dar.
    bool tem_vizinho = false;
    for (int d = 0; d < 8 && !tem_vizinho; ++d) {
        tem_vizinho = pertence(labels, label, start.x + kDx[d], start.y + kDy[d]);
    }
    if (!tem_vizinho) {
        fronteira.push_back(start);
        return fronteira;
    }

    Point atual = start;
    // Entrando por cima, que é de onde a varredura veio achar o primeiro pixel.
    int entrada = 4;
    Point segundo{-1, -1};

    const std::size_t teto = static_cast<std::size_t>(labels.width) * labels.height * 4 + 16;
    while (fronteira.size() < teto) {
        fronteira.push_back(atual);

        // Roda a partir de quem me trouxe, mais um, e pega o primeiro que for
        // da região. Girar sempre do mesmo lado é o que mantém a volta colada
        // na borda em vez de cortar caminho pelo meio.
        int achou = -1;
        for (int i = 1; i <= 8; ++i) {
            const int d = (entrada + i) % 8;
            if (pertence(labels, label, atual.x + kDx[d], atual.y + kDy[d])) {
                achou = d;
                break;
            }
        }
        if (achou < 0) {
            break;
        }

        const Point proximo{atual.x + kDx[achou], atual.y + kDy[achou]};
        if (fronteira.size() == 1) {
            segundo = proximo;
        } else if (atual.x == start.x && atual.y == start.y && proximo.x == segundo.x
                   && proximo.y == segundo.y) {
            // Critério de parada de Jacob: voltar ao começo não basta, porque
            // a fronteira pode passar duas vezes pelo mesmo pixel num istmo.
            // O que fecha a volta é repetir o primeiro passo.
            fronteira.pop_back();
            break;
        }

        // A nova entrada é a direção oposta à que eu saí.
        entrada = (achou + 4) % 8;
        atual = proximo;
    }
    return fronteira;
}

std::vector<int> chain_code(const std::vector<Point>& boundary) {
    std::vector<int> codigo;
    if (boundary.size() < 2) {
        return codigo;
    }
    codigo.reserve(boundary.size());
    for (std::size_t i = 0; i + 1 < boundary.size(); ++i) {
        const int d = direcao_entre(boundary[i], boundary[i + 1]);
        if (d >= 0) {
            codigo.push_back(d);
        }
    }
    const int fecha = direcao_entre(boundary.back(), boundary.front());
    if (fecha >= 0) {
        codigo.push_back(fecha);
    }
    return codigo;
}

std::vector<int> chain_difference(const std::vector<int>& code) {
    std::vector<int> diferenca;
    if (code.empty()) {
        return diferenca;
    }
    diferenca.reserve(code.size());
    for (std::size_t i = 0; i < code.size(); ++i) {
        const int anterior = code[(i + code.size() - 1) % code.size()];
        diferenca.push_back((code[i] - anterior + 8) % 8);
    }
    return diferenca;
}

double chain_length(const std::vector<int>& code) {
    double soma = 0.0;
    for (int d : code) {
        soma += (d % 2 == 0) ? 1.0 : std::numbers::sqrt2;
    }
    return soma;
}

std::vector<Point> convex_hull(const std::vector<Point>& pixels) {
    std::vector<Point> cantos;
    cantos.reserve(pixels.size() * 4);
    for (const Point& p : pixels) {
        cantos.push_back({2 * p.x - 1, 2 * p.y - 1});
        cantos.push_back({2 * p.x + 1, 2 * p.y - 1});
        cantos.push_back({2 * p.x - 1, 2 * p.y + 1});
        cantos.push_back({2 * p.x + 1, 2 * p.y + 1});
    }
    std::sort(cantos.begin(), cantos.end(), [](Point a, Point b) {
        return a.x != b.x ? a.x < b.x : a.y < b.y;
    });
    cantos.erase(std::unique(cantos.begin(), cantos.end(),
                             [](Point a, Point b) { return a.x == b.x && a.y == b.y; }),
                 cantos.end());
    if (cantos.size() < 3) {
        return cantos;
    }

    // Cadeia monótona: metade de baixo e metade de cima, cada uma virando
    // sempre pro mesmo lado.
    std::vector<Point> casco(cantos.size() * 2);
    std::size_t k = 0;
    for (const Point& p : cantos) {
        while (k >= 2 && cruzado(casco[k - 2], casco[k - 1], p) <= 0) {
            --k;
        }
        casco[k++] = p;
    }
    const std::size_t baixo = k + 1;
    for (std::size_t i = cantos.size() - 1; i-- > 0;) {
        while (k >= baixo && cruzado(casco[k - 2], casco[k - 1], cantos[i]) <= 0) {
            --k;
        }
        casco[k++] = cantos[i];
    }
    casco.resize(k - 1);
    return casco;
}

double polygon_area(const std::vector<Point>& polygon) {
    if (polygon.size() < 3) {
        return 0.0;
    }
    long long duas_vezes = 0;
    for (std::size_t i = 0; i < polygon.size(); ++i) {
        const Point& a = polygon[i];
        const Point& b = polygon[(i + 1) % polygon.size()];
        duas_vezes += static_cast<long long>(a.x) * b.y - static_cast<long long>(b.x) * a.y;
    }
    return std::abs(static_cast<double>(duas_vezes)) * 0.5;
}

std::vector<Shape> describe_regions(MapView<int32_t> labels) {
    std::vector<Shape> formas;
    if (labels.width <= 0 || labels.height <= 0) {
        return formas;
    }

    int32_t maior = 0;
    for (int y = 0; y < labels.height; ++y) {
        for (int x = 0; x < labels.width; ++x) {
            maior = std::max(maior, labels.at(x, y));
        }
    }
    if (maior <= 0) {
        return formas;
    }

    // Uma passada só junta área, caixa e os momentos de todo mundo. Percorrer
    // a imagem por região daria O(regiões x pixels).
    struct Acumulado {
        long long area = 0;
        long long sx = 0, sy = 0;
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        bool borda = false;
        bool visto = false;
        Point inicio{0, 0};
    };
    std::vector<Acumulado> acc(static_cast<std::size_t>(maior) + 1);

    for (int y = 0; y < labels.height; ++y) {
        for (int x = 0; x < labels.width; ++x) {
            const int32_t rotulo = labels.at(x, y);
            if (rotulo <= 0) {
                continue;
            }
            Acumulado& a = acc[rotulo];
            if (!a.visto) {
                a.visto = true;
                a.inicio = {x, y};
                a.x0 = a.x1 = x;
                a.y0 = a.y1 = y;
            }
            ++a.area;
            a.sx += x;
            a.sy += y;
            a.x0 = std::min(a.x0, x);
            a.x1 = std::max(a.x1, x);
            a.y0 = std::min(a.y0, y);
            a.y1 = std::max(a.y1, y);
            if (x == 0 || y == 0 || x == labels.width - 1 || y == labels.height - 1) {
                a.borda = true;
            }
        }
    }

    // Segunda passada, agora que os centroides existem. Acumular momento cru e
    // converter depois daria o mesmo em teoria, mas a subtração de números da
    // ordem de x ao cubo come os dígitos que importam.
    struct Momento {
        long double m11 = 0, m20 = 0, m02 = 0;
        long double m30 = 0, m21 = 0, m12 = 0, m03 = 0;
    };
    std::vector<Momento> mom(static_cast<std::size_t>(maior) + 1);
    std::vector<double> cx(acc.size(), 0.0);
    std::vector<double> cy(acc.size(), 0.0);
    for (std::size_t i = 1; i < acc.size(); ++i) {
        if (acc[i].area > 0) {
            cx[i] = static_cast<double>(acc[i].sx) / static_cast<double>(acc[i].area);
            cy[i] = static_cast<double>(acc[i].sy) / static_cast<double>(acc[i].area);
        }
    }
    for (int y = 0; y < labels.height; ++y) {
        for (int x = 0; x < labels.width; ++x) {
            const int32_t rotulo = labels.at(x, y);
            if (rotulo <= 0) {
                continue;
            }
            Momento& m = mom[rotulo];
            const long double dx = x - cx[rotulo];
            const long double dy = y - cy[rotulo];
            m.m11 += dx * dy;
            m.m20 += dx * dx;
            m.m02 += dy * dy;
            m.m30 += dx * dx * dx;
            m.m21 += dx * dx * dy;
            m.m12 += dx * dy * dy;
            m.m03 += dy * dy * dy;
        }
    }

    for (int32_t rotulo = 1; rotulo <= maior; ++rotulo) {
        const Acumulado& a = acc[rotulo];
        if (!a.visto) {
            continue;
        }

        Shape s;
        s.label = rotulo;
        s.area = static_cast<int>(a.area);
        s.touches_border = a.borda;
        s.x0 = a.x0;
        s.y0 = a.y0;
        s.x1 = a.x1;
        s.y1 = a.y1;

        const double n = static_cast<double>(a.area);
        s.cx = cx[rotulo];
        s.cy = cy[rotulo];

        const long long caixa = static_cast<long long>(a.x1 - a.x0 + 1) * (a.y1 - a.y0 + 1);
        s.extent = caixa > 0 ? n / static_cast<double>(caixa) : 0.0;

        const std::vector<Point> fronteira = trace_boundary(labels, rotulo, a.inicio);
        const std::vector<int> codigo = chain_code(fronteira);
        s.perimeter = chain_length(codigo);
        if (s.perimeter > 0.0) {
            s.circularity = 4.0 * std::numbers::pi * n / (s.perimeter * s.perimeter);
        }

        // O casco vem em coordenada dobrada, então a área sai quatro vezes
        // maior. Casco degenerado (região de um pixel só) não fecha polígono,
        // e aí ele é a própria região.
        const std::vector<Point> casco = convex_hull(fronteira);
        const double area_casco = polygon_area(casco) * 0.25;
        s.hull_area = area_casco > 0.0 ? area_casco : n;
        s.solidity = s.hull_area > 0.0 ? n / s.hull_area : 0.0;

        const Momento& mo = mom[rotulo];
        const double mu20 = static_cast<double>(mo.m20) / n;
        const double mu02 = static_cast<double>(mo.m02) / n;
        const double mu11 = static_cast<double>(mo.m11) / n;

        const double meio = (mu20 + mu02) * 0.5;
        const double raiz = std::sqrt(std::max(0.0, (mu20 - mu02) * (mu20 - mu02) * 0.25
                                                        + mu11 * mu11));
        const double l1 = meio + raiz;
        const double l2 = std::max(0.0, meio - raiz);
        s.major = 4.0 * std::sqrt(std::max(0.0, l1));
        s.minor = 4.0 * std::sqrt(l2);
        s.eccentricity = l1 > 0.0 ? std::sqrt(std::max(0.0, 1.0 - l2 / l1)) : 0.0;

        // O eixo aponta pra baixo na imagem, então o sinal de y é invertido pro
        // ângulo sair no sentido de quem olha a figura.
        s.orientation = 0.5 * std::atan2(2.0 * mu11, mu20 - mu02) * 180.0 / std::numbers::pi;
        s.orientation = -s.orientation;

        // Hu pede os momentos centrais crus, não os normalizados pela área.
        const auto eta = [n](long double mu, int ordem) {
            return static_cast<double>(mu) / std::pow(n, 1.0 + ordem / 2.0);
        };
        const double n20 = eta(mo.m20, 2);
        const double n02 = eta(mo.m02, 2);
        const double n11 = eta(mo.m11, 2);
        const double n30 = eta(mo.m30, 3);
        const double n21 = eta(mo.m21, 3);
        const double n12 = eta(mo.m12, 3);
        const double n03 = eta(mo.m03, 3);

        const double a1 = n30 + n12;
        const double a2 = n21 + n03;
        s.hu[0] = n20 + n02;
        s.hu[1] = (n20 - n02) * (n20 - n02) + 4.0 * n11 * n11;
        s.hu[2] = (n30 - 3.0 * n12) * (n30 - 3.0 * n12) + (3.0 * n21 - n03) * (3.0 * n21 - n03);
        s.hu[3] = a1 * a1 + a2 * a2;
        s.hu[4] = (n30 - 3.0 * n12) * a1 * (a1 * a1 - 3.0 * a2 * a2)
                  + (3.0 * n21 - n03) * a2 * (3.0 * a1 * a1 - a2 * a2);
        s.hu[5] = (n20 - n02) * (a1 * a1 - a2 * a2) + 4.0 * n11 * a1 * a2;
        s.hu[6] = (3.0 * n21 - n03) * a1 * (a1 * a1 - 3.0 * a2 * a2)
                  - (n30 - 3.0 * n12) * a2 * (3.0 * a1 * a1 - a2 * a2);

        formas.push_back(s);
    }
    return formas;
}

std::string shapes_to_csv(const std::vector<Shape>& shapes) {
    std::string csv =
        "rotulo,area,toca_borda,x0,y0,x1,y1,cx,cy,perimetro,circularidade,extensao,"
        "area_casco,solidez,eixo_maior,eixo_menor,orientacao,excentricidade,"
        "hu1,hu2,hu3,hu4,hu5,hu6,hu7\n";
    for (const Shape& s : shapes) {
        char linha[768];
        std::snprintf(linha, sizeof(linha),
                      "%d,%d,%d,%d,%d,%d,%d,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,"
                      "%.6f,%.6f,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g\n",
                      s.label, s.area, s.touches_border ? 1 : 0, s.x0, s.y0, s.x1, s.y1, s.cx,
                      s.cy, s.perimeter, s.circularity, s.extent, s.hull_area, s.solidity,
                      s.major, s.minor, s.orientation, s.eccentricity, s.hu[0], s.hu[1], s.hu[2],
                      s.hu[3], s.hu[4], s.hu[5], s.hu[6]);
        csv += linha;
    }
    return csv;
}
