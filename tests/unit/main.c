#include <stdio.h>

#include "test.h"

int main(void)
{
    run_core_tests();
    run_route_tests();
    run_peer_tests();
    run_pending_tests();
    run_codec_tests();
    run_tx_tests();
    run_beacon_tests();

    printf("\n%3d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}