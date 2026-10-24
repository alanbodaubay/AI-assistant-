-- ============================================================================
-- ScreenMate AI Companion: The Soul Database Architecture & Schema
-- Target: SQLite 3.38+ with FTS5 and WAL mode enabled
-- File: data/soul_schema.sql
-- ============================================================================

-- ----------------------------------------------------------------------------
-- 0. PRAGMAS & CONFIGURATION
-- ----------------------------------------------------------------------------
PRAGMA foreign_keys = ON;
PRAGMA journal_mode = WAL;
PRAGMA synchronous = NORMAL;
PRAGMA busy_timeout = 5000;

-- ----------------------------------------------------------------------------
-- 1. IMMUTABLE RAW EVENT LEDGER
-- Stores every raw conversation turn, perception event, user action, and system event.
-- Append-only physical log.
-- ----------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS events (
    id                  INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id          TEXT NOT NULL DEFAULT 'default',
    timestamp           INTEGER NOT NULL,                     -- Epoch milliseconds UTC
    event_type          TEXT NOT NULL CHECK(event_type IN (
                            'USER_CHAT', 'COMPANION_CHAT', 'SCREEN_GLANCE',
                            'SYSTEM_STATE', 'TOOL_EXECUTION', 'USER_PIN',
                            'user', 'assistant', 'screen', 'tool', 'system'
                        )),
    raw_content         TEXT NOT NULL,                        -- Unmodified raw turn or screen payload
    metadata_json       TEXT NOT NULL DEFAULT '{}',           -- Structured JSON metadata (window_title, app, valence, etc.)
    sha256_hash         TEXT NULL UNIQUE,                     -- Content tamper-evidence & deduplication
    is_pinned           INTEGER NOT NULL DEFAULT 0 CHECK(is_pinned IN (0, 1)),
    created_at          INTEGER NOT NULL DEFAULT (strftime('%s', 'now') * 1000)
);

-- Indices for events
CREATE INDEX IF NOT EXISTS idx_events_timestamp ON events(timestamp);
CREATE INDEX IF NOT EXISTS idx_events_session_time ON events(session_id, timestamp DESC);
CREATE INDEX IF NOT EXISTS idx_events_type ON events(event_type);
CREATE INDEX IF NOT EXISTS idx_events_pinned ON events(is_pinned) WHERE is_pinned = 1;

-- ----------------------------------------------------------------------------
-- 2. FULL-TEXT SEARCH (FTS5) VIRTUAL TABLE & TRIGGERS
-- Eternal BM25 safety net for all raw interactions.
-- ----------------------------------------------------------------------------
CREATE VIRTUAL TABLE IF NOT EXISTS events_fts USING fts5 (
    raw_content,
    metadata_json,
    content='events',
    content_rowid='id'
);

-- Trigger: Synchronize inserted events immediately into FTS5
CREATE TRIGGER IF NOT EXISTS trg_events_ai AFTER INSERT ON events BEGIN
    INSERT INTO events_fts (rowid, raw_content, metadata_json)
    VALUES (new.id, new.raw_content, new.metadata_json);
END;

-- Trigger: FTS5 delete sync handler (cleanliness)
CREATE TRIGGER IF NOT EXISTS trg_events_ad AFTER DELETE ON events BEGIN
    INSERT INTO events_fts (events_fts, rowid, raw_content, metadata_json)
    VALUES ('delete', old.id, old.raw_content, old.metadata_json);
END;

-- ----------------------------------------------------------------------------
-- 3. IMMUTABILITY TRIGGERS: PREVENT UPDATE OR DELETE ON EVENTS
-- Guarantees the raw ledger is 100% append-only and cannot be tampered with.
-- ----------------------------------------------------------------------------
CREATE TRIGGER IF NOT EXISTS trg_events_prevent_update
BEFORE UPDATE ON events
BEGIN
    SELECT RAISE(ABORT, 'events table is immutable');
END;

CREATE TRIGGER IF NOT EXISTS trg_events_prevent_delete
BEFORE DELETE ON events
BEGIN
    SELECT RAISE(ABORT, 'events table is immutable');
END;

-- ----------------------------------------------------------------------------
-- 4. PROPERTY GRAPH: NODES (Entities, Concepts, Episodes)
-- ----------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS nodes (
    id                      INTEGER PRIMARY KEY AUTOINCREMENT,
    uuid                    TEXT NOT NULL UNIQUE DEFAULT (lower(hex(randomblob(16)))),
    name                    TEXT NOT NULL,                    -- Entity identifier (e.g. "ScreenMate", "Peanut Allergy")
    type                    TEXT NOT NULL CHECK(type IN (
                                'USER', 'PERSON', 'PROJECT', 'APP',
                                'INTEREST', 'HABIT', 'INSIDE_JOKE',
                                'CORE_IDENTITY', 'PREFERENCE', 'EPISODE',
                                'CONCEPT', 'ENTITY'
                            )),
    attributes_json         TEXT NOT NULL DEFAULT '{}',
    is_anchor               INTEGER NOT NULL DEFAULT 0 CHECK(is_anchor IN (0, 1)),
    anchor_category         TEXT NULL CHECK(anchor_category IN (
                                'CORE_IDENTITY', 'FAMILY', 'MEDICAL',
                                'CORE_PREFERENCE', 'PINNED', 'PROJECT_GOAL', NULL
                            )),
    decay_rate              REAL NOT NULL DEFAULT 0.05,       -- Must be 0.0 if is_anchor = 1
    stability               REAL NOT NULL DEFAULT 1.0,        -- Stability in days
    stability_days          REAL NOT NULL DEFAULT 1.0,        -- Explicit stability_days alias
    retrievability          REAL NOT NULL DEFAULT 1.0,        -- Current recall probability [0.0, 1.0]
    provisional             INTEGER NOT NULL DEFAULT 1 CHECK(provisional IN (0, 1)), -- 1 = quarantined, 0 = permanent
    first_observed_date     TEXT NULL,                        -- Date string: YYYY-MM-DD
    last_observed_date      TEXT NULL,                        -- Date string: YYYY-MM-DD
    observation_days_count  INTEGER NOT NULL DEFAULT 1 CHECK(observation_days_count >= 1),
    access_count            INTEGER NOT NULL DEFAULT 1 CHECK(access_count >= 1),
    source_event_id         INTEGER REFERENCES events(id) ON DELETE SET NULL,
    created_at              INTEGER NOT NULL DEFAULT (strftime('%s', 'now') * 1000),
    updated_at              INTEGER NOT NULL DEFAULT (strftime('%s', 'now') * 1000),
    last_accessed_at        INTEGER NOT NULL DEFAULT (strftime('%s', 'now') * 1000)
);

-- Indices for nodes
CREATE INDEX IF NOT EXISTS idx_nodes_uuid ON nodes(uuid);
CREATE INDEX IF NOT EXISTS idx_nodes_name ON nodes(name);
CREATE INDEX IF NOT EXISTS idx_nodes_type ON nodes(type);
CREATE INDEX IF NOT EXISTS idx_nodes_anchor ON nodes(is_anchor);
CREATE INDEX IF NOT EXISTS idx_nodes_provisional ON nodes(provisional);
CREATE INDEX IF NOT EXISTS idx_nodes_source_event ON nodes(source_event_id);
-- High-performance covering index for retrievability lookups (<5ms)
CREATE INDEX IF NOT EXISTS idx_nodes_retrievability_covering 
ON nodes(retrievability DESC, is_anchor, stability_days, id, name);

-- ----------------------------------------------------------------------------
-- 5. PROPERTY GRAPH: EDGES (Typed Relations with Decay & Bi-Temporal Validity)
-- ----------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS edges (
    id                      INTEGER PRIMARY KEY AUTOINCREMENT,
    source_id               INTEGER NOT NULL REFERENCES nodes(id) ON DELETE CASCADE,
    target_id               INTEGER NOT NULL REFERENCES nodes(id) ON DELETE CASCADE,
    relation                TEXT NOT NULL,                    -- e.g. 'WORKS_ON', 'ALLERGIC_TO', 'PREFERS'
    weight                  REAL NOT NULL DEFAULT 1.0 CHECK(weight >= 0.0 AND weight <= 1.0),
    confidence              REAL NOT NULL DEFAULT 1.0 CHECK(confidence >= 0.0 AND confidence <= 1.0),
    is_anchor               INTEGER NOT NULL DEFAULT 0 CHECK(is_anchor IN (0, 1)),
    decay_rate              REAL NOT NULL DEFAULT 0.05,       -- Must be 0.0 if is_anchor = 1
    stability               REAL NOT NULL DEFAULT 1.0,        -- Stability in days
    stability_days          REAL NOT NULL DEFAULT 1.0,        -- Explicit stability_days alias
    valid_from              INTEGER NOT NULL DEFAULT (strftime('%s', 'now') * 1000), -- Epoch ms
    valid_until             INTEGER DEFAULT NULL,             -- Epoch ms (NULL = active)
    is_active               INTEGER NOT NULL DEFAULT 1 CHECK(is_active IN (0, 1)),
    provisional             INTEGER NOT NULL DEFAULT 1 CHECK(provisional IN (0, 1)), -- 1 = quarantined, 0 = permanent
    source_event_id         INTEGER REFERENCES events(id) ON DELETE SET NULL,
    created_at              INTEGER NOT NULL DEFAULT (strftime('%s', 'now') * 1000)
);

-- Indices for edges
CREATE INDEX IF NOT EXISTS idx_edges_source_id ON edges(source_id);
CREATE INDEX IF NOT EXISTS idx_edges_target_id ON edges(target_id);
CREATE INDEX IF NOT EXISTS idx_edges_source_event ON edges(source_event_id);
CREATE INDEX IF NOT EXISTS idx_edges_lookup ON edges(source_id, relation, target_id);
CREATE INDEX IF NOT EXISTS idx_edges_bitemporal ON edges(source_id, relation, valid_from, valid_until);
CREATE INDEX IF NOT EXISTS idx_edges_anchor ON edges(is_anchor, decay_rate);
CREATE INDEX IF NOT EXISTS idx_edges_provisional ON edges(provisional);
-- High-performance covering partial index for active edge traversal (<5ms)
CREATE INDEX IF NOT EXISTS idx_edges_active_covering 
ON edges(source_id, target_id, relation, weight, confidence) 
WHERE valid_until IS NULL AND is_active = 1;

-- ----------------------------------------------------------------------------
-- 6. ANCHOR INTEGRITY TRIGGERS (NODES & EDGES)
-- Enforce: IF is_anchor = 1 THEN decay_rate MUST be 0.0 AND provisional MUST be 0.
-- ----------------------------------------------------------------------------

-- Trigger: nodes BEFORE INSERT
CREATE TRIGGER IF NOT EXISTS trg_nodes_anchor_integrity_insert
BEFORE INSERT ON nodes
WHEN new.is_anchor = 1
BEGIN
    SELECT CASE
        WHEN new.decay_rate != 0.0 THEN
            RAISE(ABORT, 'Anchor node must have decay_rate = 0.0')
        WHEN new.provisional != 0 THEN
            RAISE(ABORT, 'Anchor node cannot remain in provisional quarantine')
    END;
END;

-- Trigger: nodes BEFORE UPDATE
CREATE TRIGGER IF NOT EXISTS trg_nodes_anchor_integrity_update
BEFORE UPDATE ON nodes
WHEN new.is_anchor = 1
BEGIN
    SELECT CASE
        WHEN new.decay_rate != 0.0 THEN
            RAISE(ABORT, 'Anchor node must have decay_rate = 0.0')
        WHEN new.provisional != 0 THEN
            RAISE(ABORT, 'Anchor node cannot remain in provisional quarantine')
    END;
END;

-- Trigger: edges BEFORE INSERT
CREATE TRIGGER IF NOT EXISTS trg_edges_anchor_integrity_insert
BEFORE INSERT ON edges
WHEN new.is_anchor = 1
BEGIN
    SELECT CASE
        WHEN new.decay_rate != 0.0 THEN
            RAISE(ABORT, 'Anchor edge must have decay_rate = 0.0')
        WHEN new.provisional != 0 THEN
            RAISE(ABORT, 'Anchor edge cannot remain in provisional quarantine')
    END;
END;

-- Trigger: edges BEFORE UPDATE
CREATE TRIGGER IF NOT EXISTS trg_edges_anchor_integrity_update
BEFORE UPDATE ON edges
WHEN new.is_anchor = 1
BEGIN
    SELECT CASE
        WHEN new.decay_rate != 0.0 THEN
            RAISE(ABORT, 'Anchor edge must have decay_rate = 0.0')
        WHEN new.provisional != 0 THEN
            RAISE(ABORT, 'Anchor edge cannot remain in provisional quarantine')
    END;
END;

-- ----------------------------------------------------------------------------
-- 7. PROVISIONAL QUARANTINE STAGING GATE
-- 2-day staging area for candidate facts, tracking observation counts,
-- distinct calendar dates, first/last seen timestamps, and candidate status.
-- ----------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS quarantine (
    id                      INTEGER PRIMARY KEY AUTOINCREMENT,
    fact_hash               TEXT UNIQUE NOT NULL,                 -- Deduplication hash of subject-predicate-object
    subject                 TEXT NOT NULL,
    predicate               TEXT NOT NULL,
    object                  TEXT NOT NULL,
    confidence              REAL NOT NULL DEFAULT 0.5 CHECK(confidence >= 0.0 AND confidence <= 1.0),
    first_seen_timestamp    INTEGER NULL,                         -- Epoch milliseconds
    last_seen_timestamp     INTEGER NULL,                         -- Epoch milliseconds
    first_observed_at       INTEGER NULL,                         -- Explicit alias
    last_observed_at        INTEGER NULL,                         -- Explicit alias
    first_observed_date     TEXT NULL,                            -- Calendar date string: YYYY-MM-DD
    last_observed_date      TEXT NULL,                            -- Calendar date string: YYYY-MM-DD
    observation_count       INTEGER NOT NULL DEFAULT 1 CHECK(observation_count >= 1),
    distinct_calendar_days  INTEGER NOT NULL DEFAULT 1 CHECK(distinct_calendar_days >= 1),
    source_event_ids_json   TEXT NOT NULL DEFAULT '[]',           -- JSON array of supporting event IDs
    status                  TEXT NOT NULL DEFAULT 'provisional' CHECK(status IN ('provisional', 'promoted', 'expired', 'rejected')),
    candidate_status        TEXT NOT NULL DEFAULT 'provisional' CHECK(candidate_status IN ('provisional', 'promoted', 'expired', 'rejected')),
    created_at              INTEGER NOT NULL DEFAULT (strftime('%s', 'now') * 1000)
);

-- Indices for quarantine
CREATE INDEX IF NOT EXISTS idx_quarantine_hash ON quarantine(fact_hash);
CREATE INDEX IF NOT EXISTS idx_quarantine_status ON quarantine(status);
CREATE INDEX IF NOT EXISTS idx_quarantine_lifecycle 
ON quarantine(status, distinct_calendar_days, first_seen_timestamp, last_seen_timestamp);

-- Trigger: keep quarantine timestamp aliases synchronized
CREATE TRIGGER IF NOT EXISTS trg_quarantine_sync_timestamps AFTER INSERT ON quarantine
BEGIN
    UPDATE quarantine
    SET 
        first_seen_timestamp = COALESCE(new.first_seen_timestamp, new.first_observed_at, strftime('%s', 'now') * 1000),
        first_observed_at = COALESCE(new.first_observed_at, new.first_seen_timestamp, strftime('%s', 'now') * 1000),
        last_seen_timestamp = COALESCE(new.last_seen_timestamp, new.last_observed_at, strftime('%s', 'now') * 1000),
        last_observed_at = COALESCE(new.last_observed_at, new.last_seen_timestamp, strftime('%s', 'now') * 1000),
        candidate_status = COALESCE(new.candidate_status, new.status, 'provisional'),
        status = COALESCE(new.status, new.candidate_status, 'provisional')
    WHERE id = new.id;
END;

-- ----------------------------------------------------------------------------
-- 8. DAILY REFLECTIONS
-- Stores 2-sentence diary reflections, target date, sentiment score, and linked key node IDs.
-- ----------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS daily_reflections (
    id                      INTEGER PRIMARY KEY AUTOINCREMENT,
    target_date             TEXT NULL,                            -- Target date string: YYYY-MM-DD
    date_iso                TEXT NULL,                            -- Alias YYYY-MM-DD
    reflection_text         TEXT NOT NULL,                        -- Exactly 2 sentences diary reflection
    sentiment_score         REAL NOT NULL DEFAULT 0.0 CHECK(sentiment_score >= -1.0 AND sentiment_score <= 1.0),
    mood_summary            TEXT NOT NULL DEFAULT 'neutral',      -- Descriptive companion mood summary
    linked_node_ids_json    TEXT NULL,                            -- JSON array of linked key node IDs
    top_entities_json       TEXT NULL,                            -- JSON array of top entities
    created_at              INTEGER NOT NULL DEFAULT (strftime('%s', 'now') * 1000)
);

-- Indices for daily_reflections
CREATE UNIQUE INDEX IF NOT EXISTS idx_daily_reflections_target_date ON daily_reflections(target_date);
CREATE INDEX IF NOT EXISTS idx_daily_reflections_date_iso ON daily_reflections(date_iso);

-- Trigger: synchronize target_date / date_iso and linked_node_ids_json / top_entities_json
CREATE TRIGGER IF NOT EXISTS trg_daily_reflections_sync AFTER INSERT ON daily_reflections
BEGIN
    UPDATE daily_reflections
    SET 
        target_date = COALESCE(new.target_date, new.date_iso, strftime('%Y-%m-%d', 'now')),
        date_iso = COALESCE(new.date_iso, new.target_date, strftime('%Y-%m-%d', 'now')),
        linked_node_ids_json = COALESCE(new.linked_node_ids_json, new.top_entities_json, '[]'),
        top_entities_json = COALESCE(new.top_entities_json, new.linked_node_ids_json, '[]')
    WHERE id = new.id;
END;

-- ----------------------------------------------------------------------------
-- 9. SITUATIONAL WORKING STATE SNAPSHOTS
-- Stores history of working state JSON snapshots for audit and rollback.
-- ----------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS situational_snapshots (
    id                      INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id              TEXT NOT NULL DEFAULT 'default',
    timestamp_unix          INTEGER NOT NULL DEFAULT (strftime('%s', 'now') * 1000), -- Epoch milliseconds
    state_json              TEXT NOT NULL DEFAULT '{}',           -- Complete working state JSON snapshot for rollback
    topic                   TEXT NOT NULL DEFAULT '',             -- Active conversational topic
    focal_entities_json     TEXT NOT NULL DEFAULT '[]',           -- JSON array of active entity IDs/names
    user_emotion            TEXT NOT NULL DEFAULT 'neutral',      -- User emotional state
    ongoing_goal            TEXT NOT NULL DEFAULT '',             -- Immediate user goal
    affinity                REAL NOT NULL DEFAULT 0.5 CHECK(affinity >= 0.0 AND affinity <= 1.0),
    snark                   REAL NOT NULL DEFAULT 0.5 CHECK(snark >= 0.0 AND snark <= 1.0),
    created_at              INTEGER NOT NULL DEFAULT (strftime('%s', 'now') * 1000)
);

-- Indices for situational_snapshots
CREATE INDEX IF NOT EXISTS idx_snapshots_session_time 
ON situational_snapshots(session_id, timestamp_unix DESC);

-- Trigger: auto-generate state_json if only individual fields are supplied
CREATE TRIGGER IF NOT EXISTS trg_snapshots_generate_json AFTER INSERT ON situational_snapshots
WHEN new.state_json = '{}' AND (new.topic != '' OR new.user_emotion != 'neutral' OR new.ongoing_goal != '')
BEGIN
    UPDATE situational_snapshots
    SET state_json = json_object(
        'topic', new.topic,
        'focal_entities', json(new.focal_entities_json),
        'user_emotion', new.user_emotion,
        'ongoing_goal', new.ongoing_goal,
        'affinity', new.affinity,
        'snark', new.snark,
        'timestamp_unix', new.timestamp_unix
    )
    WHERE id = new.id;
END;

-- ----------------------------------------------------------------------------
-- 10. SYSTEM VIEWS FOR CONSOLIDATION & QUERYING
-- ----------------------------------------------------------------------------

-- View 1: Active permanent anchor facts (decay = 0.0, valid_until is NULL)
CREATE VIEW IF NOT EXISTS v_active_anchors AS
SELECT 
    n.id AS node_id,
    n.uuid AS node_uuid,
    n.name,
    n.type,
    n.anchor_category,
    e.id AS edge_id,
    e.relation,
    t.id AS target_node_id,
    t.name AS target_name,
    e.weight,
    e.confidence,
    e.valid_from
FROM nodes n
JOIN edges e ON n.id = e.source_id
JOIN nodes t ON e.target_id = t.id
WHERE n.is_anchor = 1 
  AND e.is_anchor = 1 
  AND e.is_active = 1 
  AND e.valid_until IS NULL;

-- View 2: Provisional facts ready for promotion from quarantine (2-Day Rule)
-- Requires observations across >= 2 distinct calendar days AND elapsed time >= 36 hours (129,600,000 ms)
CREATE VIEW IF NOT EXISTS v_quarantine_promotion_candidates AS
SELECT 
    id,
    fact_hash,
    subject,
    predicate,
    object,
    confidence,
    first_seen_timestamp,
    last_seen_timestamp,
    (last_seen_timestamp - first_seen_timestamp) / 3600000.0 AS elapsed_hours,
    observation_count,
    distinct_calendar_days,
    status
FROM quarantine
WHERE status = 'provisional' 
  AND distinct_calendar_days >= 2 
  AND (last_seen_timestamp - first_seen_timestamp) >= 129600000;

-- View 3: Complete active property graph traversal
CREATE VIEW IF NOT EXISTS v_active_graph AS
SELECT 
    e.id AS edge_id,
    s.id AS source_id,
    s.name AS source_name,
    s.type AS source_type,
    e.relation,
    t.id AS target_id,
    t.name AS target_name,
    t.type AS target_type,
    e.weight,
    e.confidence,
    e.is_anchor,
    e.decay_rate,
    e.stability_days,
    e.valid_from
FROM edges e
JOIN nodes s ON e.source_id = s.id
JOIN nodes t ON e.target_id = t.id
WHERE e.is_active = 1 
  AND e.valid_until IS NULL 
  AND e.provisional = 0;
