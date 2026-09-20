/* Public-fixture interoperability checker. Not a runtime replay-state service. */
#include <stdio.h>
#include <inttypes.h>
#include "tcs/admin.h"
#include "tcs/launch.h"
int main(void)
{
    uint8_t bytes[304]; struct tcs_launch launch; struct tcs_admin administrator = {0};
    if (fread(bytes, 1, sizeof bytes, stdin) != sizeof bytes || getchar() != EOF ||
        !tcs_launch_decode(bytes, 112, TCS_FIXTURE_MODE, &launch) ||
        !tcs_admin_init(&administrator, launch.identity.public_key, launch.identity.realm, launch.boot)) return 1;
    /* Test one signature at the requested sequence, not sequence freshness. */
    administrator.next_sequence = 0;
    for (unsigned i = 0; i < 8; ++i) administrator.next_sequence |= (uint64_t)bytes[112+80+i] << (8*i);
    struct tcs_admin_command command;
    if (tcs_admin_admit(&administrator, bytes + 112, 192, &command) != TCS_ADMIN_ACCEPTED) return 1;
    printf("%" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 "\n", command.request.op,
        command.request.subject, command.sequence, command.expected_generation);
    return 0;
}
