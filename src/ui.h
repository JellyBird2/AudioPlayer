// UI - small styled-console helpers (header only, shared across TUs).
// Uses ANSI colors when the console supports VT processing, otherwise falls
// back to SetConsoleTextAttribute. Auto-disables color when stdout is piped.
#pragma once

#include <windows.h>
#include <stdio.h>
#include <string>

namespace UI {

enum Col { RESET_, BOLD_, CYAN_, GREEN_, YELLOW_, RED_, GRAY_, MAGENTA_ };

struct State {
    bool vt = false;
    bool color = true;
    bool ascii = false;
    int lastLen = 0;
};

inline State& UState() {
    static State s;
    return s;
}

inline void Init(bool noColor, bool ascii) {
    State& s = UState();
    s.ascii = ascii;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(h, &mode)) {
        DWORD want = mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        if (SetConsoleMode(h, want)) {
            DWORD check = 0;
            if (GetConsoleMode(h, &check) && (check & ENABLE_VIRTUAL_TERMINAL_PROCESSING))
                s.vt = true;
        }
    } else {
        s.color = false; // piped output: no escape codes
    }
    if (noColor) s.color = false;
}

inline const char* AnsiFor(Col c) {
    switch (c) {
        case BOLD_: return "\x1b[1m";
        case CYAN_: return "\x1b[36m";
        case GREEN_: return "\x1b[32m";
        case YELLOW_: return "\x1b[33m";
        case RED_: return "\x1b[31m";
        case GRAY_: return "\x1b[90m";
        case MAGENTA_: return "\x1b[35m";
        default: return "\x1b[0m";
    }
}

inline WORD WinFor(Col c) {
    switch (c) {
        case BOLD_: return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
        case CYAN_: return FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
        case GREEN_: return FOREGROUND_GREEN | FOREGROUND_INTENSITY;
        case YELLOW_: return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
        case RED_: return FOREGROUND_RED | FOREGROUND_INTENSITY;
        case GRAY_: return FOREGROUND_INTENSITY;
        case MAGENTA_: return FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
        default: return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
    }
}

inline void Set(Col c) {
    State& s = UState();
    if (!s.color) return;
    if (s.vt) {
        fputs(AnsiFor(c), stdout);
    } else {
        SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), WinFor(c));
    }
}

inline void Reset() { Set(RESET_); }

// Unicode block progress bar (or ASCII fallback).
inline std::string Bar(double frac, int width) {
    State& s = UState();
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    int fill = (int)(frac * width + 0.5);
    std::string out;
    if (s.ascii) {
        out.append((size_t)fill, '#');
        out.append((size_t)(width - fill), '-');
    } else {
        for (int i = 0; i < fill; i++) out += "\xE2\x96\x88"; // U+2588 FULL BLOCK
        for (int i = fill; i < width; i++) out += "\xE2\x96\x91"; // U+2591 LIGHT SHADE
    }
    return out;
}

inline std::string TimeStr(long long totalSec) {
    char b[32];
    snprintf(b, sizeof(b), "%lld:%02lld", totalSec / 60, totalSec % 60);
    return std::string(b);
}

inline void Banner() {
    State& s = UState();
    Set(CYAN_);
    if (s.ascii) {
        printf("+------------------------------------------+\n");
        printf("|  AudioPlayer v2  (plays+converts audio)  |\n");
        printf("+------------------------------------------+\n");
    } else {
        printf("\xE2\x95\x94\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x97\n");
        printf("\xE2\x95\x91  \xE2\x99\xAA AudioPlayer v2  (plays+converts audio)  \xE2\x95\x91\n");
        printf("\xE2\x95\x9A\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x90\xE2\x95\x9D\n");
    }
    Reset();
    fflush(stdout);
}

// Single-line redrawn progress: \r + text, padded to erase the old line.
inline void Progress(const char* label, double frac, const char* detail) {
    State& s = UState();
    std::string line = std::string(label) + " " + Bar(frac, 22);
    char pct[16];
    snprintf(pct, sizeof(pct), " %3d%%", (int)(frac * 100 + 0.5));
    line += pct;
    if (detail && *detail) { line += "  "; line += detail; }
    if ((int)line.size() < s.lastLen) line.append((size_t)(s.lastLen - line.size()), ' ');
    s.lastLen = (int)line.size();
    printf("\r%s", line.c_str());
    fflush(stdout);
}

inline void EndProgress() {
    UState().lastLen = 0;
    printf("\n");
    fflush(stdout);
}

inline const char* LoopName(int mode) {
    switch (mode) {
        case 1: return "REPEAT-ALL";
        case 2: return "REPEAT-ONE";
        default: return "REPEAT-OFF";
    }
}

} // namespace UI
