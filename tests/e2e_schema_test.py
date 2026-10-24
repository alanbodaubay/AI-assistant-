#!/usr/bin/env python3
"""
ScreenMate AI Brain & Memory System
E2E Schema & DDL Trigger Validation Harness
File: tests/e2e_schema_test.py

Validates:
1. SQLite DDL syntax execution and table creation.
2. Foreign key constraint enforcement & cascade deletes.
3. Immutability triggers on the `events` table (ABORT on UPDATE and DELETE).
4. FTS5 full-text indexing synchronization via triggers.
5. Anchor integrity triggers (enforcing is_anchor=1 => decay_rate=0.0 and provisional=0).
6. 2-Day provisional quarantine staging and promotion views.
"""

import hashlib
import os
import sqlite3
import sys
import unittest
from pathlib import Path

# Canonical DDL matching survey_technical.md Section 4.1 and spec_requirements.md Section 5
CANONICAL_DDL = """
PRAGMA foreign_keys = ON;
PRAGMA journal_mode = WAL;
PRAGMA synchronous = NORMAL;

-- 1. Immutable Raw Event Ledger
CREATE TABLE IF NOT EXISTS events (
    id                  INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp           INTEGER NOT NULL,
    event_type          TEXT NOT NULL CHECK(event_type IN (
                            'USER_CHAT', 'COMPANION_CHAT', 'SCREEN_GLANCE',
                            'SYSTEM_STATE', 'TOOL_EXECUTION', 'USER_PIN'
                        )),
    raw_content         TEXT NOT NULL,
    metadata_json       TEXT NOT NULL DEFAULT '{}',
    sha256_hash         TEXT NOT NULL UNIQUE,
    is_pinned           INTEGER NOT NULL DEFAULT 0 CHECK(is_pinned IN (0, 1)),
    created_at          INTEGER NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_events_timestamp ON events(timestamp);
CREATE INDEX IF NOT EXISTS idx_events_type ON events(event_type);
CREATE INDEX IF NOT EXISTS idx_events_pinned ON events(is_pinned);

-- 2. Full-Text Search (FTS5) Virtual Table
CREATE VIRTUAL TABLE IF NOT EXISTS events_fts USING fts5 (
    raw_content,
    metadata_json,
    content='events',
    content_rowid='id'
);

-- FTS5 Synchronization Triggers
CREATE TRIGGER IF NOT EXISTS trg_events_ai AFTER INSERT ON events BEGIN
    INSERT INTO events_fts (rowid, raw_content, metadata_json)
    VALUES (new.id, new.raw_content, new.metadata_json);
END;

CREATE TRIGGER IF NOT EXISTS trg_events_ad AFTER DELETE ON events BEGIN
    INSERT INTO events_fts (events_fts, rowid, raw_content, metadata_json)
    VALUES ('delete', old.id, old.raw_content, old.metadata_json);
END;

-- 3. Immutability Triggers (ABORT modifications to historical raw events)
CREATE TRIGGER IF NOT EXISTS trg_events_prevent_update
BEFORE UPDATE ON events
BEGIN
    SELECT RAISE(ABORT, 'CRITICAL: The events ledger is immutable and append-only. Updates forbidden.');
END;

CREATE TRIGGER IF NOT EXISTS trg_events_prevent_delete
BEFORE DELETE ON events
BEGIN
    SELECT RAISE(ABORT, 'CRITICAL: The events ledger is immutable and append-only. Deletes forbidden.');
END;

-- 4. Property Graph: Nodes
CREATE TABLE IF NOT EXISTS nodes (
    id                      INTEGER PRIMARY KEY AUTOINCREMENT,
    uuid                    TEXT NOT NULL UNIQUE,
    name                    TEXT NOT NULL,
    type                    TEXT NOT NULL CHECK(type IN (
                                'USER', 'PERSON', 'PROJECT', 'APP',
                                'INTEREST', 'HABIT', 'INSIDE_JOKE',
                                'CORE_IDENTITY', 'PREFERENCE', 'EPISODE'
                            )),
    attributes_json         TEXT NOT NULL DEFAULT '{}',
    is_anchor               INTEGER NOT NULL DEFAULT 0 CHECK(is_anchor IN (0, 1)),
    anchor_category         TEXT NULL CHECK(anchor_category IN (
                                'CORE_IDENTITY', 'FAMILY', 'MEDICAL', 'CORE_PREFERENCE', 'PINNED', NULL
                            )),
    created_at              INTEGER NOT NULL,
    updated_at              INTEGER NOT NULL,
    last_accessed_at        INTEGER NOT NULL,
    access_count            INTEGER NOT NULL DEFAULT 1,
    decay_rate              REAL NOT NULL DEFAULT 0.05,
    stability               REAL NOT NULL DEFAULT 1.0,
    retrievability          REAL NOT NULL DEFAULT 1.0,
    provisional             INTEGER NOT NULL DEFAULT 1 CHECK(provisional IN (0, 1)),
    first_observed_date     TEXT NOT NULL,
    last_observed_date      TEXT NOT NULL,
    observation_days_count  INTEGER NOT NULL DEFAULT 1,
    source_event_id         INTEGER REFERENCES events(id) ON DELETE SET NULL
);

CREATE INDEX IF NOT EXISTS idx_nodes_name ON nodes(name);
CREATE INDEX IF NOT EXISTS idx_nodes_type ON nodes(type);
CREATE INDEX IF NOT EXISTS idx_nodes_anchor ON nodes(is_anchor);
CREATE INDEX IF NOT EXISTS idx_nodes_provisional ON nodes(provisional);
CREATE INDEX IF NOT EXISTS idx_nodes_retrievability ON nodes(retrievability);

-- 5. Property Graph: Bi-Temporal Edges
CREATE TABLE IF NOT EXISTS edges (
    id                      INTEGER PRIMARY KEY AUTOINCREMENT,
    source_id               INTEGER NOT NULL REFERENCES nodes(id) ON DELETE CASCADE,
    target_id               INTEGER NOT NULL REFERENCES nodes(id) ON DELETE CASCADE,
    relation                TEXT NOT NULL,
    weight                  REAL NOT NULL DEFAULT 1.0,
    confidence              REAL NOT NULL DEFAULT 0.8,
    is_anchor               INTEGER NOT NULL DEFAULT 0 CHECK(is_anchor IN (0, 1)),
    decay_rate              REAL NOT NULL DEFAULT 0.05,
    stability               REAL NOT NULL DEFAULT 1.0,
    valid_from              INTEGER NOT NULL,
    valid_until             INTEGER NULL,
    is_active               INTEGER NOT NULL DEFAULT 1 CHECK(is_active IN (0, 1)),
    provisional             INTEGER NOT NULL DEFAULT 1 CHECK(provisional IN (0, 1)),
    source_event_id         INTEGER REFERENCES events(id) ON DELETE SET NULL,
    created_at              INTEGER NOT NULL,
    UNIQUE(source_id, target_id, relation)
);

CREATE INDEX IF NOT EXISTS idx_edges_lookup ON edges(source_id, relation, target_id);
CREATE INDEX IF NOT EXISTS idx_edges_active ON edges(is_active, provisional);

-- 6. Anchor Integrity Triggers (Enforce decay_rate = 0.0 and provisional = 0)
CREATE TRIGGER IF NOT EXISTS trg_nodes_anchor_integrity_bi
BEFORE INSERT ON nodes
WHEN new.is_anchor = 1
BEGIN
    SELECT CASE
        WHEN new.decay_rate != 0.0 THEN
            RAISE(ABORT, 'VIOLATION: Inserted anchor node must have decay_rate = 0.0')
        WHEN new.provisional != 0 THEN
            RAISE(ABORT, 'VIOLATION: Inserted anchor node cannot remain in provisional quarantine')
    END;
END;

CREATE TRIGGER IF NOT EXISTS trg_edges_anchor_integrity_bi
BEFORE INSERT ON edges
WHEN new.is_anchor = 1
BEGIN
    SELECT CASE
        WHEN new.decay_rate != 0.0 THEN
            RAISE(ABORT, 'VIOLATION: Inserted anchor edge must have decay_rate = 0.0')
        WHEN new.provisional != 0 THEN
            RAISE(ABORT, 'VIOLATION: Inserted anchor edge cannot remain in provisional quarantine')
    END;
END;

CREATE TRIGGER IF NOT EXISTS trg_nodes_anchor_enforce_zero_decay
BEFORE UPDATE OF is_anchor, decay_rate, provisional ON nodes
WHEN new.is_anchor = 1
BEGIN
    SELECT CASE
        WHEN new.decay_rate != 0.0 THEN
            RAISE(ABORT, 'VIOLATION: Anchor node must have decay_rate = 0.0')
        WHEN new.provisional != 0 THEN
            RAISE(ABORT, 'VIOLATION: Anchor node cannot remain in provisional quarantine')
    END;
END;

CREATE TRIGGER IF NOT EXISTS trg_edges_anchor_enforce_zero_decay
BEFORE UPDATE OF is_anchor, decay_rate, provisional ON edges
WHEN new.is_anchor = 1
BEGIN
    SELECT CASE
        WHEN new.decay_rate != 0.0 THEN
            RAISE(ABORT, 'VIOLATION: Anchor edge must have decay_rate = 0.0')
        WHEN new.provisional != 0 THEN
            RAISE(ABORT, 'VIOLATION: Anchor edge cannot remain in provisional quarantine')
    END;
END;

-- 7. Provisional Quarantine Staging Gate
CREATE TABLE IF NOT EXISTS quarantine (
    id                      INTEGER PRIMARY KEY AUTOINCREMENT,
    fact_hash               TEXT UNIQUE NOT NULL,
    subject                 TEXT NOT NULL,
    predicate               TEXT NOT NULL,
    object                  TEXT NOT NULL,
    confidence              REAL NOT NULL DEFAULT 0.5,
    first_observed_at       INTEGER NOT NULL,
    last_observed_at        INTEGER NOT NULL,
    observation_count       INTEGER NOT NULL DEFAULT 1,
    distinct_calendar_days  INTEGER NOT NULL DEFAULT 1,
    source_event_ids_json   TEXT NOT NULL DEFAULT '[]',
    status                  TEXT NOT NULL DEFAULT 'provisional' CHECK(status IN ('provisional', 'promoted', 'expired', 'rejected'))
);

CREATE INDEX IF NOT EXISTS idx_quarantine_status ON quarantine(status);
CREATE INDEX IF NOT EXISTS idx_quarantine_hash ON quarantine(fact_hash);

-- 8. Daily 2-Sentence Diary Reflections
CREATE TABLE IF NOT EXISTS daily_reflections (
    date_iso                TEXT PRIMARY KEY,
    reflection_text         TEXT NOT NULL,
    mood_summary            TEXT NOT NULL,
    top_entities_json       TEXT NOT NULL DEFAULT '[]',
    created_at              INTEGER NOT NULL
);

-- 9. Situational Working State Snapshots (for crash recovery)
CREATE TABLE IF NOT EXISTS situational_snapshots (
    id                      INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id              TEXT NOT NULL,
    timestamp_unix          INTEGER NOT NULL,
    topic                   TEXT NOT NULL,
    focal_entities_json     TEXT NOT NULL,
    user_emotion            TEXT NOT NULL,
    ongoing_goal            TEXT NOT NULL,
    affinity                REAL NOT NULL,
    snark                   REAL NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_snapshots_session ON situational_snapshots(session_id, timestamp_unix DESC);

-- 10. Active Anchor and Quarantine Views
CREATE VIEW IF NOT EXISTS v_active_anchors AS
SELECT 
    n.id AS node_id,
    n.name,
    n.type,
    n.anchor_category,
    e.relation,
    t.name AS target_name,
    e.weight,
    e.confidence
FROM nodes n
JOIN edges e ON n.id = e.source_id
JOIN nodes t ON e.target_id = t.id
WHERE n.is_anchor = 1 AND e.is_anchor = 1 AND e.is_active = 1;

CREATE VIEW IF NOT EXISTS v_quarantine_promotion_candidates AS
SELECT 
    id,
    name,
    type,
    first_observed_date,
    last_observed_date,
    observation_days_count
FROM nodes
WHERE provisional = 1 
  AND observation_days_count >= 2 
  AND first_observed_date != last_observed_date;
"""


def get_ddl_to_test() -> str:
    """Returns DDL from data/soul_schema.sql if present, otherwise returns CANONICAL_DDL."""
    schema_path = Path(__file__).resolve().parent.parent / "data" / "soul_schema.sql"
    if schema_path.exists():
        with open(schema_path, "r", encoding="utf-8") as f:
            content = f.read().strip()
            if content:
                return content
    return CANONICAL_DDL


def create_test_db(ddl: str = None) -> sqlite3.Connection:
    """Creates an in-memory SQLite database initialized with the DDL."""
    if ddl is None:
        ddl = get_ddl_to_test()
    conn = sqlite3.connect(":memory:")
    conn.execute("PRAGMA foreign_keys = ON;")
    conn.executescript(ddl)
    return conn


class TestE2ESchema(unittest.TestCase):
    """E2E Schema DDL, Immutability, FTS5, and Trigger Test Suite."""

    def setUp(self):
        self.conn = create_test_db()
        self.conn.row_factory = sqlite3.Row

    def tearDown(self):
        self.conn.close()

    def _sha256(self, text: str) -> str:
        return hashlib.sha256(text.encode("utf-8")).hexdigest()

    def test_schema_ddl_syntax_and_tables(self):
        """TC-ST-01: Validates table creation, indices, and view definitions."""
        cursor = self.conn.cursor()
        cursor.execute("SELECT name, type FROM sqlite_master WHERE type IN ('table', 'view');")
        rows = cursor.fetchall()
        names = {row["name"] for row in rows}

        expected_tables = {
            "events",
            "events_fts",
            "nodes",
            "edges",
            "quarantine",
            "daily_reflections",
            "situational_snapshots",
        }
        for table in expected_tables:
            self.assertIn(table, names, f"Expected table '{table}' missing from database schema")

        expected_views = {"v_active_anchors", "v_quarantine_promotion_candidates"}
        for view in expected_views:
            self.assertIn(view, names, f"Expected view '{view}' missing from database schema")

    def test_foreign_key_enforcement_and_cascade(self):
        """TC-ST-01B: Validates FK enforcement and cascade deletion on property graph."""
        cursor = self.conn.cursor()

        # Attempt to insert an edge with non-existent source and target nodes
        with self.assertRaises(sqlite3.IntegrityError, msg="Foreign key violation should reject orphaned edge"):
            cursor.execute(
                """
                INSERT INTO edges (source_id, target_id, relation, valid_from, created_at)
                VALUES (999, 888, 'FRIEND_OF', 1700000000, 1700000000);
                """
            )

        # Insert valid source and target nodes
        cursor.execute(
            """
            INSERT INTO nodes (uuid, name, type, created_at, updated_at, last_accessed_at,
                               first_observed_date, last_observed_date)
            VALUES ('u1', 'Alan', 'USER', 1700000000, 1700000000, 1700000000, '2026-10-24', '2026-10-24');
            """
        )
        source_id = cursor.lastrowid

        cursor.execute(
            """
            INSERT INTO nodes (uuid, name, type, created_at, updated_at, last_accessed_at,
                               first_observed_date, last_observed_date)
            VALUES ('u2', 'ScreenMate', 'APP', 1700000000, 1700000000, 1700000000, '2026-10-24', '2026-10-24');
            """
        )
        target_id = cursor.lastrowid

        # Insert valid edge
        cursor.execute(
            """
            INSERT INTO edges (source_id, target_id, relation, valid_from, created_at)
            VALUES (?, ?, 'USES_APP', 1700000000, 1700000000);
            """,
            (source_id, target_id),
        )
        edge_id = cursor.lastrowid
        self.assertIsNotNone(edge_id)

        # Delete target node and assert CASCADE deleted the edge
        cursor.execute("DELETE FROM nodes WHERE id = ?;", (target_id,))
        cursor.execute("SELECT COUNT(*) AS cnt FROM edges WHERE id = ?;", (edge_id,))
        self.assertEqual(cursor.fetchone()["cnt"], 0, "Edge should be cascade-deleted when target node is deleted")

    def test_immutability_trigger_on_events_update(self):
        """TC-ST-02: Ensures events ledger is append-only; UPDATE triggers RAISE(ABORT)."""
        cursor = self.conn.cursor()
        raw_text = "User: My favorite language is C++20."
        h = self._sha256(raw_text)

        cursor.execute(
            """
            INSERT INTO events (timestamp, event_type, raw_content, sha256_hash, created_at)
            VALUES (1700000000, 'USER_CHAT', ?, ?, 1700000000);
            """,
            (raw_text, h),
        )
        event_id = cursor.lastrowid

        # Attempt to tamper with historical raw event
        with self.assertRaises(sqlite3.DatabaseError, msg="UPDATE on events table must trigger ABORT"):
            cursor.execute(
                "UPDATE events SET raw_content = 'User: My favorite language is Python.' WHERE id = ?;",
                (event_id,),
            )

        # Verify content remained unchanged
        cursor.execute("SELECT raw_content FROM events WHERE id = ?;", (event_id,))
        self.assertEqual(cursor.fetchone()["raw_content"], raw_text)

    def test_immutability_trigger_on_events_delete(self):
        """TC-ST-03: Ensures events ledger cannot be deleted; DELETE triggers RAISE(ABORT)."""
        cursor = self.conn.cursor()
        raw_text = "User: Don't forget my cat's name is Whiskers."
        h = self._sha256(raw_text)

        cursor.execute(
            """
            INSERT INTO events (timestamp, event_type, raw_content, sha256_hash, created_at)
            VALUES (1700000100, 'USER_CHAT', ?, ?, 1700000100);
            """,
            (raw_text, h),
        )
        event_id = cursor.lastrowid

        # Attempt to delete historical event
        with self.assertRaises(sqlite3.DatabaseError, msg="DELETE on events table must trigger ABORT"):
            cursor.execute("DELETE FROM events WHERE id = ?;", (event_id,))

        # Verify event still exists
        cursor.execute("SELECT COUNT(*) AS cnt FROM events WHERE id = ?;", (event_id,))
        self.assertEqual(cursor.fetchone()["cnt"], 1, "Historical event must remain preserved")

    def test_fts5_fulltext_indexing_and_sync(self):
        """TC-ST-04: Validates automatic FTS5 inverted index synchronization."""
        cursor = self.conn.cursor()
        content = "My sister Lily is severely allergic to peanuts and hazelnuts."
        h = self._sha256(content)

        cursor.execute(
            """
            INSERT INTO events (timestamp, event_type, raw_content, metadata_json, sha256_hash, created_at)
            VALUES (1700000200, 'USER_CHAT', ?, '{"valence": 0.8}', ?, 1700000200);
            """,
            (content, h),
        )
        event_id = cursor.lastrowid

        # Query FTS5 for terms
        cursor.execute("SELECT rowid, raw_content FROM events_fts WHERE events_fts MATCH 'peanuts';")
        match = cursor.fetchone()
        self.assertIsNotNone(match, "FTS5 should immediately index inserted event")
        self.assertEqual(match["rowid"], event_id)
        self.assertIn("hazelnuts", match["raw_content"])

        # Query for unmatched term
        cursor.execute("SELECT rowid FROM events_fts WHERE events_fts MATCH 'astrophysics';")
        self.assertIsNone(cursor.fetchone(), "FTS5 should return zero matches for unrelated terms")

    def test_anchor_integrity_triggers_nodes(self):
        """TC-ST-05: Validates anchor invariants on nodes (decay_rate=0.0 and provisional=0)."""
        cursor = self.conn.cursor()

        # 1. Ephemeral node with decay_rate > 0 and provisional=1 succeeds
        cursor.execute(
            """
            INSERT INTO nodes (uuid, name, type, is_anchor, decay_rate, provisional,
                               created_at, updated_at, last_accessed_at, first_observed_date, last_observed_date)
            VALUES ('node_eph_1', 'Temporary Tab', 'APP', 0, 0.05, 1,
                    1700000000, 1700000000, 1700000000, '2026-10-24', '2026-10-24');
            """
        )
        eph_id = cursor.lastrowid
        self.assertIsNotNone(eph_id)

        # 2. Inserting anchor node with decay_rate != 0.0 MUST ABORT
        with self.assertRaises(sqlite3.DatabaseError, msg="Anchor node with decay_rate > 0.0 must abort"):
            cursor.execute(
                """
                INSERT INTO nodes (uuid, name, type, is_anchor, decay_rate, provisional,
                                   created_at, updated_at, last_accessed_at, first_observed_date, last_observed_date)
                VALUES ('node_bad_anchor_1', 'Peanut Allergy', 'CORE_IDENTITY', 1, 0.05, 0,
                        1700000000, 1700000000, 1700000000, '2026-10-24', '2026-10-24');
                """
            )

        # 3. Inserting anchor node with provisional != 0 MUST ABORT
        with self.assertRaises(sqlite3.DatabaseError, msg="Anchor node with provisional=1 must abort"):
            cursor.execute(
                """
                INSERT INTO nodes (uuid, name, type, is_anchor, decay_rate, provisional,
                                   created_at, updated_at, last_accessed_at, first_observed_date, last_observed_date)
                VALUES ('node_bad_anchor_2', 'Peanut Allergy', 'CORE_IDENTITY', 1, 0.0, 1,
                        1700000000, 1700000000, 1700000000, '2026-10-24', '2026-10-24');
                """
            )

        # 4. Inserting valid anchor node succeeds
        cursor.execute(
            """
            INSERT INTO nodes (uuid, name, type, is_anchor, decay_rate, provisional,
                               created_at, updated_at, last_accessed_at, first_observed_date, last_observed_date)
            VALUES ('node_good_anchor', 'Peanut Allergy', 'CORE_IDENTITY', 1, 0.0, 0,
                    1700000000, 1700000000, 1700000000, '2026-10-24', '2026-10-24');
            """
        )
        anchor_id = cursor.lastrowid
        self.assertIsNotNone(anchor_id)

        # 5. Updating valid anchor node to set decay_rate > 0 MUST ABORT
        with self.assertRaises(sqlite3.DatabaseError, msg="Updating anchor node to decay_rate > 0 must abort"):
            cursor.execute("UPDATE nodes SET decay_rate = 0.1 WHERE id = ?;", (anchor_id,))

    def test_anchor_integrity_triggers_edges(self):
        """TC-ST-06: Validates anchor invariants on edges (decay_rate=0.0 and provisional=0)."""
        cursor = self.conn.cursor()
        cursor.execute(
            """
            INSERT INTO nodes (uuid, name, type, is_anchor, decay_rate, provisional,
                               created_at, updated_at, last_accessed_at, first_observed_date, last_observed_date)
            VALUES ('n_sub', 'Sister Lily', 'PERSON', 1, 0.0, 0,
                    1700000000, 1700000000, 1700000000, '2026-10-24', '2026-10-24');
            """
        )
        src_id = cursor.lastrowid

        cursor.execute(
            """
            INSERT INTO nodes (uuid, name, type, is_anchor, decay_rate, provisional,
                               created_at, updated_at, last_accessed_at, first_observed_date, last_observed_date)
            VALUES ('n_obj', 'Peanut Allergy', 'CORE_IDENTITY', 1, 0.0, 0,
                    1700000000, 1700000000, 1700000000, '2026-10-24', '2026-10-24');
            """
        )
        tgt_id = cursor.lastrowid

        # 1. Inserting anchor edge with decay_rate > 0.0 MUST ABORT
        with self.assertRaises(sqlite3.DatabaseError, msg="Anchor edge with decay_rate > 0.0 must abort"):
            cursor.execute(
                """
                INSERT INTO edges (source_id, target_id, relation, is_anchor, decay_rate, provisional, valid_from, created_at)
                VALUES (?, ?, 'HAS_ALLERGY', 1, 0.05, 0, 1700000000, 1700000000);
                """,
                (src_id, tgt_id),
            )

        # 2. Inserting valid anchor edge succeeds
        cursor.execute(
            """
            INSERT INTO edges (source_id, target_id, relation, is_anchor, decay_rate, provisional, valid_from, created_at)
            VALUES (?, ?, 'HAS_ALLERGY', 1, 0.0, 0, 1700000000, 1700000000);
            """,
            (src_id, tgt_id),
        )
        edge_id = cursor.lastrowid
        self.assertIsNotNone(edge_id)

        # 3. View v_active_anchors contains the new anchor relation
        cursor.execute("SELECT * FROM v_active_anchors WHERE relation = 'HAS_ALLERGY';")
        active_anchor = cursor.fetchone()
        self.assertIsNotNone(active_anchor)
        self.assertEqual(active_anchor["name"], "Sister Lily")
        self.assertEqual(active_anchor["target_name"], "Peanut Allergy")

    def test_quarantine_staging_and_promotion_view(self):
        """TC-ST-07: Validates provisional quarantine staging and promotion view."""
        cursor = self.conn.cursor()

        # 1. Fact observed on only 1 calendar date (80s elapsed, across midnight)
        cursor.execute(
            """
            INSERT INTO quarantine (
                fact_hash, subject, predicate, object, confidence,
                first_seen_timestamp, last_seen_timestamp,
                observation_count, distinct_calendar_days, status
            ) VALUES (
                'hash_q1', 'Alan', 'VISITED', 'Random Website', 0.6,
                1700000000000, 1700000080000, 2, 1, 'provisional'
            );
            """
        )

        # 2. Fact observed across 2 distinct calendar days but elapsed time < 36h (e.g. midnight trick: 80s)
        cursor.execute(
            """
            INSERT INTO quarantine (
                fact_hash, subject, predicate, object, confidence,
                first_seen_timestamp, last_seen_timestamp,
                observation_count, distinct_calendar_days, status
            ) VALUES (
                'hash_q2', 'Alan', 'TALKED_TO', 'Midnight Contact', 0.7,
                1700000000000, 1700000080000, 2, 2, 'provisional'
            );
            """
        )

        # 3. Fact observed across >= 2 calendar days AND elapsed time >= 36 hours (129,600,000 ms)
        t_first = 1700000000000
        t_last = t_first + 130000000000  # > 36 hours
        cursor.execute(
            """
            INSERT INTO quarantine (
                fact_hash, subject, predicate, object, confidence,
                first_seen_timestamp, last_seen_timestamp,
                observation_count, distinct_calendar_days, status
            ) VALUES (
                'hash_q3', 'Alan', 'WORKS_WITH', 'Sarah', 0.9,
                ?, ?, 3, 2, 'provisional'
            );
            """,
            (t_first, t_last),
        )

        # Query candidate promotion view
        cursor.execute("SELECT subject, predicate, object FROM v_quarantine_promotion_candidates;")
        candidates = [(row["subject"], row["object"]) for row in cursor.fetchall()]

        self.assertNotIn(("Alan", "Random Website"), candidates, "Single-day observation cannot be promotion candidate")
        self.assertNotIn(("Alan", "Midnight Contact"), candidates, "Elapsed time < 36h cannot be promotion candidate")
        self.assertIn(("Alan", "Sarah"), candidates, "Observation across >= 2 days AND >= 36h must appear in promotion view")



if __name__ == "__main__":
    unittest.main()
