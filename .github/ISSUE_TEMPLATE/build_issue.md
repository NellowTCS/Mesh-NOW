---
name: Build/CI Issue
about: Report problems with building, CI/CD, or GitHub Actions workflows
title: "[BUILD] "
labels: ["build", "ci-cd"]
assignees: ""
---

## Build Issue Type

- [ ] Local build failure
- [ ] GitHub Actions workflow failure
- [ ] Target configuration issue
- [ ] Dependency/toolchain problem
- [ ] Performance/size issue

## Build Environment

**Local Build:**

- **Operating System:** (e.g., Ubuntu 22.04, Windows 11, macOS 13)
- **ESP-IDF Version:** (e.g., v6.1)
- **Python Version:** (e.g., 3.11)
- **Build Command:** (e.g., `idf.py build`, `./scripts/build.sh`)

**GitHub Actions:**

- **Workflow:** (e.g., build.yml, integration.yml)
- **Job/Step:** (Which specific job or step failed?)
- **Runner:** (ubuntu-latest, etc.)

## Target Information

**ESP32 Target:** (e.g., esp32, esp32s2, esp32s3, esp32c3, esp32c6, or "all targets")
**Configuration:** (e.g., using configs/sdkconfig.esp32)

## Error Details

**Error Message:**

```text
Paste the complete error message here
```

**Build Log:**

```text
Paste relevant portions of the build log here
```

**Workflow Run:** (If GitHub Actions, link to the failed run)

## Steps to Reproduce

1. Environment setup steps
2. Commands executed
3. When the error occurs

## Expected vs Actual Behavior

**Expected:** Build should complete successfully
**Actual:** (Describe what actually happened)

## Configuration Changes

Have you made any changes to:

- [ ] sdkconfig files
- [ ] CMakeLists.txt
- [ ] Source code in main/
- [ ] GitHub workflow files
- [ ] Build scripts

If yes, please describe the changes:

## Build Artifacts

If applicable:

- **Binary Size:** (if build completed)
- **Memory Usage:** (from `idf.py size` output)
- **Warnings:** (any compilation warnings)

## Troubleshooting Attempted

- [ ] Clean build (`rm -rf build`)
- [ ] Fresh environment setup
- [ ] Different ESP32 target
- [ ] Different build method (script vs manual)
- [ ] Checked for recent changes in main branch

## System Information

**Git Commit:** (which commit are you building?)
**Disk Space:** (if relevant)
**Available Memory:** (if relevant)
**Network Access:** (for downloading dependencies)

## Additional Context

Any additional information that might help diagnose the issue:

- Recent system updates
- Antivirus software
- Corporate network restrictions
- Docker/container usage

## Checklist

- [ ] I have tried a clean build
- [ ] I have checked the build requirements in README.md
- [ ] I have searched existing issues for similar build problems
- [ ] I have included the complete error message
- [ ] I can reproduce this issue consistently
