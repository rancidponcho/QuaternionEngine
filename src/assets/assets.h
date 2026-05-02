#ifndef ASSETS_H
#define ASSETS_H

#include <stdbool.h>

#include "assets/font.h"

typedef struct EngineContext EngineContext;

typedef struct Assets {
    Font defaultFont;
} Assets;

bool Assets_Init(EngineContext* ctx);
void Assets_Destroy(EngineContext* ctx);

#endif // ASSETS_H
