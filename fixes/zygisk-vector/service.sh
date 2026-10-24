#!/system/bin/sh

MODDIR="${0%/*}"
cd "$MODDIR" || exit 1

# Android 11's toybox unshare has no --propagation option. Use KernelSU's
# busybox implementation so mounting the resource-cache tmpfs cannot propagate
# back into the parent mount namespace.
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
