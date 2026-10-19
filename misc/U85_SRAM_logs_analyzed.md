The U85 SRAM inference did about **32 GOPS (0.032 TOPS)** and moved about **3.65 GB/s** to and from SRAM, measured over the 2.456 ms inference time. If you count only the cycles the NPU itself reported, it's about **34 GOPS** and **3.9 GB/s**. Both line up closely with Vela's estimate.

Two assumptions are behind these numbers:
- **NPU clock is 400 MHz.** This is the `core_clock` in the `Ethos_U85_SRAM_Only` config in `ensemble_vela.ini`. It's consistent with the log: 911,792 NPU cycles at 400 MHz is 2.28 ms, which fits inside the 2.456 ms measured on the CPU.
- **One beat is 16 bytes,** which is the width of one 128-bit AXI port. Vela's "Design peak SRAM bandwidth 11.92 GB/s" equals 400 MHz × 2 ports × 16 bytes. Vela's "GB" is really GiB (binary units), so that peak is 12.8 GB/s in decimal.

**Time base**

| | Cycles | Time |
|---|---|---|
| `Invoke()` measured by the CPU | n/a | 2.456 ms |
| `NPU_TOTAL` | 911,792 | 2.280 ms |
| `NPU_ACTIVE` | 856,761 | 2.142 ms |
| Vela estimate | 955,871 | 2.39 ms |

The NPU finished about 4.6% faster than Vela predicted. About 0.18 ms of the CPU-measured time is TFLM and driver overhead outside the NPU.

**OPS/second**

Vela reports 38,807,136 MACs, which is 77.6 M operations (1 MAC = 2 ops).

| Time base | OPS/s | Inferences/s |
|---|---|---|
| Measured inference time (2.456 ms) | 31.6 GOPS (15.8 GMAC/s) | 407 |
| `NPU_TOTAL` | 34.0 GOPS | 439 |
| `NPU_ACTIVE` | 36.2 GOPS | n/a |

The U85-256 peak is 256 MACs × 2 × 400 MHz = 204.8 GOPS, so this model uses about 15–17% of it. That's typical for a small, depthwise-heavy YOLO-fastest network. Vela's "0.03 Tops/s" matches.

**Memory transfer**

| | Beats | Bytes |
|---|---|---|
| SRAM read | 345,651 | 5.53 MB |
| SRAM write | 214,708 | 3.44 MB |
| Total | 560,359 | 8.97 MB (8.55 MiB) |
| External (`EXT_*`) | 0 | 0 |

| Time base | Read | Write | Total |
|---|---|---|---|
| Measured inference time | 2.25 GB/s | 1.40 GB/s | **3.65 GB/s** |
| `NPU_TOTAL` | 2.43 GB/s | 1.51 GB/s | **3.93 GB/s** (3.66 GiB/s) |
| `NPU_ACTIVE` | 2.58 GB/s | 1.60 GB/s | 4.19 GB/s |

**Comparison with Vela**
- **Bandwidth:** Vela estimated 3.79 GiB/s average SRAM bandwidth, and the hardware measured 3.66 GiB/s over `NPU_TOTAL`, within about 4%.
- **Traffic volume:** Vela estimated 9.05 MiB of SRAM traffic plus 0.50 MiB from "on-chip flash", which together is 9.55 MiB. The measured total is 8.55 MiB, about 10% less. Both counts belong in the comparison because the model data sits at `0x2080000`, which is in SRAM, and the `EXT_*` counters are 0. So weight traffic also went over the SRAM ports.
- **Bus utilisation:** 0.61 beats per cycle against a maximum of 2 is about 31% of peak SRAM bandwidth. Memory isn't the bottleneck, and nearly all of the NPU's time was compute and pipeline time (`NPU_IDLE` was only 6%).

If you want to remove the 400 MHz assumption, print the NPU clock from the clock driver at runtime and recompute from `NPU_TOTAL`.