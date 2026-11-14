#if defined(APP_XUH_PLAYER_PROFILE_SILENT) && APP_XUH_PLAYER_PROFILE_SILENT
#include <stddef.h>
#include <syscall.h>

/* XTC 15.3.1 makes --gprof and --xscope mutually exclusive. Keep printf's
 * formatting/control flow but discard its writes: JTAG logging would halt the
 * USB tile and xSCOPE writes without a receiver could block. This object must
 * only be linked in the separate, non-xSCOPE profiling build. */
int _write(int fd, const char buffer[], size_t count)
{
    (void)fd;
    (void)buffer;
    return (int)count;
}
#endif
