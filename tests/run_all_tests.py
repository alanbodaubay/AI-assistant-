#!/usr/bin/env python3
"""
ScreenMate AI Brain & Memory System
Master E2E Test Suite Runner
File: tests/run_all_tests.py

Executes all 4 test suites across the 4-Tier test infrastructure:
- tests/e2e_schema_test.py        (Tier 1 & 2: SQLite DDL, Immutability, FTS5, Anchor Triggers)
- tests/e2e_math_test.py          (Tier 1 & 2: Ebbinghaus, Stability Growth, Anchor Scoring)
- tests/e2e_token_budget_test.py  (Tier 1 & 2: Situational Mental Model Token Budget <100 tok)
- tests/e2e_failure_modes_test.py (Tier 1, 2, 3 & 4: 8-Point Adversarial Failure Mode Matrix)

Outputs structured reporting with tier breakdown, execution times, and exit status.
"""

import os
import sys
import time
import unittest
from pathlib import Path

# Ensure project root is at the head of sys.path
PROJECT_ROOT = Path(__file__).resolve().parent.parent
if str(PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(PROJECT_ROOT))

from tests.e2e_schema_test import TestE2ESchema
from tests.e2e_math_test import TestE2EMathematicalFormulations
from tests.e2e_token_budget_test import TestE2ETokenBudget
from tests.e2e_failure_modes_test import TestE2EFailureModes


def run_suite_with_timing(test_case_class) -> dict:
    """Runs a single test case class and returns structured stats."""
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(test_case_class)
    runner = unittest.TextTestRunner(verbosity=2, stream=sys.stdout)
    
    start_time = time.perf_counter()
    result = runner.run(suite)
    duration = time.perf_counter() - start_time

    return {
        "name": test_case_class.__name__,
        "tests_run": result.testsRun,
        "failures": len(result.failures),
        "errors": len(result.errors),
        "passed": result.testsRun - len(result.failures) - len(result.errors),
        "duration": duration,
        "success": result.wasSuccessful(),
    }


def main():
    print("=" * 80)
    print(" ScreenMate AI Companion: E2E Dual Track Test Suite Runner")
    print(" Target: NVIDIA RTX 4060 Laptop (8GB VRAM) / SQLite WAL / Gemma 4 E4B Q4_K_M")
    print("=" * 80)
    print(f"Project Root: {PROJECT_ROOT}\n")

    test_classes = [
        ("Tier 1 & 2: SQLite Schema, Triggers & FTS5", TestE2ESchema),
        ("Tier 1 & 2: Mathematical Retention & Stability", TestE2EMathematicalFormulations),
        ("Tier 1 & 2: Working State Token Budget (<100 tok)", TestE2ETokenBudget),
        ("Tier 1, 2, 3 & 4: Adversarial Failure Mode Defenses", TestE2EFailureModes),
    ]

    suite_results = []
    overall_start = time.perf_counter()

    for description, test_cls in test_classes:
        print(f"\n--- Running: {description} ({test_cls.__name__}) ---")
        stats = run_suite_with_timing(test_cls)
        stats["description"] = description
        suite_results.append(stats)

    overall_duration = time.perf_counter() - overall_start

    # Consolidated Report Table
    print("\n" + "=" * 80)
    print(" E2E EXECUTION SUMMARY REPORT")
    print("=" * 80)
    header = f"{'Suite / Category':<48} | {'Run':<4} | {'Pass':<4} | {'Fail':<4} | {'Time (s)':<8} | {'Status'}"
    print(header)
    print("-" * 80)

    total_run = 0
    total_passed = 0
    total_failed = 0
    total_errors = 0
    all_successful = True

    for r in suite_results:
        total_run += r["tests_run"]
        total_passed += r["passed"]
        total_failed += r["failures"]
        total_errors += r["errors"]
        if not r["success"]:
            all_successful = False

        status_str = "PASS" if r["success"] else "FAIL"
        line = f"{r['description'][:46]:<48} | {r['tests_run']:<4} | {r['passed']:<4} | {r['failures']:<4} | {r['duration']:<8.3f} | {status_str}"
        print(line)

    print("-" * 80)
    summary_line = f"{'TOTAL':<48} | {total_run:<4} | {total_passed:<4} | {total_failed + total_errors:<4} | {overall_duration:<8.3f} | {'PASS' if all_successful else 'FAIL'}"
    print(summary_line)
    print("=" * 80)

    pass_rate = (total_passed / total_run * 100.0) if total_run > 0 else 0.0
    print(f"Overall Pass Rate: {pass_rate:.1f}% ({total_passed}/{total_run})")
    print(f"Total Execution Duration: {overall_duration:.3f} seconds\n")

    if all_successful:
        print(">>> ALL TESTS PASSED CLEANLY. SYSTEM READY FOR SPECIFICATION FINALIZATION. <<<")
        sys.exit(0)
    else:
        print(">>> CRITICAL: TEST FAILURES ENCOUNTERED. REVIEW TRACES ABOVE. <<<")
        sys.exit(1)


if __name__ == "__main__":
    main()
