#!/bin/bash

REPO="$(cd "$(dirname "$0")/.." && pwd)"
MM_LINK="$REPO/Simulation_Tools/src/frontend/mm-link"
MODULE_SRC="$REPO/Aether_CCA/module"
TRACE="${1:-$REPO/Trace Dataset/US/5Gmmwave_Static.txt}"
DUR="${2:-15}"
PORT=5201

if [ ! -f "$MM_LINK" ]; then
    cd "$REPO/Simulation_Tools"
    mkdir -p traces && [ ! -f traces/Makefile.am ] && echo "EXTRA_DIST =" > traces/Makefile.am
    [ ! -f configure ] && bash autogen.sh
    [ ! -f Makefile ] && ./configure
    make -j$(nproc)
    cd - >/dev/null
fi

if ! grep -q aether /proc/sys/net/ipv4/tcp_available_congestion_control 2>/dev/null; then
    BUILD_DIR=$(mktemp -d)
    cp "$MODULE_SRC"/tcp_aether.c "$MODULE_SRC"/Makefile "$BUILD_DIR"/
    make -C "$BUILD_DIR" 2>&1 | tail -1
    sudo insmod "$BUILD_DIR"/tcp_aether.ko
    rm -rf "$BUILD_DIR"
fi

DL=$(mktemp)
seq 0 60000 > "$DL"

signal_bridge() {
    while [ -f /tmp/.aether_exp ]; do
        [ -f /tmp/mm_virtual_driver_queue ] && cat /tmp/mm_virtual_driver_queue > /proc/aether_signal 2>/dev/null
        sleep 0.01
    done
}

pkill -9 -f "iperf3 -s" 2>/dev/null
sleep 1
iperf3 -s -D -p $PORT

touch /tmp/.aether_exp
signal_bridge &
BRIDGE_PID=$!

echo "=== $(basename "$TRACE") | ${DUR}s | CCA=aether ==="
"$MM_LINK" "$TRACE" "$DL" -- \
    sh -c "sleep 1; iperf3 -c \$MAHIMAHI_BASE -p $PORT -t $DUR -C aether"

rm -f /tmp/.aether_exp "$DL"
wait $BRIDGE_PID 2>/dev/null
pkill -f "iperf3 -s" 2>/dev/null
exit 0
