## Building and Testing

- To compile the project: `meson compile -C build` (compiles everything, no target needed)
- To run tests: `meson test -C build`
- The build artifacts always live in a directory called `build` or `builddir`.
  If `build` doesn't exist, use `-C builddir` in the meson commands.

## Git Workflow

- Main branch: `master`
- Always create feature branches for new work
- Reference GitLab MR/issue numbers in commits where applicable
- Never commit build artifacts or temporary files
- Use `glab` CLI tool for GitLab interactions (MRs, issues, etc.)

## Commit Messages

- A commit message records **only the changes being made and the rationale for them**.
- For each sentence in a draft, ask what change it records or what decision it justifies. If the
  answer is neither, delete it. In particular, cut anything that:
    - explains something the project's developers already know
    - restates general knowledge about the language, toolkit or the codebase
    - narrates the reasoning behind an earlier, abandoned attempt
- Prefer terse bullets over prose.
- Verify factual claims in the message (file counts, symbol names, paths) against the actual diff
  before committing.

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

## AI Attribution Convention

When assisting with a commit (code, patches, debugging, analysis), add this trailer:

```
Assisted-by: AGENT_NAME:MODEL_VERSION [TOOL1] [TOOL2] ...
```

- **AGENT_NAME**: canonical tool name (e.g., `Claude`).
- **MODEL_VERSION**: the session's model ID, copied verbatim — every character, including any
  bracketed or suffixed variant tag (`claude-opus-5[1m]`, not `claude-opus-5`, not `claude`).
  Read it from the session for every commit; the IDs in this file and in the git log are stale
  examples of the *format*, never a source to copy from.
- **[TOOLS]**: optional — specialized analysis tools *actually used this session* (e.g., `sparse`,
  `smatch`, `clang-tidy`, a linter/fuzzer). Omit basic tooling (git, compilers, editors, build
  systems).
- Tag only what you actually did — don't imply broader authorship than your contribution. Multiple
  AI tools → separate `Assisted-by` line each.
- Example: `Assisted-by: Claude:claude-sonnet-5 sparse smatch`

**Never use `Co-Authored-By`** for AI — it must stay distinguishable from human co-authorship.

**Never add `Signed-off-by`** — only humans can legally certify a DCO/equivalent. This is a legal
boundary, not style. The human submitter alone must: review all AI-generated code, ensure
licensing/IP compliance, and take full responsibility for the contribution.

**Placement**: standard trailer block at message end, alongside other trailers (one per line, no
blank lines within the block).
