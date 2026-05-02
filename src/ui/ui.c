#include "ui.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL_gpu.h>

#include "core/engine.h"
#include "assets/font.h"

#define INVALID_TEXTBOX_ID UINT32_MAX

void UI_Init(EngineContext* ctx) {
    ctx->ui.free_count = MAX_TEXTBOXES;

    for (uint32_t i = 0; i < MAX_TEXTBOXES; i++) {
        ctx->ui.textboxes[i].id = i;
        ctx->ui.textboxes[i].active = false;
        ctx->ui.textboxes[i].length = 0;

        ctx->ui.free_stack[i] = MAX_TEXTBOXES - 1 - i;
    }
}

uint32_t UI_CreateTextBox(UI* ui, Font* font, int x, int y) {
    if (ui->free_count == 0) {
        return INVALID_TEXTBOX_ID;
    }

    uint32_t id = ui->free_stack[--ui->free_count];

    TextBox* box = &ui->textboxes[id];

    box->id = id;
    box->font = font;
    box->x = x;
    box->y = y;
    box->length = 0;
    box->active = true;

    return id;
}

void UI_DestroyTextBox(UI *ui, uint32_t id) {
    if (id >= MAX_TEXTBOXES) {
        return;
    }

    TextBox* box = &ui->textboxes[id];
    
    if (!box->active) {
        return;
    }

    box->active = false;
    box->length = 0;
    box->font = NULL;

    ui->free_stack[ui->free_count++] = id;
}

