# tools/up.ps1 -Slug <slug> -Apk <path> -Plugins <dir> -Backup <dir> [-Serial <s> -Down]: the Windows side of the client bring-up.
# Up: boots an Agent_Phone emulator for this change on a free even console port, waits for sys.boot_completed, replaces the
# official app with the APK, grants Chrome the local network, backs up the device state, puts the devtools and the three
# default plugins in place, and prints "SERIAL <emulator-port>". Down: checks the restore, stops the emulator.
param([string]$Slug, [string]$Apk, [string]$Plugins, [string]$Backup, [string]$Serial, [switch]$Down)
$ErrorActionPreference = "Stop"
$sdk = "$env:LOCALAPPDATA\Android\Sdk"; $adb = "$sdk\platform-tools\adb.exe"; $pkg = "com.jagex.oldscape.android"
$files = "/sdcard/Android/data/$pkg/files"; $defaults = @("grounditems.jar", "opponentinfo.jar", "quest-helper.jar")
function A { param([Parameter(ValueFromRemainingArguments)]$a) & $adb -s $Serial @a 2>&1 }

if ($Down) {
  $bad = @()
  A shell rm -f "$files/plugins/devtools.jar" | Out-Null
  foreach ($p in "devtools", "probe", "set") { A shell setprop "debug.osrsrl.$p" '""' | Out-Null }
  if ((A shell ls "$files/plugins" | Sort-Object) -join "," -ne ($defaults -join ",")) { $bad += "plugins dir is not the three defaults" }
  foreach ($p in "netallow", "probe", "set", "where", "say", "wdump", "keylog", "projlog", "camlog", "selflog", "tmodelog", "netlog", "netwrite", "agent", "devtools", "soundlog") {
    $v = (A shell getprop "debug.osrsrl.$p").Trim(); if ($v -and $v -ne "0") { $bad += "prop debug.osrsrl.$p = $v" }
  }
  A exec-out cat "$files/settings.properties" | Set-Content -NoNewline "$Backup\settings.after"
  if ((Get-FileHash "$Backup\settings.after").Hash -ne (Get-FileHash "$Backup\settings.before").Hash) { $bad += "settings.properties changed" }
  if ((A shell find $files -type f | Sort-Object) -join "`n" -ne (Get-Content "$Backup\files.before" -Raw).TrimEnd("`n")) { $bad += "files/ tree changed" }
  A emu kill | Out-Null
  if ($bad) { Write-Output ("RESTORE FAILED: " + ($bad -join "; ")); exit 1 } else { Write-Output "restored" }
  exit 0
}

$port = 5560..5598 | Where-Object { $_ % 2 -eq 0 -and -not (Get-NetTCPConnection -LocalPort $_ -State Listen -ErrorAction SilentlyContinue) } | Select-Object -First 1
$Serial = "emulator-$port"
Start-Process -FilePath "$sdk\emulator\emulator.exe" -ArgumentList @("-avd", "Agent_Phone", "-read-only", "-gpu", "swiftshader_indirect", "-port", $port) -WindowStyle Minimized
& $adb -s $Serial wait-for-device | Out-Null
while ((A shell getprop sys.boot_completed).Trim() -ne "1") { Start-Sleep -Seconds 2 }
A uninstall $pkg | Out-Null
$r = A install -r $Apk; if ($r -notmatch "Success") { Write-Error "install failed: $r"; exit 1 }
$path = (A shell pm path $pkg).Trim() -replace "^package:", ""
if ((A shell "unzip -l $path | grep -c libosrsrl").Trim() -ne "1") { Write-Error "the APK carries no libosrsrl"; exit 1 }
A shell pm grant com.android.chrome android.permission.ACCESS_LOCAL_NETWORK | Out-Null
A shell monkey -p $pkg -c android.intent.category.LAUNCHER 1 | Out-Null; Start-Sleep -Seconds 5; A shell am force-stop $pkg | Out-Null   # a first start creates files/
A shell mkdir -p "$files/plugins" | Out-Null; A shell chmod 777 "$files/plugins" | Out-Null
foreach ($j in $defaults) { A push "$Plugins\$j" "$files/plugins/$j" | Out-Null }
A exec-out cat "$files/settings.properties" | Set-Content -NoNewline "$Backup\settings.before"
(A shell find $files -type f | Sort-Object) -join "`n" | Set-Content "$Backup\files.before"
A push "$Plugins\devtools.jar" "$files/plugins/devtools.jar" | Out-Null
A shell setprop debug.osrsrl.devtools 1 | Out-Null
Write-Output "SERIAL $Serial"
