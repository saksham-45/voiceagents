// autonomous_agent.js — local Ollama planner + page snapshot (Phase 1–2 loop uses history)
// Loaded by background.js via importScripts. Keep interactive list logic in sync
// between collectSnapshot and clickElementByIndex (same selector + visibility rules).
"use strict";

var OLLAMA_GENERATE_URL = "http://127.0.0.1:11434/api/generate";
var OPENAI_CHAT_COMPLETIONS_URL = "https://api.openai.com/v1/chat/completions";

// Must match an exact name from `ollama list` (404 = wrong tag or not pulled).
// Default matches voice LLM in background.js; for stronger planning try e.g. llama3.1:8b after `ollama pull llama3.1:8b`.
var AUTONOMOUS_MODEL = "qwen2.5:3b";

var AUTONOMOUS_SYSTEM_PROMPT = [
  "You are a conservative planner for a Chromium browser extension. ONE tool per turn. Minimize risk: do not explore randomly.",
  "You receive JSON: page URL, title, and visible interactive elements with ids e_0, e_1, ... in stable order.",
  "Reply with ONLY a JSON object (no markdown, no prose outside JSON) with exactly these keys:",
  "  tool: one of click | scroll | navigate | type | wait | done | none",
  "  args: object (depends on tool)",
  "  reason: one short sentence",
  "",
  "Tool args:",
  "- click: { \"target_id\": \"e_N\" } using ids from the snapshot only",
  "- scroll: { \"direction\": \"up\" | \"down\", \"amount\": \"small\" | \"medium\" | \"large\" } optional amount default medium",
  "- navigate: { \"url\": \"https://...\" } loads that URL in the CURRENT tab. must be https (or http only for localhost)",
  "- type: { \"target_id\": \"e_N\", \"text\": \"...\" } for text/search inputs and textareas (focuses, sets value, fires input/change)",
  "- wait: { \"ms\": number } max 5000",
  "- done: {} when USER_GOAL is fully achieved (see counting rules below)",
  "- none: {} when you cannot act safely without guessing (explain in reason)",
  "",
  "Safety rules (critical):",
  "- NEVER navigate to a different website unless USER_GOAL clearly names that site, names a product/domain users would recognize (e.g. amazon, chatgpt), or contains an explicit https URL. If the goal is only scroll/read/click something on this page, NEVER navigate.",
  "- NEVER click unless USER_GOAL explicitly asks to activate something AND you can match it to ONE element's visible text/label/role in the snapshot. If unsure which element, use none — do not guess.",
  "- NEVER open links \"to see what happens\". No curiosity clicks.",
  "- For goals like \"scroll twice\" / \"scroll down 3 times\": count successful scroll steps in RECENT_HISTORY_JSON; after that many scrolls with execOk true, return done — do NOT keep scrolling because element counts changed (GitHub/SPAs fluctuate).",
  "- Ignore large swings in elementCountDelta for deciding whether to scroll again; trust execOk and the user's stated count.",
  "",
  "Navigate (when allowed):",
  "- If USER_GOAL says open/go to/visit a site, use navigate with full https URL even from an unrelated page. Do NOT use none for \"new tab\" — navigate uses the current tab.",
  "- After navigate, later steps may click/type on the new page.",
  "",
  "General:",
  "- Never invent element ids. Only e_0 .. e_N from the snapshot.",
  "- Prefer scroll or wait over click when the goal is vague.",
  "- If unsure, use none.",
  "",
  "RECENT_HISTORY_JSON lists prior steps: tool, args, reason, execOk, execError, verify (urlChanged, elementCountDelta, ...). Avoid repeating failed tools; use done when the goal is satisfied."
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

function typeElementByIndex(tabId, index, text, callback) {
  var safeText = String(text != null ? text : "").slice(0, 2000);
  chrome.scripting.executeScript(
    {
      target: { tabId: tabId },
      args: [index, safeText],
      func: function(idx, str) {
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
        var node = visible[idx];
        try {
          node.scrollIntoView({ block: "center", inline: "nearest" });
          node.focus();
          var tag = node.tagName.toLowerCase();
          var typ = (node.getAttribute("type") || "text").toLowerCase();
          var typeableInput =
            tag === "input" &&
            ["text", "search", "email", "url", "tel", "password", "number"].indexOf(typ) >= 0;
          if (tag === "textarea" || typeableInput) {
            var proto = tag === "textarea" ? window.HTMLTextAreaElement.prototype : window.HTMLInputElement.prototype;
            var desc = Object.getOwnPropertyDescriptor(proto, "value");
            if (desc && desc.set) desc.set.call(node, str);
            else node.value = str;
            try {
              node.dispatchEvent(new InputEvent("input", { bubbles: true, cancelable: true, inputType: "insertFromPaste", data: str }));
            } catch (e1) {
              node.dispatchEvent(new Event("input", { bubbles: true }));
            }
            node.dispatchEvent(new Event("change", { bubbles: true }));
            return { ok: true };
          }
          if (node.isContentEditable || node.getAttribute("contenteditable") === "true") {
            node.textContent = str;
            try {
              node.dispatchEvent(new InputEvent("input", { bubbles: true, cancelable: true, inputType: "insertFromPaste", data: str }));
            } catch (e2) {
              node.dispatchEvent(new Event("input", { bubbles: true }));
            }
            return { ok: true };
          }
          return { ok: false, error: "element is not typeable (tag:" + tag + " type:" + typ + ")" };
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
        callback(new Error("no type result"));
        return;
      }
      var r = results[0].result;
      if (!r.ok) callback(new Error(r.error || "type failed"));
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

function buildAutonomousUserBlock(goal, history, snapshot) {
  var hist = history || [];
  var histStr = JSON.stringify(hist);
  if (histStr.length > 12000) histStr = histStr.slice(-12000);
  var pageUrl = snapshot && snapshot.url ? String(snapshot.url) : "";
  return [
    "USER_GOAL:",
    String(goal || "").slice(0, 4000),
    "",
    "CURRENT_PAGE_URL:",
    pageUrl,
    "Do not use navigate to a different site unless USER_GOAL names that destination or includes its URL.",
    "",
    "RECENT_HISTORY_JSON:",
    histStr,
    "",
    "PAGE_SNAPSHOT_JSON:",
    JSON.stringify(snapshot || {}),
    "",
    "Output the single next tool JSON now."
  ].join("\n");
}

function planStepOpenAI(userBlock, apiKey, model, callback) {
  var controller = new AbortController();
  var to = setTimeout(function() {
    controller.abort();
  }, 90000);

  fetch(OPENAI_CHAT_COMPLETIONS_URL, {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
      Authorization: "Bearer " + apiKey
    },
    signal: controller.signal,
    body: JSON.stringify({
      model: model,
      messages: [
        { role: "system", content: AUTONOMOUS_SYSTEM_PROMPT },
        { role: "user", content: userBlock }
      ],
      response_format: { type: "json_object" },
      temperature: 0.05,
      max_tokens: 512
    })
  })
    .then(function(r) {
      clearTimeout(to);
      return r.json().then(function(data) {
        if (r.status === 401) {
          throw new Error(
            "OpenAI HTTP 401: check API key at https://platform.openai.com/api-keys (stored in extension popup)."
          );
        }
        if (r.status === 429) {
          throw new Error("OpenAI HTTP 429: rate limit or quota — try again later or check billing.");
        }
        if (!r.ok) {
          var em = data && data.error && data.error.message ? data.error.message : "HTTP " + r.status;
          throw new Error("OpenAI: " + em);
        }
        return data;
      });
    })
    .then(function(data) {
      var raw =
        (data.choices && data.choices[0] && data.choices[0].message && data.choices[0].message.content) || "";
      raw = String(raw).trim();
      var plan = parseToolJson(raw);
      plan._raw = raw;
      plan._plannerBackend = "openai";
      plan._plannerModel = model;
      callback(null, plan);
    })
    .catch(function(err) {
      clearTimeout(to);
      callback(err);
    });
}

function planStep(goal, history, snapshot, callback, options) {
  options = options || {};
  var openaiKey = options.openaiApiKey != null ? String(options.openaiApiKey).trim() : "";
  var openaiModel =
    options.openaiModel != null && String(options.openaiModel).trim()
      ? String(options.openaiModel).trim()
      : "gpt-4o-mini";

  var userBlock = buildAutonomousUserBlock(goal, history, snapshot);

  if (openaiKey) {
    planStepOpenAI(userBlock, openaiKey, openaiModel, callback);
    return;
  }

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
      plan._plannerBackend = "ollama";
      plan._plannerModel = AUTONOMOUS_MODEL;
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
  typeElementByIndex: typeElementByIndex,
  planStep: planStep
};
