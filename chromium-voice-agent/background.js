// background.js — Voice Agent Service Worker
// Always-on listening, NLU parsing, command execution.
"use strict";

importScripts("autonomous_agent.js");

var listenMode = true;
var ollamaAvailable = null;

chrome.storage.local.get(["va_listen_mode"], function(r) {
  listenMode = r.va_listen_mode !== false;
});

// ── Ollama LLM Integration ──

var LLM_SYSTEM_PROMPT = [
  "You are a voice command classifier for a Chrome browser extension.",
  "The user speaks a command. Your ONLY job: pick the best matching command from the list below, or return NONE.",
  "Reply with ONLY a JSON object. No explanation, no markdown, no extra text.",
  "",
  "COMMANDS (intent → example phrases):",
  "",
  "SCROLL: go up, go down, scroll up, scroll down, go to top, go to bottom, page up, page down",
  "  slots: {direction:'up|down|left|right|top|bottom', amount:'small|medium|large|max'}",
  "",
  "MEDIA play: play, play video, resume, start playing",
  "MEDIA pause: pause, stop playing, hold on",
  "MEDIA toggle: play pause, toggle",
  "MEDIA mute: mute, silence, shut up",
  "MEDIA unmute: unmute, turn sound on",
  "MEDIA fullscreen: fullscreen, full screen, maximize",
  "MEDIA exit_fullscreen: exit fullscreen, leave fullscreen, minimize",
  "MEDIA next: next video, skip, next one, skip this",
  "MEDIA previous: previous video, go back to last video, prev",
  "MEDIA skip: skip ahead 10 seconds, fast forward 30s, jump ahead",
  "  slots: {action:'skip', seconds:N}",
  "MEDIA rewind: go back 10 seconds, rewind 30, back up a bit",
  "  slots: {action:'rewind', seconds:N}",
  "MEDIA like: like, like this, thumbs up, please like, heart this",
  "MEDIA dislike: dislike, thumbs down, don't like this",
  "MEDIA subscribe: subscribe, sub, hit subscribe",
  "MEDIA speed_up: faster, speed up, go faster",
  "MEDIA speed_down: slower, slow down, go slower",
  "MEDIA set_speed: double speed, 2x, 1.5x speed, half speed, normal speed",
  "  slots: {action:'set_speed', speed:N}  (0.25-2.0, normal=1.0)",
  "MEDIA captions_on: captions on, subtitles on, turn on cc, show subtitles",
  "MEDIA captions_off: captions off, subtitles off, turn off cc, hide subtitles",
  "MEDIA theater: theater mode, cinema mode, wide mode",
  "MEDIA miniplayer: mini player, pip, picture in picture, small player",
  "MEDIA default_view: normal view, default view, exit theater",
  "MEDIA volume_up: louder, turn it up, volume up, increase volume",
  "MEDIA volume_down: quieter, turn it down, volume down, softer",
  "MEDIA set_volume: volume 50, set volume to 80",
  "  slots: {action:'set_volume', volume:N}  (0-100)",
  "MEDIA set_quality: 1080p, 720p, best quality, auto quality, highest quality, lowest quality",
  "  slots: {action:'set_quality', quality:'1080p|720p|480p|360p|240p|144p|2160p|1440p|max|min|auto'}",
  "MEDIA seek: jump to 2:30, go to 5 minutes, seek to 1:00",
  "  slots: {action:'seek', time:SECONDS}",
  "MEDIA restart: restart, start over, from the beginning, go to start",
  "MEDIA loop: loop, repeat, loop this video",
  "MEDIA unloop: stop looping, unloop, no repeat",
  "",
  "YT_ACTION share: share, share this, copy link, send this",
  "YT_ACTION save_watch_later: save, watch later, save for later, save this video",
  "YT_ACTION add_to_playlist: add to playlist, save to playlist",
  "YT_ACTION open_description: show description, open description, read description, what's in the description",
  "YT_ACTION close_description: close description, hide description",
  "YT_ACTION open_transcript: show transcript, open transcript, view transcript",
  "YT_ACTION show_comments: show comments, go to comments, scroll to comments, read comments",
  "YT_ACTION sort_comments: top comments, best comments, newest comments, sort newest, sort top",
  "  slots: {action:'sort_comments', sort:'top|newest'}",
  "YT_ACTION add_comment: comment, add comment, write comment, leave a comment, comment something, type a comment",
  "  With spoken text: \"comment thanks for the video\" → slots include text:\"thanks for the video\"",
  "YT_ACTION autoplay_on: autoplay on, turn on autoplay, enable autoplay",
  "YT_ACTION autoplay_off: autoplay off, turn off autoplay, disable autoplay, stop autoplay",
  "YT_ACTION go_to_channel: go to channel, channel page, visit channel, who made this, who uploaded this",
  "YT_ACTION next_short: next short, swipe up",
  "YT_ACTION prev_short: previous short, swipe down, last short",
  "YT_ACTION not_interested: not interested, don't show this, hide this, not for me",
  "YT_ACTION unsubscribe: unsubscribe, unsub",
  "YT_ACTION unlike: unlike, remove like, take back like",
  "YT_ACTION download: download, download this, save offline",
  "YT_ACTION clip: clip, make a clip, create clip",
  "",
  "NAVIGATE: go to youtube.com, open google.com, visit reddit, instagram, instagram reels",
  "  slots: {url:'https://...'}",
  "  YouTube shortcuts: trending, subscriptions, watch later, liked videos, history, shorts, library",
  "",
  "PICK_NTH: first video, second one, third result, play the top one, watch number 2, open the last one",
  "  slots: {index:N, item_type:'video|product|result|link|channel|short|playlist|auto'}",
  "",
  "SEARCH: search for cats, look up recipes, find tutorials",
  "  slots: {query:'...'}",
  "",
  "TAB_OP new: new tab, open tab",
  "TAB_OP close: close tab, close this",
  "TAB_OP reload: reload, refresh",
  "TAB_OP back: go back, back",
  "TAB_OP forward: go forward, forward",
  "TAB_OP reopen: reopen tab, undo close",
  "",
  "CLICK_TARGET: click sign in, press the button, tap download, hit submit",
  "  slots: {target_text:'...'}",
  "",
  "TYPE_TEXT: type hello world, enter my email, write thank you",
  "  slots: {text:'...', field:'(optional field name)'}",
  "",
  "ZOOM in/out/reset: zoom in, zoom out, reset zoom, make it bigger, make it smaller",
  "",
  "REDDIT_ACTION upvote|downvote|save_post|share|open_comments|scroll_next|scroll_prev|join|reply_compose (slots.text for reply body)",
  "INSTAGRAM_ACTION like|unlike|save|unsave|share|focus_comments|compose_comment|next_story|prev_story (slots.text for compose_comment)",
  "",
  "NONE: random speech, background noise, not a command, conversation, singing, humming",
  "",
  "RULES:",
  "- If the speech is clearly NOT a browser command (conversation, random words, singing), return {\"intent\":\"NONE\"}",
  "- Always return valid JSON: {\"intent\":\"...\", \"slots\":{...}}",
  "- For MEDIA, always include {\"action\":\"...\"} in slots",
  "- For YT_ACTION, always include {\"action\":\"...\"} in slots",
  "- For spoken comments like \"comment nice video\", use {\"intent\":\"YT_ACTION\",\"slots\":{\"action\":\"add_comment\",\"text\":\"nice video\"}}",
  "- For TAB_OP, always include {\"action\":\"...\"} in slots",
  "- Pick the CLOSEST matching command. Be generous — 'go up' = SCROLL up, 'please like' = MEDIA like",
  "- For timestamps like '2:30' convert to seconds: 150",
  "- For YouTube nav shortcuts: trending = https://www.youtube.com/feed/trending, etc.",
  "- NEVER explain. ONLY output the JSON object."
].join("\n");

function checkOllama() {
  var controller = new AbortController();
  var timeout = setTimeout(function() { controller.abort(); }, 5000);
  fetch("http://localhost:11434/api/tags", { signal: controller.signal })
    .then(function(r) {
      clearTimeout(timeout);
      if (r.status === 403) {
        ollamaAvailable = false;
        console.warn(
          "Ollama: HTTP 403 (extension origin blocked). Fix: OLLAMA_ORIGINS=chrome-extension://* ollama serve (see README)."
        );
        return null;
      }
      if (!r.ok) {
        ollamaAvailable = false;
        console.log("Ollama: HTTP", r.status);
        return null;
      }
      return r.json();
    })
    .then(function(data) {
      if (!data) return;
      ollamaAvailable = true;
      var models = (data.models || []).map(function(m) { return m.name; }).join(", ");
      console.log("Ollama: connected, models:", models);
    })
    .catch(function(err) {
      clearTimeout(timeout);
      ollamaAvailable = false;
      console.log("Ollama: not available —", err.message || err);
    });
}
setTimeout(checkOllama, 1000);
setInterval(checkOllama, 30000);

function callLLM(text) {
  return fetch("http://localhost:11434/api/generate", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({
      model: "qwen2.5:3b",
      prompt: "User said: \"" + text + "\"\n\nReturn the matching command JSON:",
      system: LLM_SYSTEM_PROMPT,
      stream: false,
      options: { temperature: 0.1, num_predict: 150 }
    })
  })
  .then(function(r) {
    if (r.status === 403) {
      throw new Error(
        "Ollama HTTP 403: set OLLAMA_ORIGINS=chrome-extension://* then restart Ollama (README: Ollama 403)."
      );
    }
    if (r.status === 404) {
      throw new Error(
        "Ollama HTTP 404: model not in ollama list. Change model in callLLM() in background.js or run ollama pull qwen2.5:3b (README: Ollama 404)."
      );
    }
    if (!r.ok) throw new Error("Ollama HTTP " + r.status);
    return r.json();
  })
  .then(function(data) {
    var resp = (data.response || "").trim();
    console.log("LLM raw:", resp);
    var jsonMatch = resp.match(/\{[\s\S]*\}/);
    if (!jsonMatch) throw new Error("No JSON in response");
    var parsed = JSON.parse(jsonMatch[0]);
    if (!parsed.intent) throw new Error("No intent");
    return parsed;
  });
}

function processCommand(transcript, callback) {
  var regexResult = parseCommand(transcript);
  if (regexResult.intent !== "UNKNOWN") {
    regexResult._source = "regex";
    callback(regexResult);
    return;
  }
  if (!ollamaAvailable) {
    console.log("Ollama unavailable, ignoring unrecognized:", transcript);
    callback(regexResult);
    return;
  }
  console.log("Regex miss, asking LLM:", transcript);
  safeBroadcast({ type: "LLM_THINKING", transcript: transcript });
  callLLM(transcript)
    .then(function(parsed) {
      console.log("LLM parsed:", parsed);
      if (parsed.intent === "NONE") {
        console.log("LLM says not a command, ignoring");
        callback({ intent: "UNKNOWN", slots: { text: transcript } });
      } else {
        parsed._source = "ai";
        callback(parsed);
      }
    })
    .catch(function(err) {
      console.log("LLM error, ignoring:", err.message);
      callback({ intent: "UNKNOWN", slots: { text: transcript } });
    });
}

// ── Ordinal helpers ──

var ORDINALS = {
  "first": 1, "1st": 1, "one": 1,
  "second": 2, "2nd": 2, "two": 2,
  "third": 3, "3rd": 3, "three": 3,
  "fourth": 4, "4th": 4, "four": 4,
  "fifth": 5, "5th": 5, "five": 5,
  "sixth": 6, "6th": 6, "six": 6,
  "seventh": 7, "7th": 7, "seven": 7,
  "eighth": 8, "8th": 8, "eight": 8,
  "ninth": 9, "9th": 9, "nine": 9,
  "tenth": 10, "10th": 10, "ten": 10,
  "last": -1
};

function parseOrdinal(word) {
  return ORDINALS[word] || null;
}

// ── Timestamp parser ──
// Handles: "2 minutes 30 seconds", "1:30", "45 seconds", "1 hour 20 minutes", "90", "1h30m"
function parseTimestamp(str) {
  var colonMatch = str.match(/^(\d{1,2}):(\d{2})(?::(\d{2}))?$/);
  if (colonMatch) {
    if (colonMatch[3]) return parseInt(colonMatch[1]) * 3600 + parseInt(colonMatch[2]) * 60 + parseInt(colonMatch[3]);
    return parseInt(colonMatch[1]) * 60 + parseInt(colonMatch[2]);
  }
  var compact = str.match(/^(\d+)h(?:(\d+)m)?(?:(\d+)s)?$/);
  if (compact) return (parseInt(compact[1]) || 0) * 3600 + (parseInt(compact[2]) || 0) * 60 + (parseInt(compact[3]) || 0);
  compact = str.match(/^(\d+)m(?:(\d+)s)?$/);
  if (compact) return (parseInt(compact[1]) || 0) * 60 + (parseInt(compact[2]) || 0);
  compact = str.match(/^(\d+)s$/);
  if (compact) return parseInt(compact[1]);
  var total = 0, found = false;
  var hm = str.match(/(\d+)\s*hours?/); if (hm) { total += parseInt(hm[1]) * 3600; found = true; }
  var mm = str.match(/(\d+)\s*min(?:utes?)?/); if (mm) { total += parseInt(mm[1]) * 60; found = true; }
  var sm = str.match(/(\d+)\s*sec(?:onds?)?/); if (sm) { total += parseInt(sm[1]); found = true; }
  if (found) return total;
  if (/^\d+$/.test(str.trim())) return parseInt(str.trim());
  return null;
}

// ── NLU Parser ──

function parseCommand(text) {
  var t = text.trim().toLowerCase().replace(/[.,!?]+$/, "");

  // Strip polite filler words so "please pause", "can you like", etc. match core regex
  t = t
    .replace(/^(?:please|okay|ok|hey|yo|just|now|can\s+you|could\s+you|would\s+you|i\s+want\s+(?:you\s+)?to|go\s+ahead\s+and)\s+/i, "")
    .replace(/\s+please$/, "")
    .replace(/\s+for\s+me$/, "")
    .replace(/\s+right\s+now$/, "")
    .trim();

  // ══════════════════════════════════════
  // YOUTUBE-SPECIFIC COMMANDS
  // ══════════════════════════════════════

  // ── YouTube Navigation ──
  if (/^(?:go\s+to\s+)?(?:youtube\s+)?(?:home|homepage|home\s+page|main\s+page)$/.test(t)) return { intent: "NAVIGATE", slots: { url: "https://www.youtube.com" } };
  if (/^(?:go\s+to\s+)?(?:youtube\s+)?trending$/.test(t)) return { intent: "NAVIGATE", slots: { url: "https://www.youtube.com/feed/trending" } };
  if (/^(?:go\s+to\s+)?(?:my\s+)?subscriptions?$/.test(t)) return { intent: "NAVIGATE", slots: { url: "https://www.youtube.com/feed/subscriptions" } };
  if (/^(?:go\s+to\s+)?(?:my\s+)?watch\s+later$/.test(t)) return { intent: "NAVIGATE", slots: { url: "https://www.youtube.com/playlist?list=WL" } };
  if (/^(?:go\s+to\s+)?(?:my\s+)?liked\s+videos?$/.test(t)) return { intent: "NAVIGATE", slots: { url: "https://www.youtube.com/playlist?list=LL" } };
  if (/^(?:go\s+to\s+)?(?:my\s+)?(?:watch\s+)?history$/.test(t)) return { intent: "NAVIGATE", slots: { url: "https://www.youtube.com/feed/history" } };
  if (/^(?:go\s+to\s+)?(?:youtube\s+)?shorts?$/.test(t)) return { intent: "NAVIGATE", slots: { url: "https://www.youtube.com/shorts" } };
  if (/^(?:go\s+to\s+)?(?:youtube\s+)?(?:live|streams?|live\s+streams?)$/.test(t)) return { intent: "NAVIGATE", slots: { url: "https://www.youtube.com/feed/trending?bp=6gQJRkVsaXZlX3Bx" } };
  if (/^(?:go\s+to\s+)?(?:youtube\s+)?gaming$/.test(t)) return { intent: "NAVIGATE", slots: { url: "https://www.youtube.com/gaming" } };
  if (/^(?:go\s+to\s+)?(?:youtube\s+)?music$/.test(t)) return { intent: "NAVIGATE", slots: { url: "https://music.youtube.com" } };
  if (/^(?:go\s+to\s+)?(?:my\s+)?(?:youtube\s+)?library$/.test(t)) return { intent: "NAVIGATE", slots: { url: "https://www.youtube.com/feed/library" } };
  if (/^(?:go\s+to\s+)?(?:my\s+)?(?:youtube\s+)?playlists?$/.test(t)) return { intent: "NAVIGATE", slots: { url: "https://www.youtube.com/feed/playlists" } };
  if (/^(?:go\s+to\s+)?(?:my\s+)?(?:youtube\s+)?(?:channel|my\s+channel)$/.test(t)) return { intent: "YT_ACTION", slots: { action: "my_channel" } };
  if (/^(?:go\s+to\s+)?(?:youtube\s+)?notifications?$/.test(t)) return { intent: "YT_ACTION", slots: { action: "notifications" } };

  // ══════════════════════════════════════
  // REDDIT & INSTAGRAM (navigation + actions)
  // ══════════════════════════════════════

  if (/^(?:go\s+to\s+)?reddit$|^open\s+reddit$/.test(t)) return { intent: "NAVIGATE", slots: { url: "https://www.reddit.com/" } };
  if (/^reddit\s+popular$|^popular\s+reddit$/.test(t)) return { intent: "NAVIGATE", slots: { url: "https://www.reddit.com/r/popular/" } };

  if (/^(?:go\s+to\s+)?instagram(?:\s+home)?$/.test(t) || t === "instagram") return { intent: "NAVIGATE", slots: { url: "https://www.instagram.com/" } };
  if (/^instagram\s+reels$|^open\s+reels$/.test(t)) return { intent: "NAVIGATE", slots: { url: "https://www.instagram.com/reels/" } };
  if (/^instagram\s+messages$|^open\s+dms$|^direct\s+messages$/.test(t)) return { intent: "NAVIGATE", slots: { url: "https://www.instagram.com/direct/inbox/" } };

  if (/^reddit\s+upvote$|^upvote$/.test(t)) return { intent: "REDDIT_ACTION", slots: { action: "upvote" } };
  if (/^reddit\s+downvote$|^downvote$/.test(t)) return { intent: "REDDIT_ACTION", slots: { action: "downvote" } };
  if (/^save\s+post$|^save\s+this\s+post$/.test(t)) return { intent: "REDDIT_ACTION", slots: { action: "save_post" } };
  if (/^share\s+(?:this\s+)?post$/.test(t)) return { intent: "REDDIT_ACTION", slots: { action: "share" } };
  if (/^(?:next|skip)\s+post$|^scroll\s+next\s+post$/.test(t)) return { intent: "REDDIT_ACTION", slots: { action: "scroll_next" } };
  if (/^(?:prev|previous)\s+post$|^scroll\s+prev(?:ious)?\s+post$/.test(t)) return { intent: "REDDIT_ACTION", slots: { action: "scroll_prev" } };
  if (/^reddit\s+comments$|^open\s+post\s+comments$/.test(t)) return { intent: "REDDIT_ACTION", slots: { action: "open_comments" } };
  if (/^join(?:\s+(?:subreddit|this|community))?$/.test(t)) return { intent: "REDDIT_ACTION", slots: { action: "join" } };
  var redditReply = t.match(/^reply\s+(.+)$/);
  if (redditReply && redditReply[1].trim()) return { intent: "REDDIT_ACTION", slots: { action: "reply_compose", text: redditReply[1].trim() } };

  if (/^(?:like|heart)\s+(?:this\s+)?(?:post|photo|pic)$/.test(t)) return { intent: "INSTAGRAM_ACTION", slots: { action: "like" } };
  if (/^unlike\s+(?:this\s+)?(?:post|photo)$/.test(t)) return { intent: "INSTAGRAM_ACTION", slots: { action: "unlike" } };
  if (/^insta\s+save$|^instagram\s+save$|^(?:save|bookmark)\s+(?:this\s+)?(?:photo|pic)$/.test(t)) return { intent: "INSTAGRAM_ACTION", slots: { action: "save" } };
  if (/^insta\s+unsave$|^unsave\s+(?:this\s+)?(?:photo|pic)$/.test(t)) return { intent: "INSTAGRAM_ACTION", slots: { action: "unsave" } };
  if (/^insta\s+share$|^instagram\s+share$|^share\s+(?:this\s+)?(?:photo|pic|story)$/.test(t)) return { intent: "INSTAGRAM_ACTION", slots: { action: "share" } };
  if (/^(?:next|nexxt|skip)\s+(?:story|reel)s?$/.test(t) || /^reels?\s+(?:next|nexxt|skip)$/.test(t) || /^swipe\s+up$/.test(t)) return { intent: "INSTAGRAM_ACTION", slots: { action: "next_story" } };
  if (/^(?:prev|previous)\s+(?:story|reel)s?$/.test(t) || /^reels?\s+(?:back|prev|previous)$/.test(t)) return { intent: "INSTAGRAM_ACTION", slots: { action: "prev_story" } };
  if (/^instagram\s+open\s+comments$|^insta\s+comments$/.test(t)) return { intent: "INSTAGRAM_ACTION", slots: { action: "focus_comments" } };
  var igCommentPhrase = t.match(/^(?:instagram|insta)\s+comment\s+(.+)$/);
  if (igCommentPhrase && igCommentPhrase[1].trim()) return { intent: "INSTAGRAM_ACTION", slots: { action: "compose_comment", text: igCommentPhrase[1].trim() } };

  // ── Channel Page Navigation ──
  if (/^(?:(?:go\s+to|show|view)\s+)?channel\s+videos?$/.test(t)) return { intent: "YT_ACTION", slots: { action: "channel_tab", tab: "videos" } };
  if (/^(?:(?:go\s+to|show|view)\s+)?channel\s+shorts?$/.test(t)) return { intent: "YT_ACTION", slots: { action: "channel_tab", tab: "shorts" } };
  if (/^(?:(?:go\s+to|show|view)\s+)?channel\s+(?:playlists?|lists?)$/.test(t)) return { intent: "YT_ACTION", slots: { action: "channel_tab", tab: "playlists" } };
  if (/^(?:(?:go\s+to|show|view)\s+)?channel\s+(?:community|posts?)$/.test(t)) return { intent: "YT_ACTION", slots: { action: "channel_tab", tab: "community" } };
  if (/^(?:(?:go\s+to|show|view)\s+)?channel\s+(?:live|streams?)$/.test(t)) return { intent: "YT_ACTION", slots: { action: "channel_tab", tab: "streams" } };
  if (/^(?:(?:go\s+to|show|view)\s+)?(?:channel\s+)?about(?:\s+this\s+channel)?$/.test(t)) return { intent: "YT_ACTION", slots: { action: "channel_tab", tab: "about" } };
  if (/^(?:go\s+to\s+)?(?:the\s+)?(?:channel|uploader)(?:'?s?\s+page)?$/.test(t)) return { intent: "YT_ACTION", slots: { action: "go_to_channel" } };

  // ── Playback Speed ──
  if (/^(?:speed\s+up|faster|increase\s+speed)$/.test(t)) return { intent: "MEDIA", slots: { action: "speed_up" } };
  if (/^(?:slow\s+down|slower|decrease\s+speed)$/.test(t)) return { intent: "MEDIA", slots: { action: "speed_down" } };
  if (/^(?:normal\s+speed|reset\s+speed|1x(\s+speed)?|default\s+speed)$/.test(t)) return { intent: "MEDIA", slots: { action: "set_speed", speed: 1.0 } };
  if (/^(?:double\s+speed|2x(\s+speed)?)$/.test(t)) return { intent: "MEDIA", slots: { action: "set_speed", speed: 2.0 } };
  if (/^(?:half\s+speed|0\.?5x(\s+speed)?)$/.test(t)) return { intent: "MEDIA", slots: { action: "set_speed", speed: 0.5 } };
  var speedMatch = t.match(/^(?:set\s+)?speed\s+(?:to\s+)?(\d+(?:\.\d+)?)\s*x?$/);
  if (speedMatch) return { intent: "MEDIA", slots: { action: "set_speed", speed: parseFloat(speedMatch[1]) } };
  var speedMatch2 = t.match(/^(\d+(?:\.\d+)?)\s*x\s*speed$/);
  if (speedMatch2) return { intent: "MEDIA", slots: { action: "set_speed", speed: parseFloat(speedMatch2[1]) } };

  // ── Captions / Subtitles ──
  if (/^(?:(?:turn\s+)?on\s+)?(?:captions?|subtitles?|cc|closed\s+captions?)(?:\s+on)?$/.test(t) && !/off/.test(t)) return { intent: "MEDIA", slots: { action: "captions_on" } };
  if (/^(?:turn\s+)?(?:off\s+)?(?:captions?|subtitles?|cc|closed\s+captions?)(?:\s+off)?$/.test(t) && /off/.test(t)) return { intent: "MEDIA", slots: { action: "captions_off" } };
  if (/^(?:show|enable|display)\s+(?:captions?|subtitles?|cc)$/.test(t)) return { intent: "MEDIA", slots: { action: "captions_on" } };
  if (/^(?:hide|disable|remove)\s+(?:captions?|subtitles?|cc)$/.test(t)) return { intent: "MEDIA", slots: { action: "captions_off" } };
  if (/^(?:toggle)\s+(?:captions?|subtitles?|cc)$/.test(t)) return { intent: "MEDIA", slots: { action: "captions_toggle" } };

  // ── Theater / Mini Player / PiP ──
  if (/^(?:theater|theatre|cinema|wide)(?:\s+mode)?$/.test(t)) return { intent: "MEDIA", slots: { action: "theater" } };
  if (/^(?:exit|leave|close)\s+(?:theater|theatre|cinema|wide)(?:\s+mode)?$/.test(t)) return { intent: "MEDIA", slots: { action: "default_view" } };
  if (/^(?:mini\s*player|pip|picture\s+in\s+picture|small\s+player|floating\s+player)$/.test(t)) return { intent: "MEDIA", slots: { action: "miniplayer" } };
  if (/^(?:exit|leave|close)\s+(?:mini\s*player|pip|picture\s+in\s+picture)$/.test(t)) return { intent: "MEDIA", slots: { action: "exit_miniplayer" } };
  if (/^(?:default|normal|regular|standard)\s+(?:view|mode|player|size)$/.test(t)) return { intent: "MEDIA", slots: { action: "default_view" } };

  // ── Volume ──
  if (/^(?:volume\s+up|louder|increase\s+volume|turn\s+(?:it\s+)?up)$/.test(t)) return { intent: "MEDIA", slots: { action: "volume_up" } };
  if (/^(?:volume\s+down|quieter|softer|decrease\s+volume|turn\s+(?:it\s+)?down)$/.test(t)) return { intent: "MEDIA", slots: { action: "volume_down" } };
  var volMatch = t.match(/^(?:set\s+)?volume\s+(?:to\s+)?(\d+)\s*%?$/);
  if (volMatch) return { intent: "MEDIA", slots: { action: "set_volume", volume: parseInt(volMatch[1]) } };
  if (/^(?:max|full|maximum)\s+volume$/.test(t)) return { intent: "MEDIA", slots: { action: "set_volume", volume: 100 } };

  // ── Video Quality ──
  var qualMatch = t.match(/^(?:(?:set\s+)?quality\s+(?:to\s+)?|(?:switch\s+to\s+)?)(\d{3,4})\s*p?$/);
  if (qualMatch) return { intent: "MEDIA", slots: { action: "set_quality", quality: qualMatch[1] + "p" } };
  if (/^(?:best|highest|max|maximum|hd|high)\s+quality$/.test(t)) return { intent: "MEDIA", slots: { action: "set_quality", quality: "max" } };
  if (/^(?:lowest|worst|low|minimum|min)\s+quality$/.test(t)) return { intent: "MEDIA", slots: { action: "set_quality", quality: "min" } };
  if (/^(?:auto|automatic)\s+quality$/.test(t)) return { intent: "MEDIA", slots: { action: "set_quality", quality: "auto" } };
  if (/^(?:4k|2160p?)(?:\s+quality)?$/.test(t)) return { intent: "MEDIA", slots: { action: "set_quality", quality: "2160p" } };
  if (/^(?:1440p?|2k)(?:\s+quality)?$/.test(t)) return { intent: "MEDIA", slots: { action: "set_quality", quality: "1440p" } };
  if (/^(?:1080p?|full\s*hd)(?:\s+quality)?$/.test(t)) return { intent: "MEDIA", slots: { action: "set_quality", quality: "1080p" } };
  if (/^720p?(?:\s+quality)?$/.test(t)) return { intent: "MEDIA", slots: { action: "set_quality", quality: "720p" } };
  if (/^480p?(?:\s+quality)?$/.test(t)) return { intent: "MEDIA", slots: { action: "set_quality", quality: "480p" } };
  if (/^360p?(?:\s+quality)?$/.test(t)) return { intent: "MEDIA", slots: { action: "set_quality", quality: "360p" } };
  if (/^(?:240p?|144p?)(?:\s+quality)?$/.test(t)) return { intent: "MEDIA", slots: { action: "set_quality", quality: t.match(/\d+/)[0] + "p" } };

  // ── Seek / Jump to Timestamp ──
  var seekMatch = t.match(/^(?:seek|jump|go|skip)\s+to\s+(.+)$/);
  if (seekMatch) {
    var ts = parseTimestamp(seekMatch[1]);
    if (ts !== null) return { intent: "MEDIA", slots: { action: "seek", time: ts } };
  }
  var seekMatch2 = t.match(/^(?:go\s+to\s+)?(?:the\s+)?(?:beginning|start)(?:\s+of\s+(?:the\s+)?video)?$/);
  if (seekMatch2) return { intent: "MEDIA", slots: { action: "restart" } };
  if (/^(?:restart|start\s+over|from\s+(?:the\s+)?(?:beginning|start))$/.test(t)) return { intent: "MEDIA", slots: { action: "restart" } };
  if (/^(?:go\s+to\s+)?(?:the\s+)?end(?:\s+of\s+(?:the\s+)?video)?$/.test(t)) return { intent: "MEDIA", slots: { action: "seek_end" } };

  // ── Autoplay ──
  if (/^(?:(?:turn\s+)?on\s+)?auto\s*play(?:\s+on)?$/.test(t) && !/off/.test(t)) return { intent: "YT_ACTION", slots: { action: "autoplay_on" } };
  if (/^(?:turn\s+)?(?:off\s+)?auto\s*play(?:\s+off)?$/.test(t) && /off/.test(t)) return { intent: "YT_ACTION", slots: { action: "autoplay_off" } };
  if (/^toggle\s+auto\s*play$/.test(t)) return { intent: "YT_ACTION", slots: { action: "autoplay_toggle" } };

  // ── Ambient Mode ──
  if (/^(?:(?:turn\s+)?on\s+)?ambient(?:\s+mode)?(?:\s+on)?$/.test(t) && !/off/.test(t)) return { intent: "YT_ACTION", slots: { action: "ambient_on" } };
  if (/^(?:turn\s+)?(?:off\s+)?ambient(?:\s+mode)?(?:\s+off)?$/.test(t) && /off/.test(t)) return { intent: "YT_ACTION", slots: { action: "ambient_off" } };
  if (/^toggle\s+ambient(?:\s+mode)?$/.test(t)) return { intent: "YT_ACTION", slots: { action: "ambient_toggle" } };

  // ── Annotations ──
  if (/^(?:(?:turn\s+)?on\s+)?annotations?(?:\s+on)?$/.test(t) && !/off/.test(t)) return { intent: "YT_ACTION", slots: { action: "annotations_on" } };
  if (/^(?:turn\s+)?(?:off\s+)?annotations?(?:\s+off)?$/.test(t) && /off/.test(t)) return { intent: "YT_ACTION", slots: { action: "annotations_off" } };

  // ── Loop / Repeat ──
  if (/^(?:loop|repeat)(?:\s+(?:this\s+)?video)?$/.test(t)) return { intent: "MEDIA", slots: { action: "loop" } };
  if (/^(?:unloop|stop\s+(?:loop|repeat)(?:ing)?|(?:turn\s+)?off\s+(?:loop|repeat)|no\s+(?:loop|repeat))$/.test(t)) return { intent: "MEDIA", slots: { action: "unloop" } };

  // ── Video Interactions ──
  if (/^(?:share|share\s+(?:this\s+)?video|copy\s+link|copy\s+(?:video\s+)?url|get\s+link)$/.test(t)) return { intent: "YT_ACTION", slots: { action: "share" } };
  if (/^(?:save|save\s+(?:this\s+)?video|save\s+to\s+watch\s+later|add\s+to\s+watch\s+later|watch\s+(?:this\s+)?later)$/.test(t)) return { intent: "YT_ACTION", slots: { action: "save_watch_later" } };
  if (/^(?:add\s+to\s+playlist|save\s+to\s+playlist)$/.test(t)) return { intent: "YT_ACTION", slots: { action: "add_to_playlist" } };
  if (/^(?:(?:open|show|expand|read)\s+)?description$/.test(t)) return { intent: "YT_ACTION", slots: { action: "open_description" } };
  if (/^(?:close|collapse|hide)\s+description$/.test(t)) return { intent: "YT_ACTION", slots: { action: "close_description" } };
  if (/^(?:show|open|view)\s+transcript$/.test(t)) return { intent: "YT_ACTION", slots: { action: "open_transcript" } };
  if (/^(?:close|hide)\s+transcript$/.test(t)) return { intent: "YT_ACTION", slots: { action: "close_transcript" } };
  if (/^(?:clip|create\s+(?:a\s+)?clip|make\s+(?:a\s+)?clip)$/.test(t)) return { intent: "YT_ACTION", slots: { action: "clip" } };
  if (/^(?:download|download\s+(?:this\s+)?video)$/.test(t)) return { intent: "YT_ACTION", slots: { action: "download" } };
  if (/^(?:report|report\s+(?:this\s+)?video|flag|flag\s+(?:this\s+)?video)$/.test(t)) return { intent: "YT_ACTION", slots: { action: "report" } };
  if (/^(?:not\s+interested|don'?t\s+recommend|not\s+for\s+me|hide\s+(?:this\s+)?video)$/.test(t)) return { intent: "YT_ACTION", slots: { action: "not_interested" } };
  if (/^(?:don'?t\s+recommend\s+(?:this\s+)?channel|hide\s+(?:this\s+)?channel)$/.test(t)) return { intent: "YT_ACTION", slots: { action: "dont_recommend_channel" } };
  if (/^(?:remove\s+like|unlike|take\s+back\s+like)$/.test(t)) return { intent: "YT_ACTION", slots: { action: "unlike" } };
  if (/^unsubscribe$/.test(t)) return { intent: "YT_ACTION", slots: { action: "unsubscribe" } };

  // ── Comments ──
  if (/^(?:(?:go\s+to|show|scroll\s+to|view|open)\s+)?(?:the\s+)?comments?(?:\s+section)?$/.test(t)) return { intent: "YT_ACTION", slots: { action: "show_comments" } };
  if (/^(?:sort\s+(?:by\s+)?)?(?:top|best)\s+comments?$/.test(t)) return { intent: "YT_ACTION", slots: { action: "sort_comments", sort: "top" } };
  if (/^(?:sort\s+(?:by\s+)?)?(?:new|newest|recent|latest)\s+(?:comments?\s+)?(?:first)?$/.test(t)) return { intent: "YT_ACTION", slots: { action: "sort_comments", sort: "newest" } };
  // "comment thanks for watching" / "add comment lol" — body after the phrase becomes comment text
  var ytCommentBody = t.match(/^comment\s+(.+)$/);
  if (ytCommentBody && ytCommentBody[1].trim()) return { intent: "YT_ACTION", slots: { action: "add_comment", text: ytCommentBody[1].trim() } };
  var ytCommentBody2 = t.match(/^(?:add|write|leave|post)\s+comment\s+(.+)$/);
  if (ytCommentBody2 && ytCommentBody2[1].trim()) return { intent: "YT_ACTION", slots: { action: "add_comment", text: ytCommentBody2[1].trim() } };
  if (/^(?:add|write|leave|post)\s+(?:a\s+)?comment$/.test(t)) return { intent: "YT_ACTION", slots: { action: "add_comment" } };

  // ── Shorts (say "next short"; "swipe up" is used for Instagram Reels) ──
  if (/^(?:next\s+short|skip\s+short)$/.test(t)) return { intent: "YT_ACTION", slots: { action: "next_short" } };
  if (/^(?:prev(?:ious)?\s+short|swipe\s+down|last\s+short)$/.test(t)) return { intent: "YT_ACTION", slots: { action: "prev_short" } };
  if (/^(?:like\s+(?:this\s+)?short)$/.test(t)) return { intent: "MEDIA", slots: { action: "like" } };

  // ── YouTube Search Filters ──
  if (/^(?:filter\s+(?:by\s+)?)?(?:today|uploaded?\s+today)$/.test(t)) return { intent: "YT_ACTION", slots: { action: "filter", filter_type: "date", value: "today" } };
  if (/^(?:filter\s+(?:by\s+)?)?(?:this\s+week|uploaded?\s+this\s+week)$/.test(t)) return { intent: "YT_ACTION", slots: { action: "filter", filter_type: "date", value: "week" } };
  if (/^(?:filter\s+(?:by\s+)?)?(?:this\s+month|uploaded?\s+this\s+month)$/.test(t)) return { intent: "YT_ACTION", slots: { action: "filter", filter_type: "date", value: "month" } };
  if (/^(?:filter\s+(?:by\s+)?)?(?:this\s+year|uploaded?\s+this\s+year)$/.test(t)) return { intent: "YT_ACTION", slots: { action: "filter", filter_type: "date", value: "year" } };
  if (/^(?:(?:filter|show|only)\s+)?(?:short\s+videos?|under\s+4\s+min(?:utes)?)$/.test(t)) return { intent: "YT_ACTION", slots: { action: "filter", filter_type: "duration", value: "short" } };
  if (/^(?:(?:filter|show|only)\s+)?(?:medium\s+videos?|4\s*-?\s*20\s+min(?:utes)?)$/.test(t)) return { intent: "YT_ACTION", slots: { action: "filter", filter_type: "duration", value: "medium" } };
  if (/^(?:(?:filter|show|only)\s+)?(?:long\s+videos?|over\s+20\s+min(?:utes)?)$/.test(t)) return { intent: "YT_ACTION", slots: { action: "filter", filter_type: "duration", value: "long" } };
  if (/^(?:sort\s+by\s+)?(?:(?:upload|uploaded?)\s+date|newest|most\s+recent)$/.test(t)) return { intent: "YT_ACTION", slots: { action: "filter", filter_type: "sort", value: "date" } };
  if (/^(?:sort\s+by\s+)?(?:view\s+count|most\s+view(?:ed|s))$/.test(t)) return { intent: "YT_ACTION", slots: { action: "filter", filter_type: "sort", value: "views" } };
  if (/^(?:sort\s+by\s+)?(?:rating|(?:top|highest)\s+rated)$/.test(t)) return { intent: "YT_ACTION", slots: { action: "filter", filter_type: "sort", value: "rating" } };
  if (/^(?:sort\s+by\s+)?relevance$/.test(t)) return { intent: "YT_ACTION", slots: { action: "filter", filter_type: "sort", value: "relevance" } };
  if (/^(?:filter\s+(?:by\s+)?)?(?:hd|high\s+definition)(?:\s+(?:only|videos?))?$/.test(t)) return { intent: "YT_ACTION", slots: { action: "filter", filter_type: "feature", value: "hd" } };
  if (/^(?:filter\s+(?:by\s+)?)?(?:4k)(?:\s+(?:only|videos?))?$/.test(t)) return { intent: "YT_ACTION", slots: { action: "filter", filter_type: "feature", value: "4k" } };
  if (/^(?:filter\s+(?:by\s+)?)?(?:(?:has\s+)?subtitles?|(?:has\s+)?cc|(?:with\s+)?captions?)$/.test(t)) return { intent: "YT_ACTION", slots: { action: "filter", filter_type: "feature", value: "subtitles" } };
  if (/^(?:filter\s+(?:by\s+)?)?live$/.test(t)) return { intent: "YT_ACTION", slots: { action: "filter", filter_type: "type", value: "live" } };
  if (/^(?:clear|reset|remove)\s+(?:all\s+)?filters?$/.test(t)) return { intent: "YT_ACTION", slots: { action: "clear_filters" } };

  // ══════════════════════════════════════
  // GENERAL COMMANDS (all websites)
  // ══════════════════════════════════════

  // ── Scroll ──
  if (/^(?:go\s+(?:to\s+)?|take\s+me\s+(?:to\s+)?)?(?:the\s+)?top(?:\s+of\s+(?:the\s+)?page)?$/.test(t)) return { intent: "SCROLL", slots: { direction: "top", amount: "max" } };
  if (/^(?:go\s+(?:to\s+)?|take\s+me\s+(?:to\s+)?)?(?:the\s+)?bottom(?:\s+of\s+(?:the\s+)?page)?$/.test(t)) return { intent: "SCROLL", slots: { direction: "bottom", amount: "max" } };
  var scrollMatch = t.match(/^(?:scroll|go|move)\s+(up|down|left|right)(?:\s+(a lot|a little|a bit|some|more))?$/);
  if (scrollMatch) {
    var rawAmt = scrollMatch[2] || "";
    var amount = "medium";
    if (rawAmt.includes("lot") || rawAmt.includes("more")) amount = "large";
    else if (rawAmt.includes("little") || rawAmt.includes("bit") || rawAmt.includes("some")) amount = "small";
    return { intent: "SCROLL", slots: { direction: scrollMatch[1], amount: amount } };
  }
  if (t === "page down" || t === "scroll down" || t === "down") return { intent: "SCROLL", slots: { direction: "down", amount: "large" } };
  if (t === "page up" || t === "scroll up" || t === "up") return { intent: "SCROLL", slots: { direction: "up", amount: "large" } };

  // ── Media (basic — works on any site) ──
  if (/^(?:play|resume)(?:\s+(?:the\s+)?video)?$/.test(t)) return { intent: "MEDIA", slots: { action: "play" } };
  if (/^pause(?:\s+(?:the\s+)?video)?$/.test(t)) return { intent: "MEDIA", slots: { action: "pause" } };
  if (/^(?:stop|stop\s+(?:the\s+)?video|stop\s+playing)$/.test(t)) return { intent: "MEDIA", slots: { action: "pause" } };
  if (/^(?:play\s*\/?\s*pause|toggle)$/.test(t)) return { intent: "MEDIA", slots: { action: "toggle" } };
  if (/^(?:mute|mute\s+(?:the\s+)?(?:video|audio|sound|tab))$/.test(t)) return { intent: "MEDIA", slots: { action: "mute" } };
  if (/^(?:unmute|unmute\s+(?:the\s+)?(?:video|audio|sound|tab)|turn\s+(?:the\s+)?sound\s+(?:back\s+)?on)$/.test(t)) return { intent: "MEDIA", slots: { action: "unmute" } };
  if (/^(?:full\s*screen|enter\s+full\s*screen|go\s+full\s*screen)$/.test(t)) return { intent: "MEDIA", slots: { action: "fullscreen" } };
  if (/^(?:exit\s+full\s*screen|leave\s+full\s*screen)$/.test(t)) return { intent: "MEDIA", slots: { action: "exit_fullscreen" } };
  if (/^(?:nexxt|next(?:\s+(?:the\s+)?video)?|skip(?:\s+(?:this|the)\s+video)?)$/.test(t)) return { intent: "MEDIA", slots: { action: "next" } };
  if (/^(?:prev(?:ious)?(?:\s+(?:the\s+)?video)?)$/.test(t)) return { intent: "MEDIA", slots: { action: "previous" } };
  var skipMatch = t.match(/^(?:skip|fast\s+forward|jump)\s+(?:ahead\s+)?(?:like\s+)?(\d+)\s*(?:seconds?|s|secs?)?(?:\s+ahead)?$/);
  if (skipMatch) return { intent: "MEDIA", slots: { action: "skip", seconds: parseInt(skipMatch[1]) } };
  var rewindMatch = t.match(/^(?:rewind|go\s+back|back\s+up)\s+(?:like\s+)?(\d+)\s*(?:seconds?|s|secs?)?$/);
  if (rewindMatch) return { intent: "MEDIA", slots: { action: "rewind", seconds: parseInt(rewindMatch[1]) } };
  if (/^(?:like|like\s+(?:this|the|that)\s+video|thumbs\s+up|hit\s+(?:the\s+)?like|smash\s+(?:the\s+)?like)$/.test(t)) return { intent: "MEDIA", slots: { action: "like" } };
  if (/^(?:dislike|dislike\s+(?:this|the|that)\s+video|thumbs\s+down|hit\s+(?:the\s+)?dislike)$/.test(t)) return { intent: "MEDIA", slots: { action: "dislike" } };
  if (/^(?:subscribe|sub|hit\s+subscribe|hit\s+(?:the\s+)?sub)$/.test(t)) return { intent: "MEDIA", slots: { action: "subscribe" } };

  // ── "play [something]" as search (e.g. "play Davido", "play lofi beats") ──
  var playSearch = t.match(/^play\s+(.+)$/);
  if (playSearch) {
    var what = playSearch[1].trim();
    if (!/^(?:the\s+)?(?:video|first|second|third|fourth|fifth|next|prev)/.test(what)) {
      return { intent: "SEARCH", slots: { query: what } };
    }
  }

  // ── Ordinal Selection ──
  var ordinalMatch = t.match(/^(?:watch|play|open|click|view|select|pick|choose|go\s+to)\s+(?:the\s+)?(\w+)\s+(video|product|result|link|item|article|image|song|post|story|reel|option|channel|playlist|movie|show|short|stream)s?$/);
  if (ordinalMatch) {
    var idx = parseOrdinal(ordinalMatch[1]);
    if (idx) return { intent: "PICK_NTH", slots: { index: idx, item_type: ordinalMatch[2] } };
  }
  var shortOrdinal = t.match(/^(?:the\s+)?(\w+)\s+(video|product|result|link|item|article|image|song|post|reel|option|channel|short|stream)s?$/);
  if (shortOrdinal) {
    var idx2 = parseOrdinal(shortOrdinal[1]);
    if (idx2) return { intent: "PICK_NTH", slots: { index: idx2, item_type: shortOrdinal[2] } };
  }
  var numMatch = t.match(/^(?:(?:result|option|item|product|video|link|number)\s+)?(?:number\s+)?(\d+)$/);
  if (numMatch && parseInt(numMatch[1]) <= 20) {
    return { intent: "PICK_NTH", slots: { index: parseInt(numMatch[1]), item_type: "auto" } };
  }

  // ── Tab Operations ──
  if (/^(open\s+)?(a\s+)?new\s+tab$/.test(t)) return { intent: "TAB_OP", slots: { action: "new" } };
  if (/^close(\s+this)?\s+tab$/.test(t)) return { intent: "TAB_OP", slots: { action: "close" } };
  if (/^reload(\s+(this\s+)?(tab|page))?$/.test(t) || t === "refresh") return { intent: "TAB_OP", slots: { action: "reload" } };
  if (/^go\s+back$/.test(t) || t === "back") return { intent: "TAB_OP", slots: { action: "back" } };
  if (/^go\s+forward$/.test(t) || t === "forward") return { intent: "TAB_OP", slots: { action: "forward" } };
  if (/^reopen\s+tab$/.test(t) || t === "undo close") return { intent: "TAB_OP", slots: { action: "reopen" } };
  var switchMatch2 = t.match(/^(?:switch\s+to\s+tab|tab)\s+(\d+)$/);
  if (switchMatch2) return { intent: "TAB_OP", slots: { action: "switch", tab_index: parseInt(switchMatch2[1]) } };

  // ── Navigation ──
  var goToMatch = t.match(/^(?:go\s+to|open|navigate\s+to|visit)\s+(.+)$/);
  if (goToMatch) {
    var dest = goToMatch[1].trim();
    var goOrd = dest.match(/^(?:the\s+)?(\w+)\s+(video|product|result|link|item|article|image|song|post|reel|option|channel|short|stream)s?$/);
    if (goOrd) {
      var gi = parseOrdinal(goOrd[1]);
      if (gi) return { intent: "PICK_NTH", slots: { index: gi, item_type: goOrd[2] } };
    }
    var url = dest;
    if (!/^https?:\/\//.test(url)) {
      if (/\.\w{2,}/.test(url)) url = "https://" + url;
      else url = "https://www.google.com/search?q=" + encodeURIComponent(url);
    }
    return { intent: "NAVIGATE", slots: { url: url } };
  }

  // ── Page Navigation ──
  if (/^next\s+page$/.test(t)) return { intent: "PAGE_NAV", slots: { direction: "next" } };
  if (/^(?:previous|prev)\s+page$/.test(t)) return { intent: "PAGE_NAV", slots: { direction: "prev" } };

  // ── Shopping ──
  if (/^add\s+to\s+cart$/.test(t)) return { intent: "CLICK_TARGET", slots: { target_text: "add to cart" } };
  if (/^(buy\s+now|checkout|check\s+out)$/.test(t)) return { intent: "CLICK_TARGET", slots: { target_text: t.replace(/\s+/g, " ") } };
  if (/^(add\s+to\s+wishlist|save\s+for\s+later|save\s+item)$/.test(t)) return { intent: "CLICK_TARGET", slots: { target_text: t } };

  // ── Search ──
  var searchMatch = t.match(/^(?:search|search\s+for|look\s+up|find)\s+(.+)$/);
  if (searchMatch) {
    var query = searchMatch[1].trim();
    var onPageMatch = query.match(/^(.+?)\s+on\s+(?:this\s+)?page$/);
    if (onPageMatch) return { intent: "FIND_ON_PAGE", slots: { text: onPageMatch[1] } };
    return { intent: "SEARCH", slots: { query: query } };
  }
  var googleMatch = t.match(/^google\s+(.+)$/);
  if (googleMatch) return { intent: "NAVIGATE", slots: { url: "https://www.google.com/search?q=" + encodeURIComponent(googleMatch[1].trim()) } };

  // ── Click ──
  var clickMatch = t.match(/^(?:click|tap|press|hit)\s+(?:on\s+)?(?:the\s+)?(.+)$/);
  if (clickMatch) return { intent: "CLICK_TARGET", slots: { target_text: clickMatch[1].trim() } };

  // ── Type ──
  var typeMatch = t.match(/^(?:type|enter|input|write)\s+"?([^"]+)"?(?:\s+(?:in|into|on)\s+(.+))?$/);
  if (typeMatch) return { intent: "TYPE_TEXT", slots: { text: typeMatch[1].trim(), field: (typeMatch[2] || "").trim() } };

  // ── Form ──
  if (/^submit(\s+form)?$/.test(t)) return { intent: "FORM", slots: { action: "submit" } };
  if (/^(clear|reset)(\s+form)?$/.test(t)) return { intent: "FORM", slots: { action: "clear" } };
  if (/^(next\s+field|tab\s+next)$/.test(t)) return { intent: "FORM", slots: { action: "next_field" } };

  // ── Select ──
  var selectMatch = t.match(/^select\s+"?([^"]+)"?(?:\s+(?:from|in)\s+(.+))?$/);
  if (selectMatch) return { intent: "SELECT_OPTION", slots: { option: selectMatch[1].trim(), target: (selectMatch[2] || "").trim() } };

  // ── Zoom ──
  if (/^zoom\s+in$/.test(t)) return { intent: "ZOOM", slots: { direction: "in" } };
  if (/^zoom\s+out$/.test(t)) return { intent: "ZOOM", slots: { direction: "out" } };
  if (/^(?:reset\s+zoom|zoom\s+reset|normal\s+zoom)$/.test(t)) return { intent: "ZOOM", slots: { direction: "reset" } };

  // ── System ──
  if (t === "stop" || t === "cancel" || t === "never mind" || t === "stop listening") return { intent: "SYSTEM", slots: { action: "cancel" } };

  if (t === "next" || t === "nexxt" || t === "skip") return { intent: "MEDIA", slots: { action: "next" } };

  return { intent: "UNKNOWN", slots: { text: t } };
}

// ══════════════════════════════════════
// COMMAND EXECUTOR
// ══════════════════════════════════════

function execute(command) {
  var intent = command.intent;
  var slots = command.slots;
  console.log("Executing:", intent, slots);

  if (intent === "TAB_OP") { handleTabOp(slots); return; }

  if (intent === "NAVIGATE") {
    chrome.tabs.query({ active: true, currentWindow: true }, function(tabs) {
      if (tabs[0]) chrome.tabs.update(tabs[0].id, { url: slots.url });
    });
    return;
  }

  if (intent === "SEARCH") {
    inject(function(query) {
      var selectors = [
        'input[type="search"]', 'input[name="search_query"]', 'input[name="q"]',
        'input[name="search"]', 'input[aria-label*="earch"]', 'input[placeholder*="earch"]',
        'input[title*="earch"]', 'textarea[name="q"]'
      ];
      var input = null;
      for (var i = 0; i < selectors.length; i++) {
        input = document.querySelector(selectors[i]);
        if (input) break;
      }
      if (input) {
        input.focus();
        input.value = query;
        input.dispatchEvent(new Event("input", { bubbles: true }));
        input.dispatchEvent(new Event("change", { bubbles: true }));
        var form = input.closest("form");
        if (form) { form.requestSubmit ? form.requestSubmit() : form.submit(); }
        else { input.dispatchEvent(new KeyboardEvent("keydown", { key: "Enter", code: "Enter", keyCode: 13, which: 13, bubbles: true })); }
        return { ok: true };
      }
      return { ok: false };
    }, [slots.query], function(result) {
      if (!result || !result.ok) {
        chrome.tabs.query({ active: true, currentWindow: true }, function(tabs) {
          if (tabs[0]) chrome.tabs.update(tabs[0].id, { url: "https://www.google.com/search?q=" + encodeURIComponent(slots.query) });
        });
      }
    });
    return;
  }

  if (intent === "SCROLL") {
    if (slots.direction === "top") {
      inject(function() { window.scrollTo({ top: 0, behavior: "smooth" }); }, []);
    } else if (slots.direction === "bottom") {
      inject(function() { window.scrollTo({ top: document.body.scrollHeight, behavior: "smooth" }); }, []);
    } else {
      inject(function(dir, amt) {
        var sizes = { small: 0.25, medium: 0.6, large: 1.2 };
        var px = window.innerHeight * (sizes[amt] || 0.6);
        var opts = { behavior: "smooth" };
        if (dir === "down") opts.top = px;
        else if (dir === "up") opts.top = -px;
        else if (dir === "right") opts.left = px;
        else if (dir === "left") opts.left = -px;
        window.scrollBy(opts);
      }, [slots.direction, slots.amount]);
    }
    return;
  }

  if (intent === "PICK_NTH") {
    inject(function(index, itemType) {
      var host = location.hostname;
      var items = [];
      if (host.includes("youtube.com")) {
        if (itemType === "video" || itemType === "auto") items = document.querySelectorAll("ytd-video-renderer a#video-title, ytd-rich-item-renderer a#video-title-link, ytd-compact-video-renderer a.yt-simple-endpoint, ytd-playlist-video-renderer a#video-title");
        else if (itemType === "channel") items = document.querySelectorAll("ytd-channel-renderer a.channel-link, #channel-name a");
        else if (itemType === "playlist") items = document.querySelectorAll("ytd-playlist-renderer a.ytd-playlist-renderer, ytd-grid-playlist-renderer a");
        else if (itemType === "short") items = document.querySelectorAll("ytd-reel-item-renderer a, ytd-rich-grid-slim-media a");
        else if (itemType === "stream") items = document.querySelectorAll("ytd-video-renderer a#video-title");
      }
      else if (host.includes("amazon.")) {
        if (itemType === "product" || itemType === "auto" || itemType === "item") items = document.querySelectorAll(".s-result-item:not(.AdHolder) h2 a, .s-card-container h2 a, [data-component-type='s-search-result'] h2 a");
      }
      else if (host.includes("google.com")) {
        if (itemType === "result" || itemType === "auto" || itemType === "link") {
          items = document.querySelectorAll("#search .g a h3, #rso .g a h3");
          var links = [];
          items.forEach(function(h3) { if (h3.closest("a")) links.push(h3.closest("a")); });
          items = links;
        }
      }
      else if (host.includes("instagram.com")) {
        if (itemType === "reel" || itemType === "short") items = document.querySelectorAll("a[href*='/reel/']");
        else if (itemType === "post" || itemType === "auto") items = document.querySelectorAll("article a[href*='/p/'], article a[href*='/reel/']");
      }
      else if (host.includes("reddit.com")) {
        if (itemType === "post" || itemType === "auto") items = document.querySelectorAll("a[data-click-id='body'], shreddit-post a[slot='full-post-link'], article h3 a, shreddit-post h3 a");
      }
      if (!items || items.length === 0) {
        if (itemType === "video") items = document.querySelectorAll("a[href*='watch'], a[href*='video']");
        else if (itemType === "product" || itemType === "item") items = document.querySelectorAll(".product a, [class*='product'] a, article a");
        else if (itemType === "result" || itemType === "link") items = document.querySelectorAll("main a[href]:not([href='#']):not([role='button'])");
        else if (itemType === "image") items = document.querySelectorAll("main img[src], article img[src]");
        else if (itemType === "article" || itemType === "post") items = document.querySelectorAll("article a, h2 a, h3 a");
        else items = document.querySelectorAll("main a[href]:not([href='#']):not([aria-hidden]), article a, .card a, li > a, h2 a, h3 a");
      }
      var visible = [];
      items.forEach(function(el) { var r = el.getBoundingClientRect(); if (r.width > 0 && r.height > 0) visible.push(el); });
      var seen = {}, unique = [];
      visible.forEach(function(el) { var k = el.href || el.src || el.innerText; if (k && !seen[k]) { seen[k] = true; unique.push(el); } });
      var target = index === -1 ? unique[unique.length - 1] : unique[index - 1];
      if (target) {
        target.scrollIntoView({ behavior: "smooth", block: "center" });
        var orig = target.style.outline;
        target.style.outline = "3px solid #6c5ce7";
        setTimeout(function() { target.style.outline = orig; target.click(); }, 400);
        return { ok: true, text: (target.innerText || target.alt || "").substring(0, 80), total: unique.length };
      }
      return { ok: false, total: unique.length };
    }, [slots.index, slots.item_type], function(result) {
      if (result && !result.ok) speak("Could not find item number " + slots.index);
    });
    return;
  }

  // ══════════════════════════════════════
  // MEDIA — massively expanded for YouTube
  // ══════════════════════════════════════
  if (intent === "MEDIA") {
    inject(function(action, extra) {
      var host = location.hostname;
      var video = document.querySelector("video");
      var isYT = host.includes("youtube.com");

      // ── Instagram Reels: next / previous (ArrowDown / ArrowUp) ──
      if (host.includes("instagram.com") && (action === "next" || action === "previous")) {
        var pth = location.pathname || "";
        if (/\/reels\/?$/i.test(pth) || /\/reel\//i.test(pth)) {
          function igKey(k, code, kc) {
            var vid = document.querySelector("main video, section video, article video, video");
            if (vid) { try { vid.focus(); } catch (e1) {} }
            var ev = { key: k, code: code, keyCode: kc, bubbles: true, cancelable: true, view: window };
            document.dispatchEvent(new KeyboardEvent("keydown", ev));
            document.dispatchEvent(new KeyboardEvent("keyup", ev));
          }
          if (action === "next") {
            igKey("ArrowDown", "ArrowDown", 40);
            return { ok: true };
          }
          igKey("ArrowUp", "ArrowUp", 38);
          return { ok: true };
        }
      }

      // ── Reddit: MEDIA like / dislike → vote buttons ──
      if (host.includes("reddit.com")) {
        if (action === "like") {
          var upBt = document.querySelector("shreddit-post button[aria-label*='Upvote' i], faceplate-numbered-button button[aria-label*='Upvote' i], button[aria-label^='Upvote']");
          if (upBt) { upBt.click(); return { ok: true }; }
        }
        if (action === "dislike") {
          var dnBt = document.querySelector("shreddit-post button[aria-label*='Downvote' i], button[aria-label^='Downvote']");
          if (dnBt) { dnBt.click(); return { ok: true }; }
        }
      }

      // ── Instagram: MEDIA like / dislike on posts ──
      if (host.includes("instagram.com")) {
        if (action === "like") {
          var igArt = document.querySelector("article");
          var likeSvg = igArt ? igArt.querySelector("svg[aria-label='Like']") : document.querySelector("svg[aria-label='Like']");
          if (likeSvg) {
            var lb = likeSvg.closest("button") || likeSvg.closest("[role='button']");
            if (lb) { lb.click(); return { ok: true }; }
          }
        }
        if (action === "dislike") {
          var unSvg = document.querySelector("article svg[aria-label='Unlike']") || document.querySelector("svg[aria-label='Unlike']");
          if (unSvg) {
            var ub = unSvg.closest("button") || unSvg.closest("[role='button']");
            if (ub) { ub.click(); return { ok: true }; }
          }
        }
      }

      // Helper: simulate keypress on the YouTube player
      function ytKey(key, opts) {
        var player = document.querySelector("#movie_player") || document.querySelector(".html5-video-player") || document.body;
        player.focus();
        var e = new KeyboardEvent("keydown", Object.assign({ key: key, code: "Key" + key.toUpperCase(), bubbles: true, cancelable: true }, opts || {}));
        player.dispatchEvent(e);
      }

      // Helper: click YouTube settings menu item
      function ytSettingsClick(menuLabel, optionText, cb) {
        var gear = document.querySelector(".ytp-settings-button");
        if (!gear) return false;
        gear.click();
        setTimeout(function() {
          var items = document.querySelectorAll(".ytp-settings-menu .ytp-menuitem");
          for (var i = 0; i < items.length; i++) {
            var label = items[i].querySelector(".ytp-menuitem-label");
            if (label && label.textContent.toLowerCase().includes(menuLabel.toLowerCase())) {
              items[i].click();
              setTimeout(function() {
                if (cb) cb();
              }, 200);
              return;
            }
          }
          gear.click();
        }, 200);
        return true;
      }

      // ── YouTube-specific actions ──
      if (isYT) {
        if (action === "like") { var b = document.querySelector("like-button-view-model button, #segmented-like-button button, ytd-toggle-button-renderer #button[aria-label*='like' i]"); if (b) { b.click(); return { ok: true }; } }
        if (action === "dislike") { var b2 = document.querySelector("dislike-button-view-model button, #segmented-dislike-button button"); if (b2) { b2.click(); return { ok: true }; } }
        if (action === "subscribe") { var b3 = document.querySelector("#subscribe-button button, ytd-subscribe-button-renderer button"); if (b3 && !b3.hasAttribute("subscribed")) { b3.click(); return { ok: true }; } return { ok: true }; }
        if (action === "next") { var b4 = document.querySelector(".ytp-next-button"); if (b4) { b4.click(); return { ok: true }; } }
        if (action === "previous") { var b5 = document.querySelector(".ytp-prev-button"); if (b5 && b5.style.display !== "none") { b5.click(); return { ok: true }; } if (video) { video.currentTime = 0; return { ok: true }; } }

        // Speed
        if (action === "speed_up") { if (video) { video.playbackRate = Math.min(16, video.playbackRate + 0.25); return { ok: true, speed: video.playbackRate }; } }
        if (action === "speed_down") { if (video) { video.playbackRate = Math.max(0.25, video.playbackRate - 0.25); return { ok: true, speed: video.playbackRate }; } }
        if (action === "set_speed") { if (video) { video.playbackRate = extra; return { ok: true, speed: video.playbackRate }; } }

        // Captions
        if (action === "captions_on" || action === "captions_off" || action === "captions_toggle") {
          var ccBtn = document.querySelector(".ytp-subtitles-button");
          if (ccBtn) {
            var isOn = ccBtn.getAttribute("aria-pressed") === "true";
            if (action === "captions_on" && !isOn) ccBtn.click();
            else if (action === "captions_off" && isOn) ccBtn.click();
            else if (action === "captions_toggle") ccBtn.click();
            return { ok: true };
          }
        }

        // Theater / Mini player
        if (action === "theater") { var tb = document.querySelector(".ytp-size-button"); if (tb) { tb.click(); return { ok: true }; } }
        if (action === "miniplayer") { var mb = document.querySelector(".ytp-miniplayer-button"); if (mb) { mb.click(); return { ok: true }; } }
        if (action === "exit_miniplayer" || action === "default_view") {
          var expandBtn = document.querySelector(".ytp-miniplayer-expand-watch-page-button");
          if (expandBtn) { expandBtn.click(); return { ok: true }; }
          var sizeBtn = document.querySelector(".ytp-size-button");
          if (sizeBtn) {
            var isTheater = document.querySelector("ytd-watch-flexy[theater]");
            if (isTheater) { sizeBtn.click(); return { ok: true }; }
          }
          return { ok: true };
        }

        // Volume
        if (action === "volume_up") { if (video) { video.volume = Math.min(1, video.volume + 0.1); video.muted = false; return { ok: true, volume: Math.round(video.volume * 100) }; } }
        if (action === "volume_down") { if (video) { video.volume = Math.max(0, video.volume - 0.1); return { ok: true, volume: Math.round(video.volume * 100) }; } }
        if (action === "set_volume") { if (video) { video.volume = Math.max(0, Math.min(1, extra / 100)); video.muted = false; return { ok: true, volume: extra }; } }

        // Quality
        if (action === "set_quality") {
          var qualityTarget = extra;
          ytSettingsClick("quality", null, function() {
            setTimeout(function() {
              var opts = document.querySelectorAll(".ytp-quality-menu .ytp-menuitem, .ytp-panel-menu .ytp-menuitem");
              if (!opts.length) opts = document.querySelectorAll(".ytp-menuitem");
              var picked = null;
              if (qualityTarget === "max") { picked = opts[0]; }
              else if (qualityTarget === "min") { picked = opts[opts.length - 1]; }
              else if (qualityTarget === "auto") { picked = opts[opts.length - 1]; }
              else {
                var num = qualityTarget.replace("p", "");
                for (var i = 0; i < opts.length; i++) {
                  var txt = opts[i].textContent || "";
                  if (txt.includes(num)) { picked = opts[i]; break; }
                }
              }
              if (picked) picked.click();
              else if (opts.length) {
                var gear2 = document.querySelector(".ytp-settings-button");
                if (gear2) gear2.click();
              }
            }, 200);
          });
          return { ok: true };
        }

        // Seek to timestamp
        if (action === "seek") { if (video) { video.currentTime = Math.max(0, Math.min(video.duration || 99999, extra)); return { ok: true, time: extra }; } }
        if (action === "restart") { if (video) { video.currentTime = 0; return { ok: true }; } }
        if (action === "seek_end") { if (video && video.duration) { video.currentTime = video.duration - 1; return { ok: true }; } }

        // Loop
        if (action === "loop") { if (video) { video.loop = true; return { ok: true }; } }
        if (action === "unloop") { if (video) { video.loop = false; return { ok: true }; } }
      }

      // ── Generic media (any site) ──
      if (!video) video = document.querySelector("audio");
      if (video) {
        if (action === "play") { video.play(); return { ok: true }; }
        if (action === "pause") { video.pause(); return { ok: true }; }
        if (action === "toggle") { video.paused ? video.play() : video.pause(); return { ok: true }; }
        if (action === "mute") { video.muted = true; return { ok: true }; }
        if (action === "unmute") { video.muted = false; return { ok: true }; }
        if (action === "fullscreen") { (video.requestFullscreen || video.webkitRequestFullscreen).call(video); return { ok: true }; }
        if (action === "exit_fullscreen") { document.exitFullscreen && document.exitFullscreen(); return { ok: true }; }
        if (action === "skip") { video.currentTime = Math.min(video.duration, video.currentTime + (extra || 10)); return { ok: true }; }
        if (action === "rewind") { video.currentTime = Math.max(0, video.currentTime - (extra || 10)); return { ok: true }; }
        if (action === "speed_up") { video.playbackRate = Math.min(16, video.playbackRate + 0.25); return { ok: true, speed: video.playbackRate }; }
        if (action === "speed_down") { video.playbackRate = Math.max(0.25, video.playbackRate - 0.25); return { ok: true, speed: video.playbackRate }; }
        if (action === "set_speed") { video.playbackRate = extra; return { ok: true, speed: video.playbackRate }; }
        if (action === "volume_up") { video.volume = Math.min(1, video.volume + 0.1); video.muted = false; return { ok: true }; }
        if (action === "volume_down") { video.volume = Math.max(0, video.volume - 0.1); return { ok: true }; }
        if (action === "set_volume") { video.volume = Math.max(0, Math.min(1, extra / 100)); video.muted = false; return { ok: true }; }
        if (action === "seek") { video.currentTime = Math.max(0, Math.min(video.duration || 99999, extra)); return { ok: true }; }
        if (action === "restart") { video.currentTime = 0; return { ok: true }; }
        if (action === "loop") { video.loop = true; return { ok: true }; }
        if (action === "unloop") { video.loop = false; return { ok: true }; }
      }
      var playBtn = document.querySelector("[aria-label*='Play' i], .play-button");
      if (playBtn && (action === "play" || action === "toggle")) { playBtn.click(); return { ok: true }; }
      var pauseBtn = document.querySelector("[aria-label*='Pause' i], .pause-button");
      if (pauseBtn && (action === "pause" || action === "toggle")) { pauseBtn.click(); return { ok: true }; }
      return { ok: false };
    }, [slots.action, slots.seconds || slots.speed || slots.volume || slots.quality || slots.time || 0], function(result) {
      if (result && !result.ok) speak("No media found");
      else if (result && result.speed) speak("Speed " + result.speed + "x");
      else if (result && result.volume !== undefined) speak("Volume " + result.volume + "%");
    });
    return;
  }

  // ══════════════════════════════════════
  // REDDIT_ACTION
  // ══════════════════════════════════════
  if (intent === "REDDIT_ACTION") {
    inject(function(action, body) {
      var host = location.hostname;
      if (!host.includes("reddit.com")) return { ok: false, msg: "Not on Reddit" };
      var post = document.querySelector("shreddit-post") || document.querySelector("article") || document;
      function clickIn(root, sels) {
        for (var i = 0; i < sels.length; i++) {
          var el = root.querySelector(sels[i]);
          if (el) { el.click(); return true; }
        }
        return false;
      }
      if (action === "upvote") {
        if (clickIn(post, ["button[aria-label*='Upvote' i]", "faceplate-numbered-button button[aria-label*='Upvote' i]"])) return { ok: true };
        return { ok: false };
      }
      if (action === "downvote") {
        if (clickIn(post, ["button[aria-label*='Downvote' i]"])) return { ok: true };
        return { ok: false };
      }
      if (action === "save_post") {
        if (clickIn(post, ["button[aria-label*='Save' i]", "button[aria-label^='Save' i]"])) return { ok: true };
        return { ok: false };
      }
      if (action === "share") {
        if (clickIn(post, ["button[aria-label*='Share' i]", "share-button button"])) return { ok: true };
        return { ok: false };
      }
      if (action === "open_comments") {
        if (clickIn(post, ["a[href*='comment']", "button[aria-label*='Comment' i]", "[slot='comment-button'] button"])) return { ok: true };
        return { ok: false };
      }
      if (action === "scroll_next") {
        window.scrollBy({ top: Math.min(window.innerHeight * 0.92, 720), behavior: "smooth" });
        return { ok: true };
      }
      if (action === "scroll_prev") {
        window.scrollBy({ top: -Math.min(window.innerHeight * 0.92, 720), behavior: "smooth" });
        return { ok: true };
      }
      if (action === "join") {
        if (clickIn(document, ["button[aria-label*='Join' i]", "subscribe-button button", "shreddit-join-button button"])) return { ok: true };
        return { ok: false };
      }
      if (action === "reply_compose" && body) {
        var ta0 = document.querySelector("shreddit-composer textarea, faceplate-textarea-input textarea");
        if (!ta0) clickIn(post, ["button[aria-label*='Comment' i]", "[slot='comment-button'] button"]);
        setTimeout(function() {
          var ta = document.querySelector("shreddit-composer textarea, faceplate-textarea-input textarea, textarea[placeholder*='What' i]");
          if (ta) { ta.focus(); ta.value = body; ta.dispatchEvent(new Event("input", { bubbles: true })); }
        }, 400);
        return { ok: true };
      }
      return { ok: false };
    }, [slots.action, slots.text || ""], function(result) {
      if (result && !result.ok && result.msg) speak(result.msg);
      else if (result && !result.ok) speak("Could not do that on Reddit");
    });
    return;
  }

  // ══════════════════════════════════════
  // INSTAGRAM_ACTION
  // ══════════════════════════════════════
  if (intent === "INSTAGRAM_ACTION") {
    inject(function(action, body) {
      var host = location.hostname;
      if (!host.includes("instagram.com")) return { ok: false, msg: "Not on Instagram" };
      var art = document.querySelector("article");
      function clickSvg(label) {
        var svg = (art || document).querySelector("svg[aria-label='" + label + "']") || document.querySelector("svg[aria-label='" + label + "']");
        if (!svg) return false;
        var btn = svg.closest("button") || svg.closest("[role='button']");
        if (btn) { btn.click(); return true; }
        return false;
      }
      if (action === "like") {
        if (clickSvg("Like")) return { ok: true };
        return { ok: false };
      }
      if (action === "unlike") {
        if (clickSvg("Unlike")) return { ok: true };
        return { ok: false };
      }
      if (action === "save") {
        if (clickSvg("Save")) return { ok: true };
        if (clickSvg("Remove")) return { ok: true };
        return { ok: false };
      }
      if (action === "unsave") {
        if (clickSvg("Remove")) return { ok: true };
        return { ok: false };
      }
      if (action === "share") {
        if (clickSvg("Share")) return { ok: true };
        return { ok: false };
      }
      if (action === "focus_comments") {
        var cSvg = (art || document).querySelector("svg[aria-label*='Comment' i]");
        if (cSvg) {
          var cb = cSvg.closest("button") || cSvg.closest("[role='button']");
          if (cb) { cb.click(); return { ok: true }; }
        }
        var cta = document.querySelector("article textarea[placeholder*='comment' i], article textarea[placeholder*='Add' i]");
        if (cta) { cta.scrollIntoView({ behavior: "smooth", block: "center" }); cta.focus(); return { ok: true }; }
        return { ok: false };
      }
      if (action === "compose_comment" && body) {
        var t0 = document.querySelector("article textarea[placeholder*='comment' i], article textarea[placeholder*='Add' i]");
        if (!t0 && art) {
          var cs = art.querySelector("svg[aria-label*='Comment' i]");
          if (cs) { var bx = cs.closest("button") || cs.closest("[role='button']"); if (bx) bx.click(); }
        }
        setTimeout(function() {
          var t1 = document.querySelector("article textarea[placeholder*='comment' i], article textarea[placeholder*='Add' i]");
          if (t1) { t1.focus(); t1.value = body; t1.dispatchEvent(new Event("input", { bubbles: true })); }
        }, 350);
        return { ok: true };
      }
      function igReelsAdvance(down) {
        var vid = document.querySelector("main video, section video, article video, video");
        if (vid) try { vid.focus(); } catch (e2) {}
        var key = down ? "ArrowDown" : "ArrowUp";
        var kc = down ? 40 : 38;
        document.dispatchEvent(new KeyboardEvent("keydown", { key: key, code: key, keyCode: kc, bubbles: true, cancelable: true }));
        document.dispatchEvent(new KeyboardEvent("keyup", { key: key, code: key, keyCode: kc, bubbles: true }));
        return true;
      }
      if (action === "next_story") {
        var pathN = location.pathname || "";
        if (/\/reels\/?$/i.test(pathN) || /\/reel\//i.test(pathN)) {
          igReelsAdvance(true);
          return { ok: true };
        }
        var nx = document.querySelector("button[aria-label*='Next' i], div[role='button'][aria-label*='Next' i]");
        if (nx) { nx.click(); return { ok: true }; }
        document.dispatchEvent(new KeyboardEvent("keydown", { key: "ArrowRight", code: "ArrowRight", keyCode: 39, bubbles: true }));
        return { ok: true };
      }
      if (action === "prev_story") {
        var pathP = location.pathname || "";
        if (/\/reels\/?$/i.test(pathP) || /\/reel\//i.test(pathP)) {
          igReelsAdvance(false);
          return { ok: true };
        }
        var pr = document.querySelector("button[aria-label*='Previous' i], div[role='button'][aria-label*='Previous' i]");
        if (pr) { pr.click(); return { ok: true }; }
        document.dispatchEvent(new KeyboardEvent("keydown", { key: "ArrowLeft", code: "ArrowLeft", keyCode: 37, bubbles: true }));
        return { ok: true };
      }
      return { ok: false };
    }, [slots.action, slots.text || ""], function(result) {
      if (result && !result.ok && result.msg) speak(result.msg);
      else if (result && !result.ok) speak("Could not do that on Instagram");
    });
    return;
  }

  // ══════════════════════════════════════
  // YT_ACTION — YouTube-specific non-media actions
  // ══════════════════════════════════════
  if (intent === "YT_ACTION") {
    inject(function(action, extra, commentText) {
      var host = location.hostname;
      if (!host.includes("youtube.com")) return { ok: false, msg: "Not on YouTube" };

      // ── Channel navigation ──
      if (action === "go_to_channel") {
        var ch = document.querySelector("#channel-name a, ytd-video-owner-renderer a, #owner a");
        if (ch) { ch.click(); return { ok: true }; }
        return { ok: false };
      }
      if (action === "my_channel") {
        var avatar = document.querySelector("#avatar-btn button, button#avatar-btn, img.yt-spec-avatar-shape--avatar");
        if (avatar) {
          avatar.click();
          setTimeout(function() {
            var channelLink = document.querySelector("a[href*='/channel/'], a[href*='/@'], #header a.yt-simple-endpoint");
            if (channelLink) channelLink.click();
          }, 500);
        }
        return { ok: true };
      }
      if (action === "notifications") {
        var bell = document.querySelector("#notification-button button, ytd-notification-topbar-button-renderer button, button[aria-label*='Notification' i]");
        if (bell) { bell.click(); return { ok: true }; }
        return { ok: false };
      }
      if (action === "channel_tab") {
        var tabLinks = document.querySelectorAll("yt-tab-shape a, #tabsContent tp-yt-paper-tab, .tab-content, [role='tab']");
        var tabMap = { "videos": "videos", "shorts": "shorts", "playlists": "playlists", "community": "community", "streams": "streams", "live": "streams", "about": "about" };
        var target = tabMap[extra] || extra;
        for (var i = 0; i < tabLinks.length; i++) {
          var txt = (tabLinks[i].textContent || tabLinks[i].getAttribute("aria-label") || "").toLowerCase().trim();
          if (txt.includes(target)) { tabLinks[i].click(); return { ok: true }; }
        }
        var currentUrl = location.href;
        if (currentUrl.includes("/@") || currentUrl.includes("/channel/") || currentUrl.includes("/c/")) {
          var baseUrl = currentUrl.replace(/\/(videos|shorts|playlists|community|streams|about|featured).*$/, "");
          location.href = baseUrl + "/" + target;
          return { ok: true };
        }
        return { ok: false };
      }

      // ── Autoplay ──
      if (action === "autoplay_on" || action === "autoplay_off" || action === "autoplay_toggle") {
        var apBtn = document.querySelector(".ytp-autonav-toggle-button, button[data-tooltip-target-id='ytp-autonav-toggle-button']");
        if (apBtn) {
          var isOn = apBtn.getAttribute("aria-checked") === "true" || apBtn.closest("[aria-checked='true']");
          if (action === "autoplay_on" && !isOn) apBtn.click();
          else if (action === "autoplay_off" && isOn) apBtn.click();
          else if (action === "autoplay_toggle") apBtn.click();
          return { ok: true };
        }
        return { ok: false };
      }

      // ── Ambient mode ──
      if (action === "ambient_on" || action === "ambient_off" || action === "ambient_toggle") {
        var gear = document.querySelector(".ytp-settings-button");
        if (gear) {
          gear.click();
          setTimeout(function() {
            var items = document.querySelectorAll(".ytp-settings-menu .ytp-menuitem, .ytp-menuitem");
            for (var j = 0; j < items.length; j++) {
              var lbl = (items[j].textContent || "").toLowerCase();
              if (lbl.includes("ambient")) {
                items[j].click();
                return;
              }
            }
            gear.click();
          }, 300);
        }
        return { ok: true };
      }

      // ── Annotations ──
      if (action === "annotations_on" || action === "annotations_off") {
        var gear2 = document.querySelector(".ytp-settings-button");
        if (gear2) {
          gear2.click();
          setTimeout(function() {
            var items2 = document.querySelectorAll(".ytp-menuitem");
            for (var k = 0; k < items2.length; k++) {
              if ((items2[k].textContent || "").toLowerCase().includes("annotation")) {
                items2[k].click();
                return;
              }
            }
            gear2.click();
          }, 300);
        }
        return { ok: true };
      }

      // ── Share ──
      if (action === "share") {
        var shareBtn = document.querySelector("button[aria-label*='Share' i], ytd-button-renderer:has(yt-icon) button[aria-label*='Share' i], #top-level-buttons-computed ytd-button-renderer button[aria-label*='share' i]");
        if (!shareBtn) {
          var btns = document.querySelectorAll("ytd-button-renderer button, yt-button-renderer button");
          for (var s = 0; s < btns.length; s++) {
            if ((btns[s].textContent || btns[s].getAttribute("aria-label") || "").toLowerCase().includes("share")) { shareBtn = btns[s]; break; }
          }
        }
        if (shareBtn) { shareBtn.click(); return { ok: true }; }
        return { ok: false };
      }

      // ── Save / Watch Later ──
      if (action === "save_watch_later") {
        var saveBtn = document.querySelector("button[aria-label*='Save' i], ytd-button-renderer button[aria-label*='save' i]");
        if (!saveBtn) {
          var btns2 = document.querySelectorAll("ytd-button-renderer button, yt-button-renderer button");
          for (var sv = 0; sv < btns2.length; sv++) {
            if ((btns2[sv].textContent || "").toLowerCase().includes("save")) { saveBtn = btns2[sv]; break; }
          }
        }
        if (saveBtn) {
          saveBtn.click();
          setTimeout(function() {
            var wlOption = document.querySelector("ytd-playlist-add-to-option-renderer tp-yt-paper-checkbox");
            if (wlOption && !wlOption.checked) wlOption.click();
          }, 500);
          return { ok: true };
        }
        return { ok: false };
      }

      // ── Add to playlist ──
      if (action === "add_to_playlist") {
        var saveBtn2 = document.querySelector("button[aria-label*='Save' i]");
        if (saveBtn2) { saveBtn2.click(); return { ok: true }; }
        return { ok: false };
      }

      // ── Description ──
      if (action === "open_description") {
        var descExpand = document.querySelector("#expand, tp-yt-paper-button#expand, #description-inline-expander #expand, ytd-text-inline-expander #expand");
        if (!descExpand) descExpand = document.querySelector("tp-yt-paper-button#more, #more");
        if (descExpand) { descExpand.click(); return { ok: true }; }
        var descArea = document.querySelector("#description, ytd-expander[collapsed] tp-yt-paper-button");
        if (descArea) { descArea.click(); return { ok: true }; }
        return { ok: false };
      }
      if (action === "close_description") {
        var descCollapse = document.querySelector("#collapse, tp-yt-paper-button#collapse, #description-inline-expander #collapse");
        if (!descCollapse) descCollapse = document.querySelector("tp-yt-paper-button#less, #less");
        if (descCollapse) { descCollapse.click(); return { ok: true }; }
        return { ok: false };
      }

      // ── Transcript ──
      if (action === "open_transcript") {
        var moreBtn = document.querySelector("#primary-button ytd-button-renderer button, button[aria-label*='more' i]");
        var btns3 = document.querySelectorAll("ytd-menu-renderer ytd-button-renderer button, ytd-menu-renderer yt-button-renderer button");
        for (var tr = 0; tr < btns3.length; tr++) {
          if ((btns3[tr].textContent || btns3[tr].getAttribute("aria-label") || "").toLowerCase().includes("transcript")) {
            btns3[tr].click(); return { ok: true };
          }
        }
        var dots = document.querySelector("#button-shape button[aria-label='More actions'], ytd-menu-renderer button[aria-label='More actions']");
        if (dots) {
          dots.click();
          setTimeout(function() {
            var menuItems = document.querySelectorAll("ytd-menu-service-item-renderer, tp-yt-paper-listbox ytd-menu-service-item-renderer");
            for (var ti = 0; ti < menuItems.length; ti++) {
              if ((menuItems[ti].textContent || "").toLowerCase().includes("transcript")) {
                menuItems[ti].click(); return;
              }
            }
          }, 400);
        }
        return { ok: true };
      }
      if (action === "close_transcript") {
        var closeBtn = document.querySelector("ytd-engagement-panel-section-list-renderer[target-id*='transcript'] #visibility-button button");
        if (closeBtn) { closeBtn.click(); return { ok: true }; }
        return { ok: false };
      }

      // ── Clip ──
      if (action === "clip") {
        var clipBtns = document.querySelectorAll("ytd-button-renderer button, yt-button-renderer button");
        for (var cl = 0; cl < clipBtns.length; cl++) {
          if ((clipBtns[cl].textContent || clipBtns[cl].getAttribute("aria-label") || "").toLowerCase().includes("clip")) {
            clipBtns[cl].click(); return { ok: true };
          }
        }
        return { ok: false };
      }

      // ── Download ──
      if (action === "download") {
        var dlBtns = document.querySelectorAll("ytd-button-renderer button, ytd-download-button-renderer button, yt-button-renderer button");
        for (var dl = 0; dl < dlBtns.length; dl++) {
          if ((dlBtns[dl].textContent || dlBtns[dl].getAttribute("aria-label") || "").toLowerCase().includes("download")) {
            dlBtns[dl].click(); return { ok: true };
          }
        }
        return { ok: false };
      }

      // ── Report / Not interested ──
      if (action === "report" || action === "not_interested" || action === "dont_recommend_channel") {
        var moreActions = document.querySelector("ytd-menu-renderer button[aria-label='More actions'], #button-shape button[aria-label='More actions']");
        if (moreActions) {
          moreActions.click();
          setTimeout(function() {
            var menuItems2 = document.querySelectorAll("ytd-menu-service-item-renderer, tp-yt-paper-listbox ytd-menu-service-item-renderer");
            var searchText = action === "report" ? "report" : action === "not_interested" ? "not interested" : "don't recommend";
            for (var ni = 0; ni < menuItems2.length; ni++) {
              if ((menuItems2[ni].textContent || "").toLowerCase().includes(searchText)) {
                menuItems2[ni].click(); return;
              }
            }
          }, 400);
        }
        return { ok: true };
      }

      // ── Unlike / Unsubscribe ──
      if (action === "unlike") {
        var likeBtn = document.querySelector("like-button-view-model button[aria-pressed='true'], #segmented-like-button button[aria-pressed='true']");
        if (likeBtn) { likeBtn.click(); return { ok: true }; }
        return { ok: false };
      }
      if (action === "unsubscribe") {
        var subBtn = document.querySelector("#subscribe-button button[subscribed], ytd-subscribe-button-renderer button[aria-label*='Unsubscribe' i]");
        if (subBtn) { subBtn.click(); return { ok: true }; }
        return { ok: false };
      }

      // ── Comments ──
      if (action === "show_comments") {
        var commentsSection = document.querySelector("ytd-comments#comments, #comments");
        if (commentsSection) { commentsSection.scrollIntoView({ behavior: "smooth", block: "start" }); return { ok: true }; }
        return { ok: false };
      }
      if (action === "sort_comments") {
        var sortMenu = document.querySelector("ytd-comments-header-renderer #sort-menu tp-yt-paper-dropdown-menu, ytd-comments-header-renderer yt-sort-filter-sub-menu-renderer");
        if (sortMenu) {
          sortMenu.click();
          setTimeout(function() {
            var opts = document.querySelectorAll("tp-yt-paper-listbox a, tp-yt-paper-item, yt-dropdown-menu tp-yt-paper-item");
            var target2 = extra === "top" ? 0 : 1;
            if (opts[target2]) opts[target2].click();
          }, 300);
          return { ok: true };
        }
        return { ok: false };
      }
      if (action === "add_comment") {
        var body = (commentText && String(commentText).trim()) || "";
        var commentBox = document.querySelector("#placeholder-area, #simplebox-placeholder, ytd-comment-simplebox-renderer #placeholder-area");
        if (commentBox) commentBox.click();
        if (!body) return { ok: !!commentBox };
        function fillYtComment(attempt) {
          var root = document.querySelector("ytd-comment-simplebox-renderer #contenteditable-root") ||
            document.querySelector("#simplebox #contenteditable-root") ||
            document.querySelector("#contenteditable-root[contenteditable='true']") ||
            document.querySelector("ytd-commentbox-web #contenteditable-root");
          if (root && root.getAttribute("contenteditable") === "true") {
            root.focus();
            root.textContent = body;
            root.dispatchEvent(new InputEvent("input", { bubbles: true, inputType: "insertText", data: body }));
            return true;
          }
          if (attempt < 8) setTimeout(function() { fillYtComment(attempt + 1); }, 200);
          return false;
        }
        setTimeout(function() { fillYtComment(0); }, 150);
        return { ok: true };
      }

      // ── Shorts ──
      if (action === "next_short") {
        var nextShort = document.querySelector("#navigation-button-down button, [aria-label*='Next video' i]");
        if (nextShort) { nextShort.click(); return { ok: true }; }
        return { ok: false };
      }
      if (action === "prev_short") {
        var prevShort = document.querySelector("#navigation-button-up button, [aria-label*='Previous video' i]");
        if (prevShort) { prevShort.click(); return { ok: true }; }
        return { ok: false };
      }

      // ── Search Filters ──
      if (action === "filter") {
        var filterBtn = document.querySelector("#filter-button button, tp-yt-paper-button[aria-label='Search filters']");
        if (filterBtn) {
          filterBtn.click();
          setTimeout(function() {
            var filterLinks = document.querySelectorAll("ytd-search-filter-renderer a, ytd-search-filter-group-renderer a");
            var filterType = extra.filter_type;
            var filterVal = extra.value;
            var labelMap = {
              "date": { "today": "last hour|today", "week": "this week", "month": "this month", "year": "this year" },
              "duration": { "short": "under 4|short", "medium": "4.*20|medium", "long": "over 20|long" },
              "sort": { "date": "upload date", "views": "view count", "rating": "rating", "relevance": "relevance" },
              "feature": { "hd": "hd", "4k": "4k", "subtitles": "subtitles|cc" },
              "type": { "live": "live" }
            };
            var pattern = (labelMap[filterType] && labelMap[filterType][filterVal]) || filterVal;
            var re = new RegExp(pattern, "i");
            for (var fi = 0; fi < filterLinks.length; fi++) {
              if (re.test(filterLinks[fi].textContent || "")) {
                filterLinks[fi].click();
                return;
              }
            }
          }, 400);
        }
        return { ok: true };
      }
      if (action === "clear_filters") {
        var url = new URL(location.href);
        var sp = url.searchParams.get("search_query") || url.searchParams.get("q");
        if (sp) { location.href = "/results?search_query=" + encodeURIComponent(sp); }
        return { ok: true };
      }

      return { ok: false };
    }, [slots.action, (slots.sort || slots.tab || slots.filter_type) ? slots : (slots.value || 0), slots.text || ""], function(result) {
      if (result && !result.ok && result.msg) speak(result.msg);
      else if (result && !result.ok) speak("Could not do that on this page");
    });
    return;
  }

  // ── Page Navigation ──
  if (intent === "PAGE_NAV") {
    inject(function(direction) {
      var selectors = direction === "next"
        ? ["a[aria-label*='Next' i]", "a.next", ".next a", "[class*='next'] a", "a[rel='next']", "#pnnext"]
        : ["a[aria-label*='Prev' i]", "a.prev", ".prev a", "[class*='prev'] a", "a[rel='prev']", "#pnprev"];
      for (var i = 0; i < selectors.length; i++) {
        var el = document.querySelector(selectors[i]);
        if (el) { el.click(); return { ok: true }; }
      }
      return { ok: false };
    }, [slots.direction], function(result) {
      if (result && !result.ok) speak("Could not find " + slots.direction + " page button");
    });
    return;
  }

  if (intent === "FORM") {
    inject(function(action) {
      if (action === "submit") {
        var form = document.activeElement && document.activeElement.closest("form") || document.querySelector("form");
        if (form) { form.requestSubmit ? form.requestSubmit() : form.submit(); return { ok: true }; }
      }
      if (action === "clear") { var f = document.querySelector("form"); if (f) { f.reset(); return { ok: true }; } }
      if (action === "next_field") {
        var inputs = Array.from(document.querySelectorAll("input:not([type='hidden']), textarea, select"));
        var idx = inputs.indexOf(document.activeElement);
        if (idx >= 0 && idx < inputs.length - 1) { inputs[idx + 1].focus(); return { ok: true }; }
        else if (inputs.length) { inputs[0].focus(); return { ok: true }; }
      }
      return { ok: false };
    }, [slots.action]);
    return;
  }

  if (intent === "CLICK_TARGET") {
    inject(function(target) {
      var selectors = "a, button, input[type='submit'], input[type='button'], select, [role='button'], [role='link'], [role='tab'], [role='menuitem'], [onclick], span[class*='button'], div[class*='button']";
      var best = null, bestScore = 0;
      document.querySelectorAll(selectors).forEach(function(el) {
        var rect = el.getBoundingClientRect();
        if (rect.width === 0 || rect.height === 0) return;
        var name = (el.getAttribute("aria-label") || el.innerText || el.value || el.title || el.alt || "").trim().toLowerCase();
        if (!name) return;
        var t = target.toLowerCase();
        var score = 0;
        if (name === t) score = 1.0;
        else if (name.startsWith(t)) score = 0.85;
        else if (name.includes(t)) score = 0.7;
        else if (t.includes(name)) score = 0.5;
        if (score > bestScore) { bestScore = score; best = el; }
      });
      if (best) {
        best.scrollIntoView({ behavior: "smooth", block: "center" });
        setTimeout(function() { best.focus(); best.click(); }, 300);
        return { ok: true, clicked: (best.innerText || best.value || "").substring(0, 60) };
      }
      return { ok: false };
    }, [slots.target_text], function(result) {
      if (result && !result.ok) speak("Could not find " + slots.target_text);
    });
    return;
  }

  if (intent === "TYPE_TEXT") {
    inject(function(text, field) {
      var el = null;
      if (field) {
        document.querySelectorAll("input, textarea, [contenteditable='true']").forEach(function(e) {
          var label = (e.getAttribute("aria-label") || e.getAttribute("placeholder") || e.getAttribute("name") || e.id || "").toLowerCase();
          if (label.includes(field.toLowerCase())) el = e;
        });
      }
      if (!el) el = document.activeElement;
      if (!el || (el.tagName !== "INPUT" && el.tagName !== "TEXTAREA" && !el.isContentEditable)) {
        el = document.querySelector("input:not([type='hidden']):not([type='submit']):not([type='button']), textarea");
      }
      if (el) {
        el.focus();
        if (el.isContentEditable) { el.textContent = text; } else { el.value = text; }
        el.dispatchEvent(new Event("input", { bubbles: true }));
        el.dispatchEvent(new Event("change", { bubbles: true }));
        return { ok: true };
      }
      return { ok: false };
    }, [slots.text, slots.field || ""], function(result) {
      if (result && !result.ok) speak("No input field found");
    });
    return;
  }

  if (intent === "SELECT_OPTION") {
    inject(function(option, target) {
      var sel = null;
      document.querySelectorAll("select").forEach(function(s) {
        if (!target) { sel = s; return; }
        var label = (s.getAttribute("aria-label") || s.getAttribute("name") || s.id || "").toLowerCase();
        if (label.includes(target.toLowerCase())) sel = s;
      });
      if (!sel) sel = document.querySelector("select");
      if (sel) {
        for (var i = 0; i < sel.options.length; i++) {
          if (sel.options[i].text.toLowerCase().includes(option.toLowerCase())) {
            sel.selectedIndex = i; sel.dispatchEvent(new Event("change", { bubbles: true })); return { ok: true };
          }
        }
      }
      return { ok: false };
    }, [slots.option, slots.target || ""], function(result) {
      if (result && !result.ok) speak("Could not find that option");
    });
    return;
  }

  if (intent === "ZOOM") {
    inject(function(dir) {
      var current = parseFloat(document.documentElement.style.zoom || "1");
      if (dir === "in") document.documentElement.style.zoom = (current + 0.1).toFixed(1);
      else if (dir === "out") document.documentElement.style.zoom = Math.max(0.3, current - 0.1).toFixed(1);
      else document.documentElement.style.zoom = "1";
    }, [slots.direction]);
    return;
  }

  if (intent === "FIND_ON_PAGE") {
    inject(function(searchText) { window.find(searchText, false, false, true); }, [slots.text]);
    return;
  }

  if (intent === "SYSTEM") {
    setListenMode(false);
    return;
  }

  if (intent === "UNKNOWN") {
    console.log("Unrecognized command, ignoring:", slots.text);
    safeBroadcast({ type: "COMMAND_IGNORED", text: slots.text });
    return;
  }
}

function handleTabOp(slots) {
  var action = slots.action;
  if (action === "new") { chrome.tabs.create({}); }
  else if (action === "close") { chrome.tabs.query({ active: true, currentWindow: true }, function(tabs) { if (tabs[0]) chrome.tabs.remove(tabs[0].id); }); }
  else if (action === "reload") { chrome.tabs.query({ active: true, currentWindow: true }, function(tabs) { if (tabs[0]) chrome.tabs.reload(tabs[0].id); }); }
  else if (action === "back") { chrome.tabs.query({ active: true, currentWindow: true }, function(tabs) { if (tabs[0]) chrome.tabs.goBack(tabs[0].id); }); }
  else if (action === "forward") { chrome.tabs.query({ active: true, currentWindow: true }, function(tabs) { if (tabs[0]) chrome.tabs.goForward(tabs[0].id); }); }
  else if (action === "reopen") { chrome.sessions.restore(); }
  else if (action === "switch" && slots.tab_index) {
    chrome.tabs.query({ currentWindow: true }, function(tabs) {
      var idx = slots.tab_index - 1;
      if (idx >= 0 && idx < tabs.length) chrome.tabs.update(tabs[idx].id, { active: true });
    });
  }
}

// ── Helpers ──

function inject(func, args, callback) {
  chrome.tabs.query({ active: true, currentWindow: true }, function(tabs) {
    if (!tabs || !tabs[0]) return;
    chrome.scripting.executeScript({ target: { tabId: tabs[0].id }, func: func, args: args || [] }, function(results) {
      if (chrome.runtime.lastError) { console.warn("Inject failed:", chrome.runtime.lastError.message); return; }
      if (callback && results && results[0]) callback(results[0].result);
    });
  });
}

function injectIntoTab(tabId, func, args, callback) {
  chrome.scripting.executeScript({ target: { tabId: tabId }, func: func, args: args || [] }, function(results) {
    if (chrome.runtime.lastError) {
      if (callback) callback(new Error(chrome.runtime.lastError.message));
      return;
    }
    if (callback) callback(null, results && results[0] ? results[0].result : undefined);
  });
}

function runAutonomousTool(tabId, tool, args, sendResponse) {
  var t = (tool || "").toLowerCase();
  var a = args || {};

  if (t === "done" || t === "none") {
    sendResponse({ ok: true, finished: true });
    return;
  }

  if (t === "wait") {
    var ms = Math.min(5000, Math.max(0, parseInt(a.ms, 10) || 500));
    setTimeout(function() {
      sendResponse({ ok: true });
    }, ms);
    return;
  }

  if (t === "navigate" && a.url) {
    var u = String(a.url);
    if (!/^https:\/\//i.test(u) && !/^http:\/\/localhost/i.test(u) && !/^http:\/\/127\.0\.0\.1/i.test(u)) {
      sendResponse({ ok: false, error: "only https or local http allowed" });
      return;
    }
    chrome.tabs.update(tabId, { url: u }, function() {
      if (chrome.runtime.lastError) sendResponse({ ok: false, error: chrome.runtime.lastError.message });
      else sendResponse({ ok: true });
    });
    return;
  }

  if (t === "scroll") {
    var dir = a.direction === "up" ? "up" : "down";
    var amt = a.amount === "small" || a.amount === "large" ? a.amount : "medium";
    injectIntoTab(
      tabId,
      function(direction, amount) {
        var sizes = { small: 0.25, medium: 0.6, large: 1.2 };
        var px = window.innerHeight * (sizes[amount] || 0.6);
        window.scrollBy({ behavior: "smooth", top: direction === "down" ? px : -px });
      },
      [dir, amt],
      function(err) {
        sendResponse({ ok: !err, error: err && err.message });
      }
    );
    return;
  }

  if (t === "click") {
    var m = typeof a.target_id === "string" && a.target_id.match(/^e_(\d+)$/i);
    var idx = m ? parseInt(m[1], 10) : -1;
    if (idx < 0) {
      sendResponse({ ok: false, error: "bad target_id" });
      return;
    }
    AutonomousAgent.clickElementByIndex(tabId, idx, function(err) {
      sendResponse({ ok: !err, error: err && err.message });
    });
    return;
  }

  if (t === "type") {
    sendResponse({ ok: false, error: "type tool not implemented yet" });
    return;
  }

  sendResponse({ ok: false, error: "unknown tool: " + t });
}

function speak(text) {
  try { chrome.tts.speak(text, { rate: 1.1 }); } catch (e) {}
}

function safeBroadcast(msg) {
  try { chrome.runtime.sendMessage(msg, function() { if (chrome.runtime.lastError) {} }); } catch (e) {}
}

function setListenMode(on) {
  listenMode = on;
  chrome.storage.local.set({ va_listen_mode: on });
  if (on) {
    muteActiveTab(true);
    activateOnCurrentTab();
  } else {
    muteActiveTab(false);
    chrome.tabs.query({ active: true, currentWindow: true }, function(tabs) {
      if (tabs && tabs[0]) chrome.tabs.sendMessage(tabs[0].id, { type: "STOP_SPEECH" }, function() { if (chrome.runtime.lastError) {} });
    });
  }
}

function muteActiveTab(mute) {
  chrome.tabs.query({ active: true, currentWindow: true }, function(tabs) {
    if (tabs && tabs[0]) chrome.tabs.update(tabs[0].id, { muted: mute });
  });
}

function isValidTab(tab) {
  if (!tab || !tab.url) return false;
  return !tab.url.startsWith("chrome://") && !tab.url.startsWith("chrome-extension://") && !tab.url.startsWith("about:") && !tab.url.startsWith("edge://");
}

function activateOnCurrentTab() {
  chrome.tabs.query({ active: true, currentWindow: true }, function(tabs) {
    if (!tabs || !tabs[0] || !isValidTab(tabs[0])) return;
    startSpeechOnTab(tabs[0].id);
  });
}

function startSpeechOnTab(tabId) {
  chrome.scripting.executeScript({ target: { tabId: tabId }, files: ["speech.js"] }, function() {
    if (chrome.runtime.lastError) return;
    setTimeout(function() {
      chrome.tabs.sendMessage(tabId, { type: "START_SPEECH" }, function(resp) {
        if (chrome.runtime.lastError) {
          setTimeout(function() {
            chrome.tabs.sendMessage(tabId, { type: "START_SPEECH" }, function() { if (chrome.runtime.lastError) {} });
          }, 500);
        }
      });
    }, 300);
  });
}

chrome.tabs.onUpdated.addListener(function(tabId, changeInfo, tab) {
  if (!listenMode || changeInfo.status !== "complete" || !isValidTab(tab)) return;
  chrome.tabs.query({ active: true, currentWindow: true }, function(tabs) {
    if (tabs && tabs[0] && tabs[0].id === tabId) startSpeechOnTab(tabId);
  });
});

chrome.tabs.onActivated.addListener(function(activeInfo) {
  if (!listenMode) return;
  chrome.tabs.get(activeInfo.tabId, function(tab) {
    if (chrome.runtime.lastError || !isValidTab(tab)) return;
    if (tab.status === "complete") startSpeechOnTab(tab.id);
  });
});

chrome.runtime.onInstalled.addListener(function() {
  chrome.storage.local.get(["va_listen_mode"], function(r) {
    listenMode = r.va_listen_mode !== false;
    if (listenMode) setTimeout(activateOnCurrentTab, 1000);
  });
});
chrome.runtime.onStartup.addListener(function() {
  chrome.storage.local.get(["va_listen_mode"], function(r) {
    listenMode = r.va_listen_mode !== false;
    if (listenMode) setTimeout(activateOnCurrentTab, 1000);
  });
});

chrome.runtime.onMessage.addListener(function(msg, sender, sendResponse) {
  if (msg.type === "EXECUTE_COMMAND") {
    processCommand(msg.text, function(parsed) {
      execute(parsed);
      if (parsed.intent !== "UNKNOWN") {
        safeBroadcast({ type: "COMMAND_EXECUTED", transcript: msg.text, parsed: parsed });
      }
    });
    sendResponse({ ok: true });
  }
  else if (msg.type === "TOGGLE_LISTEN") {
    setListenMode(!listenMode);
    sendResponse({ listening: listenMode });
  }
  else if (msg.type === "GET_LISTEN_MODE") {
    sendResponse({ listening: listenMode, ollama: ollamaAvailable });
  }
  else if (msg.type === "SPEECH_RESULT") {
    console.log("Voice:", msg.transcript);
    processCommand(msg.transcript, function(parsed) {
      execute(parsed);
      if (parsed.intent !== "UNKNOWN") {
        safeBroadcast({ type: "COMMAND_EXECUTED", transcript: msg.transcript, parsed: parsed });
      }
    });
  }
  else if (msg.type === "USER_STOPPED_SPEECH") {
    setListenMode(false);
    safeBroadcast({ type: "LISTEN_MODE_CHANGED", listening: false });
  }
  else if (msg.type === "SPEECH_INTERIM" || msg.type === "SPEECH_STATUS" || msg.type === "SPEECH_ERROR") {
    safeBroadcast(msg);
  }
  else if (msg.type === "AUTONOMOUS_PLAN_STEP") {
    function runPlan(tabId) {
      AutonomousAgent.collectSnapshot(tabId, function(err, snap) {
        if (err) {
          sendResponse({ ok: false, error: err.message });
          return;
        }
        AutonomousAgent.planStep(msg.goal || "", msg.history || [], snap, function(err2, plan) {
          if (err2) {
            sendResponse({
              ok: false,
              error: err2.message || String(err2),
              snapshot: snap,
              model: AutonomousAgent.AUTONOMOUS_MODEL
            });
            return;
          }
          sendResponse({
            ok: true,
            snapshot: snap,
            plan: plan,
            model: AutonomousAgent.AUTONOMOUS_MODEL
          });
        });
      });
    }
    if (msg.tabId) runPlan(msg.tabId);
    else {
      chrome.tabs.query({ active: true, currentWindow: true }, function(tabs) {
        if (!tabs || !tabs[0]) {
          sendResponse({ ok: false, error: "no active tab" });
          return;
        }
        runPlan(tabs[0].id);
      });
    }
    return true;
  }
  else if (msg.type === "AUTONOMOUS_RUN_TOOL") {
    function runToolOnTab(tabId) {
      runAutonomousTool(tabId, msg.tool, msg.args, sendResponse);
    }
    if (msg.tabId) runToolOnTab(msg.tabId);
    else {
      chrome.tabs.query({ active: true, currentWindow: true }, function(tabs) {
        if (!tabs || !tabs[0]) {
          sendResponse({ ok: false, error: "no active tab" });
          return;
        }
        runToolOnTab(tabs[0].id);
      });
    }
    return true;
  }
  else if (msg.type === "AUTONOMOUS_GET_MODEL") {
    sendResponse({ model: AutonomousAgent.AUTONOMOUS_MODEL });
  }
  return false;
});

setTimeout(function() {
  chrome.storage.local.get(["va_listen_mode"], function(r) {
    listenMode = r.va_listen_mode !== false;
    if (listenMode) activateOnCurrentTab();
  });
}, 500);
