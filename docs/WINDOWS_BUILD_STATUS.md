# Native Windows build status and requirements

## Current status

This private repository is the intended home for The Hybrid Elite Mafia Studio Suite native Windows application.

The original native project archive is currently inaccessible to the assistant and has not been imported or inspected here. The C++/JUCE source, CMake layout, dependency versions, application target, and runtime packaging requirements therefore remain unverified.

**The Windows build workflow has not been implemented. No Windows executable has been built or uploaded.** Repository creation is not a completed application build.

## Required source import

Import the genuine authorized native project, preserving its original structure, CMake files, dependency pins, assets, tests, and license notices. Record its source revision or archive checksum. Review the import for credentials and unrelated personal files before committing it. Do not substitute a demonstration project, placeholder implementation, or fabricated executable.

## Windows workflow requirements

After inspecting the actual source, implement a manually triggered GitHub Actions workflow with these requirements:

1. Use `workflow_dispatch` only and the requested `windows-latest` runner. Do not add automatic push, pull-request, or scheduled builds at this stage
2. Check out the selected revision with read-only repository permissions. Use reviewed official actions and the project's verified dependency method
3. Validate the actual project directory and `CMakeLists.txt` before configuration; fail clearly if required source or dependencies are missing
4. Configure the documented Windows toolchain for x64 and build the exact application target in Release configuration. Record the runner/compiler/CMake versions and check command exit codes
5. Run applicable project tests, preserving existing regression tests and acceptance criteria
6. Package the actual application using verified install rules or an explicit runtime manifest. Include its required DLLs, resources, and license notices. Do not collect arbitrary executables from the build tree
7. Verify the specifically named application executable and every required runtime file, then generate a SHA-256 package manifest
8. Upload the verified application bundle as a downloadable Windows x64 Release artifact. Fail on missing files and use a short retention period

The exact configure options, target name, dependency setup, executable path, and packaging commands must come from the imported source rather than assumptions.

## Execution and delivery gates

No Actions run should be started while source is absent. Before the first run, verify that the account's included allowance and spending controls support a charge-free build; paid usage is not authorized by this setup. Do not change payment, security, or access settings to proceed.

For delivery, record the source commit, workflow run URL, compilation and test results, artifact URL and expiration, and package checksum. Inspect the actual downloadable bundle. Report separately whether Windows launch and audio-device behavior were tested. Successful compilation alone does not establish audio functionality or production readiness.

## References

- [GitHub workflow syntax](https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax)
- [CMake command-line reference](https://cmake.org/cmake/help/latest/manual/cmake.1.html)
- [JUCE CMake API](https://github.com/juce-framework/JUCE/blob/master/docs/CMake%20API.md)
- [Official artifact upload action](https://github.com/actions/upload-artifact)
