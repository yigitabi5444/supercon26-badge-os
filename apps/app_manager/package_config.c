#include "package_source.h"
#ifdef AM_PACKAGE_CHANNEL_CONFIGURED
#include "am_package_channel.h"
#else
// Provision an immutable public key and HTTPS sources in the firmware profile.
const am_source_config_t am_app_source_config __attribute__((weak))={0};
#endif
