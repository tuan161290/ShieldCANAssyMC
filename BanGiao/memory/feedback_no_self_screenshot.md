---
name: feedback-no-self-screenshot
description: "Don't self-capture WPF app window screenshots via PowerShell automation — guide the user to do it instead"
metadata: 
  node_type: memory
  type: feedback
  originSessionId: 21a17996-7c87-4a35-905d-1c0fd9147bf5
  modified: 2026-08-02T07:32:29.250Z
---

Never try to screenshot the running WPF app (or any window) myself via PowerShell
(`GetWindowRect`/`CopyFromScreen`/`SetForegroundWindow`/etc.) to debug a visual/UI issue in this
project. Instead, ask the user to take the screenshot and paste it into the chat, giving them
simple steps (e.g. `Win+Shift+S` for Snipping Tool region capture, or `Alt+PrintScreen` to capture
just the active window, then paste the image directly into the conversation).

**Why:** Tried this once (SO1C86-SHIELD CAN ASSY MC project, 2026-08-02) when the user reported
"TextBox bị overlap" across the app. The automation environment does not share the user's real
interactive desktop session — `GetWindowRect` returned coordinates for the real app window, but
`CopyFromScreen` at those coordinates captured whatever was on MY rendering surface (the VS
Code/Claude Code panel itself), not the actual app. Repeated attempts (bringing window to
foreground, raising z-order) all failed the same way and just burned time without producing a
usable image. The user explicitly said: "Lần sau cần chụp ảnh window hãy hướng dẫn tôi, bạn tự làm
rất mất thời gian và không chính xác được" (next time, guide me instead — doing it yourself wastes
time and isn't accurate).

**How to apply:** Whenever a task in this project would benefit from seeing the actual running UI
(layout bugs, visual regressions, "does this look right" questions), do NOT reach for
Bash/PowerShell screen-capture tricks. Ask the user directly for a screenshot (with brief capture
instructions if needed), or ask clarifying questions about which page/control and what's visually
wrong. This likely generalizes to any project on this machine where the agent and the user's
interactive desktop are different sessions.
