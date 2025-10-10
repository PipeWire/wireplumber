## Building and Testing

- To compile the project: `meson compile -C build` (compiles everything, no target needed)
- To run tests: `meson test -C build`
- The build artifacts always live in a directory called `build` or `builddir`.
  If `build` doesn't exist, use `-C builddir` in the meson commands.

## Git Workflow

- Main branch: `master`
- Always create feature branches for new work
- Use descriptive commit messages following project conventions
- Reference GitLab MR/issue numbers in commits where applicable
- Never commit build artifacts or temporary files
- Use `glab` CLI tool for GitLab interactions (MRs, issues, etc.)

## Making a release

- Each release always consists of an entry in NEWS.rst, at the top of the file, which describes
  the changes between the previous release and the current one. In addition, each release is given
  a unique version number, which is present:
    1. on the section header of that NEWS.rst entry
    2. in the project() command in meson.build
    3. on the commit message of the commit that introduces the above 2 changes
    4. on the git tag that marks the above commit
- In order to make a release:
    - Begin by analyzing the git history and the merged MRs from GitLab between the previous release
      and today. GitLab MRs that are relevant always have the new release's version number set as a
      "milestone"
    - Create a new entry in NEWS.rst describing the changes, in a similar style and format as the
      previous entries. Consolidate the changes to larger work items and also reference the relevant
      gitlab MR that corresponds to each change and/or the gitlab issues that were addressed by each
      change.
    - Make sure to move the "Past releases" section header up, so that the only 2 top-level sections
      are the new release section and the "Past releases" section.
    - Edit meson.build to change the project version to the new release number
    - Do not commit anything to git. Let the user review the changes and commit manually.
