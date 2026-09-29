#pragma once
#ifndef CELLRESAMPLE_H
#define CELLRESAMPLE_H

#include "libdither.h"
#include <cstdint>

/* Resampling between the film grid and the coarse dither grid, see screengeometry.h for the rationale.
 *
 * Downsampling averages each n x n block of pixels in linear light, which is what a halftone reproduces: the ink
 * coverage of a cell. Blocks on the right/bottom edge may be partial; they are averaged over the pixels that
 * exist, so the edge tone is not darkened by phantom pixels.
 *
 * Upsampling replicates each coarse dot as an n x n block and crops to the film size, so the output always has
 * exactly the source image's dimensions. */

// number of coarse cells needed to cover `pixels` film pixels
int coarseSize(int pixels, int n);

// mono: returns a new image of coarseSize(w) x coarseSize(h); caller frees it with DitherImage_free
DitherImage* downsampleDitherImage(const DitherImage* src, int n);
// color: returns a new image of coarseSize(w) x coarseSize(h); caller frees it with ColorImage_free
ColorImage* downsampleColorImage(const ColorImage* src, int n);

// replicate each coarse value into an n x n block; `out` is width x height
void upsampleCells(const uint8_t* coarse, int coarseWidth, int n, uint8_t* out, int width, int height);
void upsampleCells(const int* coarse, int coarseWidth, int n, int* out, int width, int height);

#endif // CELLRESAMPLE_H
