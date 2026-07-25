#include "debug/debug_overlay.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "core/engine.h"
#include "ui/ui.h"

#define DEBUG_OVERLAY_UPDATE_INTERVAL 0.25f

static const char* DEBUG_OVERLAY_PLACEHOLDER =
    "FPS --\n"
    "MS --\n"
    "Nodes --\n"
    "Draw --\n"
    "Links --\n"
    "Bends --\n"
    "Islands --\n"
    "Upd --\n"
    "Mine --\n"
    "MineN --\n"
    "MineA --\n"
    "Player --\n"
    "Grav --\n"
    "Matter --\n"
    "Render --";

static const char* DEBUG_OVERLAY_FORMAT =
    "FPS %.1f\n"
    "MS %.2f\n"
    "Nodes %u\n"
    "Draw %u\n"
    "Links %u\n"
    "Bends %u\n"
    "Islands %u\n"
    "Upd %.2f\n"
    "Mine %.2f\n"
    "MineN %.1f\n"
    "MineA %.1f\n"
    "Player %.2f\n"
    "Grav %.2f\n"
    "Matter %.2f\n"
    "Render %.2f";

typedef struct DebugOverlayState {
    uint32_t panel_id;
    float sample_time;
    uint32_t sample_frames;
    float update_ms_sum;
    float mining_ms_sum;
    float mining_nodes_sum;
    float mining_area_sum;
    float player_ms_sum;
    float gravity_ms_sum;
    float matter_ms_sum;
    float render_ms_sum;
    bool initialized;
} DebugOverlayState;

static DebugOverlayState g_debug_overlay = {
    .panel_id = UI_INVALID_ID,
    .sample_time = 0.0f,
    .sample_frames = 0,
    .update_ms_sum = 0.0f,
    .mining_ms_sum = 0.0f,
    .mining_nodes_sum = 0.0f,
    .mining_area_sum = 0.0f,
    .player_ms_sum = 0.0f,
    .gravity_ms_sum = 0.0f,
    .matter_ms_sum = 0.0f,
    .render_ms_sum = 0.0f,
    .initialized = false
};

static void DebugOverlay_ResetSamples(void) {
    g_debug_overlay.sample_time = 0.0f;
    g_debug_overlay.sample_frames = 0;
    g_debug_overlay.update_ms_sum = 0.0f;
    g_debug_overlay.mining_ms_sum = 0.0f;
    g_debug_overlay.mining_nodes_sum = 0.0f;
    g_debug_overlay.mining_area_sum = 0.0f;
    g_debug_overlay.player_ms_sum = 0.0f;
    g_debug_overlay.gravity_ms_sum = 0.0f;
    g_debug_overlay.matter_ms_sum = 0.0f;
    g_debug_overlay.render_ms_sum = 0.0f;
}

void DebugOverlay_Init(EngineContext* ctx) {
    if (g_debug_overlay.initialized) {
        return;
    }

    g_debug_overlay.panel_id = UI_CreatePanel(&ctx->ui, 4, 4, 128);
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
    UI_SetPanelText(
        ctx,
        g_debug_overlay.panel_id,
        DEBUG_OVERLAY_PLACEHOLDER
    );

    DebugOverlay_ResetSamples();
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
    g_debug_overlay.update_ms_sum += ctx->profile.update_ms;
    g_debug_overlay.mining_ms_sum += ctx->profile.mining_ms;
    g_debug_overlay.mining_nodes_sum += (float)ctx->profile.mining_nodes;
    g_debug_overlay.mining_area_sum += ctx->profile.mining_area;
    g_debug_overlay.player_ms_sum += ctx->profile.player_ms;
    g_debug_overlay.gravity_ms_sum += ctx->profile.gravity_ms;
    g_debug_overlay.matter_ms_sum += ctx->profile.matter_ms;
    g_debug_overlay.render_ms_sum += ctx->profile.render_ms;

    if (g_debug_overlay.sample_time < DEBUG_OVERLAY_UPDATE_INTERVAL) {
        return;
    }

    float fps = 0.0f;
    float ms = 0.0f;
    if (g_debug_overlay.sample_time > 0.0f && g_debug_overlay.sample_frames > 0) {
        fps = (float)g_debug_overlay.sample_frames / g_debug_overlay.sample_time;
        ms = (g_debug_overlay.sample_time * 1000.0f) / (float)g_debug_overlay.sample_frames;
    }

    float inv_frames = g_debug_overlay.sample_frames > 0 ?
        1.0f / (float)g_debug_overlay.sample_frames :
        0.0f;

    char text[320];
    snprintf(
        text,
        sizeof(text),
        DEBUG_OVERLAY_FORMAT,
        fps,
        ms,
        ctx->matter.gpu_node_count,
        ctx->renderer.visibleMatterNodeCount,
        ctx->matter.constraint_count,
        ctx->matter.bend_constraint_count,
        ctx->matter.island_count,
        g_debug_overlay.update_ms_sum * inv_frames,
        g_debug_overlay.mining_ms_sum * inv_frames,
        g_debug_overlay.mining_nodes_sum * inv_frames,
        g_debug_overlay.mining_area_sum * inv_frames,
        g_debug_overlay.player_ms_sum * inv_frames,
        g_debug_overlay.gravity_ms_sum * inv_frames,
        g_debug_overlay.matter_ms_sum * inv_frames,
        g_debug_overlay.render_ms_sum * inv_frames
    );
    UI_SetPanelText(ctx, g_debug_overlay.panel_id, text);

    DebugOverlay_ResetSamples();
}

void DebugOverlay_Shutdown(EngineContext* ctx) {
    if (g_debug_overlay.panel_id != UI_INVALID_ID) {
        UI_DestroyPanel(&ctx->ui, g_debug_overlay.panel_id);
    }

    g_debug_overlay.panel_id = UI_INVALID_ID;
    DebugOverlay_ResetSamples();
    g_debug_overlay.initialized = false;
}
