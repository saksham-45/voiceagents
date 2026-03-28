# VoiceAgents

Hackathon repo for local voice and text agents that control LMMS through the `AgentControl` plugin.

## What is in this repo

- `integrations/lmms/AgentControl/` - the LMMS plugin source.
- `integrations/lmms/patches/` - minimal upstream LMMS patches required to host the plugin cleanly.
- `agents/lmms-text-agent/` - a local text client that sends commands into LMMS.
- `agents/lmms-voice-agent/` - a local voice bridge with optional `whisper.cpp` transcription.
- `agents/browser-voice-agent/` - the Chromium voice-control prototype preserved under the agent layout.
- `agents/shared/` - shared LMMS transport and lightweight command normalization.
- `scripts/` - scripts to install the plugin into an LMMS checkout and build it.
- `docs/` - architecture, command map, and demo notes.
- `demo/` - quick demo prompts and test commands.

## Quick start

1. Clone LMMS separately.
2. Run `scripts/install-agentcontrol.sh /path/to/lmms`.
3. Run `scripts/build-lmms.sh /path/to/lmms /path/to/lmms/build`.
4. Launch the built LMMS binary and open `Tools -> Agent Control`.
5. Use either the in-app command box, the text agent, or the voice agent.

## Current runtime contract

The plugin listens on `127.0.0.1:7777` and accepts either plain-text commands or JSON payloads such as:

```json
{"command":"add 808"}
```

```json
{"command":"import audio","path":"/tmp/test.wav"}
```

## Why the repo is structured this way

The product repo keeps only the code we own. LMMS itself stays upstream. This makes the submission smaller, reviewable, and clear about what the team actually built.
