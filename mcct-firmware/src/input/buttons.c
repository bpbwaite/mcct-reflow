#include "input/buttons.h"
#include "display/display.h"        /* BUTTON_RAW_* bit names only */
#include "common/config.h"

#define DEBOUNCE_POLLS  (CFG_DEBOUNCE_MS / CFG_BUTTON_POLL_MS)
#define HELD_MAX_MS     60000u

enum { KEY_ENTER, KEY_UP, KEY_DOWN, KEY_SET, KEY_COUNT };

typedef struct {
    uint8_t  integrator;        /* 0 = released .. DEBOUNCE_POLLS = pressed */
    uint8_t  pressed;
#if CFG_ENTER_HOLD_IS_SET
    uint8_t  long_sent;
#endif
    uint16_t held_ms;
    uint16_t next_repeat_ms;
} key_state_t;

static __xdata key_state_t keys[KEY_COUNT];
static __xdata btn_event_t injected;

static const uint8_t __code raw_bits[KEY_COUNT] = {
    BUTTON_RAW_ENTER, BUTTON_RAW_UP, BUTTON_RAW_DOWN, BUTTON_RAW_SET
};

void buttons_init(void)
{
    uint8_t i;
    for (i = 0; i < KEY_COUNT; i++) {
        keys[i].integrator = 0;
        keys[i].pressed = 0;
        keys[i].held_ms = 0;
    }
    injected = BTN_NONE;
}

void buttons_inject(btn_event_t ev)
{
    injected = ev;
}

static uint16_t repeat_interval(uint16_t held_ms)
{
    if (held_ms >= CFG_REPEAT_FAST_AFTER_MS)
        return CFG_REPEAT_FAST_MS;
    if (held_ms >= CFG_REPEAT_MEDIUM_AFTER_MS)
        return CFG_REPEAT_MEDIUM_MS;
    return CFG_REPEAT_SLOW_MS;
}

/* Debounce one key; returns 1 on the press edge, -1 on release, else 0. */
static int8_t debounce(__xdata key_state_t *k, uint8_t down)
{
    if (down) {
        if (k->integrator < DEBOUNCE_POLLS)
            k->integrator++;
    } else if (k->integrator > 0) {
        k->integrator--;
    }
    if (!k->pressed && k->integrator == DEBOUNCE_POLLS) {
        k->pressed = 1;
        k->held_ms = 0;
#if CFG_ENTER_HOLD_IS_SET
        k->long_sent = 0;
#endif
        k->next_repeat_ms = CFG_REPEAT_DELAY_MS;
        return 1;
    }
    if (k->pressed && k->integrator == 0) {
        k->pressed = 0;
        return -1;
    }
    if (k->pressed && k->held_ms < HELD_MAX_MS)
        k->held_ms += CFG_BUTTON_POLL_MS;
    return 0;
}

static btn_event_t poll_enter(int8_t edge)
{
#if CFG_ENTER_HOLD_IS_SET
    __xdata key_state_t *k = &keys[KEY_ENTER];

    if (edge < 0)
        return k->long_sent ? BTN_NONE : BTN_ENTER;
    if (k->pressed && !k->long_sent && k->held_ms >= CFG_LONG_PRESS_MS) {
        k->long_sent = 1;
        return BTN_SET;
    }
    return BTN_NONE;
#else
    return edge < 0 ? BTN_ENTER : BTN_NONE;
#endif
}

static bool poll_repeat(__xdata key_state_t *k, int8_t edge)
{
    if (edge > 0)
        return true;
    if (k->pressed && k->held_ms >= k->next_repeat_ms) {
        k->next_repeat_ms += repeat_interval(k->held_ms);
        return true;
    }
    return false;
}

btn_event_t buttons_poll(uint8_t raw)
{
    int8_t edge[KEY_COUNT];
    btn_event_t ev;
    uint8_t i;

    for (i = 0; i < KEY_COUNT; i++)
        edge[i] = debounce(&keys[i], raw & raw_bits[i]);

    if (injected != BTN_NONE) {
        ev = injected;
        injected = BTN_NONE;
        return ev;
    }
    if (edge[KEY_SET] > 0)
        return BTN_SET;
    ev = poll_enter(edge[KEY_ENTER]);
    if (ev != BTN_NONE)
        return ev;
    if (poll_repeat(&keys[KEY_UP], edge[KEY_UP]))
        return BTN_UP;
    if (poll_repeat(&keys[KEY_DOWN], edge[KEY_DOWN]))
        return BTN_DOWN;
    return BTN_NONE;
}
