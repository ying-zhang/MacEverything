# Agent Notes

- By default, build this project locally with `xcodebuild` and create the DMG with `scripts/create-dmg.sh`.
- GitHub Actions can still be used for verification when explicitly needed, using the local `gh` CLI.
- Always push code changes to the `fork` remote, not `origin`; use `git push fork main` for the main branch.
- After a successful local or GitHub Actions build, put the generated `.dmg` file in the local `artifacts/` directory.
- When downloading files from GitHub, use the macOS system proxy settings to speed up downloads when a proxy is configured.
- The downloaded `.dmg` is for manual testing by the user. Code changes should still be covered by appropriate automated checks such as unit tests where applicable.
- Release notes must be written in Chinese unless explicitly requested otherwise. They should cover the full feature updates since the fork baseline, include a concise feature overview, and call out important bug fixes. Updating release notes should use `gh release edit` when possible so the existing release page and uploaded assets are preserved.
