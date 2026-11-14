#include "alarm_page.h"
#include "ui.h"
#include <stdio.h>
#include <string.h>

#define PAGE_LINES 15U
#define LINE_LENGTH 29U
#define BACKGROUND 0x0841U
#define FOREGROUND 0xFFFFU
#define HIGHLIGHT 0x041FU
static int previous_mode = -1;
static char previous_lines[PAGE_LINES][LINE_LENGTH];
static uint8_t previous_styles[PAGE_LINES];

/**
 * 功能：使页面缓存失效，要求下一次显示时完整重绘。
 * 参数：无。
 * 返回：无。
 */
void alarm_page_invalidate(void) {
    previous_mode = -1;
}

/**
 * 功能：将指定闹钟编辑字段格式化为一行显示文字。
 * 参数：out，输出文本缓冲区；size，缓冲区容量；e，闹钟配置；field，编辑字段编号。
 * 返回：无。
 */
static void entry_field(char *out, size_t size, const alarm_entry_t *e, unsigned field) {
    static const char *const names[] = {"Enabled",  "Hour",      "Minute",     "Mode",
                                        "Year",     "Month",     "Day",        "Monday",
                                        "Tuesday",  "Wednesday", "Thursday",   "Friday",
                                        "Saturday", "Sunday",    "Snooze min", "Ring sec"};
    if (field >= 16) {
        out[0] = '\0';
        return;
    }
    if (field == 0) {
        snprintf(out, size, "%s: %s", names[field], e->enabled ? "ON" : "OFF");
    } else if (field == 3) {
        snprintf(out, size, "Mode: %s", e->repeat_mask ? "WEEKLY" : "ONCE");
    } else if (field >= 7 && field <= 13) {
        snprintf(out, size, "%s: %s", names[field],
                 e->repeat_mask & (1U << (field - 7)) ? "YES" : "NO");
    } else {
        unsigned value = 0;
        switch (field) {
        case 1:
            value = e->hour;
            break;
        case 2:
            value = e->minute;
            break;
        case 4:
            value = e->year;
            break;
        case 5:
            value = e->month;
            break;
        case 6:
            value = e->day;
            break;
        case 14:
            value = e->snooze_seconds / 60U;
            break;
        case 15:
            value = e->ring_seconds;
            break;
        default:
            break;
        }
        snprintf(out, size, "%s: %u", names[field], value);
    }
}

/**
 * 功能：依据页面模型显示菜单、响铃或贪睡状态，并仅重绘发生变化的行。
 * 参数：m，页面模型；菜单和配置必须有效，调用者持有显示锁。
 * 返回：无。
 */
void alarm_page_render(const alarm_page_model_t *m) {
    if (!m || !m->menu || !m->config) {
        return;
    }
    int mode = (m->ringing_mask || m->test_ringing) ? 4
               : m->snoozed_mask                    ? 6
               : m->test_pending                    ? 5
                                                    : (int)m->menu->view;
    if (mode == ALARM_VIEW_MAIN) {
        alarm_page_invalidate();
        return;
    }
    char lines[PAGE_LINES][LINE_LENGTH] = {{0}};
    uint8_t styles[PAGE_LINES] = {0};
    const alarm_menu_t *menu = m->menu;
    if (m->time_valid) {
        snprintf(lines[1], LINE_LENGTH, "Time %02u:%02u:%02u", (unsigned)m->now.hour,
                 (unsigned)m->now.minute, (unsigned)m->now.second);
    } else {
        snprintf(lines[1], LINE_LENGTH, "Time not set");
    }
    snprintf(lines[2], LINE_LENGTH, "%s",
             m->saving                     ? "Saving..."
             : m->message && m->message[0] ? m->message
             : m->storage_ok               ? "EEPROM ready"
                                           : "EEPROM unavailable");
    if (mode == 4) {
        snprintf(lines[0], LINE_LENGTH, "ALARM RINGING");
        styles[0] = 2;
        if (m->test_ringing) {
            snprintf(lines[4], LINE_LENGTH, "Diagnostic alarm");
        }
        if (m->ringing_mask) {
            size_t used = 0;
            for (unsigned i = 0; i < ALARM_COUNT; ++i) {
                if (m->ringing_mask & (1U << i)) {
                    int n = snprintf(lines[3] + used, LINE_LENGTH - used, "%s%u",
                                     used ? "," : "Alarms: ", i + 1U);
                    if (n < 0 || (size_t)n >= LINE_LENGTH - used) {
                        break;
                    }
                    used += (size_t)n;
                }
            }
        }
        snprintf(lines[5], LINE_LENGTH, "K1: STOP ALL");
        snprintf(lines[6], LINE_LENGTH, "K2: SNOOZE");
        snprintf(lines[8], LINE_LENGTH, "Auto stop is enabled");
    } else if (mode == 6) {
        snprintf(lines[0], LINE_LENGTH, "ALARM SNOOZED");
        size_t used = 0;
        for (unsigned i = 0; i < ALARM_COUNT; ++i) {
            if (m->snoozed_mask & (1U << i)) {
                int n = snprintf(lines[3] + used, LINE_LENGTH - used, "%s%u",
                                 used ? "," : "Alarms: ", i + 1U);
                if (n < 0 || (size_t)n >= LINE_LENGTH - used) {
                    break;
                }
                used += (size_t)n;
            }
        }
        snprintf(lines[5], LINE_LENGTH, "Waiting to ring again");
        snprintf(lines[6], LINE_LENGTH, "K1: CANCEL ALL");
        snprintf(lines[7], LINE_LENGTH, "K2: KEEP SNOOZE");
        if (m->test_pending) {
            snprintf(lines[9], LINE_LENGTH, "Diagnostic also pending");
        }
    } else if (mode == 5) {
        snprintf(lines[0], LINE_LENGTH, "TEST ALARM PENDING");
        snprintf(lines[4], LINE_LENGTH, "Temporary test reminder");
        snprintf(lines[6], LINE_LENGTH, "K1: CANCEL");
        snprintf(lines[7], LINE_LENGTH, "K2: SNOOZE 5 MIN");
    } else if (mode == ALARM_VIEW_LIST) {
        snprintf(lines[0], LINE_LENGTH, "LOCAL ALARMS");
        for (unsigned i = 0; i < ALARM_COUNT; ++i) {
            const alarm_entry_t *e = &m->config->entries[i];
            snprintf(lines[3 + i], LINE_LENGTH, "%u  %-3s %02u:%02u  %s", i + 1U,
                     e->enabled ? "ON" : "OFF", (unsigned)e->hour, (unsigned)e->minute,
                     e->repeat_mask ? "WEEK" : "ONCE");
            styles[3 + i] = (uint8_t)(i == menu->selected);
        }
        snprintf(lines[11], LINE_LENGTH, "K1 TOGGLE  K2 NEXT");
        snprintf(lines[12], LINE_LENGTH, "K3 EDIT  Hold K2 CLOCK");
        snprintf(lines[13], LINE_LENGTH, "Hold K3 BACK");
        if (m->snoozed_mask) {
            snprintf(lines[14], LINE_LENGTH, "Snooze is pending");
        }
    } else if (mode == ALARM_VIEW_EDIT) {
        snprintf(lines[0], LINE_LENGTH, "EDIT ALARM %u", (unsigned)menu->selected + 1U);
        unsigned fields[16], count = 0, position = 0;
        for (unsigned i = 0; i < 16; ++i) {
            if (menu->draft.repeat_mask && i >= 4 && i <= 6) {
                continue;
            }
            if (!menu->draft.repeat_mask && i >= 7 && i <= 13) {
                continue;
            }
            fields[count] = i;
            if (i == menu->field) {
                position = count;
            }
            ++count;
        }
        unsigned start = position >= 5 ? position - 5 : 0;
        for (unsigned row = 0; row < 6 && start + row < count; ++row) {
            unsigned field = fields[start + row];
            entry_field(lines[3 + row], LINE_LENGTH, &menu->draft, field);
            styles[3 + row] = (uint8_t)(field == menu->field);
        }
        snprintf(lines[10], LINE_LENGTH, "K1 NEXT  K2 +  K3 -");
        snprintf(lines[11], LINE_LENGTH, "Hold K1 SAVE");
        snprintf(lines[12], LINE_LENGTH, "Hold K3 CANCEL");
        snprintf(lines[14], LINE_LENGTH, "Changes apply after saving");
    } else if (mode == ALARM_VIEW_CLOCK) {
        snprintf(lines[0], LINE_LENGTH, "SET LOCAL CLOCK");
        snprintf(lines[1], LINE_LENGTH, "Beijing time UTC+8");
        static const char *const labels[] = {"Year", "Month", "Day", "Hour", "Minute", "Second"};
        unsigned values[] = {menu->clock_draft.year,   menu->clock_draft.month,
                             menu->clock_draft.day,    menu->clock_draft.hour,
                             menu->clock_draft.minute, menu->clock_draft.second};
        for (unsigned i = 0; i < 6; ++i) {
            snprintf(lines[3 + i], LINE_LENGTH, "%s: %u", labels[i], values[i]);
            styles[3 + i] = (uint8_t)(menu->field == i);
        }
        snprintf(lines[10], LINE_LENGTH, "K1 NEXT  K2 +  K3 -");
        snprintf(lines[11], LINE_LENGTH, "Hold K1 APPLY");
        snprintf(lines[12], LINE_LENGTH, "Hold K3 CANCEL");
        snprintf(lines[14], LINE_LENGTH, "Network may resync the time");
    }
    bool full = mode != previous_mode;
    if (full) {
        ui_fill_color(0, 0, UI_WIDTH - 1, UI_HEIGHT - 1, BACKGROUND);
    }
    for (unsigned i = 0; i < PAGE_LINES; ++i) {
        if (!full && styles[i] == previous_styles[i] && strcmp(lines[i], previous_lines[i]) == 0) {
            continue;
        }
        uint16_t background = styles[i] == 1 ? HIGHLIGHT : BACKGROUND;
        uint16_t color = styles[i] == 2 ? 0xF800U : FOREGROUND;
        uint16_t y = (uint16_t)(8U + 20U * i);
        ui_fill_color(6, y, 233, (uint16_t)(y + 17U), background);
        ui_write_string(8, y, lines[i], color, background, &font16_maple);
        memcpy(previous_lines[i], lines[i], LINE_LENGTH);
        previous_styles[i] = styles[i];
    }
    previous_mode = mode;
}
