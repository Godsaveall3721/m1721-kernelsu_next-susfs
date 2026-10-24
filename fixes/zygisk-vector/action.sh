#!/system/bin/sh

MANAGER_PACKAGE_NAME="org.matrix.vector.manager"
INJECTED_PACKAGE_NAME="com.android.shell"
MODDIR="${0%/*}"
DAEMON_PID=

if ! pidof vectord >/dev/null 2>&1; then
    "$MODDIR/run-vectord.sh" &
    DAEMON_PID=$!

    # A late daemon restart injects system_server once. On this device the
    # restart and binder handoff take roughly 25-30 seconds before the
    # parasitic manager can be launched safely.
    i=0
    while ! pidof vectord >/dev/null 2>&1 && [ "$i" -lt 100 ]; do
        sleep 0.1
        i=$((i + 1))
    done
    sleep 30
fi

am start -c "${MANAGER_PACKAGE_NAME}.LAUNCH_MANAGER" \
    "${INJECTED_PACKAGE_NAME}/.BugreportWarningActivity"

# Keep the action process alive as the daemon supervisor. If the action exits,
# this vendor KernelSU reaps background children started from module scripts.
if [ -n "$DAEMON_PID" ]; then
    wait "$DAEMON_PID"
fi
