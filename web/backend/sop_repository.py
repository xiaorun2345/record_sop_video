from __future__ import annotations

import json
import sqlite3
from pathlib import Path
from threading import RLock
from typing import Optional

from .sop_domain import create_default_step
from .sop_models import SopDefinition


class SopRepository:
    def __init__(self, database_path: Path):
        self.database_path = database_path
        self.database_path.parent.mkdir(parents=True, exist_ok=True)
        self._lock = RLock()
        self._initialize()

    def list(self) -> list[SopDefinition]:
        with self._connect() as connection:
            rows = connection.execute("SELECT payload FROM sops ORDER BY sequence DESC").fetchall()
        return [SopDefinition.model_validate_json(row[0]) for row in rows]

    def get(self, sop_id: str) -> Optional[SopDefinition]:
        with self._connect() as connection:
            row = connection.execute("SELECT payload FROM sops WHERE id = ?", (sop_id,)).fetchone()
        return SopDefinition.model_validate_json(row[0]) if row else None

    def get_published_version(self, sop_id: str) -> Optional[str]:
        with self._connect() as connection:
            row = connection.execute("SELECT published_version FROM sops WHERE id = ?", (sop_id,)).fetchone()
        return row[0] if row else None

    def insert(self, sop: SopDefinition) -> None:
        with self._lock, self._connect() as connection:
            sequence = connection.execute("SELECT COALESCE(MAX(sequence), 0) + 1 FROM sops").fetchone()[0]
            connection.execute(
                "INSERT INTO sops (id, sequence, payload, published_version) VALUES (?, ?, ?, ?)",
                (sop.id, sequence, self._serialize(sop), sop.version if sop.status == "published" else None),
            )

    def replace(self, sop: SopDefinition, update_published_version: bool = False) -> None:
        with self._lock, self._connect() as connection:
            if update_published_version:
                cursor = connection.execute(
                    "UPDATE sops SET payload = ?, published_version = ? WHERE id = ?",
                    (self._serialize(sop), sop.version, sop.id),
                )
            else:
                cursor = connection.execute(
                    "UPDATE sops SET payload = ? WHERE id = ?",
                    (self._serialize(sop), sop.id),
                )
            if cursor.rowcount == 0:
                raise KeyError(sop.id)

    def remove(self, sop_id: str) -> bool:
        with self._lock, self._connect() as connection:
            cursor = connection.execute("DELETE FROM sops WHERE id = ?", (sop_id,))
            return cursor.rowcount > 0

    def count(self) -> int:
        with self._connect() as connection:
            return int(connection.execute("SELECT COUNT(*) FROM sops").fetchone()[0])

    def _initialize(self) -> None:
        with self._lock, self._connect() as connection:
            connection.execute(
                """
                CREATE TABLE IF NOT EXISTS sops (
                    id TEXT PRIMARY KEY,
                    sequence INTEGER NOT NULL,
                    payload TEXT NOT NULL,
                    published_version TEXT
                )
                """
            )
            self._migrate_records(connection)

    def _migrate_records(self, connection: sqlite3.Connection) -> None:
        """Normalize records written by earlier frontend-schema revisions."""
        removed_step_fields = {
            "allowSkip",
            "allowRetry",
        }
        rows = connection.execute("SELECT id, payload FROM sops").fetchall()
        for sop_id, raw_payload in rows:
            payload = json.loads(raw_payload)
            changed = False
            if "executionMode" not in payload:
                payload["executionMode"] = "ordered"
                changed = True
            if not payload.get("steps"):
                payload["steps"] = [create_default_step().model_dump(mode="json")]
                changed = True
            for step in payload.get("steps", []):
                for field in removed_step_fields:
                    if field in step:
                        step.pop(field)
                        changed = True
                for area in step.get("roiAreas", []):
                    points = area.get("points", [])
                    uses_legacy_percentages = any(
                        point.get("x", 0) > 1 or point.get("y", 0) > 1
                        for point in points
                    )
                    if uses_legacy_percentages:
                        for point in points:
                            point["x"] = max(0, min(1, point.get("x", 0) / 100))
                            point["y"] = max(0, min(1, point.get("y", 0) / 100))
                        changed = True
                for required_object in step.get("requiredObjects", []):
                    relation = required_object.get("relation")
                    if relation and relation.get("type") != "overlaps":
                        relation["type"] = "overlaps"
                        changed = True

            if changed:
                normalized = SopDefinition.model_validate(payload)
                connection.execute(
                    "UPDATE sops SET payload = ? WHERE id = ?",
                    (self._serialize(normalized), sop_id),
                )

    def _connect(self) -> sqlite3.Connection:
        connection = sqlite3.connect(self.database_path, timeout=10)
        connection.execute("PRAGMA journal_mode=WAL")
        connection.execute("PRAGMA foreign_keys=ON")
        return connection

    @staticmethod
    def _serialize(sop: SopDefinition) -> str:
        return json.dumps(sop.model_dump(mode="json"), ensure_ascii=False, separators=(",", ":"))
