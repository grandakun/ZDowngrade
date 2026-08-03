# ZDowngrade

Batch-export every SubTool of the current ZBrush Tool to GoZ (`.goz`) files plus a readable `zmeta.txt` manifest, then batch-import them back — restoring SubTool order, folders, and subdivision levels. Useful for moving high-poly work to older ZBrush versions.

**Version:** 1.0.0

> **Disclaimer:** ZDowngrade is built entirely on official ZBrush APIs (ZScript) and the `.goz` format. It contains no cracking, reverse engineering, or license circumvention. Note that Maxon officially does **not** support moving files to an earlier ZBrush version — the documented route is the OBJ format. This tool is a community workaround for that restriction. Use it only with legitimately licensed ZBrush versions.

## Features

- **Export** — all SubTools → `.goz` (at max SDiv) + `zmeta.txt` manifest
- **Import** — batch `.goz` → SubTools in original order (first file replaces the current Tool)
- **Folders** — recreated via `RenameSetNext` + `New Folder` (names should stay ASCII)
- **Subdivision** — rebuilt with `Reconstruct Subdiv` up to the recorded `sdivMax`
- **Unicode-safe paths** — UTF-8 with ACP fallback (Chinese Windows included)

## Install

1. Copy the `ZDowngrade/` folder into `Documents\ZBrush\ZStartup\ZPlugs64\` (Windows).
2. Start ZBrush → menu `ZPlugin > ZDowngrade`.

For a quick test, load `ZDowngrade.txt` directly via `ZScript > Load`.

## Usage

| Button  | Action |
|---|---|
| Export  | Pick a `.goz` save file; every SubTool is exported as `<name>.goz` plus `zmeta.txt` in that folder |
| Import  | Pick any `.goz` in a batch folder; files import in zmeta order, then folders and subdivision levels are restored |
| About   | Version and usage info |

> ⚠ Import replaces the current Tool with the first imported `.goz` — save your work first.

## zmeta.txt

One header line + one `S|` row per SubTool:

```
schema=2;version=1.0.0;count=N;fields=o,n,f,fi,fn,fp,sc,sm,st
S|0|Dog.goz|Dog|-1||0|4|4|17
```

- `o` order, `f` .goz filename, `n` SubTool name
- `fi`/`fn`/`fp` folder index / name / position
- `sc`/`sm` current / max subdivision level
- `st` status bits

The manifest is written as lowercase `zmeta.txt` by `ZDMetaIO64.dll` (ZBrush's `MemSaveToFile` would force `.TXT`).

## Layout

```
ZDowngrade/
├── ZDowngrade.txt          main script
└── ZDowngradeData/
    ├── ZFileUtils64.dll    SubTool helpers (AppendNewSubTool, RenameSetNext)
    └── ZDMetaIO64.dll      zmeta I/O (list, folder/subdiv plans, SaveMetaFile)
```

## Compatibility

- ZBrush 2021–2025, Windows 64-bit
- GoZ (`.goz`) is cross-version stable

## Building the DLLs

Requires MinGW-w64 (e.g. w64devkit):

```
g++ -shared -O2 -static -o ZDMetaIO64.dll ZDMetaIO64.cpp
```
