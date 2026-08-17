/*
 * Desktop entry: init SDL, then jump into the same app_main() as on device.
 */

#include <stdio.h>
#include <stdlib.h>

#include "host_compat.h"
#include "host_platform.h"

#ifndef HOST_SCALE
#define HOST_SCALE 2
#endif

extern void app_main(uint8_t load_state, uint8_t start_paused, int8_t save_slot);

int main(int argc, char **argv)
{
    const char *title =
#if defined(PROJECT_KIND_HOMEBREW)
        "Retro-Go Homebrew (host)";
#else
        "Retro-Go Core (host)";
#endif
    const char *rom = getenv("HOST_ROM");

    if (argc > 1 && argv[1] && argv[1][0])
        rom = argv[1];

    if (host_platform_init(title, HOST_SCALE) != 0)
        return 1;

    gw_core_bridge_init();
    if (rom)
        host_set_rom_path(rom);

    printf("host: Esc or close window to quit\n");
    printf("host: Arrows=D-pad  Z=B  X=A  Enter=Start  Shift=Select  A/S=Y/X\n");
    printf("host: F1=save state  F2=load state  (./host_saves/)\n");
    if (rom)
        printf("host: ROM %s\n", rom);

    app_main(0, 0, -1);

    host_platform_shutdown();
    return 0;
}
