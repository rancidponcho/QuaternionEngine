#ifndef GAME_STATE_H
#define GAME_STATE_H

#include <stdbool.h>

typedef struct DisplaySettings {
    bool fullscreen;
} DisplaySettings;

typedef struct GameState {
    DisplaySettings display;
} GameState;

void GameState_Init(GameState* game);

#endif // GAME_STATE_H
