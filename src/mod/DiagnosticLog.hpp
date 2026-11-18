#pragma once

#include <windows.h>
#include <filesystem>
#include <sstream>

namespace whm {

// One file per diagnostic. Keep the writer handle open so other game
// instances cannot race the size check or overwrite a record.
class DiagnosticLog {
public:
    static constexpr DWORD maxBytes = 8 * 1024 * 1024;

    DiagnosticLog() = default;
    DiagnosticLog(const DiagnosticLog &) = delete;
    DiagnosticLog &operator=(const DiagnosticLog &) = delete;
    ~DiagnosticLog() { close(); }

    void open(const std::filesystem::path &path) {
        close();
        file_ = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (!*this) return;

        LARGE_INTEGER end{};
        if (!SetFilePointerEx(file_, {}, &end, FILE_END)) {
            close();
            return;
        }
        if (end.QuadPart > maxBytes) {
            rewind();
        } else {
            size_ = static_cast<DWORD>(end.QuadPart);
        }
    }

    explicit operator bool() const { return file_ != INVALID_HANDLE_VALUE; }

    template<class Format>
    void record(Format format) {
        if (!*this) return;

        std::ostringstream out;
        format(out);
        const auto entry = out.str();
        if (entry.empty() || entry.size() > maxBytes) return;

        const auto length = static_cast<DWORD>(entry.size());
        if (length > maxBytes - size_ && !rewind()) return;

        DWORD written = 0;
        if (!WriteFile(file_, entry.data(), length, &written, nullptr) || written != length) {
            close();
            return;
        }
        size_ += written;
    }

    void close() {
        if (*this) CloseHandle(file_);
        file_ = INVALID_HANDLE_VALUE;
        size_ = 0;
    }

private:
    bool rewind() {
        if (!SetFilePointerEx(file_, {}, nullptr, FILE_BEGIN) || !SetEndOfFile(file_)) {
            close();
            return false;
        }
        size_ = 0;
        return true;
    }

    HANDLE file_ = INVALID_HANDLE_VALUE;
    DWORD size_ = 0;
};

} // namespace whm
