#include "storage/settings.h"
#include "storage/flash.h"
#include "control/profile.h"
#include "common/config.h"
#include "common/fixed.h"
#include "board/tick.h"

#define RECORD_SIZE     16u
#define PAYLOAD_SIZE    (RECORD_SIZE - 2u)
#define PAGES           2u
#define SLOTS_PER_PAGE  (FLASH_PAGE_SIZE / RECORD_SIZE)
#define SLOTS           (PAGES * SLOTS_PER_PAGE)
#define SEQ_BLANK       0xFF            /* never used as a sequence number */

_Static_assert(sizeof(settings_t) == PAYLOAD_SIZE, "settings_t must fill a record");

__xdata settings_t settings;

static __xdata uint8_t record[RECORD_SIZE];
static __xdata struct {
    uint8_t has_record;
    uint8_t slot;                       /* newest record */
    uint8_t seq;
    uint8_t dirty;
    ms_t    changed_at;
} store;

void settings_defaults(void)
{
    settings.target_c = CFG_TARGET_DEFAULT_C;
    settings.offset_c = 0;
    settings.profile = 0;
    settings.kp_x10 = CFG_PID_KP_X10_DEFAULT;
    settings.ti_s = CFG_PID_TI_S_DEFAULT;
    settings.td_s = CFG_PID_TD_S_DEFAULT;
    settings.loss_x1000 = CFG_MODEL_LOSS_X1000_DEFAULT;
    settings.cap_x10 = CFG_MODEL_CAP_X10_DEFAULT;
}

/* A valid record can still hold values outside the current configuration
 * (e.g. after narrowing the target range), so bring those back in range.
 * PID gains are limited by the PID itself. */
static void sanitize(void)
{
    settings.target_c = fx_clamp16(settings.target_c, CFG_TARGET_MIN_C, CFG_TARGET_MAX_C);
    settings.offset_c = (int8_t)fx_clamp16(settings.offset_c, -CFG_OFFSET_LIMIT_C, CFG_OFFSET_LIMIT_C);
    if (settings.profile >= profile_count)
        settings.profile = 0;
}

static uint16_t slot_addr(uint8_t slot)
{
    return SETTINGS_FLASH_BASE + (uint16_t)slot * RECORD_SIZE;
}

static uint8_t record_crc(void)
{
    return crc8(SETTINGS_VERSION, record, RECORD_SIZE - 1);
}

/* Load a slot into record[]; true if it holds a valid record. */
static bool read_slot(uint8_t slot)
{
    uint16_t addr = slot_addr(slot);
    uint8_t i;
    for (i = 0; i < RECORD_SIZE; i++)
        record[i] = flash_read(addr + i);
    return record[0] != SEQ_BLANK && record[RECORD_SIZE - 1] == record_crc();
}

static bool slot_blank(uint8_t slot)
{
    uint16_t addr = slot_addr(slot);
    uint8_t i;
    for (i = 0; i < RECORD_SIZE; i++)
        if (flash_read(addr + i) != 0xFF)
            return false;
    return true;
}

static void copy_payload_out(void)
{
    uint8_t *dst = (uint8_t *)&settings;
    uint8_t i;
    for (i = 0; i < PAYLOAD_SIZE; i++)
        dst[i] = record[1 + i];
}

void settings_load(void)
{
    uint8_t slot, best_slot = 0, best_seq = 0;
    bool found = false;

    for (slot = 0; slot < SLOTS; slot++) {
        if (!read_slot(slot))
            continue;
        /* Sequence numbers wrap; "newer" is a positive signed difference. */
        if (!found || (int8_t)(record[0] - best_seq) > 0) {
            found = true;
            best_seq = record[0];
            best_slot = slot;
        }
    }
    store.has_record = found;
    store.dirty = 0;
    if (found) {
        read_slot(best_slot);
        copy_payload_out();
        store.slot = best_slot;
        store.seq = best_seq;
    } else {
        settings_defaults();
    }
    sanitize();
}

void settings_save(void)
{
    const uint8_t *src = (const uint8_t *)&settings;
    uint8_t slot = store.has_record ? (uint8_t)((store.slot + 1) % SLOTS) : 0;
    uint8_t seq = store.has_record ? (uint8_t)(store.seq + 1) : 0;
    uint16_t addr = slot_addr(slot);
    uint8_t i;

    if (seq == SEQ_BLANK)
        seq = 0;
    record[0] = seq;
    for (i = 0; i < PAYLOAD_SIZE; i++)
        record[1 + i] = src[i];
    record[RECORD_SIZE - 1] = record_crc();

    if (slot % SLOTS_PER_PAGE == 0 || !slot_blank(slot))
        flash_erase_page(SETTINGS_FLASH_BASE + (uint16_t)(slot / SLOTS_PER_PAGE) * FLASH_PAGE_SIZE);
    for (i = 0; i < RECORD_SIZE; i++)
        flash_write(addr + i, record[i]);

    store.has_record = 1;
    store.slot = slot;
    store.seq = seq;
    store.dirty = 0;
}

void settings_changed(void)
{
    store.dirty = 1;
    store.changed_at = clock_ms();
}

void settings_service(void)
{
    if (store.dirty && MS_ELAPSED(clock_ms(), store.changed_at) >= CFG_SAVE_DELAY_MS)
        settings_save();
}
