# Ku Linguist and Syntax Highlighting Assets

This directory collects the repository-local assets needed before proposing Ku to
GitHub Linguist.

## What is included

- `../../syntaxes/ku.tmLanguage.json` — a TextMate grammar with scope
  `source.ku` for editor syntax highlighting and future grammar packaging.
- `languages.yml` — a draft `github-linguist/linguist` language entry for Ku.
- `samples/Ku/example.ku` — a representative Linguist sample covering thoughts,
  imports, self-rewriting, pipes, anonymous functions, strings, numbers, and
  comments.

## Why this exists

GitHub's language bar is driven by `github-linguist`. A repository can mark
`*.ku` as detectable in `.gitattributes`, but GitHub will not show Ku as a
first-class language until Linguist knows the language name, extension, scope,
and samples.

These files make the future upstream PR small and reviewable. Once Ku/Dao's
surface syntax stabilizes, copy the draft entry and sample into a fork of
`github-linguist/linguist`, assign the final `language_id`, and submit the PR.

## Current repository setting

The root `.gitattributes` keeps Ku files detectable:

```gitattributes
*.ku linguist-language=Ku linguist-detectable=true
```

Until upstream Linguist accepts Ku, GitHub may still omit `.ku` files from the
language breakdown or treat the override as unknown.
