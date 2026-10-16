/* One-level category settings, compiled in the shared menu translation unit.
 * Platform and core-specific settings remain provided by the existing tables. */
#define MENU_SETTING_MAX (GAMECFG_MAX_ITEMS + MAX_DIPSWITCHS + MAX_CHEATS)

enum menu_setting_kind {
    SETTING_GAME,
    SETTING_TOGGLE,
    SETTING_OFFSET,
    SETTING_VIDEO_BACKEND,
    SETTING_VIDEO_OUTPUT,
    SETTING_AUDIO_PROCESSOR,
    SETTING_CACHE_SIZE,
    SETTING_DIP,
    SETTING_CHEAT
};

typedef struct menu_setting_row {
    const char *label;
    int label_id;
    int kind;
    int *value;
    int initial;
    int min_value;
    int max_value;
    int enabled;
    int restart;
    const gamecfg2_t *game;
    dipswitch_t *dip;
    gamecheat_t *cheat;
} menu_setting_row_t;

static void menu_setting_add(menu_setting_row_t *rows, int *count, int kind,
                             int label, int *value, int min, int max)
{
    menu_setting_row_t *row = &rows[(*count)++];
    memset(row, 0, sizeof(*row));
    row->label = TEXT(label);
    row->label_id = label;
    row->kind = kind;
    row->value = value;
    row->initial = value ? *value : 0;
    row->min_value = min;
    row->max_value = max;
    row->enabled = 1;
}

static int menu_setting_value(const menu_setting_row_t *row)
{
    if (row->kind == SETTING_DIP)
        return row->dip->value;
    if (row->kind == SETTING_CHEAT)
        return row->cheat->curr_option;
    if (row->kind == SETTING_VIDEO_BACKEND) {
        int count = video_backend_choice_count();
        for (int i = 0; i < count; ++i)
            if (video_backend_choice_at(i).id == *row->value)
                return i;
        return 0;
    }
    return row->value ? *row->value : 0;
}

static void menu_setting_assign(menu_setting_row_t *row, int value)
{
    if (row->kind == SETTING_DIP)
        row->dip->value = (uint8_t)value;
    else if (row->kind == SETTING_CHEAT)
        row->cheat->curr_option = value;
    else if (row->kind == SETTING_VIDEO_BACKEND)
        *row->value = video_backend_choice_at(value).id;
    else if (row->value)
        *row->value = value;
}

static const char *menu_setting_text(const menu_setting_row_t *row,
                                     char *buffer, size_t size)
{
    static const int modes[] = {
        VIDEO_OUTPUT_240P_LABEL, VIDEO_OUTPUT_480I_LABEL, VIDEO_OUTPUT_480P_LABEL
    };
#ifdef PSP_ME_AUDIO
    static const int processors[] = {
        AUDIO_PROCESSOR_AUTO_LABEL, AUDIO_PROCESSOR_MAIN_CPU_LABEL,
        AUDIO_PROCESSOR_MEDIA_ENGINE_LABEL
    };
#endif
    int value = menu_setting_value(row);
    switch (row->kind) {
    case SETTING_GAME:
        return TEXT(row->game->values_label[value]);
    case SETTING_TOGGLE:
        return TEXT(value ? ON : OFF);
    case SETTING_OFFSET:
        snprintf(buffer, size, "%+d px", value);
        return buffer;
    case SETTING_VIDEO_BACKEND:
        return video_backend_choice_at(value).name;
    case SETTING_VIDEO_OUTPUT:
        return TEXT(modes[value]);
    case SETTING_AUDIO_PROCESSOR:
#ifdef PSP_ME_AUDIO
        return TEXT(processors[value]);
#else
        return "";
#endif
    case SETTING_CACHE_SIZE:
#if USE_CACHE
        if (value == CACHE_READ_SIZE_AUTO)
            snprintf(buffer, size, TEXT(CACHE_READ_SIZE_AUTO_LABEL),
                     (unsigned int)(cache_resolved_read_size() / 1024));
        else
            snprintf(buffer, size, "%u KiB", 16u << (value - CACHE_READ_SIZE_16K));
        return buffer;
#else
        return "";
#endif
    case SETTING_DIP:
        return row->dip->values_label[value];
    case SETTING_CHEAT:
        if (value < row->min_value || value > row->max_value ||
            !row->cheat->cheat_option[value])
            return "";
        return row->cheat->cheat_option[value]->label;
    }
    return "";
}

static int menu_settings_page(int section, int title, int icon)
{
    /* Only Emulation can contain the potentially large DIP and cheat lists. */
    const int capacity = section == MENU_CFG_EMULATION ?
        MENU_SETTING_MAX : GAMECFG_MAX_ITEMS + 8;
    int selected = 0, top = 0, update = 1;
    int rows_visible = ui_layout_visible_rows(40, 17);
    int count = 0;
    int restart_needed = 0;
    int output_x_before = option_video_offset_x;
    int output_y_before = option_video_offset_y;
    int fps_x_before = option_fps_offset_x;
    int fps_y_before = option_fps_offset_y;
    int backend_before = option_video_backend;
    int processor_before = option_audio_processor;
    int cache_before = option_cache_read_size;
    int output_mode_before = option_video_output_mode;
    gamecfg_t gamecfg[GAMECFG_MAX_ITEMS];
    gamecfg2_t *gamecfg2 = NULL;
    menu_setting_row_t *items = calloc((size_t)capacity, sizeof(*items));
#if (EMU_SYSTEM == CPS1 || EMU_SYSTEM == MVS)
    dipswitch_t *dipswitch = NULL;
    int dip_count = 0;
#if (EMU_SYSTEM == CPS1)
    int sx = ui_layout_center_x();
    int old_dip_a = cps1_dipswitch[DIP_A] & 0xff;
    int old_dip_b = cps1_dipswitch[DIP_B] & 0xff;
    int old_dip_c = cps1_dipswitch[DIP_C] & 0xff;
#else
    int old_dip = neogeo_dipswitch & 0xff;
    int old_harddip = neogeo_hard_dipsw;
#endif
#endif

    if (!items) {
        ui_popup(TEXT(COULD_NOT_ALLOCATE_MEMORY));
        return 0;
    }
    memset(gamecfg, 0, sizeof(gamecfg));
    for (int i = 0; i < GAMECFG_MAX_ITEMS; ++i)
        gamecfg[i].enable = 1;

#define INCLUDE_GAMECFG_MENU
#if (EMU_SYSTEM == CPS1 || EMU_SYSTEM == CPS2)
#include "common/menu/cps.c"
#elif (EMU_SYSTEM == MVS)
#include "common/menu/mvs.c"
#elif (EMU_SYSTEM == NCDZ)
#include "common/menu/ncdz.c"
#endif
#undef INCLUDE_GAMECFG_MENU

    for (int source = 0; gamecfg2[source].label; ++source) {
        gamecfg2_t *game = &gamecfg2[source];
        menu_setting_row_t *row;
        if (menu_setting_group_for_label(game->label) != section ||
            (game->required_power_capabilities &&
             !power_has_capability(game->required_power_capabilities)))
            continue;
        menu_setting_add(items, &count, SETTING_GAME, game->label,
                         game->value, 0, game->value_max);
        row = &items[count - 1];
        row->game = game;
        row->enabled = gamecfg[source].enable;
        row->restart = game->flag == CFG_RESTART;
        if (row->value && (*row->value < 0 || *row->value > row->max_value))
            *row->value = 0;
        row->initial = row->value ? *row->value : 0;
    }

    if (section == MENU_CFG_VIDEO) {
        menu_setting_add(items, &count, SETTING_OFFSET, FPS_X_OFFSET,
                         &option_fps_offset_x, 0, FPS_OVERLAY_OFFSET_MAX);
        menu_setting_add(items, &count, SETTING_OFFSET, FPS_Y_OFFSET,
                         &option_fps_offset_y, 0, FPS_OVERLAY_OFFSET_MAX);
        if (video_backend_choice_count() > 1) {
            menu_setting_add(items, &count, SETTING_VIDEO_BACKEND, VIDEO_BACKEND,
                             &option_video_backend, 0, video_backend_choice_count() - 1);
        }
#ifdef PS2
        menu_setting_add(items, &count, SETTING_VIDEO_OUTPUT, VIDEO_OUTPUT_MODE,
                         &option_video_output_mode, VIDEO_OUTPUT_240P, VIDEO_OUTPUT_480P);
        menu_setting_add(items, &count, SETTING_OFFSET, VIDEO_X_OFFSET,
                         &option_video_offset_x, -VIDEO_OUTPUT_OFFSET_MAX, VIDEO_OUTPUT_OFFSET_MAX);
        menu_setting_add(items, &count, SETTING_OFFSET, VIDEO_Y_OFFSET,
                         &option_video_offset_y, -VIDEO_OUTPUT_OFFSET_MAX, VIDEO_OUTPUT_OFFSET_MAX);
#endif
    } else if (section == MENU_CFG_AUDIO) {
#ifdef PSP_ME_AUDIO
        menu_setting_add(items, &count, SETTING_AUDIO_PROCESSOR, AUDIO_PROCESSOR,
                         &option_audio_processor, AUDIO_PROCESSOR_AUTO, AUDIO_PROCESSOR_MEDIA_ENGINE);
#endif
    } else if (section == MENU_CFG_PERFORMANCE) {
#if USE_CACHE
        menu_setting_add(items, &count, SETTING_CACHE_SIZE, CACHE_READ_SIZE,
                         &option_cache_read_size, CACHE_READ_SIZE_AUTO, CACHE_READ_SIZE_64K);
#endif
    } else if (section == MENU_CFG_EMULATION) {
#if (EMU_SYSTEM == CPS1)
        dipswitch = load_dipswitch(&sx);
#elif (EMU_SYSTEM == MVS)
        dipswitch = load_dipswitch();
#endif
#if (EMU_SYSTEM == CPS1 || EMU_SYSTEM == MVS)
        if (dipswitch) {
            while (dip_count < MAX_DIPSWITCHS && dipswitch[dip_count].label[0]) {
                dipswitch_t *dip = &dipswitch[dip_count++];
                /* DIP metadata can contain a non-editable return row from the
                 * original standalone menu; the page owns its own return. */
                if (dip->label[0] == '\n' || !dip->mask)
                    continue;
                menu_setting_add(items, &count, SETTING_DIP, DIP_SWITCH_SETTINGS_MENU,
                                 NULL, 0, dip->value_max);
                items[count - 1].label = dip->label;
                items[count - 1].dip = dip;
                items[count - 1].enabled = dip->enable && dip->mask;
            }
        }
#endif
        /* Reading the file only when it exists prevents an intrusive
         * 'no cheats' popup from appearing on every visit to Emulation. */
        if (!cheat_num) {
            char path[PATH_MAX];
            if (path_format(path, sizeof(path), "%scheats/%s.ini", launchDir, game_name) &&
                access(path, F_OK) == 0)
                cheats_load();
        }
        for (int i = 0; i < cheat_num && count < capacity; ++i) {
            gamecheat_t *cheat = gamecheat[i];
            if (!cheat || !cheat->cheat_name || cheat->num_cheat_options <= 0)
                continue;
            menu_setting_add(items, &count, SETTING_CHEAT, CHEAT_CONFIGURATION_MENU,
                             NULL, 0, cheat->num_cheat_options - 1);
            items[count - 1].label = cheat->cheat_name;
            items[count - 1].cheat = cheat;
            if (cheat->curr_option < 0 || cheat->curr_option >= cheat->num_cheat_options ||
                !cheat->cheat_option[cheat->curr_option])
                cheat->curr_option = 0;
        }
    }

    /* Visual grouping requires no extra selectable navigation levels. */
    if (section == MENU_CFG_VIDEO || section == MENU_CFG_AUDIO) {
        for (int i = 1; i < count; ++i) {
            menu_setting_row_t row = items[i];
            int position = i;
            int rank = menu_setting_order(section, row.label_id);
            while (position > 0 &&
                   menu_setting_order(section, items[position - 1].label_id) > rank) {
                items[position] = items[position - 1];
                position--;
            }
            items[position] = row;
        }
    }

    pad_wait_clear();
    load_background(WP_GAMECFG);
    ui_popup_reset();
    do {
        if (section == MENU_CFG_VIDEO) {
            for (int i = 0; i < count; ++i)
                if (items[i].label_id == FRAMESKIP)
                    items[i].enabled = !option_autoframeskip;
        }

        if (update) {
            video_driver->beginFrame(video_data);
            show_background();
            small_icon_shadow(8, 3, UI_COLOR(UI_PAL_TITLE), icon);
            uifont_print_shadow(36, 5, UI_COLOR(UI_PAL_TITLE), TEXT(title));
            draw_scrollbar(ui_layout_right(10), 26, ui_layout_right(0),
                           ui_layout_bottom(1), rows_visible, count + 1, selected);
            for (int i = 0; i < rows_visible && top + i <= count; ++i) {
                int index = top + i;
                int y = 40 + i * 17;
                menu_setting_row_t *row = &items[index];
                int active = selected == index;
                int enabled = index == count || row->enabled;
                char value_buffer[48];
                const char *label = index == count ? TEXT(RETURN_TO_MAIN_MENU) : row->label;
                const char *value = index == count ? "" :
                    menu_setting_text(row, value_buffer, sizeof(value_buffer));
                int current = index == count ? 0 : menu_setting_value(row);
                int left = index != count && enabled && current > row->min_value;
                int right = index != count && enabled && current < row->max_value;
                if (active) {
                    if (enabled)
                        uifont_print_shadow(16, y, UI_COLOR(UI_PAL_SELECT), label);
                    else
                        uifont_print_shadow(16, y, COLOR_GRAY, label);
                    if (left)
                        uifont_print_shadow(190, y, UI_COLOR(UI_PAL_SELECT), FONT_LEFTTRIANGLE);
                    if (index != count) {
                        if (enabled)
                            uifont_print_shadow(210, y, UI_COLOR(UI_PAL_SELECT), value);
                        else
                            uifont_print_shadow(210, y, COLOR_GRAY, value);
                    }
                    if (right)
                        uifont_print_shadow(214 + uifont_get_string_width(value), y,
                                            UI_COLOR(UI_PAL_SELECT), FONT_RIGHTTRIANGLE);
                } else {
                    if (enabled) {
                        uifont_print(16, y, UI_COLOR(UI_PAL_NORMAL), label);
                        if (index != count)
                            uifont_print(210, y, UI_COLOR(UI_PAL_NORMAL), value);
                    } else {
                        uifont_print(16, y, COLOR_DARKGRAY, label);
                        if (index != count)
                            uifont_print(210, y, COLOR_DARKGRAY, value);
                    }
                }
            }
            update = draw_battery_status(1) | ui_show_popup(1);
            video_driver->endFrame(video_data);
            video_driver->flipScreen(video_data, 1);
        } else {
            update = draw_battery_status(0) | ui_show_popup(0);
            video_driver->waitVsync(video_data);
        }
        pad_update();
        int previous = selected;
        if (pad_pressed(PLATFORM_PAD_UP))
            selected = (selected + count) % (count + 1);
        else if (pad_pressed(PLATFORM_PAD_DOWN))
            selected = (selected + 1) % (count + 1);
        else if (pad_pressed(PLATFORM_PAD_L))
            selected = selected > rows_visible ? selected - rows_visible : 0;
        else if (pad_pressed(PLATFORM_PAD_R))
            selected = selected + rows_visible <= count ? selected + rows_visible : count;
        else if (selected == count && pad_pressed(PLATFORM_PAD_B1))
            break;
        else if (pad_pressed(PLATFORM_PAD_SELECT)) {
            help(HELP_GAMECONFIG);
            update = 1;
        }
        else if (selected < count && (pad_pressed(PLATFORM_PAD_LEFT) || pad_pressed(PLATFORM_PAD_RIGHT) ||
                                       (items[selected].kind == SETTING_CHEAT && pad_pressed(PLATFORM_PAD_B1)))) {
            menu_setting_row_t *row = &items[selected];
            int direction = pad_pressed(PLATFORM_PAD_LEFT) ? -1 : 1;
            int current = menu_setting_value(row);
            /* Confirm disables an active cheat, matching the old cheat editor. */
            int next = row->kind == SETTING_CHEAT && pad_pressed(PLATFORM_PAD_B1)
                ? 0 : current + direction;
            if (row->enabled && next >= row->min_value && next <= row->max_value) {
#ifdef PS2
                int old_output_mode = option_video_output_mode;
                int old_display_mode = option_display_mode;
#endif
                menu_setting_assign(row, next);
#ifdef PS2
                if (row->kind == SETTING_VIDEO_OUTPUT ||
                    (row->label_id == DISPLAY_MODE &&
                     menu_requires_native_ps2_scanout_switch(old_display_mode))) {
                    menu_apply_live_video_output_mode(old_output_mode,
                        old_display_mode);
                    pad_wait_clear();
                    rows_visible = ui_layout_visible_rows(40, 17);
                } else if (row->label_id == VIDEO_X_OFFSET || row->label_id == VIDEO_Y_OFFSET) {
                    if (video_driver->setOutputOffset)
                        video_driver->setOutputOffset(video_data, option_video_offset_x, option_video_offset_y);
                }
#endif
                update = 1;
            }
        }
        if (selected != previous)
            update = 1;
        if (selected < top) top = selected;
        if (selected >= top + rows_visible) top = selected - rows_visible + 1;
        if (Loop == LOOP_EXIT) break;
    } while (!pad_pressed(PLATFORM_PAD_B2));

    /* A full emulator restart takes precedence over a DIP-only reset when
     * both kinds of change are made on the same Emulation settings page. */
    for (int i = 0; i < count; ++i) {
        const menu_setting_row_t *row = &items[i];
        if (row->restart && row->value && row->initial != *row->value)
            restart_needed = 1;
    }

#if (EMU_SYSTEM == CPS1 || EMU_SYSTEM == MVS)
    if (section == MENU_CFG_EMULATION && dipswitch) {
        save_dipswitch();
#if (EMU_SYSTEM == CPS1)
        if (old_dip_a != (cps1_dipswitch[DIP_A] & 0xff) ||
            old_dip_b != (cps1_dipswitch[DIP_B] & 0xff) ||
            old_dip_c != (cps1_dipswitch[DIP_C] & 0xff)) {
            if (!restart_needed)
                restart_needed = menu_resetdip();
        }
#else
        if (!restart_needed) {
            if (old_harddip != neogeo_hard_dipsw)
                restart_needed = menu_reset();
            else if (old_dip != (neogeo_dipswitch & 0xff))
                restart_needed = menu_resetdip();
        }
#endif
    }
#endif
    if (fps_x_before != option_fps_offset_x || fps_y_before != option_fps_offset_y ||
        output_x_before != option_video_offset_x || output_y_before != option_video_offset_y ||
        output_mode_before != option_video_output_mode ||
        backend_before != option_video_backend || processor_before != option_audio_processor ||
        cache_before != option_cache_read_size)
        save_settings();
    if (backend_before != option_video_backend || processor_before != option_audio_processor ||
        cache_before != option_cache_read_size)
        restart_needed = 1;

    free(items);
    if (restart_needed && Loop == LOOP_EXEC)
        return menu_restart();
    return Loop != LOOP_EXEC;
}
