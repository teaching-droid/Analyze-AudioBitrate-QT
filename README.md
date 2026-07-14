# Analyze Audio Bitrate (GUI)

A Windows desktop app that scans a folder tree of video files, measures the real
audio bitrate of each file, and flags files that are corrupt or that don't match
the rest of their folder. Built with Qt 6 and C++.

It is a GUI version of the Analyze-AudioBitrate.ps1 PowerShell script. Same
analysis, same error checks, but with a folder picker, a live results table with
colour coding, a progress bar, and one-click report export.

## What it does

Point it at a folder and it walks every subfolder looking for `.mp4`, `.mkv`,
`.avi`, `.mov`, `.ts`, and `.m4v` files. For each file it runs ffprobe to read
the audio stream, then sums every audio packet to work out the true data rate
(the header value is often wrong or missing, so the packet sum is what actually
tells you the bitrate). While it does this it also catches files that fail to
probe or that break part way through.

Files are compared within their own folder, not across the whole tree. So if you
have 20 episodes at 192 kbps and 4 at 96 kbps in the same folder, the 4 odd ones
are highlighted. A different folder with a different norm is judged on its own.

### Colour coding in the table

- Green: bitrate matches the rest of the folder (within 10%).
- Red: bitrate is different from the folder majority (an outlier).
- Dark red row: the file is corrupt or could not be read, with the error type.

### Two bitrate columns

- **Audio Rate (kbps)**: the true rate measured from a full packet scan.
- **Header (kbps)**: the rate the container claims in its header.

When these two disagree, the file was usually remuxed or mislabelled.

### Error types

MOOV/MOOF missing, truncated, corrupt data, bad header, DTS errors, unknown
codec, no audio, and `MID-FILE:` errors that only show up during the full scan.

## Reports

The **Export TXT** and **Export CSV** buttons write the same reports the
PowerShell script produced: a formatted text report with per-folder tables and a
defective-files summary, plus a CSV for sorting and filtering in a spreadsheet.

## Languages

The interface is available in English, German, Japanese, and Italian. It follows
the system language on first run and can be changed at any time from the Language
menu.

## What you need to run it

- **Windows 10 or 11, 64-bit.**
- **FFmpeg's `ffprobe`** (version 4.0 or newer). This is the only external tool
  the app uses, and it is not bundled (FFmpeg is a separate project). Download
  FFmpeg, then do one of:
  - point the app at `ffprobe.exe` using the field at the top of the window, or
  - set the `FFPROBE_PATH` environment variable, or
  - put `ffprobe.exe` on your `PATH` and the app finds it automatically.

You do **not** need Qt or Python installed. A release build (made with
`windeployqt`, or a release zip) already ships the Qt runtime DLLs next to
`AudioBitrateGUI.exe`, so it runs on a clean machine. Just unzip and run the
exe, then set the ffprobe path once.

## Building from source

You need Qt 6 (Widgets), CMake 3.16 or newer, and a C++17 compiler.

### Windows (MSVC + Qt 6)

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 ^
      -DCMAKE_PREFIX_PATH="C:/Qt/6.8.3/msvc2022_64"
cmake --build build --config Release
```

The Release build runs `windeployqt`, so `build/Release/AudioBitrateGUI.exe`
ships next to its Qt DLLs and runs on a machine without Qt installed.

### Linux / macOS

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH="$HOME/Qt/6.8.3/gcc_64"
cmake --build build -j
```

## Project layout

```
src/
  main.cpp             entry point
  MainWindow.*         window: pickers, table, progress, export, language menu
  ScanWorker.*         background thread: recurse, group by folder, find outliers
  FfprobeAnalyzer.*    ffprobe calls, bitrate maths, error classification
  ReportWriter.*       TXT and CSV report writers
translations/
  abr_de.ts abr_ja.ts abr_it.ts   editable translation sources
```

To edit a translation, change the text in the matching `.ts` file. The build
compiles it and embeds the result, so a rebuild is all that is needed.

## License

MIT. See [LICENSE](LICENSE).
