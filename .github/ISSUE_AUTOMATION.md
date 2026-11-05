# Issue labels and release testing

The workflows become active once these files reach the default branch (`stable`).
They use the built-in `GITHUB_TOKEN`; no service account, AI API or additional
secret is required. Existing issues are not changed retroactively on deployment.

## Automatic labels

Opening, editing or reopening an issue checks its current title and description
against the English/French rules in [issue-automation.json](issue-automation.json).
Matching type, controller and component labels are added. Existing labels and
their colours/descriptions are preserved; missing matching labels are created.
Model and component labels use short names without prefixes, such as `apex4`,
`apex5`, `apex6`, `triggers`, `haptics` and `installer`.
Classification is additive: remove incorrect labels manually and adjust the rule
if needed. Existing maintainer type labels (`bug`, `enhancement`, `question`)
take precedence over automatic type classification. Pull requests are ignored.

The Tray's report window sends three categories, recognised from the title
prefix: `[Bug]` (`bug`), `[Feature request]` (`enhancement`) and
`[Unsupported game]` (`game-request`). Feature requests carry no diagnostics.

The application report includes settings for all controllers and driver names.
Folded diagnostics and fenced logs are excluded from keyword classification;
the `Controller state:` line and title take precedence over controller models
mentioned only in comparisons. The automatic report heading is also ignored.
The release status uses the repository's existing `pending testing` label;
`working on it` is removed when a published release requests testing.

Add `automation:skip` to opt an issue out of both labeling and release follow-up.
Create that label manually when first needed, using the definition in the JSON.

## Request testing when publishing a release

Mention issues in the notes for the version being released, for example:

```markdown
### Fixes
- Keep the virtual controller connected during recovery (#44, #45).
- Correct game detection: https://github.com/ReynArts/ApexSenseBridge/issues/46
```

The existing release workflow extracts those notes from `CHANGELOG.md`. After a
successful publication, it calls the reusable release follow-up workflow.
This explicit call is necessary because a release created with `GITHUB_TOKEN`
does not trigger another workflow through the `release` event. Manual GitHub
publications and edits of published notes also trigger follow-up. Stable releases
and pre-releases are supported; drafts are ignored.

For each referenced issue in this repository, automation:

1. Adds `pending testing` and removes the old status labels listed in the config.
2. Reopens an issue closed as completed, including issues auto-closed by a merge.
3. Mentions the reporter in an English comment with the exact release download
   link, reproduction instructions and the information to include in feedback.
4. Leaves the issue open until the maintainer reviews the test results.

References to pull requests, other repositories, unavailable issues, locked
threads, issues closed as not planned, or issues labeled `duplicate`, `invalid`,
`wontfix` or `automation:skip` are ignored. Every issue mention in published notes
counts, including contextual references; omit issues that should not request
testing. Hidden comments and fenced examples are ignored.

An invisible comment marker identifies the release by its GitHub ID. Rerunning
the workflow or editing notes does not duplicate a message or reset an issue that
has already been tested for that release. Adding a new issue to published notes
processes only the newly referenced issue. A new release can request another test.
There is no automatic interpretation of replies or automatic closure.

Avoid `Fixes #44` / `Closes #44` in PR descriptions or commit messages if you want
the issue to remain open before release; use `Refs #44` instead. Completed issues
will still be reopened for testing when referenced in a new release.

## Retry and verification

Use **Actions → Request issue testing after release → Run workflow**, enter the
published tag (for example `v1.0.3`), and select `stable`. It is safe to rerun after
a partial API failure: already notified issues are skipped. A failed follow-up
does not undo or republish the release.

Run the automated tests locally with Node.js 24:

```console
node --test tests/test_github_issue_automation.cjs
```

The tests use a fake GitHub client and do not post comments or change live issues.
The dedicated CI workflow runs them when the automation files change.
