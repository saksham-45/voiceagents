Website: https://anubhav-gupta-software.github.io/voiceagents/
# VoiceAgents

VoiceAgents is a dual-agent accessibility project:

- `chromium-voice-agent/` for voice-first web navigation in Chromium
- `lmmsagent/` for voice/text control of LMMS through an in-app `AgentControl` plugin

The practical goal is simple: reduce the operational burden of complex software by turning spoken intent into safe, actionable steps.

## Why this approach

Both projects use a layered command strategy:

1. deterministic commands for speed and reliability on known actions  
2. fuzzy normalization for common speech-to-text errors and phrasing variation  
3. LLM fallback only when needed, with guardrails, so unrelated speech does not trigger destructive actions

This design is intentional:

- deterministic paths keep common commands fast and predictable
- fallback intelligence improves real-world usability when transcription is imperfect
- safety gates preserve trust by refusing unrelated or low-confidence commands

## Accessibility impact

These agents are built to support users who may face barriers with mouse-heavy, menu-dense software, including:

- people with motor/physical disabilities who benefit from reduced fine-pointer demands
- people with learning disabilities or cognitive load sensitivity who benefit from intent-level commands
- beginners who know *what* they want to do but not *where* to click

The objective is not to replace UI knowledge; it is to lower entry cost, reduce fatigue, and make advanced tools more reachable.

## Why Chromium voice control is effective

Web workflows are full of repetitive mechanics: tab switching, scrolling, opening tools, confirming dialogs, and navigating deep page layouts.  
`chromium-voice-agent/` targets these mechanics directly and allows users to operate the browser by intent rather than pointer precision.

For users with disabilities, this is especially valuable because it:

- reduces repetitive cursor travel and click strain
- shortens multi-step UI paths into one spoken action
- keeps interaction in a single modality when context switching is costly

## Why LMMS voice control matters

Digital Audio Workstations are powerful but highly complex. LMMS has many windows, tracks, editors, and plugin workflows that can overwhelm first-time users.

`lmmsagent/` focuses on that exact problem:

- opening and focusing the right tool windows
- creating tracks and patterns with direct commands
- importing files and controlling common slicer workflows
- normalizing noisy spoken commands into executable LMMS actions

For beginners, this turns DAW navigation from “discover hidden UI pathways” into “state musical intent and iterate.”  
For accessibility users, it reduces the interaction complexity of dense production interfaces.

## Project layout

### `chromium-voice-agent/`

Browser automation prototype for voice-driven web control.

#### Ollama HTTP 403 from the extension

Chromium sends `Origin: chrome-extension://...` on `fetch()`. Ollama denies that by default, so you see **HTTP 403** for `/api/generate` and `/api/tags`.

1. Quit Ollama from the menu bar (macOS) so the background server stops.
2. Start it from Terminal with extensions allowed:

```bash
OLLAMA_ORIGINS=chrome-extension://* ollama serve
```

If your Ollama version does not accept that wildcard, use your real extension id from `chrome://extensions` (Developer mode on):

```bash
OLLAMA_ORIGINS=chrome-extension://abcdefghijklmnopqrstuvwxyz123456 ollama serve
```

Leave that Terminal window open, or set the same variable in a LaunchAgent / shell profile so it persists.

#### Ollama HTTP 404 on Plan step

Ollama returns **404** when the **`model`** string in the request is not installed. Tags must match **`ollama list`** exactly (for example `qwen2.5:3b`, `llama3.1:8b`, `llama3.2:latest`).

1. Run `ollama list` and pick a name.
2. Set **`AUTONOMOUS_MODEL`** in `chromium-voice-agent/autonomous_agent.js` to that exact string, **or** run `ollama pull <name>` for the model you want.
3. Reload the extension.

The autonomous planner defaults to **`qwen2.5:3b`** so it matches the voice LLM in `background.js`. For better multi-step planning on a 16GB Mac, pull something like **`llama3.1:8b`** and set **`AUTONOMOUS_MODEL`** accordingly.

Key files:

- `chromium-voice-agent/manifest.json`
- `chromium-voice-agent/background.js`
- `chromium-voice-agent/autonomous_agent.js` (local Ollama planner + page snapshot, Phase 1)
- `chromium-voice-agent/AUTONOMOUS_AGENT_PLAN.md` (roadmap for full autonomous loop)
- `chromium-voice-agent/speech.js`
- `chromium-voice-agent/popup.html`
- `chromium-voice-agent/popup.js`

### `lmmsagent/`

LMMS automation project for controlling LMMS through a local plugin boundary.

Key directories:

- `lmmsagent/integrations/lmms/AgentControl/` - LMMS plugin source
- `lmmsagent/integrations/lmms/patches/` - minimal LMMS host patch set
- `lmmsagent/lmms-text-agent/` - local text command client
- `lmmsagent/lmms-voice-agent/` - local voice bridge
- `lmmsagent/shared/` - shared LMMS socket client and command normalization
- `lmmsagent/scripts/` - install and build scripts for an external LMMS checkout
- `lmmsagent/docs/` - architecture, command map, and demo notes
- `lmmsagent/demo/` - smoke-test commands

## Intended use

- use `chromium-voice-agent/` for browser-side voice accessibility and automation experiments
- use `lmmsagent/` for accessible LMMS control, beginner onboarding, and workflow acceleration
