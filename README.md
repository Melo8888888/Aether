# Aether

## Structure

```
Aether_CCA/
  core/              Core algorithm implementation
  module/            Linux kernel module (tcp_aether)
Simulation_Tools/    Our simulation tools
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

# 2. Set permissions
sudo chown root:root Simulation_Tools/src/frontend/mm-link
sudo chmod u+s Simulation_Tools/src/frontend/mm-link

# 3. Run
cd simulation
bash run.sh "../Trace Dataset/US/5Gmmwave_Static.txt" 15
```

`run.sh` will automatically build and load the `tcp_aether` kernel module, start an iperf3 server, launch the trace-driven emulation, and run the upload test with `CCA=aether`.

## Aether CCA

`Aether_CCA/core/` contains the algorithm implementation:

- `aether_monitor` — Monitors for Aether
- `aether_bottleneck_detector` — determines whether the cellular uplink is the current bottleneck
- `aether_rate_controller` — estimates uplink capacity and updates cwnd

`Aether_CCA/module/` contains the Linux kernel module (`tcp_aether.c`), which registers Aether as a selectable TCP CCA. 

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
