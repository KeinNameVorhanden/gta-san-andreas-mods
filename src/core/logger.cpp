#include "logger.h"

#include <array>
#include <share.h>
#include <chrono>

namespace sacore {
namespace {

constexpr std::array kLevelNames = {"TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"};
constexpr std::array kCategoryNames = {"Core",      "Loader",        "GameAdapter",   "Patching", "Mods",
                                       "Resources", "Configuration", "Compatibility", "Runtime",  "Diagnostics"};

std::string DefaultTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto local = std::chrono::current_zone()->to_local(now);
    return std::format("{:%Y-%m-%d %H:%M:%S}", std::chrono::floor<std::chrono::milliseconds>(local));
}

}  // namespace

std::string_view ToString(LogLevel level) {
    return kLevelNames[static_cast<size_t>(level)];
}

std::string_view ToString(LogCategory category) {
    return kCategoryNames[static_cast<size_t>(category)];
}

bool ParseLogLevel(std::string_view text, LogLevel& out) {
    for (size_t i = 0; i < kLevelNames.size(); ++i) {
        std::string_view name = kLevelNames[i];
        if (text.size() == name.size() &&
            std::equal(text.begin(), text.end(), name.begin(), [](char a, char b) { return std::toupper(a) == b; })) {
            out = static_cast<LogLevel>(i);
            return true;
        }
    }
    return false;
}

FileLogSink::FileLogSink(const std::filesystem::path& file) {
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    // Shared read access so tools (and the test harness) can read the log while the game runs.
    file_ = _wfsopen(file.c_str(), L"wb", _SH_DENYWR);
}

FileLogSink::~FileLogSink() {
    if (file_) {
        std::fclose(file_);
    }
}

void FileLogSink::Write(std::string_view line) {
    if (!file_) {
        return;
    }
    std::fwrite(line.data(), 1, line.size(), file_);
    std::fputc('\n', file_);
    std::fflush(file_);
}

RingBufferSink::RingBufferSink(std::unique_ptr<LogSink> next, size_t capacity)
    : next_(std::move(next)), lines_(capacity) {}

void RingBufferSink::Write(std::string_view line) {
    if (!lines_.empty()) {
        lines_[next_index_] = std::string(line);
        next_index_ = (next_index_ + 1) % lines_.size();
        wrapped_ = wrapped_ || next_index_ == 0;
    }
    if (next_) {
        next_->Write(line);
    }
}

std::vector<std::string> RingBufferSink::Lines() const {
    std::vector<std::string> out;
    if (wrapped_) {
        out.insert(out.end(), lines_.begin() + static_cast<std::ptrdiff_t>(next_index_), lines_.end());
    }
    out.insert(out.end(), lines_.begin(), lines_.begin() + static_cast<std::ptrdiff_t>(next_index_));
    return out;
}

Logger::Logger(std::unique_ptr<LogSink> sink, LogLevel minimum, std::function<std::string()> timestamp)
    : sink_(std::move(sink)), minimum_(minimum), timestamp_(timestamp ? std::move(timestamp) : DefaultTimestamp) {}

void Logger::Log(LogLevel level, LogCategory category, std::string_view source, std::string_view message) {
    if (level < minimum_) {
        return;
    }
    std::lock_guard lock(mutex_);
    std::string key = std::format("{}|{}|{}|{}", static_cast<int>(level), static_cast<int>(category), source, message);
    if (key == lastKey_) {
        ++repeats_;
        return;
    }
    EmitRepeatNote();
    WriteLine(level, category, source, message);
    lastKey_ = std::move(key);
    lastLevel_ = level;
    lastCategory_ = category;
    lastSource_ = source;
}

void Logger::Flush() {
    std::lock_guard lock(mutex_);
    EmitRepeatNote();
    lastKey_.clear();
}

void Logger::SetMinimum(LogLevel level) {
    minimum_ = level;
}

void Logger::WriteLine(LogLevel level, LogCategory category, std::string_view source, std::string_view message) {
    if (sink_) {
        sink_->Write(std::format("{} [{}] [{}] {}{}{}", timestamp_(), ToString(level), ToString(category), source,
                                 source.empty() ? "" : ": ", message));
    }
}

void Logger::EmitRepeatNote() {
    if (repeats_ > 0) {
        WriteLine(lastLevel_, lastCategory_, lastSource_,
                  std::format("(previous message repeated {} more time{})", repeats_, repeats_ == 1 ? "" : "s"));
        repeats_ = 0;
    }
}

std::filesystem::path RotateLogFiles(const std::filesystem::path& directory, std::string_view stem, int keep) {
    namespace fs = std::filesystem;
    std::error_code error;
    fs::create_directories(directory, error);
    auto numbered = [&](int index) {
        return directory / std::format("{}.{}.log", stem, index);
    };
    fs::remove(numbered(keep), error);
    for (int index = keep - 1; index >= 1; --index) {
        if (fs::exists(numbered(index), error)) {
            fs::rename(numbered(index), numbered(index + 1), error);
        }
    }
    const fs::path current = directory / std::format("{}.log", stem);
    if (keep > 0 && fs::exists(current, error)) {
        fs::rename(current, numbered(1), error);
    }
    return current;
}

}  // namespace sacore
