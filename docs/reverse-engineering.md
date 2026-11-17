# Reverse-engineering notes

## Artifact examined

- File: `7th Sphere Portscan v1.1.exe`
- Architecture: PE32 / x86, Windows GUI subsystem
- Size: 25,088 bytes
- SHA-256: `3fb58064fa4dfa0af8606f103253645b49a8fa609e9a0462f7aeac7ce1c7614d`
- The original executable is not in this repository.

The UI was read from the embedded dialog templates. Code behavior was reviewed with Ghidra pseudocode, x86 disassembly, and imported API/resource references. Generated pseudocode is retained under `analysis/`.

## Evidence and implementation

| Behavior | Evidence | Rebuild |
|---|---|---|
| Original is a Windows GUI | Dialog 102, caption “7th Sphere PortScan 1.1”, 28 controls | Native Win32 GUI window; command-line mode is an added rebuild feature |
| Host and port-range input | Resource IDs 1000, 1001, 1018; host resolution at 0x4028c0 | Host plus inclusive start/stop ports |
| IPv4 name resolution | `inet_addr`, `gethostbyname`, `inet_ntoa` references | `GetAddrInfoW` with AF_INET |
| Asynchronous TCP scanning | MFC socket event paths and event mask 0x31 in the scan timer path | Nonblocking Winsock connect plus `WSAPoll` |
| Open-connection cap | Global counter compared against 0xDC (220) | Maximum 220 outstanding sockets |
| Quick Scan | Start code copies the checkbox state into each socket; successful connect closes the socket in the quick path | Same visible behavior |
| Receive/banner results | Socket receive callback requests up to 50 bytes | Waits up to 1.8 seconds for initial service data |
| Live socket count | Separate timer counts the 220 occupancy slots every 500 ms | Count is refreshed from active connections |
| Halt and result saving | Halt callback checks Reset-on-halt and Save-on-halt; save routine reads the results list | Matching controls and file output |

The original event handlers use MFC asynchronous sockets. The rebuild uses a worker thread with nonblocking Winsock sockets and polling. That preserves the observed connect/receive flow without copying the old MFC object layout.

The command-line mode reuses the rebuild's GUI controls and scanner worker through a hidden window so the scan behavior and result serialization stay shared. Command-line parsing, console output, Ctrl+C handling, and its exit codes are new rebuild behavior; none of those were present in the original application's GUI-only interface.

## Rebuild choices and limits

- The original defaults the delay field to 20 ms and the Stop Port field to 65536. The rebuild preserves both values; 65536 is treated as the sentinel after TCP port 65535. It caps concurrent connections at the observed 220.
- The original reads and writes UI controls through MFC calls whose exact wrapper signatures are not fully recovered. The rebuild updates Send Port to the current attempt and Recv Port to the latest successful connection based on the observed control IDs and callback writes.
- The original timeout behavior is not fully recovered. The rebuild applies a 4-second connect timeout and an 1.8-second banner wait so stale sockets cannot hold the run forever.
- Debug output and error strings are reconstructed from string references and socket error mapping, not copied from original source.
- The original About dialog includes a personal email address. This rebuild omits that contact detail.

## Relevant recovered addresses

- `0x00401000`: connect-event handling, including CONNECT result and quick-scan close branch
- `0x00401360`: close-event result handling
- `0x00401650`: receive callback, reads up to 50 bytes
- `0x004022B0`: start-scan setup
- `0x00402930`: timer-driven socket creation and port progression
- `0x00402730`: halt/cleanup path
- `0x00402ED0`: result-file save routine
- `0x004028C0`: IPv4 address/name resolution

The labels above are behavioral interpretations of the decompilation. The machine-generated code is preserved with original Ghidra function names where available.
