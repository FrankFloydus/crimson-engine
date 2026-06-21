#include "ui/ConsoleModel.h"

#include <algorithm>
#include <cctype>

namespace quader {
namespace {

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

} // namespace

std::vector<LogEntry> ConsoleModel::filter(const std::vector<LogEntry>& entries, const ConsoleFilter& filter) {
    std::vector<LogEntry> result;
    const std::string search = lowercase(filter.searchText);

    for (const LogEntry& entry : entries) {
        if (entry.level < filter.minimumLevel) {
            continue;
        }
        if (!search.empty() && lowercase(entry.message).find(search) == std::string::npos) {
            continue;
        }
        result.push_back(entry);
    }

    return result;
}

} // namespace quader
