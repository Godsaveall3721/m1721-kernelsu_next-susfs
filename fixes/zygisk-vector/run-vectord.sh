#!/system/bin/sh

MODDIR="${0%/*}"
cd "$MODDIR" || exit 1

if pidof vectord >/dev/null 2>&1; then
    exit 0
fi

export ANDROID_ROOT=/system
export ANDROID_DATA=/data
export ANDROID_ART_ROOT=/apex/com.android.art
export ANDROID_I18N_ROOT=/apex/com.android.i18n
export ANDROID_TZDATA_ROOT=/apex/com.android.tzdata
export EXTERNAL_STORAGE=/sdcard

BUSYBOX=/data/adb/ksu/bin/busybox
[ -x "$BUSYBOX" ] || BUSYBOX=/data/adb/magisk/busybox

LOG=/data/local/tmp/vectord-umh.log
echo "start $(date) pid=$$ cwd=$(pwd)" >"$LOG"

if [ -x "$BUSYBOX" ]; then
    exec "$BUSYBOX" unshare --propagation slave -m \
        "$MODDIR/daemon" --system-server-max-retry=3 >>"$LOG" 2>&1
fi

exec unshare -m "$MODDIR/daemon" --system-server-max-retry=3 >>"$LOG" 2>&1
