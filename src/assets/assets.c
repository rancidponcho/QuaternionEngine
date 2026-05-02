#include "assets.h"

#include "SDL3/SDL_log.h"
#include "assets/font.h"
#include "core/engine.h"

bool Assets_Init(EngineContext* ctx) {
    ctx->assets.defaultFont = (Font){
        .atlas_w = 162,
        .atlas_h = 224,
        .glyph_w = 9,
        .glyph_h = 14,
        .cols = 18,
        .rows = 16
    };

    if (!loadFont(ctx, &ctx->assets.defaultFont, "assets/fonts/ToshibaSat_9x14.fbm")) {
        SDL_LogCritical(SDL_LOG_CATEGORY_RENDER, "Font load failed: %s", SDL_GetError());
        return false;
    }

   return true;
}

void Assets_Destroy(EngineContext* ctx) {
    destroyFont(ctx, &ctx->assets.defaultFont);
}



