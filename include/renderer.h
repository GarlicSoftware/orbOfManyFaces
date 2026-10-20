#pragma once
#include <stdint.h>
#include <time.h>

bool rendererBegin(int brightness, int rotation);
void renderClock(int face, const struct tm &local, bool valid, bool force);
void rendererBrightness(int value);
uint32_t rendererFrames();
uint32_t rendererLastUs();
uint32_t rendererPushedPixels();
const uint16_t *rendererCapture();
void rendererSetFaces(const struct Face *items,int count);
bool rendererPrepare(int face);
void rendererMenu(const char *const *names,int count,int selected,const char *message);
