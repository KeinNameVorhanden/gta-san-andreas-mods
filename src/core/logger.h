// Logging with fixed categories, level filtering, repeat suppression and file rotation.
#pragma once

#include <cstdio>
#include <filesystem>
#include <format>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace sacore {

enum class LogLevel { Trace, Debug, Info, Warn, Error, Fatal };

enum class LogCategory {
    Core,
    Loader,
    GameAdapter,
    Patching,
    Mods,
    Resources,
    Configuration,
    Compatibility,
    Runtime,
    Diagnostics,
};

std::string_view ToString(LogLevel level);
std::string_view ToString(LogCategory category);
bool ParseLogLevel(std::string_view text, LogLevel& out);

class LogSink {
public:
    virtual ~LogSink() = default;
    virtual void Write(std::string_view line) = 0;
};

// Appends lines to a file, flushing every line so a crash loses nothing.
class FileLogSink final : public LogSink {
public:
    explicit FileLogSink(const std::filesystem::path& file);
    ~FileLogSink() override;
    FileLogSink(const FileLogSink&) = delete;
    FileLogSink& operator=(const FileLogSink&) = delete;
    bool IsOpen() const { return file_ != nullptr; }
    void Write(std::string_view line) override;

private:
    FILE* file_ = nullptr;
};

// Keeps the last `capacity` lines in memory (for crash reports) and forwards to another sink.
class RingBufferSink final : public LogSink {
public:
    RingBufferSink(std::unique_ptr<LogSink> next, size_t capacity);
    void Write(std::string_view line) override;
    // Oldest first. Not locked: called from the crash handler when other threads may be stopped.
    std::vector<std::string> Lines() const;

private:
    std::unique_ptr<LogSink> next_;
    std::vector<std::string> lines_;
    size_t next_index_ = 0;
    bool wrapped_ = false;
};

class Logger {
public:
    // `timestamp` produces the line prefix (injectable for tests).
    Logger(std::unique_ptr<LogSink> sink, LogLevel minimum, std::function<std::string()> timestamp = {});

    void Log(LogLevel level, LogCategory category, std::string_view source, std::string_view message);

    template <class... Args>
    void Logf(LogLevel level, LogCategory category, std::string_view source, std::format_string<Args...> fmt,
              Args&&... args) {
        if (level >= minimum_) {
            Log(level, category, source, std::format(fmt, std::forward<Args>(args)...));
        }
    }

    // Emits a pending "repeated N times" note.
    void Flush();
    void SetMinimum(LogLevel level);
    LogLevel Minimum() const { return minimum_; }

private:
    void WriteLine(LogLevel level, LogCategory category, std::string_view source, std::string_view message);
    void EmitRepeatNote();

    std::mutex mutex_;
    std::unique_ptr<LogSink> sink_;
    LogLevel minimum_;
    std::function<std::string()> timestamp_;
    std::string lastKey_;
    LogLevel lastLevel_ = LogLevel::Info;
    LogCategory lastCategory_ = LogCategory::Core;
    std::string lastSource_;
    int repeats_ = 0;
};

// Rotates <dir>/<stem>.log -> <stem>.1.log -> ... keeping `keep` old files; returns the path for the new log.
std::filesystem::path RotateLogFiles(const std::filesystem::path& directory, std::string_view stem, int keep);

}  // namespace sacore
