/**
 * @file test_define_sets.c
 * @brief A real application's implementation-macro set (EigenMOO's):
 *        core, options, MUD, compression, MCP, and no explicit events.
 *
 * The MUD implementation must bring in the events implementation, so MSP
 * and MXP work, and the whole set must build warning-free (CMake adds
 * -Werror for this target).
 */

#define TELNETTY_IMPLEMENTATION
#define TELNETTY_OPTIONS_IMPLEMENTATION
#define TELNETTY_MUD_IMPLEMENTATION
#define TELNETTY_COMPRESSION_IMPLEMENTATION
#define TELNETTY_MCP_IMPLEMENTATION

#include "telnetty_core.h"
#include "telnetty_options.h"
#include "telnetty_mud.h"
#include "telnetty_compression.h"
#include "telnetty_mcp.h"

#include <stdio.h>

int main(void) {
    int failed = 0;
    telnetty_context_t* ctx = telnetty_create(NULL, NULL);
    if (!ctx) return 1;
    if (telnetty_enable_msp(ctx, NULL, NULL) != 0) { puts("FAIL: enable_msp"); failed++; }
    if (telnetty_enable_mxp(ctx, NULL, NULL) != 0) { puts("FAIL: enable_mxp"); failed++; }
    if (telnetty_enable_mcp(ctx, true, TELNETTY_MCP_AUTH_KEY, "key") != 0) {
        puts("FAIL: enable_mcp");
        failed++;
    }
    telnetty_destroy(ctx);
    printf("%d failure(s)\n", failed);
    return failed ? 1 : 0;
}
