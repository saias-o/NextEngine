#pragma once

#include <iostream>
#include <utility>

namespace ne {

// Minimal leveled logger. Header-only; streams its arguments to stdout/stderr
// behind a tag. Replace the sink here later (file, callback) without touching
// call sites: Log::info("loaded ", n, " vertices");
class Log {
public:
    template <typename... Args>
    static void info(Args&&... args) { line(std::cout, "[info] ", std::forward<Args>(args)...); }

    template <typename... Args>
    static void warn(Args&&... args) { line(std::cerr, "[warn] ", std::forward<Args>(args)...); }

    template <typename... Args>
    static void error(Args&&... args) { line(std::cerr, "[error] ", std::forward<Args>(args)...); }

private:
    template <typename... Args>
    static void line(std::ostream& os, const char* tag, Args&&... args) {
        os << tag;
        (os << ... << args);  // C++17 fold
        os << std::endl;
    }
};

} // namespace ne
