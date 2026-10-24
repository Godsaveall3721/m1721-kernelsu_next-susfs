#!/system/bin/sh

MODDIR="${0%/*}"
cd "$MODDIR" || exit 1

if pidof vectord >/dev/null 2>&1; then
    exit 0
fi

BUSYBOX=/data/adb/ksu/bin/busybox
[ -x "$BUSYBOX" ] || BUSYBOX=/data/adb/magisk/busybox

if [ -x "$BUSYBOX" ]; then
    "$BUSYBOX" setsid "$BUSYBOX" unshare --propagation slave -m \
        "$MODDIR/daemon" --system-server-max-retry=3 "$@" \
        </dev/null >/dev/null 2>&1 &
elif unshare --propagation slave -m true 2>/dev/null; then
    setsid unshare --propagation slave -m \
        "$MODDIR/daemon" --system-server-max-retry=3 "$@" \
        </dev/null >/dev/null 2>&1 &
else
    setsid unshare -m "$MODDIR/daemon" --system-server-max-retry=3 "$@" \
        </dev/null >/dev/null 2>&1 &
fi
