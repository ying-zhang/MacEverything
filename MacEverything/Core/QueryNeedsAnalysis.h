#pragma once
#include "QueryAST.h"

/// Result of analyzing which data fields a query AST actually needs.
struct QueryNeeds {
    bool needsName    = false;  // query contains TERM or len: filter
    bool needsPath    = false;  // query contains path:/nopath:/parent:/depth: filter or path-matching TERM
    bool needsType    = false;  // query contains file:/folder:/type: filter
    bool needsSize    = false;  // query contains size: filter
    bool needsModTime = false;  // query contains dm:/dc:/da:/datemodified:/datecreated:/dateaccessed: filter
    bool hasExtFilter = false;  // query contains ext: filter (can use extension index for pre-filtering)

    /// True when the query only uses type/size/modTime — no string access needed.
    bool isPureFilter() const { return !needsName && !needsPath; }
};

/// Walk the query AST and determine which record fields are needed.
inline QueryNeeds analyzeQueryNeeds(const QueryNode& node) {
    QueryNeeds needs;

    switch (node.type) {
    case QueryNodeType::TERM:
        needs.needsName = true;
        needs.needsPath = true;  // TERM also checks full path
        break;

    case QueryNodeType::FILTER: {
        const auto& f = node.filterName;
        if (f == "ext") {
            needs.needsName = true;     // evalFilter still needs name data to extract extension
            needs.hasExtFilter = true;  // enable extension index pre-filtering
        } else if (f == "len") {
            needs.needsName = true;
        } else if (f == "size") {
            needs.needsSize = true;
        } else if (f == "file" || f == "folder" || f == "type") {
            needs.needsType = true;
        } else if (f == "dm" || f == "datemodified" ||
                   f == "dc" || f == "datecreated" ||
                   f == "da" || f == "dateaccessed") {
            needs.needsModTime = true;
        } else if (f == "path" || f == "nopath" || f == "parent" || f == "depth") {
            needs.needsPath = true;
        } else if (f == "__pathseg") {
            needs.needsPath = true;
        }
        break;
    }

    case QueryNodeType::AND:
    case QueryNodeType::OR:
    case QueryNodeType::NOT:
        for (auto& child : node.children) {
            QueryNeeds childNeeds = analyzeQueryNeeds(*child);
            needs.needsName    = needs.needsName    || childNeeds.needsName;
            needs.needsPath    = needs.needsPath    || childNeeds.needsPath;
            needs.needsType    = needs.needsType    || childNeeds.needsType;
            needs.needsSize    = needs.needsSize    || childNeeds.needsSize;
            needs.needsModTime = needs.needsModTime || childNeeds.needsModTime;
            needs.hasExtFilter = needs.hasExtFilter || childNeeds.hasExtFilter;
        }
        break;
    }

    return needs;
}
