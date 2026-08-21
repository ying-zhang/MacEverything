#pragma once
#include "test_helpers.h"
#include "../MacEverything/Core/QueryNeedsAnalysis.h"
#include "../MacEverything/Core/QueryAST.h"
#include <iostream>

inline void runQueryNeedsAnalysisTests() {
    std::cout << "\n── Part 62: QueryNeedsAnalysis Tests ──\n\n";

    // 62a: Pure TERM → needsName + needsPath, not a pure filter
    {
        auto node = QueryNode::makeTerm("hello");
        QueryNeeds needs = analyzeQueryNeeds(*node);
        CHECK(needs.needsName == true);
        CHECK(needs.needsPath == true);
        CHECK(needs.isPureFilter() == false);
    }

    // 62b: size: filter → isPureFilter, needsSize
    {
        auto node = QueryNode::makeFilter("size", ">1mb");
        node->op = CompareOp::GT;
        node->numVal1 = 1048576;
        QueryNeeds needs = analyzeQueryNeeds(*node);
        CHECK(needs.needsSize == true);
        CHECK(needs.needsName == false);
        CHECK(needs.needsPath == false);
        CHECK(needs.isPureFilter() == true);
    }

    // 62c: file: filter → isPureFilter, needsType
    {
        auto node = QueryNode::makeFilter("file", "");
        QueryNeeds needs = analyzeQueryNeeds(*node);
        CHECK(needs.needsType == true);
        CHECK(needs.isPureFilter() == true);
    }

    // 62d: folder: filter → isPureFilter, needsType
    {
        auto node = QueryNode::makeFilter("folder", "");
        QueryNeeds needs = analyzeQueryNeeds(*node);
        CHECK(needs.needsType == true);
        CHECK(needs.isPureFilter() == true);
    }

    // 62e: dm: filter → isPureFilter, needsModTime
    {
        auto node = QueryNode::makeFilter("dm", "today");
        QueryNeeds needs = analyzeQueryNeeds(*node);
        CHECK(needs.needsModTime == true);
        CHECK(needs.isPureFilter() == true);
    }

    // 62f: ext: filter → hasExtFilter + needsName (evalFilter still needs name data)
    {
        auto node = QueryNode::makeFilter("ext", "txt");
        QueryNeeds needs = analyzeQueryNeeds(*node);
        CHECK(needs.hasExtFilter == true);
        CHECK(needs.needsName == true);
        CHECK(needs.isPureFilter() == false);
    }

    // 62g: path: filter → needsPath, NOT a pure filter
    {
        auto node = QueryNode::makeFilter("path", "/usr/local");
        QueryNeeds needs = analyzeQueryNeeds(*node);
        CHECK(needs.needsPath == true);
        CHECK(needs.isPureFilter() == false);
    }

    // 62h: AND(TERM, size:) → NOT pure filter (TERM requires name+path)
    {
        std::vector<std::unique_ptr<QueryNode>> kids;
        kids.push_back(QueryNode::makeTerm("hello"));
        auto sizeNode = QueryNode::makeFilter("size", ">1mb");
        sizeNode->op = CompareOp::GT;
        sizeNode->numVal1 = 1048576;
        kids.push_back(std::move(sizeNode));
        auto andNode = QueryNode::makeAnd(std::move(kids));

        QueryNeeds needs = analyzeQueryNeeds(*andNode);
        CHECK(needs.needsName == true);
        CHECK(needs.needsPath == true);
        CHECK(needs.needsSize == true);
        CHECK(needs.isPureFilter() == false);
    }

    // 62i: OR(size:, dm:) → isPureFilter
    {
        std::vector<std::unique_ptr<QueryNode>> kids;
        auto sizeNode = QueryNode::makeFilter("size", ">1mb");
        sizeNode->op = CompareOp::GT;
        sizeNode->numVal1 = 1048576;
        kids.push_back(std::move(sizeNode));
        auto dmNode = QueryNode::makeFilter("dm", "today");
        dmNode->op = CompareOp::GT;
        dmNode->numVal1 = 1000;
        kids.push_back(std::move(dmNode));
        auto orNode = QueryNode::makeOr(std::move(kids));

        QueryNeeds needs = analyzeQueryNeeds(*orNode);
        CHECK(needs.needsSize == true);
        CHECK(needs.needsModTime == true);
        CHECK(needs.needsName == false);
        CHECK(needs.needsPath == false);
        CHECK(needs.isPureFilter() == true);
    }

    // 62j: NOT(file:) → isPureFilter, needsType
    {
        auto fileNode = QueryNode::makeFilter("file", "");
        auto notNode = QueryNode::makeNot(std::move(fileNode));

        QueryNeeds needs = analyzeQueryNeeds(*notNode);
        CHECK(needs.needsType == true);
        CHECK(needs.isPureFilter() == true);
    }

    // 62k: AND(file:, size:, dm:) → isPureFilter, all filter flags set
    {
        std::vector<std::unique_ptr<QueryNode>> kids;
        kids.push_back(QueryNode::makeFilter("file", ""));
        auto sizeNode = QueryNode::makeFilter("size", ">100");
        sizeNode->op = CompareOp::GT;
        sizeNode->numVal1 = 100;
        kids.push_back(std::move(sizeNode));
        auto dmNode = QueryNode::makeFilter("dm", "today");
        dmNode->op = CompareOp::GT;
        dmNode->numVal1 = 1000;
        kids.push_back(std::move(dmNode));
        auto andNode = QueryNode::makeAnd(std::move(kids));

        QueryNeeds needs = analyzeQueryNeeds(*andNode);
        CHECK(needs.needsType == true);
        CHECK(needs.needsSize == true);
        CHECK(needs.needsModTime == true);
        CHECK(needs.isPureFilter() == true);
    }

    // 62l: len: filter → needsName
    {
        auto node = QueryNode::makeFilter("len", ">10");
        node->op = CompareOp::GT;
        node->numVal1 = 10;
        QueryNeeds needs = analyzeQueryNeeds(*node);
        CHECK(needs.needsName == true);
        CHECK(needs.isPureFilter() == false);
    }

    // 62m: type: filter → needsType, isPureFilter
    {
        auto node = QueryNode::makeFilter("type", "file");
        QueryNeeds needs = analyzeQueryNeeds(*node);
        CHECK(needs.needsType == true);
        CHECK(needs.isPureFilter() == true);
    }

    // 62n: datemodified: (long form) → needsModTime
    {
        auto node = QueryNode::makeFilter("datemodified", "today");
        QueryNeeds needs = analyzeQueryNeeds(*node);
        CHECK(needs.needsModTime == true);
        CHECK(needs.isPureFilter() == true);
    }

    // 62o: datecreated: → needsModTime
    {
        auto node = QueryNode::makeFilter("datecreated", "today");
        QueryNeeds needs = analyzeQueryNeeds(*node);
        CHECK(needs.needsModTime == true);
        CHECK(needs.isPureFilter() == true);
    }

    // 62p: dateaccessed: → needsModTime
    {
        auto node = QueryNode::makeFilter("dateaccessed", "today");
        QueryNeeds needs = analyzeQueryNeeds(*node);
        CHECK(needs.needsModTime == true);
        CHECK(needs.isPureFilter() == true);
    }
}
