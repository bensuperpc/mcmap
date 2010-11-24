#ifndef _COLORS_
#define _COLORS_

#include "helper.h"
#include <cmath>

#define GETBRIGHTNESS(c) (uint8_t)sqrt( \
                                        double(2[c] * 2[c]) * .236 + \
                                        double(1[c] * 1[c]) * .601 + \
                                        double(0[c] * 0[c]) * .163)

// Byte order see below. Colors aligned to word boundaries for some speedup
// Brightness is precalculated to speed up calculations later
// Colors are stored twice since BMP and PNG need them in different order
// Noise is supposed to look normal when -noise 10 is given
extern uint8_t colors[256][16];
#define BLUE 0
#define GREEN 1
#define RED 2
#define ALPHA 3
#define NOISE 4
#define BRIGHTNESS 5
#define PRED 8
#define PGREEN 9
#define PBLUE 10
#define PALPHA 11

void loadColors();
bool loadColorsFromFile(const char *file);
bool dumpColorsToFile(const char *file);
bool extractColors(const char *file);
bool loadBiomeColors(const char* path);

#define AIR 0
#define STONE 1
#define GRASS 2
#define DIRT 3
#define COBBLESTONE 4
#define WOOD 5
#define WATER 8
#define STAT_WATER 9
#define LAVA 10
#define STAT_LAVA 11
#define SAND 12
#define GRAVEL 13
#define LOG 17
#define LEAVES 18
#define FLOWERY 37
#define FLOWERR 38
#define MUSHROOMB 39
#define MUSHROOMR 40
#define DOUBLESTEP 43
#define STEP 44
#define TORCH 50
#define FIRE 51
#define REDWIRE 55
#define RAILROAD 66
#define REDTORCH_OFF 75
#define REDTORCH_ON 76
#define SNOW 78
#define FENCE 85
#define VOIDBLOCK 255 // This will hopefully never be a valid block id in the near future :-)

#endif
