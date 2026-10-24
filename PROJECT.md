# Project: ScreenMate AI Companion Brain and Memory System

## Architecture
ScreenMate is a 100% local-first, privacy-preserving desktop AI companion powered by Gemma 4 E4B Q4_K_M running on an NVIDIA RTX 4060 Laptop GPU (8GB VRAM) and 16GB RAM.
The Brain and Memory System is architected across four decoupled subsystems:

```
+-----------------------------------------------------------------------------------+
|                              SCREENMATE RUNTIME                                   |
|                                                                                   |
|  +-----------------------------------------------------------------------------+  |
|  |                Cognitive Working Memory & Interaction Loop                   |  |
|  |   - Pinned Byte-Stable Persona Prefix (~1000 tok) in llama-server Slot 0    |  |
|  |   - Compact Situational Mental Model (<100 tok: topic, entities, emotion)   |  |
|  |   - Streaming Deltas over Localhost HTTP IPC (<500ms TTFT)                  |  |
|  +-----------------------------------------------------------------------------+  |
|                                       │                                           |
|                  Append Raw Turn      ▼                                           |
|  +-----------------------------------------------------------------------------+  |
|  |                     Immutable SQLite Event Ledger                           |  |
|  |   - `events` table (Append-only, BEFORE UPDATE/DELETE triggers RAISE ABORT) |  |
|  |   - `events_fts` (FTS5 full-text search with automatic sync triggers)       |  |
|  +-----------------------------------------------------------------------------+  |
|                                       │                                           |
|               Replay on Idle + AC     ▼                                           |
|  +-----------------------------------------------------------------------------+  |
|  |              Sleep Consolidation & Fact Extraction Engine                   |  |
|  |   - Off-peak execution on llama-server Slot 1 (1:2 thermal duty cycle)      |  |
|  |   - Dual Fact Classification: Permanent Anchors (decay=0.0) vs Ephemeral    |  |
|  |   - 2-Day Provisional Quarantine (staging for unverified generalizations)   |  |
|  |   - Deterministic Anchor Bypass for explicit user imperatives (Phi = 1.0)   |  |
|  |   - 2-Sentence Daily Diary Reflection synthesis                             |  |
|  +-----------------------------------------------------------------------------+  |
|                                       │                                           |
|                 Promote & Prune       ▼                                           |
|  +-----------------------------------------------------------------------------+  |
|  |                   Property Knowledge Graph (Bi-temporal)                    |  |
|  |   - `nodes` & `edges` (valid_from, valid_until, stability S, retrievability)|  |
|  |   - Modified Ebbinghaus decay R(t) = exp(-kappa*t/S)                        |  |
|  |   - Spaced repetition stability growth S_{n+1}                              |  |
|  +-----------------------------------------------------------------------------+  |
|                                       ▲                                           |
|                                       │  Abstract Interface (IBrain/IConsolidator)|
|  +-----------------------------------------------------------------------------+  |
|  |            Local/Cloud Modular Layer (Pluggable Cloud Consolidator)         |  |
|  +-----------------------------------------------------------------------------+  |
+-----------------------------------------------------------------------------------+
```

---

## Feature Inventory
Every feature identified during the Survey phase is mapped to an assigned milestone.

| # | Feature | Description | Milestone | Source |
|---|---------|-------------|-----------|--------|
| 1 | Compact Situational State | Dynamic cognitive working state (active topic, entities, emotion, goal) <100 tokens | M3 | R1, spec_miner |
| 2 | KV-Cache Slot Continuity | llama-server slot 0 pinned persona prefix (~1000 tokens) with `cache_prompt: true` | M3 | R1, survey_technical |
| 3 | Streaming Deltas Turn Protocol | Eliminates chat transcript dumping; sends only working state delta + latest turn | M3 | R1, spec_miner |
| 4 | <500ms TTFT SLA Guarantee | Mathematical proof and latency budget (typical ~75ms, worst-case ~438ms) | M3 | R1, survey_technical |
| 5 | Context Shift Resilience | Automatic context shifting (`--context-shift`) when slot 0 reaches context limit | M3 | R1, spec_miner |
| 6 | Working State Slot Sync | Structured JSON serialization and prompt injection format for working memory | M3 | R1, spec_miner |
| 7 | Permanent Anchor Classification | Fact classification with `decay_rate = 0.0`, stability $S \to \infty$, $R(t) \equiv 1.0$ | M2 | R2, survey_technical |
| 8 | Decaying Context Formulation | Modified Ebbinghaus decay $R(t) = \exp(-\kappa t / S)$ with $\kappa = \ln(1/0.90)$ | M2 | R2, survey_technical |
| 9 | Spaced Repetition Reinforcement | FSRS-style stability growth $S_{n+1} = S_n \cdot (1 + c_1 e^{-c_2 R}(1 + c_3 \Delta t))$ | M2 | R2, survey_technical |
| 10 | Anchor Scoring Function | Composite score $A(f) = \sigma(w_1 E_{\text{explicit}} + w_2 V_{\text{emotion}} + w_3 I_{\text{importance}} + w_4 P_{\text{prior}})$ | M2 | R2, spec_miner |
| 11 | Explicit Anchor Bypass | Deterministic promotion for explicit user pins/declarations ($\Phi = 1.0$) bypassing quarantine | M2 | R2, survey_technical |
| 12 | 2-Day Provisional Quarantine | Staging gate requiring observations across $\ge 2$ distinct calendar dates AND $\Delta t \ge 36\text{h}$ | M2 | R2, spec_miner |
| 13 | Conflict & Contradiction Resolution | Bi-temporal edge supersession (`valid_until = CURRENT_TIMESTAMP`) when anchors update | M2 | R2, spec_miner |
| 14 | Immutable Event Ledger DDL | SQLite `events` table with `BEFORE UPDATE/DELETE` triggers aborting modifications | M1 | R2, survey_technical |
| 15 | FTS5 External Content Sync | SQLite `events_fts` with automated `AFTER INSERT` triggers for full-text safety net | M1 | R2, survey_technical |
| 16 | Bi-temporal Property Graph DDL | SQLite `nodes` and `edges` tables with validity intervals and anchor invariants | M1 | R2, survey_technical |
| 17 | Quarantine & Reflection DDL | SQLite `quarantine` and `daily_reflections` tables with indices and integrity constraints | M1 | R2, survey_technical |
| 18 | Sleep State Machine | Discrete lifecycle: AWAKE -> IDLE_DETECTED -> REPLAY -> FACT_EXTRACT -> QUARANTINE_CHECK -> REFLECTION -> READY | M4 | R2, spec_miner |
| 19 | Daily 2-Sentence Diary Reflection | Synthesizes daily events into exactly two reflective sentences linked to graph nodes | M4 | R2, spec_miner |
| 20 | Dual-Slot VRAM Allocation Budget | Gemma 4 E4B Q4_K_M (4.67 GB committed across 2 slots), guaranteeing 3.52 GB free on 8GB GPU | M4 | R3, survey_technical |
| 21 | Thermal Duty Cycle & Throttling | 1:2 duty cycling (2s compute, 4s sleep) during consolidation, maintaining GPU <58°C, <22W | M4 | R3, survey_technical |
| 22 | Instant Consolidation Pre-emption | Pre-emption within <50ms upon user keystroke, mouse move, or AC power disconnect | M4 | R3, spec_miner |
| 23 | C++20 Abstract `IBrain` Interface | Pure virtual cognitive engine interface in `include/brain/i_brain.h` | M4 | R3, survey_codebase |
| 24 | C++20 Abstract `IConsolidator` Interface | Pure virtual consolidation interface in `include/brain/i_consolidator.h` (local/cloud swappable) | M4 | R3, survey_codebase |
| 25 | Abstract `ISoulStore` Interface | Pure virtual SQLite graph storage interface in `include/brain/i_soul_store.h` | M4 | R3, survey_codebase |
| 26 | Adversarial Roast of Naive Memory | Critique of prompt dumping, raw transcript context rot, and unvalidated fact ingestion | M5 | R4, spec_miner |
| 27 | Accidental Forgetting Defense | Architectural defense: zero-decay anchors + FTS5 immutable safety net | M5 | R4, spec_miner |
| 28 | Context Rot & Hallucination Defense | Architectural defense: <100 tok state + 2-day quarantine + pinned persona prefix | M5 | R4, spec_miner |
| 29 | Graph-State Desynchronization Defense | Architectural defense: bi-temporal edge tracking + transaction atomicity | M5 | R4, spec_miner |
| 30 | Latency Spike & GPU Thrashing Defense | Architectural defense: KV-cache slot pinning + thermal duty pacing + dedicated VRAM buffer | M5 | R4, spec_miner |

---

## Milestones

| # | Name | Scope | Dependencies | Status |
|---|------|-------|-------------|--------|
| 1 | SQLite Storage Layer & DDL | Complete SQLite schemas (`data/soul_schema.sql`), append-only immutable `events` ledger, FTS5 sync triggers, bi-temporal `nodes`/`edges`, `quarantine`, `daily_reflections`, and integrity triggers | None | DONE |
| 2 | Mathematical Fact Engine | Formal mathematical specifications: modified Ebbinghaus decay $R(t)$, zero-decay anchor invariants, FSRS stability growth $S_{n+1}$, composite scoring $A(f)$, explicit bypass logic, and 2-day quarantine math | M1 | DONE |
| 3 | Cognitive Working Memory | Situational Mental Model tokenomics (<100 tokens), llama-server dual-slot (`-np 2`) architecture, byte-stable persona prefix KV-cache reuse, streaming delta turn protocol, context-shift handling, and latency proof (<500ms TTFT) | M1, M2 | DONE |
| 4 | Consolidation & Hardware Feasibility | Sleep consolidation state machine, RTX 4060 8GB VRAM allocation budget (4.67 GB committed, 3.52 GB free), 1:2 thermal duty cycle & throttling policy, and abstract C++20 `IBrain`, `IConsolidator`, and `ISoulStore` interfaces in `include/brain/` | M1, M2, M3 | DONE |
| 5 | Adversarial Roast, Failure Modes & Blueprint | Complete adversarial roast, 8-point failure mode defense matrix, complete integration of all previous milestones into the definitive `docs/BRAIN_SPEC.md`, and passing 100% of E2E verification tests | M1, M2, M3, M4 | DONE |

---

## Interface Contracts

### `IConsolidator` ↔ `ISoulStore`
- `IConsolidator::ReplayRecentEvents(std::chrono::hours window) -> std::vector<Event>`
- `IConsolidator::ExtractCandidateFacts(const std::vector<Event>& events) -> std::vector<CandidateFact>`
- `ISoulStore::StageToQuarantine(const CandidateFact& fact) -> bool`
- `ISoulStore::PromoteAnchor(const Fact& anchor) -> bool`
- `ISoulStore::PruneDecayedEdges(float retrievability_threshold) -> size_t`

### `IBrain` ↔ `llama-server` (Slot 0)
- `POST /completion`: Payload contains `id_slot: 0`, `cache_prompt: true`, `prompt: "<situational_state>\nUser: <text>\nScreenMate:"`
- Returns: Server-Sent Events (SSE) streaming token deltas.
- SLA: Time-to-first-token < 500ms (typical ~75ms).

### `IConsolidator` ↔ `llama-server` (Slot 1)
- `POST /completion`: Payload contains `id_slot: 1`, `prompt: "<consolidation_extraction_prompt>"`
- Invoked only when `SystemPower::IsACConnected()` AND `UserActivity::IdleDuration() > 300s`.
- Pre-empted immediately (<50ms) on user input or AC disconnect.

---

## Code Layout

```
hopeful-planck/
├── data/
│   └── soul_schema.sql                 # Production SQLite DDL (Milestone 1)
├── include/
│   └── brain/
│       ├── i_brain.h                   # Abstract C++20 Brain interface (Milestone 4)
│       ├── i_consolidator.h            # Abstract C++20 Consolidator interface (Milestone 4)
│       ├── i_soul_store.h              # Abstract C++20 SoulStore interface (Milestone 4)
│       └── situational_state.h         # Situational mental model struct (<100 tokens) (Milestone 3)
├── docs/
│   └── BRAIN_SPEC.md                   # Definitive Blueprint & Specification (Milestone 5)
├── tests/
│   ├── e2e_schema_test.py              # Schema DDL & trigger validation harness (E2E Track)
│   ├── e2e_math_test.py                # Mathematical decay & stability verification (E2E Track)
│   ├── e2e_token_budget_test.py        # Situational state token budget verification (E2E Track)
│   └── e2e_failure_modes_test.py       # Failure mode mitigations automated test (E2E Track)
├── TEST_INFRA.md                       # E2E Test Suite Specification (E2E Track)
└── TEST_READY.md                       # E2E Test Suite Ready Attestation (E2E Track)
```
