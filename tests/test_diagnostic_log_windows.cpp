#include "DiagnosticLog.hpp"

#include <cassert>
#include <fstream>

int main() {
    wchar_t folder[MAX_PATH]{};
    wchar_t filename[MAX_PATH]{};
    const auto folderLength = GetTempPathW(MAX_PATH, folder);
    assert(folderLength > 0 && folderLength < MAX_PATH);
    assert(GetTempFileNameW(folder, L"whm", 0, filename));
    const std::filesystem::path path(filename);

    {
        whm::DiagnosticLog log;
        log.open(path);
        assert(log);
        whm::DiagnosticLog other;
        other.open(path);
        assert(!other);
        log.record([](auto &out) { out << std::string(whm::DiagnosticLog::maxBytes, 'x'); });
    }
    assert(std::filesystem::file_size(path) == whm::DiagnosticLog::maxBytes);

    {
        whm::DiagnosticLog log;
        log.open(path);
        assert(log);
        log.record([](auto &out) { out << "new\n"; });
    }
    assert(std::filesystem::file_size(path) == 4);

    {
        whm::DiagnosticLog log;
        log.open(path);
        log.record([](auto &out) { out << "continued\n"; });
    }
    assert(std::filesystem::file_size(path) == 14);

    std::filesystem::resize_file(path, whm::DiagnosticLog::maxBytes + 1);
    {
        whm::DiagnosticLog log;
        log.open(path);
        assert(log);
        log.record([](auto &out) { out << "legacy reset\n"; });
    }
    std::ifstream current(path);
    std::string line;
    assert(std::getline(current, line) && line == "legacy reset");
    current.close();
    assert(std::filesystem::file_size(path) == 13);
    assert(std::filesystem::remove(path));
}
