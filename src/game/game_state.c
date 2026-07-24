#include "game/game_state.h"

#include <string.h>

void GameState_Init(GameState* game) {
    memset(game, 0, sizeof(*game));

    game->display.fullscreen = true;
}
