# Autonomous Chromium agent (post-hackathon master plan)

Local-first, Chromium-only, planner runs on **Ollama** on your machine. This document is the roadmap. Each phase ends with something you can **run and review** before the next phase starts.

## North star

**Observe, plan, act, verify** in a loop until the goal is satisfied or a safety limit trips. The model never runs arbitrary JS. It only emits **structured tool calls** that the extension validates and executes.

## Design choices (locked for v1)

| Topic | Choice |
|-------|--------|
| Inference | **Ollama** at `127.0.0.1:11434` |
| Machine | **M3 Air, 16 GB**: default planner model **8B class, Q4** (you pick exact tag in Ollama) |
| Perception | Start with **pruned interactive DOM** (visible controls + short text). Add **AX tree** and optional **vision** later. |
| Grounding | **Stable indices** `e_0` … `e_N` over a deterministic query order (same order for snapshot and click). |
| Safety | **Max steps**, **timeouts**, **kill switch**, **confirm** for sensitive actions (login, pay, post) in a later phase. |

## Phase 0: Repo and contracts (done when merged)

- [x] This plan file lives in `chromium-voice-agent/`.
- [x] **Schemas** (in `autonomous_agent.js` prompts + this doc): observation JSON, tool JSON, run status.

## Phase 1: One-step planner (shipped, you review next)

**Goal:** From a **user goal string** + **active tab snapshot**, call Ollama once and return a **single valid tool JSON** (no multi-step autonomy yet).

**Deliverables:**

- [x] `autonomous_agent.js` loaded by `importScripts` from `background.js`.
- [x] Snapshot: URL, title, up to **80** visible interactive nodes, ids `e_0` … .
- [x] Message API:
  - `AUTONOMOUS_PLAN_STEP` `{ goal, history?, tabId? }` → `{ ok, snapshot?, plan?, error?, model? }`
  - `AUTONOMOUS_RUN_TOOL` `{ tool, args, tabId? }` → executes **navigate**, **scroll**, **wait**, **click** (by `e_N` index), **done** / **none**
  - `AUTONOMOUS_GET_MODEL` → `{ model }`
- [x] Popup **Autonomous agent (beta)** block: Plan 1 step / Run that step.

**Review checkpoint:** Open a normal `https` page, run **Plan 1 step**, then **Run that step** if the tool is not `done` / `none`. Set `AUTONOMOUS_MODEL` in `autonomous_agent.js` to match `ollama list`.

## Phase 2: Multi-step runner

**Goal:** **Loop**: snapshot → plan → execute → verify → repeat until `done` or limits.

**Deliverables:**

- Internal **run state**: `goal`, `stepCount`, `maxSteps`, `history[]`, `lastObservation`, `lastError`.
- **Verification** hooks: URL changed, element count changed, text appears (cheap checks before asking the model again).
- **Pause / resume / cancel** messages and popup indicators.

**Review checkpoint:** Short goals (for example: open a site, scroll once) complete without manual messages per step.

## Phase 3: Better perception

**Goal:** Reduce wrong clicks and missed controls.

**Deliverables:**

- Merge or replace snapshot with **accessibility tree** fragments where available (`chrome.debugger` or scripted AX, depending on what you allow).
- **De-duplication** and **landmarks** (main, nav, search role).
- **Diff** observations (what changed after last action) to save tokens.

**Review checkpoint:** Fewer bad `click` targets on complex SPAs (within reason).

## Phase 4: Policy and risk

**Goal:** Autonomy without scary surprises.

**Deliverables:**

- Tool allowlist per **host** / **risk tier**.
- **Confirm** step for: navigation to external domains, anything that looks like auth/checkout/post.
- **Secrets**: never send password fields’ values to the model; redact in snapshot.

**Review checkpoint:** You can define “safe sites” and demo a blocked or confirm-gated action.

## Phase 5: Voice and UX

**Goal:** Natural entry: “do X then Y” from speech.

**Deliverables:**

- Parse **multi-intent** utterances into a **task object** (regex + small LLM pass).
- Popup: **task queue**, **current step**, **last model reason**, **stop** button.
- Optional: **JSON repair** pass when the model returns invalid tool JSON (same local model, tiny prompt).

## Phase 6: Optional vision (local)

**Goal:** Handle canvas-heavy or DOM-liar UIs.

**Deliverables:**

- Screenshot capture + **local** vision model via Ollama (if you install a vision tag).
- **Rerank** click candidates using image + list of `e_N` labels.

**Review checkpoint:** One known-hard UI (for example a custom player) improves measurably.

## Model configuration

- Classifier (existing `callLLM` in `background.js`) can stay on a **small** model for latency.
- Autonomous planner should use a **stronger** local model you can run on 16 GB (for example **8B**). Set `AUTONOMOUS_MODEL` in `autonomous_agent.js` to match `ollama list`.

## Files (living map)

| File | Role |
|------|------|
| `background.js` | Service worker, speech, classic `execute()`, wires autonomous messages. |
| `autonomous_agent.js` | Snapshot, Ollama planner, tool execution helpers for autonomous path. |
| `AUTONOMOUS_AGENT_PLAN.md` | This roadmap. |
| `popup.html` / `popup.js` | Later: autonomous controls; Phase 1 can be driven via messages only. |

## Troubleshooting: `ollama HTTP 404`

Ollama returns **404** when the requested **model name** is missing. The string must match **`ollama list`** exactly.

- **Fix:** `ollama pull <tag>` or change **`AUTONOMOUS_MODEL`** in `autonomous_agent.js` to a tag you already have.
- Default planner model is **`qwen2.5:3b`** (same family as voice fallback in `background.js`). Upgrade the constant after `ollama pull llama3.1:8b` (or similar).

## Troubleshooting: `ollama HTTP 403`

The extension is not broken. Ollama rejects browser/extension origins unless you allow them.

- **Fix:** quit the Ollama app, then run `OLLAMA_ORIGINS=chrome-extension://* ollama serve` (or your exact id from `chrome://extensions`, e.g. `chrome-extension://abcd...`).
- Details: root **README.md** section **Ollama HTTP 403 from the extension**.

## Open risks (track explicitly)

- **Order drift:** DOM changes between snapshot and click; mitigated with short waits and re-snapshot each step (Phase 2).
- **Token size:** Large pages; mitigated with pruning, diffing, smaller model context discipline.
- **Ollama JSON:** Invalid JSON; mitigated with extract + repair + retry limit.

---

When you finish reviewing a phase, say **“phase N done, go to N+1”** and we implement the next chunk in the same style.
