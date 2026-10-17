# BluetoothBridge (Windows 10/11, C++20)

A native Windows desktop prototype for quick Bluetooth radio control, device enumeration, and a clean entry point for file transfer.

## Current functionality

- Native Win32 UI.
- Reads Bluetooth radio state using `Windows.Devices.Radios`.
- Requests permission and toggles the Bluetooth radio through the supported Windows API. Hardware switches and Windows policy can deny a change.
- Enumerates Bluetooth device endpoints known to Windows.
- Opens Windows' built-in OBEX send wizard (`fsquirt.exe -send`) and receiver wizard as compatibility fallbacks.

## Important status / honest limitations

This is an initial source prototype, **not yet the finished unattended-transfer product**. The device list currently enumerates endpoints returned by Windows; it does not promise an active inquiry of every nearby device. The send button currently delegates the OBEX operation to Windows' inbox wizard. The receive button opens the Windows receiver rather than running a permanent custom OBEX Object Push server.

A true no-click receiver requires a packaged/appropriately-capable Windows app that advertises an OBEX Object Push service, implements OBEX PUT/session handling, validates incoming names and sizes, chooses a safe save folder, and handles pairing/consent. It should be opt-in, run only when enabled, and display incoming transfer prompts. This should be implemented and tested against Windows 10/11, Android, and another Windows PC before claiming interoperability.

## Build with Clang on Windows

Requirements:
- Windows 10/11
- Visual Studio Build Tools or Visual Studio with **Desktop development with C++** and a Windows 10/11 SDK
- LLVM/Clang for Windows (`clang-cl`)
- CMake 3.25+ and Ninja

Open **x64 Native Tools Command Prompt for VS**, ensure `clang-cl`, `cmake`, and `ninja` are on PATH, then run:

```bat
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=clang-cl -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The executable will be `build\BluetoothBridge.exe`.

The build must be done on Windows with the Windows SDK because this environment has no Windows SDK or Windows cross-compiler. The project has therefore **not been compiled or runtime-tested here**. Do not distribute it as a finished binary until built and tested on a real Windows machine.

## Build automatically with GitHub Actions

The workflow at `.github/workflows/windows-clang.yml` builds the x64 Release executable on a GitHub-hosted Windows runner with `clang-cl`, CMake, Ninja, and the Windows SDK. It runs on pushes to `main`/`master`, pull requests, and manual dispatch.

To use it:
1. Push this project to a GitHub repository.
2. Open the repository's **Actions** tab and select **Build BluetoothBridge (Windows Clang)**.
3. For a manual build, select **Run workflow**.
4. Open the completed run and download the `BluetoothBridge-windows-x64-clang` artifact. It contains `BluetoothBridge.exe`, `README.md`, and a source ZIP. Artifacts are retained for 30 days.

The workflow produces a Windows binary; it does not imply the prototype's Bluetooth transfer features have been fully implemented or tested on all devices.
