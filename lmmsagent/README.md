# LMMS Agent

Hackathon project for local voice and text agents that control LMMS through the `AgentControl` plugin.

## Contents

- `integrations/lmms/AgentControl/` - LMMS plugin source
- `integrations/lmms/patches/` - minimal LMMS host patch set
- `lmms-text-agent/` - local text client for LMMS commands
- `lmms-voice-agent/` - local voice bridge with optional `whisper.cpp`
- `shared/` - shared LMMS transport and command normalization
- `scripts/` - LMMS install and build scripts
- `docs/` - architecture, command map, and demo notes
- `demo/` - smoke-test command list

## Quick start

1. Clone LMMS separately.
2. Run `scripts/install-agentcontrol.sh /path/to/lmms`.
3. Run `scripts/build-lmms.sh /path/to/lmms /path/to/lmms/build`.
4. Launch LMMS and open `Tools -> Agent Control`.
5. Use the in-app box, the text agent, or the voice agent.
