from __future__ import annotations

from openai import AsyncOpenAI

from ..config import settings
from ..models import AssistantReply


class LlmService:
    def __init__(self) -> None:
        self._client = AsyncOpenAI(api_key=settings.openai_api_key)

    async def answer(
        self,
        *,
        user_text: str,
        knowledge_context: str,
        memory_context: str,
        system_prompt: str,
    ) -> AssistantReply:
        prompt = system_prompt
        if knowledge_context:
            prompt += "\n\n" + knowledge_context
        if memory_context:
            prompt += "\n\n" + memory_context

        completion = await self._client.chat.completions.create(
            model=settings.openai_model,
            messages=[
                {"role": "system", "content": prompt},
                {"role": "user", "content": user_text},
            ],
            temperature=0.4,
        )
        text = (completion.choices[0].message.content or "").strip()
        return AssistantReply(user_text=user_text, assistant_text=text)
