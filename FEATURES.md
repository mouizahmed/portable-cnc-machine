# Portable CNC Machine — Innovative Feature Ideas

---

## UI Architecture: Hybrid Web Interface + Desktop App

### Concept
Inspired by USB-emulated Ethernet web servers used on high-end industrial printers (badge/ID printers
etc.) to serve a configuration interface without drivers or a dedicated app.

Primary access is via **USB CDC-ECM** (virtual network adapter over USB cable). The Pico 2W also has
built-in Wi-Fi for wireless access from phones/tablets on the shop floor.

### USB CDC-ECM (Primary)
```
Laptop
├── Wi-Fi adapter    → normal internet, office network (unchanged)
└── USB adapter      → 192.168.7.1 (Pico only, isolated)
        │
        └── browser → http://192.168.7.1 → Pico web interface
```
- Laptop stays on its own network with full internet — USB adapter is completely separate
- Fully driverless: Windows 10/11, Mac, Linux all support CDC-ECM natively
- USB cable also powers the Pico
- Fixed IP `192.168.7.1` — no configuration needed

### Wi-Fi (Secondary — phones/tablets)
```
Shop Wi-Fi router
    ├── Pico 2W  → http://cnc.local
    └── Phone/tablet on same network
```
- Pico joins shop Wi-Fi and advertises via mDNS as `cnc.local`
- Tech uses phone/tablet browser without needing a cable
- Falls back to AP mode (Pico broadcasts its own network) if no shop Wi-Fi

### Both Run Simultaneously
```
USB cable plugged in  → http://192.168.7.1   (laptop, always works)
On shop Wi-Fi         → http://cnc.local      (phone/tablet, wireless)
```

### Pico Web Interface (day-to-day shop floor use)
- No app to install — works on Windows, Mac, Linux, phone, tablet
- G-code file upload via HTTP multipart directly to SD card
- Real-time DRO and job progress via WebSockets (50ms update rate)
- Toolpath visualization via WebGL/JS (runs in browser, not on Pico)
- Machine configuration and settings page
- Job start / pause / stop / E-STOP

### REST API Endpoints
| Action | Request |
|---|---|
| Start job | `POST /control` `{"cmd":"start"}` |
| Pause | `POST /control` `{"cmd":"pause"}` |
| E-STOP | `POST /control` `{"cmd":"estop"}` |
| Jog axis | `POST /jog` `{"axis":"x","dist":1.0}` |
| Upload G-code | `POST /upload` multipart → streamed to SD |
| List SD files | `GET /files` |
| Select job | `POST /files/select` `{"file":"part.nc"}` |

### Desktop App (advanced use only)
- ML crash prediction and tool wear inference (ML.NET, needs PC resources)
- Advanced toolpath visualizer with ML overlay
- Diagnostics console and error history
- Supervisor-level config and access control

### Pico-Side Stack (C++)
- **lwIP** — TCP/IP stack, bundled in Pico SDK
- **httpd** — lightweight HTTP server, part of lwIP
- **WebSocket** — on top of lwIP
- **LittleFS** — stores HTML/JS/CSS on Pico flash
- **TinyUSB** — CDC-ECM virtual network adapter, bundled in Pico SDK

### Why Hybrid
| Feature | Web Interface | Desktop App |
|---|---|---|
| Basic control + DRO | ✓ | ✓ |
| G-code file transfer | ✓ HTTP multipart | ✓ |
| Toolpath visualization | ✓ WebGL | ✓ Avalonia/OpenGL |
| ML crash prediction | ✗ needs PC | ✓ ML.NET |
| Works from phone/tablet | ✓ | ✗ |
| No install required | ✓ | ✗ |
| Laptop keeps internet | ✓ (separate adapter) | ✓ |

A technician on the shop floor uses a browser on their phone or tablet for day-to-day operation.
The desktop app is only needed for ML-assisted features and advanced diagnostics.

---

## AI-Assisted Features

### Crash Prediction
- Pico streams real-time feed rate + spindle load to desktop
- ML model flags when cutting forces deviate from baseline (tool wear, wrong material, wrong depth)
- Warns before a crash happens, not after
- Training data builds up automatically over jobs

### G-code Optimizer
- Desktop analyzes uploaded G-code and suggests improvements: redundant moves, suboptimal feed rates for detected material
- "Compiler warning" system for G-code

---

## Computer Vision (Webcam)

### Work Zero Detection
- Webcam pointed at bed + OpenCV detects material edges
- Auto-suggests work zero coordinates rather than manual jogging
- Major time saver for new technicians

### In-Job Monitoring
- Watches for smoke, sparks, material shift mid-cut
- Triggers E-STOP automatically via the Pico safety channel

---

## Digital Twin

### Live Toolpath Mirror
- Desktop renders exact current machine position in 3D in real time as the job runs
- If actual position deviates from expected (Teensy reports back), highlights the discrepancy
- Makes lost steps immediately visible

---

## Job Management

### Job History + Analytics
- Every job logged: duration, material, tool, any errors
- Dashboard shows tool life estimates, average job times, failure patterns
- Useful for shops running multiple operators

### Remote Monitoring
- Pico 2W Wi-Fi streams job status + webcam feed to a web dashboard
- Supervisor can monitor from another room or phone

---

## Accessibility for New Technicians

### Safety
- **Pre-job checklist** — modal before START: tool secured? material clamped? work zero set?
- **Soft limits visualization** — show travel boundaries on toolpath, highlight overruns before running
- **Feed/spindle override with hard caps** — admin-configurable max override %

### Guided Workflow
- **Job setup wizard** — step-by-step: load file → verify dimensions → set zero → confirm tool → review → run
- **Contextual tooltips** — every button/field explains what it does and consequences of misuse
- **Plain-English status bar** — "Ready — no job loaded" instead of just "IDLE"

### Toolpath Visualizer
- **Simulated run** — animate toolpath before cutting for visual verification
- **Layer/pass highlight** — color-code roughing vs finishing passes

### Error Recovery
- **Guided E-STOP recovery** — step-by-step checklist to safely resume after an E-STOP
- **Error history log** — timestamped, plain-English errors, exportable for supervisor review

### Access Control
- **Operator vs Supervisor mode** — Supervisor unlocks settings/config; Operator sees simplified job-run UI only

---

## Standout Combination

**Crash Prediction + Computer Vision E-STOP** together would be genuinely novel at this scale.
Most industrial machines rely purely on operator attention. Combining spindle load monitoring
with a vision-based safety layer directly addresses the safety-first design goal of this project.

---

## ML Implementation

### Model Types

| Feature | Model | Library |
|---|---|---|
| Crash prediction | Anomaly detection (Isolation Forest, LSTM) | scikit-learn / PyTorch |
| Tool wear estimation | Regression (Random Forest, XGBoost) | scikit-learn |
| In-job vision monitoring | Lightweight CNN (MobileNet, YOLO-nano) | PyTorch |
| Work zero detection | Classical edge detection | OpenCV |

### Architecture: Python Training → ONNX → .NET Inference

Models are trained in Python and exported to ONNX format. The .NET desktop app loads and
runs inference via ONNX Runtime — no Python required on the end user's machine.

```
Python (train + export)          .NET/Avalonia (load + run)
─────────────────────            ──────────────────────────
scikit-learn / PyTorch
       │
       │  export to .onnx
       ▼
   model.onnx          ───────►  Microsoft.ML.OnnxRuntime
                                 runs inference in C#
```

### .NET NuGet Packages

| Feature | Package |
|---|---|
| Tabular models (crash, tool wear) | `Microsoft.ML` + `Microsoft.ML.OnnxRuntime` |
| Vision models | `Microsoft.ML.OnnxRuntime` |
| Work zero detection | `OpenCvSharp4` |

### Python Export Example (scikit-learn → ONNX)

```python
from sklearn.ensemble import IsolationForest
from skl2onnx import convert_sklearn

model = IsolationForest().fit(training_data)
onnx_model = convert_sklearn(model, ...)
with open("crash_detection.onnx", "wb") as f:
    f.write(onnx_model.SerializeToString())
```

### .NET Inference Example

```csharp
var session = new InferenceSession("crash_detection.onnx");
var result = session.Run(inputs);
```

### Development Split

| Task | Where |
|---|---|
| Data collection | .NET app logs sensor data to CSV |
| Model training | Python (dev machine) |
| Model updates | Ship new `.onnx` file with app update |
| Inference | .NET via ONNX Runtime |

### Low-End Laptop Performance (Core i3, 4GB RAM, no GPU)

| Model | CPU Load | RAM |
|---|---|---|
| Isolation Forest (crash detection) | <1% | ~10MB |
| Random Forest (tool wear) | <1% | ~20MB |
| MobileNet / YOLO-nano at 10fps (vision) | 15–30% | ~80MB |
| OpenCV edge detection | <2% | ~5MB |

All models run on CPU via ONNX Runtime — no GPU required.

### Self-Calibrating Crash Detection

Ship with a **learning mode** that collects data for the first N jobs before enabling predictions.
The model learns the specific machine's normal cutting behavior — more accurate than a generic model.
