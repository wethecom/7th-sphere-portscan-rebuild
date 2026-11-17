# 7th Sphere PortScan 1.1 Rebuild

An independent 64-bit Windows rebuild of the 1997 **7th Sphere PortScan 1.1** GUI, with an added command-line mode. The implementation is new C++17 code informed by static analysis of the original Windows application.

> This is a behavioral rebuild, not recovered original source code. The original executable is not included.

## Download and run

The current Windows x64 build is in [`dist/7th-sphere-portscan-rebuild.exe`](dist/7th-sphere-portscan-rebuild.exe). Run it with no arguments to open the resizable GUI.

## Command-line use

Pass a target to run a scan without opening the GUI. For example, in PowerShell:

```powershell
.\dist\7th-sphere-portscan-rebuild.exe `
  --target 192.168.1.20 `
  --start-port 1 `
  --stop-port 1024 `
  --delay 20 `
  --quick `
  --output "$env:USERPROFILE\Documents\portscan.txt" `
  --overwrite
```

`--target` is required. Use `--help` to print the full option list. Short aliases are available for target (`-t`), start port (`-s`), stop port (`-e`), delay (`-d`), output (`-o`), quick scan (`-q`), and help (`-h`).

| Option | Default | Meaning |
| --- | --- | --- |
| `--target HOST` | Required | Host name or IPv4 address |
| `--start-port PORT` | `1` | First TCP port to try; valid range is 1–65535 |
| `--stop-port PORT` | `65536` | Last port; `65536` is the original full-range marker and scans through 65535 |
| `--delay MS` | `20` | Milliseconds between new attempts; valid range is 1–60000 |
| `--output FILE` | `%USERPROFILE%\Documents\SCANLOG.TXT` | Results file; a relative path is relative to the current directory |
| `--quick` | Off | Close each successful connection promptly instead of waiting for a service banner |
| `--debug` | Off | Include connection errors and debug rows in console output and saved results |
| `--overwrite` | Off | Replace the output file; without it, results are appended |

The command-line mode prints scan results as it runs. Ctrl+C halts the scan and saves the results gathered so far. Exit codes are `0` for a saved scan, `1` for startup failure, `2` for invalid arguments, `3` for an unresolved target, `4` for a save failure, and `130` when interrupted.

## GUI features

- Resizable Win32 window with the original control names and scan workflow.
- Target, inclusive start/stop ports, delay, live socket count, and latest connected port.
- Quick Scan, Debug, Scroll, Reset on halt, Save on halt, and Overwrite options.
- Results list with Save and Clear controls.
- UTF-8 text output; existing files are appended to unless Overwrite is checked.

The default range is ports 1 through 65535. The original Stop Port value `65536` is accepted as a full-range end marker. The scanner uses nonblocking IPv4 TCP connections, starts no more than 220 concurrent attempts, and launches new attempts at the selected delay. With Quick Scan off, it waits up to 1.8 seconds for up to 50 bytes of initial service data; connection attempts time out after 4 seconds.

## Build from source

Requirements:

- 64-bit Windows
- CMake 3.22 or newer
- 64-bit MinGW-w64 C++17 toolchain (`g++` on `PATH`)
- PowerShell

From the repository directory, run:

```powershell
.\build.ps1
```

The script configures CMake, builds the GUI executable, and writes it to `dist/7th-sphere-portscan-rebuild.exe`. The MinGW C++ runtime is statically linked; the program still uses Windows system libraries.

## Verification

The current build was exercised against a temporary localhost TCP listener: the CLI reported the open test port, saved the requested output file, and exited successfully. A separate loopback harness is available at [`tests/smoke.ps1`](tests/smoke.ps1) for open-banner and closed-port checks.

## What was reconstructed

Static analysis of the original 32-bit Windows GUI informed the control layout, IPv4 resolution, asynchronous TCP connect/receive flow, 220-socket limit, quick-scan behavior, and save/reset options. The rebuild uses a new Win32 interface and a worker thread with nonblocking Winsock sockets and `WSAPoll`.

Some behavior could not be recovered exactly. In particular, the original timeout details and some MFC control interactions are uncertain; rebuild choices are documented in [`docs/reverse-engineering.md`](docs/reverse-engineering.md). The optional command-line mode is new and was not part of the original GUI.

The [`analysis/`](analysis/) directory contains human-readable Ghidra pseudocode and disassembly/listing extracts used during reconstruction. They are incomplete, can contain decompiler mistakes, and are evidence rather than original source. The `tools/` directory contains scripts used to extract those artifacts. Neither the original executable nor the portable Ghidra installation is included.

## License and provenance

The MIT license in this repository applies to the new implementation and project-authored build and documentation files. It does not grant rights to the original 1997 program or other third-party material. Analysis extracts are included as research notes; no claim is made that they are the original source code.

Only scan systems and networks you own or have permission to assess.
