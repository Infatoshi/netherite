/* Minecraft 1.7.10 noise: NoiseGeneratorImproved (Perlin), NoiseGeneratorOctaves,
 * NoiseGeneratorSimplex and NoiseGeneratorPerlin (octaves of simplex). */
#ifndef NETHERITE_NOISE_H
#define NETHERITE_NOISE_H

#include "jrand.h"

struct improved { int p[512]; double xo, yo, zo; };
struct octaves { int n; struct improved g[16]; };
struct simplex { int p[512]; double xo, yo, zo; };
struct perlin { int n; struct simplex g[8]; };

void improved_init(struct improved *n, jrand *r);
void octaves_init(struct octaves *o, jrand *r, int n);
void simplex_init(struct simplex *s, jrand *r);
void perlin_init(struct perlin *p, jrand *r, int n);

/* NoiseGeneratorOctaves.generateNoiseOctaves, 3D: out has xs*ys*zs doubles,
 * index (x * zs + z) * ys + y. */
void octaves_3d(const struct octaves *o, double *out, int x, int y, int z, int xs, int ys, int zs,
                double sx, double sy, double sz);

/* octaves_3d at the points with need[k] set (same index); the others are
 * left unspecified. Each needed point gets the value octaves_3d gives it:
 * the octaves add in the same order, and a y cell's corner gradients are
 * the ones octaves_3d computes at the cell's first y (NoiseGeneratorImproved
 * reuses them with that y), computed when a needed point first reads them. */
void octaves_3d_need(const struct octaves *o, double *out, const unsigned char *need, int x, int y, int z, int xs,
                     int ys, int zs, double sx, double sy, double sz);

/* The 2D overload: 3D with y = 10, ys = 1, sy = 1. Index x * zs + z. */
void octaves_2d(const struct octaves *o, double *out, int x, int z, int xs, int zs, double sx, double sz);

/* NoiseGeneratorPerlin.func_151601_a: one point, no generator offsets. */
double perlin_point(const struct perlin *p, double x, double z);

/* NoiseGeneratorPerlin.func_151599_a (octave falloff 0.5). Index z * xs + x. */
void perlin_2d(const struct perlin *p, double *out, double x, double z, int xs, int zs,
               double sx, double sz, double lacunarity);

#endif
