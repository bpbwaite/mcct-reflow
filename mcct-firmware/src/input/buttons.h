/*
 * Button events from raw samples (pure). Call buttons_poll() every
 * CFG_BUTTON_POLL_MS with the raw bits from the display scan.
 *
 * - Debounce: a button must read the same for CFG_DEBOUNCE_MS.
 * - UP / DOWN: an event on press, then auto-repeat after CFG_REPEAT_DELAY_MS
 *   that speeds up the longer the button is held.
 * - ENTER: BTN_ENTER on release of a short press, BTN_ENTER_LONG once when
 *   held for CFG_LONG_PRESS_MS (the "set" action: menu / back).
 */
#ifndef INPUT_BUTTONS_H
#define INPUT_BUTTONS_H

#include "common/types.h"

typedef enum {
    BTN_NONE = 0,
    BTN_UP,
    BTN_DOWN,
    BTN_ENTER,
    BTN_ENTER_LONG
} btn_event_t;

void buttons_init(void);
btn_event_t buttons_poll(uint8_t raw);

/* Queue an event as if a button had been pressed (debug console / tests). */
void buttons_inject(btn_event_t ev);

#endif
