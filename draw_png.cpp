/**
 * This file contains functions to create and draw to a png image
 */

#include "draw_png.h"
#include "colors.h"
#include "globals.h"
#include "helper.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <list>
#include <png.h>
#ifndef _WIN32
#include <sys/stat.h>
#endif
#if defined(_WIN32) && !defined(__GNUC__)
#include <direct.h>
#endif

#ifndef Z_BEST_SPEED
#define Z_BEST_SPEED 6
#endif

#define PIXEL(x, y)                                                            \
  (gImageBuffer[((x) + gOffsetX) * CHANSPERPIXEL +                             \
                ((y) + gOffsetY) * gPngLocalLineWidthChans])

namespace {
struct ImagePart {
  int x, y, width, height;
  char *filename;
  FILE *pngFileHandle;
  png_structp pngPtr;
  png_infop pngInfo;
  ImagePart(const char *_file, int _x, int _y, int _w, int _h) {
    filename = strdup(_file);
    x = _x;
    y = _y;
    width = _w;
    height = _h;
    pngPtr = NULL;
    pngFileHandle = NULL;
    pngInfo = NULL;
  }
  ~ImagePart() { free(filename); }
};

struct ImageTile {
  FILE *fileHandle;
  png_structp pngPtr;
  png_infop pngInfo;
};
typedef std::list<ImagePart *> imageList;
imageList partialImages;

uint8_t *gImageBuffer = NULL;
int gPngLocalLineWidthChans = 0, gPngLocalWidth = 0, gPngLocalHeight = 0;
int gPngLineWidthChans = 0, gPngWidth = 0, gPngHeight = 0;
int gOffsetX = 0, gOffsetY = 0;
uint64_t gPngSize = 0, gPngLocalSize = 0;
png_structp pngPtrMain = NULL; // Main image
png_infop pngInfoPtrMain = NULL;
png_structp pngPtrCurrent = NULL; // This will be either the same as above, or a
                                  // temp image when using disk caching
FILE *gPngPartialFileHandle = NULL;

inline void blend(uint8_t *const destination, const uint8_t *const source);
inline void modColor(uint8_t *const color, const int mod);
inline void addColor(uint8_t *const color, const uint8_t *const add);

} // namespace

void createImageBuffer(const size_t width, const size_t height,
                       const bool splitUp) {
  gPngLocalWidth = gPngWidth = (int)width;
  gPngLocalHeight = gPngHeight = (int)height;
  gPngLocalLineWidthChans = gPngLineWidthChans = gPngWidth * CHANSPERPIXEL;
  gPngSize = gPngLocalSize =
      (uint64_t)gPngLineWidthChans * (uint64_t)gPngHeight;
  printf("Image dimensions are %dx%d, 32bpp, %.2fMiB\n", gPngWidth, gPngHeight,
         float(gPngSize / float(1024 * 1024)));
  if (!splitUp) {
    gImageBuffer = new uint8_t[gPngSize];
    memset(gImageBuffer, 0, (size_t)gPngSize);
  }
}

bool PNG::Image::create() {
  gPngLocalWidth = gPngWidth = (int)width;
  gPngLocalHeight = gPngHeight = (int)height;

  gPngLocalLineWidthChans = gPngLineWidthChans = gPngWidth * 4;

  gPngSize = gPngLocalSize =
      (uint64_t)gPngLineWidthChans * (uint64_t)gPngHeight;

  printf("Image dimensions are %dx%d, 32bpp, %.2fMiB\n", gPngWidth, gPngHeight,
         float(gPngSize / float(1024 * 1024)));

  gImageBuffer = new uint8_t[gPngSize];
  memset(gImageBuffer, 0, (size_t)gPngSize);
  fseeko(imageHandle, 0, SEEK_SET);
  // Write header
  pngPtrMain = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

  if (pngPtrMain == NULL) {
    return false;
  }

  pngInfoPtrMain = png_create_info_struct(pngPtrMain);

  if (pngInfoPtrMain == NULL) {
    png_destroy_write_struct(&pngPtrMain, NULL);
    return false;
  }

  if (setjmp(png_jmpbuf(pngPtrMain))) { // libpng will issue a longjmp on error,
                                        // so code flow will end up
    png_destroy_write_struct(
        &pngPtrMain,
        &pngInfoPtrMain); // here if something goes wrong in the code below
    return false;
  }

  png_init_io(pngPtrMain, imageHandle);

  png_set_IHDR(pngPtrMain, pngInfoPtrMain, (uint32_t)width, (uint32_t)height, 8,
               PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
               PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

  png_text title_text;
  title_text.compression = PNG_TEXT_COMPRESSION_NONE;
  title_text.key = (png_charp) "Software";
  title_text.text = (png_charp) "mcmap";
  png_set_text(pngPtrMain, pngInfoPtrMain, &title_text, 1);

  png_write_info(pngPtrMain, pngInfoPtrMain);
  pngPtrCurrent = pngPtrMain;

  return true;
}

bool PNG::Image::save() {
  // libpng will issue a longjmp on error, so code flow will end up here if
  // something goes wrong in the code below
  if (setjmp(png_jmpbuf(pngPtrMain))) {
    png_destroy_write_struct(&pngPtrMain, &pngInfoPtrMain);
    return false;
  }

  uint8_t *srcLine = gImageBuffer;

  printf("Writing to file...\n");
  for (int y = 0; y < gPngHeight; ++y) {
    png_write_row(pngPtrMain, (png_bytep)srcLine);
    srcLine += gPngLineWidthChans;
  }

  png_write_end(pngPtrMain, NULL);

  png_destroy_write_struct(&pngPtrMain, &pngInfoPtrMain);
  delete[] gImageBuffer;
  gImageBuffer = NULL;
  return true;
}

/**
 * @return 0 = OK, -1 = Error, 1 = Zero/Negative size
 */
int loadImagePart(const int startx, const int starty, const int width,
                  const int height) {
  // These are set to NULL in saveImahePartPng to make sure the two functions
  // are called in turn
  if (pngPtrCurrent != NULL || gPngPartialFileHandle != NULL) {
    printf("Something wrong with disk caching.\n");
    return -1;
  }
  // In case the image needs to be cropped the offsets will be negative
  gOffsetX = MIN(startx, 0);
  gOffsetY = MIN(starty, 0);
  gPngLocalWidth = width;
  gPngLocalHeight = height;
  int localX = startx;
  int localY = starty;
  // Also modify gPngLocalWidth and gPngLocalHeight in these cases
  if (localX < 0) {
    gPngLocalWidth += localX;
    localX = 0;
  }
  if (localY < 0) {
    gPngLocalHeight += localY;
    localY = 0;
  }
  if (localX + gPngLocalWidth > gPngWidth) {
    gPngLocalWidth = gPngWidth - localX;
  }
  if (localY + gPngLocalHeight > gPngHeight) {
    gPngLocalHeight = gPngHeight - localY;
  }
  if (gPngLocalWidth < 1 || gPngLocalHeight < 1)
    return 1;
  char name[200];
  snprintf(name, 200, "cache/%d.%d.%d.%d.%d.png", localX, localY,
           gPngLocalWidth, gPngLocalHeight, (int)time(NULL));
  ImagePart *img =
      new ImagePart(name, localX, localY, gPngLocalWidth, gPngLocalHeight);
  partialImages.push_back(img);
  // alloc mem for image and open tempfile
  gPngLocalLineWidthChans = gPngLocalWidth * CHANSPERPIXEL;
  uint64_t size = (uint64_t)gPngLocalLineWidthChans * (uint64_t)gPngLocalHeight;
  printf("Creating temporary image: %dx%d, 32bpp, %.2fMiB\n", gPngLocalWidth,
         gPngLocalHeight, float(size / float(1024 * 1024)));
  if (gImageBuffer == NULL) {
    gImageBuffer = new uint8_t[size];
    gPngLocalSize = size;
  } else if (size > gPngLocalSize) {
    delete[] gImageBuffer;
    gImageBuffer = new uint8_t[size];
    gPngLocalSize = size;
  }
  memset(gImageBuffer, 0, (size_t)size);
  // Create temp image
  // This is done here to detect early if the target is not writable
#ifdef _WIN32
  mkdir("cache");
#else
  mkdir("cache", 0755);
#endif
  gPngPartialFileHandle = fopen(name, "wb");
  if (gPngPartialFileHandle == NULL) {
    printf("Could not create temporary image at %s; check permissions in "
           "current dir.\n",
           name);
    return -1;
  }
  return 0;
}

bool saveImagePart() {
  if (gPngPartialFileHandle == NULL || pngPtrCurrent != NULL) {
    printf("saveImagePart() called in bad state.\n");
    return false;
  }
  // Write header
  png_infop info_ptr = NULL;
  pngPtrCurrent =
      png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

  if (pngPtrCurrent == NULL) {
    return false;
  }

  info_ptr = png_create_info_struct(pngPtrCurrent);

  if (info_ptr == NULL) {
    png_destroy_write_struct(&pngPtrCurrent, NULL);
    return false;
  }

  if (setjmp(png_jmpbuf(pngPtrCurrent))) { // libpng will issue a longjmp on
                                           // error, so code flow will end up
    png_destroy_write_struct(
        &pngPtrCurrent,
        &info_ptr); // here if something goes wrong in the code below
    return false;
  }

  png_init_io(pngPtrCurrent, gPngPartialFileHandle);
  png_set_compression_level(pngPtrCurrent, Z_BEST_SPEED);

  png_set_IHDR(pngPtrCurrent, info_ptr, (uint32_t)gPngLocalWidth,
               (uint32_t)gPngLocalHeight, 8, PNG_COLOR_TYPE_RGBA,
               PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE,
               PNG_FILTER_TYPE_BASE);

  png_write_info(pngPtrCurrent, info_ptr);

  uint8_t *line = gImageBuffer;
  for (int y = 0; y < gPngLocalHeight; ++y) {
    png_write_row(pngPtrCurrent, (png_bytep)line);
    line += gPngLocalLineWidthChans;
  }
  png_write_end(pngPtrCurrent, NULL);
  png_destroy_write_struct(&pngPtrCurrent, &info_ptr);
  pngPtrCurrent = NULL;
  fclose(gPngPartialFileHandle);
  gPngPartialFileHandle = NULL;
  return true;
}

bool discardImagePart() {
  if (gPngPartialFileHandle == NULL || pngPtrCurrent != NULL) {
    printf("discardImagePart() called in bad state.\n");
    return false;
  }
  fclose(gPngPartialFileHandle);
  gPngPartialFileHandle = NULL;
  ImagePart *img = partialImages.back();
  remove(img->filename);
  delete img;
  partialImages.pop_back();
  return true;
}

bool composeFinalImage() {
  char *tmpString = NULL;
  size_t tmpLen = 0;
  if (g_TilePath == NULL) {
    printf("Composing final png file...\n");
    if (setjmp(png_jmpbuf(pngPtrMain))) {
      png_destroy_write_struct(&pngPtrMain, NULL);
      return false;
    }
  } else {
    // Tiled output, suitable for google maps
    printf("Composing final png files...\n");
    tmpLen = strlen(g_TilePath) + 40;
    tmpString = new char[tmpLen];
    // Prepare a temporary buffer to copy the current line to, since we need the
    // width to be a multiple of 4096 and adjusting the whole image to that
    // would be a waste of memory
  }
  const size_t tempWidth =
      (g_TilePath == NULL ? gPngLineWidthChans
                          : ((gPngWidth - 5) / 4096 + 1) * 4096);
  const size_t tempWidthChans = tempWidth * CHANSPERPIXEL;

  uint8_t *lineWrite = new uint8_t[tempWidthChans];
  uint8_t *lineRead = new uint8_t[gPngLineWidthChans];

  // Prepare an array of png structs that will output simultaneously to the
  // various tiles
  size_t sizeOffset[7], last = 0;
  ImageTile *tile = NULL;
  if (g_TilePath != NULL) {
    for (size_t i = 0; i < 7; ++i) {
      sizeOffset[i] = last;
      last += ((tempWidth - 1) / pow(2, 12 - i)) + 1;
    }
    tile = new ImageTile[sizeOffset[6]];
    memset(tile, 0, sizeOffset[6] * sizeof(ImageTile));
  }

  for (int y = 0; y < gPngHeight; ++y) {
    if (y % 100 == 0) {
      printProgress(size_t(y), size_t(gPngHeight));
    }
    // paint each image on this one
    memset(lineWrite, 0, tempWidthChans);
    // the partial images are kept in this list. they're already in the correct
    // order in which they have to me merged and blended
    for (imageList::iterator it = partialImages.begin();
         it != partialImages.end(); it++) {
      ImagePart *img = *it;
      // do we have to open this image?
      if (img->y == y && img->pngPtr == NULL) {
        img->pngFileHandle = fopen(img->filename, "rb");
        if (img->pngFileHandle == NULL) {
          printf("Error opening temporary image %s\n", img->filename);
          return false;
        }
        img->pngPtr =
            png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
        if (img->pngPtr == NULL) {
          printf("Error creating read struct for temporary image %s\n",
                 img->filename);
          return false; // Not really cleaning up here, but program will
                        // terminate anyways, so why bother
        }
        img->pngInfo = png_create_info_struct(img->pngPtr);
        if (img->pngInfo == NULL || setjmp(png_jmpbuf(img->pngPtr))) {
          printf("Error reading data from temporary image %s\n", img->filename);
          return false; // Same here
        }
        png_init_io(img->pngPtr, img->pngFileHandle);
        png_read_info(img->pngPtr, img->pngInfo);
        // Check if image dimensions match what is expected
        int type, interlace, comp, filter, bitDepth;
        png_uint_32 width, height;
        png_uint_32 ret =
            png_get_IHDR(img->pngPtr, img->pngInfo, &width, &height, &bitDepth,
                         &type, &interlace, &comp, &filter);
        if (ret == 0 || width != (png_uint_32)img->width ||
            height != (png_uint_32)img->height) {
          printf(
              "Temp image %s has wrong dimensions; expected %dx%d, got %dx%d\n",
              img->filename, img->width, img->height, (int)width, (int)height);
          return false;
        }
      }
      // Here, the image is either open and ready for reading another line, or
      // its not open when it doesn't have to be copied/blended here, or is
      // already finished
      if (img->pngPtr == NULL) {
        continue; // Not your turn, image!
      }
      // Read next line from current image chunk
      png_read_row(img->pngPtr, (png_bytep)lineRead, NULL);
      // Now this puts all the pixels in the right spot of the current line of
      // the final image
      const uint8_t *end = lineWrite + (img->x + img->width) * CHANSPERPIXEL;
      uint8_t *read = lineRead;
      for (uint8_t *write = lineWrite + (img->x * CHANSPERPIXEL); write < end;
           write += CHANSPERPIXEL) {
        blend(write, read);
        read += CHANSPERPIXEL;
      }
      // Now check if we're done with this image chunk
      if (--(img->height) == 0) { // if so, close and discard
        png_destroy_read_struct(&(img->pngPtr), &(img->pngInfo), NULL);
        fclose(img->pngFileHandle);
        img->pngFileHandle = NULL;
        img->pngPtr = NULL;
        remove(img->filename);
      }
    }
    // Done composing this line, write to final image
    if (g_TilePath == NULL) {
      // Single file
      png_write_row(pngPtrMain, (png_bytep)lineWrite);
    } else {
      // Tiled output
      // Handle all png files
      if (y % 128 == 0) {
        size_t start;
        if (y % 4096 == 0)
          start = 0;
        else if (y % 2048 == 0)
          start = 1;
        else if (y % 1024 == 0)
          start = 2;
        else if (y % 512 == 0)
          start = 3;
        else if (y % 256 == 0)
          start = 4;
        else
          start = 5;
        for (size_t tileSize = start; tileSize < 6; ++tileSize) {
          const size_t tileWidth = pow(2, 12 - tileSize);
          for (size_t tileIndex = sizeOffset[tileSize];
               tileIndex < sizeOffset[tileSize + 1]; ++tileIndex) {
            ImageTile &t = tile[tileIndex];
            if (t.fileHandle != NULL) { // Unload/close first
              // printf("Calling end with ptr == %p, y == %d, start == %d,
              // tileSize == %d, tileIndex == %d, to == %d, numpng == %d\n",
              // t.pngPtr, y, (int)start, (int)tileSize, (int)tileIndex,
              // (int)sizeOffset[tileSize+1], (int)numpng);
              png_write_end(t.pngPtr, NULL);
              png_destroy_write_struct(&(t.pngPtr), &(t.pngInfo));
              fclose(t.fileHandle);
              t.fileHandle = NULL;
            }
            if (tileWidth * (tileIndex - sizeOffset[tileSize]) <
                size_t(gPngWidth)) {
              // Open new tile file for a while
              snprintf(tmpString, tmpLen, "%s/x%dy%dz%d.png", g_TilePath,
                       int(tileIndex - sizeOffset[tileSize]),
                       int((y / pow(2, 12 - tileSize))), int(tileSize));
#ifdef _DEBUG
              printf("Starting tile %s of size %d...\n", tmpString,
                     (int)pow(2, 12 - tileSize));
#endif
              t.fileHandle = fopen(tmpString, "wb");
              if (t.fileHandle == NULL) {
                printf("Error opening file!\n");
                return false;
              }
              t.pngPtr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL,
                                                 NULL, NULL);
              if (t.pngPtr == NULL) {
                printf("Error creating png write struct!\n");
                return false;
              }
              if (setjmp(png_jmpbuf(t.pngPtr))) {
                return false;
              }
              t.pngInfo = png_create_info_struct(t.pngPtr);
              if (t.pngInfo == NULL) {
                printf("Error creating png info struct!\n");
                png_destroy_write_struct(&(t.pngPtr), NULL);
                return false;
              }
              png_init_io(t.pngPtr, t.fileHandle);
              png_set_IHDR(t.pngPtr, t.pngInfo, uint32_t(pow(2, 12 - tileSize)),
                           uint32_t(pow(2, 12 - tileSize)), 8,
                           PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
                           PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
              png_write_info(t.pngPtr, t.pngInfo);
            }
          }
        }
      } // done preparing tiles
      // Write data to all current tiles
      for (size_t tileSize = 0; tileSize < 6; ++tileSize) {
        const size_t tileWidth = pow(2, 12 - tileSize);
        for (size_t tileIndex = sizeOffset[tileSize];
             tileIndex < sizeOffset[tileSize + 1]; ++tileIndex) {
          if (tile[tileIndex].fileHandle == NULL)
            continue;
          png_write_row(
              tile[tileIndex].pngPtr,
              png_bytep(lineWrite + tileWidth *
                                        (tileIndex - sizeOffset[tileSize]) *
                                        CHANSPERPIXEL));
        }
      } // done writing line
        //
    }
    // Y-Loop
  }
  if (g_TilePath == NULL) {
    png_write_end(pngPtrMain, NULL);
    png_destroy_write_struct(&pngPtrMain, &pngInfoPtrMain);
  } else {
    // Finish all current tiles
    memset(lineWrite, 0, tempWidth * BYTESPERPIXEL);
    for (size_t tileSize = 0; tileSize < 6; ++tileSize) {
      const size_t tileWidth = pow(2, 12 - tileSize);
      for (size_t tileIndex = sizeOffset[tileSize];
           tileIndex < sizeOffset[tileSize + 1]; ++tileIndex) {
        if (tile[tileIndex].fileHandle == NULL)
          continue;
        const int imgEnd = (((gPngHeight - 1) / tileWidth) + 1) * tileWidth;
        for (int i = gPngHeight; i < imgEnd; ++i) {
          png_write_row(tile[tileIndex].pngPtr, png_bytep(lineWrite));
        }
        png_write_end(tile[tileIndex].pngPtr, NULL);
        png_destroy_write_struct(&(tile[tileIndex].pngPtr),
                                 &(tile[tileIndex].pngInfo));
        fclose(tile[tileIndex].fileHandle);
      }
    }
  }
  printProgress(10, 10);
  delete[] lineWrite;
  delete[] lineRead;
  return true;
}

namespace {

inline void blend(uint8_t *const destination, const uint8_t *const source) {
  if (destination[PALPHA] == 0 || source[PALPHA] == 255) {
    memcpy(destination, source, BYTESPERPIXEL);
    return;
  }
#define BLEND(ca, aa, cb)                                                      \
  uint8_t(((size_t(ca) * size_t(aa)) + (size_t(255 - aa) * size_t(cb))) / 255)
  destination[0] = BLEND(source[0], source[PALPHA], destination[0]);
  destination[1] = BLEND(source[1], source[PALPHA], destination[1]);
  destination[2] = BLEND(source[2], source[PALPHA], destination[2]);
  destination[PALPHA] +=
      (size_t(source[PALPHA]) * size_t(255 - destination[PALPHA])) / 255;
}

inline void blend(uint8_t *const destination, const Colors::Block *block) {
  if (destination[PALPHA] == 0 || block->primary.ALPHA == 255) {
    memcpy(destination, &block->primary, BYTESPERPIXEL);
    return;
  }
#define BLEND(ca, aa, cb)                                                      \
  uint8_t(((size_t(ca) * size_t(aa)) + (size_t(255 - aa) * size_t(cb))) / 255)
  destination[0] =
      BLEND(block->primary.R, block->primary.ALPHA, destination[0]);
  destination[1] =
      BLEND(block->primary.G, block->primary.ALPHA, destination[1]);
  destination[2] =
      BLEND(block->primary.B, block->primary.ALPHA, destination[2]);
  destination[PALPHA] +=
      (size_t(block->primary.ALPHA) * size_t(255 - destination[PALPHA])) / 255;
}

inline void modColor(uint8_t *const color, const int mod) {
  color[0] = clamp(color[0] + mod);
  color[1] = clamp(color[1] + mod);
  color[2] = clamp(color[2] + mod);
}

inline void addColor(uint8_t *const color, const uint8_t *const add) {
  const float v2 = (float(add[PALPHA]) / 255.0f);
  const float v1 = (1.0f - (v2 * .2f));
  color[0] = clamp(uint16_t(float(color[0]) * v1 + float(add[0]) * v2));
  color[1] = clamp(uint16_t(float(color[1]) * v1 + float(add[1]) * v2));
  color[2] = clamp(uint16_t(float(color[2]) * v1 + float(add[2]) * v2));
}

void drawHead(const size_t x, const size_t y, const NBT &,
              const Colors::Block *block) {
  /* Small block centered
   * |    |
   * |    |
   * | PP |
   * | DL | */
  uint8_t *pos = &PIXEL(x + 1, y + 2);
  memcpy(pos, &block->primary, BYTESPERPIXEL);
  memcpy(pos + CHANSPERPIXEL, &block->primary, BYTESPERPIXEL);

  pos = &PIXEL(x + 1, y + 3);
  memcpy(pos, &block->dark, BYTESPERPIXEL);
  memcpy(pos + CHANSPERPIXEL, &block->light, BYTESPERPIXEL);
}

void drawThin(const size_t x, const size_t y, const NBT &,
              const Colors::Block *block) {
  /* Overwrite the block below's top layer
   * |    |
   * |    |
   * |    |
   * |XXXX|
   *   XX   */
  uint8_t *pos = &PIXEL(x, y + 3);
  for (size_t i = 0; i < 4; ++i, pos += CHANSPERPIXEL) {
    memcpy(pos, &block->primary, BYTESPERPIXEL);
  }
#ifndef LEGACY
  pos = &PIXEL(x + 1, y + 4);
  memcpy(pos, &block->primary, BYTESPERPIXEL);
  memcpy(pos + CHANSPERPIXEL, &block->primary, BYTESPERPIXEL);
#endif
}

void drawHidden(const size_t, const size_t, const NBT &,
                const Colors::Block *) {
  return;
}

void drawTransparent(const size_t x, const size_t y, const NBT &,
                     const Colors::Block *block) {
  // Avoid the dark/light edges for a clearer look through
  for (uint8_t i = 0; i < 4; i++)
    for (uint8_t j = 0; j < 3; j++)
      blend(&PIXEL(x + i, y + j), block);
}

void drawTorch(const size_t x, const size_t y, const NBT &,
               const Colors::Block *block) {
  /* TODO Callback to handle the orientation
   * Print the secondary on top of two primary
   * |    |
   * |  S |
   * |  P |
   * |  P | */
  uint8_t *pos = &PIXEL(x + 2, y + 1);
  memcpy(pos, &block->secondary, BYTESPERPIXEL);
  pos = &PIXEL(x + 2, y + 2);
#ifdef LEGACY
  memcpy(pos, &block->secondary, BYTESPERPIXEL);
#else
  memcpy(pos, &block->primary, BYTESPERPIXEL);
  pos = &PIXEL(x + 2, y + 3);
  memcpy(pos, &block->primary, BYTESPERPIXEL);
#endif
}

void drawPlant(const size_t x, const size_t y, const NBT &,
               const Colors::Block *block) {
  /* Print a plant-like block
   * TODO Make that nicer ?
   * |    |
   * | X X|
   * |  X |
   * | X  | */
  uint8_t *pos = &PIXEL(x, y + 1);
  memcpy(pos + (CHANSPERPIXEL), &block->primary, BYTESPERPIXEL);
  memcpy(pos + (CHANSPERPIXEL * 3), &block->primary, BYTESPERPIXEL);
  pos = &PIXEL(x + 2, y + 2);
  memcpy(pos, &block->primary, BYTESPERPIXEL);
  pos = &PIXEL(x + 1, y + 3);
  memcpy(pos, &block->primary, BYTESPERPIXEL);
}

void drawFire(const size_t x, const size_t y, const NBT &,
              const Colors::Block *const color) {
  // This basically just leaves out a few pixels
  // Top row
  uint8_t *pos = &PIXEL(x, y);
  blend(pos, (uint8_t *)&color->light);
  blend(pos + CHANSPERPIXEL * 2, (uint8_t *)&color->dark);
  // Second and third row
  for (size_t i = 1; i < 3; ++i) {
    pos = &PIXEL(x, y + i);
    blend(pos, (uint8_t *)&color->dark);
    blend(pos + (CHANSPERPIXEL * i), color);
    blend(pos + (CHANSPERPIXEL * 3), (uint8_t *)&color->light);
  }
  // Last row
  pos = &PIXEL(x, y + 3);
  blend(pos + (CHANSPERPIXEL * 2), (uint8_t *)&color->light);
}

void drawOre(const size_t x, const size_t y, const NBT &,
             const Colors::Block *color) {
  /* Print a vein with the secondary in the block
   * |PPPS|
   * |DDSL|
   * |DSLS|
   * |SDLL| */

  // Top row
  uint8_t *pos = &PIXEL(x, y);
  for (size_t i = 0; i < 4; ++i, pos += CHANSPERPIXEL)
    memcpy(pos, (i == 3 ? &color->secondary : &color->primary), BYTESPERPIXEL);

  // Second row
  pos = &PIXEL(x, y + 1);
  memcpy(pos, &color->dark, BYTESPERPIXEL);
  memcpy(pos + CHANSPERPIXEL, &color->dark, BYTESPERPIXEL);
  memcpy(pos + CHANSPERPIXEL * 2, &color->secondary, BYTESPERPIXEL);
  memcpy(pos + CHANSPERPIXEL * 3, &color->light, BYTESPERPIXEL);
  // Third row
  pos = &PIXEL(x, y + 2);
  memcpy(pos, &color->dark, BYTESPERPIXEL);
  memcpy(pos + CHANSPERPIXEL, &color->secondary, BYTESPERPIXEL);
  memcpy(pos + CHANSPERPIXEL * 2, &color->light, BYTESPERPIXEL);
  memcpy(pos + CHANSPERPIXEL * 3, &color->secondary, BYTESPERPIXEL);
  // Last row
  pos = &PIXEL(x, y + 3);
  memcpy(pos, &color->secondary, BYTESPERPIXEL);
  memcpy(pos + CHANSPERPIXEL, &color->dark, BYTESPERPIXEL);
  memcpy(pos + CHANSPERPIXEL * 2, &color->light, BYTESPERPIXEL);
  memcpy(pos + CHANSPERPIXEL * 3, &color->light, BYTESPERPIXEL);
}

void drawGrown(const size_t x, const size_t y, const NBT &,
               const Colors::Block *color) {
  /* Print the secondary color on top
   * |SSSS|
   * |DSSL|
   * |DDLL|
   * |DDLL| */

  // First determine how much the color has to be lightened up or darkened
  // The brighter the color, the stronger the impact
  int sub = (float(color->primary.BRIGHTNESS) / 323.0f + .21f);

  uint8_t L[CHANSPERPIXEL], D[CHANSPERPIXEL];
  memcpy(L, &color->secondary, BYTESPERPIXEL);
  memcpy(D, &color->secondary, BYTESPERPIXEL);
  modColor(L, sub - 15);
  modColor(D, sub - 25);

  // Top row
  uint8_t *pos = &PIXEL(x, y);
  for (size_t i = 0; i < 4; ++i, pos += CHANSPERPIXEL) {
    memcpy(pos, &color->secondary, BYTESPERPIXEL);
  }

  // Second row
  pos = &PIXEL(x, y + 1);
  memcpy(pos, &color->dark, BYTESPERPIXEL);
#ifdef LEGACY
  memcpy(pos + CHANSPERPIXEL, &color->dark, BYTESPERPIXEL);
  memcpy(pos + CHANSPERPIXEL * 2, &color->light, BYTESPERPIXEL);
#else
  memcpy(pos + CHANSPERPIXEL, D, BYTESPERPIXEL);
  memcpy(pos + CHANSPERPIXEL * 2, L, BYTESPERPIXEL);
#endif
  memcpy(pos + CHANSPERPIXEL * 3, &color->light, BYTESPERPIXEL);

  // Third row
  pos = &PIXEL(x, y + 2);
  memcpy(pos, &color->dark, BYTESPERPIXEL);
  memcpy(pos + CHANSPERPIXEL, &color->dark, BYTESPERPIXEL);
  memcpy(pos + CHANSPERPIXEL * 2, &color->light, BYTESPERPIXEL);
  memcpy(pos + CHANSPERPIXEL * 3, &color->light, BYTESPERPIXEL);

  // Last row
  pos = &PIXEL(x, y + 3);
  memcpy(pos, &color->dark, BYTESPERPIXEL);
  memcpy(pos + CHANSPERPIXEL, &color->dark, BYTESPERPIXEL);
  memcpy(pos + CHANSPERPIXEL * 2, &color->light, BYTESPERPIXEL);
  memcpy(pos + CHANSPERPIXEL * 3, &color->light, BYTESPERPIXEL);
}

void drawRod(const size_t x, const size_t y, const NBT &,
             const Colors::Block *const color) {
  /* A full fat rod
   * | PP |
   * | DL |
   * | DL |
   * | DL | */
  uint8_t *pos = &PIXEL(x + 1, y);
  memcpy(pos, &color->primary, BYTESPERPIXEL);
  memcpy(pos + CHANSPERPIXEL, &color->primary, BYTESPERPIXEL);
  for (int i = 1; i < 4; i++) {
    pos = &PIXEL(x + 1, y + i);
    memcpy(pos, &color->dark, BYTESPERPIXEL);
    memcpy(pos + CHANSPERPIXEL, &color->light, BYTESPERPIXEL);
  }
}

void drawSlab(const size_t x, const size_t y, const NBT &metadata,
              const Colors::Block *color) {
  /* This one has a hack to make it look like a gradual step up:
   * The second layer has primary colors to make the difference less
   * obvious.
   * |    |
   * |PPPP|
   * |DPPL|
   * |DDLL| */

  bool top = false;
#define SLAB_OFFSET (top ? 0 : 1)
  if (*metadata["Properties"]["type"].get<const string *>() == "top")
    top = true;

  uint8_t *pos = &PIXEL(x, y + SLAB_OFFSET);
  for (size_t i = 0; i < 4; ++i, pos += CHANSPERPIXEL) {
    memcpy(pos, &color->primary, BYTESPERPIXEL);
  }

  pos = &PIXEL(x, y + SLAB_OFFSET + 1);
  memcpy(pos, &color->dark, BYTESPERPIXEL);
  if (top) {
    memcpy(pos + CHANSPERPIXEL, &color->dark, BYTESPERPIXEL);
    memcpy(pos + CHANSPERPIXEL * 2, &color->light, BYTESPERPIXEL);
  } else {
    memcpy(pos + CHANSPERPIXEL, &color->primary, BYTESPERPIXEL);
    memcpy(pos + CHANSPERPIXEL * 2, &color->primary, BYTESPERPIXEL);
  }
  memcpy(pos + CHANSPERPIXEL * 3, &color->light, BYTESPERPIXEL);

  pos = &PIXEL(x, y + SLAB_OFFSET + 2);
  for (size_t i = 0; i < 4; ++i, pos += CHANSPERPIXEL) {
    memcpy(pos, (i < 2 ? &color->dark : &color->light), BYTESPERPIXEL);
  }
#undef SLAB_OFFSET
}

void drawWire(const size_t x, const size_t y, const NBT &,
              const Colors::Block *color) {
  uint8_t *pos = &PIXEL(x + 1, y + 2);
  memcpy(pos, &color->primary, BYTESPERPIXEL);
  memcpy(pos + CHANSPERPIXEL, &color->primary, BYTESPERPIXEL);
}

/*
        void setUpStep(const size_t x, const size_t y, const uint8_t * const
   color, const uint8_t * const light, const uint8_t * const dark) { uint8_t
   *pos = &PIXEL(x, y); for (size_t i = 0; i < 4; ++i, pos += CHANSPERPIXEL) {
                        memcpy(pos, color, BYTESPERPIXEL);
                }
                pos = &PIXEL(x, y+1);
                for (size_t i = 0; i < 4; ++i, pos += CHANSPERPIXEL) {
                        memcpy(pos, color, BYTESPERPIXEL);
                }
        }
*/

} // namespace

void drawFull(const size_t x, const size_t y, const NBT &,
              const Colors::Block *color) {
  // Sets pixels around x,y where A is the anchor
  // T = given color, D = darker, L = lighter
  // A T T T
  // D D L L
  // D D L L
  //   D L

  // Ordinary blocks are all rendered the same way
  if (color->primary.ALPHA == 255) { // Fully opaque - faster
    // Top row
    uint8_t *pos = &PIXEL(x, y);
    for (size_t i = 0; i < 4; ++i, pos += CHANSPERPIXEL) {
      memcpy(pos, &color->primary, BYTESPERPIXEL);
    }
    // Second row
    pos = &PIXEL(x, y + 1);
    for (size_t i = 0; i < 4; ++i, pos += CHANSPERPIXEL) {
      memcpy(pos, (i < 2 ? &color->dark : &color->light), BYTESPERPIXEL);
    }
    // Third row
    pos = &PIXEL(x, y + 2);
    for (size_t i = 0; i < 4; ++i, pos += CHANSPERPIXEL) {
      memcpy(pos, (i < 2 ? &color->dark : &color->light), BYTESPERPIXEL);
    }
    // Last row
    pos = &PIXEL(x, y + 3);
    for (size_t i = 0; i < 4; ++i, pos += CHANSPERPIXEL) {
      memcpy(pos, (i < 2 ? &color->dark : &color->light), BYTESPERPIXEL);
      // The weird check here is to get the pattern right, as the noise should
      // be stronger every other row, but take into account the isometric
      // perspective
    }
  } else { // Not opaque, use slower blending code
    // Top row
    uint8_t *pos = &PIXEL(x, y);
    for (size_t i = 0; i < 4; ++i, pos += CHANSPERPIXEL) {
      blend(pos, color);
    }
    // Second row
    pos = &PIXEL(x, y + 1);
    for (size_t i = 0; i < 4; ++i, pos += CHANSPERPIXEL) {
      blend(pos, (i < 2 ? (uint8_t *)&color->dark : (uint8_t *)&color->light));
    }
    // Third row
    pos = &PIXEL(x, y + 2);
    for (size_t i = 0; i < 4; ++i, pos += CHANSPERPIXEL) {
      blend(pos, (i < 2 ? (uint8_t *)&color->dark : (uint8_t *)&color->light));
    }
    // Last row
    pos = &PIXEL(x, y + 3);
    for (size_t i = 0; i < 4; ++i, pos += CHANSPERPIXEL) {
      blend(pos, (i < 2 ? (uint8_t *)&color->dark : (uint8_t *)&color->light));
    }
  }
  // The above two branches are almost the same, maybe one could just create a
  // function pointer and...
}

void (*blockRenderer[])(const size_t, const size_t, const NBT &,
                        const Colors::Block *) = {
    &drawFull,
#define DEFINETYPE(STRING, CALLBACK) &CALLBACK,
#include "./blocktypes.def"
#undef DEFINETYPE
};

void PNG::Image::drawBlock(const size_t x, const size_t y,
                           const NBT &blockData) {
  if (x < 0 || x > width - 1)
    throw std::range_error("Invalid x: " + std::to_string(x) + "/" +
                           std::to_string(gPngWidth));

  if (y < 0 || y > height - 1)
    throw std::range_error("Invalid y: " + std::to_string(y) + "/" +
                           std::to_string(gPngHeight));

  if (!blockData.contains("Name"))
    return;

  const string &name = *(blockData["Name"].get<const string *>());
  auto defined = palette.find(name);

  if (defined == palette.end()) {
    fprintf(stderr, "Error getting color of block %s\n", name.c_str());
    return;
  }

  const Colors::Block *blockColor = &(defined->second);

  if (blockColor->primary.empty())
    return;

  // Then call the function registered with the block's type
  blockRenderer[blockColor->type](x, y, blockData, blockColor);
}
