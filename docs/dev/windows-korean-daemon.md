# Windows non-ASCII profile paths: daemon regression

Base: `v1.27.0` (`081f6478e`). Work branch: `fix/windows-korean-daemon`.
The source requires C++20 and Qt >= 6.7; retain the upstream build settings.

## Failure path

1. The GUI sends the configuration file path to the daemon over the existing
   UTF-8 local IPC protocol (`IpcClient::sendMessage`, `IpcServer::processMessage`).
2. `DaemonApp::applyWatchdogCommand` reads the user configuration with `QSettings`
   and constructs a quoted executable/settings command. `QString::toStdString`
   produces UTF-8. Client and server use the same watchdog path.
3. `MSWindowsWatchdog::setProcessConfig` previously widened each UTF-8 byte into
   a `wchar_t`, corrupting non-ASCII arguments (and sign-extending bytes when
   `char` is signed). This conversion alone does not allocate a vector.
4. `MSWindowsWatchdog::startProcess` logs that wide command with narrow printf
   `%ls` **before** calling `CreateProcess` / `CreateProcessAsUser`. This depends
   on the CRT locale. Unrepresentable/invalid wide characters cause
   `vsnprintf` to return -1 (`EILSEQ`).
5. `Log::print` treated every negative result as buffer exhaustion and repeatedly
   doubled a signed `int` buffer length. The formatting error persists; memory
   exhaustion or integer overflow followed by `std::vector<char>::resize` can
   throw `std::length_error` (MSVC: `vector too long`). The watchdog catches this
   exception and emits `daemon failed to start process`. No Core was started.

This is a source-supported causal path, with an encoding-error reproduction.
The exact throw site in the reported installation still requires its Windows
stack trace or debugger: allocation failures and other startup exceptions can
also be caught by the same watchdog handler. Do not infer that every occurrence
of `vector too long` has this cause.

Microsoft references:

- [vsnprintf return values and encoding errors](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/vsnprintf-vsnprintf-vsnprintf-l-vsnwprintf-vsnwprintf-l?view=msvc-170)
- [CreateProcessW mutable command buffer and length limit](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessw)
- [MultiByteToWideChar strict UTF-8 conversion](https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-multibytetowidechar)

## Changes

- Decode UTF-8 with `MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS)` in the
  Windows process helper. The legacy `Unicode::UTF8ToUTF16` replaces malformed
  input and does not report decoding failures, so it cannot provide strict
  command validation. No new library dependency is introduced.
- Accept an empty command as the existing watchdog stop request. Reject embedded
  NUL and malformed UTF-8; log the byte count and cause. Preserve previous
  configuration/state on rejection.
- Bound input to 98,298 UTF-8 bytes before integer narrowing/allocation; check the
  decoded command against 32,766 UTF-16 code units (32,767 including NUL).
  These are command-line limits, not filesystem long-path support guarantees.
- Keep quoting and arguments unchanged. Pass writable `wstring::data()` to the
  explicit wide Win32 process APIs. Log commands as UTF-8 with `%s`, including
  the verbose log that previously passed `wchar_t*` to `%s`.
- Treat negative `vsnprintf` results as explicit formatting errors with errno,
  rather than repeatedly allocating. Resize valid output to the returned length
  plus its terminator; fix the exact 1024-byte boundary truncation.
- Preserve the process creation error across token/environment cleanup. Do not
  replace it with a subsequent invalid-handle `GetExitCodeProcess` error.

Desktop `QProcess` execution, IPC messages, settings schema, elevation/session
selection, service installation, process shutdown and SAS behavior are retained.
The logger fix is shared across platforms; other unconvertible `%ls` callers now
receive a diagnostic exception instead of unbounded buffer growth.

## Automated validation

`LogTests` covers formatting failures in the C locale, UTF-8 command output,
long logs, and the 1023/1024/1025-byte terminator boundaries.

Windows-only `MSWindowsProcessTests` covers:

- English, Korean, spaces, Japanese, Chinese, punctuation and supplementary
  Unicode paths, with exact UTF-16 command comparison.
- Empty input, bounded non-NUL-terminated string views, embedded NUL, truncated
  UTF-8, stray continuation bytes, overlong encoding, encoded surrogates and
  code points above U+10FFFF.
- Accepted/rejected command-length boundaries, including multibyte input and
  an oversized input rejected before conversion/output allocation.
- Actual `CreateProcessW` execution of a test child. The child records its Qt
  arguments to a Korean/space-containing output filename, and the parent checks
  client/server settings arguments without requiring those simulated profiles
  to exist. This is not a LocalSystem service or login-screen test.

The branch-specific workflow `.github/workflows/windows-daemon.yml` reuses
upstream MSVC setup, Qt/vcpkg dependency installation, CMake, CTest and WiX/CPack.
It builds Windows x64, runs the complete unit-test suite, then packages and
uploads the MSI plus test diagnostics. The existing all-platform CI is retained.
No release or upstream pull request is created.

The upstream portable ZIP deliberately omits `deskflow-daemon.exe` and cannot
validate daemon/login-screen behavior. Use the MSI artifact for service testing.
Artifacts and run logs are available in the fork's **Actions > Windows daemon
regression**. A successful CI run is not proof of deployed service behavior.

## Required Windows acceptance tests

Back up the existing user and service settings and record the installed version
before installing the test MSI. Keep the existing `C:\Users\혈액검사` profile.

1. On Windows 10 and Windows 11 x64, install the MSI and confirm service `Deskflow`
   uses LocalSystem, Automatic startup, and the expected installed daemon path.
2. Select Daemon mode in the GUI for both Client and Server roles. Confirm the
   user settings path survives IPC, daemon logging and Core arguments unchanged:
   `C:\Users\혈액검사\AppData\Roaming\Deskflow\Deskflow.conf`.
3. Confirm `deskflow-core.exe` exists, stays running, connects to a peer, and
   keyboard/mouse sharing works. Check that logs contain neither `vector too long`
   nor formatting/conversion errors. A Running service alone is insufficient.
4. Test elevated/non-elevated operation, GUI reconnect, stop/start, configuration
   changes, restarting the service and reboot/automatic startup. Verify existing
   user settings and the daemon's separately persisted system-profile settings.
   A service INI containing only `logLevel=INFO` does not itself specify a Core
   settings path; the GUI sends and persists that path through existing IPC.
5. Test Desktop mode and both roles with ASCII and non-ASCII profiles; compare
   process start/stop and settings behavior with unmodified 1.27.0.
6. Test lock/unlock, UAC, sign-out/sign-in, session switching and the Windows login
   screen with elevation configured. Verify sharing and Ctrl+Alt+Del/SAS as
   applicable. These require real interactive Windows sessions and a peer.
7. Record OS build, MSI/run/commit, role, elevation, log level, Core command and
   daemon/Core logs. If startup still fails, capture the original exception stack
   at `Log::print` / `MSWindowsWatchdog::startProcess` and Windows API error codes.

Known unchanged limitations: the IPC protocol uses `=` and newline as separators;
paths containing those characters are not addressed by this patch. UNC-path
rejection remains in force. Existing Win32 executable-path and filesystem limits
still apply. Test installers are unsigned unless signing is separately configured.
