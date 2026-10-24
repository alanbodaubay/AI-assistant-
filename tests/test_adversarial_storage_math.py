#!/usr/bin/env python3
"""
ScreenMate AI Companion: Storage & Mathematical Adversarial Stress Harness
File: tests/test_adversarial_storage_math.py

Adversarially challenges:
1. SQLite Event Ledger Immutability (UPDATE, DELETE, REPLACE, UPSERT, CTE, multi-row).
2. Anchor Invariants on Nodes & Edges (decay_rate != 0, provisional != 0, updates).
3. Foreign Key Cascading & Bi-temporal Invariants (cascade delete, set null, interval checks).
4. FTS5 Search Robustness (punctuation, unbalanced quotes, syntax errors, SQL injection, unicode, CJK, RTL).
5. Mathematical Retention & Stability Extreme Bounds (t=0, t=10^9, S->0, S->inf, NaN, inf).
"""

import math
import os
import sqlite3
import sys
import unittest
from pathlib import Path
from typing import Dict, Any, List, Tuple

PROJECT_ROOT = Path(__file__).resolve().parent.parent
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

from tests.e2e_math_test import (
    calculate_retrievability,
    calculate_stability_growth_fsrs,
    calculate_anchor_score,
    evaluate_quarantine_promotion,
    KAPPA,
    R_REF,
)


def load_production_schema() -> str:
    schema_path = PROJECT_ROOT / "data" / "soul_schema.sql"
    with open(schema_path, "r", encoding="utf-8") as f:
        return f.read()


def create_db() -> sqlite3.Connection:
    conn = sqlite3.connect(":memory:")
    conn.execute("PRAGMA foreign_keys = ON;")
    conn.row_factory = sqlite3.Row
    conn.executescript(load_production_schema())
    return conn


class TestAdversarialImmutability(unittest.TestCase):
    """Stress-test immutability of the 'events' table against various bypass attempts."""

    def setUp(self):
        self.conn = create_db()
        self.cursor = self.conn.cursor()
        # Seed an event
        self.cursor.execute(
            """
            INSERT INTO events (id, session_id, timestamp, event_type, raw_content, metadata_json, sha256_hash, is_pinned)
            VALUES (1, 's1', 1700000000000, 'USER_CHAT', 'Initial raw turn content', '{"source":"user"}', 'hash_001', 0);
            """
        )

    def tearDown(self):
        self.conn.close()

    def test_direct_update_fails(self):
        """Attacking via direct single-row UPDATE."""
        with self.assertRaises(sqlite3.DatabaseError):
            self.cursor.execute("UPDATE events SET raw_content = 'tampered' WHERE id = 1;")

    def test_direct_delete_fails(self):
        """Attacking via direct single-row DELETE."""
        with self.assertRaises(sqlite3.DatabaseError):
            self.cursor.execute("DELETE FROM events WHERE id = 1;")

    def test_insert_or_replace_pk_conflict_fails(self):
        """
        Attacking via INSERT OR REPLACE with colliding primary key.
        In SQLite, REPLACE executes a DELETE on the conflicting row, which MUST fire BEFORE DELETE trigger.
        """
        with self.assertRaises(sqlite3.DatabaseError):
            self.cursor.execute(
                """
                INSERT OR REPLACE INTO events (id, session_id, timestamp, event_type, raw_content, metadata_json, sha256_hash, is_pinned)
                VALUES (1, 's1', 1700000001000, 'USER_CHAT', 'tampered replacement', '{}', 'hash_002', 0);
                """
            )

    def test_insert_or_replace_unique_hash_conflict_fails(self):
        """Attacking via INSERT OR REPLACE with colliding sha256_hash."""
        with self.assertRaises(sqlite3.DatabaseError):
            self.cursor.execute(
                """
                INSERT OR REPLACE INTO events (id, session_id, timestamp, event_type, raw_content, metadata_json, sha256_hash, is_pinned)
                VALUES (2, 's1', 1700000001000, 'USER_CHAT', 'tampered replacement', '{}', 'hash_001', 0);
                """
            )

    def test_upsert_do_update_fails(self):
        """Attacking via INSERT ... ON CONFLICT(id) DO UPDATE."""
        with self.assertRaises(sqlite3.DatabaseError):
            self.cursor.execute(
                """
                INSERT INTO events (id, session_id, timestamp, event_type, raw_content, metadata_json, sha256_hash, is_pinned)
                VALUES (1, 's1', 1700000001000, 'USER_CHAT', 'tampered upsert', '{}', 'hash_003', 0)
                ON CONFLICT(id) DO UPDATE SET raw_content = excluded.raw_content;
                """
            )

    def test_upsert_do_update_unique_hash_fails(self):
        """Attacking via INSERT ... ON CONFLICT(sha256_hash) DO UPDATE."""
        with self.assertRaises(sqlite3.DatabaseError):
            self.cursor.execute(
                """
                INSERT INTO events (id, session_id, timestamp, event_type, raw_content, metadata_json, sha256_hash, is_pinned)
                VALUES (2, 's1', 1700000001000, 'USER_CHAT', 'tampered upsert', '{}', 'hash_001', 0)
                ON CONFLICT(sha256_hash) DO UPDATE SET raw_content = excluded.raw_content;
                """
            )

    def test_cte_update_fails(self):
        """Attacking via Common Table Expression (CTE) UPDATE."""
        with self.assertRaises(sqlite3.DatabaseError):
            self.cursor.execute(
                """
                WITH target AS (SELECT id FROM events WHERE id = 1)
                UPDATE events SET raw_content = 'cte_tampered' WHERE id IN (SELECT id FROM target);
                """
            )

    def test_cte_delete_fails(self):
        """Attacking via CTE DELETE."""
        with self.assertRaises(sqlite3.DatabaseError):
            self.cursor.execute(
                """
                WITH target AS (SELECT id FROM events WHERE id = 1)
                DELETE FROM events WHERE id IN (SELECT id FROM target);
                """
            )

    def test_truncate_simulation_fails(self):
        """Attacking via unconditional table wipe: DELETE FROM events."""
        with self.assertRaises(sqlite3.DatabaseError):
            self.cursor.execute("DELETE FROM events;")

    def test_multi_row_update_fails(self):
        """Inserting multiple events and attempting batch update."""
        self.cursor.execute(
            """
            INSERT INTO events (id, session_id, timestamp, event_type, raw_content, metadata_json, sha256_hash, is_pinned)
            VALUES (2, 's1', 1700000002000, 'COMPANION_CHAT', 'Turn 2', '{}', 'hash_002', 0);
            """
        )
        with self.assertRaises(sqlite3.DatabaseError):
            self.cursor.execute("UPDATE events SET is_pinned = 1 WHERE session_id = 's1';")


class TestAdversarialAnchorInvariants(unittest.TestCase):
    """Stress-test anchor invariants (is_anchor=1 => decay_rate=0.0 AND provisional=0)."""

    def setUp(self):
        self.conn = create_db()
        self.cursor = self.conn.cursor()

    def tearDown(self):
        self.conn.close()

    def test_insert_node_anchor_violations(self):
        """Reject anchor node with non-zero decay or provisional flag."""
        # Non-zero decay
        with self.assertRaises(sqlite3.DatabaseError):
            self.cursor.execute(
                """
                INSERT INTO nodes (name, type, is_anchor, decay_rate, provisional)
                VALUES ('Allergy', 'CORE_IDENTITY', 1, 0.01, 0);
                """
            )
        # Provisional = 1
        with self.assertRaises(sqlite3.DatabaseError):
            self.cursor.execute(
                """
                INSERT INTO nodes (name, type, is_anchor, decay_rate, provisional)
                VALUES ('Allergy', 'CORE_IDENTITY', 1, 0.0, 1);
                """
            )
        # Negative decay
        with self.assertRaises(sqlite3.DatabaseError):
            self.cursor.execute(
                """
                INSERT INTO nodes (name, type, is_anchor, decay_rate, provisional)
                VALUES ('Allergy', 'CORE_IDENTITY', 1, -0.05, 0);
                """
            )

    def test_insert_edge_anchor_violations(self):
        """Reject anchor edge with non-zero decay or provisional flag."""
        # Seed two valid nodes
        self.cursor.execute("INSERT INTO nodes (name, type, is_anchor, decay_rate, provisional) VALUES ('A', 'USER', 1, 0.0, 0);")
        n1 = self.cursor.lastrowid
        self.cursor.execute("INSERT INTO nodes (name, type, is_anchor, decay_rate, provisional) VALUES ('B', 'CONCEPT', 1, 0.0, 0);")
        n2 = self.cursor.lastrowid

        # Edge anchor with decay > 0
        with self.assertRaises(sqlite3.DatabaseError):
            self.cursor.execute(
                """
                INSERT INTO edges (source_id, target_id, relation, is_anchor, decay_rate, provisional)
                VALUES (?, ?, 'KNOWS', 1, 0.05, 0);
                """,
                (n1, n2),
            )

        # Edge anchor with provisional = 1
        with self.assertRaises(sqlite3.DatabaseError):
            self.cursor.execute(
                """
                INSERT INTO edges (source_id, target_id, relation, is_anchor, decay_rate, provisional)
                VALUES (?, ?, 'KNOWS', 1, 0.0, 1);
                """,
                (n1, n2),
            )

    def test_update_ephemeral_to_anchor_uncleanly_fails(self):
        """Updating an ephemeral node/edge to is_anchor=1 without clearing decay/provisional must fail."""
        self.cursor.execute(
            """
            INSERT INTO nodes (name, type, is_anchor, decay_rate, provisional)
            VALUES ('Casual Topic', 'CONCEPT', 0, 0.05, 1);
            """
        )
        node_id = self.cursor.lastrowid

        # Update only is_anchor = 1 (decay_rate and provisional remain 0.05 and 1)
        with self.assertRaises(sqlite3.DatabaseError):
            self.cursor.execute("UPDATE nodes SET is_anchor = 1 WHERE id = ?;", (node_id,))

        # Update is_anchor = 1, decay_rate = 0.0, but leave provisional = 1
        with self.assertRaises(sqlite3.DatabaseError):
            self.cursor.execute("UPDATE nodes SET is_anchor = 1, decay_rate = 0.0 WHERE id = ?;", (node_id,))

        # Clean update to anchor succeeds
        self.cursor.execute("UPDATE nodes SET is_anchor = 1, decay_rate = 0.0, provisional = 0 WHERE id = ?;", (node_id,))
        self.cursor.execute("SELECT is_anchor, decay_rate, provisional FROM nodes WHERE id = ?;", (node_id,))
        row = self.cursor.fetchone()
        self.assertEqual(row["is_anchor"], 1)
        self.assertEqual(row["decay_rate"], 0.0)
        self.assertEqual(row["provisional"], 0)

    def test_corrupting_existing_anchor_fails(self):
        """Updating an existing anchor to have non-zero decay or provisional=1 must fail."""
        self.cursor.execute(
            """
            INSERT INTO nodes (name, type, is_anchor, decay_rate, provisional)
            VALUES ('Alan Birthday', 'CORE_IDENTITY', 1, 0.0, 0);
            """
        )
        node_id = self.cursor.lastrowid

        # Try setting decay_rate > 0
        with self.assertRaises(sqlite3.DatabaseError):
            self.cursor.execute("UPDATE nodes SET decay_rate = 0.05 WHERE id = ?;", (node_id,))

        # Try setting provisional = 1
        with self.assertRaises(sqlite3.DatabaseError):
            self.cursor.execute("UPDATE nodes SET provisional = 1 WHERE id = ?;", (node_id,))


class TestAdversarialForeignKeysAndBiTemporal(unittest.TestCase):
    """Stress-test foreign key cascading and bi-temporal logic."""

    def setUp(self):
        self.conn = create_db()
        self.cursor = self.conn.cursor()

    def tearDown(self):
        self.conn.close()

    def test_cascade_delete_on_nodes(self):
        """Deleting a node cascades to all associated edges (both source and target)."""
        self.cursor.execute("INSERT INTO nodes (name, type, is_anchor, decay_rate, provisional) VALUES ('NodeA', 'USER', 1, 0.0, 0);")
        nA = self.cursor.lastrowid
        self.cursor.execute("INSERT INTO nodes (name, type, is_anchor, decay_rate, provisional) VALUES ('NodeB', 'CONCEPT', 1, 0.0, 0);")
        nB = self.cursor.lastrowid
        self.cursor.execute("INSERT INTO nodes (name, type, is_anchor, decay_rate, provisional) VALUES ('NodeC', 'CONCEPT', 1, 0.0, 0);")
        nC = self.cursor.lastrowid

        # Create edges: A -> B, C -> A
        self.cursor.execute("INSERT INTO edges (source_id, target_id, relation, is_anchor, decay_rate, provisional) VALUES (?, ?, 'REL1', 1, 0.0, 0);", (nA, nB))
        e1 = self.cursor.lastrowid
        self.cursor.execute("INSERT INTO edges (source_id, target_id, relation, is_anchor, decay_rate, provisional) VALUES (?, ?, 'REL2', 1, 0.0, 0);", (nC, nA))
        e2 = self.cursor.lastrowid

        # Delete NodeA
        self.cursor.execute("DELETE FROM nodes WHERE id = ?;", (nA,))

        # Both edges must be deleted
        self.cursor.execute("SELECT COUNT(*) AS c FROM edges WHERE id IN (?, ?);", (e1, e2))
        self.assertEqual(self.cursor.fetchone()["c"], 0)

    def test_source_event_reference_set_null_behavior(self):
        """
        Nodes and edges have source_event_id REFERENCES events(id) ON DELETE SET NULL.
        Since events cannot be deleted, verify referencing valid events works,
        and referencing non-existent events violates FK constraint.
        """
        with self.assertRaises(sqlite3.IntegrityError):
            self.cursor.execute(
                """
                INSERT INTO nodes (name, type, source_event_id)
                VALUES ('Ghost Source', 'CONCEPT', 99999);
                """
            )

    def test_bitemporal_supersession_flow(self):
        """Simulate realistic anchor fact supersession and verify active view."""
        self.cursor.execute("INSERT INTO nodes (name, type, is_anchor, decay_rate, provisional) VALUES ('Alan', 'USER', 1, 0.0, 0);")
        u_id = self.cursor.lastrowid
        self.cursor.execute("INSERT INTO nodes (name, type, is_anchor, decay_rate, provisional) VALUES ('Seattle', 'CONCEPT', 1, 0.0, 0);")
        sea_id = self.cursor.lastrowid
        self.cursor.execute("INSERT INTO nodes (name, type, is_anchor, decay_rate, provisional) VALUES ('Austin', 'CONCEPT', 1, 0.0, 0);")
        aus_id = self.cursor.lastrowid

        t0 = 1700000000000
        t1 = 1700050000000

        # Step 1: Alan lived in Seattle
        self.cursor.execute(
            """
            INSERT INTO edges (source_id, target_id, relation, is_anchor, decay_rate, provisional, valid_from, valid_until, is_active)
            VALUES (?, ?, 'LIVES_IN', 1, 0.0, 0, ?, NULL, 1);
            """,
            (u_id, sea_id, t0),
        )
        edge_old_id = self.cursor.lastrowid

        # Query v_active_anchors
        self.cursor.execute("SELECT target_name FROM v_active_anchors WHERE relation = 'LIVES_IN';")
        rows = self.cursor.fetchall()
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]["target_name"], "Seattle")

        # Step 2: Alan moved to Austin. Close old edge, insert new edge atomically.
        self.cursor.execute("BEGIN TRANSACTION;")
        self.cursor.execute("UPDATE edges SET valid_until = ?, is_active = 0 WHERE id = ?;", (t1, edge_old_id))
        self.cursor.execute(
            """
            INSERT INTO edges (source_id, target_id, relation, is_anchor, decay_rate, provisional, valid_from, valid_until, is_active)
            VALUES (?, ?, 'LIVES_IN', 1, 0.0, 0, ?, NULL, 1);
            """,
            (u_id, aus_id, t1),
        )
        self.cursor.execute("COMMIT;")

        # Query v_active_anchors: only Austin should be returned
        self.cursor.execute("SELECT target_name FROM v_active_anchors WHERE relation = 'LIVES_IN';")
        rows = self.cursor.fetchall()
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]["target_name"], "Austin")

        # Inactive/historical record is retained in edges table
        self.cursor.execute("SELECT target_id, valid_until, is_active FROM edges WHERE id = ?;", (edge_old_id,))
        hist = self.cursor.fetchone()
        self.assertEqual(hist["target_id"], sea_id)
        self.assertEqual(hist["valid_until"], t1)
        self.assertEqual(hist["is_active"], 0)


class TestAdversarialFTS5(unittest.TestCase):
    """Stress-test FTS5 search behavior against malicious inputs, unicode, and special syntax."""

    def setUp(self):
        self.conn = create_db()
        self.cursor = self.conn.cursor()

    def tearDown(self):
        self.conn.close()

    def test_fts5_indexing_special_characters_and_sql_injection(self):
        """Verify inserting SQL injection payloads, script tags, quotes into events and searching."""
        payloads = [
            "'; DROP TABLE events; --",
            "' OR '1'='1",
            "\"Unbalanced quote in user turn",
            "<script>alert('xss');</script>",
            "Symbols: !@#$%^&*()_+=-`~[]{};':\",./<>?",
            "Backslash \\ and newline \n\r\t and tab",
        ]
        for i, p in enumerate(payloads):
            self.cursor.execute(
                """
                INSERT INTO events (id, session_id, timestamp, event_type, raw_content, sha256_hash)
                VALUES (?, 's_sec', 1700000000000 + ?, 'USER_CHAT', ?, ?);
                """,
                (100 + i, i, p, f"hash_sec_{i}"),
            )

        # Confirm all 6 events inserted and indexed
        self.cursor.execute("SELECT count(*) AS cnt FROM events_fts;")
        self.assertEqual(self.cursor.fetchone()["cnt"], len(payloads))

        # Querying literal terms using FTS5 phrase quotes: '"DROP"'
        self.cursor.execute("SELECT rowid, raw_content FROM events_fts WHERE events_fts MATCH '\"DROP\"';")
        row = self.cursor.fetchone()
        self.assertIsNotNone(row)
        self.assertIn("DROP TABLE", row["raw_content"])

    def test_fts5_unicode_multilingual_and_emojis(self):
        """Verify indexing and retrieving CJK, Arabic, German umlauts, and emojis."""
        test_data = [
            (201, "CJK_Turn", "我的名字是艾伦，喜欢使用 C++20 编写智能伴侣。"),
            (202, "Arabic_Turn", "مرحبا يا رفيقي الذكي، أتمنى لك يوما سعيدا."),
            (203, "German_Turn", "Möchtest du ein schönes Frühstück mit Käse und Weißbrot?"),
            (204, "Emoji_Turn", "Family picnic with pets 👨‍👩‍👧‍👦 🐕 🍕 in the park!"),
            (205, "Math_Turn", "Retrievability formula: R(t) = exp(-κ · t / S) ∀ t ≥ 0"),
        ]

        for eid, name, content in test_data:
            self.cursor.execute(
                """
                INSERT INTO events (id, session_id, timestamp, event_type, raw_content, sha256_hash)
                VALUES (?, 's_uni', 1700000000000 + ?, 'USER_CHAT', ?, ?);
                """,
                (eid, eid, content, f"hash_uni_{eid}"),
            )

        # Search for CJK term "智能伴侣"
        self.cursor.execute("SELECT rowid, raw_content FROM events_fts WHERE events_fts MATCH '\"智能伴侣\"';")
        r_cjk = self.cursor.fetchone()
        self.assertIsNotNone(r_cjk)
        self.assertEqual(r_cjk["rowid"], 201)

        # Search for German term "Frühstück"
        self.cursor.execute("SELECT rowid, raw_content FROM events_fts WHERE events_fts MATCH 'Frühstück';")
        r_ger = self.cursor.fetchone()
        self.assertIsNotNone(r_ger)
        self.assertEqual(r_ger["rowid"], 203)

        # Search for Math symbol or variable
        self.cursor.execute("SELECT rowid, raw_content FROM events_fts WHERE events_fts MATCH 'Retrievability';")
        r_math = self.cursor.fetchone()
        self.assertIsNotNone(r_math)
        self.assertEqual(r_math["rowid"], 205)

    def test_fts5_query_sanitization_defense(self):
        """
        FTS5 query parser throws syntax errors on bare punctuation like `AND`, `OR`, `*`, `"`
        if not sanitized. Verify defensive escaping pattern.
        """
        def sanitize_fts_query(user_query: str) -> str:
            # Tokenize or wrap terms in double quotes to prevent FTS5 syntax crashes
            cleaned = user_query.replace('"', '""')
            return f'"{cleaned}"'

        adversarial_queries = [
            'AND',
            'OR',
            'NOT',
            '*',
            '"""',
            'NEAR()',
            'raw_content:',
            '{invalid query syntax}',
        ]

        for q in adversarial_queries:
            safe_q = sanitize_fts_query(q)
            try:
                self.cursor.execute("SELECT rowid FROM events_fts WHERE events_fts MATCH ?;", (safe_q,))
                _ = self.cursor.fetchall()
            except sqlite3.OperationalError as e:
                self.fail(f"Sanitized query '{safe_q}' caused FTS5 syntax error: {e}")


class TestAdversarialMathematicalFormulations(unittest.TestCase):
    """Stress-test mathematical retention, stability growth, and anchor scoring across extreme domains."""

    def test_retrievability_extreme_ranges(self):
        """Evaluate R(t, S) across extreme values of t and S."""
        # 1. t = 0 -> R(0) == 1.0 identically regardless of S
        for s in [1e-6, 0.01, 1.0, 100.0, 1e9]:
            r0 = calculate_retrievability(0.0, stability=s)
            self.assertAlmostEqual(r0, 1.0, places=9)

        # 2. t -> inf (e.g. 10^9 days) -> R(t) -> 0.0 smoothly without NaN or crash
        r_inf = calculate_retrievability(1e9, stability=10.0)
        self.assertEqual(r_inf, 0.0)

        # 3. S -> inf (e.g. 10^12) -> R(t) ~ 1.0 for moderate t
        r_large_s = calculate_retrievability(365.0, stability=1e12)
        self.assertAlmostEqual(r_large_s, 1.0, places=6)

        # 4. S -> 0 (e.g. 1e-12) -> rapid decay to 0.0
        r_tiny_s = calculate_retrievability(0.01, stability=1e-12)
        self.assertEqual(r_tiny_s, 0.0)

        # 5. Invalid inputs: negative t or stability <= 0 must raise ValueError
        with self.assertRaises(ValueError):
            calculate_retrievability(-1.0, stability=1.0)
        with self.assertRaises(ValueError):
            calculate_retrievability(1.0, stability=0.0)
        with self.assertRaises(ValueError):
            calculate_retrievability(1.0, stability=-5.0)

        # 6. Anchors are completely immune to any positive t
        for t in [0.0, 1.0, 365.0, 1e9]:
            r_anchor = calculate_retrievability(t, stability=1.0, is_anchor=True)
            self.assertEqual(r_anchor, 1.0)

    def test_stability_growth_extreme_ranges(self):
        """Evaluate FSRS stability growth under extreme parameters."""
        s0 = 10.0

        # Boundary R in [0.0, 1.0]
        s_r0 = calculate_stability_growth_fsrs(s0, r_retrievability=0.0, delta_t_days=1.0)
        s_r1 = calculate_stability_growth_fsrs(s0, r_retrievability=1.0, delta_t_days=1.0)
        self.assertGreater(s_r0, s_r1, "R=0 (hard recall) must yield strictly higher stability than R=1 (easy recall)")

        # Clamping check: out-of-bounds R (< 0 or > 1) should be safely clamped
        s_neg_r = calculate_stability_growth_fsrs(s0, r_retrievability=-0.5, delta_t_days=1.0)
        self.assertEqual(s_neg_r, s_r0, "Negative R must clamp to 0.0")

        s_high_r = calculate_stability_growth_fsrs(s0, r_retrievability=1.5, delta_t_days=1.0)
        self.assertEqual(s_high_r, s_r1, "R > 1.0 must clamp to 1.0")

        # Spacing interval cap: delta_t > 365 days is capped at 365
        s_365 = calculate_stability_growth_fsrs(s0, r_retrievability=0.5, delta_t_days=365.0)
        s_1000 = calculate_stability_growth_fsrs(s0, r_retrievability=0.5, delta_t_days=1000.0)
        self.assertEqual(s_365, s_1000, "delta_t must cap at 365.0 days")

        # Contradiction penalty boundary: penalty = 1.0 => zero growth bonus
        s_penalized_full = calculate_stability_growth_fsrs(s0, r_retrievability=0.5, delta_t_days=10.0, penalty_contradiction=1.0)
        self.assertEqual(s_penalized_full, s0, "100% penalty must result in exactly 1.0 multiplier (no growth)")

        # Invalid current stability <= 0
        with self.assertRaises(ValueError):
            calculate_stability_growth_fsrs(0.0, r_retrievability=0.5, delta_t_days=1.0)
        with self.assertRaises(ValueError):
            calculate_stability_growth_fsrs(-5.0, r_retrievability=0.5, delta_t_days=1.0)

    def test_anchor_scoring_extreme_ranges(self):
        """Evaluate anchor scoring under extreme inputs."""
        # 1. Deterministic explicit bypass always yields 1.0
        for cat in ["CORE_IDENTITY", "TRANSIENT_STATE", "UNKNOWN_CATEGORY"]:
            score = calculate_anchor_score(c_explicit=1, emotional_valence=0.0, semantic_centrality=0.0, category=cat)
            self.assertEqual(score, 1.0)

        # 2. Extreme valence: negative or out-of-range magnitude
        s_neg = calculate_anchor_score(c_explicit=0, emotional_valence=-1.0, semantic_centrality=0.5, category="CORE_PREFERENCE")
        s_pos = calculate_anchor_score(c_explicit=0, emotional_valence=1.0, semantic_centrality=0.5, category="CORE_PREFERENCE")
        self.assertAlmostEqual(s_neg, s_pos, places=9, msg="Emotional valence magnitude must be symmetric around 0")

        # Out-of-bounds valence clamps to [0, 1]
        s_over = calculate_anchor_score(c_explicit=0, emotional_valence=50.0, semantic_centrality=0.5, category="CORE_PREFERENCE")
        self.assertEqual(s_over, s_pos)

        # 3. Score always in [0.0, 1.0]
        s_min = calculate_anchor_score(c_explicit=0, emotional_valence=0.0, semantic_centrality=0.0, category="TRANSIENT_STATE")
        self.assertGreater(s_min, 0.0)
        self.assertLess(s_min, 1.0)

    def test_quarantine_promotion_boundary_values(self):
        """Evaluate exact boundary values for 2-day quarantine promotion."""
        # Baseline passing: 2 days, 36.0h, 0.70 confidence
        self.assertTrue(evaluate_quarantine_promotion(2, 36.0, 0.70))

        # Fails if elapsed_hours < 36.0 (e.g. 35.999h)
        self.assertFalse(evaluate_quarantine_promotion(2, 35.999, 0.95))

        # Fails if distinct_calendar_days < 2 (e.g. 1 day, even if elapsed is 40h)
        self.assertFalse(evaluate_quarantine_promotion(1, 40.0, 0.95))

        # Fails if confidence < 0.70 (e.g. 0.699, even if days=3 and elapsed=72h)
        self.assertFalse(evaluate_quarantine_promotion(3, 72.0, 0.699))

        # Explicit anchor overrides all conditions
        self.assertTrue(evaluate_quarantine_promotion(1, 0.1, 0.1, is_explicit_anchor=True))


if __name__ == "__main__":
    unittest.main()
