# Codex Project Rules

Keep request prefixes stable for better cache reuse.

Do not scan or summarize these paths unless the user explicitly asks:

- `.git/`
- `__pycache__/`
- `.pytest_cache/`
- `pytest-cache-files-*/`
- `.codegraph/`
- `backups/`
- `scratch/`
- `node_modules/`
- `venv/`
- `.venv/`
- `dist/`
- `build/`
- `*.log`

Prefer reading only the files needed for the current task. Avoid broad recursive scans from `D:\Tools` or other parent directories.

Use the existing project test commands and keep changes scoped to the requested task.
