#pragma once
#include "QueryAST.h"
#include "QueryDateParser.h"
#include "StringUtils.h"
#include <string>
#include <cmath>
#include <limits>
#include <cctype>
#include <cstdint>
#include <algorithm>

/// Parse filter arguments and populate structured fields on QueryNode.
/// Called after the parser creates a FILTER node from raw filterName + filterArg.
class QueryFilterParser {
public:
    /// Post-process a FILTER node to parse its arguments.
    /// Fills in op, numVal1, numVal2, extList as appropriate.
    static void parse(QueryNode& node) {
        if (node.type != QueryNodeType::FILTER) return;

        const auto& name = node.filterName;
        const auto& arg = node.filterArg;

        if (name == "ext") {
            parseExt(node, arg);
        } else if (name == "size") {
            parseSize(node, arg);
        } else if (name == "len" || name == "depth") {
            parseNumeric(node, arg);
        } else if (name == "file" || name == "folder") {
            // No argument parsing needed for file:/folder: (boolean filters).
        } else if (name == "type") {
            // type:file / type:folder / type:dir — lowercase for case-insensitive match.
            node.filterArg = me::toLower(arg);
        } else if (name == "dm" || name == "datemodified" ||
                   name == "dc" || name == "datecreated" ||
                   name == "da" || name == "dateaccessed") {
            QueryDateParser::parse(node, arg);
        } else if (name == "path" || name == "nopath" || name == "parent") {
            // Argument is a substring to match — lowercase for case-insensitive matching
            node.filterArg = me::toLower(arg);
        }
        // Phase 4: Modifiers — convert FILTER → TERM
        else if (name == "case") {
            node.type = QueryNodeType::TERM;
            node.text = arg;
            node.textLower = me::toLower(arg);
            node.mode = MatchMode::SUBSTRING;
            node.caseSensitive = true;
        } else if (name == "nocase") {
            node.type = QueryNodeType::TERM;
            node.text = arg;
            node.textLower = me::toLower(arg);
            node.mode = MatchMode::SUBSTRING;
            node.caseSensitive = false;
        } else if (name == "regex") {
            node.type = QueryNodeType::TERM;
            node.text = arg;
            // textLower not needed — regex uses node.text with icase flag
            node.mode = MatchMode::REGEX;
        } else if (name == "ww" || name == "wholeword") {
            node.type = QueryNodeType::TERM;
            node.text = arg;
            node.textLower = me::toLower(arg);
            node.mode = MatchMode::WHOLEWORD;
        } else if (name == "wfn" || name == "wholefilename") {
            node.type = QueryNodeType::TERM;
            node.text = arg;
            node.textLower = me::toLower(arg);
            node.mode = MatchMode::WHOLEFILENAME;
        }
        // Phase 4: Macros — convert to ext: filter
        else if (name == "audio") {
            expandMacro(node, "mp3;wav;flac;aac;ogg;m4a;wma;alac");
        } else if (name == "video") {
            expandMacro(node, "mp4;avi;mkv;mov;wmv;flv;webm;m4v");
        } else if (name == "pic") {
            expandMacro(node, "jpg;jpeg;png;gif;bmp;tiff;tif;webp;svg;ico;heic;heif;raw");
        } else if (name == "doc") {
            expandMacro(node, "pdf;doc;docx;xls;xlsx;ppt;pptx;txt;rtf;odt;ods;odp;csv;md");
        } else if (name == "exe") {
            expandMacro(node, "app;dmg;pkg;sh;command;csh;action");
        } else if (name == "zip") {
            expandMacro(node, "zip;rar;7z;tar;gz;bz2;xz;tgz;zst;lz4");
        }
    }

private:
    /// Expand a macro (audio:, video:, etc.) into an ext: filter.
    static void expandMacro(QueryNode& node, const std::string& extString) {
        node.filterName = "ext";
        node.filterArg = extString;
        parseExt(node, extString);
    }

    /// Parse ext:cpp or ext:cpp;h;hpp into extList
    static void parseExt(QueryNode& node, const std::string& arg) {
        node.op = CompareOp::EQ;
        std::string lowered = me::toLower(arg);
        size_t start = 0;
        for (size_t i = 0; i <= lowered.size(); i++) {
            if (i == lowered.size() || lowered[i] == ';') {
                if (i > start) node.extList.push_back(lowered.substr(start, i - start));
                start = i + 1;
            }
        }
    }

    /// Parse size:>1mb, size:<500kb, size:>=1gb, size:100kb..1mb, size:1024
    static void parseSize(QueryNode& node, const std::string& arg) {
        parseNumericWithUnits(node, arg);
    }

    /// Parse a plain numeric comparison: len:>10, depth:<3, depth:5
    static void parseNumeric(QueryNode& node, const std::string& arg) {
        parseNumericWithUnits(node, arg);
    }

    /// Unified numeric parser: handles comparison operators, ranges, and size units.
    /// Supports: >N, >=N, <N, <=N, N..M (range), N (exact equality)
    static void parseNumericWithUnits(QueryNode& node, const std::string& arg) {
        if (arg.empty()) return;

        // Check for range: N..M (open-ended endpoints supported, like date ranges)
        auto dotdot = arg.find("..");
        if (dotdot != std::string::npos) {
            node.op = CompareOp::RANGE;
            std::string left = arg.substr(0, dotdot);
            std::string right = arg.substr(dotdot + 2);
            node.numVal1 = left.empty() ? 0 : parseValueWithUnit(left);
            node.numVal2 = right.empty() ? std::numeric_limits<uint64_t>::max()
                                         : parseValueWithUnit(right);
            return;
        }

        // Check for comparison operators
        size_t valStart = 0;
        if (arg.size() >= 2 && arg[0] == '>' && arg[1] == '=') {
            node.op = CompareOp::GE;
            valStart = 2;
        } else if (arg.size() >= 2 && arg[0] == '<' && arg[1] == '=') {
            node.op = CompareOp::LE;
            valStart = 2;
        } else if (arg[0] == '>') {
            node.op = CompareOp::GT;
            valStart = 1;
        } else if (arg[0] == '<') {
            node.op = CompareOp::LT;
            valStart = 1;
        } else {
            node.op = CompareOp::EQ;
        }

        node.numVal1 = parseValueWithUnit(arg.substr(valStart));
    }

    /// Parse a numeric value with optional size unit suffix.
    /// Supports: b, kb, mb, gb, tb (case insensitive)
    static uint64_t parseValueWithUnit(const std::string& s) {
        if (s.empty()) return 0;

        // Find where digits end and unit begins
        size_t numEnd = 0;
        while (numEnd < s.size() && (std::isdigit(static_cast<unsigned char>(s[numEnd])) || s[numEnd] == '.')) {
            numEnd++;
        }

        if (numEnd == 0) return 0;

        double value = 0;
        try { value = std::stod(s.substr(0, numEnd)); }
        catch (...) { return 0; }

        // Parse unit suffix
        std::string unit = me::toLower(s.substr(numEnd));

        uint64_t multiplier = 1;
        if (unit == "kb" || unit == "k") {
            multiplier = 1024ULL;
        } else if (unit == "mb" || unit == "m") {
            multiplier = 1024ULL * 1024;
        } else if (unit == "gb" || unit == "g") {
            multiplier = 1024ULL * 1024 * 1024;
        } else if (unit == "tb" || unit == "t") {
            multiplier = 1024ULL * 1024 * 1024 * 1024;
        }
        // "b" or empty = bytes, no multiplier

        if (value <= 0) return 0;
        const double maxValue = static_cast<double>(std::numeric_limits<uint64_t>::max());
        if (!std::isfinite(value) || value >= maxValue / static_cast<double>(multiplier)) {
            return std::numeric_limits<uint64_t>::max();
        }
        return static_cast<uint64_t>(value * static_cast<double>(multiplier));
    }
};
