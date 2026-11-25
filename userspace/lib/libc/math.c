#include "math.h"

float fabsf(float x) {
    return x < 0.0f ? -x : x;
}

double fabs(double x) {
    return x < 0.0 ? -x : x;
}

float sqrtf(float x) {
    if (x <= 0.0f) return 0.0f;
    float res;
    __asm__ ("sqrtss %1, %0" : "=x"(res) : "x"(x));
    return res;
}

double sqrt(double x) {
    if (x <= 0.0) return 0.0;
    double res;
    __asm__ ("sqrtsd %1, %0" : "=x"(res) : "x"(x));
    return res;
}

// Taylor series approximation for sin/cos
float sinf(float x) {
    // Wrap to [-PI, PI]
    while (x > (float)PI) x -= (float)(2.0 * PI);
    while (x < (float)-PI) x += (float)(2.0 * PI);
    float x2 = x * x;
    return x * (1.0f - x2 * (1.0f / 6.0f - x2 * (1.0f / 120.0f - x2 / 5040.0f)));
}

float cosf(float x) {
    return sinf(x + (float)(PI * 0.5));
}

double sin(double x) {
    return (double)sinf((float)x);
}

double cos(double x) {
    return (double)cosf((float)x);
}

double tan(double x) {
    double c = cos(x);
    if (fabs(c) < 1e-7) return 0.0;
    return sin(x) / c;
}
