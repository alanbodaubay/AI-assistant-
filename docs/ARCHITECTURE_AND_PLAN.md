# ScreenMate AI: Full Technical Architecture & Implementation Plan

Compiled: 2026-10-24
Status: Plan finalized and aligned; ready for Milestone 1 execution.

---

## 1. Executive Summary

ScreenMate AI is an autonomous, visible, and evolving personal AI companion for Windows that:
1. Lives on your desktop as an animated, bioluminescent **"Blob of Light"** rendered directly to a Win32 layered window (`UpdateLayeredWindow` with per-pixel alpha), consuming near 0% CPU at idle.
2. Runs **100% locally and privately** on your laptop using `llama-server` (llama.cpp) with an ~8GB VRAM RTX 4060 budget.
3. Decouples the **"Soul" (Memory & Persona)** from model weights using a local SQLite Property Graph + FTS5 event log, allowing model upgrades without memory loss.
4. Uses a **Two-Slot Latency Architecture** (`-np 2`): Slot 0 for real-time chat with `cache_prompt: true` (yielding ~0ms prompt evaluation), and Slot 1 for idle memory consolidation.
5. Employs a **2-Day Provisional Quarantine** to guarantee that small local models (4B-8B) cannot pollute long-term memory with hallucinations.
6. Implements an **Anti-Clippy Gatekeeper** with settle timers (3–5s), 64-bit dHash perceptual filtering, token bucket pacing, and strict privacy blacklists.
7. Provides an instant **`Ctrl + Space` Comic Speech Bubble** with Undertale-style typing blips and text input.

---

## 2. Hardware Constraints & Thermal Strategy

- **GPU**: NVIDIA GeForce RTX 4060 Laptop GPU (8 GB GDDR6 VRAM)
- **CPU**: AMD Ryzen 5 7640HS (6 cores, 12 threads)
- **RAM**: 16 GB DDR5
- **OS**: Windows 11 (64-bit)

### VRAM & Thermal Budget
- Model: **Gemma 4 E4B Q4_K_M** (~4.2 GB VRAM)
- Multimodal Projector (`mmproj`): ~0.8 GB VRAM
- KV Cache: Split across 2 slots of 4,096 tokens each (total 8,192 tokens) using FP16 or `q8_0` KV cache quantization (~0.5–0.8 GB).
- Total sidecar VRAM footprint: **~5.5 GB maximum**, leaving **~2.5–3.0 GB of VRAM free** for Windows DWM and the user's regular apps and games.
- Duty Cycle: Background memory extraction is strictly gated by `ISystemState` to run **only** when AC power is connected, user is idle > 5 minutes, and GPU utilization is low.

---

## 3. Detailed Component Architecture

### 3.1 Win32 Layered Window UI & Procedural Blob Renderer
- Pure Win32 API without heavy frameworks (Electron, Qt, or Direct2D).
- Uses two borderless Win32 layered windows:
  1. `BlobWindow`: 128x128 pixel transparent window housing the living bioluminescent organic orb.
  2. `BubbleWindow`: Dynamic comic speech bubble with pointer directed at the blob, animated text streaming, and auto-focusing input box when summoned.
- Window styles:
  ```cpp
  WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE
  ```
- Screen capture immunity: Calls `SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE)` on both windows so ScreenMate never takes screenshots of itself (preventing infinite self-referential loops).
- Procedural Renderer:
  - Generates a 32-bit premultiplied BGRA buffer with soft radial gradient falloff and harmonic sine-wave breathing.
  - Idle loop throttled to ~10 FPS when static to ensure CPU usage remains < 0.2%.

### 3.2 Local LLM Sidecar Integration (`llama-server`)
- Runs as a localhost HTTP service on `http://127.0.0.1:8080`.
- Server launch configuration:
  ```bash
  llama-server.exe -m models/gemma-4-E4B-it-Q4_K_M.gguf --mmproj models/gemma-4-E4B-mmproj.gguf --ctx-size 8192 -np 2 -ngl 99 --port 8080
  ```
- Two-Slot Latency Architecture:
  - **Slot 0 (`id_slot: 0`)**: Dedicated exclusively to real-time user dialogue and quick screen glances.
    - Uses `cache_prompt: true`.
    - Starts every request with a byte-stable ~1,000-token `persona_prefix` (core traits, formatting instructions, few-shot voice anchors). Because it never changes, prompt evaluation time drops to almost **0ms**, giving instant responses!
  - **Slot 1 (`id_slot: 1`)**: Reserved for background memory extraction.
    - Runs only when: AC power is connected, user is idle > 5 minutes, no fullscreen 3D app is active, and GPU load is low.
    - Never evicts Slot 0's cached prompt!

### 3.3 Active Screen Perception & Gatekeeper
- Capture: Desktop Duplication / Win32 `BitBlt` targeting the active foreground window (`GetForegroundWindow`).
- Privacy Gate: Instant drop list for window titles matching `*Password*`, `*Bitwarden*`, `*1Password*`, `*KeePass*`, `*Bank*`, `*Incognito*`, `*Private Browsing*`. If matched, capture is completely skipped and no logs are saved.
- Annoyance Gatekeeper (Anti-Clippy):
  - **Settle Timer**: Must observe 3–5 seconds of window stability before looking (avoids firing during fast scrolling or video playback).
  - **dHash Filter**: Computes 64-bit perceptual hash difference on the active window; if screen changed by < 10%, skips inference to save GPU/power.
  - **Token Bucket**: At most 1 proactive remark every 3–5 minutes. If an utterance is ignored by the user, the cooldown doubles automatically.

### 3.4 The SQLite Graph Brain ("The Soul")
- Schema (`data/soul.db`):
  - `events`: Append-only log of all chat turns and screen descriptions with SQLite `FTS5` full-text search. Immutable source of truth.
  - `nodes`: `id`, `name`, `type` (`USER`, `PROJECT`, `APP`, `INTEREST`, `HABIT`, `INSIDE_JOKE`), `attributes_json`, `created_at`, `access_count`.
  - `edges`: `source_id`, `target_id`, `relation` (`WORKS_ON`, `DISLIKES`, `PREFERS`, `MENTIONED`), `weight`, `confidence`, `valid_from`, `valid_until`.
  - `quarantine`: New facts extracted by the local model start with `provisional = 1`. A fact is only promoted to the permanent knowledge graph if confirmed across **2+ separate days**, completely eliminating hallucinations from the small local model!
- Persona & Evolution:
  - `persona_core.md`: Immutable foundation defining the witty, banter-friendly friend personality.
  - `mood_state.json`: Sliders for `affinity` (0.0 to 1.0, increases with interaction), `snark` (playfulness meter), and `energy`.
  - `voice_anchors.json`: Rolling few-shot prompt bank capturing the best past witty interactions.

---

## 4. Phased Implementation Roadmap

### Phase 1: Build Environment & The Blob of Light
- Create `CMakeLists.txt` configured for Clang / MinGW.
- Vendor `sqlite3`, `httplib.h`, `json.hpp`, and `miniaudio.h` into `third_party/`.
- Implement `bgra_buffer` and `blob_renderer` (procedural bioluminescent particle & glow engine).
- Implement `blob_window` with Win32 `UpdateLayeredWindow`, per-pixel alpha, and `WDA_EXCLUDEFROMCAPTURE`.
- Verify the blob floats smoothly on the desktop, drags with mouse, and animates with near 0% CPU.

### Phase 2: Speech Bubble, Hotkey & Local LLM Client
- Implement `bubble_window` (comic speech bubble, auto-expanding text typing, input bar).
- Register global hotkey `Ctrl + Space` and blob click handler to summon/dismiss bubble.
- Implement `llama_client` communicating with `llama-server` over HTTP `/v1/chat/completions`.
- Implement `prompt_builder` with byte-stable cached persona prefix.
- Verify hotkey summons the bubble, typing a prompt streams back witty text from the local model.

### Phase 3: The SQLite Graph Brain ("The Soul")
- Implement `soul_db` schema in SQLite:
  - FTS5 `events` table for conversation and screen memories.
  - `nodes` and `edges` tables for the knowledge graph.
  - `quarantine` table with 2-day confirmation rule.
- Wire turn logging and active context injection into `prompt_builder`.
- Implement dynamic affinity and snark progression in `mood_state.json`.
- Unit test graph traversals and quarantine promotions (`tests/test_soul_db.cpp`).

### Phase 4: Screen Perception & The Gatekeeper
- Implement `screen_capture` for the active foreground window.
- Implement privacy gate (drops password managers, banking, incognito).
- Implement `gatekeeper` (settle timer, 64-bit dHash filter, 3–5 min token bucket).
- Verify companion observes window context on demand or during idle periods without spamming.

### Phase 5: Core Tools & Extension Bridge
- Implement `app_launcher` for opening apps by name or path.
- Implement `notes_vault` for creating and querying local markdown notes and reminders.
- Add hook to connect to `epic-pascal`'s Windows Controller for future UI automation.
