#!/usr/bin/env python3
"""
ScreenMate AI: Pre-emption Latency & Thermal Duty Cycle Stress Test
File: tests/stress_preemption_and_duty_cycle.py

Validates:
1. Sleep consolidation pre-emption latency (<50ms SLA) under active SQLite transaction load.
2. Pre-emption atomicity: ROLLBACK leaves no uncommitted artifacts and resets stage cleanly.
3. 1:2 duty cycle thermal pacing: Evaluates static 4s pause vs dynamic adaptive pause under variable LLM inference durations.
"""

import hashlib
import os
import sqlite3
import sys
import tempfile
import threading
import time
from pathlib import Path
from typing import List, Dict, Any

ROOT_DIR = Path(__file__).resolve().parent.parent
if str(ROOT_DIR) not in sys.path:
    sys.path.insert(0, str(ROOT_DIR))

from tests.e2e_schema_test import get_ddl_to_test


def test_sqlite_transaction_preemption_latency(num_trials: int = 50) -> Dict[str, Any]:
    """
    Measures latency to abort an active consolidation transaction and execute ROLLBACK.
    SLA requirement: < 50ms from interrupt signal to clean rollback.
    """
    with tempfile.NamedTemporaryFile(suffix=".db", delete=False) as tf:
        db_path = tf.name

    latencies_ms = []

    try:
        init_conn = sqlite3.connect(db_path)
        init_conn.executescript(get_ddl_to_test())
        init_conn.close()

        for trial in range(num_trials):
            conn = sqlite3.connect(db_path, timeout=5.0)
            conn.execute("PRAGMA journal_mode = WAL;")
            conn.execute("PRAGMA busy_timeout = 5000;")
            cursor = conn.cursor()

            # Begin transaction and stage 100 uncommitted quarantine items
            cursor.execute("BEGIN IMMEDIATE;")
            for i in range(100):
                h = hashlib.sha256(f"trial_{trial}_item_{i}".encode()).hexdigest()
                cursor.execute(
                    """
                    INSERT INTO quarantine (
                        fact_hash, subject, predicate, object, confidence,
                        first_seen_timestamp, last_seen_timestamp,
                        observation_count, distinct_calendar_days, status
                    ) VALUES (?, 'User', 'PREFERS', 'Testing', 0.8, 1700000000, 1700000000, 1, 1, 'provisional');
                    """,
                    (h,),
                )

            # Pre-emption interrupt arrives
            t_interrupt_start = time.perf_counter()
            conn.rollback() # Rollback active batch
            latency_ms = (time.perf_counter() - t_interrupt_start) * 1000.0
            latencies_ms.append(latency_ms)

            # Verify complete rollback
            cursor.execute("SELECT COUNT(*) FROM quarantine;")
            count = cursor.fetchone()[0]
            assert count == 0, f"Rollback failed: {count} uncommitted items persisted"
            conn.close()

    finally:
        for ext in ["", "-wal", "-shm"]:
            p = Path(db_path + ext)
            if p.exists():
                try:
                    p.unlink()
                except OSError:
                    pass

    return {
        "num_trials": len(latencies_ms),
        "min_latency_ms": round(min(latencies_ms), 3),
        "avg_latency_ms": round(sum(latencies_ms) / len(latencies_ms), 3),
        "max_latency_ms": round(max(latencies_ms), 3),
        "p99_latency_ms": round(sorted(latencies_ms)[int(len(latencies_ms) * 0.99)], 3),
        "all_under_50ms": all(l < 50.0 for l in latencies_ms),
    }


def test_duty_cycle_thermal_analysis():
    """
    Analyzes the 1:2 duty cycle pacing model.
    Fixed pause (4.0s) vs Adaptive pause (2 * T_compute) under variable LLM inference durations.
    """
    peak_gpu_power_w = 85.0
    idle_gpu_power_w = 12.0

    scenarios = [
        ("Nominal Spec", 2.0),
        ("Short Extraction (1 fact)", 0.8),
        ("Complex Batch (multi-turn)", 3.2),
        ("Heavy Graph Reconciliation", 4.5),
    ]

    results = []

    for name, t_comp in scenarios:
        # Static pause policy (hardcoded 4.0s)
        t_static_pause = 4.0
        static_cycle = t_comp + t_static_pause
        static_duty = t_comp / static_cycle
        static_avg_power = (static_duty * peak_gpu_power_w) + ((1.0 - static_duty) * idle_gpu_power_w)

        # Dynamic adaptive pause policy (t_pause = 2.0 * t_comp)
        t_dynamic_pause = 2.0 * t_comp
        dynamic_cycle = t_comp + t_dynamic_pause
        dynamic_duty = t_comp / dynamic_cycle # Always exactly 33.33%
        dynamic_avg_power = (dynamic_duty * peak_gpu_power_w) + ((1.0 - dynamic_duty) * idle_gpu_power_w)

        results.append({
            "scenario": name,
            "compute_time_s": t_comp,
            "static_duty_pct": round(static_duty * 100.0, 1),
            "static_power_w": round(static_avg_power, 1),
            "static_exceeds_22w": static_avg_power > 22.0,
            "dynamic_duty_pct": round(dynamic_duty * 100.0, 1),
            "dynamic_power_w": round(dynamic_avg_power, 1),
            "dynamic_exceeds_22w": dynamic_avg_power > 22.0,
        })

    return results


if __name__ == "__main__":
    print("==================================================================")
    print("TEST 1: SQLite Transaction Pre-emption Latency (<50ms SLA)")
    print("==================================================================")
    res_lat = test_sqlite_transaction_preemption_latency(num_trials=100)
    print(f"Trials executed:            {res_lat['num_trials']}")
    print(f"Min Rollback Latency:       {res_lat['min_latency_ms']} ms")
    print(f"Avg Rollback Latency:       {res_lat['avg_latency_ms']} ms")
    print(f"Max Rollback Latency:       {res_lat['max_latency_ms']} ms")
    print(f"P99 Rollback Latency:       {res_lat['p99_latency_ms']} ms")
    print(f"Strictly < 50ms SLA Met:    {res_lat['all_under_50ms']}")
    print()

    print("==================================================================")
    print("TEST 2: 1:2 Duty Cycle Thermal Pacing & Power Modeling")
    print("==================================================================")
    duty_res = test_duty_cycle_thermal_analysis()
    print(f"{'Scenario':<28} | {'T_comp':<6} | {'Static Duty':<11} | {'Static Pwr':<10} | {'Dynamic Duty':<12} | {'Dynamic Pwr':<11}")
    print("-" * 88)
    for r in duty_res:
        stat_warn = " (! >22W)" if r['static_exceeds_22w'] else ""
        print(f"{r['scenario']:<28} | {r['compute_time_s']:<6.1f} | {r['static_duty_pct']:<5.1f}%     | {r['static_power_w']:<5.1f}W{stat_warn:<8} | {r['dynamic_duty_pct']:<5.1f}%      | {r['dynamic_power_w']:<5.1f}W")
