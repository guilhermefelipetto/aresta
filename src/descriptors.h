#pragma once

#include <cstdint>
#include <utility>
#include <string>
#include <vector>

#include "map.h"

// Um ponto da fronteira, em coordenada de pixel.
struct Point {
    int x = 0;
    int y = 0;
};

// Fronteira externa de uma região, em ordem, pelo seguimento de Moore. Começa
// no pixel dado, que precisa ser o primeiro da região em varredura de cima pra
// baixo, senão a volta pode sair por dentro de um buraco.
std::vector<Point> trace_boundary(MapView<int32_t> labels, int32_t label, Point start);

// Código de cadeia de oito direções, no sentido em que a fronteira veio. A
// direção 0 é pra direita e elas crescem no anti-horário.
std::vector<int> chain_code(const std::vector<Point>& boundary);

// Primeira diferença do código, que é o que não muda quando a forma gira num
// múltiplo de 45 graus.
std::vector<int> chain_difference(const std::vector<int>& code);

// Comprimento da fronteira pelo código: passo reto vale 1, diagonal vale raiz
// de 2. Contar pixel de borda dá um número maior que o perímetro de verdade.
double chain_length(const std::vector<int>& code);

// Casco convexo dos cantos dos pixels, pela cadeia monótona, em coordenada
// dobrada: o canto cai em meio pixel, e dobrar mantém tudo inteiro. O centro
// do pixel (x, y) vira (2x, 2y). Sobre os centros, o casco de um quadrado 3x3
// mediria 2x2 e a solidez daria errado.
std::vector<Point> convex_hull(const std::vector<Point>& pixels);

double polygon_area(const std::vector<Point>& polygon);

struct Shape {
    int32_t label = 0;
    int area = 0;
    bool touches_border = false;

    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // caixa, com x1 e y1 inclusivos
    double cx = 0.0, cy = 0.0;

    double perimeter = 0.0;
    // 4 pi A / P^2, vale 1 no círculo. Passa de 1 em região de poucos pixels,
    // onde o perímetro pelo código de cadeia fica curto demais pra área. Não
    // corto em 1: valor acima disso é o aviso de que a região é pequena demais
    // pra medida querer dizer alguma coisa.
    double circularity = 0.0;
    double extent = 0.0;        // quanto da caixa a região ocupa
    double hull_area = 0.0;
    // A região é união de quadrados unitários, então o casco dela é o casco
    // dos cantos, e a solidez é a área da região sobre a área desse casco.
    // Retângulo alinhado dá 1 exato; disco digital dá 0.968, porque a escada
    // da borda deixa mesmo um pedaço de fora. O scikit-image usa outra
    // convenção e chega em 0.98.
    double solidity = 0.0;

    // Da matriz de covariância dos pixels: os dois eixos, o ângulo do maior em
    // graus, e a excentricidade da elipse equivalente.
    double major = 0.0, minor = 0.0;
    double orientation = 0.0;
    double eccentricity = 0.0;

    // Invariantes de Hu, que não mudam com translação, escala nem rotação.
    // Guardados crus; quem mostra costuma usar o log do módulo, porque eles
    // variam por ordens de grandeza.
    double hu[7] = {};
};

std::vector<Shape> describe_regions(MapView<int32_t> labels);

// Linha de cabeçalho e uma linha por região, pra jogar no Python depois.
std::string shapes_to_csv(const std::vector<Shape>& shapes);
