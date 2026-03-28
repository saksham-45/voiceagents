// popup.js — Voice Agent Popup
(function() {
  "use strict";

  var toggle = document.getElementById("listen-toggle");
  var statusDot = document.getElementById("status-dot");
  var statusText = document.getElementById("status-text");
  var cmdInput = document.getElementById("cmd-input");
  var cmdForm = document.getElementById("cmd-form");
  var log = document.getElementById("log");

  chrome.runtime.sendMessage({ type: "GET_LISTEN_MODE" }, function(resp) {
    if (resp) {
      toggle.checked = resp.listening;
      updateStatus(resp.listening);
      var badge = document.getElementById("llm-badge");
      if (badge) {
        badge.textContent = resp.ollama ? "AI" : "Regex";
        badge.className = "llm-badge " + (resp.ollama ? "on" : "off");
      }
    }
  });

  toggle.addEventListener("change", function() {
    chrome.runtime.sendMessage({ type: "TOGGLE_LISTEN" }, function(resp) {
      if (resp) updateStatus(resp.listening);
    });
  });

  function updateStatus(listening) {
    if (listening) {
      statusDot.className = "dot on";
      statusText.textContent = "Listening — speak a command";
    } else {
      statusDot.className = "dot off";
      statusText.textContent = "Mic off — toggle to enable";
    }
  }

  chrome.runtime.onMessage.addListener(function(msg) {
    if (msg.type === "SPEECH_INTERIM") {
      statusText.textContent = msg.transcript;
    }
    else if (msg.type === "SPEECH_STATUS") {
      if (msg.status === "listening") {
        statusDot.className = "dot on";
        statusText.textContent = "Listening...";
      }
    }
    else if (msg.type === "SPEECH_ERROR") {
      statusDot.className = "dot error";
      if (msg.error === "not-allowed") statusText.textContent = "Mic blocked on this site";
      else statusText.textContent = "Restarting...";
    }
    else if (msg.type === "COMMAND_IGNORED") {
      addLog("ignored", msg.text || "?");
      statusDot.className = "dot on";
      statusText.textContent = "Not a command — listening...";
    }
    else if (msg.type === "LLM_THINKING") {
      statusDot.className = "dot thinking";
      statusText.textContent = "AI thinking...";
    }
    else if (msg.type === "COMMAND_EXECUTED") {
      addLog("you", msg.transcript);
      var src = msg.parsed && msg.parsed._source === "ai" ? "ai" : "agent";
      addLog(src, formatParsed(msg.parsed));
      statusDot.className = "dot on";
      statusText.textContent = "Done — listening...";
    }
    else if (msg.type === "LISTEN_MODE_CHANGED") {
      toggle.checked = msg.listening;
      updateStatus(msg.listening);
    }
  });

  cmdForm.addEventListener("submit", function(e) {
    e.preventDefault();
    var text = cmdInput.value.trim();
    if (!text) return;
    cmdInput.value = "";
    statusDot.className = "dot thinking";
    statusText.textContent = "Processing...";
    chrome.runtime.sendMessage({ type: "EXECUTE_COMMAND", text: text }, function(resp) {
      if (chrome.runtime.lastError) {
        addLog("error", chrome.runtime.lastError.message);
      }
    });
  });

  function formatParsed(p) {
    if (!p) return "?";
    if (p.intent === "TAB_OP") return "TAB: " + (p.slots.action || "");
    if (p.intent === "SCROLL") return "SCROLL " + (p.slots.direction || "");
    if (p.intent === "NAVIGATE") return "GO TO " + (p.slots.url || "").substring(0, 40);
    if (p.intent === "SEARCH") return "SEARCH: " + (p.slots.query || "");
    if (p.intent === "CLICK_TARGET") return "CLICK: " + (p.slots.target_text || "");
    if (p.intent === "TYPE_TEXT") return "TYPE: " + (p.slots.text || "");
    if (p.intent === "ZOOM") return "ZOOM " + (p.slots.direction || "");
    if (p.intent === "PICK_NTH") return "PICK #" + p.slots.index + " " + (p.slots.item_type || "");
    if (p.intent === "MEDIA") return "MEDIA: " + (p.slots.action || "");
    if (p.intent === "PAGE_NAV") return "PAGE: " + (p.slots.direction || "");
    if (p.intent === "FORM") return "FORM: " + (p.slots.action || "");
    if (p.intent === "YT_ACTION") return "YT: " + (p.slots.action || "").replace(/_/g, " ");
    if (p.intent === "SYSTEM") return "Stopped listening";
    if (p.intent === "UNKNOWN") return "? (not a command)";
    return p.intent;
  }

  function addLog(role, text) {
    var entry = document.createElement("div");
    entry.className = "log-entry log-" + role;
    var label = document.createElement("span");
    label.className = "log-label";
    if (role === "you") label.textContent = "You";
    else if (role === "agent") label.textContent = "Bot";
    else if (role === "ai") label.textContent = "AI";
    else if (role === "ignored") label.textContent = "Skip";
    else if (role === "error") label.textContent = "Err";
    else label.textContent = role;
    var msg = document.createElement("span");
    msg.className = "log-msg";
    msg.textContent = text;
    entry.appendChild(label);
    entry.appendChild(msg);
    log.insertBefore(entry, log.firstChild);
    while (log.children.length > 30) log.removeChild(log.lastChild);
  }

  document.querySelectorAll(".hint-grid span").forEach(function(chip) {
    chip.addEventListener("click", function() {
      var text = chip.textContent.trim();
      if (text.includes("[")) {
        cmdInput.value = text.replace(/\[.*?\]/, "").trim() + " ";
        cmdInput.focus();
      } else {
        statusDot.className = "dot thinking";
        statusText.textContent = "Processing...";
        chrome.runtime.sendMessage({ type: "EXECUTE_COMMAND", text: text }, function() {});
      }
    });
  });

  cmdInput.focus();
})();
