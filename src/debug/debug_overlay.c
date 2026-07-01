#include "debug/debug_overlay.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "core/engine.h"
#include "ui/ui.h"

#define DEBUG_OVERLAY_UPDATE_INTERVAL 0.25f

typedef struct DebugOverlayState {
    uint32_t panel_id;
    float sample_time;
    uint32_t sample_frames;
    bool initialized;
} DebugOverlayState;

static DebugOverlayState g_debug_overlay = {
    .panel_id = UI_INVALID_ID,
    .sample_time = 0.0f,
    .sample_frames = 0,
    .initialized = false
};

void DebugOverlay_Init(EngineContext* ctx) {
    if (g_debug_overlay.initialized) {
        return;
    }

    g_debug_overlay.panel_id = UI_CreatePanel(&ctx->ui, 4, 4, 104);
    if (g_debug_overlay.panel_id == UI_INVALID_ID) {
        return;
    }

    UIStyle style = {
        .padding = 3,
        .border = 1,
        .text_color = UI_COLOR_RGBA(226, 245, 255, 255),
        .fill_color = UI_COLOR_RGBA(0, 0, 0, 0),
        .border_color = UI_COLOR_RGBA(0, 0, 0, 0)
    };

    UI_SetPanelStyle(ctx, g_debug_overlay.panel_id, &style);
    UI_SetPanelText(ctx, g_debug_overlay.panel_id, "FPS --\nMS --");

    g_debug_overlay.sample_time = 0.0f;
    g_debug_overlay.sample_frames = 0;
    g_debug_overlay.initialized = true;
}

void DebugOverlay_Update(EngineContext* ctx, float dt) {
    if (!g_debug_overlay.initialized || g_debug_overlay.panel_id == UI_INVALID_ID) {
        return;
    }

    if (dt < 0.0f) {
        dt = 0.0f;
    }

    g_debug_overlay.sample_time += dt;
    g_debug_overlay.sample_frames++;

    if (g_debug_overlay.sample_time < DEBUG_OVERLAY_UPDATE_INTERVAL) {
        return;
    }

    float fps = 0.0f;
    float ms = 0.0f;
    if (g_debug_overlay.sample_time > 0.0f && g_debug_overlay.sample_frames > 0) {
        fps = (float)g_debug_overlay.sample_frames / g_debug_overlay.sample_time;
        ms = (g_debug_overlay.sample_time * 1000.0f) / (float)g_debug_overlay.sample_frames;
    }

    char text[64];
    snprintf(text, sizeof(text), "FPS %.1f\nMS %.2f", fps, ms);
    UI_SetPanelText(ctx, g_debug_overlay.panel_id, text);

    g_debug_overlay.sample_time = 0.0f;
    g_debug_overlay.sample_frames = 0;
}

void DebugOverlay_Shutdown(EngineContext* ctx) {
    if (g_debug_overlay.panel_id != UI_INVALID_ID) {
        UI_DestroyPanel(&ctx->ui, g_debug_overlay.panel_id);
    }

    g_debug_overlay.panel_id = UI_INVALID_ID;
    g_debug_overlay.sample_time = 0.0f;
    g_debug_overlay.sample_frames = 0;
    g_debug_overlay.initialized = false;
}
