# TODO

## Multiple Search Windows

Consider supporting multiple search windows, similar to Everything. The recommended direction is to support it, but implement it by separating global indexing state from per-window search state.

Suggested shape:

- Allow multiple search windows, each with its own search text, result list, column widths, selection, and search option state.
- Keep the file index, filesystem watcher, content index, settings, HTTP API, and maintenance state global and shared.
- Make the menu bar icon and global shortcut show the most recently used search window by default.
- Add a "New Search Window" command, likely with `Cmd+N`.
- Closing a search window should only close that UI window and must not stop indexing or monitoring.

Implementation notes:

- Avoid creating a full independent `SearchViewModel` with its own `startIncremental()` per window. That would duplicate bridge callbacks and risk races around scanner, watcher, and content indexing state.
- Split the current model into a global service model and a window model:
  - `SearchServiceModel`: owns indexing, scanning progress, monitoring state, content indexing state, bridge callbacks, and runtime configuration.
  - `SearchWindowViewModel`: owns query text, search results, pagination, selected row, ghost suggestion, and per-window UI state.
- Start with a lightweight version: add a command to open another search window using the shared service model.
- Consider saved searches, tabbed windows, or session restoration only after the state split is stable.
