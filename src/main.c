#include "pico/stdlib.h"

int main()
{
    // Production entrypoint:
    // - Initializes stdio for potential diagnostics/logging
    // - Does not run validation or PIO timing test logic
    // - Remains in a low-overhead idle loop
    stdio_init_all();

    // Keep firmware alive. Replace this loop with production functionality.
    while (true) {
        tight_loop_contents();
    }
}
