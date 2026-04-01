// autonomous_agent.js — local Ollama planner + page snapshot (Phase 1)
// Loaded by background.js via importScripts. Keep interactive list logic in sync
// between collectSnapshot and clickElementByIndex (same selector + visibility rules).
"use strict";

var OLLAMA_GENERATE_URL = "http://127.0.0.1:11434/api/generate";

// Must match an exact name from `ollama list` (404 = wrong tag or not pulled).
// Default matches voice LLM in background.js; for stronger planning try e.g. llama3.1:8b after `ollama pull llama3.1:8b`.
var AUTONOMOUS_MODEL = "qwen2.5:3b";

var AUTONOMOUS_SYSTEM_PROMPT = [
  "You are the planner for a Chromium browser extension. You choose ONE tool call per turn.",
  "You receive JSON: page URL, title, and a list of visible interactive elements with ids e_0, e_1, ... in stable order.",
  "Reply with ONLY a JSON object (no markdown, no prose outside JSON) with exactly these keys:",
  "  tool: one of click | scroll | navigate | type | wait | done | none",
  "  args: object (depends on tool)",
  "  reason: one short sentence",
  "",
  "Tool args:",
  "- click: { \"target_id\": \"e_N\" } using ids from the snapshot only",
  "- scroll: { \"direction\": \"up\" | \"down\", \"amount\": \"small\" | \"medium\" | \"large\" } optional amount default medium",
  "- navigate: { \"url\": \"https://...\" } must be https (or http only for localhost)",
  "- type: { \"target_id\": \"e_N\", \"text\": \"...\" } (execution may be limited in early builds)",
  "- wait: { \"ms\": number } max 5000",
  "- done: {} when the USER_GOAL is fully achieved on this page",
  "- none: {} when you cannot safely act (explain in reason)",
  "",
  "Rules:",
  "- Prefer the smallest action that moves toward USER_GOAL.",
  "- Never invent element ids. Only use e_0 .. e_N from the snapshot.",
  "- If the goal needs a different site, use navigate.",
  "- If you are unsure, use none."
].join("\n");

function collectSnapshot(tabId, callback) {
  chrome.scripting.executeScript(
    {
      target: { tabId: tabId },
      func: function() {
        function vaInteractiveSelector() {
          return 'a[href], button, [role="button"], input:not([type="hidden"]), select, textarea, [tabindex]:not([tabindex="-1"])';
        }
        var sel = document.querySelectorAll(vaInteractiveSelector());
        var out = [];
        var lim = 80;
        for (var i = 0; i < sel.length && out.length < lim; i++) {
          var el = sel[i];
          var r = el.getBoundingClientRect();
          if (r.width < 2 || r.height < 2 || r.bottom < 0 || r.top > window.innerHeight) continue;
          var label =
            el.getAttribute("aria-label") ||
            el.innerText ||
            el.value ||
            el.placeholder ||
            el.title ||
            el.alt ||
            "";
          label = String(label).replace(/\s+/g, " ").trim().slice(0, 120);
          out.push({
            id: "e_" + out.length,
            tag: el.tagName.toLowerCase(),
            role: el.getAttribute("role") || "",
            text: label
          });
        }
        return {
          url: location.href,
          title: document.title,
          elements: out,
          truncated: sel.length > lim
        };
      }
    },
    function(results) {
      if (chrome.runtime.lastError) {
        callback(new Error(chrome.runtime.lastError.message));
        return;
      }
      if (!results || !results.length) {
        callback(new Error("empty snapshot"));
        return;
      }
      callback(null, results[0].result);
    }
  );
}

function clickElementByIndex(tabId, index, callback) {
  chrome.scripting.executeScript(
    {
      target: { tabId: tabId },
      args: [index],
      func: function(idx) {
        function vaInteractiveSelector() {
          return 'a[href], button, [role="button"], input:not([type="hidden"]), select, textarea, [tabindex]:not([tabindex="-1"])';
        }
        var sel = document.querySelectorAll(vaInteractiveSelector());
        var visible = [];
        for (var i = 0; i < sel.length && visible.length < 80; i++) {
          var el = sel[i];
          var r = el.getBoundingClientRect();
          if (r.width < 2 || r.height < 2 || r.bottom < 0 || r.top > window.innerHeight) continue;
          visible.push(el);
        }
        if (idx < 0 || idx >= visible.length) {
          return { ok: false, error: "index out of range", count: visible.length };
        }
        try {
          visible[idx].click();
          return { ok: true };
        } catch (e) {
          return { ok: false, error: String(e && e.message ? e.message : e) };
        }
      }
    },
    function(results) {
      if (chrome.runtime.lastError) {
        callback(new Error(chrome.runtime.lastError.message));
        return;
      }
      if (!results || !results.length) {
        callback(new Error("no click result"));
        return;
      }
      var r = results[0].result;
      if (!r.ok) callback(new Error(r.error || "click failed"));
      else callback(null, r);
    }
  );
}

function parseToolJson(text) {
  var trimmed = (text || "").trim();
  var match = trimmed.match(/\{[\s\S]*\}/);
  if (!match) throw new Error("no JSON in model output");
  var obj = JSON.parse(match[0]);
  if (!obj.tool || typeof obj.tool !== "string") throw new Error("missing tool");
  obj.args = obj.args && typeof obj.args === "object" ? obj.args : {};
  if (typeof obj.reason !== "string") obj.reason = "";
  return obj;
}

function planStep(goal, history, snapshot, callback) {
  var hist = history || [];
  var histStr = JSON.stringify(hist);
  if (histStr.length > 12000) histStr = histStr.slice(-12000);

  var userBlock = [
    "USER_GOAL:",
    String(goal || "").slice(0, 4000),
    "",
    "RECENT_HISTORY_JSON:",
    histStr,
    "",
    "PAGE_SNAPSHOT_JSON:",
    JSON.stringify(snapshot || {}),
    "",
    "Output the single next tool JSON now."
  ].join("\n");

  var controller = new AbortController();
  var to = setTimeout(function() {
    controller.abort();
  }, 90000);

  fetch(OLLAMA_GENERATE_URL, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    signal: controller.signal,
    body: JSON.stringify({
      model: AUTONOMOUS_MODEL,
      system: AUTONOMOUS_SYSTEM_PROMPT,
      prompt: userBlock,
      stream: false,
      options: { temperature: 0.15, num_predict: 256 }
    })
  })
    .then(function(r) {
      clearTimeout(to);
      if (r.status === 403) {
        throw new Error(
          "ollama HTTP 403: Ollama blocks the extension Origin. Quit the Ollama menu-bar app, then in Terminal run: " +
            "OLLAMA_ORIGINS=chrome-extension://* ollama serve " +
            "(or list your exact id from chrome://extensions). See README \"Ollama 403\"."
        );
      }
      if (r.status === 404) {
        throw new Error(
          "ollama HTTP 404: model \"" +
            AUTONOMOUS_MODEL +
            "\" not found. Run `ollama list` and set AUTONOMOUS_MODEL in autonomous_agent.js to an exact tag, " +
            "or run `ollama pull " +
            AUTONOMOUS_MODEL +
            "` (example for a larger planner: `ollama pull llama3.1:8b` then set AUTONOMOUS_MODEL to that tag)."
        );
      }
      if (!r.ok) throw new Error("ollama HTTP " + r.status);
      return r.json();
    })
    .then(function(data) {
      var raw = (data.response || "").trim();
      var plan = parseToolJson(raw);
      plan._raw = raw;
      callback(null, plan);
    })
    .catch(function(err) {
      clearTimeout(to);
      callback(err);
    });
}

var AutonomousAgent = {
  AUTONOMOUS_MODEL: AUTONOMOUS_MODEL,
  setModel: function(name) {
    AUTONOMOUS_MODEL = name;
  },
  collectSnapshot: collectSnapshot,
  clickElementByIndex: clickElementByIndex,
  planStep: planStep
};
