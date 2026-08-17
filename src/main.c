/*
 * Retro-Go SD template — minimal skeleton for a CORE or a GWHB homebrew.
 *
 * Select the kind at build time:
 *   make PROJECT_KIND=core      (default)  → ROM loader + cheat hooks + footer logos
 *   make PROJECT_KIND=homebrew             → no ROM load; ACTIVE_FILE is this .bin
 *
 * Shows path/size (cores) or the GWHB name (homebrews) and which buttons are held.
 * Holding a gameplay button plays a square-wave beep (audio path demo).
 * Save/load/screenshot hooks are stubs — fill them when you plug in real logic.
 *
 * Also demonstrates:
 *   - Pause-menu game options via odroid_dialog_choice_t
 *   - Per-core string tables with gw_i18n() (firmware language)
 *   - Cheat Codes entry (cores only: pack --cheat-ext + cheat_update_cb)
 *   - Optional shutdown / sleep-wake / SRAM-save hooks
 *
 * Entry (run_dynamic_core / run_gwhb_homebrew):
 *   void app_main(uint8_t load_state, uint8_t start_paused, int8_t save_slot)
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include "common.h"
#include "gw_lcd.h"
#include "gw_audio.h"
#include "rom_manager.h"
#include "odroid_system.h"
#include "odroid_overlay.h"
#include "odroid_settings.h"
#include "gw_malloc.h"

#ifndef HOST_BUILD
#include "gw_core_bridge.h"
#include "gw_core_i18n.h"
#else
#include "host_compat.h"
#include "gw_core_i18n.h"
#endif

#if defined(PROJECT_KIND_HOMEBREW)
/* Matches firmware APPID_HOMEBREW — keeps savestate paths under homebrew. */
#define APP_ID  14
#elif defined(PROJECT_KIND_CORE)
#define APP_ID  100
#else
#error "Build with PROJECT_KIND=core or PROJECT_KIND=homebrew"
#endif

#define FPS          60
#define SAMPLE_RATE  16000
#define AUDIO_LENGTH (SAMPLE_RATE / FPS)

#ifndef MAX_CHEAT_CODES
#define MAX_CHEAT_CODES 13
#endif

#if defined(PROJECT_KIND_CORE)
static const uint8_t *rom_data;
static uint32_t rom_size;
static bool rom_in_ram;
static int cheats_on; /* count of enabled cheat slots */
#endif

static uint32_t frame;
static odroid_gamepad_state_t pad; /* last read — used by blit() / audio */
static uint32_t audio_phase;       /* 16.16 phase for square-wave demo */

/* Pause-menu demo option (persisted per app id via settings). */
static int beep_enabled = 1;
static char beep_value[8];

static void blit(void);

/* --- i18n tables (English required; others optional) ---------------------- */

static const gw_i18n_entry_t i18n_title[] = {
#if defined(PROJECT_KIND_HOMEBREW)
    { "en", "Example homebrew (GWHB)" },
    { "fr", "Homebrew exemple (GWHB)" },
    { "es", "Homebrew de ejemplo (GWHB)" },
    { "de", "Beispiel-Homebrew (GWHB)" },
#else
    { "en", "Example core" },
    { "fr", "Core exemple" },
    { "es", "Núcleo de ejemplo" },
    { "de", "Beispiel-Core" },
#endif
    GW_I18N_END
};

static const gw_i18n_entry_t i18n_hold_beep[] = {
    { "en", "Hold a button for a beep:" },
    { "fr", "Maintenir un bouton pour un bip :" },
    { "es", "Mantén un botón para un pitido:" },
    { "de", "Taste halten für Piepton:" },
    GW_I18N_END
};

static const gw_i18n_entry_t i18n_beep[] = {
    { "en", "Button beep" },
    { "fr", "Bip boutons" },
    { "es", "Pitido" },
    { "de", "Tasten-Piep" },
    GW_I18N_END
};

static const gw_i18n_entry_t i18n_on[] = {
    { "en", "ON" },
    { "fr", "OUI" },
    { "es", "SÍ" },
    { "de", "AN" },
    GW_I18N_END
};

static const gw_i18n_entry_t i18n_off[] = {
    { "en", "OFF" },
    { "fr", "NON" },
    { "es", "NO" },
    { "de", "AUS" },
    GW_I18N_END
};

#if defined(PROJECT_KIND_CORE) && CHEAT_CODES == 1
static const gw_i18n_entry_t i18n_cheats[] = {
    { "en", "Cheats on" },
    { "fr", "Cheats actifs" },
    { "es", "Trampas activas" },
    { "de", "Cheats an" },
    GW_I18N_END
};
#endif

static void beep_value_sync(void)
{
    strncpy(beep_value,
            beep_enabled ? gw_i18n(i18n_on) : gw_i18n(i18n_off),
            sizeof(beep_value) - 1);
    beep_value[sizeof(beep_value) - 1] = '\0';
}

static bool beep_update_cb(odroid_dialog_choice_t *option,
                           odroid_dialog_event_t event, uint32_t repeat)
{
    (void)repeat;
    if (event == ODROID_DIALOG_PREV || event == ODROID_DIALOG_NEXT) {
        beep_enabled = !beep_enabled;
        odroid_settings_app_int32_set("beep", beep_enabled);
    }
    beep_value_sync();
    strcpy(option->value, beep_value);
    return event == ODROID_DIALOG_ENTER;
}

/* Hz per button — first match wins (A over B over D-pad…). */
static uint16_t tone_hz(void)
{
    if (!beep_enabled)
        return 0;
    if (pad.values[ODROID_INPUT_A])     return 440;  /* A4 */
    if (pad.values[ODROID_INPUT_B])     return 523;  /* C5 */
#if defined(PROJECT_KIND_CORE)
    if (pad.values[ODROID_INPUT_X])     return 659;  /* E5  (START) */
    if (pad.values[ODROID_INPUT_Y])     return 784;  /* G5  (SELECT) */
#endif
    if (pad.values[ODROID_INPUT_UP])    return 330;
    if (pad.values[ODROID_INPUT_DOWN])  return 294;
    if (pad.values[ODROID_INPUT_LEFT])  return 262;
    if (pad.values[ODROID_INPUT_RIGHT]) return 349;
    return 0;
}

#if defined(PROJECT_KIND_CORE)
/* --- ROM: RAM if it fits, else QSPI flash (same policy as other cores) --- */

static bool load_rom(void)
{
    uint32_t size;
    uint8_t *dest;

    if (!ACTIVE_FILE || !ACTIVE_FILE->path[0]) {
        printf("example: no ACTIVE_FILE\n");
        return false;
    }

    size = ACTIVE_FILE->size;

    if (size > 0 && size <= ram_get_free_size()) {
        dest = ram_malloc(size);
        if (!dest)
            return false;
        if (odroid_overlay_cache_file_in_ram(ACTIVE_FILE->path, dest) != size)
            return false;
        rom_data = dest;
        rom_in_ram = true;
    } else {
        dest = odroid_overlay_cache_file_in_flash(ACTIVE_FILE->path, &size, false);
        if (!dest || size == 0)
            return false;
        rom_data = dest;
        rom_in_ram = false;
    }

    rom_size = size;
    printf("example: ROM %lu bytes in %s @ %p\n",
           (unsigned long)size, rom_in_ram ? "RAM" : "FLASH", (void *)dest);
    return true;
}
#endif

/* --- System callbacks ----------------------------------------------------- */

static bool LoadState(const char *savePathName)
{
    (void)savePathName;
    /* TODO: fopen(savePathName, "rb"), read your snapshot, apply it.
     * Return true on success so the pause menu can confirm the load. */
    return false;
}

static bool SaveState(const char *savePathName)
{
    (void)savePathName;
    /* TODO: serialize state into a buffer, fwrite to savePathName.
     * Return true on success. */
    return false;
}

static void *Screenshot(void)
{
    /* TODO: wait for vblank, redraw one clean frame into the active LCD
     * buffer (no HUD if you prefer), then return lcd_get_active_buffer().
     * The firmware copies that RGB565 bitmap to the screenshot file. */
    lcd_wait_for_vblank();
    blit();
    return lcd_get_active_buffer();
}

static void Shutdown(void)
{
    /* Called on power-off from the pause menu. Flush config / open files. */
    odroid_settings_app_int32_set("beep", beep_enabled);
}

static void SleepWake(void)
{
    /* After deep sleep the firmware restores clocks; re-arm SAI/DMA at the
     * sample rate so audio does not stay silent or at the wrong pitch. */
    odroid_audio_init(SAMPLE_RATE);
    audio_clear_buffers();
    audio_start_playing(AUDIO_LENGTH);
}

static void SramSave(void)
{
    /* TODO (cores): write battery-backed cart RAM (ODROID_PATH_SAVE_SRAM)
     * when dirty. Called on pause / power paths. Homebrews may ignore. */
}

#if defined(PROJECT_KIND_CORE) && CHEAT_CODES == 1
/* Re-apply enabled codes whenever the user confirms the Cheats submenu.
 * Real cores parse ACTIVE_FILE->cheat_codes[i] into the emulator; here we
 * only count how many slots are on so the HUD can show it. */
static void update_cheats(void)
{
    int n = 0;

    if (ACTIVE_FILE) {
        int i;
        for (i = 0; i < MAX_CHEAT_CODES && i < ACTIVE_FILE->cheat_count; i++) {
            if (odroid_settings_ActiveGameGenieCodes_is_enabled(ACTIVE_FILE->path, i))
                n++;
        }
    }
    cheats_on = n;
    printf("example: %d cheat slot(s) enabled\n", n);
}
#endif

/* --- Input: after common_emu_input_loop (MENU/VOLUME stay with firmware) -- */

static void input_read(const odroid_gamepad_state_t *joy)
{
    /* TODO: map joy->values[ODROID_INPUT_*] into your console's joypad
     * register (see firmware cores for typical patterns). */
    pad = *joy;
}

/* --- Video ---------------------------------------------------------------- */

static void blit(void)
{
    uint16_t *fb = lcd_get_active_buffer();
    char line[80];
    int y = 8;

    memset(fb, 0, WIDTH * HEIGHT * sizeof(uint16_t));

    odroid_overlay_draw_text(8, y, 0, gw_i18n(i18n_title), 0xFFFF, 0x0000);
    y += 20;

    {
        const char *name = (ACTIVE_FILE && ACTIVE_FILE->name[0])
                               ? ACTIVE_FILE->name
#if defined(PROJECT_KIND_HOMEBREW)
                               : "(no file)";
#else
                               : "(no rom)";
#endif
        snprintf(line, sizeof(line), "%.70s", name);
        odroid_overlay_draw_text(8, y, 0, line, 0xFFFF, 0x0000);
        y += 16;
    }

#if defined(PROJECT_KIND_CORE)
    snprintf(line, sizeof(line), "%lu bytes  %s",
             (unsigned long)rom_size, rom_in_ram ? "RAM" : "FLASH");
    odroid_overlay_draw_text(8, y, 0, line, 0xFFFF, 0x0000);
    y += 16;
#endif

    snprintf(line, sizeof(line), "frame %lu", (unsigned long)frame);
    odroid_overlay_draw_text(8, y, 0, line, 0xFFFF, 0x0000);
    y += 16;

#if defined(PROJECT_KIND_CORE) && CHEAT_CODES == 1
    snprintf(line, sizeof(line), "%s: %d", gw_i18n(i18n_cheats), cheats_on);
    odroid_overlay_draw_text(8, y, 0, line, 0xFFFF, 0x0000);
    y += 16;
#endif

    y += 8;
    odroid_overlay_draw_text(8, y, 0, gw_i18n(i18n_hold_beep), 0xFFFF, 0x0000);
    y += 16;

    /* One line listing every currently pressed gameplay button. */
    line[0] = '\0';
    if (pad.values[ODROID_INPUT_UP])     strcat(line, "UP ");
    if (pad.values[ODROID_INPUT_DOWN])   strcat(line, "DOWN ");
    if (pad.values[ODROID_INPUT_LEFT])   strcat(line, "LEFT ");
    if (pad.values[ODROID_INPUT_RIGHT])  strcat(line, "RIGHT ");
    if (pad.values[ODROID_INPUT_A])      strcat(line, "A ");
    if (pad.values[ODROID_INPUT_B])      strcat(line, "B ");
#if defined(PROJECT_KIND_CORE)
    if (pad.values[ODROID_INPUT_X])      strcat(line, "START ");
    if (pad.values[ODROID_INPUT_Y])      strcat(line, "SELECT ");
    if (pad.values[ODROID_INPUT_START])  strcat(line, "GAME ");
    if (pad.values[ODROID_INPUT_SELECT]) strcat(line, "TIME ");
#endif
    if (line[0] == '\0')
        strcpy(line, "(none)");
    odroid_overlay_draw_text(8, y, 0, line, 0xFFFF, 0x0000);

    /* Volume/brightness/turbo/… HUD drawn by the firmware — must run after
     * painting, or a full-framebuffer clear hides it and PAUSE+UP/DOWN
     * look broken. */
    common_ingame_overlay();
}

/* Fill one DMA half-buffer: silence, or a mono square wave at tone_hz.
 * Real cores write emulator PCM here the same way. */
static void submit_audio(void)
{
    int16_t *buf;
    uint16_t len;
    uint16_t hz;
    uint32_t step;
    int32_t vol;
    uint16_t i;

    if (common_emu_sound_loop_is_muted())
        return;

    buf = audio_get_active_buffer();
    len = audio_get_buffer_length();
    if (!buf || !len)
        return;

    hz = tone_hz();
    if (hz == 0) {
        memset(buf, 0, len * sizeof(int16_t));
        return;
    }

    /* 16.16 fixed phase so the wave continues cleanly across frames. */
    step = ((uint32_t)hz << 16) / SAMPLE_RATE;
    vol = common_emu_sound_get_volume(); /* 0..255 from the volume menu */

    for (i = 0; i < len; i++) {
        int16_t sample = (audio_phase & 0x8000u) ? 8000 : -8000;
        buf[i] = (int16_t)((sample * vol) / 255);
        audio_phase = (audio_phase + step) & 0xffffu;
    }
}

/* --- Main ----------------------------------------------------------------- */

void app_main(uint8_t load_state, uint8_t start_paused, int8_t save_slot)
{
    odroid_gamepad_state_t joystick;
    odroid_dialog_choice_t options[2];

    gw_core_bridge_init();
    memset(&pad, 0, sizeof(pad));
    audio_phase = 0;
#if defined(PROJECT_KIND_CORE)
    cheats_on = 0;
#endif

    if (start_paused) {
        common_emu_state.pause_after_frames = 2;
        odroid_audio_mute(true);
    } else {
        common_emu_state.pause_after_frames = 0;
    }
    common_emu_state.frame_time_10us = (uint16_t)(100000 / FPS + 0.5f);
    lcd_set_refresh_rate(FPS);

    odroid_system_init(APP_ID, SAMPLE_RATE);
    odroid_system_emu_init(&LoadState, &SaveState, &Screenshot,
                           &Shutdown, &SleepWake, &SramSave,
#if defined(PROJECT_KIND_CORE) && CHEAT_CODES == 1
                           &update_cheats
#else
                           NULL
#endif
                           );

    /* App-scoped settings need odroid_system_init (sets current app id). */
    beep_enabled = odroid_settings_app_int32_get("beep", 1) ? 1 : 0;
    beep_value_sync();

    /* Game options appear under the pause menu. Labels are looked up once
     * at start — reopen the menu after a language change to refresh. */
    options[0].id = 100;
    options[0].label = gw_i18n(i18n_beep);
    options[0].value = beep_value;
    options[0].enabled = 1;
    options[0].update_cb = &beep_update_cb;
    options[1] = (odroid_dialog_choice_t)ODROID_DIALOG_CHOICE_LAST;

    audio_start_playing(AUDIO_LENGTH);

#if defined(PROJECT_KIND_CORE)
    if (!load_rom()) {
        rom_data = NULL;
        rom_size = 0;
    }

#if CHEAT_CODES == 1
    /* Apply any slots already enabled for this ROM (resume / prior session). */
    update_cheats();
#endif
#endif

    if (load_state) {
        /* When LoadState is implemented, this applies the chosen slot. */
        odroid_system_emu_load_state(save_slot);
    } else {
        lcd_clear_buffers();
    }

    while (1) {
        wdog_refresh();

        bool draw_frame = common_emu_frame_loop();

        odroid_input_read_gamepad(&joystick);
        common_emu_input_loop(&joystick, options, &blit);
        common_emu_input_loop_handle_turbo(&joystick);

        input_read(&joystick);

        frame++;

        if (draw_frame) {
            blit();
            lcd_swap();
        }

        submit_audio();
        common_emu_sound_sync(false);
    }
}
