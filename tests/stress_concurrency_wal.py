#!/usr/bin/env python3
"""
ScreenMate AI: Concurrency & SQLite WAL Stress Harness
File: tests/stress_concurrency_wal.py

Simulates heavy real-world concurrent workloads:
- Slot 1: Heavy consolidation batch transactions (quarantine inserts, graph edge reconciliation, node updates).
- Slot 0: Rapid chat turn appends to immutable events ledger (triggering FTS5 sync triggers).
- Screen Glance Observer: Fast periodic screen glance event logging.
- Slot 0 Reader: High-frequency concurrent queries against v_active_anchors and events_fts.

Evaluates:
1. Zero SQLITE_BUSY errors when busy_timeout = 5000 and BEGIN IMMEDIATE are enforced.
2. Latency profile of Slot 0 appends during heavy Slot 1 transactions.
3. Vulnerability of deferred transactions (BEGIN DEFERRED) to deadlocks.
"""

import hashlib
import os
import sqlite3
import tempfile
import threading
import time
import sys
from pathlib import Path
ROOT_DIR = Path(__file__).resolve().parent.parent
if str(ROOT_DIR) not in sys.path:
    sys.path.insert(0, str(ROOT_DIR))

from tests.e2e_schema_test import get_ddl_to_test


def run_concurrency_stress_test(
    num_chat_turns: int = 50,
    num_glances: int = 50,
    consolidation_cycles: int = 5,
    busy_timeout_ms: int = 5000,
    use_immediate: bool = True
) -> Dict[str, Any]:
    with tempfile.NamedTemporaryFile(suffix=".db", delete=False) as tf:
        db_path = tf.name

    stats = {
        "chat_writes_succeeded": 0,
        "glance_writes_succeeded": 0,
        "consolidation_batches_succeeded": 0,
        "reads_succeeded": 0,
        "sqlite_busy_errors": 0,
        "other_errors": [],
        "max_chat_write_latency_ms": 0.0,
        "avg_chat_write_latency_ms": 0.0,
        "total_chat_write_duration_ms": 0.0,
    }

    try:
        # Initialize database with production DDL
        init_conn = sqlite3.connect(db_path)
        init_conn.executescript(get_ddl_to_test())
        init_conn.close()

        stop_event = threading.Event()
        chat_latencies = []
        lock = threading.Lock()

        # --------------------------------------------------------------------
        # Slot 1: Heavy Sleep Consolidation Worker
        # --------------------------------------------------------------------
        def slot1_worker():
            conn = sqlite3.connect(db_path, timeout=busy_timeout_ms / 1000.0)
            conn.execute("PRAGMA journal_mode = WAL;")
            conn.execute(f"PRAGMA busy_timeout = {busy_timeout_ms};")
            cursor = conn.cursor()

            try:
                for cycle in range(consolidation_cycles):
                    if stop_event.is_set():
                        break

                    tx_start = "BEGIN IMMEDIATE;" if use_immediate else "BEGIN;"
                    cursor.execute(tx_start)

                    # 1. Insert 20 quarantine candidate items
                    for i in range(20):
                        h = hashlib.sha256(f"cand_{cycle}_{i}_{time.time()}".encode()).hexdigest()
                        cursor.execute(
                            """
                            INSERT INTO quarantine (
                                fact_hash, subject, predicate, object, confidence,
                                first_seen_timestamp, last_seen_timestamp,
                                observation_count, distinct_calendar_days, status
                            ) VALUES (?, 'User', 'PREFERS', 'Dark_Mode', 0.85, 1700000000, 1700000000, 1, 1, 'provisional');
                            """,
                            (h,),
                        )

                    # 2. Reconcile graph: create nodes and bi-temporal edges
                    now_ms = int(time.time() * 1000)
                    cursor.execute(
                        """
                        INSERT INTO nodes (uuid, name, type, is_anchor, decay_rate, provisional, created_at, updated_at, last_accessed_at, first_observed_date, last_observed_date)
                        VALUES (?, ?, 'CONCEPT', 0, 0.05, 0, ?, ?, ?, '2026-10-24', '2026-10-24');
                        """,
                        (f"node_{cycle}_{now_ms}", f"Concept_{cycle}", now_ms, now_ms, now_ms),
                    )
                    node_id = cursor.lastrowid

                    # Artificially hold the transaction open for 50ms to simulate batch extraction
                    time.sleep(0.05)

                    conn.commit()
                    with lock:
                        stats["consolidation_batches_succeeded"] += 1

                    # Small pause between consolidation cycles
                    time.sleep(0.02)
            except sqlite3.OperationalError as e:
                with lock:
                    if "locked" in str(e).lower() or "busy" in str(e).lower():
                        stats["sqlite_busy_errors"] += 1
                    else:
                        stats["other_errors"].append(str(e))
            except Exception as ex:
                with lock:
                    stats["other_errors"].append(str(ex))
            finally:
                conn.close()

        # --------------------------------------------------------------------
        # Slot 0: Rapid Interactive Chat Appends (to events & events_fts)
        # --------------------------------------------------------------------
        def slot0_chat_worker():
            conn = sqlite3.connect(db_path, timeout=busy_timeout_ms / 1000.0)
            conn.execute("PRAGMA journal_mode = WAL;")
            conn.execute(f"PRAGMA busy_timeout = {busy_timeout_ms};")
            cursor = conn.cursor()

            try:
                for i in range(num_chat_turns):
                    raw = f"User conversational turn #{i} discussing C++20 coroutines in audio engine."
                    h = hashlib.sha256(raw.encode()).hexdigest()
                    now_ms = int(time.time() * 1000)

                    t0 = time.perf_counter()
                    tx_start = "BEGIN IMMEDIATE;" if use_immediate else "BEGIN;"
                    cursor.execute(tx_start)
                    cursor.execute(
                        """
                        INSERT INTO events (timestamp, event_type, raw_content, sha256_hash, created_at)
                        VALUES (?, 'USER_CHAT', ?, ?, ?);
                        """,
                        (now_ms, raw, h, now_ms),
                    )
                    conn.commit()
                    lat_ms = (time.perf_counter() - t0) * 1000.0
                    chat_latencies.append(lat_ms)

                    with lock:
                        stats["chat_writes_succeeded"] += 1

                    # Real user typing/glance interval (5-10ms bursts)
                    time.sleep(0.005)
            except sqlite3.OperationalError as e:
                with lock:
                    if "locked" in str(e).lower() or "busy" in str(e).lower():
                        stats["sqlite_busy_errors"] += 1
                    else:
                        stats["other_errors"].append(str(e))
            except Exception as ex:
                with lock:
                    stats["other_errors"].append(str(ex))
            finally:
                conn.close()

        # --------------------------------------------------------------------
        # Screen Glance Observer (Third Writer)
        # --------------------------------------------------------------------
        def glance_worker():
            conn = sqlite3.connect(db_path, timeout=busy_timeout_ms / 1000.0)
            conn.execute("PRAGMA journal_mode = WAL;")
            conn.execute(f"PRAGMA busy_timeout = {busy_timeout_ms};")
            cursor = conn.cursor()

            try:
                for i in range(num_glances):
                    raw = f"Screen Glance #{i}: Active window 'Visual Studio Code - screenmate.cpp'"
                    h = hashlib.sha256(raw.encode()).hexdigest()
                    now_ms = int(time.time() * 1000)

                    tx_start = "BEGIN IMMEDIATE;" if use_immediate else "BEGIN;"
                    cursor.execute(tx_start)
                    cursor.execute(
                        """
                        INSERT INTO events (timestamp, event_type, raw_content, sha256_hash, created_at)
                        VALUES (?, 'SCREEN_GLANCE', ?, ?, ?);
                        """,
                        (now_ms, raw, h, now_ms),
                    )
                    conn.commit()
                    with lock:
                        stats["glance_writes_succeeded"] += 1

                    time.sleep(0.007)
            except sqlite3.OperationalError as e:
                with lock:
                    if "locked" in str(e).lower() or "busy" in str(e).lower():
                        stats["sqlite_busy_errors"] += 1
                    else:
                        stats["other_errors"].append(str(e))
            except Exception as ex:
                with lock:
                    stats["other_errors"].append(str(ex))
            finally:
                conn.close()

        # --------------------------------------------------------------------
        # Concurrent Reader Worker (FTS5 + Views)
        # --------------------------------------------------------------------
        def reader_worker():
            conn = sqlite3.connect(db_path, timeout=busy_timeout_ms / 1000.0)
            conn.execute("PRAGMA journal_mode = WAL;")
            conn.execute(f"PRAGMA busy_timeout = {busy_timeout_ms};")
            cursor = conn.cursor()

            try:
                while not stop_event.is_set():
                    # Read FTS5
                    cursor.execute("SELECT COUNT(*) FROM events_fts WHERE events_fts MATCH 'coroutines';")
                    cursor.fetchone()

                    # Read active anchors view
                    cursor.execute("SELECT COUNT(*) FROM v_active_anchors;")
                    cursor.fetchone()

                    with lock:
                        stats["reads_succeeded"] += 1
                    time.sleep(0.002)
            except sqlite3.OperationalError as e:
                with lock:
                    if "locked" in str(e).lower() or "busy" in str(e).lower():
                        stats["sqlite_busy_errors"] += 1
                    else:
                        stats["other_errors"].append(str(e))
            finally:
                conn.close()

        # Launch threads
        t_slot1 = threading.Thread(target=slot1_worker)
        t_slot0 = threading.Thread(target=slot0_chat_worker)
        t_glance = threading.Thread(target=glance_worker)
        t_reader = threading.Thread(target=reader_worker)

        t_slot1.start()
        t_slot0.start()
        t_glance.start()
        t_reader.start()

        # Wait for writers
        t_slot0.join(timeout=15.0)
        t_glance.join(timeout=15.0)
        t_slot1.join(timeout=15.0)
        stop_event.set()
        t_reader.join(timeout=5.0)

        if chat_latencies:
            stats["max_chat_write_latency_ms"] = round(max(chat_latencies), 2)
            stats["avg_chat_write_latency_ms"] = round(sum(chat_latencies) / len(chat_latencies), 2)
            stats["total_chat_write_duration_ms"] = round(sum(chat_latencies), 2)

    finally:
        for ext in ["", "-wal", "-shm"]:
            p = Path(db_path + ext)
            if p.exists():
                try:
                    p.unlink()
                except OSError:
                    pass

    return stats


if __name__ == "__main__":
    print("==================================================================")
    print("RUNNING EXPERIMENT A: Production Config (BEGIN IMMEDIATE + 5000ms)")
    print("==================================================================")
    res_a = run_concurrency_stress_test(
        num_chat_turns=100,
        num_glances=100,
        consolidation_cycles=8,
        busy_timeout_ms=5000,
        use_immediate=True,
    )
    print(f"Chat writes succeeded:            {res_a['chat_writes_succeeded']}")
    print(f"Glance writes succeeded:          {res_a['glance_writes_succeeded']}")
    print(f"Consolidation batches succeeded:  {res_a['consolidation_batches_succeeded']}")
    print(f"Concurrent reads succeeded:       {res_a['reads_succeeded']}")
    print(f"SQLITE_BUSY / Lockouts:           {res_a['sqlite_busy_errors']}")
    print(f"Other errors:                     {res_a['other_errors']}")
    print(f"Average chat write latency:       {res_a['avg_chat_write_latency_ms']} ms")
    print(f"Max chat write latency:           {res_a['max_chat_write_latency_ms']} ms")
    print()

    print("==================================================================")
    print("RUNNING EXPERIMENT B: Stress Test with Low Busy Timeout (50ms)")
    print("==================================================================")
    res_b = run_concurrency_stress_test(
        num_chat_turns=50,
        num_glances=50,
        consolidation_cycles=5,
        busy_timeout_ms=50, # Adversarially short timeout
        use_immediate=True,
    )
    print(f"Chat writes succeeded:            {res_b['chat_writes_succeeded']}")
    print(f"Glance writes succeeded:          {res_b['glance_writes_succeeded']}")
    print(f"Consolidation batches succeeded:  {res_b['consolidation_batches_succeeded']}")
    print(f"SQLITE_BUSY / Lockouts:           {res_b['sqlite_busy_errors']}")
    print(f"Other errors:                     {res_b['other_errors']}")
    print(f"Max chat write latency:           {res_b['max_chat_write_latency_ms']} ms")
    print()

    print("==================================================================")
    print("RUNNING EXPERIMENT C: Pitfall Test with DEFERRED Transactions")
    print("==================================================================")
    res_c = run_concurrency_stress_test(
        num_chat_turns=50,
        num_glances=50,
        consolidation_cycles=5,
        busy_timeout_ms=5000,
        use_immediate=False, # Naive BEGIN instead of BEGIN IMMEDIATE
    )
    print(f"Chat writes succeeded:            {res_c['chat_writes_succeeded']}")
    print(f"Glance writes succeeded:          {res_c['glance_writes_succeeded']}")
    print(f"Consolidation batches succeeded:  {res_c['consolidation_batches_succeeded']}")
    print(f"SQLITE_BUSY / Lockouts:           {res_c['sqlite_busy_errors']}")
    print(f"Other errors:                     {res_c['other_errors']}")
    print(f"Max chat write latency:           {res_c['max_chat_write_latency_ms']} ms")
