#include "assets.h"

#include "SDL3/SDL_log.h"
#include "assets/font.h"
#include "core/engine.h"

bool Assets_Init(EngineContext* ctx) {
    ctx->assets.defaultFont = (Font){
        .atlas_w = 144,
        .atlas_h = 352,
        .glyph_w = 8,
        .glyph_h = 8,
        .cols = 18,
        .rows = 44
    };

    if (!loadFont(ctx, &ctx->assets.defaultFont, "assets/fonts/ToshibaSat_8x8.fbm")) {
        SDL_LogCritical(SDL_LOG_CATEGORY_RENDER, "Font load failed: %s", SDL_GetError());
        return false;
    }

    return true;
}

void Assets_Destroy(EngineContext* ctx) {
    if (!ctx) {
        return;
    }

    destroyFont(ctx, &ctx->assets.defaultFont);
}
