# GPU selection and measurements

These are **local, hardware-specific measurements from 2026-10-18**, not
performance targets or a general GPU compatibility matrix. The test machine's
drivers, PCIe topology, output and encoder capabilities are listed below. A
second Parsec account did not receive and play these streams over the network.

Capture GPU and Encoding GPU are independent persistent settings. Same as
capture GPU (`encoding_gpu=auto`) keeps the original handle-sharing path.
An explicitly selected AMD/Intel card uses VA-API, including a headless card.
The display, compositor and games remain on the capture/display GPU.

## This machine, 2026-10-18

- RTX 5070: `0000:01:00.0`, DP-1 connected.
- RX 570: `0000:05:00.0`, amdgpu, no connected display, PCIe 3.0 x1 link.
- Kernel `7.3.0-rc6-2-cachyos-rc`, Mesa `26.2.4`, NVIDIA `615.78.08`.
- AMD VA-API exposes H.264/HEVC eight-bit encode; HEVC ten-bit is decode only.
- Added the missing `vulkan-radeon` package for device discovery. Polaris lacks
  the Vulkan DRM modifier import extension required by the normal GPU pipeline.

NVIDIA-converted frame import into AMD VA-API was refused. AMD-side Vulkan
conversion was also refused for the missing import extension. The working
cross-GPU path therefore explicitly copies NV12 through system memory; encoding
still runs on AMD hardware. It does not move the Linux desktop to AMD.

## Bounded local tests

Same 1920x1080 capture, 120 frames, H.264, 20 Mbps, serial capture/encode:

| Path | Average total |
|---|---:|
| RTX 5070 NVENC | 1.174 ms/frame |
| RX 570, first staging prototype | 29.279 ms/frame |
| RX 570, cached RAM and latency quality | 18.989 ms/frame |

Cached readback reduced median convert/readback from 11.954 to 1.680 ms.
These are serial probes, not network latency, sustained throughput or game-FPS
measurements. Comparisons are valid only for the tested hardware/software setup.
The real host stream loop was tested separately with a local seat:

- H.264, requested 60 FPS: 231 frames in a five-second run including startup.
- HEVC, requested 144 FPS: 342 frames in a five-second run including startup.
- Both streams decoded to the null sink in FFmpeg without errors.

Startup took approximately 0.7 s. Short tests cannot establish sustained frame
pacing, 144 FPS, remote playback or unchanged game performance. The tested
configuration chose AMD encoding with these limits disclosed. Requested FPS
remains a ceiling, not a guarantee.

Switching GPU or capture settings saves automatically and requires Stop hosting
then Start hosting. Select Same as capture GPU to return to RTX NVENC. If the
RX 570 belongs to a Windows VFIO VM, stop Linux hosting before assigning it.
No GPU driver rebinding, global desktop GPU settings or clocks are changed by
this feature. The diagnostic attempt to read the power profile returned busy;
no power-profile write took place.
