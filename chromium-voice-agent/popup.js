// popup.js — Voice Agent Popup
(function() {
  "use strict";

  var toggle = document.getElementById("listen-toggle");
  var statusDot = document.getElementById("status-dot");
  var statusText = document.getElementById("status-text");
  var cmdInput = document.getElementById("cmd-input");
  var cmdForm = document.getElementById("cmd-form");
  var log = document.getElementById("log");
  var agentGoal = document.getElementById("agent-goal");
  var agentMaxSteps = document.getElementById("agent-max-steps");
  var agentRunLoop = document.getElementById("agent-run-loop");
  var agentPlanBtn = document.getElementById("agent-plan");
  var agentRunBtn = document.getElementById("agent-run");
  var agentPause = document.getElementById("agent-pause");
  var agentResume = document.getElementById("agent-resume");
  var agentStopLoop = document.getElementById("agent-stop-loop");
  var agentOut = document.getElementById("agent-out");
  var lastAutonomousPlan = null;

  function prependAgentLog(ev) {
    if (!agentOut) return;
    var line =
      "[" +
      new Date().toLocaleTimeString() +
      "] " +
      (ev.event || "?") +
      " " +
      JSON.stringify(ev).slice(0, 320);
    agentOut.textContent = line + "\n" + agentOut.textContent;
    if (agentOut.textContent.length > 9000) agentOut.textContent = agentOut.textContent.slice(0, 9000);
  }

  function setLoopUiRunning() {
    if (agentRunLoop) agentRunLoop.disabled = true;
    if (agentPlanBtn) agentPlanBtn.disabled = true;
    if (agentRunBtn) agentRunBtn.disabled = true;
    if (agentPause) agentPause.disabled = false;
    if (agentResume) agentResume.disabled = true;
    if (agentStopLoop) agentStopLoop.disabled = false;
    if (agentGoal) agentGoal.disabled = true;
    if (agentMaxSteps) agentMaxSteps.disabled = true;
  }

  function setLoopUiPaused() {
    if (agentPause) agentPause.disabled = true;
    if (agentResume) agentResume.disabled = false;
    if (agentStopLoop) agentStopLoop.disabled = false;
  }

  function setLoopUiIdle() {
    if (agentRunLoop) agentRunLoop.disabled = false;
    if (agentPlanBtn) agentPlanBtn.disabled = false;
    if (agentPause) agentPause.disabled = true;
    if (agentResume) agentResume.disabled = true;
    if (agentStopLoop) agentStopLoop.disabled = true;
    if (agentGoal) agentGoal.disabled = false;
    if (agentMaxSteps) agentMaxSteps.disabled = false;
  }

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

  chrome.runtime.sendMessage({ type: "AUTONOMOUS_STATUS" }, function(st) {
    if (!st || chrome.runtime.lastError) return;
    if (st.status === "running") setLoopUiRunning();
    else if (st.status === "paused") {
      setLoopUiRunning();
      setLoopUiPaused();
    } else setLoopUiIdle();
  });

  var openaiKeyInput = document.getElementById("openai-api-key");
  var openaiModelInput = document.getElementById("openai-model");
  var openaiSaveBtn = document.getElementById("openai-save");
  var openaiClearBtn = document.getElementById("openai-clear");
  var openaiStatus = document.getElementById("openai-save-status");
  var plannerBackendBar = document.getElementById("planner-backend-bar");
  var plannerBackendText = document.getElementById("planner-backend-text");

  function normOpenAIKeyStored(raw) {
    if (raw == null) return "";
    return String(raw)
      .replace(/\uFEFF/g, "")
      .replace(/\s+/g, "")
      .trim();
  }

  function refreshPlannerBackendLabel() {
    if (!plannerBackendText || !plannerBackendBar) return;
    plannerBackendText.textContent = "Checking planner…";
    plannerBackendBar.className = "planner-backend-bar";
    chrome.runtime.sendMessage({ type: "AUTONOMOUS_GET_MODEL" }, function(resp) {
      if (chrome.runtime.lastError || !resp) {
        var errMsg = chrome.runtime.lastError ? chrome.runtime.lastError.message : "no response";
        plannerBackendText.textContent = "Autonomous planner: unknown (" + errMsg + ")";
        return;
      }
      var openai = resp.plannerBackend === "openai";
      plannerBackendBar.className = "planner-backend-bar " + (openai ? "openai" : "ollama");
      plannerBackendText.textContent =
        "Autonomous planner: " + (openai ? "OpenAI" : "Ollama") + " — " + (resp.model || "?");
    });
  }

  function loadOpenAISettings() {
    if (!openaiStatus && !openaiModelInput) return;
    chrome.storage.local.get(["va_openai_api_key", "va_openai_model"], function(r) {
      if (openaiModelInput) openaiModelInput.value = (r.va_openai_model || "gpt-4o-mini").trim();
      if (openaiKeyInput) openaiKeyInput.value = "";
      if (openaiStatus) {
        if (normOpenAIKeyStored(r.va_openai_api_key)) {
          openaiStatus.textContent =
            "Saved API key on file — paste a new key to replace, or Save to update the model only.";
        } else {
          openaiStatus.textContent = "No OpenAI key: autonomous planner uses local Ollama.";
        }
      }
    });
  }

  if (openaiSaveBtn && openaiModelInput) {
    openaiSaveBtn.addEventListener("click", function() {
      var keyRaw = openaiKeyInput ? openaiKeyInput.value : "";
      var model = openaiModelInput.value.trim() || "gpt-4o-mini";
      chrome.runtime.sendMessage(
        { type: "VA_SAVE_OPENAI_SETTINGS", apiKey: keyRaw, model: model },
        function(resp) {
          if (chrome.runtime.lastError) {
            if (openaiStatus) openaiStatus.textContent = "Save failed: " + chrome.runtime.lastError.message;
            return;
          }
          if (!resp || !resp.ok) {
            if (openaiStatus) openaiStatus.textContent = (resp && resp.error) || "Save failed.";
            return;
          }
          if (openaiKeyInput) openaiKeyInput.value = "";
          if (openaiStatus) {
            openaiStatus.textContent =
              "Saved. Autonomous planner uses OpenAI (" + (resp.model || model) + "). Check the green bar below.";
          }
          refreshPlannerBackendLabel();
        }
      );
    });
  }
  if (openaiClearBtn) {
    openaiClearBtn.addEventListener("click", function() {
      chrome.runtime.sendMessage({ type: "VA_CLEAR_OPENAI_KEY" }, function(resp) {
        if (chrome.runtime.lastError) {
          if (openaiStatus) openaiStatus.textContent = "Clear failed: " + chrome.runtime.lastError.message;
          return;
        }
        if (!resp || !resp.ok) {
          if (openaiStatus) openaiStatus.textContent = (resp && resp.error) || "Clear failed.";
          return;
        }
        if (openaiKeyInput) openaiKeyInput.value = "";
        if (openaiStatus) openaiStatus.textContent = "OpenAI key removed. Planner uses local Ollama.";
        refreshPlannerBackendLabel();
      });
    });
  }
  loadOpenAISettings();
  refreshPlannerBackendLabel();

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
    else if (msg.type === "AUTONOMOUS_LOOP_EVENT") {
      var ev = msg.payload || msg;
      prependAgentLog(ev);
      if (ev.event === "started") setLoopUiRunning();
      else if (ev.event === "paused") setLoopUiPaused();
      else if (ev.event === "resumed") setLoopUiRunning();
      else if (
        ev.event === "finished" ||
        ev.event === "cancelled" ||
        ev.event === "limit" ||
        ev.event === "error"
      ) {
        setLoopUiIdle();
        lastAutonomousPlan = null;
        if (agentRunBtn) agentRunBtn.disabled = true;
      }
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
    if (p.intent === "YT_ACTION") {
      var ya = (p.slots.action || "").replace(/_/g, " ");
      if (p.slots.action === "add_comment" && p.slots.text) return "YT: comment \"" + (p.slots.text.length > 40 ? p.slots.text.substring(0, 40) + "…" : p.slots.text) + "\"";
      return "YT: " + ya;
    }
    if (p.intent === "REDDIT_ACTION") {
      var ra = (p.slots.action || "").replace(/_/g, " ");
      if (p.slots.text) return "Reddit: " + ra + " \"" + (p.slots.text.length > 35 ? p.slots.text.substring(0, 35) + "…" : p.slots.text) + "\"";
      return "Reddit: " + ra;
    }
    if (p.intent === "INSTAGRAM_ACTION") {
      var ia = (p.slots.action || "").replace(/_/g, " ");
      if (p.slots.text) return "IG: " + ia + " \"" + (p.slots.text.length > 35 ? p.slots.text.substring(0, 35) + "…" : p.slots.text) + "\"";
      return "IG: " + ia;
    }
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

  if (agentRunLoop && agentGoal && agentOut) {
    agentRunLoop.addEventListener("click", function() {
      var goal = agentGoal.value.trim();
      if (!goal) {
        agentOut.textContent = "Enter a goal first.";
        return;
      }
      var ms = agentMaxSteps ? parseInt(agentMaxSteps.value, 10) : 15;
      prependAgentLog({ event: "user", action: "run_loop", goal: goal, maxSteps: ms });
      chrome.runtime.sendMessage({ type: "AUTONOMOUS_RUN_LOOP", goal: goal, maxSteps: ms }, function(resp) {
        if (chrome.runtime.lastError) {
          agentOut.textContent = "Error: " + chrome.runtime.lastError.message;
          return;
        }
        if (!resp || !resp.ok) {
          agentOut.textContent = "Could not start: " + (resp && resp.error ? resp.error : JSON.stringify(resp));
          return;
        }
        setLoopUiRunning();
      });
    });

    if (agentStopLoop) {
      agentStopLoop.addEventListener("click", function() {
        chrome.runtime.sendMessage({ type: "AUTONOMOUS_CANCEL" }, function() {});
      });
    }
    if (agentPause) {
      agentPause.addEventListener("click", function() {
        chrome.runtime.sendMessage({ type: "AUTONOMOUS_PAUSE" }, function() {});
      });
    }
    if (agentResume) {
      agentResume.addEventListener("click", function() {
        chrome.runtime.sendMessage({ type: "AUTONOMOUS_RESUME" }, function(resp) {
          if (resp && resp.ok) setLoopUiRunning();
        });
      });
    }
  }

  if (agentPlanBtn && agentRunBtn && agentGoal && agentOut) {
    agentPlanBtn.addEventListener("click", function() {
      var goal = agentGoal.value.trim();
      if (!goal) {
        agentOut.textContent = "Enter a goal first.";
        return;
      }
      lastAutonomousPlan = null;
      agentRunBtn.disabled = true;
      agentOut.textContent = "Planning...";
      chrome.runtime.sendMessage({ type: "AUTONOMOUS_PLAN_STEP", goal: goal, history: [] }, function(resp) {
        if (chrome.runtime.lastError) {
          agentOut.textContent = "Error: " + chrome.runtime.lastError.message;
          return;
        }
        if (!resp || !resp.ok) {
          agentOut.textContent = "Plan failed: " + (resp && resp.error ? resp.error : "?") + "\n\n" + JSON.stringify(resp, null, 2);
          return;
        }
        var plan = resp.plan;
        var copy = { tool: plan.tool, args: plan.args, reason: plan.reason };
        lastAutonomousPlan = copy;
        agentRunBtn.disabled = plan.tool === "done" || plan.tool === "none";
        agentOut.textContent =
          "Planner: " +
          (resp.plannerBackend === "openai" ? "OpenAI" : "Ollama") +
          " · model: " +
          (resp.model || "?") +
          "\n\n" +
          JSON.stringify(copy, null, 2) +
          "\n\n(snapshot: " +
          (resp.snapshot && resp.snapshot.elements ? resp.snapshot.elements.length : 0) +
          " elements)";
      });
    });

    agentRunBtn.addEventListener("click", function() {
      if (!lastAutonomousPlan) {
        agentOut.textContent = "Plan a step first.";
        return;
      }
      agentOut.textContent = "Running: " + lastAutonomousPlan.tool + "...";
      chrome.runtime.sendMessage(
        {
          type: "AUTONOMOUS_RUN_TOOL",
          tool: lastAutonomousPlan.tool,
          args: lastAutonomousPlan.args
        },
        function(resp) {
          if (chrome.runtime.lastError) {
            agentOut.textContent = "Error: " + chrome.runtime.lastError.message;
            return;
          }
          agentOut.textContent = JSON.stringify(resp, null, 2);
          if (resp && resp.ok && resp.finished) agentRunBtn.disabled = true;
        }
      );
    });
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
