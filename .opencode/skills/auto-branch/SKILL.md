---
name: auto-branch
description: |
  Automates branch creation based on work type.
  Integrates with SpecKit to create branches automatically when starting features.
  Standard workflow: start conversation, create branch, request merge approval, then rebase to the target branch.
  Use when starting new work, creating a feature, or fixing a bug.
allowed-tools:
  - Bash
  - Read
  - Write
user-invocable: true
---

# Auto-Branch Skill

## Purpose

This skill automates branch creation and lifecycle management using standard naming conventions and a strict workflow, from conversation initiation to final rebase and merge.

## Standard Workflow

1. **Start conversation**
   - Detect the user's intent: feature, bug, hotfix, and so on.
   - Gather the minimum description required to build a branch name.
2. **Create branch**
   - Generate the branch name using the patterns below.
   - Create and check out the branch with the hook script.
3. **Request merge approval**
   - After the work is completed, explicitly ask the user for permission to merge.
   - Present a concise summary of the changes for review.
4. **Rebase and merge**
   - After approval, rebase the current branch onto the target branch such as `main` or `develop`.
   - Complete the integration flow.

## Branch Patterns

| Type     | Pattern                  | Example                         |
|----------|--------------------------|---------------------------------|
| Bug Fix  | `fix/{description}`      | `fix/api-connection-timeout`    |
| Hotfix   | `hotfix/{description}`   | `hotfix/sql-injection-security` |
| Feature  | `feature/{name}`         | `feature/pdf-export`            |
| Release  | `release/v{version}`     | `release/v1.2.0`                |
| Chore    | `chore/{description}`    | `chore/update-dependencies`     |
| Refactor | `refactor/{description}` | `refactor/split-layers`         |
| Docs     | `docs/{description}`     | `docs/api-reference`            |

## Usage

### Via Command

```text
/auto-branch feature "feature name"
/auto-branch fix "bug description"
```

## Integration with SDLC

### Level 0 (Quick Flow)

When a bug fix or hotfix is detected:

```yaml
trigger:
  prefixes:
    - "fix:"
    - "hotfix:"
  position: start
action:
  command: auto-branch.sh
  args:
    - fix
    - "{description}"
```

### Level 1 (Feature)

When a new feature is detected:

```yaml
trigger:
  prefixes:
    - "feat:"
    - "feature:"
  position: start
action:
  command: auto-branch.sh
  args:
    - feature
    - "{description}"
```

### Level 2+ (Full SDLC)

When full SDLC is initiated:

```yaml
trigger:
  command: /sdlc-start
  min-level: 2
action:
  command: auto-branch.sh
  args:
    - feature
    - "{project-id}"
```

## SpecKit Integration

Upon executing `/spec-create`, the branch is created automatically:

```yaml
spec_create_flow:
  - detect: spec-name
  - execute:
      command: auto-branch.sh
      args:
        - feature
        - "{spec-name}"
  - create: spec-file
  - register: project-manifest
```

## Validations

- Normalize names to lowercase.
- Replace spaces with hyphens.
- Enforce a maximum length of 50 characters.
- Reject special characters.
- If the branch already exists, check it out instead of failing.

## Script Execution

The main script is located at `.claude/hooks/auto-branch.sh`.

### Usage Examples

```bash
# Create feature branch
.claude/hooks/auto-branch.sh feature "PDF Duplicate Export"
# Result: feature/pdf-duplicate-export

# Create fix branch
.claude/hooks/auto-branch.sh fix "CERC connection timeout"
# Result: fix/cerc-connection-timeout

# Rebase phase, only after explicit user approval
git fetch origin
git rebase origin/main
```

## Configuration in settings.json

To enable automatic branch creation, add this to `settings.json`:

```json
{
  "hooks": {
    "PreToolUse": [
      {
        "matcher": "Write(*.spec.md)",
        "hooks": [
          {
            "type": "command",
            "command": ".claude/hooks/auto-branch.sh feature $(basename $TOOL_INPUT_FILE_PATH .spec.md)"
          }
        ]
      }
    ]
  }
}
```

## Troubleshooting

### Branch already exists

If the branch already exists, the script automatically checks it out.

### Uncommitted changes

The script warns if there are uncommitted changes but does not block the operation.

### Name too long

Names are truncated to 50 characters to avoid reference errors.
