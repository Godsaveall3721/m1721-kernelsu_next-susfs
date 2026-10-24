#!/system/bin/sh

MOD=/data/adb/modules/zygisksu
ZYGISKD="$MOD/bin/zygiskd"

[ -x "$ZYGISKD" ] || exit 0
[ -f "$MOD/disable" ] && exit 0

# The vendor init can miss the first zygote execve. Once the Zygisk daemon
# has installed its hooks in init, one zygote restart performs injection.
STATUS="$("$ZYGISKD" status 2>/dev/null || true)"
echo "$STATUS" | grep -q '^zygote_states:0$' || exit 0

settings put global development_settings_enabled 1
settings put global adb_enabled 1
setprop persist.sys.usb.config mtp,adb
setprop sys.usb.config mtp,adb
setprop ctl.restart zygote
