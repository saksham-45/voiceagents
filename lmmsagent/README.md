# LMMS Agent

Local LMMS agent stack built around typed `AgentControl` tools, with a production-first voice layer:

- Continuous listening with wake-phrase execution gate.
- Hybrid interpreter (deterministic + heuristic + Ollama structured parser).
- Confirm-on-risk for destructive/ambiguous actions.
- Context-aware command chaining (`open slicer and import sample.wav and split into 16`).
- Structured stage traces for debugging and latency tuning.
- Persistent daemon mode (`lmms-agentd`) for runtime split and retries.

## Structure

- `integrations/lmms/AgentControl/`: integration contract and schema.
- `plugins/AgentControl/`: typed LMMS tool server implementation.
- `shared/`: deterministic discovery, planner, orchestrator, and project memory.
- `lmms-agentd/`: persistent daemon API (`run_goal`, `health`, `warmup`) with idempotency and timeout classes.
- `lmms-text-agent/`: text frontend over shared planner/orchestrator.
- `lmms-voice-agent/`: voice frontend using `whisper.cpp` transcript + same planner/orchestrator.
- `scripts/`: build/install/run helpers.
- `evals/`: fixed evaluation task taxonomy.
- `docs/MANUAL_TASK_MAP.md`: compatibility-aware transfer from LMMS 0.4.12 manual.
- `docs/MANUAL_FEATURE_COVERAGE.md`: full feature-family map from manual to agent capabilities.
- `docs/MANUAL_TOC_EXTRACTED.md`: extracted TOC index used for manual coverage.

## Core Tool Contract

All tool responses follow:

```json
{
  "ok": true,
  "result": {},
  "state_delta": {},
  "warnings": [],
  "error_code": null,
  "error_message": null
}
```

## Build + Install

```bash
./lmmsagent/scripts/build_agentcontrol.sh
./lmmsagent/scripts/install_agentcontrol.sh
```

## Run Text Agent

```bash
./lmmsagent/scripts/run_agentd.sh
# then in another shell:
./lmmsagent/scripts/run_text_agent.sh --interactive
# or single command
./lmmsagent/scripts/run_text_agent.sh "set tempo to 124"
# guided mode (confirm each step)
./lmmsagent/scripts/run_text_agent.sh --guided "compose from score sheet"
# manual capability map
./lmmsagent/scripts/run_text_agent.sh "manual map"
./lmmsagent/scripts/run_text_agent.sh "manual map automation"
```

## Run Voice Agent

```bash
./lmmsagent/scripts/run_agentd.sh
# then in another shell:
./lmmsagent/scripts/run_voice_agent.sh --audio /path/to/input.wav --whisper-model /path/to/ggml-model.bin
# or bypass ASR
./lmmsagent/scripts/run_voice_agent.sh --transcript "load instrument triple oscillator"
```

Use `--direct` on text/voice commands to bypass daemon and call the orchestrator in-process.

## Phase 0 Baseline Benchmark

```bash
python3 ./lmmsagent/scripts/benchmark_phase01.py --repeat 2
```

The script writes a report JSON under `lmmsagent/evals/reports/` with success/clarification rates and p50/p95 latencies.

## Voice Runtime Configuration

Key environment variables:

```bash
export LMMS_OLLAMA_MODEL=qwen2.5:7b-instruct
export LMMS_WAKE_REQUIRED=1
export LMMS_WAKE_PHRASES="hey lmms,ok lmms"
export LMMS_WAKE_WINDOW_MS=8000
export LMMS_HYBRID_MARGIN=0.12
export LMMS_CONFIRM_CONFIDENCE=0.74
export LMMS_CONFIRM_WINDOW_MS=9000
export LMMS_VOICE_TRACE=1
```

Per-intent rollout/canary flags are exposed as `LMMS_CAP_*` (for example `LMMS_CAP_SLICER_IMPORT=0` to disable one command family without disabling the whole pipeline).

## Notes

- Planner output is always structured (`plan`, `clarify`, or `reject`).
- Clarification is triggered for low confidence, subjective prompts, and risky operations.
- Project memory is append-only journal + preference cache under `~/.lmmsagent/memory/`.
- Beginner prompts are mapped to manual-driven playbooks with explicit compatibility cautions.

## Contract / Regression Checks

```bash
python3 ./lmmsagent/scripts/validate_voice_contracts.py
```

This validates:

- command manifest shape and intent uniqueness,
- structured LLM output schema requirements,
- golden scenario metadata consistency.
