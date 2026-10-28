#!/system/bin/sh
# Remove the exact ROM release marker confirmed to trigger this app's No2.
# Does not change the kernel, app files/data, root, or injection configuration.
RP=/data/adb/ksu/bin/resetprop
[ -x "$RP" ] || exit 1
COUNT=0
for KEY in ro.lineage.display.version ro.lineage.releasetype ro.lineage.version ro.modversion; do
  VALUE=$(getprop "$KEY")
  case "$VALUE" in
    *UNOFFICIAL*)
      NEW=$(printf '%s' "$VALUE" | sed 's/-UNOFFICIAL//g;s/UNOFFICIAL//g')
      "$RP" -n "$KEY" "$NEW" || exit 1
      COUNT=$((COUNT + 1))
      ;;
  esac
done
log -t LepaoRomMetadata "Applied ROM metadata compatibility adjustment: $COUNT properties"
