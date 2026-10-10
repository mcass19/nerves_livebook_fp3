# Adreno 506 compute benchmarks

`gpubench` measures what the Fairphone 3 GPU delivers through Mesa
(freedreno) as headless GLES 3.1 on `/dev/dri/renderD128`. `cpupeak` gives
the NEON baseline of the same SoC.

```sh
./build.sh ~/Projects/nerves_system_fp3/.nerves/artifacts/nerves_system_fp3-portable-0.3.0
# copy gpubench / cpupeak to the phone, then:
echo 725000000 > /sys/class/devfreq/1c00000.gpu/min_freq      # pin the GPU clock
echo 10000 > /sys/kernel/debug/dri/1a01000.display-controller/hangcheck_period_ms
./gpubench info bw alu2 fsalu ssbo gemm conv
./cpupeak 4,5,6,7
```

`GPUBENCH_DEBUG=1 FD_MESA_DEBUG=shaderdb` prints ir3 register and wave
statistics per shader; `GPUBENCH_VERBOSE=1` prints wrong gemm samples.

## Results (FP3, Mesa 26.1.8, GPU at 725 MHz, 2026-10-10)

SSBO access is broken on a4xx/a5xx since Mesa 25.3: element `i` is
addressed at byte `i`, so every GLES compute result is wrong.
`0003-ir3-pass-a4xx-a5xx-SSBO-accesses-a-byte-offset-again.patch` fixes it;
the numbers below were taken with it applied.

| Test | fp32 | fp16 |
| --- | --- | --- |
| FMA peak, fragment shader | 30.8 GFLOPS | 61.6 GFLOPS |
| FMA peak, compute shader | 11.5–15.3 GFLOPS | 22.9–30.0 GFLOPS |
| GEMM 512³, shared-memory tiled | 9.35 GFLOPS | 6.97 GFLOPS |
| GEMM 512³, register-blocked | 2.65 GFLOPS | 1.53 GFLOPS |
| Direct conv 3×3 (YOLO-sized layers) | 3.4 GFLOPS | 3.1 GFLOPS |
| Bandwidth, write / copy | 5.8 / 4.1 GB/s | |
| CPU NEON FMA peak, 4× Kryo Gold / 4× Silver | 18 / 6.4 GFLOPS | no fp16 ALU (ARMv8.0) |

- fp16 really runs at twice the fp32 rate (ir3 allocates half registers).
- Compute shaders reach less than half the ALU throughput of fragment
  shaders on identical code. Mesa's a5xx compute setup, not the hardware,
  is the limit.
- The GPU clock and the CX voltage vote follow devfreq (CX level 256 idle,
  384 at 725 MHz).
- The published ~125 GFLOPS fp32 figure is not reachable; the measured
  ceiling is ~31 GFLOPS fp32 / ~62 GFLOPS fp16.

YOLOv5n (4.5 GFLOP per frame) would take ~480 ms at the tiled-GEMM rate
and ~1.3 s with direct convolution, against ~300 ms on the CPU today.
