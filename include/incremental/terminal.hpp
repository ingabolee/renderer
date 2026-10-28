// include/incremental/terminal.hpp
// Terminal rendering utilities (colors, box drawing, alignment).
// This header defines the styling constants and utility types.
// The actual rendering logic is in src/terminal.cpp.
#pragma once

#include <string>
#include <string_view>
#include <ostream>
#include <vector>
#include <cstdio>
#include <algorithm>

// Detect Windows console
#ifdef _WIN32
#  define INCREMENTAL_WINDOWS 1
#  include <io.h>  // _isatty, _fileno
#else
#  include <unistd.h> // isatty
#endif

namespace incremental::term {

// ── Color mode ────────────────────────────────────────────────────────────────
enum class ColorMode { Auto, Always, Never };

// ── TermConfig ────────────────────────────────────────────────────────────────
struct TermConfig {
    ColorMode color{ColorMode::Auto};
    bool ascii{false};          ///< Use ASCII instead of Unicode box chars
    bool quiet{false};          ///< Suppress informational output
    bool verbose{false};        ///< Extra debug output
    int  width{80};             ///< Target line width for wrapping/boxes
};

// ── Global config (set once at startup by CLI) ────────────────────────────────
inline TermConfig& config() {
    static TermConfig cfg;
    return cfg;
}

// ── Terminal detection ────────────────────────────────────────────────────────
[[nodiscard]] inline bool is_tty(FILE* stream = stdout) noexcept {
#ifdef INCREMENTAL_WINDOWS
    return _isatty(_fileno(stream)) != 0;
#else
    return isatty(fileno(stream)) != 0;
#endif
}

[[nodiscard]] inline bool no_color_env() noexcept {
    const char* nc = std::getenv("NO_COLOR");
    return nc != nullptr;
}

[[nodiscard]] inline bool use_color() noexcept {
    switch (config().color) {
    case ColorMode::Always: return !no_color_env();
    case ColorMode::Never:  return false;
    case ColorMode::Auto:   return is_tty() && !no_color_env();
    }
    return false;
}

// ── ANSI escape helpers ───────────────────────────────────────────────────────
namespace ansi {
    inline std::string reset()   { return use_color() ? "\033[0m"    : ""; }
    inline std::string bold()    { return use_color() ? "\033[1m"    : ""; }
    inline std::string dim()     { return use_color() ? "\033[2m"    : ""; }
    inline std::string cyan()    { return use_color() ? "\033[36m"   : ""; }
    inline std::string green()   { return use_color() ? "\033[32m"   : ""; }
    inline std::string yellow()  { return use_color() ? "\033[33m"   : ""; }
    inline std::string red()     { return use_color() ? "\033[31m"   : ""; }
    inline std::string magenta() { return use_color() ? "\033[35m"   : ""; }
    inline std::string blue()    { return use_color() ? "\033[34m"   : ""; }
    inline std::string white()   { return use_color() ? "\033[97m"   : ""; }
    inline std::string b_green() { return use_color() ? "\033[1;32m" : ""; }
    inline std::string b_red()   { return use_color() ? "\033[1;31m" : ""; }
    inline std::string b_cyan()  { return use_color() ? "\033[1;36m" : ""; }
    inline std::string b_white() { return use_color() ? "\033[1;97m" : ""; }
} // namespace ansi

// ── Box drawing characters ────────────────────────────────────────────────────
namespace box {
    inline std::string tl()  { return config().ascii ? "+" : "\u256D"; } // ╭
    inline std::string tr()  { return config().ascii ? "+" : "\u256E"; } // ╮
    inline std::string bl()  { return config().ascii ? "+" : "\u2570"; } // ╰
    inline std::string br()  { return config().ascii ? "+" : "\u256F"; } // ╯
    inline std::string h()   { return config().ascii ? "-" : "\u2500"; } // ─
    inline std::string v()   { return config().ascii ? "|" : "\u2502"; } // │
    inline std::string tee() { return config().ascii ? "-" : "\u2500"; } // ─ (section sep)
    inline std::string check(){ return config().ascii ? "[OK]" : "\u2713"; } // ✓
    inline std::string cross(){ return config().ascii ? "[NG]" : "\u2717"; } // ✗
    inline std::string arrow(){ return config().ascii ? "->" : "\u2192"; }   // →
    inline std::string bullet(){ return config().ascii ? "*" : "\u2022"; }   // •
    inline std::string dash() { return config().ascii ? "=" : "\u2550"; }    // = or ═
} // namespace box

// ── Padding utilities ─────────────────────────────────────────────────────────
[[nodiscard]] inline std::string repeat(const std::string& s, int n) {
    std::string r;
    r.reserve(s.size() * static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) r += s;
    return r;
}

[[nodiscard]] inline std::string pad_right(std::string_view s, int width) {
    std::string result{s};
    while (static_cast<int>(result.size()) < width) result += ' ';
    return result;
}

[[nodiscard]] inline std::string pad_left(std::string_view s, int width) {
    std::string result{s};
    while (static_cast<int>(result.size()) < width) result = " " + result;
    return result;
}

// ── Rendering primitives ─────────────────────────────────────────────────────
// These are implemented inline to keep terminal.hpp self-contained
// when used in the CLI src files.

inline void print_banner(std::ostream& out) {
    const int W = config().width;
    std::string inner_top  = box::tl() + repeat(box::h(), W - 2) + box::tr();
    std::string inner_bot  = box::bl() + repeat(box::h(), W - 2) + box::br();

    auto line = [&](std::string_view text) {
        std::string padded = "  " + std::string(text);
        while (static_cast<int>(padded.size()) < W - 2) padded += ' ';
        out << ansi::b_cyan() << box::v() << ansi::reset()
            << ansi::b_white() << padded << ansi::reset()
            << ansi::b_cyan() << box::v() << ansi::reset() << '\n';
    };

    out << ansi::b_cyan() << inner_top << ansi::reset() << '\n';
    line("INCREMENTAL / RECALC v1.0.0");
    line("Dependency-tracked computation engine");
    out << ansi::b_cyan() << inner_bot << ansi::reset() << '\n';
}

inline void print_section(std::ostream& out, std::string_view title) {
    const int W = config().width;
    std::string t = std::string(" ") + std::string(title) + " ";
    int fill = W - static_cast<int>(t.size()) - 3;
    if (fill < 0) fill = 0;
    out << '\n'
        << ansi::cyan() << box::tee() << box::tee() << ansi::reset()
        << ansi::b_white() << t << ansi::reset()
        << ansi::cyan() << repeat(box::tee(), fill) << ansi::reset()
        << '\n' << '\n';
}

inline void print_kv(std::ostream& out,
                     std::string_view key,
                     std::string_view value,
                     int key_width = 24)
{
    out << "  " << ansi::dim() << pad_right(key, key_width) << ansi::reset()
        << "  " << ansi::white() << value << ansi::reset() << '\n';
}

inline void print_ok(std::ostream& out, std::string_view msg) {
    out << "  " << ansi::b_green() << box::check() << ansi::reset()
        << "  " << msg << '\n';
}

inline void print_err(std::ostream& out, std::string_view msg) {
    out << "  " << ansi::b_red() << box::cross() << ansi::reset()
        << "  " << ansi::red() << msg << ansi::reset() << '\n';
}

inline void print_warn(std::ostream& out, std::string_view msg) {
    out << "  " << ansi::yellow() << box::bullet() << ansi::reset()
        << "  " << ansi::yellow() << msg << ansi::reset() << '\n';
}

inline void print_info(std::ostream& out, std::string_view msg) {
    out << "  " << ansi::cyan() << box::bullet() << ansi::reset()
        << "  " << msg << '\n';
}

// ── Table renderer ────────────────────────────────────────────────────────────
struct TableRow {
    std::string col1;
    std::string col2;
    std::string col3; // optional
};

inline void print_table(std::ostream& out,
                        const std::vector<TableRow>& rows,
                        int col1_w = 24, int col2_w = 14)
{
    for (const auto& row : rows) {
        out << "  " << ansi::dim() << pad_right(row.col1, col1_w) << ansi::reset()
            << "  " << ansi::white() << pad_left(row.col2, col2_w) << ansi::reset();
        if (!row.col3.empty()) {
            out << "  " << ansi::dim() << row.col3 << ansi::reset();
        }
        out << '\n';
    }
}

} // namespace incremental::term
