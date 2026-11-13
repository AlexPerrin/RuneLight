# Sourced by the build and test scripts: finds the Android SDK, adb and apktool without a hardcoded home directory.
# Every value can be set in the environment first: ANDROID_SDK, ADB, APKTOOL_JAR, ANDROID_NDK.
if [[ -z ${ANDROID_SDK:-} ]]; then
    for _d in "${ANDROID_HOME:-}" "$HOME/Android/Sdk" "$HOME/Library/Android/sdk" /mnt/c/Users/*/AppData/Local/Android/Sdk; do
        [[ -n $_d && -d $_d/platforms ]] && { ANDROID_SDK=$_d; break; }
    done
    unset _d
fi
ANDROID_SDK=${ANDROID_SDK:-$HOME/Android/Sdk}
if [[ -z ${ADB:-} ]]; then
    # under WSL the Windows SDK's adb.exe is the one that sees a Windows emulator
    for _a in "$ANDROID_SDK/platform-tools/adb.exe" "$ANDROID_SDK/platform-tools/adb"; do
        [[ -x $_a ]] && { ADB=$_a; break; }
    done
    unset _a
fi
ADB=${ADB:-adb}
APKTOOL_JAR=${APKTOOL_JAR:-$HOME/tools/apktool.jar}
export ANDROID_SDK ADB APKTOOL_JAR
