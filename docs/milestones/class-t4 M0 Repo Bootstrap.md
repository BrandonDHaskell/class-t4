# class-t4 M0: Repo Bootstrap

Nov 12, 2026 · @Brandon

## Overview

M0 is about 70% done, but none of it is on `main`: the bootstrap files were committed on Nov 11 to `feat/m1-walking-skeleton`, mixed in with M1 work. The remaining M0 work is 12 small tasks to fill three gaps, land the governance files on `main`, and sync the docs with what was actually built. Roadmap context: [class-t4: Project Plan](https://claude.ai/artifact/Eoy2RvQxbejN5U8tmEtY6J).

**Goal:** `main` holds everything that governs how class-t4 is built (agent rules, design, milestone spec, style configs, license files, signing key), so every M1 PR is reviewed against it.

**Exit criteria**

- Every file in the audit below is on `main` and current.
- `docs/design.md` and `docs/milestones/class-t4 M1 Walking Skeleton.md` match the reconciled planning docs.
- `keys/class-t4.pub` is committed and verifies a test signature.
- `main` rejects force pushes and deletion.
- `feat/m1-walking-skeleton` is updated from the new `main` and carries only M1 work.
- Every validation check below passes.

## Current state audit

Audited on Nov 12 against `main` (`e7a0cfb`) and `feat/m1-walking-skeleton` (`a947a49`). Seven deliverables are done, five are stale or empty, two are missing, and the ruleset is unverified.

| Deliverable | Where | Status | Gap |
| --- | --- | --- | --- |
| `LICENSE` (Apache 2.0) | `main` | Done | None |
| `CLAUDE.md` | Branch (`964e161`) | Stale | Predates the signing key and docs-sync decisions |
| `.claude/settings.json` | Branch (`964e161`) | Done | None |
| `docs/design.md` | Branch (`964e161`) | Stale | Exported before the Nov 11 decisions; still says ops repo "TBD" and lists resolved open items |
| `docs/milestones/class-t4 M1 Walking Skeleton.md` | Branch | Stale | Replaces the removed `m1-walking-skeleton.md` spec; differs from what was built (config schema, script names, CLI parser) |
| `.clang-format` | Branch | Done | LLVM base, 80 columns, include grouping |
| `.clang-tidy` | Branch | Done | Naming rules match the style decision |
| `.editorconfig` | Branch | Done | None |
| `.gitignore` | Branch | Done | Covers `build/`, `.cache/`, IDE dirs |
| `CHANGELOG.md` | Branch | Empty | No header or Unreleased section |
| `.github/workflows/ci.yml`, `release.yml` | Branch | Empty placeholders | Empty workflow files show as invalid in the Actions tab |
| `NOTICE` | Nowhere | Missing | Required by the tarball spec |
| `keys/class-t4.pub` | Nowhere | Missing | Waits on the signing key decision |
| `main` ruleset | GitHub | Unverified | Needs your check |
| lovelace ready for Claude Code | lovelace | Done | Proven by the Nov 11 commits and the pinned toolchain digest |

The branch also holds M1 work: toolchain image and digest (phase A), CMake, presets and container script (phase B) and most of the app skeleton (phase C). That is tracked in the M1 doc, not here.

## Decisions and specifications

Six decisions shape the remaining M0 work; each has a recommended default.

| # | Decision | Recommended | Blocks |
| --- | --- | --- | --- |
| D1 | How M0 reaches `main` | Separate `chore/m0-bootstrap` PR, then rebase the M1 branch onto it | M0-9 |
| D2 | Source of truth for specs | Repo files are canonical; a PR that deviates from a spec updates the spec in the same PR | M0-4, M0-8 |
| D3 | Signing key | Dedicated `class-t4.key`, separate from the kernel `release.key` | M0-7 |
| D4 | `NOTICE` text | Below | M0-5 |
| D5 | `main` ruleset | Below | M0-10 |
| D6 | Who pushes and opens PRs | Claude Code commits and pushes feature branches; you open and merge PRs; the ruleset stops any direct push to `main` | M0-8, M0-10 |

**D4: `NOTICE`** (confirm the copyright holder name; this matches your git author name)

```
class-t4
Copyright 2026 Brandon Haskell
```

**D5: `main` ruleset**

| Setting | Value |
| --- | --- |
| Target | `main` |
| Restrict deletions | On |
| Block force pushes | On |
| Require a pull request | On, 0 required approvals |
| Required status checks | None yet; added in M1 task E4 once `ci.yml` exists |
| Bypass | None, matching crows-nest |

**`CHANGELOG.md` format:** Keep a Changelog, starting with an empty `## [Unreleased]` section.

**Empty workflow placeholders:** removed now; M1 tasks E1 and G1 add `ci.yml` and `release.yml` with real content.

## Tasks

12 tasks, about 3 working sessions in total; you own 4 and review the rest. Order: M0-1, then M0-2 to M0-8 and M0-12 in any order (respecting the dependencies listed), then M0-10, M0-9 and M0-11.

Task format: **ID. Name** · size · owner · depends on, then details and validation.

**M0-1. Confirm decisions D1 to D6** · S · **you** · none

- **Details:** accept or change each default in the decisions table.
- **Validate:** decisions logged in this doc's change log.

**M0-2. Refresh the reference doc** · S · Claude (planning chat) · M0-1

- **Details:** fold in the Nov 11 decisions (GCC 16.2, glibc 2.41 target, static libstdc++ and libgcc, ports 8443 and 9464, GHCR toolchain image, Claude Code on lovelace). Name crows-nest as the ops repo, record the CA on tranquility and the separate host runbooks, remove resolved open items, and link the milestone docs.
- **Validate:** C-03; your review.

**M0-3. Export `docs/design.md`** · S · Claude exports, Claude Code commits · M0-2

- **Details:** Markdown export of the refreshed reference doc replaces the branch copy.
- **Validate:** C-02; renders cleanly on GitHub.

**M0-4. Reconcile the M1 spec with what was built** · M · Claude (planning chat), you review · M0-1

- **Details:** under D2, adopt what the branch built unless a spec reason says otherwise: config keys (`https`, `ops`, `cert_file`, `key_file`, `log_level`), script names (`check-release-binary.sh`, `smoke-test.sh`), the hand-rolled CLI instead of CLI11, the `/.cache/` vcpkg cache, `module.hpp` in `core`. Mark finished M1 tasks done.
- **Validate:** every M1 task has a status; the M1 doc matches the branch.

**M0-5. Add `NOTICE`** · S · Claude Code · D4

- **Details:** text from D4, at the repo root.
- **Validate:** C-01.

**M0-6. Fix placeholders** · S · Claude Code · M0-1

- **Details:** give `CHANGELOG.md` its Keep a Changelog header and Unreleased section; delete the empty `ci.yml` and `release.yml`.
- **Validate:** C-01, C-09.

**M0-7. Generate the class-t4 signing key** · S · **you** · D3

- **Details:** on lovelace, same container pattern as the kernel runbook:

```
docker run --rm -it -u "$(id -u):$(id -g)" -e HOME=/tmp \
  -v ~/.minisign:/keys pi5-kernel-build:<YYYYMMDD> \
  minisign -G -p /keys/class-t4.pub -s /keys/class-t4.key
cp ~/.minisign/class-t4.pub keys/class-t4.pub
```

Store the password and back up `class-t4.key` the same way as `release.key`.

- **Validate:** C-05, C-08.

**M0-8. Update `CLAUDE.md`** · S · Claude Code, you review · M0-1, M0-7

- **Details:** name the key files (`keys/class-t4.pub` verifies; `~/.minisign/` stays off limits); add the D2 rule (a PR that changes behavior updates the spec in the same PR); add the D6 flow (push feature branches only, never `main`, never open or merge PRs); point to `docs/milestones/` as the task source.
- **Validate:** your review; C-04.

**M0-9. Land M0 on `main`** · S · **you** · M0-3 to M0-8, M0-10, M0-12

- **Details:**
  1. Create `chore/m0-bootstrap` from `main`.
  2. `git checkout origin/feat/m1-walking-skeleton -- CLAUDE.md .claude docs/design.md docs/milestones .clang-format .clang-tidy .editorconfig .gitignore`, then add the M0-3 to M0-8 and M0-12 changes.
  3. Open the PR, check C-01 to C-03, merge.
  4. Merge `main` into `feat/m1-walking-skeleton`, keeping `main`'s version of every M0 file. A merge avoids rewriting the pushed branch.
- **Validate:** C-01, C-06.

**M0-10. Apply the `main` ruleset** · S · **you** · D5

- **Details:** settings from D5, applied before M0-9 so the M0 PR is the first one under the rule.
- **Validate:** C-07.

**M0-11. Set up M1 tracking** · S · Claude drafts, you create · M0-4

- **Details:** GitHub Milestone `M1`, labels `phase:A` to `phase:H`, and one issue per *remaining* M1 task. Claude drafts a `gh issue create` script outside the repo; you run it.
- **Validate:** the milestone's open issue count equals the number of remaining M1 tasks.

**M0-12. Repo hygiene gates** · S · Claude Code builds, **you** apply the settings · M0-1

- **Details:** the same gates crows-nest M0 uses, since both repos are public.
  - `.gitleaks.toml` extending the default rules, plus a `private-ipv4` rule (10.0.0.0/8, 172.16.0.0/12, 192.168.0.0/16); one pinned gitleaks release with its SHA-256; a `pre-commit` hook that runs it. M1 task E3 adds the same scan to CI.
  - Docs exported to `docs/` name hosts, never LAN addresses.
  - You: turn on secret scanning and push protection; add a tag ruleset that restricts creating and deleting `v*` tags to you; give Claude Code a repo-scoped deploy key for pushes.
  - Workflows added in M1 pin every action by commit SHA, with Dependabot for the `github-actions` ecosystem.
- **Validate:** C-10, C-11.

## Validation checks

Eleven checks prove M0; each is one command or one look, run from a fresh clone of `main` unless noted. M0 has no code, so these replace a test suite.

| ID | Check | Command or method | Pass when | Tasks |
| --- | --- | --- | --- | --- |
| C-01 | Governance files on `main` | `git ls-tree -r --name-only origin/main` | Lists `CLAUDE.md`, `.claude/settings.json`, `docs/design.md`, `docs/milestones/class-t4 M1 Walking Skeleton.md`, `.clang-format`, `.clang-tidy`, `.editorconfig`, `.gitignore`, `CHANGELOG.md`, `LICENSE`, `NOTICE`, `keys/class-t4.pub` | M0-3 to M0-9 |
| C-02 | No em dashes in docs | `grep -rnP '\x{2014}' CLAUDE.md README.md docs/` | No output | M0-3, M0-4, M0-8 |
| C-03 | Design doc is current | `grep -n 'TBD' docs/design.md`; read the summary table | No decided item marked TBD; crows-nest, 9464 and glibc 2.41 present | M0-2, M0-3 |
| C-04 | Agent cannot touch signing keys | Ask Claude Code on lovelace to read `~/.minisign/class-t4.pub` | Request denied by `settings.json` | M0-8 |
| C-05 | Committed key verifies | `minisign -Sm /tmp/t -s ~/.minisign/class-t4.key`, then `minisign -Vm /tmp/t -p keys/class-t4.pub` | Signature verified | M0-7 |
| C-06 | M1 branch carries only M1 work | `git diff --stat origin/main...origin/feat/m1-walking-skeleton` | No M0 file in the list | M0-9 |
| C-07 | Ruleset active | `gh api repos/BrandonDHaskell/class-t4/rules/branches/main` | Shows deletion, non-fast-forward and pull request rules | M0-10 |
| C-08 | No secrets in history | `git log -p --all \| grep -nE 'secret key\|PRIVATE KEY'` | No output | M0-7, M0-9 |
| C-09 | No invalid workflows | Actions tab after the M0 merge | No "invalid workflow file" runs | M0-6 |
| C-10 | No secrets or LAN addresses | Pinned gitleaks, full-history scan with .gitleaks.toml; a scratch file holding 10.1.2.3 | History is clean; the scratch file fails the scan | M0-12 |
| C-11 | Scanning and tag protection | Settings, Code security; gh api repos/BrandonDHaskell/class-t4/rulesets | Secret scanning and push protection Enabled; a v\* tag ruleset is active | M0-12 |

C-07 reads the ruleset instead of test-pushing to `main`, because a test push would land if the rule were missing.

## Acceptance and sign-off

M0 is done when the checklist below is complete; you are the approver, and M1 work resumes on the updated branch right after.

- [ ] Decisions D1 to D6 confirmed (M0-1)
- [ ] All 12 tasks complete
- [ ] Checks C-01 to C-11 pass
- [ ] Project plan updated: M0 marked done, M1 status reflects the reconciled M1 doc
- [ ] M1 kickoff prompt for Claude Code points at `docs/milestones/class-t4 M1 Walking Skeleton.md` and the next open M1 task

## Risks and open items

The main M0 risk is drift: the planning docs and the code already disagree in five places, and that gap widens with every M1 commit until M0-4 closes it.

| Risk | Response |
| --- | --- |
| Planning docs and code keep drifting | D2 rule in `CLAUDE.md`; M0-4 reconciles before more M1 work merges |
| Merging `main` into the M1 branch conflicts on M0 files | Take `main`'s version of every M0 file; the files differ only in content M0 refreshed |
| Ruleset blocks an urgent fix | Fix through a PR; there is no bypass |
| Secret key lands in the repo | Key generated outside the repo; C-08 checks history |

**Open items for you**

- [ ] Confirm D1 to D6, or change any of them
- [ ] Confirm the `NOTICE` copyright holder name
- [ ] Check whether `main` already has a ruleset or branch protection

## Change log

| Date | Change |
| --- | --- |
| 2026-11-12 | Created from an audit of `main` and `feat/m1-walking-skeleton` |
| 2026-11-12 | Cross-repo review applied: M0-12 repo hygiene gates added (gitleaks with a private-address rule, push protection, v\* tag ruleset, SHA-pinned actions); admin bypass removed from the main ruleset |
| 2026-11-12 | `docs/milestones/m1-walking-skeleton.md` removed; the M1 doc is the canonical M1 spec, so M0-4 no longer regenerates a separate file |
