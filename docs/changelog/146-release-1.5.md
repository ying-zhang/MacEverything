# 146 — MacEverything 1.5 Release Notes

MacEverything 1.5 focuses on improving search responsiveness, reducing index memory usage, and making the search and settings surfaces easier to use.

## Highlights

- Search results now explain when the result set exceeds the configured maximum instead of presenting the maximum count as if it were the exact total.
- The main window now shows approximate index memory usage next to the indexed file count.
- Startup now loads the base records first so the UI and HTTP status endpoint can respond earlier, then builds optional accelerators in the background.
- The path index now stores stable path hashes instead of full path strings, reducing memory while preserving duplicate detection.
- Optional pinyin initials search and accelerated path search can be controlled separately.
- The HTTP API port enablement logic now persists and applies correctly, and numeric settings support direct validated text entry.
- The search window includes a Settings button next to the search options, making customization easier to discover.
- Settings now includes shortcuts and MCP integration controls in the General tab.
- Search syntax help and regular expression help are grouped together in Search & Results settings.
- The menu bar shortcut menu now places Settings at the top of the second group and includes Regular Expression Help.
- Default indexed folders now include iCloud Drive and the user's Public folder when those folders exist.
- The menu bar icon has been refreshed, with the selected SVG source kept in `assets/` alongside the alternate preview icon and HTML preview page.

## Release Checklist

- GitHub Actions Release build should be used for the DMG.
- The downloaded DMG should be tested manually before publishing the GitHub release.
- The local 1.4 tag was removed because the corresponding GitHub tag and release were deleted.
