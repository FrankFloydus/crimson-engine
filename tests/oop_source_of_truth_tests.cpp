#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#ifndef QUADER_SOURCE_DIR
#define QUADER_SOURCE_DIR "."
#endif

namespace {

std::string readFile(const std::filesystem::path& relativePath) {
    const std::filesystem::path path = std::filesystem::path(QUADER_SOURCE_DIR) / relativePath;
    std::ifstream file(path);
    EXPECT_TRUE(file.is_open()) << path.string();
    if (!file.is_open()) {
        return {};
    }
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream stream(text);
    for (std::string line; std::getline(stream, line);) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }
    return lines;
}

std::vector<std::string> splitTableRow(const std::string& line) {
    std::vector<std::string> columns;
    std::istringstream stream(line);
    for (std::string current; std::getline(stream, current, '|');) {
        const auto first = current.find_first_not_of(' ');
        const auto last = current.find_last_not_of(' ');
        columns.push_back(first == std::string::npos ? "" : current.substr(first, last - first + 1));
    }
    if (!columns.empty() && columns.front().empty()) {
        columns.erase(columns.begin());
    }
    return columns;
}

bool isAllowedStatus(const std::string& status) {
    static constexpr std::array allowed{"TODO", "CODED", "INTEGRATED", "TESTED", "ACCEPTED"};
    return std::find(allowed.begin(), allowed.end(), status) != allowed.end();
}

} // namespace

TEST(OopSourceOfTruthTests, UsesStrictRequiredSections) {
    const std::string source = readFile("oop-refactor.md");

    for (const std::string_view heading : {
                 "## Acceptance Definition",
                 "## Status Legend",
                 "## Requirement Matrix",
                 "## Test Matrix",
                 "## Architecture Guardrails",
                 "## Final Acceptance Commands",
                 "## Verification Log",
                 "## Final Checklist",
         }) {
        EXPECT_NE(source.find(heading), std::string::npos) << heading;
    }
}

TEST(OopSourceOfTruthTests, DoesNotUseLegacyCompletionLanguage) {
    const std::string source = readFile("oop-refactor.md");

    EXPECT_EQ(source.find("Progress Ledger"), std::string::npos);
    EXPECT_EQ(source.find("Done:"), std::string::npos);
    EXPECT_EQ(source.find("Done with documented deferrals"), std::string::npos);
    EXPECT_EQ(source.find("- Verified:"), std::string::npos);
    EXPECT_EQ(source.find("intentionally deferred"), std::string::npos);
}

TEST(OopSourceOfTruthTests, RequirementRowsUseStrictColumnsAndStatuses) {
    const std::vector<std::string> lines = splitLines(readFile("oop-refactor.md"));
    std::size_t requirementRows = 0;

    for (const std::string& line : lines) {
        if (!line.starts_with("| R-")) {
            continue;
        }

        std::vector<std::string> columns = splitTableRow(line);
        while (columns.size() > 7 && columns.back().empty()) {
            columns.pop_back();
        }
        ASSERT_EQ(columns.size(), 7u) << line;
        EXPECT_FALSE(columns[0].empty()) << line;
        EXPECT_FALSE(columns[1].empty()) << line;
        EXPECT_FALSE(columns[2].empty()) << line;
        EXPECT_FALSE(columns[3].empty()) << line;
        EXPECT_FALSE(columns[4].empty()) << line;
        EXPECT_FALSE(columns[5].empty()) << line;
        EXPECT_TRUE(isAllowedStatus(columns[5])) << columns[5] << " in " << line;
        ++requirementRows;
    }

    EXPECT_GE(requirementRows, 20u);
}

TEST(OopSourceOfTruthTests, AcceptedRowsMustCarryEvidence) {
    const std::vector<std::string> lines = splitLines(readFile("oop-refactor.md"));

    for (const std::string& line : lines) {
        if (!line.starts_with("| R-") && !line.starts_with("| T-")) {
            continue;
        }

        std::vector<std::string> columns = splitTableRow(line);
        const std::size_t expectedColumns = line.starts_with("| R-") ? 7u : 5u;
        while (columns.size() > expectedColumns && columns.back().empty()) {
            columns.pop_back();
        }
        ASSERT_GE(columns.size(), 5u) << line;
        if (columns[columns.size() - 2] == "ACCEPTED") {
            EXPECT_FALSE(columns.back().empty()) << line;
        }
    }
}
