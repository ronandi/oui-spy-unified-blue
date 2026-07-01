#pragma once
// ============================================================================
// Read-only glanceable status display for boards with an LCD (M5 Cardputer ADV).
// Compiled active only when OUISPY_HAS_DISPLAY is defined; a no-op inline on
// every headless board so callers can invoke it unconditionally.
// ============================================================================
#ifdef OUISPY_HAS_DISPLAY
void uiStatusInit(void);
#else
static inline void uiStatusInit(void) {}
#endif
