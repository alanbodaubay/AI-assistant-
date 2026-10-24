#!/usr/bin/env python3
"""
ScreenMate AI: Final Challenger Storage Schema & Trigger Verification
File: tests/challenger_final_storage_test.py
Author: Final Adversarial Challenger
"""

import sqlite3
import unittest
from pathlib import Path

SCHEMA_PATH = Path(__file__).resolve().parent.parent / "data" / "soul_schema.sql"

def get_db():
    conn = sqlite3.connect(":memory:")
    conn.execute("PRAGMA foreign_keys = ON;")
    conn.execute("PRAGMA recursive_triggers = ON;")
    conn.row_factory = sqlite3.Row
    with open(SCHEMA_PATH, "r", encoding="utf-8") as f:
        conn.executescript(f.read())
    return conn

class TestFinalStorageHardening(unittest.TestCase):
    def setUp(self):
        self.conn = get_db()
        self.cursor = self.conn.cursor()

    def tearDown(self):
        self.conn.close()

    # -------------------------------------------------------------------------
    # 1. EVENTS LEDGER IMMUTABILITY & CONFLICT PREVENTION
    # -------------------------------------------------------------------------
    def test_events_standard_insert(self):
        self.cursor.execute(
            "INSERT INTO events (session_id, timestamp, event_type, raw_content, sha256_hash) "
            "VALUES ('s1', 1000, 'USER_CHAT', 'Hello ScreenMate', 'h1');"
        )
        rowid = self.cursor.lastrowid
        self.assertEqual(rowid, 1)
        self.cursor.execute("SELECT raw_content FROM events WHERE id = 1;")
        self.assertEqual(self.cursor.fetchone()["raw_content"], "Hello ScreenMate")

    def test_events_insert_or_replace_pk_blocked(self):
        self.cursor.execute(
            "INSERT INTO events (id, session_id, timestamp, event_type, raw_content, sha256_hash) "
            "VALUES (10, 's1', 1000, 'USER_CHAT', 'Original turn', 'h10');"
        )
        with self.assertRaises(sqlite3.DatabaseError) as ctx:
            self.cursor.execute(
                "INSERT OR REPLACE INTO events (id, session_id, timestamp, event_type, raw_content, sha256_hash) "
                "VALUES (10, 's1', 2000, 'USER_CHAT', 'Malicious overwrite', 'h10_tampered');"
            )
        self.assertIn("immutable", str(ctx.exception).lower())
        self.cursor.execute("SELECT raw_content FROM events WHERE id = 10;")
        self.assertEqual(self.cursor.fetchone()["raw_content"], "Original turn")

    def test_events_insert_or_replace_hash_blocked(self):
        self.cursor.execute(
            "INSERT INTO events (id, session_id, timestamp, event_type, raw_content, sha256_hash) "
            "VALUES (20, 's1', 1000, 'USER_CHAT', 'Original turn', 'sha_unique_1');"
        )
        with self.assertRaises(sqlite3.DatabaseError) as ctx:
            self.cursor.execute(
                "INSERT OR REPLACE INTO events (id, session_id, timestamp, event_type, raw_content, sha256_hash) "
                "VALUES (21, 's1', 2000, 'USER_CHAT', 'Overwriting by hash', 'sha_unique_1');"
            )
        self.assertIn("immutable", str(ctx.exception).lower())
        self.cursor.execute("SELECT count(*) AS c FROM events WHERE id = 21;")
        self.assertEqual(self.cursor.fetchone()["c"], 0)

    def test_events_insert_or_ignore_pk_blocked(self):
        self.cursor.execute(
            "INSERT INTO events (id, session_id, timestamp, event_type, raw_content, sha256_hash) "
            "VALUES (30, 's1', 1000, 'USER_CHAT', 'Original turn', 'h30');"
        )
        # BEFORE INSERT trigger fires BEFORE IGNORE conflict resolution, raising ABORT
        with self.assertRaises(sqlite3.DatabaseError) as ctx:
            self.cursor.execute(
                "INSERT OR IGNORE INTO events (id, session_id, timestamp, event_type, raw_content, sha256_hash) "
                "VALUES (30, 's1', 2000, 'USER_CHAT', 'Ignore attempt', 'h30_new');"
            )
        self.assertIn("immutable", str(ctx.exception).lower())
        self.cursor.execute("SELECT raw_content FROM events WHERE id = 30;")
        self.assertEqual(self.cursor.fetchone()["raw_content"], "Original turn")

    def test_events_update_and_delete_strictly_blocked(self):
        self.cursor.execute(
            "INSERT INTO events (id, session_id, timestamp, event_type, raw_content) "
            "VALUES (40, 's1', 1000, 'USER_CHAT', 'Content 40');"
        )
        with self.assertRaises(sqlite3.DatabaseError):
            self.cursor.execute("UPDATE events SET raw_content = 'tampered' WHERE id = 40;")
        with self.assertRaises(sqlite3.DatabaseError):
            self.cursor.execute("DELETE FROM events WHERE id = 40;")
        with self.assertRaises(sqlite3.DatabaseError):
            self.cursor.execute("DELETE FROM events;")

    def test_events_fts_automatic_sync(self):
        self.cursor.execute(
            "INSERT INTO events (session_id, timestamp, event_type, raw_content, metadata_json) "
            "VALUES ('s1', 1000, 'USER_CHAT', 'Peanut allergy reminder', '{\"user\":\"Alan\"}');"
        )
        eid = self.cursor.lastrowid
        self.cursor.execute("SELECT rowid, raw_content FROM events_fts WHERE events_fts MATCH '\"Peanut allergy\"';")
        row = self.cursor.fetchone()
        self.assertIsNotNone(row)
        self.assertEqual(row["rowid"], eid)

    # -------------------------------------------------------------------------
    # 2. NODES ANCHOR INVARIANTS & CONSTRAINTS
    # -------------------------------------------------------------------------
    def test_nodes_insert_valid_anchor(self):
        self.cursor.execute(
            "INSERT INTO nodes (name, type, is_anchor, decay_rate, provisional) "
            "VALUES ('Lily Allergy', 'CORE_IDENTITY', 1, 0.0, 0);"
        )
        nid = self.cursor.lastrowid
        self.cursor.execute("SELECT is_anchor, decay_rate, provisional FROM nodes WHERE id = ?;", (nid,))
        row = self.cursor.fetchone()
        self.assertEqual(row["is_anchor"], 1)
        self.assertEqual(row["decay_rate"], 0.0)
        self.assertEqual(row["provisional"], 0)

    def test_nodes_insert_invalid_anchor_aborts(self):
        with self.assertRaises(sqlite3.DatabaseError):
            self.cursor.execute("INSERT INTO nodes (name, type, is_anchor, decay_rate, provisional) VALUES ('Bad', 'CONCEPT', 1, 0.05, 0);")
        with self.assertRaises(sqlite3.DatabaseError):
            self.cursor.execute("INSERT INTO nodes (name, type, is_anchor, decay_rate, provisional) VALUES ('Bad', 'CONCEPT', 1, 0.0, 1);")

    def test_nodes_check_constraints(self):
        # decay_rate >= 0.0
        with self.assertRaises(sqlite3.IntegrityError):
            self.cursor.execute("INSERT INTO nodes (name, type, decay_rate) VALUES ('Bad', 'CONCEPT', -0.01);")
        # stability > 0.0
        with self.assertRaises(sqlite3.IntegrityError):
            self.cursor.execute("INSERT INTO nodes (name, type, stability) VALUES ('Bad', 'CONCEPT', 0.0);")
        with self.assertRaises(sqlite3.IntegrityError):
            self.cursor.execute("INSERT INTO nodes (name, type, stability) VALUES ('Bad', 'CONCEPT', -1.0);")
        # stability_days > 0.0
        with self.assertRaises(sqlite3.IntegrityError):
            self.cursor.execute("INSERT INTO nodes (name, type, stability_days) VALUES ('Bad', 'CONCEPT', 0.0);")
        # observation_days_count >= 1
        with self.assertRaises(sqlite3.IntegrityError):
            self.cursor.execute("INSERT INTO nodes (name, type, observation_days_count) VALUES ('Bad', 'CONCEPT', 0);")
        # access_count >= 1
        with self.assertRaises(sqlite3.IntegrityError):
            self.cursor.execute("INSERT INTO nodes (name, type, access_count) VALUES ('Bad', 'CONCEPT', 0);")

    def test_nodes_prevent_anchor_demotion_and_decay(self):
        self.cursor.execute("INSERT INTO nodes (name, type, is_anchor, decay_rate, provisional) VALUES ('Alan Anchor', 'USER', 1, 0.0, 0);")
        nid = self.cursor.lastrowid

        # Attempt demotion: is_anchor = 0
        with self.assertRaises(sqlite3.DatabaseError) as ctx:
            self.cursor.execute("UPDATE nodes SET is_anchor = 0 WHERE id = ?;", (nid,))
        self.assertIn("demoted", str(ctx.exception).lower())

        # Attempt adding decay
        with self.assertRaises(sqlite3.DatabaseError):
            self.cursor.execute("UPDATE nodes SET decay_rate = 0.05 WHERE id = ?;", (nid,))

        # Attempt provisional flag
        with self.assertRaises(sqlite3.DatabaseError):
            self.cursor.execute("UPDATE nodes SET provisional = 1 WHERE id = ?;", (nid,))

        # Allowed update
        self.cursor.execute("UPDATE nodes SET name = 'Alan Updated' WHERE id = ?;", (nid,))
        self.cursor.execute("SELECT name FROM nodes WHERE id = ?;", (nid,))
        self.assertEqual(self.cursor.fetchone()["name"], "Alan Updated")

    # -------------------------------------------------------------------------
    # 3. EDGES ANCHOR INVARIANTS & CONSTRAINTS
    # -------------------------------------------------------------------------
    def test_edges_insert_valid_anchor(self):
        self.cursor.execute("INSERT INTO nodes (name, type, is_anchor, decay_rate, provisional) VALUES ('N1', 'USER', 1, 0.0, 0);")
        n1 = self.cursor.lastrowid
        self.cursor.execute("INSERT INTO nodes (name, type, is_anchor, decay_rate, provisional) VALUES ('N2', 'CONCEPT', 1, 0.0, 0);")
        n2 = self.cursor.lastrowid

        self.cursor.execute("INSERT INTO edges (source_id, target_id, relation, is_anchor, decay_rate, provisional) VALUES (?, ?, 'REL', 1, 0.0, 0);", (n1, n2))
        eid = self.cursor.lastrowid
        self.cursor.execute("SELECT is_anchor, decay_rate, provisional FROM edges WHERE id = ?;", (eid,))
        row = self.cursor.fetchone()
        self.assertEqual(row["is_anchor"], 1)
        self.assertEqual(row["decay_rate"], 0.0)

    def test_edges_check_constraints(self):
        self.cursor.execute("INSERT INTO nodes (name, type) VALUES ('A', 'USER'), ('B', 'CONCEPT');")
        nA, nB = 1, 2

        # Inverted validity interval
        with self.assertRaises(sqlite3.IntegrityError):
            self.cursor.execute("INSERT INTO edges (source_id, target_id, relation, valid_from, valid_until) VALUES (?, ?, 'R', 5000, 4000);", (nA, nB))
        # Valid interval
        self.cursor.execute("INSERT INTO edges (source_id, target_id, relation, valid_from, valid_until) VALUES (?, ?, 'R', 5000, 5000);", (nA, nB))
        self.cursor.execute("INSERT INTO edges (source_id, target_id, relation, valid_from, valid_until) VALUES (?, ?, 'R', 5000, 6000);", (nA, nB))

        # Weight [0, 1]
        with self.assertRaises(sqlite3.IntegrityError):
            self.cursor.execute("INSERT INTO edges (source_id, target_id, relation, weight) VALUES (?, ?, 'R', -0.1);", (nA, nB))
        with self.assertRaises(sqlite3.IntegrityError):
            self.cursor.execute("INSERT INTO edges (source_id, target_id, relation, weight) VALUES (?, ?, 'R', 1.01);", (nA, nB))

        # Confidence [0, 1]
        with self.assertRaises(sqlite3.IntegrityError):
            self.cursor.execute("INSERT INTO edges (source_id, target_id, relation, confidence) VALUES (?, ?, 'R', -0.01);", (nA, nB))
        with self.assertRaises(sqlite3.IntegrityError):
            self.cursor.execute("INSERT INTO edges (source_id, target_id, relation, confidence) VALUES (?, ?, 'R', 1.01);", (nA, nB))

        # Stability > 0
        with self.assertRaises(sqlite3.IntegrityError):
            self.cursor.execute("INSERT INTO edges (source_id, target_id, relation, stability) VALUES (?, ?, 'R', 0.0);", (nA, nB))
        with self.assertRaises(sqlite3.IntegrityError):
            self.cursor.execute("INSERT INTO edges (source_id, target_id, relation, stability_days) VALUES (?, ?, 'R', 0.0);", (nA, nB))

    def test_edges_prevent_anchor_demotion_and_decay(self):
        self.cursor.execute("INSERT INTO nodes (name, type) VALUES ('A', 'USER'), ('B', 'CONCEPT');")
        nA, nB = 1, 2
        self.cursor.execute("INSERT INTO edges (source_id, target_id, relation, is_anchor, decay_rate, provisional) VALUES (?, ?, 'R', 1, 0.0, 0);", (nA, nB))
        eid = self.cursor.lastrowid

        # Attempt demotion
        with self.assertRaises(sqlite3.DatabaseError) as ctx:
            self.cursor.execute("UPDATE edges SET is_anchor = 0 WHERE id = ?;", (eid,))
        self.assertIn("demoted", str(ctx.exception).lower())

        # Attempt decay rate change
        with self.assertRaises(sqlite3.DatabaseError):
            self.cursor.execute("UPDATE edges SET decay_rate = 0.05 WHERE id = ?;", (eid,))

        # Allowed: closing edge bitemporally (valid_until = now)
        self.cursor.execute("UPDATE edges SET valid_until = valid_from + 1000 WHERE id = ?;", (eid,))
        self.cursor.execute("SELECT valid_until FROM edges WHERE id = ?;", (eid,))
        self.assertIsNotNone(self.cursor.fetchone()["valid_until"])

    # -------------------------------------------------------------------------
    # 4. QUARANTINE PROMOTION VIEW FILTERING
    # -------------------------------------------------------------------------
    def test_quarantine_promotion_view_filters(self):
        t0 = 1700000000000
        dt_36h = 129600000
        cases = [
            ("valid_candidate", 0.70, 2, 2, dt_36h, "provisional", True),
            ("low_conf", 0.69, 2, 2, dt_36h, "provisional", False),
            ("low_obs", 0.85, 1, 2, dt_36h, "provisional", False),
            ("low_cal_days", 0.85, 3, 1, dt_36h, "provisional", False),
            ("short_elapsed", 0.85, 3, 2, dt_36h - 1000, "provisional", False),
            ("already_promoted", 0.95, 5, 3, dt_36h + 50000, "promoted", False),
            ("rejected", 0.95, 5, 3, dt_36h + 50000, "rejected", False),
            ("strong_candidate", 0.99, 10, 5, dt_36h * 2, "provisional", True),
        ]

        for fact_hash, conf, obs, days, elapsed, status, expected_in_view in cases:
            self.cursor.execute(
                """
                INSERT INTO quarantine (
                    fact_hash, subject, predicate, object, confidence,
                    first_seen_timestamp, last_seen_timestamp,
                    observation_count, distinct_calendar_days, status
                ) VALUES (?, 'Sub', 'Pred', 'Obj', ?, ?, ?, ?, ?, ?);
                """,
                (fact_hash, conf, t0, t0 + elapsed, obs, days, status)
            )

        self.cursor.execute("SELECT fact_hash FROM v_quarantine_promotion_candidates;")
        in_view = {row["fact_hash"] for row in self.cursor.fetchall()}

        for fact_hash, _, _, _, _, _, expected_in_view in cases:
            if expected_in_view:
                self.assertIn(fact_hash, in_view, f"{fact_hash} should be in v_quarantine_promotion_candidates")
            else:
                self.assertNotIn(fact_hash, in_view, f"{fact_hash} should NOT be in v_quarantine_promotion_candidates")

if __name__ == "__main__":
    unittest.main()
