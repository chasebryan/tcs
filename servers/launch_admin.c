#include "tcs/admin_server.h"
#include "tcs/launch_profile.h"
static struct tcs_admin administrator;
void init(void)
{
    microkit_msginfo reply = microkit_ppcall(1, microkit_msginfo_new(TCS_LAUNCH_CONTEXT, 0));
    if (microkit_msginfo_get_label(reply) != TCS_LAUNCH_DATA ||
        microkit_msginfo_get_count(reply) != TCS_LAUNCH_BYTES / 8) return;
    uint8_t bytes[TCS_LAUNCH_BYTES]; struct tcs_launch launch;
    tcs_bytes_from_words(bytes, sizeof bytes);
    if (!tcs_launch_decode(bytes, sizeof bytes, TCS_LAUNCH_MODE, &launch)) return;
    (void)tcs_admin_init(&administrator, launch.identity.public_key, launch.identity.realm, launch.boot);
}
void notified(microkit_channel ch) { (void)ch; }
microkit_msginfo protected(microkit_channel ch, microkit_msginfo msg)
{ return tcs_admin_server_request(&administrator, ch, msg); }
