#ifndef FONT_H
#define FONT_H

#include <stdbool.h>
#include <SDL3/SDL_gpu.h>

typedef struct EngineContext EngineContext;

typedef struct Font { 
    SDL_GPUTexture* atlas; 
    int atlas_w; 
    int atlas_h; 
    int glyph_w; 
    int glyph_h; 
    int cols; 
    int rows; 
} Font;

bool loadFont(EngineContext* ctx, Font *font, const char* path);
void destroyFont(EngineContext *ctx, Font *font);

#endif // FONT_H
