#pragma once

#include "image.h"

// Todas operam em espaço linear, no lugar, e não cortam o resultado — quem
// corta é a conversão pra 8 bits, na saída.
void adjust_exposure(ImageView image, float stops);
void adjust_contrast(ImageView image, float amount);
void adjust_gamma(ImageView image, float gamma);
