#!/usr/bin/env python3
"""
ScreenMate AI Brain & Memory System
E2E Mathematical Verification Harness
File: tests/e2e_math_test.py

Validates:
1. Modified Ebbinghaus retention curve R(t) = exp(-kappa * t / S) with kappa = ln(1 / 0.90).
2. Zero-decay anchor permanence (R(t) = 1.0 for all t, decay_rate = 0.0).
3. Spaced repetition stability growth S_{n+1} (FSRS desirable difficulty effect).
4. Anchor composite scoring function Phi(E) / A(f).
5. Explicit anchor bypass logic (C_explicit = 1 => immediate promotion).
6. 2-Day quarantine gate multi-condition evaluation.
"""

import math
import unittest
from typing import Dict, Any


# ============================================================================
# Mathematical Reference Implementations
# ============================================================================

R_REF = 0.90
KAPPA = math.log(1.0 / R_REF)  # ln(1 / 0.90) ≈ 0.1053605156578263

CATEGORY_PRIORS = {
    "CORE_IDENTITY": 0.98,
    "FAMILY": 0.95,
    "MEDICAL": 0.95,
    "CORE_PREFERENCE": 0.85,
    "PROJECT_GOAL": 0.65,
    "EPISODIC_EVENT": 0.30,
    "TRANSIENT_STATE": 0.05,
}


def calculate_retrievability(t: float, stability: float, is_anchor: bool = False, decay_rate: float = 0.05) -> float:
    """
    Computes instantaneous retrievability R(t) at elapsed time t (days).
    For anchors (is_anchor=True or decay_rate=0.0): R(t) = 1.0 identically.
    For ephemeral memories: R(t) = exp(-kappa * t / stability).
    """
    if is_anchor or decay_rate == 0.0:
        return 1.0

    if stability <= 0.0:
        raise ValueError("Stability must be strictly positive")

    if t < 0.0:
        raise ValueError("Elapsed time t cannot be negative")

    exponent = -(KAPPA * t) / stability
    return math.exp(exponent)


def calculate_stability_growth_fsrs(
    s_current: float,
    r_retrievability: float,
    delta_t_days: float,
    c1: float = 2.4,
    c2: float = 0.8,
    c3: float = 0.05,
    penalty_contradiction: float = 0.0,
) -> float:
    """
    Computes updated stability S_{n+1} upon recall or reinforcement:
    S_{n+1} = S_n * (1 + c1 * exp(-c2 * R) * (1 + c3 * delta_t) * (1 - penalty))
    """
    if s_current <= 0.0:
        raise ValueError("Current stability must be > 0")

    r_clamped = max(0.0, min(1.0, r_retrievability))
    dt_capped = min(max(0.0, delta_t_days), 365.0)
    penalty_clamped = max(0.0, min(1.0, penalty_contradiction))

    desirable_difficulty = math.exp(-c2 * r_clamped)
    spacing_bonus = 1.0 + c3 * dt_capped
    multiplier = 1.0 + c1 * desirable_difficulty * spacing_bonus * (1.0 - penalty_clamped)

    return s_current * multiplier


def calculate_stability_growth_beta(s_current: float, r_retrievability: float, beta: float = 1.0) -> float:
    """Alternative linear beta-reinforcement formula: S_{n+1} = S_n * (1 + beta * R)."""
    return s_current * (1.0 + beta * max(0.0, min(1.0, r_retrievability)))


def calculate_anchor_score(
    c_explicit: int,
    emotional_valence: float,
    semantic_centrality: float,
    category: str,
    w_exp: float = 5.0,
    w_val: float = 1.8,
    w_sem: float = 1.5,
    theta_anchor: float = 2.20,
) -> float:
    """
    Computes anchor probability Phi(E) via logistic regression:
    Phi(E) = sigma(w_exp * C_exp + w_val * |V| + w_sem * I + Prior(T) - theta)
    Deterministic override: if C_explicit == 1 => Phi(E) := 1.0.
    """
    if c_explicit == 1:
        return 1.0

    prior = CATEGORY_PRIORS.get(category, 0.30)
    abs_v = min(1.0, max(0.0, abs(emotional_valence)))
    sem_i = min(1.0, max(0.0, semantic_centrality))

    logit = (w_exp * c_explicit) + (w_val * abs_v) + (w_sem * sem_i) + prior - theta_anchor
    phi = 1.0 / (1.0 + math.exp(-logit))
    return phi


def evaluate_quarantine_promotion(
    distinct_calendar_days: int,
    elapsed_hours: float,
    confidence: float,
    is_explicit_anchor: bool = False,
    confidence_threshold: float = 0.70,
    min_elapsed_hours: float = 36.0,
) -> bool:
    """
    Evaluates whether a candidate fact is promoted to the permanent graph.
    Rule: (DistinctDays >= 2 AND elapsed_hours >= 36.0 AND confidence >= 0.70) OR is_explicit_anchor.
    """
    if is_explicit_anchor:
        return True

    return (
        distinct_calendar_days >= 2
        and elapsed_hours >= min_elapsed_hours
        and confidence >= confidence_threshold
    )


# ============================================================================
# Unit Tests
# ============================================================================

class TestE2EMathematicalFormulations(unittest.TestCase):
    """Empirical tests for memory retention, stability growth, and classification math."""

    def test_modified_ebbinghaus_retention_curve(self):
        """TC-MC-02: Validates Ebbinghaus retention curve characteristics and exact reference point."""
        s = 10.0  # Stability = 10 days

        # 1. At t = 0, R(0) must equal 1.0
        r_0 = calculate_retrievability(0.0, s)
        self.assertAlmostEqual(r_0, 1.0, places=7, msg="R(0) must be 1.0")

        # 2. At t = S, R(S) must equal R_ref (0.90) exactly
        r_s = calculate_retrievability(s, s)
        self.assertAlmostEqual(r_s, 0.90, places=6, msg="R(S) must equal reference probability 0.90")

        # 3. At t = 2S, R(2S) must equal 0.90^2 = 0.81
        r_2s = calculate_retrievability(2 * s, s)
        self.assertAlmostEqual(r_2s, 0.81, places=6, msg="R(2S) must equal 0.81")

        # 4. Half-life test: R(t_half) == 0.50
        t_half = (s * math.log(2.0)) / KAPPA
        self.assertAlmostEqual(t_half, 6.5788119 * s, places=3)
        r_half = calculate_retrievability(t_half, s)
        self.assertAlmostEqual(r_half, 0.50, places=6, msg="R(t_half) must equal 0.50")

        # 5. Pruning threshold test: tau_prune = 0.15
        t_prune = (s * math.log(1.0 / 0.15)) / KAPPA
        r_prune = calculate_retrievability(t_prune, s)
        self.assertAlmostEqual(r_prune, 0.15, places=6, msg="R(t_prune) must equal 0.15")

        # 6. Monotonicity: R(t) strictly decreases as t increases
        times = [0.0, 1.0, 5.0, 10.0, 20.0, 50.0, 100.0]
        recalls = [calculate_retrievability(t, s) for t in times]
        for i in range(len(recalls) - 1):
            self.assertGreater(recalls[i], recalls[i + 1], "Retention must be strictly decreasing over time")

        # 7. As t -> inf, R(t) -> 0.0
        r_large = calculate_retrievability(1000.0, s)
        self.assertLess(r_large, 1e-4)

    def test_zero_decay_anchor_permanence(self):
        """TC-MC-01: Validates zero-decay permanence of anchor memories over vast timescales."""
        extreme_times = [
            0.0,
            1.0,          # 1 day
            365.25,       # 1 year
            3652.5,       # 10 years
            11574.0,      # 10^9 seconds ≈ 31.7 years
            1e6,          # 1 million days
        ]
        for t in extreme_times:
            r = calculate_retrievability(t, stability=1.0, is_anchor=True)
            self.assertEqual(r, 1.0, f"Anchor memory retrievability must be exactly 1.0 at t={t}")

            # Also verify when decay_rate is explicitly 0.0
            r_zero_rate = calculate_retrievability(t, stability=1.0, is_anchor=False, decay_rate=0.0)
            self.assertEqual(r_zero_rate, 1.0, f"decay_rate=0.0 must yield R(t)=1.0 at t={t}")

    def test_stability_growth_fsrs_spaced_repetition(self):
        """TC-MC-03: Validates FSRS stability growth and Desirable Difficulty principle."""
        s0 = 5.0

        # Case A: Recall when memory is fresh (R = 0.95, immediate reinforcement)
        s1_fresh = calculate_stability_growth_fsrs(s0, r_retrievability=0.95, delta_t_days=0.5)

        # Case B: Recall when memory is difficult / nearly forgotten (R = 0.20, delayed reinforcement)
        s1_difficult = calculate_stability_growth_fsrs(s0, r_retrievability=0.20, delta_t_days=0.5)

        # Desirable difficulty: difficult recall produces significantly higher stability growth!
        growth_fresh = s1_fresh / s0
        growth_difficult = s1_difficult / s0

        self.assertGreater(
            growth_difficult,
            growth_fresh,
            "Desirable difficulty: challenging recall must yield greater stability increment than easy recall",
        )

        # Spacing effect: longer elapsed interval delta_t increases stability multiplier
        s1_short_dt = calculate_stability_growth_fsrs(s0, r_retrievability=0.70, delta_t_days=1.0)
        s1_long_dt = calculate_stability_growth_fsrs(s0, r_retrievability=0.70, delta_t_days=14.0)
        self.assertGreater(s1_long_dt, s1_short_dt, "Longer spacing delta_t must enhance stability growth")

        # Contradiction penalty: reduces stability growth
        s1_clean = calculate_stability_growth_fsrs(s0, r_retrievability=0.70, delta_t_days=3.0, penalty_contradiction=0.0)
        s1_penalized = calculate_stability_growth_fsrs(s0, r_retrievability=0.70, delta_t_days=3.0, penalty_contradiction=0.8)
        self.assertLess(s1_penalized, s1_clean, "Contradiction penalty must throttle stability growth")

    def test_anchor_scoring_function(self):
        """TC-MC-04: Validates anchor classification scoring across category priors and valence."""
        # 1. High emotional valence + core identity candidate (e.g., peanut allergy)
        score_allergy = calculate_anchor_score(
            c_explicit=0,
            emotional_valence=0.9,
            semantic_centrality=0.8,
            category="CORE_IDENTITY",
        )
        self.assertGreater(score_allergy, 0.80, "High-emotion core identity must score as high anchor candidate")

        # 2. Transient screen activity (e.g., scrolled past a webpage)
        score_transient = calculate_anchor_score(
            c_explicit=0,
            emotional_valence=0.05,
            semantic_centrality=0.1,
            category="TRANSIENT_STATE",
        )
        self.assertLess(score_transient, 0.25, "Transient state noise must receive low anchor score")

        # 3. Core preference with moderate valence (e.g. favorite IDE theme)
        score_pref = calculate_anchor_score(
            c_explicit=0,
            emotional_valence=0.4,
            semantic_centrality=0.6,
            category="CORE_PREFERENCE",
        )
        self.assertGreater(score_pref, score_transient)

    def test_explicit_directive_anchor_bypass(self):
        """TC-MC-05: Validates deterministic override when C_explicit = 1."""
        # Even with zero emotional valence and transient category, explicit pin forces Phi(E) = 1.0
        score_forced = calculate_anchor_score(
            c_explicit=1,
            emotional_valence=0.0,
            semantic_centrality=0.0,
            category="TRANSIENT_STATE",
        )
        self.assertEqual(score_forced, 1.0, "C_explicit=1 must deterministically force Phi(E) = 1.0")

        # Direct promotion bypass
        promoted = evaluate_quarantine_promotion(
            distinct_calendar_days=1,
            elapsed_hours=0.01,
            confidence=0.50,
            is_explicit_anchor=True,
        )
        self.assertTrue(promoted, "Explicit anchor directive must bypass quarantine immediately on Day 1")

    def test_quarantine_mathematical_gate(self):
        """TC-MC-06: Validates multi-condition 2-day quarantine gate."""
        # Condition 1: Same day (1 distinct day) -> Fails
        p1 = evaluate_quarantine_promotion(distinct_calendar_days=1, elapsed_hours=48.0, confidence=0.90)
        self.assertFalse(p1, "Single calendar day must not be promoted")

        # Condition 2: 2 distinct days, but elapsed time < 36h (e.g., 23:59 to 00:01) -> Fails
        p2 = evaluate_quarantine_promotion(distinct_calendar_days=2, elapsed_hours=0.1, confidence=0.90)
        self.assertFalse(p2, "Elapsed hours < 36h must not be promoted even if calendar dates differ")

        # Condition 3: 2 days, 40 hours elapsed, but low confidence (0.60 < 0.70) -> Fails
        p3 = evaluate_quarantine_promotion(distinct_calendar_days=2, elapsed_hours=40.0, confidence=0.60)
        self.assertFalse(p3, "Low extraction confidence (<0.70) must not be promoted")

        # Condition 4: Satisfies all conditions (2 days, 38 hours elapsed, confidence 0.85) -> Succeeds
        p4 = evaluate_quarantine_promotion(distinct_calendar_days=2, elapsed_hours=38.0, confidence=0.85)
        self.assertTrue(p4, "Fact satisfying all 3 criteria must be promoted")


if __name__ == "__main__":
    unittest.main()
