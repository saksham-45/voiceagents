# Architecture

## 1) Typed LMMS Tool Server

`plugins/AgentControl/` exposes JSON tools over localhost transport.

### Query tools

- `get_project_state`
- `list_tracks`
- `get_track_details`
- `list_patterns`
- `list_instruments`
- `list_effects`
- `list_tool_windows`
- `get_selection_state`
- `find_track_by_name`
- `search_project_audio`

### Write tools

- `create_track`
- `rename_track`
- `load_instrument`
- `load_sample`
- `create_pattern`
- `add_notes`
- `add_steps`
- `set_tempo`
- `add_effect`
- `remove_effect`
- `set_effect_param` (stub, returns not implemented)
- `open_tool`
- `import_audio`
- `import_midi`
- `import_hydrogen`
- `select_track`
- `mute_track`
- `solo_track`

### Safety tools

- `create_snapshot`
- `undo_last_action`
- `rollback_to_snapshot`
- `diff_since_snapshot`

## 2) Runtime Split (Phase 1)

`lmmsagent/lmms-agentd/main.py` is a persistent daemon that owns:

- discovery refresh/index cache
- planner/orchestrator lifecycle
- retry policy by timeout class
- idempotency cache for duplicate request suppression

Protocol envelope (newline-delimited JSON over localhost, default `127.0.0.1:7781`):

- request: `op`, `request_id`, `idempotency_key`, `timeout_class`, `retries`, payload fields
- response: `ok`, `request_id`, `op`, `attempts`, `duration_ms`, `result|error`

LMMS plugin stays the executor behind `ToolClient` on `127.0.0.1:7777`.

## 3) Discovery Layer (`lmmsagent/shared/discovery.py`)

Deterministic resolution pipeline:

1. exact normalized match
2. lexical contains/prefix match
3. token overlap ranking
4. tag/type boost

Indexed asset sources:

- plugin inventory (instruments/effects/tools)
- LMMS windows/tools
- project-local audio
- Downloads and configured sample roots

## 4) Planner + Orchestrator

- Planner (`planner.py`) emits JSON-only `plan` / `clarify` / `reject`.
- Orchestrator (`orchestrator.py`) executes step-by-step with verify loop:
  1. refresh discovery + read state
  2. plan
  3. snapshot before risky steps
  4. execute typed step
  5. re-read state
  6. rollback on failure

Clarification policy:

- confidence `< 0.70`
- risky/destructive steps
- subjective prompts (e.g. texture/vibe/harder)

## 5) Phase 0 Observability Baseline

Orchestrator returns per-request telemetry:

- `request_id`
- `total_runtime_ms`
- `stage_timings_ms` (`discovery_refresh`, `planning`, `step_execution`, etc.)
- structured `trace_events` (`start/end/error` by stage)

`ToolClient` also attaches transport timing to each tool response as `_transport.latency_ms`.

Baseline benchmark script:

- `lmmsagent/scripts/benchmark_phase01.py`
- computes success/clarification rates and p50/p95 latency from reproducible command buckets.

## 6) Project Memory (`memory.py`)

Project-scoped data under `~/.lmmsagent/memory/<project_hash>/`:

- `journal.jsonl` append-only execution history
- `preferences.json` last-writer-wins preference cache

LMMS project state remains authoritative; memory is advisory.
