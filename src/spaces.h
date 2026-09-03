#pragma once

// O buffer é sempre RGB linear. Estes são os espaços que dá pra olhar por
// cima dele, um componente por vez.
//
// Qual deles vê o valor linear e qual vê o codificado não é escolha de gosto:
// Lab é definido a partir do XYZ, que vem do linear; HSV, HSI, YCbCr e CMY são
// definidos sobre o valor com gama. A conversão faz isso sozinha.
enum class Space { RGB, HSV, HSI, Lab, YCbCr, CMY };

const char* space_name(Space space);
const char* component_name(Space space, int index);

// Faixa natural de cada componente, pra UI dizer o que esperar.
void component_range(Space space, int index, float* lo, float* hi);

void to_space(Space space, const float rgb[3], float out[3]);
void from_space(Space space, const float in[3], float rgb[3]);
