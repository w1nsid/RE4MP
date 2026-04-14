# RE4MP — Resident Evil 4 Multiplayer Mod

A multiplayer mod for **Resident Evil 4** (2005 PC) that lets two players share the same game world over the internet. Each player's Leon is mirrored as a partner character (Ashley) on the other player's screen. A lightweight UDP relay server forwards position packets between the two clients. No VPN or Hamachi required.

Works with both **v1.06** and **v1.1.0** of the game — no downgrade needed.

## How It Works

```
  Player A                    Dedicated Server                    Player B
  RE4 + RE4MP.dll  ──UDP──►  re4mp-server (relay)  ◄──UDP──  RE4 + RE4MP.dll
     Leon pos  ────────────►  forwards to other  ◄────────  Leon pos
   Partner  ◄────────────────  player's position  ──────────►  Partner
```

The mod injects a DLL into the game process, hooks into the partner-AI subsystem, and replaces the partner's built-in behaviour with networked position data from the other player. It uses runtime signature scanning to locate game functions, making it version-independent.

## In-Game Controls

| Key | Action |
|-----|--------|
| **F5** | Manual partner activation (usually auto-acquired) |
| **End** | Unload the mod |

## Requirements

| Component | What you need |
|-----------|---------------|
| **Game** | Resident Evil 4 (2005 PC release) |
| **Players** | Windows 10 or later |
| **Server** | Any Linux or Windows machine with a public IP or port-forwarded UDP port |
| **Build tools** | Visual Studio 2022+ with C++ Desktop workload; gcc for the Linux server |

## Building

### Windows (DLL + Injector + Server)

```bat
build.bat
```

Or open `RE4MP.sln` in Visual Studio, set **Debug | Win32**, and build.

Output goes to `build/`: `RE4MP.dll`, `RE4MPInjector.exe`, `RE4MPServer.exe`.

### Linux Server

```bash
cd RE4MPServer
make
```

Produces `re4mp-server`. Cross-compile for Windows with `make windows` (requires `mingw-w64`).

## Setup

### 1. Start the Server

```bash
./re4mp-server 27015
```

The server listens on UDP port 27015, accepts exactly two clients, and relays packets between them. Clients that go silent for 30 seconds are automatically evicted.

A systemd unit file is included at `RE4MPServer/re4mp-server.service` for running as a user service.

Make sure **UDP port 27015** is open / forwarded on your server.

### 2. Configure the Client

Place `re4mp.ini` next to `RE4MP.dll`:

```ini
[RE4MP]
ServerIP=re4mp.example.com
ServerPort=27015
```

`ServerIP` accepts an IP address or a DNS hostname.

### 3. Play

1. Launch Resident Evil 4 on both machines.
2. Run `RE4MPInjector.exe` as **Administrator** on both machines.
3. Load into an active gameplay level (e.g. the village in Chapter 1).
4. The partner character spawns automatically.
5. Move around — your Leon appears as the partner on the other player's screen.

## Project Layout

```
RE4MP.sln                     Visual Studio solution
re4mp.ini                     Client config (ship next to RE4MP.dll)
build.bat                     One-click Windows build script
RE4MP/                        Game DLL (injected into RE4)
  RE4MP.cpp                   Entry point, main loop, network I/O
  Config.h                    INI config reader (Windows API)
  Network.h                   UDP client networking (non-blocking, DNS)
  SigScan.h                   Runtime IDA-style byte pattern scanner
  hooks.h                     Game function hooks & pointer resolution
  Re4Detours.h                Detour hooks (partner pathfinding)
  Cache.h                     Shared global state
  detours.cpp / detours.h     Microsoft Detours 3.0
  lib/detours.lib             Precompiled Detours library (x86)
RE4MPInjector/                Injector EXE
  RE4MPInjector.cpp           Finds game window & injects DLL
RE4MPServer/                  Dedicated relay server
  re4mp_server.c              Single-file C server (Linux / Windows)
  Makefile                    Linux build
  re4mp-server.service        systemd unit file
```

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| Partner doesn't appear | Make sure you're in an active gameplay level. Try pressing F5. |
| Injector says "window not found" | Launch the game first, then run the injector. |
| No connection on server | Check `re4mp.ini` is next to `RE4MP.dll` with the correct IP/hostname and port. Verify UDP 27015 is forwarded. |

Client logs are written to `re4mp.log` next to the DLL.

## License

This project is for educational and personal use. Resident Evil 4 is a trademark of Capcom.
