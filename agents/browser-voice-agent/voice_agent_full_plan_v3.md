# Chromium Voice + Eye Agent
## Comprehensive Implementation Plan (Local-Only, High-Performance, Accessibility-First)

Version: 3.0
Date: 2026-03-27
Authoring mode: Implementation specification for coding AI execution
Status: Ready for build

---

## 1. Purpose and Scope

This document defines a complete, unambiguous implementation plan for a voice-controlled, click-free Chromium agent with optional eye-tracking guidance. It is written so another coding AI can execute implementation work with minimal interpretation.

Primary goal:
- Enable users (including users with disabilities) to control browser and web content hands-free via voice, with optional eye tracking for disambiguation.

Hard constraints:
- All processing MUST be on-device (local-only).
- Inference MUST use available accelerators (GPU/NPU/CPU) with deterministic fallback.
- System MUST be safe by default, accessibility-first, and opt-in.

Out of scope:
- Bypassing anti-bot systems, CAPTCHAs, biometric prompts, or legal consent flows.
- Performing irreversible sensitive actions without explicit confirmation.

---

## 2. Normative Language

The following terms are normative:
- MUST / REQUIRED: mandatory.
- SHOULD: recommended unless there is a documented reason not to.
- MAY: optional.

All requirements in this document are normative unless explicitly labeled "Informative".

---

## 3. Product Requirements

### 3.1 Functional requirements

FR-001: Voice activation
- Agent MUST support push-to-talk and wake-word modes.
- Agent MUST support wake-word disable.

FR-002: Core browser control
- Agent MUST support: open URL, back, forward, reload, new tab, close tab, switch tab, reopen closed tab.

FR-003: Page interaction
- Agent MUST support: click target, type text into field, select option, scroll, submit forms.

FR-004: Clarification flow
- Agent MUST ask concise clarifying questions when command grounding confidence is below threshold.

FR-005: Eye-tracking assisted disambiguation
- Eye tracking MUST be a ranking signal, not an automatic click trigger by default.

FR-006: Accessibility feedback
- Agent MUST provide visual and optional spoken confirmation for recognized command and selected target.

FR-007: Undo and recovery
- Agent MUST provide "undo last action" where reversible.

FR-008: User personalization
- Agent MUST support command aliases and confirmation verbosity settings.

### 3.2 Non-functional requirements

NFR-001: Local-only processing
- ASR, NLU, intent parsing, gaze estimation/fusion, and TTS MUST run locally.
- No cloud inference fallback is allowed.

NFR-002: Accelerator usage
- Runtime MUST prefer hardware acceleration when available, fallback to CPU without failure.

NFR-003: Performance
- p95 latency target for browser-level commands: <= 700 ms.
- p95 latency target for grounded page actions: <= 2000 ms.

NFR-004: Reliability
- Wrong-target execution rate in ambiguity benchmarks MUST be < 1%.
- Core task success rate MUST be >= 95% on defined benchmark set.

NFR-005: Privacy
- Audio, gaze, and command data MUST remain local unless user explicitly exports logs.

NFR-006: Safety
- Sensitive/destructive actions MUST require explicit confirmation unless user has configured trusted-context shortcuts.

---

## 4. System Architecture

## 4.1 Process model

The system consists of:
1) Browser Process
- VoiceAgentController (orchestrator)
- UI World Model cache
- Policy Engine
- Action Executor

2) Utility Process: Audio + ASR
- mic capture
- VAD
- wake-word detection
- streaming ASR decode

3) Utility Process: NLU + Planner
- intent classification
- slot/entity extraction
- workflow decomposition

4) Utility Process: Eye Tracking
- tracker input adapter(s)
- gaze smoothing and calibration confidence
- gaze-to-target probability estimator

5) Utility Process: TTS
- local speech synthesis

Rule:
- No long-running model inference on UI thread.
- All IPC MUST be bounded with backpressure.

## 4.2 Data flow

1) Audio frame arrives.
2) VAD marks speech segments.
3) ASR emits partial and final transcript events.
4) NLU generates structured command candidates.
5) World model returns candidate actionable nodes.
6) Fusion scorer ranks target nodes using transcript intent + gaze + UI priors.
7) Policy engine evaluates risk and confirmation requirement.
8) Executor performs action via safest valid layer.
9) Verifier checks post-condition.
10) Feedback module announces success/failure and recovery.

---

## 5. Local Inference Runtime (GPU/CPU mandatory)

### 5.1 Accelerator selection policy

Priority order MUST be:
1) NPU/Neural accelerator (if supported by runtime)
2) GPU backend (Metal, Vulkan, CUDA, DirectML/OpenVINO where available)
3) CPU SIMD (AVX2/AVX512/NEON)
4) CPU generic

At startup, runtime MUST produce a capability map:
- available backends
- model compatibility
- expected memory budget

### 5.2 Scheduling policy

Inference scheduler priorities:
- P0: wake-word, VAD (real-time)
- P1: streaming ASR chunk decode
- P2: intent parsing and grounding
- P3: gaze fusion updates
- P4: TTS playback generation

Load shedding rules (in order):
1) reduce beam width / decoding complexity
2) switch to smaller quantized model
3) reduce gaze fusion update frequency
4) disable non-essential verbose confirmations

The scheduler MUST NOT violate P0/P1 latency due to P3/P4 workloads.

### 5.3 Memory and thermal constraints

- Hard memory ceiling per utility process MUST be configurable.
- If thermal throttling is detected, runtime SHOULD move low-priority models to smaller variants.

---

## 6. Model Stack (Local-only)

Implementation can vary, but architecture MUST preserve interfaces.

### 6.1 ASR

Required capabilities:
- streaming partials
- punctuation optional
- confidence score per segment
- timestamps

Recommended local options:
- whisper.cpp class runtime with quantized models and optional GPU offload.

### 6.2 VAD + wake word

Required:
- low false-positive operation
- configurable sensitivity
- offline custom wake phrase support

### 6.3 NLU

Hybrid approach REQUIRED:
- deterministic grammar parser for top commands
- local semantic parser for free-form requests
- schema-constrained output

### 6.4 Eye tracking

Mode A (preferred): dedicated hardware SDK integration.
Mode B (fallback): webcam-based gaze estimation.

Important:
- Eye signal MUST be treated as probabilistic intent hint only.
- If calibration confidence is low, gaze weight MUST drop automatically.

### 6.5 TTS

- Local TTS only.
- Must support concise feedback mode and silent mode.

---

## 7. Command Ontology and Schemas

All parsed commands MUST conform to this schema.

```json
{
  "command_id": "uuid",
  "timestamp_ms": 0,
  "utterance": "string",
  "intent": "NAVIGATE|TAB_OP|CLICK_TARGET|TYPE_TEXT|SELECT_OPTION|SCROLL|WORKFLOW|SYSTEM",
  "slots": {
    "url": "string|null",
    "target_text": "string|null",
    "target_role": "button|link|textbox|combobox|menuitem|tab|other|null",
    "text": "string|null",
    "direction": "up|down|left|right|null",
    "amount": "small|medium|large|pixels|null",
    "tab_index": "integer|null",
    "scope": "browser_ui|web_content|active_dialog|omnibox|auto"
  },
  "nlu_confidence": 0.0,
  "requires_target_grounding": true,
  "risk_level": "LOW|MEDIUM|HIGH|CRITICAL"
}
```

Grounding candidate schema:

```json
{
  "node_id": "string",
  "source": "AX|DOM|BROWSER_UI",
  "name": "string",
  "role": "string",
  "bounds": {"x":0,"y":0,"w":0,"h":0},
  "is_visible": true,
  "is_enabled": true,
  "action": "default|focus|set_value|scroll|open",
  "scores": {
    "utterance": 0.0,
    "gaze": 0.0,
    "ui_prior": 0.0,
    "history": 0.0,
    "final": 0.0
  }
}
```

---

## 8. World Model and Grounding

### 8.1 World model content

The world model MUST include:
- active browser window id
- active tab id and URL
- frame tree context
- actionable nodes from browser UI and web content
- focus chain
- recent action history (N latest actions)

### 8.2 Update mechanism

- Use AX tree updates incrementally.
- Rebuild full index only on major navigation/frame reset.
- Keep per-node searchable fields: normalized name, role, aria attributes, bounding box, visibility.

### 8.3 Target grounding algorithm

Given intent + slots + gaze sample:
1) Candidate pre-filter by role and visibility.
2) Text match by normalized name and synonyms.
3) Scope filter (dialog > focused container > page > browser UI, unless intent specifies).
4) Compute final score:
   final = a*utterance + b*gaze + c*ui_prior + d*history - penalties
5) Pick top candidate if:
   - final >= AUTO_THRESHOLD
   - margin(top1 - top2) >= MARGIN_THRESHOLD
6) Else trigger clarification flow.

Default coefficients (initial):
- a=0.55, b=0.20, c=0.15, d=0.10

Dynamic adjustment:
- if gaze calibration low: b -> 0.05
- if utterance confidence low: a decreases, clarification threshold increases

---

## 9. Eye Tracking Fusion Details

### 9.1 Gaze signal pipeline

1) Acquire gaze point + confidence + timestamp.
2) Smooth with short horizon filter.
3) Detect fixation windows (100-250 ms configurable).
4) Convert gaze point to candidate nodes using spatial overlap and distance.

### 9.2 Gaze-derived target probability

For each node i:
- P_gaze(i) based on:
  - fixation overlap with node bounds
  - distance to node center
  - dwell duration
  - historical continuity

### 9.3 Ambiguity resolver policy

If two nodes have similar utterance score:
- prefer node with stronger P_gaze if confidence >= min_conf
- else ask clarifying question using concise labels

### 9.4 Anti-misfire safeguards

- Never auto-trigger click solely from gaze unless explicit "gaze click mode" is enabled.
- In gaze-click mode, require dwell + spoken confirm for HIGH/CRITICAL risk actions.

---

## 10. Action Execution Stack

Execution layers (ordered):
1) Browser APIs (preferred for browser operations)
2) Accessibility action APIs
3) DOM/CDP actions
4) Coordinate/input synthesis fallback

Rules:
- Executor MUST log selected layer and fallback reason.
- Executor MUST verify post-condition.

Post-condition examples:
- NAVIGATE: URL changed or load started.
- CLICK_TARGET: target state changed / event fired / expected dialog opened.
- TYPE_TEXT: field value matches expected text.

Retry policy:
- Max 2 retries with alternate grounding candidate.
- On failure, provide explicit error and ask next step.

---

## 11. Safety and Policy Engine

### 11.1 Risk tiers

LOW:
- scrolling, non-destructive navigation

MEDIUM:
- form edits, tab close

HIGH:
- submission actions, settings modifications

CRITICAL:
- payments, account/security changes, irreversible destructive operations

### 11.2 Confirmation policy

- LOW: optional confirm (user setting)
- MEDIUM: brief post-confirmation
- HIGH: pre-confirmation required
- CRITICAL: explicit verbal confirmation phrase required

### 11.3 Deny list and restricted contexts

Agent MUST refuse or escalate when:
- OS secure dialogs or permission prompts are involved
- anti-bot verification or CAPTCHA is detected
- action target is hidden/obscured in suspicious way

---

## 12. Accessibility UX Specification

### 12.1 Feedback channels

Required channels:
- visual command bubble (recognized utterance + interpreted action)
- target highlight outline before execution
- optional spoken confirmation

### 12.2 Modes

- Standard mode
- Low vision mode: thicker outlines, high contrast cues
- Motor support mode: reduced command verbosity and larger target confirmations
- Cognitive support mode: step-by-step prompts and slower pacing

### 12.3 Interaction principles

- No drag-only required interaction.
- Every command flow MUST have a no-pointer path.
- Clarification prompts MUST be short and deterministic.

---

## 13. Privacy, Security, and Compliance

### 13.1 Data handling

- Raw audio buffers are ring-buffered in memory and dropped after processing.
- Gaze stream is transient by default.
- Persistent logging is OFF by default and opt-in only.

### 13.2 Network controls

- Agent inference processes MUST run in network-isolated mode.
- Build/test pipeline MUST fail if outbound inference endpoints are present in config.

### 13.3 User controls

Settings MUST include:
- mic enable/disable
- eye tracker enable/disable
- wake-word enable/disable
- log retention and export controls

---

## 14. Configuration Contract

Create a single source of truth config schema.

```json
{
  "voice_agent_enabled": true,
  "local_only": true,
  "wake_word": {
    "enabled": true,
    "phrase": "hey chromium",
    "sensitivity": 0.55
  },
  "asr": {
    "model": "asr-small-q5",
    "backend_priority": ["npu","gpu","cpu_simd","cpu"],
    "max_latency_ms": 500
  },
  "nlu": {
    "grammar_enabled": true,
    "semantic_parser_enabled": true,
    "confidence_threshold": 0.70
  },
  "gaze": {
    "enabled": true,
    "provider": "auto",
    "min_confidence": 0.60,
    "weight": 0.20
  },
  "policy": {
    "require_confirmation_high": true,
    "require_confirmation_critical": true
  },
  "telemetry": {
    "local_metrics_only": true,
    "persist_logs": false
  }
}
```

---

## 15. State Machines

### 15.1 Agent lifecycle state machine

States:
- DISABLED
- IDLE
- LISTENING
- TRANSCRIBING
- PARSING
- GROUNDING
- CONFIRMING
- EXECUTING
- VERIFYING
- FEEDBACK
- ERROR_RECOVERY

Transitions MUST be explicit and logged.

### 15.2 Clarification state machine

- NeedClarification -> Ask -> WaitUserChoice -> Resolve -> Execute
- Timeout handling: if no response, return to IDLE with clear message.

---

## 16. Metrics and SLOs

Mandatory metrics:
- end_to_end_latency_ms
- asr_partial_latency_ms
- nlu_parse_latency_ms
- grounding_latency_ms
- action_execution_latency_ms
- post_condition_success_rate
- wrong_target_rate
- clarification_rate
- fallback_layer_distribution
- local_only_policy_violations

SLOs:
- command success >= 95%
- wrong-target < 1%
- crash-free sessions >= 99.5%

---

## 17. Testing Strategy

### 17.1 Unit tests

- parser intent mapping
- slot extraction edge cases
- grounding scorer math
- risk tier mapping
- configuration validation

### 17.2 Integration tests

- simulated ASR transcript to execution path
- ambiguity cases with/without gaze signal
- confirmation policies across risk tiers
- offline mode enforcement

### 17.3 End-to-end tests

Benchmark suites:
1) Browser control suite (50 tasks)
2) Form automation suite (100 tasks)
3) Ambiguity + disambiguation suite (100 cases)
4) Accessibility assistive suite (screen reader coexistence)
5) Performance suite under CPU-only and GPU-enabled modes

Pass/fail gates:
- all NFR targets met
- no cloud dependency observed
- no critical safety policy bypass

### 17.4 Red-team and abuse tests

- adversarial commands
- background speech collision
- prompt injection in page text affecting semantic parser
- deceptive UI labels and hidden targets

---

## 18. Implementation Backlog (Executable Work Packages)

WP-01: Feature flags and settings UI
- Deliverables:
  - enable flag
  - settings page toggles
  - persistence wiring
- Done when: toggles correctly control runtime components.

WP-02: Local inference runtime
- Deliverables:
  - backend capability probe
  - scheduler and model loader
  - accelerator fallback logic
- Done when: identical command path works in GPU and CPU-only environments.

WP-03: Audio pipeline
- Deliverables:
  - mic capture
  - VAD
  - wake-word
  - streaming ASR event bus
- Done when: transcript events generated with stable latency.

WP-04: NLU and command schema
- Deliverables:
  - deterministic grammar parser
  - semantic parser
  - schema validator
- Done when: parser emits valid structured command objects only.

WP-05: World model and indexing
- Deliverables:
  - AX ingestion
  - action node index
  - scope filters
- Done when: actionable node query p95 <= 30 ms in common pages.

WP-06: Gaze provider and fusion
- Deliverables:
  - provider abstraction
  - calibration confidence module
  - fusion scorer integration
- Done when: ambiguity benchmark wrong-target rate reduced vs no-gaze baseline.

WP-07: Action executor + verifier
- Deliverables:
  - layered executor
  - post-condition checks
  - retry/fallback strategy
- Done when: execution success meets SLO in benchmark suite.

WP-08: Policy engine
- Deliverables:
  - risk classification
  - confirmation prompts
  - restricted context handling
- Done when: all high/critical actions require expected confirmation path.

WP-09: Accessibility feedback UX
- Deliverables:
  - visual overlays
  - optional TTS
  - accessibility modes
- Done when: user can operate full flow without mouse/keyboard.

WP-10: Testing, telemetry, release gating
- Deliverables:
  - benchmark harness
  - metric dashboards (local)
  - CI release gates
- Done when: all SLOs and policy checks pass in CI.

---

## 19. Milestone Timeline

M1 (Weeks 1-3): Foundations
- WP-01, WP-02, WP-03 baseline
- Gate: offline streaming transcript works with local model.

M2 (Weeks 4-6): Command core
- WP-04, WP-05
- Gate: browser and simple page commands execute with verification.

M3 (Weeks 7-9): Robust execution
- WP-07, WP-08
- Gate: risk-tiered execution and recovery flows stable.

M4 (Weeks 10-12): Eye tracking + ambiguity
- WP-06 + ambiguity benchmark
- Gate: wrong-target < 1.5% pre-final tuning.

M5 (Weeks 13-15): Accessibility polish + hardening
- WP-09, WP-10
- Gate: >=95% success, <1% wrong-target, no local-only violations.

---

## 20. AI-Execution Notes (No Ambiguity Rules)

Rule A:
- Do not create alternate command schemas. Use the schema in section 7.

Rule B:
- Do not add cloud fallback paths.

Rule C:
- Every action must run through PolicyEngine before Executor.

Rule D:
- Every execution must run post-condition verification.

Rule E:
- Any ambiguous target with margin below threshold must trigger clarification.

Rule F:
- Gaze is only a ranking signal unless explicit gaze-click mode is enabled.

Rule G:
- Keep all thresholds configurable; do not hardcode except defaults.

Rule H:
- If uncertain about safety tier, escalate to higher risk tier.

---

## 21. Default Thresholds (Initial Values)

These are starting values and MUST be configurable:
- AUTO_THRESHOLD = 0.78
- MARGIN_THRESHOLD = 0.12
- NLU_CONF_MIN = 0.70
- GAZE_MIN_CONF = 0.60
- FIXATION_MIN_MS = 120
- FIXATION_MAX_MS = 250
- MAX_RETRIES = 2

---

## 22. Deliverable Artifacts

Required artifacts for implementation:
1) Architecture doc + sequence diagrams
2) Interface definitions (IPC and JSON schemas)
3) Config schema and migration doc
4) Test plans and benchmark datasets
5) Release checklist and policy compliance report

---

## 23. Definition of Done (Program Level)

The project is done only when all are true:
- Functional requirements FR-001..FR-008 pass.
- NFR-001 local-only guarantee verified in CI and runtime tests.
- Performance and reliability SLOs met on representative hardware tiers.
- Accessibility modes validated with user testing sessions.
- Security/policy red-team tests show no critical bypasses.

---

## 24. Final Notes
.
If implementation tradeoffs are required, preserve priority order:
1) Safety and accessibility
2) Local-only privacy guarantees
3) Correctness and reliability
4) Performance
5) Feature breadth

End of specification.