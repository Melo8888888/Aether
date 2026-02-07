# Aether

This project implements the Aether congestion control algorithm, related simulation tools, and trace dataset.

## 1. Simulation Tools 

**Directory:** `Simulation_Tools/` 

Modified Mahimahi emulator that simulates cellular uplink buffering.
- **Build**: Run `./autogen.sh` inside this directory.

## 2. Aether Core Algorithm (Core Code)

**Directory:** `Aether_CCA/`

The core C implementation of the Aether algorithm (Kernel Style).
- `aether_monitor`: Monitors packet dwell time.
- `aether_bottleneck_detector`: Detects uplink vs internet bottlenecks.
- `aether_rate_controller`: Calculates capacity and sets congestion window.
- `aether_cca`: Main integration module.

## 3. Trace Dataset

**Directory:** `Trace Dataset/`

Uplink throughput traces collected from real-world 5G/LTE networks.
- Includes data from **US**, **Europe**, and **China**.
- Covers various mobility scenarios (Static, Walking, Driving, High-speed rail).


