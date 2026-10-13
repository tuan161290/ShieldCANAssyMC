export const meta = {
  name: 'verify-vision-align-guide',
  description: 'Adversarially verify the Vietnamese Vision Align calibration guide (artifact 587a99f0) against the Hikrobot calibration PDF, VisionMaster 4.4.0 Help pages and the ShieldCanAssy source code',
  phases: [
    { title: 'Find', detail: '8 lenses + 16 seed claims' },
    { title: 'Dedup', detail: 'merge overlapping findings' },
    { title: 'Verify', detail: '3 independent refuters per finding (docs / code / impact)' },
    { title: 'Critic', detail: 'completeness pass, then one extra find+verify round' },
    { title: 'Confirm', detail: 'check the claims the guide gets right' },
  ],
}

const ART = 'C:/Users/Hoyoen/.claude/projects/e--OneDrive-Tuan-2026-Actro-SO1C86-SHIELD-CAN-ASSY-MC-ShieldCANAssyMC/65e89b4f-210b-435b-8c98-d9e24daa9c5f/tool-results/artifact-587a99f0-1789131031-24fb.html'
const SP = 'C:/Users/Hoyoen/AppData/Local/Temp/claude/e--OneDrive-Tuan-2026-Actro-SO1C86-SHIELD-CAN-ASSY-MC-ShieldCANAssyMC/65e89b4f-210b-435b-8c98-d9e24daa9c5f/scratchpad'
const PDF = SP + '/hik_calib_pdf.txt'
const HELP = SP + '/vm_help'
const TITLES = SP + '/vm_help_titles.txt'
const HELPRAW = 'C:/Program Files/VisionMaster4.4.0/Applications/Help/EN'
const CODE = 'E:/OneDrive/Tuan/2026/Actro/SO1C86-SHIELD CAN ASSY MC/ShieldCANAssyMC'
const SDK = 'C:/Program Files/VisionMaster4.4.0/Development/V4.x/Samples/C#/PlatformSDKSampleCS/GetResultControl/Form1.cs'

const CTX = `
## What you are verifying
A Vietnamese operator guide ("Cẩm nang Calib & Teaching") published as an HTML artifact. It documents the vision calibration + alignment workflow of a WPF/C# pick-and-place machine app (ShieldCanAssy) that drives HIKROBOT VisionMaster 4.4.0 through its PlatformSDK. The guide has: glossary, "Cách đọc sơ đồ Flow" (flow diagrams + output-name contract table), Bước 1 (CAM bottom-up checkerboard), Bước 2 (CAM top-down 9-point numpad + LocateTop flow), Bước 3 (Y sign), Bước 4 (Align calib per tool: AlignCalib_Tool1/2 and AlignBottom_Tool1/2 flows, 6 rotation angles), Bước 5 (hand-eye), Bước 6 (tray teaching), section "Output → phép tính Align" (formula), "Phụ lục — dựng Flow Align", checklist, and a <script> at the end that defines the flow diagrams (FLOWS object: node names + output bindings — treat this JS data as part of the guide's claims).

Guide file (READ ALL 1260 LINES, in 3-4 chunks with offset/limit): ${ART}

## Sources of truth
1. Hikrobot training PDF "Calibration and Locating Theory" (text extract, 61 pages, page markers "===== PAGE n ====="): ${PDF}
2. VisionMaster 4.4.0 Help pages (text extracts; see INDEX.txt for the list): ${HELP}/  — key files: 10_N-point_Calibration.txt, 11_Rotation_Calibration.txt, 12_Translation_Calibration.txt, 13_CalibBoard_Calibration.txt, 14_N-image_Calibration.txt, 18_Load_Calibration.txt, 20_Calibration_Transformation.txt, 40_High_Precision_Match.txt, 50_Single_Point_Alignment.txt, 51_Single_Point_Rectify.txt, 52_Single_Point_Grab.txt, 53_Single_Point_Map_Alignment.txt, 54_Rotate_Calculate.txt, 01..04 calibration configuration guides. Full list of ALL Help page titles (to check whether a module name exists at all): ${TITLES}. Raw HTML of any other Help page: ${HELPRAW}/<GUID>.html (grep titles file for the GUID).
3. App source (C#, comments in Vietnamese), root: "${CODE}" — relevant files:
   ShieldCanAssy.Core/Core.Controller/Routine_Calib.cs, PlacementSolver.cs, Routine_Auto.cs (S05/S20/S21/S3x), McControl.cs;
   ShieldCanAssy.Core/Core.Communication/VisionConnect.cs, IVisionDevice.cs, Sim/FakeVision.cs;
   ShieldCanAssy.Core/Lib.VisionMaster/VisionMasterDevice.cs;
   ShieldCanAssy.Core/Core.Data/SystemData.cs (VisionOutputNames defaults, thresholds), Core.Data.Model/ModelData.cs (CameraCalib, VisionCalib, IsUsable), Core.Data.Model/TrayLayout.cs;
   ShieldCanAssy.Wpf/ViewModels/VisionCalibViewModel.cs, Views/VisionEditView.xaml, Views/TeachingView.xaml(.cs), ViewModels/TeachingViewModel.cs, Views/ManualView.xaml, SelfTest.cs;
   PLAN.md section 6 (lines 706-1018) — the design doc the guide cites.
4. Official SDK sample showing how procedure outputs are read: ${SDK}

## Ground facts already established (do not re-derive; you may cite)
- App reads flow outputs with VmProcedure.ModuResult.GetOutputFloat(name) for ALL outputs (Found, PoseX, PoseY, PoseAngle, Score; and for CalibBottomUp: Found, ScaleU, ScaleV, SkewDeg, CornerCount, OriginPixelU, OriginPixelV); throws "thiếu output" if nValueNum < 1. Found is compared > 0.5.
- Default flow names: LocateTop, TopDownCalib, CalibBottomUp, AlignBottom_Tool1/_Tool2, AlignCalib_Tool1/_Tool2 (SystemData.VisionOutputNames).
- The numpad 9-point path (VisionCalibViewModel.RunTopDownGridPointAsync / RunAutoCalibAsync 1→9) only moves + triggers TopDownCalib; nothing writes Calib.TopDown. Routine_Calib.CalibrateTopDownAsync (auto fit) exists but is only called by SelfTest.
- BottomUpYSign is set by the UI but consumed nowhere (PlacementSolver dropped it).
- The bottom-up camera is fixed on the frame looking up; the gantry carrying the nozzles moves in X (and Z, and rotates A1/A2); Y1/Y2 are tray axes. The top-down camera rides on the gantry (X) and looks at the assembly tray (Y2).
- Teaching page: buttons "Tới T1 Trigger"/"Tới T2 Trigger" move ONLY X to AlignPosX_Nozzle1/2 and require double-click; vacuum buttons are labelled "Hút #1"/"Hút #2" (state by colour); "Lưu" saves the model. Jog speed toggle "Chậm"/"Nhanh" lives on the Teaching page (global), not on the Manual page.
- No VisionMaster SDK sample sets a global variable, sends a string into a procedure, or sets module parameters at runtime; the app only calls VmProcedure.Run().

## Output rules
- A finding = one concrete statement (or omission) in the guide that is wrong, refers to something that does not exist in VisionMaster 4.4.0, contradicts the source code, or would make an operator following the guide fail. Not style.
- 'claim': quote the guide's own text (Vietnamese) briefly and name the section/step ('Bước 4 bước 5', 'Phụ lục §2 bước 4', 'script FLOWS.AlignBottom_Tool1', ...).
- 'evidence': file path + line numbers, or Help page name + a quoted sentence, or 'PDF p.NN' + quoted sentence. Quotes stay in the original language.
- 'problem' and 'fix': write in Vietnamese, concrete, 1-4 sentences each.
- severity: critical = would produce wrong placements / calibration cannot work as described; major = procedure fails or blocks, or a module/parameter is misdescribed in a way that misleads the operator; minor = wrong label/number/name with no functional impact; info = plausible concern you could not verify (say 'cần xác minh trong VM').
- Be exhaustive within your lens. Do not pad with duplicates of the same root cause.
`

const FINDINGS_SCHEMA = {
  type: 'object',
  properties: {
    findings: {
      type: 'array',
      items: {
        type: 'object',
        properties: {
          section: { type: 'string' },
          claim: { type: 'string' },
          problem: { type: 'string' },
          evidence: { type: 'string' },
          severity: { type: 'string', enum: ['critical', 'major', 'minor', 'info'] },
          category: { type: 'string' },
          fix: { type: 'string' },
        },
        required: ['section', 'claim', 'problem', 'evidence', 'severity', 'category', 'fix'],
      },
    },
  },
  required: ['findings'],
}

const MERGED_SCHEMA = {
  type: 'object',
  properties: {
    findings: {
      type: 'array',
      items: {
        type: 'object',
        properties: {
          id: { type: 'string' },
          section: { type: 'string' },
          claim: { type: 'string' },
          problem: { type: 'string' },
          evidence: { type: 'string' },
          severity: { type: 'string', enum: ['critical', 'major', 'minor', 'info'] },
          category: { type: 'string' },
          fix: { type: 'string' },
          merged_from: { type: 'integer' },
        },
        required: ['id', 'section', 'claim', 'problem', 'evidence', 'severity', 'category', 'fix', 'merged_from'],
      },
    },
  },
  required: ['findings'],
}

const VERDICT_SCHEMA = {
  type: 'object',
  properties: {
    refuted: { type: 'boolean' },
    confidence: { type: 'string', enum: ['low', 'medium', 'high'] },
    reasoning: { type: 'string' },
    corrected: { type: 'string' },
    severity_suggested: { type: 'string', enum: ['critical', 'major', 'minor', 'info'] },
  },
  required: ['refuted', 'confidence', 'reasoning', 'corrected', 'severity_suggested'],
}

const CRITIC_SCHEMA = {
  type: 'object',
  properties: {
    gaps: {
      type: 'array',
      items: {
        type: 'object',
        properties: {
          area: { type: 'string' },
          why: { type: 'string' },
          prompt: { type: 'string' },
        },
        required: ['area', 'why', 'prompt'],
      },
    },
  },
  required: ['gaps'],
}

const CORRECT_SCHEMA = {
  type: 'object',
  properties: {
    correct: {
      type: 'array',
      items: {
        type: 'object',
        properties: {
          section: { type: 'string' },
          claim: { type: 'string' },
          evidence: { type: 'string' },
        },
        required: ['section', 'claim', 'evidence'],
      },
    },
  },
  required: ['correct'],
}

const LENSES = [
  { key: 'vm-modules', prompt: `LENS: VisionMaster module existence, exact names, parameters and result names. Check EVERY module name and EVERY module-result/output name the guide uses or draws (in prose, in the contract table, and in the FLOWS JS object at the end: e.g. 'Image Source', 'High Precision Match', 'Rectangle Search', 'Calib Board Calibration', 'N-Point Calibration', 'Calib Transformation', 'Position Correction', 'Match Num', 'Match Point X (pixel)', 'Angle (°)', 'Score', 'dX (mm, đã bù)', 'Teach chuẩn / Teach Standard Pose', 'nạp điểm / capture point', 'Tính / Fit', 'chế độ có xoay', 'số điểm = 6'). For each: does it exist in VisionMaster 4.4.0 under that name (use the titles file + Help extracts), what is the real name, what are the real parameters/results and their types (Int/Float/String) and units (radians vs degrees, px/mm vs mm/px, score 0..1). Also check the guide's description of what each module needs as predecessor (e.g. Single Point Alignment needs physical coordinates → Calibration Transformation before it).` },
  { key: 'npoint-mechanics', prompt: `LENS: mechanics of N-point / Translation / Rotation / N-image calibration as the guide describes them for Bước 2 (TopDownCalib, 9-point numpad) and Bước 4 (AlignCalib_Tool1/2, six 60° rotations). Check against Help 10/11/12/14 and PDF p.13-27: Trigger Acquisition vs Manual Input (is there a manual 'nạp điểm' button, or does every flow run add a point?); how physical coordinates get into the module (auto-generated from InitPoint/Offset/Movement First/Commutation Number — S-shaped — vs the app's numpad order 1..9 raster, see VisionCalibViewModel.TopDownGridOffset and RunAutoCalibAsync; vs Receive Data); Clear Point; Demonstration signal; Camera Mode (Camera Stationary Up/Down vs Dynamic Camera) for each camera; Use Relative Coordinates / Calibration Origin; 'rotation is performed at the position of the 5th point'; Translation Times / Rotation Times minimums and whether a rotation-only or X-only-translation calibration is possible for the fixed bottom-up camera (gantry has only X + A over it); what Rotation Calibration can and cannot output; Update File / Create Calibration File / Ctrl+S; what happens to the point list when test buttons ('Trigger Flow TopDownCalib', 'Trigger AlignCalib (không xoay)') are pressed.` },
  { key: 'hik-theory', prompt: `LENS: Hikrobot positioning theory (PDF p.13-60: N-point with/without rotation, moving camera third-column property p.20, single point alignment/grab/rectify formulas p.30-38, conclusions p.39-40, typical scenarios p.42-54, trilogy calibration→teaching→calculation p.57-60) versus (a) the guide's section 'Output nào đi vào phép tính Align' and its formula block, and (b) PlacementSolver.Solve (PlacementSolver.cs) and Routine_Auto S2x_Align/S3x_LocateAndPlace. Questions: is 'p' (PoseX/PoseY of AlignBottom) defined consistently — offset of product centre from the ROTATION CENTRE in machine-axis mm, un-rotated — and would a VisionMaster calculation module (Single Point Rectify/Grab/Alignment) output that, or would it output a mechanism offset that already includes the rotation-induced translation (double compensation with the app's e = R(Δθ)·p)? Is Δθ = θt − θp legitimate when θt is a raw top-down image angle and θp is an angle in the bottom-up calibration frame (mirror image from below; camera mounting rotation; sign convention 'clockwise positive')? Is the hand-eye + captureX + tdx construction consistent with p.20/p.45? Does the guide state the required 'photography posture unchanged' condition (AlignPosX_NozzleN must equal the calibration position)?` },
  { key: 'code-consistency', prompt: `LENS: every UI label, dialog message, status text, gating condition, default value, flow/output name, and function the guide mentions, compared with the C# source: VisionCalibViewModel.cs, VisionEditView.xaml, Routine_Calib.cs, VisionConnect.cs, VisionMasterDevice.cs, Routine_Auto.cs, ModelData.cs, SystemData.cs, TeachingView.xaml + TeachingViewModel.cs, ManualView.xaml, McControl.cs, PLAN.md §6. Check: quoted dialog strings ('Hiệu chuẩn CAM bottom-up ĐẠT…', 'Auto Calib hoàn tất…', 'Điểm N: …', 'Đã kích hoạt Flow…', 'Chưa lưu Calibration Center Point', hand-eye messages), status line formats and whether they can actually show what the guide's 'Kết quả đúng' boxes say (e.g. TopDown status after the numpad path), the 'Cần xử lý' box's claims about CalibrateTopDownAsync/--selftest/IsUsable, the contract table (7 flows / 5 outputs / ImageData optional / CalibBottomUp outputs), the 'Hai lỗi hay gặp' box (missing-output exception ordering), the glossary flow names (AlignBottomTool1/2 vs AlignBottom_Tool1), which Auto states trigger which flow (S05/S20/S21/S3x), thresholds (15°, 3 mm, 5 shots), which buttons exist and what they move, double-click requirement, and any code path the guide says exists but doesn't (or vice versa).` },
  { key: 'checkerboard', prompt: `LENS: Bước 1 + the CalibBottomUp flow diagram + Bước 3 (Y sign). Compare the guide with Help 13_CalibBoard_Calibration (module results: Calibration Error, Scale, CalibOriPointX/Y, PosXVector/PosYVector, CalibPointNum, TranslateX/Y, Rotate, Skew (radians), Aspect, Pixel Accuracy; run parameters Origin/Rotation Angle/Coordinate System Mode/Physical Size/board types incl. Hikrobot coded boards) and with Routine_Calib.CalibrateBottomUpAsync (B2/B3/B4 logic: ScaleU, SkewDeg treated as degrees, OriginPixelU difference between two X positions used to derive scaleFromAxis). Questions: can the 7 outputs the guide names be bound from real module results, with correct types/units? Is the B3 cross-check valid when the checkerboard is larger than the FOV (does 'first corner point in the upper left' stay the same physical corner after the board slides 20 mm)? Where must the board be mounted for it to move with X (guide says 'jog X và trục hàng')? Is 'vẫn còn trong khung hình' the right condition? Is the Bước 3 physical test (move Y2 by +5 mm and re-measure with the bottom camera) physically meaningful given Y2 is the tray axis? Is BottomUpYSign consumed anywhere (grep)? Does the 'Kết quả đúng' status line ('812 điểm', 'góc 0.08°', RMS) match what the code can produce?` },
  { key: 'sdk-contract', prompt: `LENS: the SDK/data contract between app and solution as the guide states it ('Cách đọc sơ đồ Flow', contract table, 'Hai lỗi hay gặp', Phụ lục step 'Khai Output của Flow'). Compare with VisionMasterDevice.cs, VisionConnect.cs, IVisionDevice.cs, FakeVision.cs, SystemData.VisionOutputNames and the SDK sample ${SDK} (procedure-level output reading, EnableResultCallback, GetOutputImageV2). Questions: are flow-level output variables the right place (vs module results)? Can a Float flow output be bound to an Int module result (MatchNum, Module Status, CalibPointNum) — if unknown, say so and propose a safe alternative (e.g. bind CalibStatus Float, or a Script/Format module)? Is 'Found ← Match Num (App so > 0.5)' consistent? Score units (MatchScore 0..1 vs app 0..100)? Is the claim 'Pose rỗng → App vẫn coi là không thấy, log ghi thiếu output' exactly right given the read order in RunAndGetFloatsWithImage? ImageData optional behaviour? Timeout behaviour? Any output the app reads that the guide forgot, or vice versa?` },
  { key: 'operator-steps', prompt: `LENS: procedural soundness of Bước 1-6 as an operator would physically perform them. For each numbered step ask: is the prerequisite stated (Z_Pick taught before checkerboard height; Calibration Center saved; Tool selected; product held; N-point point list cleared; calibration file created and referenced; solution saved), does the action move what the guide says it moves (Y2 is the tray axis, X is the gantry, A1/A2 rotate nozzles; 'Tới T1 Trigger' moves X only), is the described observation actually observable on the app ('dòng trạng thái', dialogs), is the teach image for the 'mẫu chuẩn' taken through the right flow (calibration flow vs runtime flow with Create Reference), are the 'làm 2 lần Tool 1 & Tool 2' loops complete, does Bước 5 hand-eye work given TopDown.IsCalibrated is never set by the numpad path, does Bước 6 (tray teaching) claim anything about vision that is wrong, and does the final checklist match reality. Also flag any step whose failure mode the 'Nếu báo lỗi' box misattributes.` },
]

// ---------- Phase 1: Find ----------
phase('Find')
log('Find: 8 lenses in parallel (+ correct-points confirmer)')
const rawResults = await parallel(LENSES.map(l => () =>
  agent(CTX + '\n\n' + l.prompt + '\n\nReturn every finding for this lens.', { label: 'find:' + l.key, phase: 'Find', schema: FINDINGS_SCHEMA, effort: 'high' })
))
const found = rawResults.filter(Boolean).flatMap(r => r.findings || [])
const seeds = Array.isArray(args && args.seeds) ? args.seeds : []
log('Find: ' + found.length + ' raw findings from lenses + ' + seeds.length + ' seed claims')

// ---------- Phase 2: Dedup (barrier is required: cross-item merge) ----------
phase('Dedup')
const all = found.concat(seeds)
const merged = await agent(CTX + `

TASK: You receive ${all.length} candidate findings (JSON below) produced by independent reviewers and by the lead reviewer's seed list. Merge findings that share the same ROOT CAUSE or the same wrong statement in the guide into ONE finding: keep the clearest claim quote, union the evidence (keep all distinct citations), keep the strongest severity, keep the most concrete fix. Do NOT merge findings that concern different statements even if in the same section. Do NOT drop any distinct finding. Assign ids F01, F02, ... ordered critical → major → minor → info. Set merged_from = number of source findings merged. Keep 'problem' and 'fix' in Vietnamese.

CANDIDATES:
` + JSON.stringify(all, null, 1), { label: 'dedup', phase: 'Dedup', schema: MERGED_SCHEMA, effort: 'high' })
let findings = (merged && merged.findings) || []
log('Dedup: ' + all.length + ' → ' + findings.length + ' distinct findings')

const VERIFY_CAP = 48
if (findings.length > VERIFY_CAP) {
  log('Verify cap: verifying first ' + VERIFY_CAP + ' of ' + findings.length + ' (dropped ' + (findings.length - VERIFY_CAP) + ' lowest-severity)')
  findings = findings.slice(0, VERIFY_CAP)
}

// ---------- Phase 3: Verify (pipeline: each finding independently) ----------
const REFUTERS = [
  { key: 'docs', prompt: 'REFUTER LENS = DOCUMENT ACCURACY. Using ONLY the Hikrobot PDF text and the VisionMaster Help pages, try to REFUTE this finding: is its statement about VisionMaster/Hikrobot modules, parameters, result names, units, or theory wrong, overstated, or actually supported by the guide after all? Read the guide passage AND the cited Help/PDF passages yourself; do not trust the citation blindly.' },
  { key: 'code', prompt: 'REFUTER LENS = CODE ACCURACY. Using ONLY the app source code and PLAN.md, try to REFUTE this finding: does the code really behave as the finding says? Open the cited files and lines. Are line numbers and quoted strings right? Is there another code path that makes the guide correct?' },
  { key: 'impact', prompt: 'REFUTER LENS = PRACTICAL IMPACT AND FIX. Assume the cited facts are true. Try to REFUTE that this matters: would an operator following the guide as written actually get a wrong placement, a blocked wizard, or a misconfigured VisionMaster flow? Could the severity be lower? Is the proposed fix itself correct per the sources (check the Help/PDF for the replacement module or parameter it names)? If the fix is wrong or unverifiable, say so in corrected.' },
]

// Tiered verification keeps the run near the approved agent budget:
// critical/major → 3 independent refuters each; minor/info → batches of 4, one combined refuter per batch.
const RULES_ONE = `

Rules: set refuted=true ONLY if you find concrete contrary evidence (quote it in reasoning). If you cannot verify either way, refuted=false with confidence 'low' and say what test in VisionMaster would settle it. 'corrected' = the finding's problem/fix rewritten in Vietnamese as you would state it after your check (keep it if unchanged). Suggest a severity.`
const COMBINED_PROMPT = 'REFUTER LENS = COMBINED (documents + code + impact). For EACH finding below, independently: open the guide passage and every cited source yourself (Help/PDF text extract, code file:line). Try to REFUTE it: is the cited name/value/behaviour actually different, is the guide in fact correct, or would it not mislead an operator?'
const BATCH_SCHEMA = {
  type: 'object',
  properties: {
    verdicts: {
      type: 'array',
      items: {
        type: 'object',
        properties: {
          id: { type: 'string' },
          refuted: { type: 'boolean' },
          confidence: { type: 'string', enum: ['low', 'medium', 'high'] },
          reasoning: { type: 'string' },
          corrected: { type: 'string' },
          severity_suggested: { type: 'string', enum: ['critical', 'major', 'minor', 'info'] },
        },
        required: ['id', 'refuted', 'confidence', 'reasoning', 'corrected', 'severity_suggested'],
      },
    },
  },
  required: ['verdicts'],
}
const isHeavy = f => f.severity === 'critical' || f.severity === 'major'

function verifyHeavy(f, phaseName, prefix) {
  return parallel(REFUTERS.map(r => () =>
    agent(CTX + '\n\n' + r.prompt + `

FINDING UNDER TEST (${f.id}, severity ${f.severity}):
` + JSON.stringify(f, null, 1) + RULES_ONE,
      { label: prefix + ':' + f.id + ':' + r.key, phase: phaseName, schema: VERDICT_SCHEMA, effort: 'high' })
  )).then(votes => ({ finding: f, votes: votes.map((v, i) => ({ lens: REFUTERS[i].key, verdict: v })) }))
}

function verifyBatch(batch, phaseName, prefix) {
  return agent(CTX + '\n\n' + COMBINED_PROMPT + `

FINDINGS UNDER TEST (return exactly one verdict per id):
` + JSON.stringify(batch, null, 1) + RULES_ONE,
    { label: prefix + ':' + batch.map(b => b.id).join('+'), phase: phaseName, schema: BATCH_SCHEMA, effort: 'high' })
    .then(res => batch.map(f => {
      const v = res && Array.isArray(res.verdicts) ? res.verdicts.find(x => x.id === f.id) : null
      return { finding: f, votes: [{ lens: 'combined', verdict: v || null }] }
    }))
}

async function verifyAll(list, phaseName, prefix) {
  const heavy = list.filter(isHeavy)
  const light = list.filter(f => !isHeavy(f))
  const batches = []
  for (let i = 0; i < light.length; i += 4) batches.push(light.slice(i, i + 4))
  log(phaseName + ': ' + heavy.length + ' critical/major × 3 refuters; ' + light.length + ' minor/info in ' + batches.length + ' batch(es) × 1 combined refuter')
  const both = await Promise.all([
    pipeline(heavy, f => verifyHeavy(f, phaseName, prefix)),
    parallel(batches.map(b => () => verifyBatch(b, phaseName, prefix + '-batch'))),
  ])
  return both[0].filter(Boolean).concat(both[1].filter(Boolean).flat())
}

phase('Verify')
const verified = await verifyAll(findings, 'Verify', 'verify')

function tally(item) {
  const vs = item.votes.filter(v => v.verdict)
  const notRefuted = vs.filter(v => !v.verdict.refuted).length
  const low = vs.some(v => !v.verdict.refuted && v.verdict.confidence === 'low')
  const need = item.votes.length >= 3 ? 2 : 1
  return { notRefuted, total: vs.length, need, lowConfidence: low }
}
const confirmed = []
const refuted = []
for (const item of verified) {
  const t = tally(item)
  const rec = Object.assign({}, item.finding, { votes: item.votes, tally: t })
  if (t.total > 0 && t.notRefuted >= t.need) confirmed.push(rec); else refuted.push(rec)
}
log('Verify: ' + confirmed.length + ' confirmed, ' + refuted.length + ' refuted')

// ---------- Phase 4: Critic + one extra round ----------
phase('Critic')
const critic = await agent(CTX + `

TASK: completeness critic. Below are the CONFIRMED findings and the REFUTED ones so far. Read the whole guide again and list the areas/claims of the guide that NO finding covers yet but that could still be wrong or missing — e.g. sections, steps, diagram bindings (FLOWS JS), the checklist, the footer's references, numeric examples, the 'Cách đọc sơ đồ Flow' legend, Bước 5/6, the glossary. For each gap give a precise finder prompt (English, 2-4 sentences, naming the guide passage and the source to check). Max 5 gaps, most valuable first. If coverage is complete, return an empty list.

CONFIRMED:
` + JSON.stringify(confirmed.map(c => ({ id: c.id, section: c.section, claim: c.claim, problem: c.problem })), null, 1) + `

REFUTED:
` + JSON.stringify(refuted.map(c => ({ id: c.id, section: c.section, claim: c.claim })), null, 1),
  { label: 'critic', phase: 'Critic', schema: CRITIC_SCHEMA, effort: 'high' })

const gaps = (critic && critic.gaps) || []
log('Critic: ' + gaps.length + ' gaps')
let round2Confirmed = []
let round2Refuted = []
if (gaps.length) {
  const extraRaw = await parallel(gaps.map((g, i) => () =>
    agent(CTX + '\n\nLENS (gap ' + (i + 1) + ', ' + g.area + '): ' + g.prompt + '\n\nAlready-known findings (do NOT repeat these root causes): ' + confirmed.map(c => c.id + ' ' + c.section + ': ' + c.problem.slice(0, 120)).join(' | ') + '\n\nReturn only NEW findings.',
      { label: 'find2:' + g.area.slice(0, 30), phase: 'Critic', schema: FINDINGS_SCHEMA, effort: 'high' })
  ))
  const extra = extraRaw.filter(Boolean).flatMap(r => r.findings || [])
  log('Round 2: ' + extra.length + ' new raw findings')
  if (extra.length) {
    const merged2 = await agent(CTX + `

TASK: You receive NEW candidate findings from a second review round plus the list of ALREADY CONFIRMED findings. Drop any new candidate whose root cause is already covered by a confirmed finding. Merge the remaining new candidates among themselves (same rules as before). Assign ids G01, G02, ... Keep 'problem'/'fix' in Vietnamese.

ALREADY CONFIRMED (for exclusion only):
` + JSON.stringify(confirmed.map(c => ({ id: c.id, section: c.section, problem: c.problem })), null, 1) + `

NEW CANDIDATES:
` + JSON.stringify(extra, null, 1), { label: 'dedup2', phase: 'Critic', schema: MERGED_SCHEMA, effort: 'high' })
    let f2 = (merged2 && merged2.findings) || []
    if (f2.length > 10) { log('Round 2 cap: verifying 10 of ' + f2.length + ' (dropped ' + (f2.length - 10) + ' lowest-severity)'); f2 = f2.slice(0, 10) }
    const v2 = await verifyAll(f2, 'Critic', 'verify2')
    for (const item of v2) {
      const t = tally(item)
      const rec = Object.assign({}, item.finding, { votes: item.votes, tally: t })
      if (t.total > 0 && t.notRefuted >= t.need) round2Confirmed.push(rec); else round2Refuted.push(rec)
    }
    log('Round 2: ' + round2Confirmed.length + ' confirmed, ' + round2Refuted.length + ' refuted')
  }
}

// ---------- Phase 5: Confirm what the guide gets right ----------
phase('Confirm')
const correctRaw = await agent(CTX + `

LENS: CORRECT POINTS. List the substantive claims in the guide that ARE verified correct against the sources (flow/output names matching SystemData defaults, the S05/S20/S21/S3x mapping, the formula structure vs PDF p.60 / PlacementSolver, the moving-camera third-column reasoning, the hand-eye method vs PLAN §6.5, dialog strings that match the code, the 'Hai lỗi hay gặp' box, the checkerboard height requirement, 5-shot averaging, etc.). Each entry: section, the claim (short Vietnamese quote), evidence (file:line or Help/PDF quote). Only include claims you actually checked. 10-25 entries. Do NOT include claims that overlap these already-confirmed problems: ` + confirmed.concat(round2Confirmed).map(c => c.section + ': ' + c.claim.slice(0, 80)).join(' | '),
  { label: 'correct-points', phase: 'Confirm', schema: CORRECT_SCHEMA, effort: 'high' })
const correctPoints = (correctRaw && correctRaw.correct) || []
const indexed = correctPoints.map((c, i) => Object.assign({ id: 'C' + (i + 1) }, c))
const checkedRes = indexed.length ? await agent(CTX + `

Check, one by one, each claim below that the lister says the guide gets RIGHT. Try to REFUTE each (open the cited source and the guide passage yourself). refuted=true only with concrete contrary evidence. corrected = the claim restated precisely in Vietnamese. severity_suggested: put 'info' (not used). Return exactly one verdict per id.

CLAIMS:
` + JSON.stringify(indexed, null, 1), { label: 'confirm:check-all', phase: 'Confirm', schema: BATCH_SCHEMA, effort: 'high' }) : null
const verdictOf = c => (checkedRes && Array.isArray(checkedRes.verdicts)) ? checkedRes.verdicts.find(x => x.id === c.id) : null
const correctFinal = indexed.filter(c => { const v = verdictOf(c); return v && !v.refuted }).map(c => Object.assign({}, c, { corrected: verdictOf(c).corrected }))
log('Confirm: ' + correctFinal.length + '/' + correctPoints.length + ' correct points held')

return {
  confirmed: confirmed.concat(round2Confirmed),
  refuted: refuted.concat(round2Refuted),
  gaps,
  correctPoints: correctFinal,
  stats: { rawFindings: found.length, seeds: seeds.length, merged: (merged && merged.findings ? merged.findings.length : 0), confirmed: confirmed.length + round2Confirmed.length, refuted: refuted.length + round2Refuted.length },
}
