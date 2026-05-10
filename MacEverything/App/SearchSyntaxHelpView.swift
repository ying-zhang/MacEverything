import SwiftUI

struct SearchSyntaxHelpView: View {
    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 20) {
                basicSearchSection
                booleanSection
                wildcardSection
                regexSection
                extensionFilterSection
                sizeFilterSection
                typeFilterSection
                pathFilterSection
                dateFilterSection
                lengthFilterSection
                modifierSection
                typeMacroSection
                structuredPathSection
                contentSearchSection
                tildeSection
            }
            .padding(24)
        }
    }

    // MARK: - Sections

    private var basicSearchSection: some View {
        SyntaxSection(title: L10n.tr("Basic Search")) {
            SyntaxRow("hello", L10n.tr("Substring match (case-insensitive)"))
            SyntaxRow("hello world", L10n.tr("AND - both must match"))
            SyntaxRow("\"exact phrase\"", L10n.tr("Quoted exact phrase match"))
        }
    }

    private var booleanSection: some View {
        SyntaxSection(title: L10n.tr("Boolean Operators")) {
            SyntaxRow("A | B", L10n.tr("OR - either matches"))
            SyntaxRow("!A", L10n.tr("NOT - exclude matches"))
            SyntaxRow("<A | B>", L10n.tr("Grouping with angle brackets"))
            SyntaxNote(L10n.tr("Example: <hello | world> .txt"))
        }
    }

    private var wildcardSection: some View {
        SyntaxSection(title: L10n.tr("Wildcards / Glob")) {
            SyntaxRow("*", L10n.tr("Zero or more characters"))
            SyntaxRow("?", L10n.tr("Exactly one character"))
            SyntaxRow("*.txt", L10n.tr("Files ending with .txt"))
            SyntaxRow("test*", L10n.tr("Files starting with test"))
            SyntaxRow("*keyword*", L10n.tr("Files containing keyword"))
        }
    }

    private var regexSection: some View {
        SyntaxSection(title: L10n.tr("Regex")) {
            SyntaxRow("regex:pattern", L10n.tr("ECMAScript regex (case-insensitive)"))
            SyntaxNote(L10n.tr("Example: regex:^test  regex:\\.cpp$"))
        }
    }

    private var extensionFilterSection: some View {
        SyntaxSection(title: L10n.tr("Extension Filter")) {
            SyntaxRow("ext:cpp", L10n.tr("Files with .cpp extension"))
            SyntaxRow("ext:cpp;h;hpp", L10n.tr("Multiple extensions (semicolon-separated)"))
        }
    }

    private var sizeFilterSection: some View {
        SyntaxSection(title: L10n.tr("Size Filter")) {
            SyntaxRow("size:>1mb", L10n.tr("Larger than 1 MB"))
            SyntaxRow("size:<500kb", L10n.tr("Smaller than 500 KB"))
            SyntaxRow("size:>=1gb", L10n.tr("At least 1 GB"))
            SyntaxRow("size:100kb..1mb", L10n.tr("Between 100 KB and 1 MB"))
            SyntaxNote(L10n.tr("Units: b, kb/k, mb/m, gb/g, tb/t"))
        }
    }

    private var typeFilterSection: some View {
        SyntaxSection(title: L10n.tr("Type Filter")) {
            SyntaxRow("file:", L10n.tr("Match only files"))
            SyntaxRow("folder:", L10n.tr("Match only directories"))
            SyntaxRow("type:file", L10n.tr("Same as file:"))
            SyntaxRow("type:folder", L10n.tr("Same as folder:"))
        }
    }

    private var pathFilterSection: some View {
        SyntaxSection(title: L10n.tr("Path Filter")) {
            SyntaxRow("path:keyword", L10n.tr("Path contains keyword"))
            SyntaxRow("nopath:keyword", L10n.tr("Path does not contain keyword"))
            SyntaxRow("parent:dirname", L10n.tr("Immediate parent matches"))
            SyntaxRow("depth:<3", L10n.tr("Directory depth less than 3"))
            SyntaxRow("depth:>5", L10n.tr("Directory depth greater than 5"))
        }
    }

    private var dateFilterSection: some View {
        SyntaxSection(title: L10n.tr("Date Filter")) {
            SyntaxNote(L10n.tr("Prefixes: dm: (modified)  dc: (created)  da: (accessed)"))
            SyntaxRow("dm:today", L10n.tr("Modified today"))
            SyntaxRow("dm:yesterday", L10n.tr("Modified yesterday"))
            SyntaxRow("dm:thisweek", L10n.tr("Modified this week"))
            SyntaxRow("dm:lastmonth", L10n.tr("Modified last month"))
            SyntaxRow("dm:last7days", L10n.tr("Modified in the last 7 days"))
            SyntaxRow("dm:last3months", L10n.tr("Modified in the last 3 months"))
            SyntaxRow("dm:2024-01-15", L10n.tr("Modified on specific date"))
            SyntaxRow("dm:>2024-01", L10n.tr("Modified after January 2024"))
            SyntaxRow("dm:2024-01..2024-06", L10n.tr("Modified between Jan and Jun 2024"))
            SyntaxNote(L10n.tr("Also: datemodified:  datecreated:  dateaccessed:"))
        }
    }

    private var lengthFilterSection: some View {
        SyntaxSection(title: L10n.tr("Name Length Filter")) {
            SyntaxRow("len:>10", L10n.tr("Filename longer than 10 chars"))
            SyntaxRow("len:<5", L10n.tr("Filename shorter than 5 chars"))
            SyntaxRow("len:>=8", L10n.tr("Filename at least 8 chars"))
        }
    }

    private var modifierSection: some View {
        SyntaxSection(title: L10n.tr("Match Modifiers")) {
            SyntaxRow("case:term", L10n.tr("Force case-sensitive match"))
            SyntaxRow("nocase:term", L10n.tr("Force case-insensitive match"))
            SyntaxRow("ww:hello", L10n.tr("Whole word match"))
            SyntaxRow("wfn:readme", L10n.tr("Whole filename match (without extension)"))
            SyntaxNote(L10n.tr("Aliases: wholeword:  wholefilename:"))
        }
    }

    private var typeMacroSection: some View {
        SyntaxSection(title: L10n.tr("File Type Macros")) {
            SyntaxRow("audio:", "mp3, wav, flac, aac, ogg, m4a, wma, alac")
            SyntaxRow("video:", "mp4, avi, mkv, mov, wmv, flv, webm, m4v")
            SyntaxRow("pic:", "jpg, png, gif, bmp, tiff, webp, svg, heic ...")
            SyntaxRow("doc:", "pdf, doc, docx, xls, xlsx, ppt, pptx, txt, md ...")
            SyntaxRow("exe:", "app, dmg, pkg, sh, command")
            SyntaxRow("zip:", "zip, rar, 7z, tar, gz, bz2, xz, tgz, zst, lz4")
        }
    }

    private var structuredPathSection: some View {
        SyntaxSection(title: L10n.tr("Structured Path Query")) {
            SyntaxRow("/abc/def", L10n.tr("Name matches def, path contains abc"))
            SyntaxRow("/abc/def/", L10n.tr("Directory named def under abc"))
            SyntaxRow("/abc/def/*", L10n.tr("List children of def under abc"))
            SyntaxRow("/abc/*/def", L10n.tr("Non-adjacent: abc and def with gaps"))
            SyntaxNote(L10n.tr("Path segments are matched right-to-left"))
        }
    }

    private var contentSearchSection: some View {
        SyntaxSection(title: L10n.tr("Content Search")) {
            SyntaxRow("content:keyword", L10n.tr("Search file contents for keyword"))
        }
    }

    private var tildeSection: some View {
        SyntaxSection(title: L10n.tr("Tilde Expansion")) {
            SyntaxRow("~/Downloads", L10n.tr("Expands ~ to your home directory"))
            SyntaxRow("~/*.txt", L10n.tr("Glob in home directory"))
            SyntaxNote(L10n.tr("Only expanded when ~ is at the start of the query"))
        }
    }
}

// MARK: - Helper Components

private struct SyntaxSection<Content: View>: View {
    let title: String
    @ViewBuilder let content: () -> Content

    init(title: String, @ViewBuilder content: @escaping () -> Content) {
        self.title = title
        self.content = content
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text(title)
                .font(.headline)
                .padding(.bottom, 2)
            content()
        }
    }
}

private struct SyntaxRow: View {
    let syntax: String
    let description: String

    init(_ syntax: String, _ description: String) {
        self.syntax = syntax
        self.description = description
    }

    var body: some View {
        HStack(alignment: .top, spacing: 12) {
            Text(syntax)
                .font(.system(.body, design: .monospaced))
                .foregroundColor(.accentColor)
                .frame(width: 180, alignment: .leading)
            Text(description)
                .foregroundColor(.secondary)
            Spacer()
        }
    }
}

private struct SyntaxNote: View {
    let text: String

    init(_ text: String) {
        self.text = text
    }

    var body: some View {
        Text(text)
            .font(.callout)
            .foregroundColor(.secondary)
            .italic()
            .padding(.leading, 4)
    }
}
