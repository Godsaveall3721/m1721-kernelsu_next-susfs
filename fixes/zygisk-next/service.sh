#!/system/bin/sh

MODDIR="${0%/*}"

if [ ! -f /data/adb/service.d/.zn_cleanup.sh ]; then
  mkdir -p /data/adb/service.d
  cat "$MODDIR/cleanup.sh" > /data/adb/service.d/.zn_cleanup.sh
  chmod +x /data/adb/service.d/.zn_cleanup.sh
fi

# On this vendor 4.9 kernel the initial zygote can race the Zygisk execve hook.
# Once the daemon has installed its hooks, one zygote restart performs injection.
if "$MODDIR/bin/zygiskd" status 2>/dev/null | grep -q '^zygote_states:0$'; then
  setprop ctl.restart zygote
  exit 0
fi

if [ "$ZYGISK_ENABLED" = "1" ]; then
  exit 0
fi

"$MODDIR/bin/zygiskd" service-stage
