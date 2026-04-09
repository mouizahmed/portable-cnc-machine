# Progress Timeline and Deliverables: Desktop GUI Application

---

## Technology Selection: Why .NET C# + Avalonia?

| Option | Why Not |
|--------|---------|
| **Electron** | Too heavy (200MB+ RAM), poor serial port support, slow startup |
| **Qt** | GPL licensing requires open-sourcing code or expensive commercial license, C++ adds complexity |
| **WPF** | Windows only - does not meet cross-platform requirement for macOS/Linux users |
| **Flutter** | Desktop support still maturing, Dart ecosystem limited for hardware/industrial applications |
| **Python (Tkinter/PyQt)** | Interpreted language slower for real-time UI updates, difficult to package as standalone executable, PyQt has same GPL licensing issues as Qt |
| **Java (JavaFX)** | Requires JVM installation (~150MB), dated UI appearance, verbose boilerplate code, slower startup time |

**Why Avalonia + .NET C#:**
- ✅ Cross-platform (Windows, macOS, Linux) from single codebase
- ✅ Native serial port support via `System.IO.Ports` for USB CDC communication
- ✅ Lightweight (~60MB RAM vs Electron's 200MB+), fast startup
- ✅ Strong typing catches errors at compile time - important for safety-critical CNC code
- ✅ MIT license (free for commercial use, no royalties)
- ✅ MVVM architecture with XAML data binding for clean, maintainable code
- ✅ Self-contained deployment - no runtime installation required by end user

---

## GUI Features Overview

### Dashboard
- Real-time Digital Readout (DRO) showing X, Y, Z positions
- Machine state indicators (Motion Controller + Safety Supervisor)
- Spindle speed and feed rate display
- Program progress bar with current line number
- Environmental monitoring (temperature, humidity)
- Start / Pause / Stop / Home control buttons
- G-code visualization area (placeholder)

### Jog Control
- XY directional pad (X+, X-, Y+, Y-)
- Z axis controls (Z+, Z-)
- Step size presets: 0.01, 0.1, 1, 10, 50 mm
- Adjustable jog feed rate
- Continuous jog mode option
- Home commands: Home All, Home X, Home Y, Home Z
- Zero/Work offset: Zero All, X=0, Y=0, Z=0

### File Management
- G-code file browser (.gcode, .nc formats)
- File preview with G-code content display
- Load file to machine
- Refresh and delete operations

### Connection
- USB CDC Serial port selection
- Baud rate configuration (9600 - 1,000,000)
- Connect / Disconnect / Test controls
- Device status display (Pico 2W, Teensy 4.1)
- Firmware version information

### Settings
- Steps per mm (X, Y, Z axes)
- Maximum feed rates per axis
- Acceleration settings per axis
- Travel limits (max X, Y, Z)
- Soft/Hard limits enable
- Spindle min/max RPM
- Temperature warning thresholds
- Save / Load / Apply / Import / Export

### Diagnostics (Advanced Mode)
- Command console with log output
- Manual G-code/GRBL command input
- Quick command buttons ($$, ?, $#, etc.)
- Sensor readings (CPU temp, ambient, spindle, humidity, vibration)
- Driver status indicators (X, Y, Z, Spindle)
- Limit switch status display
- Fault reset and E-Stop unlock

### Global UI
- Header bar with dual state indicators
- E-STOP button (always visible)
- Sidebar navigation with quick DRO
- Status bar with current feed/temp/page
- Dark theme optimized for workshop use
- Cross-platform support (Windows, macOS, Linux)

---

## 30% Completion of GUI (Desktop App)
**Progress Level: 30%**

**Completed:**
- Avalonia UI framework setup with cross-platform support (Windows, macOS, Linux)
- Project structure established (MVVM pattern with ViewModels and Views)
- Main window shell template with header, sidebar navigation, and content area
- State machine enums defined (MotionState, SafetyState matching SRS)
- Basic navigation structure between pages
- Initial theme/styling foundation (dark mode base)
- ViewModelBase and RelayCommand implementations

**Work to be Done:**
- Finalize UI/UX design mockups
- Refine color scheme and visual styling
- Implement proper icons (replace emoji placeholders)
- User testing for layout and usability

**End Date for Section:** February 14, EOD

---

## 60% Completion of GUI (Desktop App)
**Progress Level: 0%**

**Completed:** None

**Work to be Done:**
- USB CDC Serial communication implementation (connect to Pico 2W)
- Real-time position data display from machine
- G-code file loading and parsing
- Basic G-code validation (syntax check)
- Functional jog controls sending commands to Pico
- Settings persistence (save/load configuration)
- Error handling and user notifications
- Connection state management

**End Date for Section:** February 20, EOD

---

## 100% Completion of GUI (Desktop App)
**Progress Level: 0%**

**Completed:** None

**Work to be Done:**
- G-code 3D toolpath visualization renderer
- Real-time sensor data polling and display
- Complete Start/Pause/Stop/E-Stop functionality
- File browser dialog integration
- Keyboard shortcuts for jog controls
- Polished final UI design
- Cross-platform testing (Windows, macOS)
- Unit testing for ViewModels
- User documentation

**End Date for Section:** February 28, EOD

---

## Current State: UI Template/Scaffold

The current implementation provides a **structural template** with:

### Template Pages Created:
| Page | Description | Functional Status |
|------|-------------|-------------------|
| Dashboard | Layout with DRO, controls, status panels | Template only |
| Jog Control | XY pad, Z controls, step presets | Template only |
| Files | File list and preview layout | Template only |
| Connect | Serial port selection UI | Template only |
| Settings | Machine parameter forms | Template only |
| Diagnostics | Console and sensor display | Template only |

### What the Template Provides:
- Page layouts and component structure
- Data binding infrastructure (ViewModels)
- Navigation system
- Placeholder controls and displays
- State machine definitions

### What Still Needs Implementation:
- Actual serial communication
- Real machine data integration
- G-code parsing and visualization
- Settings persistence
- Final visual design and polish

---

## Technical Foundation

### Architecture
```
PC (Desktop App) ──USB CDC Serial──► Pico 2W ──I²C/UART──► Teensy 4.1
                                        │                      │
                                   Sensors/LCD            Stepper Drivers
```

### Technology Stack
| Component | Technology |
|-----------|------------|
| Framework | Avalonia UI 11.3 |
| Runtime | .NET 10 |
| Pattern | MVVM |
| Serial | System.IO.Ports (to be implemented) |
| Platforms | Windows, macOS, Linux |

---

## USB CDC Connection Protocol (PC ↔ Pico 2W)

### Connection Parameters
| Parameter | Value |
|-----------|-------|
| Interface | USB CDC (Virtual COM Port) |
| Baud Rate | 115200 (default) |
| Data Bits | 8 |
| Parity | None |
| Stop Bits | 1 |
| Flow Control | None |

### Port Identification
| Platform | Port Format | Example |
|----------|-------------|---------|
| Windows | COMx | COM3, COM4 |
| macOS | /dev/tty.usbmodem* | /dev/tty.usbmodem14101 |
| Linux | /dev/ttyACM* | /dev/ttyACM0 |

### Message Format
```
<COMMAND>:<PARAMETERS>\n
```
- All messages terminated with newline (`\n`)
- Parameters separated by commas if multiple
- Responses prefixed with status indicator

### Command Set (PC → Pico 2W)

#### Connection & Status
| Command | Description | Example |
|---------|-------------|---------|
| `PING` | Connection test | `PING\n` |
| `STATUS` | Request full status | `STATUS\n` |
| `VERSION` | Get firmware versions | `VERSION\n` |

#### Motion Control (Relayed to Teensy)
| Command | Description | Example |
|---------|-------------|---------|
| `GCODE:<line>` | Send G-code line | `GCODE:G0 X10 Y20\n` |
| `JOG:<axis>,<dist>,<feed>` | Jog axis | `JOG:X,10.0,500\n` |
| `HOME:<axis>` | Home axis (ALL/X/Y/Z) | `HOME:ALL\n` |
| `STOP` | Immediate stop | `STOP\n` |
| `PAUSE` | Feed hold | `PAUSE\n` |
| `RESUME` | Resume from hold | `RESUME\n` |
| `RESET` | Reset/unlock machine | `RESET\n` |

#### Settings
| Command | Description | Example |
|---------|-------------|---------|
| `SET:<param>,<value>` | Set parameter | `SET:FEED_RATE,500\n` |
| `GET:<param>` | Get parameter | `GET:FEED_RATE\n` |

#### File Transfer
| Command | Description | Example |
|---------|-------------|---------|
| `FILE:START:<name>,<lines>` | Begin file transfer | `FILE:START:part.gcode,150\n` |
| `FILE:LINE:<n>:<gcode>` | Send file line | `FILE:LINE:1:G21\n` |
| `FILE:END` | End file transfer | `FILE:END\n` |
| `FILE:RUN` | Run loaded file | `FILE:RUN\n` |

### Response Format (Pico 2W → PC)

#### Status Responses
| Response | Description |
|----------|-------------|
| `OK` | Command accepted |
| `OK:<data>` | Command accepted with data |
| `ERR:<code>:<message>` | Error occurred |
| `BUSY` | Cannot process (machine busy) |

#### Periodic Status Updates
```
STATUS:MOTION:<state>,SAFETY:<state>,X:<pos>,Y:<pos>,Z:<pos>,F:<feed>,S:<spindle>
```

Example:
```
STATUS:MOTION:IDLE,SAFETY:MONITORING,X:10.000,Y:20.000,Z:5.000,F:500,S:0
```

#### Sensor Data Updates
```
SENSOR:TEMP:<value>,HUM:<value>,VIB:<value>
```

Example:
```
SENSOR:TEMP:24.5,HUM:45.0,VIB:0.02
```

#### Event Notifications
| Event | Description |
|-------|-------------|
| `EVENT:LIMIT:<axis>` | Limit switch triggered |
| `EVENT:ESTOP` | E-Stop activated |
| `EVENT:FAULT:<code>` | Fault condition |
| `EVENT:COMPLETE` | Program completed |
| `EVENT:HOMED:<axis>` | Homing complete |

### Error Codes
| Code | Description |
|------|-------------|
| `E01` | Invalid command |
| `E02` | Invalid parameter |
| `E03` | Communication timeout |
| `E04` | Teensy not responding |
| `E05` | Motion in progress |
| `E06` | Limit switch triggered |
| `E07` | E-Stop active |
| `E08` | File transfer error |

### Connection Sequence
```
1. PC opens COM port
2. PC sends: PING\n
3. Pico responds: OK:PICO2W\n
4. PC sends: VERSION\n
5. Pico responds: OK:PICO:1.0.0,TEENSY:1.2.3,GRBL:1.1h\n
6. PC sends: STATUS\n
7. Pico responds: STATUS:MOTION:IDLE,SAFETY:SAFE_IDLE,...\n
8. Connection established, begin normal operation
```

### Polling Intervals
| Data Type | Interval |
|-----------|----------|
| Position (DRO) | 100ms |
| Machine State | 250ms |
| Sensor Data | 1000ms |

---

## File Structure
```
desktop/
├── App.axaml                    # Application resources & DataTemplates
├── App.axaml.cs                 # Application entry point
├── Program.cs                   # Main entry
├── desktop.csproj               # Project configuration
├── Views/
│   ├── MainWindow.axaml(.cs)    # Main shell
│   └── Pages/
│       ├── DashboardView.axaml(.cs)
│       ├── JogView.axaml(.cs)
│       ├── FilesView.axaml(.cs)
│       ├── ConnectView.axaml(.cs)
│       ├── SettingsView.axaml(.cs)
│       └── DiagnosticsView.axaml(.cs)
└── ViewModels/
    ├── MainWindowViewModel.cs   # Central state management
    ├── ViewModelBase.cs         # INPC base class
    ├── RelayCommand.cs          # ICommand implementation
    ├── Enums.cs                 # MotionState, SafetyState, ConnectionStatus
    ├── Converters.cs            # Value converters for UI
    ├── DashboardViewModel.cs
    ├── JogViewModel.cs
    ├── FilesViewModel.cs
    ├── ConnectViewModel.cs
    ├── SettingsViewModel.cs
    └── DiagnosticsViewModel.cs
```
