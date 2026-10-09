# Windows daemon non-ASCII command regression

Base: `v1.27.0`. The Windows daemon receives a UTF-8 command built by
`DaemonApp::applyWatchdogCommand`, including the quoted `--settings` path.

## Cause and fix

The previous byte-to-`wchar_t` widening corrupted non-ASCII arguments.
The pre-launch `%ls` log also depended on the CRT locale: an encoding error
made `vsnprintf` return a negative value, which `Log::print` repeatedly retried
with larger buffers. That path could overflow the signed length and lead to
MSVC's `vector too long` exception before Core creation.

The process helper now decodes UTF-8 strictly with `MultiByteToWideChar`, rejects
embedded NUL and commands over 32,766 UTF-16 units, and preserves quoting.
Empty commands retain the watchdog stop behavior. Command logs use UTF-8 `%s`;
negative formatting results produce a diagnostic instead of repeated allocation.
Win32 launch errors survive cleanup and mutable buffers go to the wide APIs.
Settings, IPC, token/session selection and SAS behavior are unchanged.

## Validation

`MSWindowsProcessTests` checks English, Korean, Japanese, Chinese, spaces,
punctuation and supplementary Unicode, malformed UTF-8, NUL, empty input and
length boundaries. A real Windows child process verifies Client/Server argument
preservation; this does not exercise LocalSystem or secure desktops.

`LogTests` covers encoding errors, UTF-8 output, NUL buffer boundaries, long
messages and INFO filtering of DEBUG/VERBOSE messages. Existing screen-switch
coordinates are INFO messages; per-motion diagnostics are VERBOSE and unchanged.

The branch workflow reuses the project's MSVC, Qt/vcpkg and WiX/CPack setup,
runs the full CTest suite, and uploads an MSI and detailed regression reports.
The upstream portable ZIP omits the daemon, so use the MSI for service tests.

Manual testing reported by the patch author confirmed Daemon and Desktop modes,
keyboard/mouse sharing and no recurrence of the startup error under a Korean
profile. The precise OS build, role coverage and repeat counts were not supplied.

Still required: Windows login screen, UAC, lock/session changes, reboot/automatic
connection, and macOS/Linux build/runtime validation. Install GUI, Core and daemon
from the same build because their IPC handshake checks version/Git SHA.

Existing IPC separator restrictions (`=` and newline), UNC rejection and Win32
filesystem/executable-path limits remain. Logs retain existing configuration
paths; remove personal information before sharing them publicly. Test MSIs are
unsigned and retain the upstream VC++ runtime prerequisite.
