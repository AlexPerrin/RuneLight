# tools/up.ps1 -Slug <slug> -Apk <path> [-Down]: the Windows side of the client bring-up, one interop call.
# Boots the Agent_Phone emulator for this change (a free even console port, recorded by slug), waits for
# sys.boot_completed, installs the APK, grants Chrome the local network, backs up the device state, enables devtools
# and prints `LOGGED_IN <serial> <devtools port>`; -Down restores the state (checked) and stops the emulator.
param([string]$Slug, [string]$Apk, [switch]$Down)
