# Repository Guidelines

## Project Structure & Module Organization

This workspace currently contains no source code, tests, assets, or build configuration. No project layout is established yet. Keep new top-level directories purposeful and document their roles here as the project develops. Group implementation files by component, place tests near the code they exercise or in a dedicated `tests/` directory, and keep generated artifacts separate from maintained sources.

## Build, Test, and Development Commands

No build, test, or local execution commands are currently available. When adding the initial implementation, provide a README with prerequisites, supported toolchain versions, and exact commands for building and validating the project. Prefer reproducible commands that run from the repository root. Do not document assumed commands until the corresponding configuration exists.

## Coding Style & Naming Conventions

No language, formatter, linter, or indentation standard has been established. Follow the conventions of the language selected for each component, keep indentation consistent within each file, and use descriptive names. Add formatter or linter configuration when introducing a toolchain so contributors can reproduce style checks. Avoid unrelated formatting changes in functional patches.

## Testing Guidelines

There is no configured test framework or coverage threshold. Introduce an appropriate test setup with the first testable functionality and document how to run it. Give tests descriptive names that identify the behavior under test. Cover normal behavior, boundary cases, and failure paths; add regression tests when fixing defects.

## Commit & Pull Request Guidelines

Git history is unavailable in this workspace, so existing commit conventions cannot be verified. Use concise, imperative commit subjects, such as `Add initial build configuration`, and keep commits focused. Pull requests should explain the change, its motivation, and validation performed. Link relevant issues, identify toolchain or configuration changes, and state explicitly when checks could not be run.
