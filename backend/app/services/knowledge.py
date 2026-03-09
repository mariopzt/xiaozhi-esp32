from __future__ import annotations

import json
from pathlib import Path
from typing import Any


class KnowledgeService:
    def __init__(self, knowledge_dir: Path) -> None:
        self._knowledge_dir = knowledge_dir

    async def ensure_directory(self) -> None:
        self._knowledge_dir.mkdir(parents=True, exist_ok=True)

    def render_context_block(self) -> str:
        sections: list[str] = []
        if not self._knowledge_dir.exists():
            return ""

        for path in sorted(self._knowledge_dir.iterdir()):
            if not path.is_file() or path.name.startswith("_"):
                continue
            if path.suffix.lower() not in {".json", ".md", ".txt"}:
                continue

            content = self._render_file(path)
            if not content:
                continue
            sections.append(f"{path.stem}:\n{content}")

        if not sections:
            return ""
        return "Knowledge base:\n" + "\n\n".join(f"- {section}" for section in sections)

    def _render_file(self, path: Path) -> str:
        if path.suffix.lower() == ".json":
            try:
                data = json.loads(path.read_text(encoding="utf-8"))
            except json.JSONDecodeError:
                return ""
            lines = self._render_json("", data)
            return "\n".join(lines)

        text = path.read_text(encoding="utf-8").strip()
        return text

    def _render_json(self, prefix: str, value: Any) -> list[str]:
        if value in (None, "", [], {}):
            return []

        if isinstance(value, dict):
            lines: list[str] = []
            for key, child in value.items():
                child_prefix = f"{prefix}{key}" if not prefix else f"{prefix}.{key}"
                lines.extend(self._render_json(child_prefix, child))
            return lines

        if isinstance(value, list):
            lines: list[str] = []
            for item in value:
                if isinstance(item, (dict, list)):
                    lines.extend(self._render_json(prefix, item))
                elif item not in (None, ""):
                    lines.append(f"{prefix}: {item}")
            return lines

        return [f"{prefix}: {value}"]
