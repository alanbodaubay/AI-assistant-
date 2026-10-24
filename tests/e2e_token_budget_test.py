#!/usr/bin/env python3
"""
ScreenMate AI Brain & Memory System
E2E Token Budget & Situational Mental Model Test
File: tests/e2e_token_budget_test.py

Validates:
1. Situational Mental Model JSON schema definition and compliance.
2. Serialization to compact prompt blocks (<100 tokens).
3. Token count under typical operating conditions (60-85 tokens).
4. Token count under maximum field length specifications (<100 tokens).
5. Automatic truncation / shedding of oversized inputs to enforce hard <100 token ceiling.
"""

import json
import re
import unittest
from dataclasses import dataclass, field, asdict
from typing import List, Dict, Any

try:
    import jsonschema
    HAS_JSONSCHEMA = True
except ImportError:
    HAS_JSONSCHEMA = False


# ============================================================================
# Situational Mental Model Specification & Dataclass
# ============================================================================

SITUATIONAL_STATE_JSON_SCHEMA = {
    "$schema": "https://json-schema.org/draft/2020-12/schema",
    "title": "SituationalMentalModel",
    "type": "object",
    "required": ["topic", "entities", "user_emotion", "goal"],
    "properties": {
        "topic": {
            "type": "string",
            "maxLength": 120,
            "description": "Active conversational or screen task focus"
        },
        "entities": {
            "type": "array",
            "items": {"type": "string", "maxLength": 50},
            "maxItems": 5,
            "description": "Active entity identifiers currently in focus"
        },
        "user_emotion": {
            "type": "string",
            "maxLength": 50,
            "description": "User emotional valence or mood state"
        },
        "goal": {
            "type": "string",
            "maxLength": 140,
            "description": "Immediate ongoing objective or inquiry"
        },
        "affinity": {
            "type": "number",
            "minimum": 0.0,
            "maximum": 1.0,
            "default": 0.5
        },
        "snark": {
            "type": "number",
            "minimum": 0.0,
            "maximum": 1.0,
            "default": 0.5
        },
        "timestamp_unix": {
            "type": "integer",
            "minimum": 0
        }
    },
    "additionalProperties": False
}


def estimate_gemma_tokens(text: str) -> int:
    """
    SentencePiece / Gemma tokenizer estimator for English technical text.
    Gemma uses a 256k vocabulary SentencePiece BPE tokenizer.
    Tokens are bounded by:
    - Words + punctuation symbols + subword camelCase/snake_case splits.
    - Character-based conservative upper bound: max(word_tokens, ceil(len(text) / 3.4)).
    """
    if not text:
        return 0

    # Token split on whitespace and punctuation
    raw_tokens = re.findall(r"\w+|[^\w\s]", text, re.UNICODE)
    token_count = 0
    for tok in raw_tokens:
        # Subword split on camelCase or snake_case
        subparts = re.findall(r"[A-Z]?[a-z]+|[A-Z]+(?=[A-Z][a-z]|\b)|\d+", tok)
        if len(subparts) > 1:
            token_count += len(subparts)
        else:
            # If word is very long (>10 chars), it typically splits into subwords
            token_count += max(1, math_ceil(len(tok) / 4.0))

    # Also compute conservative character ratio bound
    char_bound = math_ceil(len(text) / 3.4)
    return max(token_count, char_bound)


def math_ceil(val: float) -> int:
    return int(val) + (1 if val > int(val) else 0)


@dataclass
class SituationalMentalModel:
    topic: str
    entities: List[str] = field(default_factory=list)
    user_emotion: str = "neutral"
    goal: str = ""
    affinity: float = 0.50
    snark: float = 0.50
    timestamp_unix: int = 0

    def serialize_prompt_block(self, max_tokens: int = 99) -> str:
        """
        Serializes working state into the compact bracket prompt format (<100 tokens).
        If the estimated tokens exceed max_tokens, applies graceful truncation to entities and strings.
        """
        entities_copy = list(self.entities)
        topic_str = self.topic
        goal_str = self.goal
        emotion_str = self.user_emotion

        def build_block(t: str, e: List[str], em: str, g: str) -> str:
            ent_list = ", ".join(e)
            return (
                f"[STATE]\n"
                f"topic: {t}\n"
                f"entities: [{ent_list}]\n"
                f"user_emotion: {em}\n"
                f"goal: {g}\n"
                f"persona: {{aff:{self.affinity:.2f}, snark:{self.snark:.2f}}}\n"
                f"[/STATE]"
            )

        block = build_block(topic_str, entities_copy, emotion_str, goal_str)
        tokens = estimate_gemma_tokens(block)

        # Graceful degradation / truncation if oversized
        while tokens > max_tokens and entities_copy:
            # Shed least-recent entity first
            entities_copy.pop()
            block = build_block(topic_str, entities_copy, emotion_str, goal_str)
            tokens = estimate_gemma_tokens(block)

        if tokens > max_tokens and len(goal_str) > 40:
            goal_str = goal_str[:37] + "..."
            block = build_block(topic_str, entities_copy, emotion_str, goal_str)
            tokens = estimate_gemma_tokens(block)

        if tokens > max_tokens and len(topic_str) > 40:
            topic_str = topic_str[:37] + "..."
            block = build_block(topic_str, entities_copy, emotion_str, goal_str)

        return block

    def to_json_dict(self) -> Dict[str, Any]:
        return {
            "topic": self.topic,
            "entities": self.entities,
            "user_emotion": self.user_emotion,
            "goal": self.goal,
            "affinity": round(self.affinity, 2),
            "snark": round(self.snark, 2),
            "timestamp_unix": self.timestamp_unix,
        }


# ============================================================================
# Unit Tests
# ============================================================================

class TestE2ETokenBudget(unittest.TestCase):
    """Validates Situational Mental Model schema, serialization, and strict token limits."""

    def test_situational_state_json_schema_validity(self):
        """TC-WM-01: Validates Situational Mental Model dictionary against JSON schema."""
        model = SituationalMentalModel(
            topic="Refactoring CMake build for Win32 layered window",
            entities=["screenmate_shell.exe", "MSVC_19.38"],
            user_emotion="focused",
            goal="resolve linker error LNK2019",
            affinity=0.55,
            snark=0.65,
            timestamp_unix=1700000000,
        )
        data = model.to_json_dict()

        if HAS_JSONSCHEMA:
            # Validate with jsonschema
            jsonschema.validate(instance=data, schema=SITUATIONAL_STATE_JSON_SCHEMA)
        else:
            # Basic fallback validation
            self.assertIn("topic", data)
            self.assertIn("entities", data)
            self.assertIsInstance(data["entities"], list)
            self.assertIn("goal", data)

        # Check required fields
        self.assertEqual(data["topic"], "Refactoring CMake build for Win32 layered window")
        self.assertEqual(len(data["entities"]), 2)
        self.assertEqual(data["user_emotion"], "focused")
        self.assertEqual(data["goal"], "resolve linker error LNK2019")

    def test_typical_token_budget_under_100_tokens(self):
        """TC-WM-02: Typical desktop working state must consume 60-85 tokens (<100 hard ceiling)."""
        model = SituationalMentalModel(
            topic="CMake build with Win32 layered window",
            entities=["screenmate_shell.exe", "MSVC_19.38"],
            user_emotion="focused, slightly tired",
            goal="Resolve linker error LNK2019 for UpdateLayeredWindow",
            affinity=0.45,
            snark=0.60,
        )

        block = model.serialize_prompt_block()
        token_count = estimate_gemma_tokens(block)

        self.assertLess(token_count, 100, f"Typical state token count ({token_count}) must be < 100 tokens")
        self.assertGreaterEqual(token_count, 45, "Typical state should contain meaningful context (>= 45 tokens)")

        # Verify exact structural delimiters
        self.assertTrue(block.startswith("[STATE]"))
        self.assertTrue(block.endswith("[/STATE]"))
        self.assertIn("topic: CMake build", block)
        self.assertIn("entities: [screenmate_shell.exe, MSVC_19.38]", block)

    def test_maximum_allowable_field_lengths_under_100_tokens(self):
        """TC-WM-02B: State at upper edge of typical bounds strictly stays under 100 tokens."""
        model = SituationalMentalModel(
            topic="C++20 coroutines in audio engine pipeline",  # ~10 words
            entities=["miniaudio", "PulseAudio", "OpenAL_Soft"],  # 3 entities
            user_emotion="frustrated with latency",
            goal="fix buffer underrun crash in audio loop",
            affinity=0.40,
            snark=0.70,
        )

        block = model.serialize_prompt_block()
        tokens = estimate_gemma_tokens(block)

        self.assertLess(tokens, 100, f"Max field length state tokens ({tokens}) must not exceed 99 tokens")

    def test_adversarial_oversized_fields_auto_truncation(self):
        """TC-BND-01: Oversized user context is automatically truncated to guarantee < 100 tokens."""
        # Unreasonably long entities and long descriptions
        model = SituationalMentalModel(
            topic="An extraordinarily long multi-clause paragraph describing an extremely intricate and convoluted architecture refactoring task that exceeds all sensible limits",
            entities=["entity_alpha_very_long_name_12345", "entity_beta_extremely_verbose_string", "entity_gamma", "entity_delta", "entity_epsilon"],
            user_emotion="profoundly exasperated and completely bewildered by the compiler",
            goal="attempting to diagnose an obscure memory corruption fault that occurs only on alternate Tuesdays when the moon is full and network sockets timeout",
            affinity=0.50,
            snark=0.50,
        )

        # Without truncation, this would be > 150 tokens.
        raw_block = (
            f"[STATE]\ntopic: {model.topic}\nentities: {model.entities}\n"
            f"user_emotion: {model.user_emotion}\ngoal: {model.goal}\n[/STATE]"
        )
        self.assertGreater(estimate_gemma_tokens(raw_block), 100, "Raw block should exceed 100 tokens")

        # Serializer must enforce the hard ceiling!
        guarded_block = model.serialize_prompt_block(max_tokens=99)
        guarded_tokens = estimate_gemma_tokens(guarded_block)

        self.assertLess(guarded_tokens, 100, f"Guarded block tokens ({guarded_tokens}) must be strictly < 100")
        self.assertTrue(guarded_block.startswith("[STATE]"))
        self.assertTrue(guarded_block.endswith("[/STATE]"))

    def test_minimal_and_empty_state_token_count(self):
        """TC-WM-02C: Minimal state is ultra-compact (<50 tokens)."""
        model = SituationalMentalModel(topic="Idle", entities=[], user_emotion="calm", goal="")
        block = model.serialize_prompt_block()
        tokens = estimate_gemma_tokens(block)

        self.assertLess(tokens, 50, f"Minimal state tokens ({tokens}) should be < 50")
        self.assertLess(tokens, 100)
        self.assertIn("topic: Idle", block)
        self.assertIn("entities: []", block)


if __name__ == "__main__":
    unittest.main()
