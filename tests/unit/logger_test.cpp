#include "logger.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <string>
#include <vector>

using sacore::LogCategory;
using sacore::Logger;
using sacore::LogLevel;

namespace {

struct MemorySink final : sacore::LogSink {
    std::vector<std::string>* lines;
    explicit MemorySink(std::vector<std::string>* target) : lines(target) {}
    void Write(std::string_view line) override { lines->emplace_back(line); }
};

Logger MakeLogger(std::vector<std::string>& lines, LogLevel minimum = LogLevel::Debug) {
    return Logger(std::make_unique<MemorySink>(&lines), minimum, [] { return std::string("T"); });
}

}  // namespace

TEST_CASE("identical consecutive messages collapse into one line plus a repeat count") {
    std::vector<std::string> lines;
    Logger logger = MakeLogger(lines);
    for (int i = 0; i < 5; ++i) {
        logger.Log(LogLevel::Warn, LogCategory::Patching, "hud", "signature mismatch");
    }
    logger.Log(LogLevel::Info, LogCategory::Core, "", "next");
    logger.Log(LogLevel::Info, LogCategory::Core, "", "next");
    logger.Flush();
    REQUIRE(lines.size() == 4);
    CHECK(lines[0] == "T [WARN] [Patching] hud: signature mismatch");
    CHECK(lines[1] == "T [WARN] [Patching] hud: (previous message repeated 4 more times)");
    CHECK(lines[2] == "T [INFO] [Core] next");
    CHECK(lines[3] == "T [INFO] [Core] (previous message repeated 1 more time)");
}

TEST_CASE("same text in another category is not treated as a repeat; filtered levels never reach the sink") {
    std::vector<std::string> lines;
    Logger logger = MakeLogger(lines, LogLevel::Info);
    logger.Log(LogLevel::Debug, LogCategory::Core, "", "hidden");
    logger.Log(LogLevel::Info, LogCategory::Core, "", "same");
    logger.Log(LogLevel::Info, LogCategory::Mods, "", "same");
    CHECK(lines.size() == 2);
}

TEST_CASE("the ring buffer keeps the last lines in order across wrap-around and still forwards everything") {
    std::vector<std::string> forwarded;
    sacore::RingBufferSink ring(std::make_unique<MemorySink>(&forwarded), 3);
    for (int i = 1; i <= 5; ++i) {
        ring.Write("line " + std::to_string(i));
    }
    CHECK(ring.Lines() == std::vector<std::string>{"line 3", "line 4", "line 5"});
    CHECK(forwarded.size() == 5);
    sacore::RingBufferSink partial(nullptr, 4);
    partial.Write("a");
    partial.Write("b");
    CHECK(partial.Lines() == std::vector<std::string>{"a", "b"});
}

TEST_CASE("rotation keeps the newest files and drops the oldest") {
    std::random_device random;
    const auto dir = std::filesystem::temp_directory_path() / ("sacore_log_" + std::to_string(random()));
    for (int run = 1; run <= 4; ++run) {
        const auto current = sacore::RotateLogFiles(dir, "sacore", 2);
        std::ofstream(current) << "run " << run;
    }
    auto read = [&](const char* name) {
        std::ifstream stream(dir / name);
        std::string text;
        std::getline(stream, text);
        return text;
    };
    CHECK(read("sacore.log") == "run 4");
    CHECK(read("sacore.1.log") == "run 3");
    CHECK(read("sacore.2.log") == "run 2");
    CHECK_FALSE(std::filesystem::exists(dir / "sacore.3.log"));
    std::filesystem::remove_all(dir);
}
