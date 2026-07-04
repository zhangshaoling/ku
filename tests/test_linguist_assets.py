import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_ku_textmate_grammar_declares_scope_and_core_tokens():
    grammar_path = ROOT / "syntaxes" / "ku.tmLanguage.json"
    grammar = json.loads(grammar_path.read_text(encoding="utf-8"))

    assert grammar["scopeName"] == "source.ku"
    assert "ku" in grammar["fileTypes"]

    rendered = json.dumps(grammar, ensure_ascii=False)
    for token in ["thought", "self", "let", "return", "fn", "引", "别"]:
        assert token in rendered


def test_linguist_candidate_entry_matches_repository_attributes():
    attrs = (ROOT / ".gitattributes").read_text(encoding="utf-8")
    candidate = (ROOT / "docs" / "linguist" / "languages.yml").read_text(encoding="utf-8")

    assert "*.ku linguist-language=Ku linguist-detectable=true" in attrs
    assert "Ku:" in candidate
    assert 'tm_scope: source.ku' in candidate
    assert 'extensions:' in candidate
    assert '  - ".ku"' in candidate
    assert 'color: "#cba6f7"' in candidate


def test_linguist_sample_exercises_ku_surface_syntax():
    sample = (ROOT / "docs" / "linguist" / "samples" / "Ku" / "example.ku").read_text(encoding="utf-8")

    for phrase in [
        "thought greet",
        "thought fibonacci",
        "self.count",
        '引 "std/math" 别 Math',
        "| map",
    ]:
        assert phrase in sample
