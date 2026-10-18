#pragma once
// Continuous-input candidate reuse and deterministic top-K ordering.

static std::vector<std::string> namesOf(SearchEngine& engine, const std::vector<uint32_t>& ids) {
    std::vector<std::string> names;
    names.reserve(ids.size());
    for (uint32_t id : ids) names.push_back(engine.getRecord(id).name);
    return names;
}

static std::vector<FileRecord> sampleRecords() {
    return {
        {"a", "/tmp", 1, 10, 1000},
        {"zzz-alpha", "/tmp", 1, 10, 1000},
        {"alpine", "/opt", 1, 10, 1000},
        {"readme.txt", "/p", 1, 10, 1000},
        {"readme.txt", "/q", 1, 10, 1000},
        {"alpha beta.txt", "/tmp", 1, 10, 1000},
        {"beta", "/tmp", 1, 10, 1000},
        {"文档.txt", "/tmp", 1, 10, 1000},
    };
}

static void loadSample(SearchEngine& engine) {
    engine.loadRecords(sampleRecords());
}

static void runNarrowTopKTests() {
    std::cout << "========================================\n";
    std::cout << "  Part 82: Narrow reuse and top-K\n";
    std::cout << "========================================\n\n";

    {
        std::cout << "  --- full hit list survives a truncated window ---\n";
        SearchEngine typing;
        SearchEngine reference;
        loadSample(typing);
        loadSample(reference);
        auto first = typing.query("a", 1);
        check(first.size() == 1, "Narrow: maxResults=1 returns one row");
        check(typing.getRecord(first[0]).name == "a", "Narrow: exact 'a' is the only displayed row");

        QueryTimingInfo timing;
        auto narrowed = typing.query("al", 20, true, timing);
        auto expected = reference.query("al", 20);
        check(timing.searchPath == "advanced-narrow",
              ("Narrow: extended query reuses candidates (" + timing.searchPath + ")").c_str());
        check(namesOf(typing, narrowed) == namesOf(reference, expected),
              "Narrow: 'a' -> 'al' matches a fresh query, including hits outside the first window");
    }

    {
        std::cout << "\n  --- repeated and appended terms ---\n";
        SearchEngine typing;
        SearchEngine reference;
        loadSample(typing);
        loadSample(reference);
        typing.query("alpha");
        QueryTimingInfo repeated;
        auto again = typing.query("alpha", 20, true, repeated);
        check(repeated.searchPath == "advanced-narrow", "Narrow: repeating the same plain query reuses candidates");
        check(namesOf(typing, again) == namesOf(reference, reference.query("alpha", 20)),
              "Narrow: repeated query keeps the same order");

        QueryTimingInfo appended;
        auto both = typing.query("alpha beta", 20, true, appended);
        check(appended.searchPath == "advanced-narrow", "Narrow: appending a plain term reuses candidates");
        check(namesOf(typing, both) == namesOf(reference, reference.query("alpha beta", 20)),
              "Narrow: 'alpha' -> 'alpha beta' matches a fresh query");
    }

    {
        std::cout << "\n  --- non-monotonic and non-plain queries do not reuse ---\n";
        SearchEngine typing;
        loadSample(typing);
        typing.query("alpha");
        QueryTimingInfo backspace;
        typing.query("alp", 20, true, backspace);
        check(backspace.searchPath != "advanced-narrow", "Narrow: deleting characters scans again");

        typing.query("alpha beta");
        QueryTimingInfo reordered;
        typing.query("beta alpha", 20, true, reordered);
        check(reordered.searchPath != "advanced-narrow", "Narrow: reordered terms scan again");

        typing.query("readme");
        QueryTimingInfo filtered;
        typing.query("readme ext:txt", 20, true, filtered);
        check(filtered.searchPath != "advanced-narrow", "Narrow: ext: filter scans again");

        QueryTimingInfo disjunction;
        typing.query("readme|alpine", 20, true, disjunction);
        check(disjunction.searchPath != "advanced-narrow", "Narrow: OR scans again");

        QueryTimingInfo sensitive;
        typing.query("case:Readme", 20, true, sensitive);
        check(sensitive.searchPath != "advanced-narrow", "Narrow: case sensitivity change scans again");
    }

    {
        std::cout << "\n  --- case-sensitive extension can reuse ---\n";
        SearchEngine typing;
        SearchEngine reference;
        loadSample(typing);
        loadSample(reference);
        typing.query("case:read");
        QueryTimingInfo timing;
        auto got = typing.query("case:readme", 20, true, timing);
        check(timing.searchPath == "advanced-narrow", "Narrow: case-sensitive extension reuses candidates");
        check(namesOf(typing, got) == namesOf(reference, reference.query("case:readme", 20)),
              "Narrow: case-sensitive extension matches a fresh query");
    }

    {
        std::cout << "\n  --- index mutation drops the candidate cache ---\n";
        SearchEngine typing;
        loadSample(typing);
        typing.query("al");
        typing.addRecord({"alpine-new", "/tmp", 1, 10, 1000});
        QueryTimingInfo timing;
        auto got = typing.query("alp", 20, true, timing);
        check(timing.searchPath != "advanced-narrow", "Narrow: a new file invalidates the candidate cache");
        bool found = false;
        for (uint32_t id : got) {
            if (typing.getRecord(id).name == "alpine-new") found = true;
        }
        check(found, "Narrow: query after addRecord includes the new file");
    }

    {
        std::cout << "\n  --- compaction invalidates record numbers ---\n";
        SearchEngine typing;
        std::vector<FileRecord> compactRecords = {
            {"aa-gone", "/tmp", 1, 10, 1000},
            {"ab-kept", "/tmp", 1, 10, 1000},
        };
        typing.loadRecords(std::move(compactRecords));
        typing.query("a");
        typing.removeByPath("/tmp/aa-gone");
        // The deletion already bumps the mutation epoch. Rebuild the cache,
        // then compact, so this checks compaction itself.
        QueryTimingInfo primedTiming;
        auto primed = typing.query("ab", 20, true, primedTiming);
        check(primedTiming.searchPath != "advanced-narrow",
              "Narrow: deletion forces a fresh scan before compaction");
        check(primed.size() == 1 && typing.getRecord(primed[0]).name == "ab-kept",
              "Narrow: fresh scan after deletion returns the live file");
        typing.compactRecords();
        QueryTimingInfo timing;
        auto got = typing.query("ab", 20, true, timing);
        check(timing.searchPath != "advanced-narrow", "Narrow: compaction invalidates the candidate cache");
        check(got.size() == 1 && typing.getRecord(got[0]).name == "ab-kept",
              "Narrow: query after compaction returns the live file");
    }

    {
        std::cout << "\n  --- top-K keeps the best row, ties break by record index ---\n";
        SearchEngine ranked;
        loadSample(ranked);
        QueryTimingInfo unlimitedTiming;
        auto unlimited = ranked.queryAdvanced("a", 0, true, unlimitedTiming);
        QueryTimingInfo limitedTiming;
        auto limited = ranked.queryAdvanced("a", 1, true, limitedTiming);
        check(limited.size() == 1, "Top-K: limit 1 returns one row");
        check(limited[0] == unlimited[0], "Top-K: the retained row is the best unlimited row");

        auto ties = ranked.query("readme.txt", 10);
        check(ties.size() == 2, "Top-K: two equal names both match");
        check(ties[0] < ties[1], "Top-K: equal scores keep the lower record index first");
    }

    std::cout << "\n";
}
