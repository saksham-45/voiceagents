# LMMS Voice Agent

Local voice bridge for LMMS. It accepts either a transcript directly or an audio file transcribed with an external `whisper.cpp` binary.

## Usage

Direct transcript:

```bash
python3 voice_agent.py --text "open mixer"
```

Audio with `whisper.cpp`:

```bash
python3 voice_agent.py --audio-file sample.wav --whisper-cli /path/to/whisper-cli
```
