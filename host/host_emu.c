/*
 * Firmware API stand-ins for the host SDL build.
 * Enough for src/main.c's frame loop: LCD, pad, audio, pacing, text.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/stat.h>

#include "host_compat.h"
#include "host_platform.h"

#include "gw_lcd.h"
#include "gw_audio.h"
#include "gw_malloc.h"
#include "odroid_system.h"
#include "odroid_overlay.h"
#include "odroid_settings.h"
#include "odroid_input.h"
#include "odroid_audio.h"
#include "common.h"
#include "rom_manager.h"
#include "main.h"

/* --- Globals matching firmware / bridge surface -------------------------- */

uint32_t __CORE_BSS_END__;
uint32_t __CORE_CODE_END__;
uint32_t ram_start;

common_emu_state_t common_emu_state;
uint32_t common_emu_sound_dma_marker;
cpumon_stats_t cpumon_stats;

retro_emulator_file_t host_active_file;
retro_emulator_file_t *ACTIVE_FILE = &host_active_file;

pixel_t *framebuffer1;
pixel_t *framebuffer2;
uint32_t active_framebuffer;
uint32_t frame_counter;

uint32_t audio_mute;
dma_transfer_state_t dma_state;
uint32_t dma_counter;
int16_t audiobuffer_dma[AUDIO_BUFFER_LENGTH * 2];

sdcard_hw_type_t sdcard_hw_type;
RTC_HandleTypeDef hrtc;
OSPI_HandleTypeDef hospi1;
SPI_HandleTypeDef hspi1;
DMA_HandleTypeDef hdma_spi1_tx;
DMA_HandleTypeDef hdma_spi1_rx;
odroid_menu_state_t odroid_menu_state;

static pixel_t fb_storage[2][GW_LCD_WIDTH * GW_LCD_HEIGHT];
static host_pad_t host_pad;
static int16_t audio_half_bufs[2][AUDIO_BUFFER_LENGTH];
static uint16_t audio_half_len;
static int audio_active_half;
static int audio_started;
static int audio_muted;
static uint8_t sound_volume = 255;
static uint32_t lcd_refresh_hz = 60;
static uint32_t frame_start_ms;
static int quit_requested;
static uint8_t *ram_pool;
static size_t ram_pool_size;
static size_t ram_pool_used;
static int32_t settings_beep = 1;
static state_handler_t host_load_state_cb;
static state_handler_t host_save_state_cb;
static const int host_default_slot = 0;

bool odroid_system_emu_load_state(int slot);
bool odroid_system_emu_save_state(int slot);

/* Tiny 8x8 font for ASCII 32..127 (bit0 = left). */
static const uint8_t font8x8_basic[96][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* space */
    {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00},
    {0x36,0x36,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x36,0x36,0x7F,0x36,0x7F,0x36,0x36,0x00},
    {0x0C,0x3E,0x03,0x1E,0x30,0x1F,0x0C,0x00},
    {0x00,0x63,0x33,0x18,0x0C,0x66,0x63,0x00},
    {0x1C,0x36,0x1C,0x6E,0x3B,0x33,0x6E,0x00},
    {0x06,0x06,0x03,0x00,0x00,0x00,0x00,0x00},
    {0x18,0x0C,0x06,0x06,0x06,0x0C,0x18,0x00},
    {0x06,0x0C,0x18,0x18,0x18,0x0C,0x06,0x00},
    {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00},
    {0x00,0x0C,0x0C,0x3F,0x0C,0x0C,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x06},
    {0x00,0x00,0x00,0x3F,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x00},
    {0x60,0x30,0x18,0x0C,0x06,0x03,0x01,0x00},
    {0x3E,0x63,0x73,0x7B,0x6F,0x67,0x3E,0x00},
    {0x0C,0x0E,0x0C,0x0C,0x0C,0x0C,0x3F,0x00},
    {0x1E,0x33,0x30,0x1C,0x06,0x33,0x3F,0x00},
    {0x1E,0x33,0x30,0x1C,0x30,0x33,0x1E,0x00},
    {0x38,0x3C,0x36,0x33,0x7F,0x30,0x78,0x00},
    {0x3F,0x03,0x1F,0x30,0x30,0x33,0x1E,0x00},
    {0x1C,0x06,0x03,0x1F,0x33,0x33,0x1E,0x00},
    {0x3F,0x33,0x30,0x18,0x0C,0x0C,0x0C,0x00},
    {0x1E,0x33,0x33,0x1E,0x33,0x33,0x1E,0x00},
    {0x1E,0x33,0x33,0x3E,0x30,0x18,0x0E,0x00},
    {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x00},
    {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x06},
    {0x18,0x0C,0x06,0x03,0x06,0x0C,0x18,0x00},
    {0x00,0x00,0x3F,0x00,0x00,0x3F,0x00,0x00},
    {0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00},
    {0x1E,0x33,0x30,0x18,0x0C,0x00,0x0C,0x00},
    {0x3E,0x63,0x7B,0x7B,0x7B,0x03,0x1E,0x00},
    {0x0C,0x1E,0x33,0x33,0x3F,0x33,0x33,0x00},
    {0x3F,0x66,0x66,0x3E,0x66,0x66,0x3F,0x00},
    {0x3C,0x66,0x03,0x03,0x03,0x66,0x3C,0x00},
    {0x1F,0x36,0x66,0x66,0x66,0x36,0x1F,0x00},
    {0x7F,0x46,0x16,0x1E,0x16,0x46,0x7F,0x00},
    {0x7F,0x46,0x16,0x1E,0x16,0x06,0x0F,0x00},
    {0x3C,0x66,0x03,0x03,0x73,0x66,0x7C,0x00},
    {0x33,0x33,0x33,0x3F,0x33,0x33,0x33,0x00},
    {0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00},
    {0x78,0x30,0x30,0x30,0x33,0x33,0x1E,0x00},
    {0x67,0x66,0x36,0x1E,0x36,0x66,0x67,0x00},
    {0x0F,0x06,0x06,0x06,0x46,0x66,0x7F,0x00},
    {0x63,0x77,0x7F,0x6B,0x63,0x63,0x63,0x00},
    {0x63,0x67,0x6F,0x7B,0x73,0x63,0x63,0x00},
    {0x1C,0x36,0x63,0x63,0x63,0x36,0x1C,0x00},
    {0x3F,0x66,0x66,0x3E,0x06,0x06,0x0F,0x00},
    {0x1E,0x33,0x33,0x33,0x3B,0x1E,0x38,0x00},
    {0x3F,0x66,0x66,0x3E,0x36,0x66,0x67,0x00},
    {0x1E,0x33,0x07,0x0E,0x38,0x33,0x1E,0x00},
    {0x3F,0x2D,0x0C,0x0C,0x0C,0x0C,0x1E,0x00},
    {0x33,0x33,0x33,0x33,0x33,0x33,0x3F,0x00},
    {0x33,0x33,0x33,0x33,0x33,0x1E,0x0C,0x00},
    {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00},
    {0x63,0x63,0x36,0x1C,0x1C,0x36,0x63,0x00},
    {0x33,0x33,0x33,0x1E,0x0C,0x0C,0x1E,0x00},
    {0x7F,0x63,0x31,0x18,0x4C,0x66,0x7F,0x00},
    {0x1E,0x06,0x06,0x06,0x06,0x06,0x1E,0x00},
    {0x03,0x06,0x0C,0x18,0x30,0x60,0x40,0x00},
    {0x1E,0x18,0x18,0x18,0x18,0x18,0x1E,0x00},
    {0x08,0x1C,0x36,0x63,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF},
    {0x0C,0x0C,0x18,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x1E,0x30,0x3E,0x33,0x6E,0x00},
    {0x07,0x06,0x06,0x3E,0x66,0x66,0x3B,0x00},
    {0x00,0x00,0x1E,0x33,0x03,0x33,0x1E,0x00},
    {0x38,0x30,0x30,0x3e,0x33,0x33,0x6E,0x00},
    {0x00,0x00,0x1E,0x33,0x3f,0x03,0x1E,0x00},
    {0x1C,0x36,0x06,0x0f,0x06,0x06,0x0F,0x00},
    {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x1F},
    {0x07,0x06,0x36,0x6E,0x66,0x66,0x67,0x00},
    {0x0C,0x00,0x0E,0x0C,0x0C,0x0C,0x1E,0x00},
    {0x30,0x00,0x30,0x30,0x30,0x33,0x33,0x1E},
    {0x07,0x06,0x66,0x36,0x1E,0x36,0x67,0x00},
    {0x0E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00},
    {0x00,0x00,0x33,0x7F,0x7F,0x6B,0x63,0x00},
    {0x00,0x00,0x1F,0x33,0x33,0x33,0x33,0x00},
    {0x00,0x00,0x1E,0x33,0x33,0x33,0x1E,0x00},
    {0x00,0x00,0x3B,0x66,0x66,0x3E,0x06,0x0F},
    {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x78},
    {0x00,0x00,0x3B,0x6E,0x66,0x06,0x0F,0x00},
    {0x00,0x00,0x3E,0x03,0x1E,0x30,0x1F,0x00},
    {0x08,0x0C,0x3E,0x0C,0x0C,0x2C,0x18,0x00},
    {0x00,0x00,0x33,0x33,0x33,0x33,0x6E,0x00},
    {0x00,0x00,0x33,0x33,0x33,0x1E,0x0C,0x00},
    {0x00,0x00,0x63,0x6B,0x7F,0x7F,0x36,0x00},
    {0x00,0x00,0x63,0x36,0x1C,0x36,0x63,0x00},
    {0x00,0x00,0x33,0x33,0x33,0x3E,0x30,0x1F},
    {0x00,0x00,0x3F,0x19,0x0C,0x26,0x3F,0x00},
    {0x38,0x0C,0x0C,0x07,0x0C,0x0C,0x38,0x00},
    {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00},
    {0x07,0x0C,0x0C,0x38,0x0C,0x0C,0x07,0x00},
    {0x6E,0x3B,0x00,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
};

void gw_core_bridge_init(void)
{
    framebuffer1 = fb_storage[0];
    framebuffer2 = fb_storage[1];
    active_framebuffer = 0;
    memset(&common_emu_state, 0, sizeof(common_emu_state));
    memset(&host_pad, 0, sizeof(host_pad));
    memset(&host_active_file, 0, sizeof(host_active_file));
#if defined(PROJECT_KIND_HOMEBREW)
    strncpy(host_active_file.name, "ExampleHB.bin", sizeof(host_active_file.name) - 1);
    strncpy(host_active_file.path, "ExampleHB.bin", sizeof(host_active_file.path) - 1);
#else
    strncpy(host_active_file.name, "(no rom)", sizeof(host_active_file.name) - 1);
#endif
    ram_pool_size = 4 * 1024 * 1024;
    ram_pool = (uint8_t *)malloc(ram_pool_size);
    ram_pool_used = 0;
    frame_start_ms = host_platform_ticks_ms();
}

void host_set_rom_path(const char *path)
{
    if (!path || !path[0])
        return;
    memset(&host_active_file, 0, sizeof(host_active_file));
    strncpy(host_active_file.path, path, sizeof(host_active_file.path) - 1);
    {
        const char *base = strrchr(path, '/');
#ifdef _WIN32
        const char *base2 = strrchr(path, '\\');
        if (base2 && (!base || base2 > base))
            base = base2;
#endif
        strncpy(host_active_file.name, base ? base + 1 : path,
                sizeof(host_active_file.name) - 1);
    }
    FILE *f = fopen(path, "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fclose(f);
        if (sz > 0)
            host_active_file.size = (uint32_t)sz;
    }
}

int host_poll_events(void)
{
    if (!host_platform_poll(&host_pad)) {
        quit_requested = 1;
        return 0;
    }
    if (host_pad.want_save) {
        bool ok = odroid_system_emu_save_state(host_default_slot);
        printf("host: F1 save slot %d → %s\n", host_default_slot, ok ? "ok" : "failed");
    }
    if (host_pad.want_load) {
        bool ok = odroid_system_emu_load_state(host_default_slot);
        printf("host: F2 load slot %d → %s\n", host_default_slot, ok ? "ok" : "failed");
    }
    return 1;
}

static void host_maybe_quit(void)
{
    if (quit_requested)
        exit(0);
}

/* --- LCD ------------------------------------------------------------------ */

void lcd_clear_buffers(void)
{
    memset(fb_storage, 0, sizeof(fb_storage));
}

void *lcd_clear_active_buffer(void)
{
    memset(lcd_get_active_buffer(), 0, GW_LCD_FRAME_SIZE);
    return lcd_get_active_buffer();
}

void *lcd_clear_inactive_buffer(void)
{
    memset(lcd_get_inactive_buffer(), 0, GW_LCD_FRAME_SIZE);
    return lcd_get_inactive_buffer();
}

void *lcd_get_active_buffer(void)
{
    return active_framebuffer ? framebuffer2 : framebuffer1;
}

void *lcd_get_inactive_buffer(void)
{
    return active_framebuffer ? framebuffer1 : framebuffer2;
}

void lcd_swap(void)
{
    host_poll_events();
    host_platform_present_rgb565((const uint16_t *)lcd_get_active_buffer(),
                                 GW_LCD_WIDTH, GW_LCD_HEIGHT);
    active_framebuffer ^= 1;
    frame_counter++;
    host_maybe_quit();
}

void lcd_wait_for_vblank(void)
{
    host_platform_delay_ms(1);
}

void lcd_set_refresh_rate(uint32_t frequency)
{
    if (frequency)
        lcd_refresh_hz = frequency;
}

uint32_t lcd_get_last_refresh_rate(void)
{
    return lcd_refresh_hz;
}

void lcd_set_buffers(uint16_t *buf1, uint16_t *buf2)
{
    if (buf1)
        framebuffer1 = (pixel_t *)buf1;
    if (buf2)
        framebuffer2 = (pixel_t *)buf2;
}

/* --- Audio ---------------------------------------------------------------- */

void audio_start_playing(uint16_t length)
{
    if (length == 0 || length > AUDIO_BUFFER_LENGTH)
        length = AUDIO_BUFFER_LENGTH;
    audio_half_len = length;
    audio_active_half = 0;
    audio_started = 1;
    memset(audio_half_bufs, 0, sizeof(audio_half_bufs));
    host_platform_audio_start(odroid_audio_sample_rate_get(), (int)length);
}

void audio_start_playing_full_length(uint16_t length)
{
    audio_start_playing(length);
}

void audio_stop_playing(void)
{
    audio_started = 0;
    host_platform_audio_stop();
}

void audio_set_buffer_length(uint16_t length)
{
    if (length && length <= AUDIO_BUFFER_LENGTH)
        audio_half_len = length;
}

uint16_t audio_get_buffer_length(void)
{
    return audio_half_len ? audio_half_len : (uint16_t)(16000 / 60);
}

uint16_t audio_get_buffer_full_length(void)
{
    return (uint16_t)(audio_get_buffer_length() * 2);
}

uint16_t audio_get_buffer_size(void)
{
    return (uint16_t)(audio_get_buffer_length() * sizeof(int16_t));
}

int16_t *audio_get_active_buffer(void)
{
    return audio_half_bufs[audio_active_half & 1];
}

int16_t *audio_get_inactive_buffer(void)
{
    return audio_half_bufs[(audio_active_half ^ 1) & 1];
}

void audio_clear_active_buffer(void)
{
    memset(audio_get_active_buffer(), 0, audio_get_buffer_size());
}

void audio_clear_inactive_buffer(void)
{
    memset(audio_get_inactive_buffer(), 0, audio_get_buffer_size());
}

void audio_clear_buffers(void)
{
    memset(audio_half_bufs, 0, sizeof(audio_half_bufs));
}

void common_emu_sound_sync(bool use_nops)
{
    (void)use_nops;
    host_poll_events();
    if (audio_started && !audio_muted && !audio_mute) {
        int16_t *buf = audio_get_active_buffer();
        uint16_t len = audio_get_buffer_length();
        host_platform_audio_queue(buf, (int)len);
        /* Pace roughly to one half-buffer. */
        while (host_platform_audio_queued_samples() > (int)len * 3) {
            host_platform_delay_ms(1);
            host_poll_events();
            host_maybe_quit();
        }
    }
    audio_active_half ^= 1;
    dma_counter++;
    common_emu_sound_dma_marker = dma_counter;
    host_maybe_quit();
}

void common_emu_sound_sync_reset(void) {}

bool common_emu_sound_loop_is_muted(void)
{
    return audio_muted || audio_mute != 0;
}

uint8_t common_emu_sound_get_volume(void)
{
    return sound_volume;
}

static int host_sample_rate = 16000;

void odroid_audio_init(int sample_rate)
{
    if (sample_rate > 0)
        host_sample_rate = sample_rate;
}

int odroid_audio_sample_rate_get(void)
{
    return host_sample_rate;
}

void odroid_audio_mute(bool mute)
{
    audio_muted = mute ? 1 : 0;
}

void odroid_audio_terminate(void) {}
void odroid_audio_volume_set(int level) { (void)level; }
int odroid_audio_volume_get(void) { return ODROID_AUDIO_VOLUME_DEFAULT; }
void odroid_audio_set_sink(ODROID_AUDIO_SINK sink) { (void)sink; }
ODROID_AUDIO_SINK odroid_audio_get_sink(void) { return ODROID_AUDIO_SINK_SPEAKER; }
void odroid_audio_submit(short *stereoAudioBuffer, int frameCount)
{
    (void)stereoAudioBuffer;
    (void)frameCount;
}

/* --- Frame pacing --------------------------------------------------------- */

bool common_emu_frame_loop(void)
{
    uint32_t now;
    uint32_t target_ms;

    host_poll_events();
    host_maybe_quit();

    target_ms = common_emu_state.frame_time_10us
                    ? (uint32_t)((common_emu_state.frame_time_10us + 50) / 100)
                    : (1000u / (lcd_refresh_hz ? lcd_refresh_hz : 60));
    if (target_ms == 0)
        target_ms = 16;

    now = host_platform_ticks_ms();
    if (now < frame_start_ms + target_ms) {
        host_platform_delay_ms(frame_start_ms + target_ms - now);
    }
    frame_start_ms = host_platform_ticks_ms();
    return true;
}

void common_emu_frame_loop_reset(void) {}

void common_emu_input_loop(odroid_gamepad_state_t *joystick,
                           odroid_dialog_choice_t *game_options,
                           void_callback_t repaint)
{
    (void)joystick;
    (void)game_options;
    (void)repaint;
}

void common_emu_input_loop_handle_turbo(odroid_gamepad_state_t *joystick)
{
    (void)joystick;
}

void common_ingame_overlay(void) {}

void common_emu_enable_dwt_cycles(void) {}
unsigned int common_emu_get_dwt_cycles(void) { return 0; }
void common_emu_clear_dwt_cycles(void) {}
void cpumon_sleep(void) {}
void cpumon_busy(void) {}
void cpumon_reset(void) {}

/* --- Input ---------------------------------------------------------------- */

void odroid_input_read_gamepad(odroid_gamepad_state_t *out_state)
{
    int i;
    if (!out_state)
        return;
    host_poll_events();
    memset(out_state, 0, sizeof(*out_state));
    for (i = 0; i < ODROID_INPUT_MAX && i < (int)(sizeof(host_pad.values)); i++) {
        out_state->values[i] = host_pad.values[i];
        if (host_pad.values[i])
            out_state->bitmask |= (uint16_t)(1u << i);
    }
    host_maybe_quit();
}

void odroid_input_init(void) {}
void odroid_input_terminate(void) {}
long odroid_input_gamepad_last_read(void) { return 0; }
bool odroid_input_key_is_pressed(odroid_gamepad_key_t key)
{
    return key < ODROID_INPUT_MAX && host_pad.values[key] != 0;
}
void odroid_input_wait_for_key(odroid_gamepad_key_t key, bool pressed)
{
    (void)key;
    (void)pressed;
}
odroid_gamepad_state_t odroid_input_read_gamepad_(void)
{
    odroid_gamepad_state_t s;
    odroid_input_read_gamepad(&s);
    return s;
}
odroid_battery_state_t odroid_input_read_battery(void)
{
    odroid_battery_state_t b = {4200, 100, ODROID_BATTERY_CHARGE_STATE_FULL};
    return b;
}

/* --- Overlay text --------------------------------------------------------- */

int odroid_overlay_draw_text(uint16_t x, uint16_t y, uint16_t width,
                             const char *text, uint16_t color, uint16_t color_bg)
{
    pixel_t *fb = (pixel_t *)lcd_get_active_buffer();
    int cx = (int)x;
    int cy = (int)y;
    int max_x = width ? (int)x + (int)width : GW_LCD_WIDTH;

    if (!text || !fb)
        return 0;

    for (; *text; text++) {
        unsigned char ch = (unsigned char)*text;
        const uint8_t *glyph;
        int row, col;

        if (ch == '\n') {
            cx = (int)x;
            cy += 8;
            continue;
        }
        if (ch < 32 || ch > 127)
            ch = '?';
        if (cx + 8 > max_x || cx + 8 > GW_LCD_WIDTH || cy + 8 > GW_LCD_HEIGHT)
            break;

        glyph = font8x8_basic[ch - 32];
        for (row = 0; row < 8; row++) {
            uint8_t bits = glyph[row];
            for (col = 0; col < 8; col++) {
                pixel_t pix = (bits & (1u << col)) ? (pixel_t)color : (pixel_t)color_bg;
                fb[(cy + row) * GW_LCD_WIDTH + (cx + col)] = pix;
            }
        }
        cx += 8;
    }
    return cx - (int)x;
}

void odroid_overlay_init(void) {}
void odroid_overlay_set_font_size(int size) { (void)size; }
int odroid_overlay_get_font_size(void) { return 8; }
int odroid_overlay_get_font_width(void) { return 8; }
void odroid_overlay_draw_rect(int x, int y, int width, int height, int border, uint16_t color)
{
    (void)x; (void)y; (void)width; (void)height; (void)border; (void)color;
}
void odroid_overlay_draw_fill_rect(int x, int y, int width, int height, uint16_t color)
{
    (void)x; (void)y; (void)width; (void)height; (void)color;
}
void odroid_overlay_draw_battery(odroid_battery_state_t battery, int x, int y)
{
    (void)battery; (void)x; (void)y;
}
void odroid_overlay_draw_dialog(const char *header, odroid_dialog_choice_t *options, int sel)
{
    (void)header; (void)options; (void)sel;
}
void odroid_overlay_draw_banner_text(int center_x, int center_y, const char *text)
{
    (void)center_x; (void)center_y; (void)text;
}
void odroid_overlay_sleep_pause_banner(void_callback_t repaint, odroid_menu_flags_t flags,
                                       pause_input_callback_t input_cb)
{
    (void)repaint; (void)flags; (void)input_cb;
}
int odroid_overlay_dialog(const char *header, odroid_dialog_choice_t *options, int selected,
                          void_callback_t repaint, odroid_menu_flags_t flags)
{
    (void)header; (void)options; (void)selected; (void)repaint; (void)flags;
    return -1;
}
int odroid_overlay_confirm(const char *text, bool yes_selected, void_callback_t repaint)
{
    (void)text; (void)yes_selected; (void)repaint;
    return 0;
}
void odroid_overlay_alert(const char *text) { (void)text; }

uint8_t *odroid_overlay_cache_file_in_flash(const char *file_path, uint32_t *file_size_p,
                                            bool byte_swap)
{
    (void)file_path;
    (void)byte_swap;
    if (file_size_p)
        *file_size_p = 0;
    return NULL;
}

size_t odroid_overlay_cache_file_in_ram(const char *file_path, uint8_t *dest_address)
{
    FILE *f;
    size_t n;

    if (!file_path || !dest_address)
        return 0;
    f = fopen(file_path, "rb");
    if (!f)
        return 0;
    n = fread(dest_address, 1, host_active_file.size ? host_active_file.size : ram_get_free_size(), f);
    fclose(f);
    return n;
}

size_t odroid_overlay_cache_file_in_ram_with_offset(const char *file_path, uint8_t *dest_address,
                                                    uint32_t offset)
{
    (void)offset;
    return odroid_overlay_cache_file_in_ram(file_path, dest_address);
}

int odroid_overlay_settings_menu(odroid_dialog_choice_t *extra_options, void_callback_t repaint,
                                 odroid_menu_flags_t flags)
{
    (void)extra_options; (void)repaint; (void)flags;
    return -1;
}
int odroid_overlay_game_settings_menu(odroid_dialog_choice_t *extra_options, void_callback_t repaint,
                                      odroid_menu_flags_t flags)
{
    (void)extra_options; (void)repaint; (void)flags;
    return -1;
}
int odroid_overlay_game_menu(odroid_dialog_choice_t *extra_options, void_callback_t repaint,
                             odroid_menu_flags_t flags)
{
    (void)extra_options; (void)repaint; (void)flags;
    return -1;
}
int odroid_savestate_menu(const char *title, const char *rom_path, bool show_preview,
                          bool skip_on_single_used_slot, void_callback_t repaint)
{
    (void)title; (void)rom_path; (void)show_preview; (void)skip_on_single_used_slot; (void)repaint;
    return -1;
}

/* --- System / settings / malloc ------------------------------------------- */

void odroid_system_init(int app_id, int sampleRate)
{
    (void)app_id;
    odroid_audio_init(sampleRate);
}

void odroid_system_emu_init(state_handler_t load_cb, state_handler_t save_cb,
                            screenshot_handler_t screenshot_cb,
                            shutdown_handler_t shutdown_cb,
                            sleep_post_wakeup_handler_t sleep_post_wakeup_cb,
                            sram_save_handler_t sram_save_cb,
                            cheat_update_handler_t cheat_update_cb)
{
    host_load_state_cb = load_cb;
    host_save_state_cb = save_cb;
    (void)screenshot_cb; (void)shutdown_cb;
    (void)sleep_post_wakeup_cb; (void)sram_save_cb; (void)cheat_update_cb;
}

static void host_sanitize_stem(char *dst, size_t dst_sz, const char *name)
{
    size_t i, o = 0;
    if (!name || !name[0])
        name = "host";
    for (i = 0; name[i] && o + 1 < dst_sz; i++) {
        char c = name[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_')
            dst[o++] = c;
        else if (c == '.' )
            break;
        else
            dst[o++] = '_';
    }
    if (o == 0) {
        strncpy(dst, "host", dst_sz - 1);
        dst[dst_sz - 1] = '\0';
    } else {
        dst[o] = '\0';
    }
}

void odroid_system_get_save_path(char *path, size_t size, int slot)
{
    char stem[64];
    const char *name = (ACTIVE_FILE && ACTIVE_FILE->name[0]) ? ACTIVE_FILE->name : "host";

    host_sanitize_stem(stem, sizeof(stem), name);
    if (slot < 0)
        slot = 0;
    snprintf(path, size, "host_saves/%s.slot%d.sav", stem, slot);
}

static int host_ensure_save_dir(void)
{
    if (mkdir("host_saves", 0755) == 0 || errno == EEXIST)
        return 0;
    perror("host: mkdir host_saves");
    return -1;
}

bool odroid_system_emu_load_state(int slot)
{
    char path[512];

    if (!host_load_state_cb)
        return false;
    odroid_system_get_save_path(path, sizeof(path), slot);
    printf("host: loading %s\n", path);
    return host_load_state_cb(path);
}

bool odroid_system_emu_save_state(int slot)
{
    char path[512];

    if (!host_save_state_cb)
        return false;
    if (host_ensure_save_dir() != 0)
        return false;
    odroid_system_get_save_path(path, sizeof(path), slot);
    printf("host: saving %s\n", path);
    return host_save_state_cb(path);
}

int32_t odroid_settings_app_int32_get(const char *key, int32_t value_default)
{
    if (key && strcmp(key, "beep") == 0)
        return settings_beep;
    return value_default;
}

void odroid_settings_app_int32_set(const char *key, int32_t value)
{
    if (key && strcmp(key, "beep") == 0)
        settings_beep = value;
}

bool odroid_settings_ActiveGameGenieCodes_is_enabled(char *game_path, int code_index)
{
    (void)game_path;
    (void)code_index;
    return false;
}

bool odroid_settings_ActiveGameGenieCodes_set(char *game_path, int code_index, bool enable)
{
    (void)game_path;
    (void)code_index;
    (void)enable;
    return false;
}

void *ram_malloc(size_t size)
{
    void *p;
    size = (size + 7u) & ~7u;
    if (!ram_pool || ram_pool_used + size > ram_pool_size)
        return NULL;
    p = ram_pool + ram_pool_used;
    ram_pool_used += size;
    return p;
}

void *ram_calloc(size_t count, size_t size)
{
    size_t n = count * size;
    void *p = ram_malloc(n);
    if (p)
        memset(p, 0, n);
    return p;
}

size_t ram_get_free_size(void)
{
    return ram_pool ? (ram_pool_size - ram_pool_used) : 0;
}

void ram_init(void)
{
    ram_pool_used = 0;
}

void *ahb_malloc(size_t size) { return malloc(size); }
void *ahb_calloc(size_t count, size_t size) { return calloc(count, size); }
size_t ahb_get_free_size(void) { return 1024 * 1024; }
void itc_init(void) {}
void *itc_malloc(size_t size) { return malloc(size); }
void *itc_calloc(size_t count, size_t size) { return calloc(count, size); }
size_t itc_get_free_size(void) { return 64 * 1024; }
void dtc_init(void) {}
void *dtc_malloc(size_t size) { return malloc(size); }
void *dtc_calloc(size_t count, size_t size) { return calloc(count, size); }
size_t dtc_get_free_size(void) { return 64 * 1024; }

void wdog_refresh(void)
{
    host_poll_events();
    host_maybe_quit();
}

void Error_Handler(void) { abort(); }
void BSOD(BSOD_t fault, uint32_t pc, uint32_t lr)
{
    (void)fault; (void)pc; (void)lr;
    abort();
}
void boot_magic_set(uint32_t magic) { (void)magic; }
void SystemClock_Config(uint8_t new_oc_level) { (void)new_oc_level; }
void uptime_inc(void) {}
uint32_t uptime_get(void) { return host_platform_ticks_ms(); }

unsigned int crc32_le(unsigned int crc, unsigned char const *buf, unsigned int len)
{
    /* Minimal stub — not used by the template loop. */
    (void)buf;
    (void)len;
    return crc;
}
