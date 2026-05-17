# Agent Notes

- This project is built through GitHub Actions using the local `gh` CLI.
- After a successful GitHub Actions build, download the build artifacts, especially the `.dmg` file, to the local `artifacts/` directory.
- When downloading files from GitHub, use the macOS system proxy settings to speed up downloads when a proxy is configured.
- The downloaded `.dmg` is for manual testing by the user. Code changes should still be covered by appropriate automated checks such as unit tests where applicable.
