#ifndef DEBUG_OVERLAY_H
#define DEBUG_OVERLAY_H

typedef struct EngineContext EngineContext;

void DebugOverlay_Init(EngineContext* ctx);
void DebugOverlay_Update(EngineContext* ctx, float dt);
void DebugOverlay_Shutdown(EngineContext* ctx);

#endif // DEBUG_OVERLAY_H
