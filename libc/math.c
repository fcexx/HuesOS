#include <stdint.h>

/**
 * Simple math functions for audio generation
 */

#define M_PI 3.14159265358979323846

/**
 * sin - Sine function using Taylor series approximation
 * 
 * Good enough for audio tone generation.
 */
double sin(double x)
{
    /* Normalize x to [-PI, PI] range */
    while (x > M_PI)
        x -= 2.0 * M_PI;
    while (x < -M_PI)
        x += 2.0 * M_PI;

    /* Taylor series approximation: sin(x) ≈ x - x³/3! + x⁵/5! - x⁷/7! */
    double x2 = x * x;
    double x3 = x * x2;
    double x5 = x3 * x2;
    double x7 = x5 * x2;
    
    return x - (x3 / 6.0) + (x5 / 120.0) - (x7 / 5040.0);
}

/**
 * cos - Cosine function
 */
double cos(double x)
{
    return sin(x + M_PI / 2.0);
}

/**
 * sqrt - Square root using Newton's method
 */
double sqrt(double x)
{
    if (x <= 0.0)
        return 0.0;

    double guess = x / 2.0;
    int i;
    
    for (i = 0; i < 10; i++) {
        guess = (guess + x / guess) / 2.0;
    }
    
    return guess;
}

/**
 * pow - Power function (integer exponent only)
 */
double pow(double base, int exp)
{
    double result = 1.0;
    int i;
    
    if (exp < 0) {
        base = 1.0 / base;
        exp = -exp;
    }
    
    for (i = 0; i < exp; i++) {
        result *= base;
    }
    
    return result;
}

/**
 * abs - Absolute value (integer)
 */
int abs(int x)
{
    return x < 0 ? -x : x;
}

/**
 * fabs - Absolute value (floating point)
 */
double fabs(double x)
{
    return x < 0.0 ? -x : x;
}
