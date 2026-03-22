# Aether

Aether is a congestion control algorithm for cellular uplink. It monitors the modem driver buffer to capture uplink dynamics and adjusts the sending rate via dwell-time-based signals. When the cellular uplink is not the bottleneck, Aether defers to the kernel's default CCA.

## Structure

```
Aether_CCA/
  core/              Core algorithm implementation
  module/            Linux kernel module (tcp_aether)
Simulation_Tools/    Modified Mahimahi with virtual driver queue
Trace Dataset/       Cellular uplink traces (US, Europe, China)
simulation/          Quick-start example for trace-driven experiments
```

## Quick Start

```bash
# 1. Build simulation tools
cd Simulation_Tools
mkdir -p traces && echo "EXTRA_DIST =" > traces/Makefile.am
bash autogen.sh && ./configure && make -j$(nproc)
cd ..

# 2. Set permissions (mm-link requires setuid for network namespaces)
sudo chown root:root Simulation_Tools/src/frontend/mm-link
sudo chmod u+s Simulation_Tools/src/frontend/mm-link

# 3. Run
cd simulation
bash run.sh "../Trace Dataset/US/5Gmmwave_Static.txt" 15
```

`run.sh` will automatically build and load the `tcp_aether` kernel module, start an iperf3 server, launch the trace-driven emulation via `mm-link`, and run the upload test with `CCA=aether`.

## Aether CCA

`Aether_CCA/core/` contains the algorithm implementation:

- `aether_monitor` — hooks driver TX ring enqueue/dequeue, measures packet dwell time
- `aether_bottleneck_detector` — determines whether the cellular uplink is the current bottleneck
- `aether_rate_controller` — estimates uplink capacity and updates cwnd

`Aether_CCA/module/` contains the Linux kernel module (`tcp_aether.c`), which registers Aether as a selectable TCP CCA. The module receives driver queue signals via `/proc/aether_signal` and activates rate control only when the uplink is the bottleneck. Otherwise, the default CCA handles all window updates.

## Simulation Tools

Modified Mahimahi emulator that simulates cellular uplink buffering.

The driver state is exported to `/tmp/mm_virtual_driver_queue` for real-time CCA consumption.

## Trace Dataset

Cellular uplink throughput traces in Mahimahi format:

| Region | Scenarios | Technology |
|--------|-----------|------------|
| US (5 cities) | Static, Walking, Driving | 5G mmWave, 5G Sub-6, LTE |
| Europe (Belgium) | Walking, Cycling, Driving | LTE |
| China (Beijing) | Outdoors, Indoors, Underground | 5G |

## Dependencies

```bash
sudo apt install iperf3 autoconf automake libtool protobuf-compiler \
    libprotobuf-dev libssl-dev apache2-dev libapr1-dev libxcb1-dev \
    libxcb-present-dev libpango1.0-dev dnsmasq ssl-cert linux-headers-$(uname -r)
```
