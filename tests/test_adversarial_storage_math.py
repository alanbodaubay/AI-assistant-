#!/usr/bin/env python3
"""
ScreenMate AI Companion: Empirical Challenger Stress Test Harness
File: tests/test_adversarial_storage_math.py

Author: Storage & Mathematical Challenger
Mission: Empirically stress-test data/soul_schema.sql and mathematical retention engine.

Test Dimensions:
1. Event Ledger Immutability & Conflict Attack Vectors (UPDATE, DELETE, REPLACE, UPSERT, CTE).
2. Anchor Invariants & Corruption Vectors (decay_rate != 0, provisional != 0, anchor demotion).
3. Foreign Keys & Bi-Temporal Intervals (cascading deletes, orphaned references, inverted intervals).
4. FTS5 Search Robustness & Edge Cases (syntax characters, SQL injection, unicode/CJK tokenization).
5. Mathematical Formulations across Extreme Bounds (t=0, t=10^9, S->0, S->inf, NaN, inf).
6. Quarantine Promotion Gate & View Integrity.
"""

import math
import os
import sqlite3
import sys
import unittest
from pathlib import Path
from typing import Dict, Any, List

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


def create_db(isolation_level=None) -> sqlite3.Connection:
    conn = sqlite3.connect(":memory:", isolation_level=isolation_level)
    conn.execute("PRAGMA foreign_keys = ON;")
    conn.row_factory = sqlite3.Row
    conn.executescript(load_production_schema())
    return conn


class TestAdversarialImmutability(unittest.TestCase):
    """
    Stress-tests the immutability of the 'events' table.
    Validates blocked mutations and exposes REPLACE conflict resolution bypasses.
    """

    def setUp(self):
        self.conn = create_db()
        self.cursor = self.conn.cursor()
        self.cursor.execute(
            """
            INSERT INTO events (id, session_id, timestamp, event_type, raw_content, metadata_json, sha256_hash, is_pinned)
            VALUES (1, 's1', 1700000000000, 'USER_CHAT', 'Initial raw turn content', '{"source":"user"}', 'hash_001', 0);
            """
        )

    def tearDown(self):
        self.conn.close()

    def test_direct_update_aborts(self):
        """TC-ADV-01: Direct single-row UPDATE on events is blocked by trigger."""
        with self.assertRaises(sqlite3.DatabaseError):
            self.cursor.execute("UPDATE events SET raw_content = 'tampered' WHERE id = 1;")

    def test_direct_delete_aborts(self):
        """TC-ADV-02: Direct single-row DELETE on events is blocked by trigger."""
        with self.assertRaises(sqlite3.DatabaseError):
            self.cursor.execute("DELETE FROM events WHERE id = 1;")

    def test_truncate_simulation_aborts(self):
        """TC-ADV-03: Mass table wipe DELETE FROM events is blocked by trigger."""
        with self.assertRaises(sqlite3.DatabaseError):
            self.cursor.execute("DELETE FROM events;")

    def test_cte_update_aborts(self):
        """TC-ADV-04: Common Table Expression (CTE) UPDATE is blocked by trigger."""
        with self.assertRaises(sqlite3.DatabaseError):
            self.cursor.execute(
                """
                WITH target AS (SELECT id FROM events WHERE id = 1)
                UPDATE events SET raw_content = 'cte_tampered' WHERE id IN (SELECT id FROM target);
                """
            )

    def test_cte_delete_aborts(self):
        """TC-ADV-05: Common Table Expression (CTE) DELETE is blocked by trigger."""
        with self.assertRaises(sqlite3.DatabaseError):
            self.cursor.execute(
                """
                WITH target AS (SELECT id FROM events WHERE id = 1)
                DELETE FROM events WHERE id IN (SELECT id FROM target);
                """
            )

    def test_upsert_do_update_aborts(self):
        """TC-ADV-06: INSERT ... ON CONFLICT(id) DO UPDATE is blocked by BEFORE UPDATE trigger."""
        with self.assertRaises(sqlite3.DatabaseError):
            self.cursor.execute(
                """
                INSERT INTO events (id, session_id, timestamp, event_type, raw_content, metadata_json, sha256_hash, is_pinned)
                VALUES (1, 's1', 1700000001000, 'USER_CHAT', 'tampered upsert', '{}', 'hash_003', 0)
                ON CONFLICT(id) DO UPDATE SET raw_content = excluded.raw_content;
                """
            )

    def test_vulnerability_insert_or_replace_pk_bypass(self):
        """
        TC-ADV-07 (HARDENING VERIFICATION):
        Verifies that INSERT OR REPLACE / REPLACE INTO is strictly blocked by the
        trg_events_prevent_insert_replace trigger and recursive_triggers pragma,
        preserving append-only ledger immutability.
        """
        with self.assertRaises(sqlite3.DatabaseError):
            self.cursor.execute(
                """
                INSERT OR REPLACE INTO events (id, session_id, timestamp, event_type, raw_content, metadata_json, sha256_hash, is_pinned)
                VALUES (1, 's1', 1700000001000, 'USER_CHAT', 'TAMPERED_BY_REPLACE', '{}', 'hash_002', 0);
                """
            )
        self.cursor.execute("SELECT raw_content, sha256_hash FROM events WHERE id = 1;")
        row = self.cursor.fetchone()
        self.assertEqual(row["raw_content"], "Initial raw turn content")

    def test_hardening_solution_blocks_replace(self):
        """
        TC-ADV-08:
        Demonstrates the architectural fix: adding PRAGMA recursive_triggers = ON and a BEFORE INSERT trigger
        blocks all REPLACE bypass attempts.
        """
        conn = sqlite3.connect(":memory:")
        conn.execute("PRAGMA recursive_triggers = ON;")
        conn.row_factory = sqlite3.Row
        conn.executescript(load_production_schema())
        # Add proposed hardening trigger
        conn.execute(
            """
            CREATE TRIGGER trg_events_prevent_replace
            BEFORE INSERT ON events
            WHEN EXISTS (SELECT 1 FROM events WHERE id = new.id OR (new.sha256_hash IS NOT NULL AND sha256_hash = new.sha256_hash))
            BEGIN
                SELECT RAISE(ABORT, 'events table is immutable: cannot replace or overwrite existing records');
            END;
            """
        )
        c = conn.cursor()
        c.execute("INSERT INTO events (id, session_id, timestamp, event_type, raw_content, sha256_hash) VALUES (1, 's1', 100, 'USER_CHAT', 'orig', 'h1');")

        # Now INSERT OR REPLACE must abort
        with self.assertRaises(sqlite3.DatabaseError):
            c.execute("INSERT OR REPLACE INTO events (id, session_id, timestamp, event_type, raw_content, sha256_hash) VALUES (1, 's1', 100, 'USER_CHAT', 'tampered', 'h2');")
        conn.close()


class TestAdversarialAnchorInvariants(unittest.TestCase):
    """
    Stress-tests anchor invariants on nodes and edges.
    Enforces is_anchor=1 => decay_rate=0.0 and provisional=0,
    and tests anchor demotion vulnerabilities.
    """

    def setUp(self):
        self.conn = create_db()
        self.cursor = self.conn.cursor()

    def tearDown(self):
        self.conn.close()

    def test_insert_node_anchor_violations_abort(self):
        """TC-ADV-09: Inserting anchor node with non-zero decay or provisional=1 aborts."""
        # Non-zero decay
        with self.assertRaises(sqlite3.DatabaseError):
            self.cursor.execute("INSERT INTO nodes (name, type, is_anchor, decay_rate, provisional) VALUES ('Allergy', 'CORE_IDENTITY', 1, 0.05, 0);")
        # Provisional = 1
        with self.assertRaises(sqlite3.DatabaseError):
            self.cursor.execute("INSERT INTO nodes (name, type, is_anchor, decay_rate, provisional) VALUES ('Allergy', 'CORE_IDENTITY', 1, 0.0, 1);")

    def test_insert_edge_anchor_violations_abort(self):
        """TC-ADV-10: Inserting anchor edge with non-zero decay or provisional=1 aborts."""
        self.cursor.execute("INSERT INTO nodes (name, type, is_anchor, decay_rate, provisional) VALUES ('A', 'USER', 1, 0.0, 0);")
        nA = self.cursor.lastrowid
        self.cursor.execute("INSERT INTO nodes (name, type, is_anchor, decay_rate, provisional) VALUES ('B', 'CONCEPT', 1, 0.0, 0);")
        nB = self.cursor.lastrowid

        with self.assertRaises(sqlite3.DatabaseError):
            self.cursor.execute("INSERT INTO edges (source_id, target_id, relation, is_anchor, decay_rate, provisional) VALUES (?, ?, 'REL', 1, 0.05, 0);", (nA, nB))
        with self.assertRaises(sqlite3.DatabaseError):
            self.cursor.execute("INSERT INTO edges (source_id, target_id, relation, is_anchor, decay_rate, provisional) VALUES (?, ?, 'REL', 1, 0.0, 1);", (nA, nB))

    def test_update_existing_anchor_corruptions_abort(self):
        """TC-ADV-11: Mutating decay_rate or provisional on existing anchor aborts."""
        self.cursor.execute("INSERT INTO nodes (name, type, is_anchor, decay_rate, provisional) VALUES ('Allergy', 'CORE_IDENTITY', 1, 0.0, 0);")
        nid = self.cursor.lastrowid

        with self.assertRaises(sqlite3.DatabaseError):
            self.cursor.execute("UPDATE nodes SET decay_rate = 0.1 WHERE id = ?;", (nid,))
        with self.assertRaises(sqlite3.DatabaseError):
            self.cursor.execute("UPDATE nodes SET provisional = 1 WHERE id = ?;", (nid,))

    def test_anchor_demotion_vulnerability(self):
        """
        TC-ADV-12 (HARDENING VERIFICATION):
        Demonstrates that trg_nodes_prevent_anchor_demotion strictly prevents
        demoting an established anchor node (is_anchor = 1) to is_anchor = 0.
        """
        self.cursor.execute("INSERT INTO nodes (name, type, is_anchor, decay_rate, provisional) VALUES ('Lily Allergy', 'CORE_IDENTITY', 1, 0.0, 0);")
        nid = self.cursor.lastrowid

        with self.assertRaises(sqlite3.DatabaseError):
            self.cursor.execute("UPDATE nodes SET is_anchor = 0, decay_rate = 0.5 WHERE id = ?;", (nid,))

        self.cursor.execute("SELECT is_anchor, decay_rate FROM nodes WHERE id = ?;", (nid,))
        row = self.cursor.fetchone()
        self.assertEqual(row["is_anchor"], 1)
        self.assertEqual(row["decay_rate"], 0.0)


class TestAdversarialForeignKeysAndBiTemporal(unittest.TestCase):
    """
    Stress-tests foreign key cascading, orphaned references,
    and bi-temporal validity interval constraints.
    """

    def setUp(self):
        self.conn = create_db()
        self.cursor = self.conn.cursor()

    def tearDown(self):
        self.conn.close()

    def test_cascade_delete_on_parent_nodes(self):
        """TC-ADV-13: Deleting a node cascades to all incoming and outgoing edges."""
        self.cursor.execute("INSERT INTO nodes (name, type, is_anchor, decay_rate, provisional) VALUES ('NodeA', 'USER', 1, 0.0, 0);")
        nA = self.cursor.lastrowid
        self.cursor.execute("INSERT INTO nodes (name, type, is_anchor, decay_rate, provisional) VALUES ('NodeB', 'CONCEPT', 1, 0.0, 0);")
        nB = self.cursor.lastrowid
        self.cursor.execute("INSERT INTO edges (source_id, target_id, relation, is_anchor, decay_rate, provisional) VALUES (?, ?, 'USES', 1, 0.0, 0);", (nA, nB))
        eid = self.cursor.lastrowid

        # Deleting node A cascades to edge
        self.cursor.execute("DELETE FROM nodes WHERE id = ?;", (nA,))
        self.cursor.execute("SELECT count(*) AS c FROM edges WHERE id = ?;", (eid,))
        self.assertEqual(self.cursor.fetchone()["c"], 0)

    def test_orphaned_foreign_key_rejected(self):
        """TC-ADV-14: Inserting edge with non-existent source/target node raises IntegrityError."""
        with self.assertRaises(sqlite3.IntegrityError):
            self.cursor.execute("INSERT INTO edges (source_id, target_id, relation) VALUES (999, 888, 'KNOWS');")

    def test_inverted_bitemporal_interval_vulnerability(self):
        """
        TC-ADV-15 (HARDENING VERIFICATION):
        Verifies that edges table strictly enforces valid_until >= valid_from
        via table CHECK constraint.
        """
        self.cursor.execute("INSERT INTO nodes (name, type) VALUES ('A', 'USER'), ('B', 'CONCEPT');")
        nA, nB = 1, 2

        with self.assertRaises(sqlite3.IntegrityError):
            self.cursor.execute(
                """
                INSERT INTO edges (source_id, target_id, relation, valid_from, valid_until)
                VALUES (?, ?, 'TRAVELS_TO', 2000, 1000);
                """,
                (nA, nB),
            )

    def test_missing_positive_stability_check_deficiency(self):
        """
        TC-ADV-16 (HARDENING VERIFICATION):
        Verifies that nodes and edges table CHECK constraints reject stability <= 0.0,
        preventing mathematical division-by-zero errors in retrievability calculations.
        """
        with self.assertRaises(sqlite3.IntegrityError):
            self.cursor.execute("INSERT INTO nodes (name, type, stability, stability_days) VALUES ('Zero Stability', 'CONCEPT', 0.0, 0.0);")


class TestAdversarialFTS5(unittest.TestCase):
    """
    Stress-tests FTS5 full-text indexing, syntax character crashes,
    injection attacks, and unicode tokenization boundaries.
    """

    def setUp(self):
        self.conn = create_db()
        self.cursor = self.conn.cursor()

    def tearDown(self):
        self.conn.close()

    def test_fts5_sql_injection_payload_storage(self):
        """TC-ADV-17: Malicious SQL injection payloads in raw_content are safely indexed as string literals."""
        payload = "'; DROP TABLE events; -- ' OR '1'='1"
        self.cursor.execute(
            "INSERT INTO events (id, session_id, timestamp, event_type, raw_content, sha256_hash) VALUES (501, 's', 100, 'USER_CHAT', ?, 'h_sql');",
            (payload,),
        )
        self.cursor.execute("SELECT count(*) AS c FROM events_fts WHERE events_fts MATCH '\"DROP TABLE\"';")
        self.assertEqual(self.cursor.fetchone()["c"], 1)

    def test_fts5_unescaped_syntax_queries_raise_operational_error(self):
        """
        TC-ADV-18 (SEARCH ROBUSTNESS REQUIREMENT):
        Empirically verifies that raw unescaped boolean/syntax operators (AND, OR, NOT, *, unclosed quote)
        cause FTS5 parser crashes unless sanitized by the query engine.
        """
        self.cursor.execute("INSERT INTO events (id, session_id, timestamp, event_type, raw_content, sha256_hash) VALUES (502, 's', 100, 'USER_CHAT', 'test content', 'h_syntax');")

        syntax_crash_queries = ["AND", "OR", "NOT", "*", "\"unclosed quote", "NEAR(a, b)"]
        for q in syntax_crash_queries:
            with self.assertRaises(sqlite3.OperationalError, msg=f"Query {q!r} should trigger FTS5 syntax error"):
                self.cursor.execute("SELECT rowid FROM events_fts WHERE events_fts MATCH ?;", (q,))

    def test_fts5_sanitization_defense_prevents_crashes(self):
        """TC-ADV-19: Token-wrapping sanitization defense completely eliminates FTS5 query parser crashes."""
        def sanitize_fts5_token(term: str) -> str:
            escaped = term.replace('"', '""')
            return f'"{escaped}"'

        syntax_queries = ["AND", "OR", "NOT", "*", "\"unclosed quote", "NEAR(a, b)", "column:foo"]
        for q in syntax_queries:
            safe_q = sanitize_fts5_token(q)
            self.cursor.execute("SELECT rowid FROM events_fts WHERE events_fts MATCH ?;", (safe_q,))
            _ = self.cursor.fetchall()  # Must not raise sqlite3.OperationalError

    def test_fts5_cjk_unsegmented_tokenization_limitation(self):
        """
        TC-ADV-20 (INTERNATIONALIZATION LIMITATION):
        Demonstrates that default unicode61 tokenizer does not segment continuous CJK ideographs,
        causing sub-word query misses unless prefix matching or space-separated tokens are used.
        """
        # Continuous CJK text
        self.cursor.execute(
            "INSERT INTO events (id, session_id, timestamp, event_type, raw_content, sha256_hash) VALUES (601, 's', 100, 'USER_CHAT', '我的名字是艾伦，编写智能伴侣。', 'h_cjk1');"
        )
        # Space-segmented CJK text
        self.cursor.execute(
            "INSERT INTO events (id, session_id, timestamp, event_type, raw_content, sha256_hash) VALUES (602, 's', 100, 'USER_CHAT', '编写 智能 伴侣', 'h_cjk2');"
        )

        # Query subword "智能" without prefix
        self.cursor.execute("SELECT rowid FROM events_fts WHERE events_fts MATCH '智能';")
        matches = [r["rowid"] for r in self.cursor.fetchall()]

        # Space-segmented matches, continuous does not
        self.assertIn(602, matches, "Space-segmented CJK must match")
        self.assertNotIn(601, matches, "Continuous CJK without word spaces is not segmented by unicode61")

        # Prefix wildcard on full token matches continuous text
        self.cursor.execute("SELECT rowid FROM events_fts WHERE events_fts MATCH '编写智能伴侣*';")
        prefix_matches = [r["rowid"] for r in self.cursor.fetchall()]
        self.assertIn(601, prefix_matches, "Full token prefix match succeeds")


class TestAdversarialMathematicalFormulations(unittest.TestCase):
    """
    Stress-tests mathematical functions across extreme numerical boundaries:
    t = 0, t = 10^9, S -> 0, S -> inf, NaN, inf, clamping, and edge cases.
    """

    def test_retrievability_extremes(self):
        """TC-ADV-21: R(t, S) behavior at numerical extremes."""
        # 1. t = 0 identically gives 1.0
        for s in [1e-12, 1e-3, 1.0, 100.0, 1e12]:
            self.assertAlmostEqual(calculate_retrievability(0.0, stability=s), 1.0, places=9)

        # 2. t = 10^9 seconds (11574.07 days) with S = 1.0 -> underflows to 0.0 smoothly
        t_10_9_days = 1e9 / 86400.0
        r_10_9 = calculate_retrievability(t_10_9_days, stability=1.0)
        self.assertEqual(r_10_9, 0.0)

        # 3. S -> infinity -> R(t) = 1.0 for any reasonable t
        r_inf_s = calculate_retrievability(365.0, stability=1e15)
        self.assertAlmostEqual(r_inf_s, 1.0, places=9)

        # 4. S -> 0 (e.g. 1e-15) -> immediate decay to 0.0
        r_tiny_s = calculate_retrievability(0.001, stability=1e-15)
        self.assertEqual(r_tiny_s, 0.0)

        # 5. Invalid parameters raise ValueError
        with self.assertRaises(ValueError):
            calculate_retrievability(1.0, stability=0.0)
        with self.assertRaises(ValueError):
            calculate_retrievability(1.0, stability=-1.0)
        with self.assertRaises(ValueError):
            calculate_retrievability(-0.1, stability=1.0)

        # 6. Anchors remain 1.0 permanently
        for t in [0.0, 1.0, 365.0, 1e6, 1e9]:
            self.assertEqual(calculate_retrievability(t, stability=1.0, is_anchor=True), 1.0)

    def test_stability_growth_extremes_and_clamping(self):
        """TC-ADV-22: FSRS stability growth under extreme bounds."""
        s0 = 10.0

        # R out-of-bounds (<0 or >1) is safely clamped
        s_clamped_neg = calculate_stability_growth_fsrs(s0, r_retrievability=-5.0, delta_t_days=1.0)
        s_r0 = calculate_stability_growth_fsrs(s0, r_retrievability=0.0, delta_t_days=1.0)
        self.assertEqual(s_clamped_neg, s_r0)

        # Spacing delta_t cap at 365 days
        s_cap = calculate_stability_growth_fsrs(s0, r_retrievability=0.5, delta_t_days=365.0)
        s_huge_dt = calculate_stability_growth_fsrs(s0, r_retrievability=0.5, delta_t_days=1e6)
        self.assertEqual(s_cap, s_huge_dt)

        # 100% penalty eliminates growth bonus completely
        s_penalty_100 = calculate_stability_growth_fsrs(s0, r_retrievability=0.5, delta_t_days=10.0, penalty_contradiction=1.0)
        self.assertEqual(s_penalty_100, s0)

    def test_anchor_scoring_extremes(self):
        """TC-ADV-23: Anchor scoring logistic regression bounds."""
        # Explicit bypass always returns 1.0
        self.assertEqual(calculate_anchor_score(c_explicit=1, emotional_valence=0.0, semantic_centrality=0.0, category="TRANSIENT_STATE"), 1.0)

        # Output is strictly bounded in (0.0, 1.0)
        score_min = calculate_anchor_score(c_explicit=0, emotional_valence=0.0, semantic_centrality=0.0, category="TRANSIENT_STATE")
        score_max = calculate_anchor_score(c_explicit=0, emotional_valence=1.0, semantic_centrality=1.0, category="CORE_IDENTITY")
        self.assertGreater(score_min, 0.0)
        self.assertLess(score_max, 1.0)
        self.assertGreater(score_max, score_min)


class TestQuarantinePromotionViewIntegrity(unittest.TestCase):
    """
    Stress-tests the promotion candidate view against the authoritative specification.
    """

    def setUp(self):
        self.conn = create_db()
        self.cursor = self.conn.cursor()

    def tearDown(self):
        self.conn.close()

    def test_view_confidence_and_observation_count_deficiency(self):
        """
        TC-ADV-24 (SPECIFICATION DISCREPANCY):
        Authoritative specification BRAIN_SPEC.md §4.7 requires:
          - distinct_calendar_days >= 2
          - dt >= 36.0h (129,600,000 ms)
          - observation_count >= 2
          - confidence >= 0.70
        However, data/soul_schema.sql v_quarantine_promotion_candidates view omits
        confidence >= 0.70 and observation_count >= 2 from its WHERE clause!
        """
        # Insert low-confidence candidate (0.15) with observation_count = 1
        t_first = 1700000000000
        t_last = t_first + 130000000000  # > 36h
        self.cursor.execute(
            """
            INSERT INTO quarantine (
                fact_hash, subject, predicate, object, confidence,
                first_seen_timestamp, last_seen_timestamp,
                distinct_calendar_days, observation_count, status
            ) VALUES (
                'h_low_conf', 'Alan', 'MIGHT_LIKE', 'Bogus Food', 0.15,
                ?, ?, 2, 1, 'provisional'
            );
            """,
            (t_first, t_last),
        )

        self.cursor.execute("SELECT fact_hash, confidence, observation_count FROM v_quarantine_promotion_candidates WHERE fact_hash = 'h_low_conf';")
        row = self.cursor.fetchone()

        # The view correctly filters out low-confidence (<0.70) or low-observation (<2) candidates!
        self.assertIsNone(
            row,
            "HARDENING VERIFIED: v_quarantine_promotion_candidates correctly filtered out candidate with confidence=0.15 and observation_count=1!"
        )


if __name__ == "__main__":
    unittest.main()
