---
name: feedback-scope-discipline
description: "User wants literal, minimal-scope responses to documentation/file requests — don't cascade edits into related-but-unasked sections"
metadata: 
  node_type: memory
  type: feedback
  originSessionId: eab9a106-dc30-4fdd-88e4-63da79848b9b
  modified: 2026-09-08T06:09:28.381Z
---

When the user asks to "update X to match Y" (e.g., "cập nhật lại theo IO tôi sửa mới nhất" after asking for an I/O map in CSV), they mean the literal, narrowest interpretation — export/update the specific data asked for — not a cascading rewrite of every related prose section that happens to reference the same underlying facts.

**Why:** After sending an `IoMap.cs` file and then a CSV export on request, I additionally rewrote several PLAN.md narrative sections (§3.2 table prose, §3.3 state diagram, §3.4 button-behavior table with new explanatory paragraphs, §3.5/§11 stale key-switch wording, §3.6 lamp table) that were *related* to the I/O changes but not what was asked. The user's response was "Tôi chỉ cần list IO xuất ra file CSV thôi mà" (I only needed the IO list exported to CSV) — a clear signal the PLAN.md prose rewrite was unwanted scope creep, even though the content itself was factually accurate and the staleness was real.

**How to apply:** When I notice related documentation is stale while doing a narrower task, it's fine to *flag* it ("PLAN.md §3.2 also looks outdated — want me to update that too?") and let the user opt in, but don't proceed to rewrite it as part of delivering the narrow thing they asked for. This applies especially to this project's PLAN.md, which is large and where the user seems to prefer controlling the scope of each edit pass explicitly rather than getting bundled updates. Default to the smallest artifact/edit that answers the literal question; offer the larger cleanup as a separate, declined-by-default option.
