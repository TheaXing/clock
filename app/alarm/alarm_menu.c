#include "alarm_menu.h"
#include <string.h>

/**
 * 功能：计算指定年月的天数。
 * 参数：year，支持范围内的年份；month，取值为 1～12 的月份。
 * 返回：该月的日历天数。
 */
static unsigned days(unsigned year, unsigned month) {
    static const uint8_t d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return d[month - 1] + (month == 2 && year % 4 == 0);
}
/**
 * 功能：按方向增减数值，并在指定上下界之间循环。
 * 参数：value，当前值；low，下界；high，上界；delta，正数表示递增，否则递减。
 * 返回：调整后的值；原值越界时返回下界。
 */
static unsigned change(unsigned value, unsigned low, unsigned high, int delta) {
    if (value < low || value > high) {
        return low;
    }
    if (delta > 0) {
        return value == high ? low : value + 1;
    }
    return value == low ? high : value - 1;
}
/**
 * 功能：在闹钟年月变化后将日期限制在当月天数内。
 * 参数：e，待修正的闹钟配置。
 * 返回：无。
 */
static void clamp_day(alarm_entry_t *e) {
    unsigned maximum = days(e->year, e->month);
    if (e->day > maximum) {
        e->day = (uint8_t)maximum;
    }
}
/**
 * 功能：清空菜单状态并回到主页面。
 * 参数：menu，待初始化的菜单；为空时不处理。
 * 返回：无。
 */
void alarm_menu_init(alarm_menu_t *menu) {
    if (menu) {
        memset(menu, 0, sizeof(*menu));
    }
}

/**
 * 功能：按当前编辑字段修改闹钟草稿，并同步调整日期和重复模式。
 * 参数：menu，菜单和闹钟草稿；delta，修改方向；now，切换单次模式时采用的当前日期，可为空。
 * 返回：无。
 */
static void edit(alarm_menu_t *menu, int delta, const rtc_date_time_t *now) {
    alarm_entry_t *e = &menu->draft;
    switch (menu->field) {
    case 0:
        e->enabled ^= 1;
        break;
    case 1:
        e->hour = (uint8_t)change(e->hour, 0, 23, delta);
        break;
    case 2:
        e->minute = (uint8_t)change(e->minute, 0, 59, delta);
        break;
    case 3:
        if (e->repeat_mask) {
            e->repeat_mask = 0;
            if (now && now->year >= 2020 && now->year <= 2099 && now->month >= 1 &&
                now->month <= 12) {
                e->year = now->year;
                e->month = now->month;
                e->day = now->day;
                clamp_day(e);
            }
        } else {
            e->repeat_mask = 0x1f;
        }
        break;
    case 4:
        e->year = (uint16_t)change(e->year, 2020, 2099, delta);
        clamp_day(e);
        break;
    case 5:
        e->month = (uint8_t)change(e->month, 1, 12, delta);
        clamp_day(e);
        break;
    case 6:
        e->day = (uint8_t)change(e->day, 1, days(e->year, e->month), delta);
        break;
    case 14: {
        unsigned minutes = e->snooze_seconds / 60U;
        e->snooze_seconds = (uint16_t)(change(minutes, 1, 30, delta) * 60U);
        break;
    }
    case 15: {
        unsigned units = e->ring_seconds / 5U;
        e->ring_seconds = (uint16_t)(change(units, 1, 120, delta) * 5U);
        break;
    }
    default:
        if (menu->field >= 7 && menu->field <= 13) {
            uint8_t mask = (uint8_t)(e->repeat_mask ^ (1U << (menu->field - 7)));
            if (mask) {
                e->repeat_mask = mask;
            }
        }
        break;
    }
}

/**
 * 功能：按当前编辑字段修改时钟草稿，并限制当月日期。
 * 参数：menu，菜单和时钟草稿；delta，修改方向。
 * 返回：无。
 */
static void edit_clock(alarm_menu_t *menu, int delta) {
    rtc_date_time_t *d = &menu->clock_draft;
    switch (menu->field) {
    case 0:
        d->year = (uint16_t)change(d->year, 2020, 2099, delta);
        break;
    case 1:
        d->month = (uint8_t)change(d->month, 1, 12, delta);
        break;
    case 2:
        d->day = (uint8_t)change(d->day, 1, days(d->year, d->month), delta);
        break;
    case 3:
        d->hour = (uint8_t)change(d->hour, 0, 23, delta);
        break;
    case 4:
        d->minute = (uint8_t)change(d->minute, 0, 59, delta);
        break;
    case 5:
        d->second = (uint8_t)change(d->second, 0, 59, delta);
        break;
    default:
        break;
    }
    if (d->day > days(d->year, d->month)) {
        d->day = (uint8_t)days(d->year, d->month);
    }
}

/**
 * 功能：处理菜单按键，优先生成停止或贪睡动作，再处理页面导航与编辑。
 * 参数：menu，菜单状态；input，按键动作；config，当前闹钟配置；now，当前时间，可为空；active，是否存在活动提醒；saving，是否正在保存。
 * 返回：需要服务层执行的动作；无需执行时返回 ALARM_ACTION_NONE。
 */
alarm_action_t alarm_menu_input(alarm_menu_t *menu, alarm_input_t input,
                                const alarm_config_t *config, const rtc_date_time_t *now,
                                bool active, bool saving) {
    if (!menu || !config || menu->selected >= ALARM_COUNT) {
        return ALARM_ACTION_NONE;
    }
    /* 持久化保存期间，物理停止和贪睡操作仍可使用。 */
    if (active) {
        if (input == ALARM_INPUT_CONFIRM || input == ALARM_INPUT_CONFIRM_LONG) {
            return ALARM_ACTION_STOP;
        }
        if (input == ALARM_INPUT_PLUS || input == ALARM_INPUT_PLUS_LONG) {
            return ALARM_ACTION_SNOOZE;
        }
        return ALARM_ACTION_NONE;
    }
    if (saving) {
        return ALARM_ACTION_NONE;
    }
    switch (menu->view) {
    case ALARM_VIEW_MAIN:
        if (input == ALARM_INPUT_PLUS_LONG) {
            return ALARM_ACTION_TEST;
        }
        if (input == ALARM_INPUT_CONFIRM || input == ALARM_INPUT_MINUS ||
            input == ALARM_INPUT_CONFIRM_LONG) {
            menu->view = ALARM_VIEW_LIST;
        }
        break;
    case ALARM_VIEW_LIST:
        if (input == ALARM_INPUT_PLUS) {
            menu->selected = (uint8_t)((menu->selected + 1U) % ALARM_COUNT);
        } else if (input == ALARM_INPUT_CONFIRM) {
            menu->draft = config->entries[menu->selected];
            menu->draft.enabled ^= 1;
            return ALARM_ACTION_SAVE;
        } else if (input == ALARM_INPUT_MINUS) {
            menu->draft = config->entries[menu->selected];
            menu->field = 0;
            menu->view = ALARM_VIEW_EDIT;
        } else if (input == ALARM_INPUT_PLUS_LONG) {
            menu->clock_draft = (rtc_date_time_t){2026, 1, 1, 12, 0, 0, 4};
            if (now && now->year >= 2020 && now->year <= 2099 && now->month >= 1 &&
                now->month <= 12) {
                menu->clock_draft = *now;
            }
            if (menu->clock_draft.day < 1 ||
                menu->clock_draft.day > days(menu->clock_draft.year, menu->clock_draft.month)) {
                menu->clock_draft.day = 1;
            }
            menu->field = 0;
            menu->view = ALARM_VIEW_CLOCK;
        } else if (input == ALARM_INPUT_MINUS_LONG || input == ALARM_INPUT_CONFIRM_LONG) {
            menu->view = ALARM_VIEW_MAIN;
        }
        break;
    case ALARM_VIEW_EDIT:
        if (input == ALARM_INPUT_CONFIRM) {
            menu->field = (uint8_t)((menu->field + 1U) % 16U);
            if (menu->draft.repeat_mask && menu->field == 4) {
                menu->field = 7;
            }
            if (!menu->draft.repeat_mask && menu->field == 7) {
                menu->field = 14;
            }
        } else if (input == ALARM_INPUT_PLUS || input == ALARM_INPUT_MINUS) {
            edit(menu, input == ALARM_INPUT_PLUS ? 1 : -1, now);
        } else if (input == ALARM_INPUT_CONFIRM_LONG) {
            return ALARM_ACTION_SAVE;
        } else if (input == ALARM_INPUT_MINUS_LONG) {
            menu->view = ALARM_VIEW_LIST;
        }
        break;
    case ALARM_VIEW_CLOCK:
        if (input == ALARM_INPUT_CONFIRM) {
            menu->field = (uint8_t)((menu->field + 1U) % 6U);
        } else if (input == ALARM_INPUT_PLUS || input == ALARM_INPUT_MINUS) {
            edit_clock(menu, input == ALARM_INPUT_PLUS ? 1 : -1);
        } else if (input == ALARM_INPUT_CONFIRM_LONG) {
            return ALARM_ACTION_SET_CLOCK;
        } else if (input == ALARM_INPUT_MINUS_LONG) {
            menu->view = ALARM_VIEW_LIST;
        }
        break;
    default:
        alarm_menu_init(menu);
        break;
    }
    return ALARM_ACTION_NONE;
}
/**
 * 功能：根据保存结果决定是否退出编辑并返回闹钟列表。
 * 参数：menu，菜单状态；success，保存成功标志。
 * 返回：无。
 */
void alarm_menu_saved(alarm_menu_t *menu, bool success) {
    if (menu && success) {
        menu->view = ALARM_VIEW_LIST;
    }
}
