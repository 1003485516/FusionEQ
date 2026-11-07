#!/usr/bin/env python3
"""Make JUCE 7.0.12 build against the macOS 15 SDK (macos-15 runners).

JUCE 7.0.12's juce_gui_basics/native/juce_Windowing_mac.mm calls
CGWindowListCreateImage(), which Apple marked "obsoleted in macOS 15.0" in the
macOS 15 SDK -> a hard compile error, not a deprecation warning. This breaks
juceaide during CMake configure, and would also break the plug-in build.

Window snapshots are not used by Fusion EQ. Replace the single function
createNSWindowSnapshot() with a null Image so both juceaide and the plug-in
compile. Idempotent: safe to run on every CI invocation and on cached JUCE.

Usage: python3 installer/patch_juce_macos15.py "$JUCE_DIR"
"""
import os
import sys

MARKER = "FUSION_MACOS15_PATCH"
START = "static Image createNSWindowSnapshot (NSWindow* nsWindow)"
END = "Image createSnapshotOfNativeWindow"

REPLACEMENT = """static Image createNSWindowSnapshot (NSWindow* nsWindow)
{
    // FUSION_MACOS15_PATCH: JUCE 7.0.12 calls CGWindowListCreateImage, which the
    // macOS 15 SDK marks unavailable (hard error). Fusion EQ never takes window
    // snapshots, so return a null Image. Keeps JUCE 7.0.12 usable on newer
    // runners without upgrading to JUCE 8 (which removed APIs this project uses).
    (void) nsWindow;
    return Image();
}

"""


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: patch_juce_macos15.py <JUCE_DIR>")
    path = os.path.join(sys.argv[1], "modules", "juce_gui_basics",
                        "native", "juce_Windowing_mac.mm")
    with open(path, "r", encoding="utf-8") as f:
        src = f.read()

    if MARKER in src:
        print("[*] JUCE already patched for macOS 15 SDK")
        return
    if START not in src or END not in src:
        raise SystemExit("[X] unexpected JUCE source layout: " + path)

    start = src.index(START)
    end = src.index(END, start)
    with open(path, "w", encoding="utf-8") as f:
        f.write(src[:start] + REPLACEMENT + src[end:])
    print("[*] patched:", path)


if __name__ == "__main__":
    main()
