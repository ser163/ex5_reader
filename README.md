# EX5 Reader

> An e-book reader implementing the RFC EX5-001 protocol — your highlights and notes travel with the book.

[![Platform](https://img.shields.io/badge/platform-Windows%20x64-0078d4)](https://github.com/ser163/ex5_reader)
[![C++](https://img.shields.io/badge/C%2B%2B-17-00599C)](https://isocpp.org/)
[![MSVC](https://img.shields.io/badge/MSVC-2022-CC2929)](https://visualstudio.microsoft.com/)
[![License](https://img.shields.io/badge/license-MIT-green)](LICENSE)

## Overview

EX5 Reader is a **single-binary, zero-runtime-dependency** e-book reader that fully implements the [RFC EX5-001](docs/产品文档.md) protocol (`.ex5` file format v1.0, with v1.1 shared-reading compatibility).

`.ex5` is a **ZIP-based e-book container**: book content (JSON metadata + chapter resources) and reader data (SQLite database) live in the same file. Reading progress, highlights, excerpts, notes, inspirations, reviews, and ratings — **all travel with the book**. Switch devices, switch software, hand it to a friend — your data goes with you.

Two builds are provided:

- **`bin\ex5reader.exe`** — command-line interface, ideal for scripting and protocol debugging
- **`bin\ex5reader_gui.exe`** — graphical interface (native Win32), with mouse-driven highlighting and yellow text rendering

## Features

| Category | Details |
|---|---|
| **Reading** | Streamed loading (huge books without OOM), resume from last position, chapter navigation, night mode, font/size/color/background customization |
| **Annotations** | Highlights (with optional comment), excerpts, notes, inspirations; filter by chapter / type; TXT export; **right-click sidebar entry → view full detail** |
| **Shared Reading** | **v1.1 shared reading**: in unencrypted `.ex5` files, highlights / excerpts / notes / inspirations / reviews / ratings are **read-only shared** among all holders (with author names). Only the original author can edit or delete. |
| **Multi-user** | Built-in `local` user; GUI can create / switch / password-protect additional users. Passwords stored as 16-byte random salt + SHA-256, constant-time compared. |
| **Format** | `.ex5` = ZIP + JSON + SQLite; supports `.txt` / `.html` chapter resources. Encrypted files (`encrypt_scope != 0`) are explicitly rejected. |
| **Extensible** | DLL plugin mechanism (scans `plugins\*.dll`), standard SDK header, API version check, hot-load. Ships with a demo "Reading Stats" plugin. |

## Quick Start

### Requirements

- **Windows 10/11 x64**
- **MSVC 2022** (Community or Build Tools, with the "Desktop development with C++" workload)

### Build

From the repo root:

```cmd
build.bat
```

The script will:
1. Source `vcvars64.bat`
2. Build `ex5reader.exe` (CLI)
3. Build `ex5reader_gui.exe` (GUI, with .rc resources)
4. Build the demo plugin `bin\plugins\demo_stats.dll`

Output lives in `bin\`. Zero runtime dependencies — just double-click to run.

### Run

**GUI (recommended):**

```cmd
bin\ex5reader_gui.exe samples\sample_book.ex5
```

Double-click also works after running `installer\ex5_setup.iss` to register the file association.

**CLI:**

```cmd
bin\ex5reader.exe samples\sample_book.ex5
```

Drops you into an interactive REPL. Type `help` for the command list.

### Make Your Own Book

```cmd
python tools\make_sample.py
```

Generates `samples\sample_book.ex5` (3 chapters, "Sea and Light"), including complete `book_data/`, `resources/`, an empty `read_data.db` (7 tables), and `meta.xml`.

## CLI Commands

```
info                          Book metadata
chapters                      Chapter list
read <ch> [offset] [count]    Read a chapter (auto-records progress)
mark <ch> <start> <end> [cmt] Highlight (optional comment)
excerpt <ch> <start> <end>    Excerpt raw text
note <ch> <content>           Write a note
think <ch> <content>          Write an inspiration
notes / thoughts / reviews    List all records
delnote <id> / delthink <id>  Delete a record
rate <1-5>                    Rate the book
review <content>              Write a review
progress                      Reading progress
save                          Save immediately (rewrite .ex5)
quit                          Auto-save and exit
```

## Project Structure

```
ex5_reader/
├── src/                     Source
│   ├── main.cpp             CLI entry
│   ├── ex5book.h/.cpp       EX5 container I/O layer (ZIP/JSON/SQLite)
│   ├── gui_main.cpp         Main window / toolbar / message loop
│   ├── gui_reader.cpp       Reading area (streamed load / highlight / mark)
│   ├── gui_panel.cpp        Right-side notes panel
│   ├── gui_dialogs.cpp      Notebook / inspirations editor
│   ├── gui_style.cpp        Text style / night mode
│   ├── gui_plugin.cpp       Plugin host
│   ├── ex5_plugin.h         Plugin SDK header
│   └── app.rc / app.manifest
├── plugins/demo/            Demo plugin "Reading Stats" source
├── third_party/             Statically-linked third-party libs
│   ├── miniz/               ZIP I/O (richgel999/miniz)
│   ├── sqlite3.c/.h         SQLite amalgamation
│   └── json.hpp             nlohmann/json
├── samples/                 Sample books
├── docs/                    Product docs, protocol notes
├── installer/               INNO Setup script (.iss)
├── tools/make_sample.py     Sample book generator
└── build.bat                One-shot build script
```

## Tech Stack

| Category | Choice | Rationale |
|---|---|---|
| Language | C++17 | Performance + native system API |
| GUI | Win32 native | Zero dependency, ~1.8 MB binary, < 100 ms cold start |
| ZIP | miniz | Single-header, pure C, statically linked |
| JSON | nlohmann/json | Ergonomic API, many protocol fields |
| Database | SQLite amalgamation | Single .c file, statically linked |
| Crypto | Windows BCrypt | OS-native random salt generation |
| Charset | UTF-8 + handwritten codepoint utils | Chinese paths / content work end-to-end |

## Protocol Conformance

- Implements all required features of [RFC EX5-001 v1.0](docs/产品文档.md)
- Implements the v1.1 draft (§5.4 shared reading): in unencrypted files, `notes` / `inspiration` / `reviews` / `ratings` are read-only-shared across all holders (with author names)
- Encrypted files (`encrypt_scope != 0`) are read and explicitly rejected; decryption is not implemented

See `docs/产品文档.md` and `docs/插件规范.md` for the full design notes.

## Roadmap

- [ ] Remote sync (RFC §5 RESTful API)
- [ ] Encrypted-file decryption (waiting for RFC §4 to stabilize)
- [ ] More plugin samples (translation / TTS / themes)
- [ ] Cross-platform: macOS / Linux (after abstracting the Win32-specific code behind a Platform layer)

## Contributing

PRs are welcome! Please keep:

- C++17-compatible, builds with MSVC 2022
- Follow the existing naming conventions (`snake_case` functions, `PascalCase` types, `kCamelCase` constants)
- Update `docs/产品文档.md` and `docs/插件规范.md` when adding features
- Keep third-party deps single-file, statically linked

## Acknowledgments

- [miniz](https://github.com/richgel999/miniz) by Rich Geldreich
- [nlohmann/json](https://github.com/nlohmann/json) by Niels Lohmann
- [SQLite](https://www.sqlite.org) by D. Richard Hipp
- INNO Setup by Jordan Russell

## License

MIT — see [LICENSE](LICENSE).
