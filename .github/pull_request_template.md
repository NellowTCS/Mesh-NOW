<!-- markdownlint-disable MD041 -->
## Description
<!-- markdownlint-enable MD041 -->

Brief description of what this PR does.

Fixes #(issue number)

## Type of Change

- [ ] Bug fix (non-breaking change which fixes an issue)
- [ ] New feature (non-breaking change which adds functionality)
- [ ] Breaking change (fix or feature that would cause existing functionality to not work as expected)
- [ ] Documentation update
- [ ] Build system/CI improvement
- [ ] ESP32 target support (new target or configuration)

## ESP32 Target Impact

Which ESP32 targets are affected by this change?

- [ ] ESP32 (Xtensa dual-core)
- [ ] ESP32-S2 (Xtensa single-core)
- [ ] ESP32-S3 (Xtensa dual-core + PSRAM)
- [ ] ESP32-C3 (RISC-V single-core)
- [ ] ESP32-C6 (RISC-V single-core + 802.15.4)
- [ ] All targets
- [ ] No target-specific changes

## Testing

**Build Testing:**

- [ ] Local build successful for affected targets
- [ ] All targets build successfully (if applicable)
- [ ] GitHub Actions workflows pass
- [ ] No new build warnings introduced

**Functional Testing:**

- [ ] WiFi AP creation works
- [ ] Web interface accessible
- [ ] ESP-NOW communication functional
- [ ] Multi-device mesh networking tested
- [ ] Configuration changes validated

**Hardware Testing:**

- [ ] Tested on actual ESP32 hardware
- [ ] Multiple devices tested (if mesh functionality affected)
- [ ] Flash and boot process verified
- [ ] Network performance acceptable

## Changes Made

**Code Changes:**

- [ ] Modified main application code
- [ ] Updated configuration files
- [ ] Changed build scripts
- [ ] Updated documentation

**Configuration Changes:**

- [ ] Modified sdkconfig files
- [ ] Updated CMakeLists.txt
- [ ] Changed component dependencies
- [ ] Updated build parameters

**CI/CD Changes:**

- [ ] Modified GitHub workflows
- [ ] Updated build matrix
- [ ] Changed test procedures
- [ ] Updated release process

## Performance Impact

- **Binary Size Change:** (increase/decrease/no change)
- **Memory Usage:** (RAM/Flash impact)
- **Network Performance:** (ESP-NOW/HTTP server impact)
- **Build Time:** (faster/slower/same)

## Breaking Changes

If this introduces breaking changes, describe:

1. What breaks
2. How users should migrate
3. Backward compatibility considerations

## Documentation

- [ ] README.md updated (if needed)
- [ ] Configuration documentation updated
- [ ] Build instructions updated
- [ ] Code comments added/updated

## Checklist

- [ ] My code follows the project's coding standards
- [ ] I have performed a self-review of my code
- [ ] I have commented my code, particularly in hard-to-understand areas
- [ ] I have made corresponding changes to the documentation
- [ ] My changes generate no new warnings
- [ ] I have added tests that prove my fix is effective or that my feature works
- [ ] New and existing unit tests pass locally with my changes
- [ ] Any dependent changes have been merged and published

## Additional Notes

Any additional information that reviewers should know:

- Implementation decisions made
- Alternative approaches considered
- Future work planned
- Known limitations
