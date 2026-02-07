# Aether

This project implements the Aether congestion control algorithm, related simulation tools, and trace dataset.

## 1. Simulation Tools 

**Directory:** `Simulation_Tools/` 

Modified Mahimahi emulator that simulates cellular uplink buffering.
- **Build**: Run `./autogen.sh` inside this directory.

## 2. Aether Algorithm 

**Directory:** `Aether_CCA/`

- `aether_monitor`: Monitors for Aether.
- `aether_bottleneck_detector`: Detects bottlenecks.
- `aether_rate_controller`: Calculates capacity and sets congestion window.
- `aether_cca`: Main integration module.

## 3. Trace Dataset

**Directory:** `Trace Dataset/`

Uplink throughput traces collected from real-world 5G/LTE networks.
- Includes data from **US**, **Europe**, and **China**.
- Covers various mobility scenarios (Static, Walking, Driving, High-speed rail).


