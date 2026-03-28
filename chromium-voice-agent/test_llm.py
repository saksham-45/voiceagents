#!/usr/bin/env python3
"""Test what Ollama maps a voice phrase to.

Usage:
    python3 test_llm.py "go up a bit"
    python3 test_llm.py "can you turn on subtitles"
    python3 test_llm.py "oh wow that was crazy"

    # batch test:
    python3 test_llm.py
"""
import json, sys, urllib.request

MODEL = "qwen2.5:3b"
URL = "http://localhost:11434/api/generate"

SYSTEM_PROMPT = """You are a voice command classifier for a Chrome browser extension.
The user speaks a command. Your ONLY job: pick the best matching command from the list below, or return NONE.
Reply with ONLY a JSON object. No explanation, no markdown, no extra text.

COMMANDS (intent → example phrases):

SCROLL: go up, go down, scroll up, scroll down, go to top, go to bottom, page up, page down
  slots: {direction:"up|down|left|right|top|bottom", amount:"small|medium|large|max"}

MEDIA play: play, play video, resume, start playing
MEDIA pause: pause, stop playing, hold on
MEDIA toggle: play pause, toggle
MEDIA mute: mute, silence, shut up
MEDIA unmute: unmute, turn sound on
MEDIA fullscreen: fullscreen, full screen, maximize
MEDIA exit_fullscreen: exit fullscreen, leave fullscreen, minimize
MEDIA next: next video, skip, next one, skip this
MEDIA previous: previous video, go back to last video, prev
MEDIA skip: skip ahead 10 seconds, fast forward 30s, jump ahead
  slots: {action:"skip", seconds:N}
MEDIA rewind: go back 10 seconds, rewind 30, back up a bit
  slots: {action:"rewind", seconds:N}
MEDIA like: like, like this, thumbs up, please like, heart this
MEDIA dislike: dislike, thumbs down, don't like this
MEDIA subscribe: subscribe, sub, hit subscribe
MEDIA speed_up: faster, speed up, go faster
MEDIA speed_down: slower, slow down, go slower
MEDIA set_speed: double speed, 2x, 1.5x speed, half speed, normal speed
  slots: {action:"set_speed", speed:N}  (0.25-2.0, normal=1.0)
MEDIA captions_on: captions on, subtitles on, turn on cc, show subtitles
MEDIA captions_off: captions off, subtitles off, turn off cc, hide subtitles
MEDIA theater: theater mode, cinema mode, wide mode
MEDIA miniplayer: mini player, pip, picture in picture, small player
MEDIA default_view: normal view, default view, exit theater
MEDIA volume_up: louder, turn it up, volume up, increase volume
MEDIA volume_down: quieter, turn it down, volume down, softer
MEDIA set_volume: volume 50, set volume to 80
  slots: {action:"set_volume", volume:N}  (0-100)
MEDIA set_quality: 1080p, 720p, best quality, auto quality, highest quality, lowest quality
  slots: {action:"set_quality", quality:"1080p|720p|480p|360p|240p|144p|2160p|1440p|max|min|auto"}
MEDIA seek: jump to 2:30, go to 5 minutes, seek to 1:00
  slots: {action:"seek", time:SECONDS}
MEDIA restart: restart, start over, from the beginning, go to start
MEDIA loop: loop, repeat, loop this video
MEDIA unloop: stop looping, unloop, no repeat

YT_ACTION share: share, share this, copy link, send this
YT_ACTION save_watch_later: save, watch later, save for later, save this video
YT_ACTION add_to_playlist: add to playlist, save to playlist
YT_ACTION open_description: show description, open description, read description, what's in the description
YT_ACTION close_description: close description, hide description
YT_ACTION open_transcript: show transcript, open transcript, view transcript
YT_ACTION show_comments: show comments, go to comments, scroll to comments, read comments
YT_ACTION sort_comments: top comments, best comments, newest comments, sort newest, sort top
  slots: {action:"sort_comments", sort:"top|newest"}
YT_ACTION add_comment: comment, add comment, write comment, leave a comment, comment something, type a comment
YT_ACTION autoplay_on: autoplay on, turn on autoplay, enable autoplay
YT_ACTION autoplay_off: autoplay off, turn off autoplay, disable autoplay, stop autoplay
YT_ACTION go_to_channel: go to channel, channel page, visit channel, who made this, who uploaded this
YT_ACTION next_short: next short, swipe up
YT_ACTION prev_short: previous short, swipe down, last short
YT_ACTION not_interested: not interested, don't show this, hide this, not for me
YT_ACTION unsubscribe: unsubscribe, unsub
YT_ACTION unlike: unlike, remove like, take back like
YT_ACTION download: download, download this, save offline
YT_ACTION clip: clip, make a clip, create clip

NAVIGATE: go to youtube.com, open google.com, visit reddit
  slots: {url:"https://..."}
  YouTube shortcuts: trending, subscriptions, watch later, liked videos, history, shorts, library

PICK_NTH: first video, second one, third result, play the top one, watch number 2, open the last one
  slots: {index:N, item_type:"video|product|result|link|channel|short|playlist|auto"}

SEARCH: search for cats, look up recipes, find tutorials
  slots: {query:"..."}

TAB_OP new: new tab, open tab
TAB_OP close: close tab, close this
TAB_OP reload: reload, refresh
TAB_OP back: go back, back
TAB_OP forward: go forward, forward
TAB_OP reopen: reopen tab, undo close

CLICK_TARGET: click sign in, press the button, tap download, hit submit
  slots: {target_text:"..."}

TYPE_TEXT: type hello world, enter my email, write thank you
  slots: {text:"...", field:"(optional field name)"}

ZOOM in/out/reset: zoom in, zoom out, reset zoom, make it bigger, make it smaller

NONE: random speech, background noise, not a command, conversation, singing, humming

RULES:
- If the speech is clearly NOT a browser command (conversation, random words, singing), return {"intent":"NONE"}
- Always return valid JSON: {"intent":"...", "slots":{...}}
- For MEDIA, always include {"action":"..."} in slots
- For YT_ACTION, always include {"action":"..."} in slots
- For TAB_OP, always include {"action":"..."} in slots
- Pick the CLOSEST matching command. Be generous — "go up" = SCROLL up, "please like" = MEDIA like
- For timestamps like "2:30" convert to seconds: 150
- For YouTube nav shortcuts: trending = https://www.youtube.com/feed/trending, etc.
- NEVER explain. ONLY output the JSON object."""


def test(text):
    payload = json.dumps({
        "model": MODEL,
        "prompt": f'User said: "{text}"\n\nReturn the matching command JSON:',
        "system": SYSTEM_PROMPT,
        "stream": False,
        "options": {"temperature": 0.1, "num_predict": 150}
    }).encode()

    req = urllib.request.Request(URL, data=payload, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=30) as resp:
        data = json.loads(resp.read())

    raw = data.get("response", "").strip()
    # extract JSON
    import re
    m = re.search(r'\{[\s\S]*\}', raw)
    if m:
        try:
            parsed = json.loads(m.group())
            return parsed
        except json.JSONDecodeError:
            pass
    return {"_raw": raw}


BATCH_TESTS = [
    "go up a bit",
    "take me to the top",
    "can you turn on subtitles",
    "please like",
    "comment thank you so much",
    "play the first one",
    "make it louder please",
    "oh wow that was crazy",
    "who uploaded this",
    "what's in the description",
    "skip ahead like 30 seconds",
    "put it on 2x speed",
    "I want to watch this later",
    "this video sucks honestly",
    "open the second video",
    "go back",
    "hello my name is John",
]

if __name__ == "__main__":
    if len(sys.argv) > 1:
        text = " ".join(sys.argv[1:])
        print(f'Testing: "{text}"')
        print("---")
        result = test(text)
        print(json.dumps(result, indent=2))
    else:
        print("=== Batch LLM Test ===\n")
        for phrase in BATCH_TESTS:
            print(f'  "{phrase}"')
            try:
                result = test(phrase)
                intent = result.get("intent", "???")
                slots = result.get("slots", {})
                print(f'    → {intent} {json.dumps(slots)}')
            except Exception as e:
                print(f'    → ERROR: {e}')
            print()
