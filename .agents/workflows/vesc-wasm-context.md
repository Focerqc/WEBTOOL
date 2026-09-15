---
description: goal description 
---

Project Goal: We are porting the VESC Tool to a browser-based WebAssembly (WASM) application using Qt 6.11 and Emscripten.
Hardware Constraints: We are completely bypassing native OS hardware APIs. You must aggressively stub out, remove, or disable native QSerialPort and native Bluetooth classes to ensure the WASM compiler does not fail.
Current Phase: We are currently just trying to compile a "dead" non-functional UI. Do not attempt to implement HTML5 Web Serial or Web Bluetooth yet.