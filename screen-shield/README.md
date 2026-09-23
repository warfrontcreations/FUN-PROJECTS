# Screen Shield

A tiny, native Windows overlay that hides a region of your screen from screenshots and screen recordings — while that region stays fully visible to you in person.

Drag it over whatever you don't want captured (a password, an API key, a chat window, anything). It looks like a plain black box on your own monitor, but tools that respect Windows' capture-exclusion API (Snipping Tool, `PrintScreen`, Xbox Game Bar, OBS's *Windows Graphics Capture* source, Teams/Zoom screen share) will render that region as solid black instead of what's really there.

## How it works

Windows exposes [`SetWindowDisplayAffinity`](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowdisplayaffinity) with the `WDA_EXCLUDEFROMCAPTURE` flag — the same mechanism password managers like 1Password use to keep vault windows out of screenshots. Screen Shield creates a small always-on-top window, marks it with that flag, and lets you park it over sensitive content.

## Why native, not Python/.NET

Earlier attempts at this used Python (`tkinter`) and then .NET (WinForms). Both required a runtime the target machine might not have installed, which turned into real friction (missing file associations, "install .NET to continue" prompts). This version is a single self-contained Win32 executable — **no Python, no .NET, no installer** — built with `gcc` via `mingw-w64` and statically linked, so it only depends on `user32.dll`, `gdi32.dll`, `comdlg32.dll`, and `kernel32.dll`, which ship with every copy of Windows.

## Requirements

- Windows 10 build 19041 (version 2004) or later, or Windows 11.
- Nothing else. `ScreenShield.exe` runs as-is.

## Usage

Download `ScreenShield.exe` and double-click it. A black box appears in the middle of your screen.

| Action | How |
| --- | --- |
| Move it | Drag anywhere on the box |
| Resize it | Drag the bottom-right corner |
| Collapse to a small tab | Click `-` (click again, or right-click → *Collapse/Restore*, to bring it back) |
| Close it | Click `x`, or right-click → *Quit* |
| Change color / opacity / click-through / lock position / toggle capture-exclusion | Right-click anywhere on the box |

**Click-through** lets mouse clicks pass through to whatever is underneath, so you can still interact with the app you're hiding a piece of.

## ⚠️ Test before you rely on it

Capture-exclusion behavior can vary by Windows build and by which capture method a given tool uses under the hood. **Before relying on this for anything sensitive**, take a screenshot or recording with the exact tool you plan to use and confirm the shielded area actually comes out solid black.

It will **not** protect against:
- Someone photographing your monitor.
- Remote Desktop / VNC sessions viewing your screen remotely.
- Older capture tools that use legacy GDI `BitBlt` instead of the modern Windows Graphics Capture API.

Full opacity (the default) is the most reliably-excluded setting. Lowering opacity is for your own viewing convenience and hasn't been verified against every capture tool — re-test after changing it.

## Building from source

No dependencies beyond a C compiler that targets Win32. Cross-compiling from Linux with `mingw-w64`:

```bash
sudo apt install mingw-w64
x86_64-w64-mingw32-gcc -O2 -Wall -mwindows -static -static-libgcc \
  -o ScreenShield.exe screen_shield.c \
  -luser32 -lgdi32 -lcomdlg32 -lkernel32
```

Or natively on Windows with MSYS2/MinGW, or MSVC's `cl.exe` (link against the same four libraries).

## License

MIT — do whatever you want with it.
