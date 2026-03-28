# Command Map

## Stable commands

- `add 808`
- `add kick`
- `show mixer`
- `show song editor`
- `show piano roll`
- `show automation editor`
- `show controller rack`
- `show project notes`
- `show microtuner`
- `new sample track`
- `new instrument track`
- `new automation track`
- `new instrument kicker`
- `new instrument tripleoscillator`
- `open slicer`
- `add effect amplifier`
- `remove effect amplifier`
- `import <filename>` from Downloads
- `import audio <full-path>`
- `import midi <full-path>`
- `import hydrogen <full-path>`

## Transport examples

Plain text:

```text
add 808
show mixer
open slicer
```

JSON:

```json
{"command":"add 808"}
```

```json
{"command":"import audio","path":"/tmp/test.wav"}
```

## Short-term roadmap

- richer track targeting, for example `add effect amplifier to agent 808`
- plugin routing, for example `open plugin slicer` resolving to the correct LMMS action
- planner-based intent expansion instead of relying on command aliases alone
