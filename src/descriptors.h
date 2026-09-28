#pragma once

#include <complex>
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

    // Descritores de Fourier da fronteira, já sem posição, escala, rotação e
    // ponto de partida: o módulo de cada coeficiente dividido pelo de a(1). O
    // a(1) é o círculo que a volta percorre, e os outros dizem quanto a forma
    // se afasta dele. A ordem é -1, 2, -2, 3, -3, 4, -4, 5, contada no sentido
    // em que a volta anda, e o primeiro já diz o quanto a forma é elipse.
    //
    // Saem da volta reamostrada por comprimento de arco, não dos pixels
    // crus: passo diagonal mede raiz de 2, e sem igualar a amostragem a mesma
    // forma girada dá outro número.
    double fourier[8] = {};
};

// Coeficientes a(u) = soma de s(k) e^(-j2pi uk/K), com s(k) = x + jy, pra u de
// `lo` a `hi`. Direto pela soma, sem FFT: fronteira tem umas centenas de
// pontos, quase nunca potência de dois, e preencher até a próxima distorceria
// o contorno. Pedir só os coeficientes usados custa O(K por coeficiente).
std::vector<std::complex<double>> fourier_coefficients(const std::vector<Point>& boundary, int lo,
                                                       int hi);

// Cada região redesenhada com os `coefficients` coeficientes de frequência mais
// baixa, contando o a(0), que é o centroide da volta. Com poucos sobra uma
// elipse, e cada um a mais devolve um pouco do detalhe. Sai o contorno, ou a
// região preenchida, com o mesmo rótulo de entrada.
Map<int32_t> fourier_approximation(MapView<int32_t> labels, int coefficients, bool fill,
                                   int* regions, int* longest);

std::vector<Shape> describe_regions(MapView<int32_t> labels);

// Linha de cabeçalho e uma linha por região, pra jogar no Python depois.
std::string shapes_to_csv(const std::vector<Shape>& shapes);
