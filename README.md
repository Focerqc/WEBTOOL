# VESC® Tool Web (WebAssembly Port)

> **Note to VESC® Project Maintainers & Community:**
> This repository is an experimental port of the official **VESC® Tool** to **WebAssembly (WASM)** using **Qt 6.11 (Multithreaded)** and **Emscripten**.
> Our primary goal is to provide a zero-install, cross-platform browser experience for configuring and monitoring VESC® motor controllers and accessories over **Web Bluetooth (WebBLE)** and **Web Serial (USB)** directly on desktop browsers, Chromebooks, Android tablets, and iPads.
>
> We welcome your review, feedback, architectural guidance, and hope to upstream improvements to make browser-based VESC tooling an official, first-class citizen!

---

## 🚀 Work Accomplished So Far

1. **Full Qt 6.11 Multithreaded WebAssembly Build Pipeline**:
   - Compiled VESC Tool's core engine, Qt Quick / QML interfaces, and protocol decoders into WebAssembly using Emscripten.
   - Configured `SharedArrayBuffer` support with full `Cross-Origin-Opener-Policy: same-origin` and `Cross-Origin-Embedder-Policy: require-corp` header isolation.

2. **Native Web Hardware Transport Bridges**:
   - **Web Bluetooth (WebBLE)**: Direct BLE connection from the browser to VESC Express, NRF52 modules, and built-in BLE chips without requiring native OS BLE drivers or background daemons.
   - **Web Serial**: Direct USB serial CDC communication via Chrome/Edge `navigator.serial`.
   - Bidirectional C++ / JavaScript packet bridge (`bleuart_wasm.cpp`, `wasm_serial_bridge.cpp`) integrated into `VescInterface`.

3. **Responsive Mobile & Desktop UIs**:
   - Seamless switching between desktop layout and touch-optimized mobile layouts (smartphones, iPad, Android tablets).
   - Real-time telemetry streaming (RT Data, Refloat, IMU gauges, BMS monitors).

4. **Web File System Access API Integration**:
   - In-browser backup and restore for motor configs (`mcconf`), app configs (`appconf`), and custom board XMLs.
   - Saves organized JSON/XML backup bundles directly to the user's chosen local folder via modern browser File System Access API.

5. **CAN Bus Forwarding & Multi-Device Isolation**:
   - Fixed CAN device discovery and routing when connected locally via VESC Express or NRF to remote ESCs (e.g. Thor400v2, Little FOCer) and BMS units (e.g. VBMS32-micro).
   - Separated local hardware tracking (`mLocalFwParams`) from active CAN target parameters (`mLastFwParams`), preventing temporary CAN scan probes from corrupting active controller profiles.

6. **WASM-Optimized IMU Calibration & Wizards**:
   - Re-engineered the IMU setup wizard and Gyroscope/Accelerometer calibration screens to use reactive QML properties, ensuring real-time value updates under Qt 6 QML.
   - Converted internal IMU hardware detection from blocking synchronous terminal requests into asynchronous command/response pipelines to prevent browser UI freezing.

---

## ⚡ Key Technical Challenges & Main Struggles

1. **Absence of Synchronous Event Loops in WebAssembly**:
   - In native desktop Qt builds, VESC Tool frequently relies on synchronous blocking helpers like `Utility::waitSignal()`, `sendTerminalCmdSync()`, or nested `QEventLoop::exec()` while waiting for hardware responses.
   - In WebAssembly, blocking the main thread immediately deadlocks the browser's JavaScript event loop, halting WebBLE packet reception and Web Serial I/O.
   - **Ongoing effort**: Systematically refactoring synchronous wizards (motor detection, CAN pinging, terminal commands) into asynchronous signal-slot workflows.

2. **WebBLE Bandwidth Pacing & Flow Control**:
   - Web Bluetooth does not provide deep driver-level buffering. Sending rapid back-to-back requests (e.g., 50Hz IMU polling or unthrottled CAN scans) saturates browser queues and causes dropped packets.
   - Requires careful interval tuning (e.g., 40ms polling intervals) and packet batching to maintain rock-solid wireless stability.

3. **Browser Security Sandboxing (Cross-Origin Isolation & Permissions)**:
   - Multithreaded WebAssembly requires `SharedArrayBuffer`, which modern browsers strictly prohibit unless served over HTTPS with strict `COOP`/`COEP` security headers.
   - Hardware APIs (`navigator.bluetooth`, `navigator.serial`) require explicit user activation (gestures) and cannot be opened spontaneously from background threads.

4. **Qt 6 QML Property Reactivity Differences**:
   - In Qt 6 QML, assigning an updated JavaScript object back to a `var` property no longer triggers change notifications if the object pointer reference is unchanged.
   - Required explicitly migrating live telemetry and wizard state to primitive QML properties.

---

## 🤝 Next Steps & Review Points for VESC Leaders

- Reviewing the asynchronous communication patterns to see if they can be unified with the official upstream code.
- Feedback on the Web File System backup architecture and whether a standardized REST/WebSocket or WebTransport fallback should be adopted.
- Discussion on trademark compliance, hosting guidelines, and potential official inclusion under the VESC Project umbrella.

---

# OLD README:

# VESC® Tool

This is the source code of VESC Tool. A pre-compiled binary of both the stable release as well as the development release packaged with all the matching firmware for all supported hardware can be downloaded at http://vesc-project.com/

The stable binary is available for **Linux**, **Windows**, **MacOS**, **Android** and **iOS**. The development binary is available for **Linux**, **Windows** and **Android** and is updated every few days.

All binaries can also be downloaded free-of-charge for all platforms except for iOS, which only is available via the Apple App Store as they do not allow any other distribution channel.

## Code Contribution, Distribution and Trademark Usage

VESC is a registered trademark of Benjamin Vedder. Read the [trademark policies](https://vesc-project.com/trademark_policies) for more information.

The "official" binary release of VESC Tool is done via VESC Project only, as that gives users a way to verify that releases, that use the registered VESC trademark, originate from the VESC Project. It is not ok to host a binary release on a different channel and use the VESC trademark for that release.

It is ok to use the github fork function to make contributions to the code. That is because 1) it is the most convenient way to make contributions and 2) the forked repository states clearly that it is a fork and points back to the main repository where the original code can be found. Further, it is easy to see what the code changes are from the forked repository compared to the main repository via github, but that information is lost in a binary release.

Forks of VESC Tool on github are not encouraged to provide a binary release in the repository. That is because there is no way to tell the final binary apart from the official release once downloaded. Further, packaging the firmware, which has to be done as an additional step from a different repository, also cannot be verified whether it is done correctly.

**Forks without branding**  

Because the topic came up, here are some words about forking VESC Tool and removing the branding.  

If you make a fork of VESC Tool and remove all traces of the VESC trademark you are not breaking the trademark policies, but we still do not encourage that. The reason is that such forks are 1) confusing to users and 2) divert users away from the VESC Project itself and take away the opportunity to learn about it and to make donations if they choose to. For example, the majority of VESC donations today come from VESC Tool downloads.  

If you see a missing feature and you want to put in some work and make that feature available, we would appreciate if you contribute that back to the main VESC repositories. That way there is only one consistent and compatible release for everyone that is managed by the main authors of the VESC code who make the vast majority of the development. It also gives the main authors, who are the most familiar with the code, a chance to review features to make sure that they are as safe as possible and don't break other parts of the functionality.

## Add Your Hardware to the Binary Release

If you have custom hardware and you want to add support for it in the official release of VESC Tool, you can use the following steps:

1) Go to https://github.com/vedderb/bldc and use the github fork function.  
2) Make your changes, test them and make a pull request to the main repository.  
3) If the pull request gets accepted your hardware will become part of the next official release. It will show up in the binary beta typically after a few days and in the stable version the next time a stable release is made.

## Development

**Note:** These instructions build VESC Tool without the BLDC firmwares bundled.

### Linux

Make sure that the required dependencies are installed. There is some advice in the [build_lin](./build_lin) file's comments. If you have Nix installed see below.

```shell
qmake -config release "CONFIG += release_lin build_original exclude_fw"
make -j8
./build/lin/vesc_tool_6.06
```

### Nix

The most easy way to build and run VESC Tool is to just run the provided program:

```shell
nix run
```

This will rebuild the program from scratch on each invokation. To enter a build environment with the dependencies installed for building it manually with QMake, run

```shell
nix develop
```

Then follow the normal build instructions for Linux.

**Note:** The Nix flake's outputs currently only supports x86 Linux.

### Starting QT Creator in Nix

QT Creator allows you to easily build and run the project. It also allows you to edit the page UIs with it's graphical editor. To run it using Nix simply start QT Creator from a shell with the build dependencies:

```shell
nix develop
nix run nixpkgs#qtcreator
```

This makes sure that QT Creator has access to the required dependencies.
