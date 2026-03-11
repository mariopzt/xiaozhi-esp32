from __future__ import annotations

import re
from datetime import timedelta, timezone
from typing import Any
from zoneinfo import ZoneInfo, ZoneInfoNotFoundError

from motor.motor_asyncio import AsyncIOMotorDatabase

from ..models import MemoryContext, utc_now


class MemoryService:
    def __init__(self, database: AsyncIOMotorDatabase) -> None:
        self._profiles = database["profiles"]
        self._memories = database["memories"]
        self._reminders = database["reminders"]
        self._summaries = database["summaries"]
        self._sessions = database["sessions"]
        self._brains = database["brains"]
        self._turns = database["turns"]
        try:
            self._timezone = ZoneInfo("Europe/Madrid")
        except ZoneInfoNotFoundError:
            self._timezone = timezone.utc

    async def ensure_indexes(self) -> None:
        await self._backfill_turn_indexes()
        await self._profiles.create_index("user_id", unique=True)
        await self._brains.create_index("user_id", unique=True)
        await self._memories.create_index([("user_id", 1), ("created_at", -1)])
        await self._reminders.create_index([("user_id", 1), ("status", 1), ("created_at", -1)])
        await self._summaries.create_index([("user_id", 1), ("created_at", -1)])
        await self._summaries.create_index([("user_id", 1), ("session_id", 1)])
        await self._sessions.create_index([("user_id", 1), ("last_turn_at", -1)])
        await self._sessions.create_index([("user_id", 1), ("session_id", 1)], unique=True)
        await self._turns.create_index([("user_id", 1), ("created_at", -1)])
        await self._turns.create_index([("user_id", 1), ("session_id", 1), ("created_at", -1)])
        await self._turns.create_index([("user_id", 1), ("session_id", 1), ("turn_index", 1)], unique=True)

    async def _backfill_turn_indexes(self) -> None:
        cursor = self._turns.find(
            {"turn_index": {"$exists": False}},
            {"_id": 1, "user_id": 1, "session_id": 1, "created_at": 1},
        ).sort([("user_id", 1), ("session_id", 1), ("created_at", 1), ("_id", 1)])

        counters: dict[tuple[str, str], int] = {}
        async for doc in cursor:
            user_id = doc.get("user_id") or "unknown-user"
            session_id = doc.get("session_id") or "unknown-session"
            key = (user_id, session_id)
            next_index = counters.get(key)
            if next_index is None:
                last_doc = await self._turns.find_one(
                    {
                        "user_id": user_id,
                        "session_id": session_id,
                        "turn_index": {"$exists": True},
                    },
                    {"_id": 0, "turn_index": 1},
                    sort=[("turn_index", -1)],
                )
                next_index = int(last_doc.get("turn_index", 0)) + 1 if last_doc else 1

            await self._turns.update_one(
                {"_id": doc["_id"]},
                {"$set": {"turn_index": next_index}},
            )
            counters[key] = next_index + 1

    async def save_turn(
        self,
        user_id: str,
        session_id: str,
        role: str,
        text: str,
        *,
        device_id: str = "",
    ) -> None:
        normalized = self._normalize_text(text)
        if not normalized:
            return

        turn_index = await self._next_turn_index(user_id, session_id)
        now = utc_now()
        await self._turns.insert_one(
            {
                "user_id": user_id,
                "session_id": session_id,
                "device_id": device_id or user_id,
                "turn_index": turn_index,
                "role": role,
                "text": normalized,
                "normalized_text": normalized.casefold(),
                "tokens": sorted(self._tokenize(normalized)),
                "created_at": now,
            }
        )
        await self._update_session_archive(
            user_id=user_id,
            session_id=session_id,
            device_id=device_id or user_id,
            role=role,
            text=normalized,
            created_at=now,
        )
        await self._update_brain_archive(
            user_id=user_id,
            session_id=session_id,
            device_id=device_id or user_id,
            role=role,
            created_at=now,
        )

    async def learn_from_user_text(self, user_id: str, text: str) -> list[str]:
        normalized = " ".join(text.split()).strip()
        if not normalized:
            return []

        saved: list[str] = []
        profile_updates = self._extract_profile_updates(normalized)
        tone_signal = self._extract_tone_signal(normalized)
        if profile_updates:
            await self._profiles.update_one(
                {"user_id": user_id},
                {"$set": {**profile_updates, "updated_at": utc_now()}, "$setOnInsert": {"created_at": utc_now()}},
                upsert=True,
            )
            saved.extend(self._format_profile_updates(profile_updates))
        if tone_signal:
            await self._apply_tone_signal(user_id, tone_signal)

        explicit_memory = self._extract_explicit_memory(normalized)
        if explicit_memory:
            await self._store_memory(user_id, explicit_memory, "explicit_remember")
            saved.append(explicit_memory)

        reminder_saved = await self._store_reminder_from_text(user_id, normalized)
        if reminder_saved:
            saved.append(reminder_saved)

        reminder_done = await self._complete_reminder_from_text(user_id, normalized)
        if reminder_done:
            saved.append(reminder_done)

        for memory in self._extract_implicit_memories(normalized):
            await self._store_memory(user_id, memory, "implicit_fact")
            if memory not in saved:
                saved.append(memory)

        return saved

    async def get_context(self, user_id: str, query: str = "") -> MemoryContext:
        profile = await self._profiles.find_one({"user_id": user_id}, {"_id": 0, "user_id": 0}) or {}
        brain_doc = await self._brains.find_one({"user_id": user_id}, {"_id": 0, "user_id": 0}) or {}
        memory_docs = await self._memories.find(
            {"user_id": user_id},
            {"_id": 0, "text": 1, "created_at": 1},
        ).sort("created_at", -1).limit(40).to_list(length=40)
        turn_docs = await self._turns.find(
            {"user_id": user_id},
            {"_id": 0, "role": 1, "text": 1, "created_at": 1},
        ).sort("created_at", -1).limit(10).to_list(length=10)
        history_docs = await self._turns.find(
            {"user_id": user_id},
            {"_id": 0, "role": 1, "text": 1, "session_id": 1, "created_at": 1},
        ).sort("created_at", -1).limit(160).to_list(length=160)
        summary_docs = await self._summaries.find(
            {"user_id": user_id},
            {"_id": 0, "summary": 1, "session_id": 1, "created_at": 1},
        ).sort("created_at", -1).limit(80).to_list(length=80)
        session_docs = await self._sessions.find(
            {"user_id": user_id},
            {
                "_id": 0,
                "session_id": 1,
                "turn_count": 1,
                "last_turn_at": 1,
                "last_summary": 1,
                "excerpt": 1,
                "last_user_text": 1,
                "last_assistant_text": 1,
            },
        ).sort("last_turn_at", -1).limit(24).to_list(length=24)
        reminder_docs = await self._reminders.find(
            {"user_id": user_id, "status": "pending"},
            {"_id": 0, "text": 1, "due_hint": 1, "due_date": 1, "created_at": 1},
        ).sort("created_at", -1).limit(12).to_list(length=12)

        selected_memories = self._select_relevant_memories(query, memory_docs)
        relevant_turns = self._select_relevant_turns(query, history_docs)
        summaries = self._select_relevant_summaries(query, summary_docs)
        session_summaries = self._select_relevant_sessions(query, session_docs)
        reminders = self._select_relevant_reminders(query, reminder_docs)
        archive_stats = self._build_archive_stats(brain_doc, history_docs, session_docs)
        style_state = self._build_style_state(profile, turn_docs)

        return MemoryContext(
            profile=profile,
            memories=selected_memories,
            recent_turns=list(reversed(turn_docs)),
            style_state=style_state,
            relevant_turns=relevant_turns,
            summaries=summaries,
            session_summaries=session_summaries,
            reminders=reminders,
            archive_stats=archive_stats,
        )

    def render_context_block(self, context: MemoryContext) -> str:
        lines: list[str] = []

        if context.archive_stats:
            lines.append("Conversation archive:")
            total_turns = context.archive_stats.get("total_turns")
            total_sessions = context.archive_stats.get("total_sessions")
            if total_turns is not None:
                lines.append(f"- Total stored turns: {total_turns}")
            if total_sessions is not None:
                lines.append(f"- Total stored sessions: {total_sessions}")

        profile_lines = self._render_profile_lines(context.profile)
        if profile_lines:
            lines.append("Known user profile:")
            lines.extend(f"- {line}" for line in profile_lines)

        style_lines = self._render_style_lines(context.style_state)
        if style_lines:
            if lines:
                lines.append("")
            lines.append("Conversation style:")
            lines.extend(f"- {line}" for line in style_lines)

        if context.memories:
            if lines:
                lines.append("")
            lines.append("Explicit memories:")
            lines.extend(f"- {item}" for item in context.memories)

        if context.recent_turns:
            if lines:
                lines.append("")
            lines.append("Recent conversation:")
            lines.extend(f"- {turn['role']}: {turn['text']}" for turn in context.recent_turns)

        if context.relevant_turns:
            if lines:
                lines.append("")
            lines.append("Relevant past conversation:")
            lines.extend(f"- {turn['role']}: {turn['text']}" for turn in context.relevant_turns)

        if context.summaries:
            if lines:
                lines.append("")
            lines.append("Past conversation summaries:")
            lines.extend(f"- {item}" for item in context.summaries)

        if context.session_summaries:
            if lines:
                lines.append("")
            lines.append("Session archive:")
            lines.extend(f"- {item}" for item in context.session_summaries)

        if context.reminders:
            if lines:
                lines.append("")
            lines.append("Pending reminders:")
            lines.extend(f"- {item}" for item in context.reminders)

        return "\n".join(lines)

    async def save_interaction_summary(self, user_id: str, session_id: str, user_text: str, assistant_text: str) -> None:
        summary = self._build_interaction_summary(user_text, assistant_text)
        if not summary:
            return
        now = utc_now()
        await self._summaries.insert_one(
            {
                "user_id": user_id,
                "session_id": session_id,
                "summary": summary,
                "keywords": sorted(self._tokenize(f"{user_text} {assistant_text}")),
                "created_at": now,
            }
        )
        await self._sessions.update_one(
            {"user_id": user_id, "session_id": session_id},
            {
                "$set": {
                    "last_summary": summary,
                    "updated_at": now,
                },
                "$push": {
                    "recent_summaries": {
                        "$each": [summary],
                        "$slice": -8,
                    }
                },
            },
            upsert=True,
        )
        await self._brains.update_one(
            {"user_id": user_id},
            {
                "$set": {
                    "last_summary_at": now,
                    "updated_at": now,
                },
                "$inc": {"summary_count": 1},
                "$setOnInsert": {"created_at": now},
            },
            upsert=True,
        )

    async def export_device_memory(self, user_id: str, query: str = "") -> dict[str, str]:
        context = await self.get_context(user_id, query)
        profile = context.profile or {}
        user_name = str(profile.get("name") or "").strip()

        notes_lines: list[str] = []
        for item in self._render_profile_lines(profile):
            if item and item not in notes_lines:
                notes_lines.append(item)
        for item in context.memories:
            cleaned = str(item).strip()
            if cleaned and cleaned not in notes_lines and self._is_useful_memory_line(cleaned):
                notes_lines.append(cleaned)

        recent_lines: list[str] = []
        for turn in context.recent_turns[-20:]:
            role = str(turn.get("role", "")).strip().lower()
            text = str(turn.get("text", "")).strip()
            if not text:
                continue
            speaker = "U" if role == "user" else "A"
            recent_lines.append(f"{speaker}: {text}")

        direct_lines = self._build_direct_fact_lines(query, profile)
        combined_context = self.render_context_block(context)
        if direct_lines:
            direct_block = "Direct memory matches:\n" + "\n".join(f"- {line}" for line in direct_lines)
            combined_context = direct_block + ("\n\n" + combined_context if combined_context else "")

        return {
            "user_name": user_name,
            "notes": "\n".join(notes_lines),
            "recent_turns": "\n".join(recent_lines),
            "combined_context": combined_context,
        }

    async def import_device_snapshot(
        self,
        user_id: str,
        *,
        user_name: str = "",
        notes: str = "",
        recent_turns: str = "",
    ) -> None:
        clean_name = " ".join((user_name or "").split()).strip()
        now = utc_now()
        if clean_name:
            await self._profiles.update_one(
                {"user_id": user_id},
                {"$set": {"name": clean_name, "updated_at": now}, "$setOnInsert": {"created_at": now}},
                upsert=True,
            )

        for raw_line in (notes or "").splitlines():
            line = " ".join(raw_line.split()).strip()
            if not line:
                continue
            await self._store_memory(user_id, line, "device_snapshot")

        for raw_line in (recent_turns or "").splitlines():
            line = " ".join(raw_line.split()).strip()
            if len(line) < 4 or line[1:3] != ": ":
                continue
            speaker = line[0].upper()
            role = "user" if speaker == "U" else "assistant"
            text = line[3:].strip()
            normalized = self._normalize_text(text)
            if not normalized:
                continue

            existing = await self._turns.find_one(
                {
                    "user_id": user_id,
                    "session_id": "device_snapshot",
                    "role": role,
                    "normalized_text": normalized.casefold(),
                },
                {"_id": 1},
            )
            if existing is not None:
                continue

            await self.save_turn(
                user_id=user_id,
                session_id="device_snapshot",
                role=role,
                text=normalized,
                device_id=user_id,
            )
            if role == "user":
                await self.learn_from_user_text(user_id, normalized)

    async def remember_note(self, user_id: str, note: str) -> None:
        normalized = self._normalize_text(note)
        if not normalized:
            return
        await self._store_memory(user_id, normalized, "device_remember")

    async def clear_device_memory(self, user_id: str) -> None:
        await self._profiles.delete_many({"user_id": user_id})
        await self._memories.delete_many({"user_id": user_id})
        await self._reminders.delete_many({"user_id": user_id})
        await self._summaries.delete_many({"user_id": user_id})
        await self._sessions.delete_many({"user_id": user_id})
        await self._brains.delete_many({"user_id": user_id})
        await self._turns.delete_many({"user_id": user_id})

    async def _next_turn_index(self, user_id: str, session_id: str) -> int:
        last_turn = await self._turns.find_one(
            {"user_id": user_id, "session_id": session_id},
            {"_id": 0, "turn_index": 1},
            sort=[("turn_index", -1)],
        )
        if last_turn is None:
            return 1
        return int(last_turn.get("turn_index", 0)) + 1

    async def _update_session_archive(
        self,
        *,
        user_id: str,
        session_id: str,
        device_id: str,
        role: str,
        text: str,
        created_at,
    ) -> None:
        excerpt_line = f"{role}: {text[:180]}"
        session_update: dict[str, Any] = {
            "$set": {
                "updated_at": created_at,
                "last_turn_at": created_at,
            },
            "$setOnInsert": {
                "user_id": user_id,
                "session_id": session_id,
                "created_at": created_at,
                "started_at": created_at,
            },
            "$inc": {
                "turn_count": 1,
                f"role_counts.{role}": 1,
            },
            "$push": {
                "excerpt": {
                    "$each": [excerpt_line],
                    "$slice": -12,
                }
            },
        }
        if device_id:
            session_update["$addToSet"] = {"device_ids": device_id}
        if role == "user":
            session_update["$set"]["last_user_text"] = text
        else:
            session_update["$set"]["last_assistant_text"] = text

        result = await self._sessions.update_one(
            {"user_id": user_id, "session_id": session_id},
            session_update,
            upsert=True,
        )
        if result.upserted_id is not None:
            await self._brains.update_one(
                {"user_id": user_id},
                {
                    "$inc": {"session_count": 1},
                    "$set": {"updated_at": created_at},
                    "$setOnInsert": {"created_at": created_at},
                },
                upsert=True,
            )

    async def _update_brain_archive(
        self,
        *,
        user_id: str,
        session_id: str,
        device_id: str,
        role: str,
        created_at,
    ) -> None:
        update: dict[str, Any] = {
            "$set": {
                "last_seen_at": created_at,
                "last_session_id": session_id,
                "updated_at": created_at,
            },
            "$setOnInsert": {
                "created_at": created_at,
                "session_count": 0,
                "summary_count": 0,
            },
            "$inc": {
                "total_turns": 1,
                f"role_counts.{role}": 1,
            },
        }
        if device_id:
            update["$addToSet"] = {"device_ids": device_id}
        await self._brains.update_one({"user_id": user_id}, update, upsert=True)

    def _build_archive_stats(
        self,
        brain_doc: dict[str, Any],
        history_docs: list[dict[str, Any]],
        session_docs: list[dict[str, Any]],
    ) -> dict[str, Any]:
        stats: dict[str, Any] = {}
        total_turns = brain_doc.get("total_turns")
        total_sessions = brain_doc.get("session_count")
        if total_turns is None:
            total_turns = len(history_docs)
        if total_sessions is None:
            total_sessions = len(session_docs)
        stats["total_turns"] = total_turns
        stats["total_sessions"] = total_sessions
        return stats

    def _select_relevant_sessions(self, query: str, session_docs: list[dict[str, Any]]) -> list[str]:
        if not session_docs:
            return []

        keywords = self._tokenize(query)
        scored: list[tuple[int, int, str]] = []
        for index, doc in enumerate(session_docs):
            summary = self._render_session_summary(doc)
            if not summary:
                continue
            overlap = len(self._tokenize(summary) & keywords) if keywords else 0
            if keywords and overlap == 0 and index > 4:
                continue
            score = overlap * 14 + max(0, 24 - index)
            scored.append((score, -index, summary))

        selected = [text for _score, _idx, text in sorted(scored, reverse=True)[:6]]
        selected.reverse()
        return selected

    def _render_session_summary(self, doc: dict[str, Any]) -> str:
        if doc.get("last_summary"):
            return doc["last_summary"]

        excerpt = [item for item in doc.get("excerpt", []) if item]
        if excerpt:
            sample = " | ".join(excerpt[-3:])
            return f"Session with {doc.get('turn_count', 0)} turns: {sample}"

        last_user = doc.get("last_user_text", "").strip()
        last_assistant = doc.get("last_assistant_text", "").strip()
        if last_user or last_assistant:
            return f"Last exchange: user={last_user} assistant={last_assistant}".strip()
        return ""

    def _normalize_text(self, text: str) -> str:
        return " ".join(text.split()).strip()

    def _is_useful_memory_line(self, text: str) -> bool:
        lowered = text.casefold().strip()
        if not lowered:
            return False
        if lowered.startswith("%"):
            return False
        if "unknown_" in lowered:
            return False
        if lowered in {"memo.", "memo", "número 2.", "numero 2.", "número 2", "numero 2"}:
            return False
        return True

    def _build_direct_fact_lines(self, query: str, profile: dict[str, Any]) -> list[str]:
        lowered = (query or "").casefold()
        lines: list[str] = []

        def add_if(condition: bool, value: Any, template: str) -> None:
            if condition and value:
                lines.append(template.format(value))

        partner_terms = ("mujer", "esposa", "esposo", "pareja", "novia", "novio")
        dog_terms = ("perro", "perra")
        cat_terms = ("gato", "gata")
        mother_terms = ("madre",)
        father_terms = ("padre",)
        son_terms = ("hijo",)
        daughter_terms = ("hija",)

        add_if(any(term in lowered for term in ("como me llamo", "cómo me llamo", "mi nombre", "quien soy", "quién soy")), profile.get("name"), "The user's name is {}.")
        add_if(any(term in lowered for term in partner_terms), profile.get("partner_name"), "The user's partner is {}.")
        add_if(any(term in lowered for term in dog_terms), profile.get("dog_name"), "The user's dog is {}.")
        add_if(any(term in lowered for term in cat_terms), profile.get("cat_name"), "The user's cat is {}.")
        add_if(any(term in lowered for term in mother_terms), profile.get("mother_name"), "The user's mother is {}.")
        add_if(any(term in lowered for term in father_terms), profile.get("father_name"), "The user's father is {}.")
        add_if(any(term in lowered for term in son_terms), profile.get("son_name"), "The user's son is {}.")
        add_if(any(term in lowered for term in daughter_terms), profile.get("daughter_name"), "The user's daughter is {}.")
        add_if(any(term in lowered for term in ("trabajo", "trabajas", "curro", "me dedico")), profile.get("work"), "The user's work is {}.")
        add_if(any(term in lowered for term in ("vives", "vivo", "ciudad", "eres de", "soy de", "donde", "dónde")), profile.get("city"), "The user lives in or is from {}.")

        return lines

    async def _store_memory(self, user_id: str, text: str, kind: str) -> None:
        if not self._should_store_memory_text(text, kind):
            return
        existing = await self._memories.find_one({"user_id": user_id, "text": text})
        if existing is not None:
            return
        await self._memories.insert_one(
            {
                "user_id": user_id,
                "kind": kind,
                "text": text,
                "created_at": utc_now(),
            }
        )

    def _should_store_memory_text(self, text: str, kind: str) -> bool:
        normalized = self._normalize_text(text)
        if not normalized:
            return False

        lowered = normalized.casefold()
        if kind not in {"explicit_remember", "device_remember"}:
            if "?" in normalized or "¿" in normalized:
                return False
            if len(normalized) < 8:
                return False
            if lowered.startswith(("como ", "cómo ", "que ", "qué ", "cual ", "cuál ")):
                return False
            if lowered in {"si", "sí", "ok", "vale", "memo", "numero 2", "número 2"}:
                return False
            letters = sum(1 for ch in normalized if ch.isalpha())
            if letters < 4:
                return False
        return True

    async def _store_reminder_from_text(self, user_id: str, text: str) -> str:
        reminder = self._extract_reminder(text)
        if not reminder:
            return ""

        existing = await self._reminders.find_one(
            {
                "user_id": user_id,
                "status": "pending",
                "text": reminder["text"],
                "due_hint": reminder["due_hint"],
            }
        )
        if existing is not None:
            return ""

        await self._reminders.insert_one(
            {
                "user_id": user_id,
                "text": reminder["text"],
                "due_hint": reminder["due_hint"],
                "due_date": reminder["due_date"],
                "status": "pending",
                "created_at": utc_now(),
            }
        )
        if reminder["due_hint"]:
            return f"Reminder saved: {reminder['text']} ({reminder['due_hint']})."
        return f"Reminder saved: {reminder['text']}."

    async def _complete_reminder_from_text(self, user_id: str, text: str) -> str:
        target = self._extract_reminder_completion(text)
        if not target:
            return ""

        reminder = await self._reminders.find_one(
            {
                "user_id": user_id,
                "status": "pending",
                "text": {"$regex": re.escape(target), "$options": "i"},
            },
            sort=[("created_at", -1)],
        )
        if reminder is None:
            return ""

        await self._reminders.update_one(
            {"_id": reminder["_id"]},
            {"$set": {"status": "done", "done_at": utc_now()}},
        )
        return f"Reminder completed: {reminder['text']}."

    def _extract_explicit_memory(self, text: str) -> str:
        patterns = [
            r"^recuerda que\s+(.+)$",
            r"^recuerda\s+(.+)$",
            r"^acu[e\u00e9]rdate de que\s+(.+)$",
            r"^acu[e\u00e9]rdate que\s+(.+)$",
        ]
        lowered = text.lower()
        for pattern in patterns:
            match = re.match(pattern, lowered, flags=re.IGNORECASE)
            if not match:
                continue
            start = match.start(1)
            return text[start:].strip(" .")
        return ""

    def _extract_profile_updates(self, text: str) -> dict[str, Any]:
        updates: dict[str, Any] = {}

        name_match = re.search(
            r"\b(me llamo|mi nombre es|ll[a\u00e1]mame)\s+([A-Za-z\u00c1\u00c9\u00cd\u00d3\u00da\u00e1\u00e9\u00ed\u00f3\u00fa\u00d1\u00f1 ]{1,40})",
            text,
            re.IGNORECASE,
        )
        if name_match:
            updates["name"] = " ".join(name_match.group(2).split())[:40]

        age_match = re.search(r"\btengo\s+(\d{1,3})\s+a(?:\u00f1|n)os\b", text, re.IGNORECASE)
        if age_match:
            updates["age"] = int(age_match.group(1))

        city_match = re.search(r"\b(vivo en|soy de)\s+([A-Za-z\u00c1\u00c9\u00cd\u00d3\u00da\u00e1\u00e9\u00ed\u00f3\u00fa\u00d1\u00f1' -]{2,40})", text, re.IGNORECASE)
        if city_match:
            updates["city"] = " ".join(city_match.group(2).split())[:40]

        work_match = re.search(r"\b(trabajo en|trabajo de|trabajo como|me dedico a)\s+(.{2,60})$", text, re.IGNORECASE)
        if work_match:
            updates["work"] = work_match.group(2).strip(" .")[:60]

        relation_patterns = {
            "partner_name": r"\bmi (?:pareja|novia|novio|mujer|esposa|esposo)\s+se llama\s+([A-Za-z\u00c1-\u00ff' -]{1,40})",
            "mother_name": r"\bmi madre\s+se llama\s+([A-Za-z\u00c1-\u00ff' -]{1,40})",
            "father_name": r"\bmi padre\s+se llama\s+([A-Za-z\u00c1-\u00ff' -]{1,40})",
            "sister_name": r"\bmi hermana\s+se llama\s+([A-Za-z\u00c1-\u00ff' -]{1,40})",
            "brother_name": r"\bmi hermano\s+se llama\s+([A-Za-z\u00c1-\u00ff' -]{1,40})",
            "daughter_name": r"\bmi hija\s+se llama\s+([A-Za-z\u00c1-\u00ff' -]{1,40})",
            "son_name": r"\bmi hijo\s+se llama\s+([A-Za-z\u00c1-\u00ff' -]{1,40})",
            "dog_name": r"\bmi perr(?:o|a)\s+se llama\s+([A-Za-z\u00c1-\u00ff' -]{1,40})",
            "cat_name": r"\bmi gat(?:o|a)\s+se llama\s+([A-Za-z\u00c1-\u00ff' -]{1,40})",
        }
        for key, pattern in relation_patterns.items():
            match = re.search(pattern, text, re.IGNORECASE)
            if match:
                updates[key] = " ".join(match.group(1).split())[:40]

        partner_name_match = re.search(
            r"\bel nombre de mi (?:mujer|esposa|esposo|pareja) es\s+([A-Za-z\u00c1-\u00ff' -]{1,40})",
            text,
            re.IGNORECASE,
        )
        if partner_name_match:
            updates["partner_name"] = " ".join(partner_name_match.group(1).split())[:40]

        return updates

    def _format_profile_updates(self, updates: dict[str, Any]) -> list[str]:
        items: list[str] = []
        if "name" in updates:
            items.append(f"The user's name is {updates['name']}.")
        if "age" in updates:
            items.append(f"The user is {updates['age']} years old.")
        if "city" in updates:
            items.append(f"The user lives in or is from {updates['city']}.")
        if "work" in updates:
            items.append(f"The user's work is {updates['work']}.")
        if "relationship_tone" in updates:
            items.append(f"The preferred relationship tone is {updates['relationship_tone']}.")
        if "assistant_style" in updates:
            items.append(f"The preferred assistant style is {updates['assistant_style']}.")
        relation_labels = {
            "partner_name": "The user's partner is",
            "mother_name": "The user's mother is",
            "father_name": "The user's father is",
            "sister_name": "The user's sister is",
            "brother_name": "The user's brother is",
            "daughter_name": "The user's daughter is",
            "son_name": "The user's son is",
            "dog_name": "The user's dog is",
            "cat_name": "The user's cat is",
        }
        for key, label in relation_labels.items():
            if key in updates:
                items.append(f"{label} {updates[key]}.")
        return items

    def _render_profile_lines(self, profile: dict[str, Any]) -> list[str]:
        lines: list[str] = []
        if profile.get("name"):
            lines.append(f"Name: {profile['name']}")
        if profile.get("age") is not None:
            lines.append(f"Age: {profile['age']}")
        if profile.get("city"):
            lines.append(f"City: {profile['city']}")
        if profile.get("work"):
            lines.append(f"Work: {profile['work']}")
        if profile.get("relationship_tone"):
            lines.append(f"Relationship tone: {profile['relationship_tone']}")
        if profile.get("assistant_style"):
            lines.append(f"Assistant style: {profile['assistant_style']}")
        relation_labels = {
            "partner_name": "Partner",
            "mother_name": "Mother",
            "father_name": "Father",
            "sister_name": "Sister",
            "brother_name": "Brother",
            "daughter_name": "Daughter",
            "son_name": "Son",
            "dog_name": "Dog",
            "cat_name": "Cat",
        }
        for key, label in relation_labels.items():
            if profile.get(key):
                lines.append(f"{label}: {profile[key]}")
        for key, value in profile.items():
            if key in {
                "name", "age", "city", "work", "relationship_tone", "assistant_style",
                "relationship_tone_scores", "assistant_style_scores",
                "partner_name", "mother_name", "father_name", "sister_name", "brother_name",
                "daughter_name", "son_name", "dog_name", "cat_name",
                "created_at", "updated_at",
            }:
                continue
            lines.append(f"{key}: {value}")
        return lines

    def _build_interaction_summary(self, user_text: str, assistant_text: str) -> str:
        user_text = " ".join(user_text.split()).strip()
        assistant_text = " ".join(assistant_text.split()).strip()
        if not user_text or not assistant_text:
            return ""
        if len(user_text) > 140:
            user_text = user_text[:137].rstrip() + "..."
        if len(assistant_text) > 180:
            assistant_text = assistant_text[:177].rstrip() + "..."
        return f"User said: {user_text} Assistant replied: {assistant_text}"

    async def _apply_tone_signal(self, user_id: str, signal: dict[str, Any]) -> None:
        now = utc_now()
        profile = await self._profiles.find_one({"user_id": user_id}, {"_id": 0}) or {}

        relationship_tone = str(signal.get("relationship_tone") or "").strip()
        assistant_style = str(signal.get("assistant_style") or "").strip()
        weight = int(signal.get("weight") or 1)
        if weight < 1:
            weight = 1

        relationship_scores = dict(profile.get("relationship_tone_scores") or {})
        style_scores = dict(profile.get("assistant_style_scores") or {})

        if relationship_tone:
            relationship_scores[relationship_tone] = int(relationship_scores.get(relationship_tone, 0)) + weight
        if assistant_style:
            style_scores[assistant_style] = int(style_scores.get(assistant_style, 0)) + weight

        dominant_relationship = self._pick_dominant_state(relationship_scores, fallback="neutral")
        dominant_style = self._pick_dominant_state(style_scores, fallback="balanced")

        await self._profiles.update_one(
            {"user_id": user_id},
            {
                "$set": {
                    "relationship_tone_scores": relationship_scores,
                    "assistant_style_scores": style_scores,
                    "relationship_tone": dominant_relationship,
                    "assistant_style": dominant_style,
                    "updated_at": now,
                },
                "$setOnInsert": {"created_at": now},
            },
            upsert=True,
        )

    def _pick_dominant_state(self, scores: dict[str, Any], *, fallback: str) -> str:
        best_key = fallback
        best_score = -1
        for key, value in scores.items():
            try:
                score = int(value)
            except (TypeError, ValueError):
                continue
            if score > best_score:
                best_key = str(key)
                best_score = score
        return best_key

    def _extract_tone_signal(self, text: str) -> dict[str, Any]:
        lowered = text.casefold()
        signal: dict[str, Any] = {}

        warm_terms = ("cariño", "mi vida", "guapo", "guapa", "bonita", "te quiero", "amor")
        playful_terms = ("jaja", "jajaja", "xd", "xds", "jeje", "broma", "vacile")
        direct_terms = ("hazlo", "rápido", "rapido", "sin rodeos", "al grano")
        frustrated_terms = ("tío", "tio", "joder", "mierda", "wtf", "no va", "que pasa", "qué pasa")

        if any(term in lowered for term in warm_terms):
            signal["relationship_tone"] = "warm"
            signal["assistant_style"] = "close"
            signal["weight"] = 1
        elif any(term in lowered for term in playful_terms):
            signal["relationship_tone"] = "playful"
            signal["assistant_style"] = "casual"
            signal["weight"] = 1
        elif any(term in lowered for term in direct_terms):
            signal["relationship_tone"] = "direct"
            signal["assistant_style"] = "concise"
            signal["weight"] = 1
        elif any(term in lowered for term in frustrated_terms):
            signal["relationship_tone"] = "calm"
            signal["assistant_style"] = "calm_and_brief"
            signal["weight"] = 1

        explicit_tone = re.search(
            r"\b(h[aá]blame|resp[oó]ndeme|cont[eé]stame)\s+(m[aá]s )?(cari[nñ]oso|cercano|serio|directo|divertido|gracioso|fr[ií]o|breve)",
            text,
            re.IGNORECASE,
        )
        if explicit_tone:
            tone_word = explicit_tone.group(3).casefold()
            tone_map = {
                "cariñoso": ("warm", "close"),
                "cercano": ("warm", "close"),
                "serio": ("direct", "formal"),
                "directo": ("direct", "concise"),
                "divertido": ("playful", "casual"),
                "gracioso": ("playful", "casual"),
                "frío": ("neutral", "concise"),
                "breve": ("direct", "concise"),
            }
            mapped = tone_map.get(tone_word)
            if mapped:
                signal["relationship_tone"], signal["assistant_style"] = mapped
                signal["weight"] = 3

        return signal

    def _build_style_state(self, profile: dict[str, Any], turn_docs: list[dict[str, Any]]) -> dict[str, Any]:
        recent_user_texts = [
            str(doc.get("text", "")).strip()
            for doc in turn_docs[-6:]
            if str(doc.get("role", "")).strip().lower() == "user"
        ]
        merged_recent = " ".join(recent_user_texts).casefold()
        session_mood = "neutral"

        if any(term in merged_recent for term in ("joder", "mierda", "wtf", "no va", "que pasa", "qué pasa")):
            session_mood = "frustrated"
        elif any(term in merged_recent for term in ("jaja", "jajaja", "jeje", "xd", "xds")):
            session_mood = "playful"
        elif any(term in merged_recent for term in ("gracias", "amor", "cariño", "bonita", "guapa")):
            session_mood = "warm"
        elif any(term in merged_recent for term in ("rápido", "rapido", "hazlo", "al grano", "sin rodeos")):
            session_mood = "direct"

        relationship_tone = str(profile.get("relationship_tone") or "neutral").strip() or "neutral"
        assistant_style = str(profile.get("assistant_style") or "balanced").strip() or "balanced"

        return {
            "session_mood": session_mood,
            "relationship_tone": relationship_tone,
            "assistant_style": assistant_style,
        }

    def _render_style_lines(self, style_state: dict[str, Any]) -> list[str]:
        if not style_state:
            return []

        lines: list[str] = []
        session_mood = str(style_state.get("session_mood") or "").strip()
        relationship_tone = str(style_state.get("relationship_tone") or "").strip()
        assistant_style = str(style_state.get("assistant_style") or "").strip()

        if session_mood:
            lines.append(f"Current user mood looks {session_mood}.")
        if relationship_tone:
            lines.append(f"Preferred relationship tone is {relationship_tone}.")
        if assistant_style:
            lines.append(f"Assistant should answer in a {assistant_style} style.")

        return lines

    def _extract_implicit_memories(self, text: str) -> list[str]:
        if "?" in text:
            return []

        memories: list[str] = []
        patterns = [
            r"^(me gusta[n]?\s+.+)$",
            r"^(no me gusta[n]?\s+.+)$",
            r"^(prefiero\s+.+)$",
            r"^(mi color favorito es\s+.+)$",
            r"^(mi comida favorita es\s+.+)$",
            r"^(mi juego favorito es\s+.+)$",
            r"^(mi cumplea(?:n|ñ)os es\s+.+)$",
            r"^(mi pareja se llama\s+.+)$",
            r"^(mi novia se llama\s+.+)$",
            r"^(mi mujer se llama\s+.+)$",
            r"^(mi esposa se llama\s+.+)$",
            r"^(mi esposo se llama\s+.+)$",
            r"^(el nombre de mi mujer es\s+.+)$",
            r"^(el nombre de mi esposa es\s+.+)$",
            r"^(el nombre de mi esposo es\s+.+)$",
            r"^(mi novio se llama\s+.+)$",
            r"^(mi hija se llama\s+.+)$",
            r"^(mi hijo se llama\s+.+)$",
            r"^(mi madre se llama\s+.+)$",
            r"^(mi padre se llama\s+.+)$",
            r"^(mi hermana se llama\s+.+)$",
            r"^(mi hermano se llama\s+.+)$",
            r"^(mi perro se llama\s+.+)$",
            r"^(mi gata se llama\s+.+)$",
            r"^(mi gato se llama\s+.+)$",
            r"^(vivo en\s+.+)$",
            r"^(soy de\s+.+)$",
            r"^(trabajo en\s+.+)$",
            r"^(trabajo de\s+.+)$",
            r"^(trabajo como\s+.+)$",
            r"^(me dedico a\s+.+)$",
        ]
        for pattern in patterns:
            match = re.match(pattern, text, re.IGNORECASE)
            if match:
                memories.append(match.group(1).strip(" ."))

        normalized_text = text.lower()
        if normalized_text.startswith("tengo un ") or normalized_text.startswith("tengo una "):
            if len(text) <= 80:
                memories.append(text.strip(" ."))

        unique: list[str] = []
        seen: set[str] = set()
        for memory in memories:
            key = memory.casefold()
            if key in seen:
                continue
            seen.add(key)
            unique.append(memory)
        return unique

    def _select_relevant_memories(self, query: str, memory_docs: list[dict[str, Any]]) -> list[str]:
        if not memory_docs:
            return []

        keywords = self._tokenize(query)
        scored: list[tuple[int, int, str]] = []
        for index, doc in enumerate(memory_docs):
            text = doc["text"]
            score = 0
            text_tokens = self._tokenize(text)
            if keywords:
                overlap = len(keywords & text_tokens)
                score += overlap * 10
            score += max(0, 40 - index)
            scored.append((score, -index, text))

        top = sorted(scored, reverse=True)[:8]
        ordered = [text for _score, _idx, text in top if text]
        ordered.reverse()
        return ordered

    def _select_relevant_turns(self, query: str, history_docs: list[dict[str, Any]]) -> list[dict[str, Any]]:
        if not history_docs:
            return []

        keywords = self._tokenize(query)
        recent_texts = {doc["text"] for doc in history_docs[:10]}
        scored: list[tuple[int, int, dict[str, Any]]] = []
        for index, doc in enumerate(history_docs):
            if doc["text"] in recent_texts:
                continue
            tokens = self._tokenize(doc["text"])
            overlap = len(tokens & keywords) if keywords else 0
            if keywords and overlap == 0:
                continue
            score = overlap * 12 + max(0, 40 - index)
            scored.append((score, -index, doc))

        top_docs = [doc for _score, _idx, doc in sorted(scored, reverse=True)[:6]]
        top_docs.reverse()
        return [{"role": doc["role"], "text": doc["text"]} for doc in top_docs]

    def _select_relevant_summaries(self, query: str, summary_docs: list[dict[str, Any]]) -> list[str]:
        if not summary_docs:
            return []

        keywords = self._tokenize(query)
        scored: list[tuple[int, int, str]] = []
        for index, doc in enumerate(summary_docs):
            text = doc["summary"]
            tokens = self._tokenize(text)
            overlap = len(tokens & keywords) if keywords else 0
            score = overlap * 10 + max(0, 30 - index)
            if keywords and overlap == 0 and index > 10:
                continue
            scored.append((score, -index, text))

        selected = [text for _score, _idx, text in sorted(scored, reverse=True)[:6]]
        selected.reverse()
        return selected

    def _select_relevant_reminders(self, query: str, reminder_docs: list[dict[str, Any]]) -> list[str]:
        if not reminder_docs:
            return []

        keywords = self._tokenize(query)
        scored: list[tuple[int, int, str]] = []
        for index, doc in enumerate(reminder_docs):
            text = self._format_reminder(doc)
            overlap = len(self._tokenize(text) & keywords) if keywords else 0
            score = overlap * 15 + max(0, 30 - index)
            if keywords and overlap == 0 and not self._query_is_about_reminders(query):
                continue
            scored.append((score, -index, text))

        selected = [text for _score, _idx, text in sorted(scored, reverse=True)[:8]]
        selected.reverse()
        return selected

    def _extract_reminder(self, text: str) -> dict[str, Any] | None:
        patterns = [
            r"^(mañana|hoy|pasado mañana|esta tarde|esta noche)\s+recu[eé]rdame\s+(.+)$",
            r"^recu[eé]rdame\s+(mañana|hoy|pasado mañana|esta tarde|esta noche)\s+(.+)$",
            r"^recu[eé]rdame\s+(.+)$",
        ]
        for pattern in patterns:
            match = re.match(pattern, text, re.IGNORECASE)
            if not match:
                continue
            groups = [group.strip() for group in match.groups() if group]
            if not groups:
                continue
            if len(groups) == 1:
                due_hint = ""
                reminder_text = groups[0]
            else:
                due_hint = groups[0]
                reminder_text = groups[1]
            reminder_text = re.sub(r"^que\s+", "", reminder_text, flags=re.IGNORECASE).strip(" .")
            if not reminder_text:
                return None
            return {
                "text": reminder_text,
                "due_hint": due_hint,
                "due_date": self._resolve_due_date(due_hint),
            }
        return None

    def _extract_reminder_completion(self, text: str) -> str:
        patterns = [
            r"^ya (?:lo )?(?:hice|he hecho)\s+(.+)$",
            r"^(?:borra|quita|elimina)\s+(?:el\s+)?recordatorio(?:\s+de)?\s+(.+)$",
            r"^ya no me recuerdes\s+(.+)$",
        ]
        for pattern in patterns:
            match = re.match(pattern, text, re.IGNORECASE)
            if match:
                return match.group(1).strip(" .")
        return ""

    def _resolve_due_date(self, due_hint: str) -> str:
        if not due_hint:
            return ""
        now_local = utc_now().astimezone(self._timezone)
        hint = due_hint.lower()
        if hint == "hoy":
            target = now_local.date()
        elif hint == "mañana":
            target = now_local.date() + timedelta(days=1)
        elif hint == "pasado mañana":
            target = now_local.date() + timedelta(days=2)
        else:
            return ""
        return target.isoformat()

    def _format_reminder(self, doc: dict[str, Any]) -> str:
        text = doc.get("text", "").strip()
        due_hint = doc.get("due_hint", "").strip()
        if due_hint:
            return f"{text} ({due_hint})"
        return text

    def _query_is_about_reminders(self, query: str) -> bool:
        lowered = query.lower()
        reminder_terms = ("recordatorio", "recordatorios", "recordar", "recuérdame", "recuerdame")
        return any(term in lowered for term in reminder_terms)

    def _tokenize(self, text: str) -> set[str]:
        stopwords = {
            "que", "como", "para", "pero", "porque", "sobre", "desde", "hasta", "este",
            "esta", "estos", "estas", "una", "unos", "unas", "del", "con", "sin", "por",
            "las", "los", "mis", "tus", "sus", "soy", "eres", "fue", "muy", "mas", "más",
            "hoy", "ayer", "manana", "mañana", "hola", "vale",
        }
        tokens = re.findall(r"[a-zA-Z\u00c1-\u00ff0-9]{3,}", text.lower())
        return {token for token in tokens if token not in stopwords}
