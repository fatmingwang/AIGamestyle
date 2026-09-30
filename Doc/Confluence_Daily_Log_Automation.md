# Confluence Daily Work Log — Python Automation Guide

## Overview

This guide explains how to use `_pipeline/tools/Confurence/daily_log.ps1` and `jira_daily_log.py` to automatically create and update a daily work log page on Confluence via the Atlassian REST API.

---

## Prerequisites

### 1. Install the Python libraries

```powershell
pip install atlassian-python-api
pip install anthropic        # only needed for AI file analysis
```

### 2. Generate your Atlassian API Token

1. Open: https://id.atlassian.com/manage-profile/security/api-tokens
2. Click **Create API token**
3. Give it a label (e.g. `daily-log-script`)
4. Copy the token — store it only in an environment variable, never in a file or document.

> No admin permission is needed. Any Atlassian account holder can create their own personal token.

### 3. Set environment variables

Open PowerShell and set these before running the script:

```powershell
$env:CONFLUENCE_URL   = "https://yourcompany.atlassian.net"
$env:CONFLUENCE_USER  = "your.email@company.com"
$env:CONFLUENCE_TOKEN = "your-api-token-here"
```

To make these permanent (user-level), add them via System Properties → Environment Variables, or add them to your PowerShell profile (`$PROFILE`).

---

## Script Location

```
_pipeline/tools/Confurence/daily_log.ps1       ← PowerShell entry point (run this)
_pipeline/tools/Confurence/jira_daily_log.py   ← Python script (called by PS1)
```

---

## Usage

### Recommended — run the PS1 script (auto-collects git changes)

```powershell
# Basic: auto-collect today's commits and changed files
.\daily_log.ps1

# With extra notes
.\daily_log.ps1 -Notes "Blocked on texture export, following up tomorrow"

# Against a different repo
.\daily_log.ps1 -RepoPath "D:\Work\OtherProject"
```

The PS1 script:
- Sets the page title to `ElderWang 2026/May/21 log` (git username + today's date)
- Reads today's commits from `git log --since` as tasks
- Reads changed files with status (Added / Modified / Deleted) from `git log --name-status`
- Passes everything to `jira_daily_log.py`

### Advanced — call Python directly

```powershell
python _pipeline\tools\Confurence\jira_daily_log.py `
  --space  APCLIENTV2 `
  --parent "logs" `
  --tasks  "Set up ComfyUI integration" "Fixed asset pipeline bug" `
  --notes  "Will continue tomorrow"
```

### Override the date (e.g. to backfill yesterday)

```powershell
python _pipeline\tools\Confurence\jira_daily_log.py `
  --space  APCLIENTV2 `
  --tasks  "..." `
  --date   2026-05-20
```

---

## Arguments

| Argument        | Required | Description                                                              |
| --------------- | -------- | ------------------------------------------------------------------------ |
| `--space`       | Yes      | Confluence space key (find it in the space URL: `/wiki/spaces/MYSPACE`) |
| `--tasks`       | Yes      | One or more task descriptions (space-separated, quote each one)          |
| `--parent`      | No       | Title of an existing parent page to nest the log under                   |
| `--notes`       | No       | Free-text notes appended as a section at the bottom                      |
| `--date`        | No       | Override date in `YYYY-MM-DD` format. Defaults to today                 |
| `--title`       | No       | Override full page title                                                 |
| `--files`       | No       | Changed files as `STATUS\|path\|commit` entries (set by PS1 automatically) |
| `--ai-analyze`  | No       | Use Claude AI to describe each file change (needs `ANTHROPIC_API_KEY`)  |
| `--repo-path`   | No       | Path to git repo root, used with `--ai-analyze`                         |

---

## AI File Analysis

When `--ai-analyze` is enabled, the script reads the git diff for each changed file and asks Claude to write a one-sentence description of **what the file is for** and **what changed**. The result replaces the raw commit message in the "Changed Files" table on the Confluence page.

### Setup

**1. Get an Anthropic API key**

1. Go to https://console.anthropic.com/
2. Sign in (or create a free account)
3. Click **API Keys** in the left sidebar
4. Click **Create Key**, give it a name (e.g. `daily-log`)
5. Copy the key — it starts with `sk-ant-...`

**2. Set the environment variable**

```powershell
$env:ANTHROPIC_API_KEY = "sk-ant-your-key-here"
```

To make it permanent, add it to your PowerShell profile (`notepad $PROFILE`).

**3. Install the package**

```powershell
pip install anthropic
```

### Running with AI analysis

```powershell
# Via PS1 (recommended)
.\daily_log.ps1 -AiAnalyze

# Via Python directly
python _pipeline\tools\Confurence\jira_daily_log.py `
  --space  APCLIENTV2 `
  --parent "logs" `
  --tasks  "..." `
  --files  "M|src/foo.py|fix bug" "A|src/bar.py|new feature" `
  --ai-analyze `
  --repo-path "<path-to-your-clone>"
```

Replace `<path-to-your-clone>` with wherever you cloned this repository.

### Cost

The script uses `claude-haiku-4-5`, Anthropic's fastest and cheapest model.

| Files per run | Estimated cost |
| ------------- | -------------- |
| 5 files       | ~$0.005        |
| 10 files      | ~$0.01         |
| 20 files      | ~$0.02         |

The free tier includes $5 of credits — enough for hundreds of daily runs.

### Graceful fallback

If `ANTHROPIC_API_KEY` is not set, or the `anthropic` package is not installed, the script prints a warning and falls back to showing the raw commit message instead. The Confluence page is still created normally.

---

## Confluence Page Output

The script creates a page titled `ElderWang 2026/May/21 log` (when run via PS1) with:

- **Tasks Completed** — table of today's commits, each marked as Done
- **Changed Files** — colored table showing Added (green) / Modified (blue) / Deleted (red) files, with either the commit message or AI-generated description
- **Notes** — optional free-text section
- A footer marking the page as auto-generated

Re-running the script on the same day **updates** the existing page rather than creating a duplicate.

---

## Permissions

| What                    | Permission needed                                    |
| ----------------------- | ---------------------------------------------------- |
| Generate Atlassian token | None — any Atlassian user can do this              |
| Generate Anthropic key  | None — any Anthropic account holder can do this     |
| Create pages in a space | "Add Pages" permission in that Confluence space      |
| Update existing pages   | Must be the page creator, or have space admin rights |

If you get a **403 Forbidden** error, ask your Confluence space admin to grant you **Add Pages** permission in the target space.

---

## Troubleshooting

| Error                                     | Cause                             | Fix                                                               |
| ----------------------------------------- | --------------------------------- | ----------------------------------------------------------------- |
| `atlassian-python-api is not installed`   | Library missing                   | `pip install atlassian-python-api`                               |
| `Missing environment variable(s)`         | Env vars not set                  | Set `CONFLUENCE_URL`, `CONFLUENCE_USER`, `CONFLUENCE_TOKEN`      |
| `403 Forbidden`                           | No write permission in that space | Ask space admin for "Add Pages" permission                        |
| `Parent page not found`                   | Wrong parent title                | Check the exact page title in Confluence (case-sensitive)         |
| `anthropic is not installed` (warning)    | Package missing                   | `pip install anthropic` — or run without `-AiAnalyze`            |
| `ANTHROPIC_API_KEY not set` (warning)     | Env var missing                   | Set `$env:ANTHROPIC_API_KEY` — or run without `-AiAnalyze`       |

---

## How It Works (Architecture)

```
daily_log.ps1
  │
  ├─ git log --since today → today's commit messages (tasks)
  ├─ git log --name-status → changed files with status codes
  └─ calls jira_daily_log.py with --tasks, --files, [--ai-analyze]

jira_daily_log.py
  │
  ├─ (if --ai-analyze) for each file:
  │     git diff HEAD~1 HEAD -- <file>  → get file diff
  │     Claude API (claude-haiku-4-5)   → one-sentence description
  │
  ├─ builds Confluence storage-format HTML body
  ├─ connects via atlassian-python-api (REST API v1)
  │     GET  /wiki/rest/api/content?title=...   → check if page exists
  │     POST /wiki/rest/api/content             → create new page
  │     PUT  /wiki/rest/api/content/{id}        → update existing page
  └─ prints the live Confluence URL on success
```

The `atlassian-python-api` library wraps the [Confluence REST API](https://developer.atlassian.com/cloud/confluence/rest/v1/intro/). Authentication uses **Basic Auth** with your email + API token (never your password).

---

## Daily Workflow Recommendation

Add to your PowerShell profile (`notepad $PROFILE`) so you can log your day in one command:

```powershell
# In $PROFILE
Set-Alias daily-log <path-to-your-clone>\_pipeline\tools\Confurence\daily_log.ps1
```

Then use:

```powershell
daily-log                                              # basic
daily-log -Notes "Following up on X tomorrow"         # with notes
daily-log -AiAnalyze                                   # with AI file descriptions
daily-log -AiAnalyze -Notes "Blocked on Y"            # both
```
