# Storage Upload Performance Notes

## MVP Decision

USB upload to Pico SD storage is good enough for MVP.

Current measured throughput for `desktop/samples/test_2mb.gcode`:

| Build state | Size | Total | Throughput | Notes |
|---|---:|---:|---:|---|
| Baseline binary upload | 2,097,173 bytes | 18.8 s | 111 KB/s | Desktop blocked mostly in USB serial writes. |
| Pico upload worker queue | 2,097,173 bytes | 18.1 s | 116 KB/s | Small improvement; queue absorbs some SD stalls. |
| Preallocation enabled | 2,097,173 bytes | 18.3 s | 115 KB/s | `f_expand()` works, but allocation was not the main bottleneck. |

This means a 2 MB G-code upload takes about 18 seconds. Larger files scale roughly linearly; a 10 MB file may take around 90 seconds. That is acceptable for MVP if uploads are occasional and typical job files are small.

## Current Implementation

The upload path uses binary USB CDC transfer frames:

- Desktop sends 4096-byte upload data frames.
- Desktop keeps up to 16 chunks in flight.
- Pico validates sequence/session state and queues chunks.
- Pico core 1 writes queued chunks to FatFS/SD.
- Pico ACKs committed chunks every 8 chunks, or at file end.
- Desktop finalizes with CRC-32, and Pico validates final size/CRC before reporting success.

Relevant constants:

| Component | Value | Location |
|---|---:|---|
| Transfer chunk size | 4096 bytes | `PicoProtocolService.BinaryTransferChunkSize`, `UsbCdcTransport::kMaxTransferPayloadSize` |
| Desktop upload window | 16 chunks | `FilesViewModel.UploadWindowSize` |
| Pico upload queue | 16 chunks | `DesktopProtocol::kUploadQueueCapacity` |
| Pico upload ACK stride | 8 chunks | `DesktopProtocol::kUploadAckStride` |

## Preallocation

FatFS `f_expand()` is enabled for the Pico build by copying the SDK FatFS sources into `pico2W/build/fatfs` and patching generated `ffconf.h`:

```c
#define FF_USE_EXPAND 1
```

The repo does this in `pico2W/CMakeLists.txt` instead of editing the SDK install. Upload setup calls:

```cpp
f_expand(&transfer_.file(), size, 1)
```

before `FILE_UPLOAD_READY`. If preallocation fails for a non-empty upload, the upload is rejected before data transfer starts and the partial file is removed.

Measured result:

```text
PREALLOC_MS=39
WRITE_MS=12787
TOTAL_MS=18257
```

Preallocation works, but it did not materially improve total upload speed. It should remain enabled because it makes allocation failure deterministic before the transfer starts.

## Bottleneck

The bottleneck is Pico-side SD/FatFS write throughput, not desktop CPU or ACK traffic.

Representative desktop profile:

```text
total_ms=18262
send_ms=17694
serial_write_ms=17694
wait_ack_ms=324
serial_build_ms=12
serial_cobs_ms=27
serial_wire_ms=20
```

Representative Pico profile:

```text
TOTAL_MS=18257
PREALLOC_MS=39
WRITE_MS=12787
MAX_WRITE_MS=393
QUEUE_MAX=13
BPS=114869
```

Interpretation:

- Desktop CPU work is negligible.
- Desktop blocks in `BaseStream.Write()` because Pico/USB buffers back up.
- Pico upload queue gets near full, which means the USB side can outpace SD writes.
- Pico spends most of the transfer in FatFS/SD write calls.
- `f_expand()` is fast, so hot-path cluster allocation is not the dominant cost.

## Experiments Tried

### ACK Stride 16

Changing Pico upload ACK stride from 8 to 16 was compatible but not kept.

Reason: ACK wait was not the bottleneck, and larger ACK spacing reduces progress/error responsiveness.

### Core 1 Upload Worker

Kept.

This decouples USB receive/decode from blocking SD writes. It improved throughput slightly and gives useful queue-pressure telemetry.

### FatFS Preallocation

Kept.

This did not improve throughput materially, but it fails early on allocation problems and records `PREALLOC_MS`.

### SD Multi-Block Write

Reverted.

A simple `CMD25` multi-block write path was tested and made the measured 2 MB upload slower. Do not reintroduce without lower-level profiling.

## MVP Acceptance Checks

Before tagging the MVP firmware, verify:

- Upload a new file.
- Upload the same file with overwrite.
- Abort an upload from the TFT.
- Upload with SD removed or unavailable.
- Load the uploaded file as the active job.
- Delete an uploaded file.
- Confirm upload diagnostics include `STORAGE_UPLOAD_PROFILE`.
- Confirm failed uploads remove partial files.

## Post-MVP Work

If upload speed becomes important, prioritize:

1. Add SD driver timing around `disk_write()`, `send_command()`, `write_data_block()`, and `wait_ready()`.
2. Compare at least one known-good SD card formatted FAT32 with 32 KB allocation units.
3. Test higher `SD_SPI_BAUD` values if wiring is stable.
4. Revisit SD multi-block writes with detailed timing instead of a blind `CMD25` swap.
5. Consider direct TinyUSB CDC for bulk transfer only after storage throughput is understood.

Do not spend more MVP time tuning USB CDC windows or ACK spacing unless profiles show `wait_ack_ms` dominating total time.
