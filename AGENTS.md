# Agent Notes

- By default, build this project locally with `xcodebuild` and create the DMG with `scripts/create-dmg.sh`.
- Build Release app: `xcodebuild -scheme MacEverything -configuration Release build`. The output is in `~/Library/Developer/Xcode/DerivedData/MacEverything-*/Build/Products/Release/MacEverything.app`. After building, copy the `.app` to `artifacts/`: `cp -R .../Release/MacEverything.app artifacts/`.
- GitHub Actions can still be used for verification when explicitly needed, using the local `gh` CLI.
- Always push code changes to the `fork` remote, not `origin`; use `git push fork main` for the main branch.
- After a successful local or GitHub Actions build, put the generated `.app` or `.dmg` in the local `artifacts/` directory. Keep `artifacts/` clean — only final deliverables (`.app`, `.dmg`), not build dependencies or source trees.
- When downloading files from GitHub, use the macOS system proxy settings to speed up downloads when a proxy is configured.
- The downloaded `.dmg` is for manual testing by the user. Code changes should still be covered by appropriate automated checks such as unit tests where applicable.
- Release notes must be written in Chinese unless explicitly requested otherwise. They should cover the full feature updates since the fork baseline, include a concise feature overview, and call out important bug fixes. Updating release notes should use `gh release edit` when possible so the existing release page and uploaded assets are preserved.

## Personal information audit

- Before committing or publishing, inspect both the working tree and reachable Git history for personal information. Do not publish private email addresses, phone numbers, home-directory paths, screenshots with identifiable data, credentials, tokens, or machine-specific usernames.
- Review commit identities with:
  `git log --all --format='%H%x09%an <%ae>%x09%cn <%ce>' | sort -u`
- Scan tracked files for email addresses and local paths with:
  `git grep -n -I -E '[[:alnum:]_.+-]+@[[:alnum:]_.-]+\\.[[:alpha:]]{2,}|(/Users/|/home/|C:\\\\Users\\\\)' -- ':!*.xcuserstate'`
- Treat matches in tests and documentation as candidates for anonymization. Keep only intentional public project identities, repository URLs, and generic examples such as `/Users/username` after manual review.
- If history still contains a private identity, report the exact refs and commits before release. Do not rewrite published history unless explicitly requested; use a follow-up history rewrite and force-push plan when needed.
