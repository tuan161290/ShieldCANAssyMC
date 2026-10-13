using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using ShieldCanAssy.Core.Lang;

namespace ShieldCanAssy.Core.Communication
{
    /// <summary>
    /// Một output mà Flow <b>đang khai</b> trong solution — đọc thẳng từ tab Output Params qua SDK
    /// (<c>ProcedureResult.GetAllOutputNameInfo</c>), không phải thứ App mong đợi.
    /// </summary>
    public class VmDeclaredOut
    {
        public string Name { get; set; }

        /// <summary>Mã kiểu của SDK (<c>IMVS_MODULE_BASE_DATA_TYPE</c>) — giữ nguyên số để lớp trên không
        /// phải tham chiếu assembly VisionMaster.</summary>
        public int TypeCode { get; set; }

        /// <summary>Tên kiểu cho người đọc: "float", "int", hoặc mã thô nếu là kiểu khác.</summary>
        public string TypeText { get; set; }

        /// <summary>App đọc được thành số hay không — <c>GetOutputFloat</c> rồi <c>GetOutputInt</c>
        /// (xem <c>VisionMasterDevice.TryReadNumber</c>). Kiểu khác thì Flow vẫn chạy, chỉ có điều
        /// App báo "thiếu output" y như khi output không tồn tại.</summary>
        public bool Numeric { get; set; }
    }

    /// <summary>Một dòng trong tab Input Params / Output Params của một MODULE bên trong Flow.</summary>
    public class VmIoInfo
    {
        public string Name { get; set; }

        /// <summary>Tên kiểu VisionMaster hiện ở cột Type.</summary>
        public string TypeText { get; set; }

        /// <summary>Ô Subscribe trỏ vào đâu. RỖNG nghĩa là chưa bind — với Input Params của Script thì
        /// đây là lỗi im lặng nguy hiểm nhất: Script vẫn biên dịch, số vào luôn bằng 0.</summary>
        public string Source { get; set; }

        public bool Bound { get { return !string.IsNullOrEmpty(Source); } }
    }

    /// <summary>Một module bên trong Flow, kèm hai tab tham số của nó.</summary>
    public class VmModuleIo
    {
        public string Name { get; set; }
        public List<VmIoInfo> Inputs { get; } = new List<VmIoInfo>();
        public List<VmIoInfo> Outputs { get; } = new List<VmIoInfo>();
    }

    /// <summary>Những gì đọc được của MỘT Flow. <see cref="Error"/> khác rỗng nghĩa là không đọc được gì.</summary>
    public class VmFlowOutputs
    {
        public string Flow { get; set; }
        public string Error { get; set; }
        public List<VmDeclaredOut> Declared { get; } = new List<VmDeclaredOut>();

        /// <summary>Số Input Params Flow còn khai. −1 = SDK không trả lời được.</summary>
        public int InputParamCount { get; set; } = -1;

        /// <summary>Các module bên trong Flow (kể cả module nằm trong Group). Rỗng nghĩa là SDK không
        /// duyệt được — phần kiểm Script sẽ nói thẳng thay vì báo thiếu sạch.</summary>
        public List<VmModuleIo> Modules { get; } = new List<VmModuleIo>();

        public static VmFlowOutputs Fail(string flow, string why)
        {
            return new VmFlowOutputs { Flow = flow, Error = why };
        }
    }

    /// <summary>Bộ output mà hợp đồng đòi ở một Flow — dựng từ chính các hàm tạo lời gọi của
    /// <c>VmCycle</c> nên không thể lệch với thứ App thật sự đọc lúc chạy.</summary>
    public class VmFlowExpect
    {
        public string Flow { get; set; }

        /// <summary>Mọi tên phải có trong tab Output Params. Ở đây KHÔNG chia bắt buộc / tuỳ chọn như
        /// lúc chạy: <c>TopX_mm</c> được phép rỗng khi không thấy vật, nhưng <b>khai thiếu</b> nó thì
        /// vẫn là lỗi dựng solution.</summary>
        public List<string> Names { get; } = new List<string>();

        /// <summary>Tên output ảnh — có thì tốt (trang Main hiện ảnh), thiếu không phải lỗi.</summary>
        public string ImageName { get; set; }

        /// <summary>Một câu nhắc riêng của Flow này, in kèm khi có chuyện.</summary>
        public string Note { get; set; }
    }

    /// <summary>
    /// Hai tab tham số mà một module <b>Script</b> phải khai — theo cẩm nang v14.
    ///
    /// <para><b>Tên ở tầng này là tự do.</b> Script gọi biến bằng đúng tên khai ở hai tab, nên đặt tên khác
    /// cẩm nang mà mã cũng đổi theo thì vẫn chạy; gõ lệch giữa tab và mã thì VisionMaster báo <b>lỗi biên
    /// dịch</b>, thấy ngay. Vì vậy phần đối chiếu tên ở đây là "khác cẩm nang", không phải "sai".</para>
    ///
    /// <para>Thứ thật sự im lặng là <b>ô Subscribe bỏ trống</b>: Script biên dịch trót lọt, số vào bằng 0,
    /// không có lỗi nào hiện ra ở đâu cả. Đó là thứ chính của phép kiểm này.</para>
    /// </summary>
    public class VmScriptExpect
    {
        public string Flow { get; set; }

        /// <summary>Tên module Script trong cẩm nang — chỉ để gọi tên trong báo cáo.</summary>
        public string ScriptName { get; set; }

        public List<string> Inputs { get; } = new List<string>();
        public List<string> Outputs { get; } = new List<string>();
    }

    /// <summary>Hợp đồng của ba Script: <c>TopAbs</c> trong LocateTop, <c>Cmd</c> trong hai Flow
    /// AlignBottom — mỗi Flow một Script, đúng ba Flow của App (2026-10-08).</summary>
    public static class VmScriptContract
    {
        public static readonly string[] TopAbsIn =
        {
            "TransPointX_mm", "TransPointY_mm", "TransPointA_deg",
            "EchoX_mm", "EchoY2_mm", "EchoRunId", "MatchNum"
        };
        public static readonly string[] TopAbsOut = { "TopFound", "TopX_mm", "TopY_mm", "TopA_deg", "TopRunId" };

        public static readonly string[] CmdIn =
        {
            "HasTarget", "TarX_mm", "TarY_mm", "AlignSign",
            "DeltaX_mm", "DeltaY_mm", "DeltaTheta_deg", "SpaStatus",
            "ObjX_mm", "ObjY_mm", "EchoA_deg", "EchoRunId"
        };
        public static readonly string[] CmdOut =
        {
            "Status", "CmdX_mm", "CmdY2_mm", "CmdA_deg",
            "DiagDA_deg", "DiagEcc_mm", "DiagTarX_mm", "DiagTarY_mm", "ObjRunId"
        };

        public static List<VmScriptExpect> Build(Data.VisionOutputNames n)
        {
            var list = new List<VmScriptExpect>();
            if (n == null) return list;

            list.Add(Make(n.FlowLocateTop, "TopAbs", TopAbsIn, TopAbsOut));
            list.Add(Make(n.FlowAlignBottomTool1, "Cmd", CmdIn, CmdOut));
            list.Add(Make(n.FlowAlignBottomTool2, "Cmd", CmdIn, CmdOut));
            return list;
        }

        private static VmScriptExpect Make(string flow, string script, string[] ins, string[] outs)
        {
            var e = new VmScriptExpect { Flow = flow, ScriptName = script };
            e.Inputs.AddRange(ins);
            e.Outputs.AddRange(outs);
            return e;
        }
    }

    public enum EVmOutState
    {
        /// <summary>Có, đúng kiểu số.</summary>
        Ok,
        /// <summary>Hợp đồng đòi mà Flow không khai.</summary>
        Missing,
        /// <summary>Có tên nhưng kiểu App không đọc được thành số.</summary>
        WrongType,
        /// <summary>Flow khai một tên không có trong hợp đồng, mà lại rất giống một tên đang thiếu.</summary>
        Typo,
        /// <summary>Flow khai thêm tên ngoài hợp đồng — App không đọc, không phải lỗi.</summary>
        Extra,
        /// <summary>Input Params của Script khai rồi nhưng bỏ trống ô Subscribe — Script vẫn biên dịch,
        /// số vào luôn bằng 0, không có lỗi nào hiện ra.</summary>
        Unbound
    }

    public class VmOutItem
    {
        public string Flow { get; set; }
        public string Name { get; set; }
        public EVmOutState State { get; set; }
        public string TypeText { get; set; }

        /// <summary>Với <see cref="EVmOutState.Typo"/>: tên đúng theo hợp đồng.</summary>
        public string Suggest { get; set; }
    }

    /// <summary>Kết quả cả lượt kiểm — ba Flow.</summary>
    public class VmOutCheckReport
    {
        /// <summary>Lỗi chặn cả lượt (chưa nạp solution…).</summary>
        public string Error { get; set; }

        public List<VmOutItem> Items { get; } = new List<VmOutItem>();

        /// <summary>Flow không đọc được: không có trong solution, hoặc SDK không trả danh sách.</summary>
        public List<string> FlowErrors { get; } = new List<string>();

        /// <summary>Chuyện đáng nói nhưng không phải lỗi (Input Params còn sót…).</summary>
        public List<string> Notes { get; } = new List<string>();

        /// <summary>Bảng Input/Output của từng Script, in nguyên khối ở cuối báo cáo.</summary>
        public List<string> ScriptBlocks { get; } = new List<string>();

        public int Count(EVmOutState state) { return Items.Count(i => i.State == state); }

        /// <summary>Không có gì phải sửa.</summary>
        public bool Clean
        {
            get
            {
                return string.IsNullOrEmpty(Error) && FlowErrors.Count == 0 &&
                       Count(EVmOutState.Missing) == 0 && Count(EVmOutState.WrongType) == 0 &&
                       Count(EVmOutState.Typo) == 0 && Count(EVmOutState.Unbound) == 0;
            }
        }

        public static VmOutCheckReport Fail(string error) { return new VmOutCheckReport { Error = error }; }

        public string ToText()
        {
            var sb = new StringBuilder();
            if (!string.IsNullOrEmpty(Error)) return Loc.F("Vm.ReportError", Error);

            sb.Append(Loc.F("Vm.OutOkCount", Count(EVmOutState.Ok)));
            sb.Append(Loc.F("Vm.CountMissing", Count(EVmOutState.Missing)));
            if (Count(EVmOutState.Typo) > 0) sb.Append(Loc.F("Vm.OutTypoCount", Count(EVmOutState.Typo)));
            if (Count(EVmOutState.WrongType) > 0) sb.Append(Loc.F("Vm.CountWrongType", Count(EVmOutState.WrongType)));
            if (Count(EVmOutState.Unbound) > 0) sb.Append(Loc.F("Vm.OutUnboundCount", Count(EVmOutState.Unbound)));
            if (Count(EVmOutState.Extra) > 0) sb.Append(Loc.F("Vm.OutExtraCount", Count(EVmOutState.Extra)));
            sb.AppendLine();

            foreach (var e in FlowErrors) sb.Append("✗ ").AppendLine(e);

            // Gom theo Flow, giữ đúng thứ tự hợp đồng để người đọc lần theo được từng tab một.
            foreach (var flow in Items.Select(i => i.Flow).Distinct())
            {
                var bad = Items.Where(i => i.Flow == flow && i.State != EVmOutState.Ok && i.State != EVmOutState.Extra).ToList();
                if (bad.Count == 0) continue;

                sb.AppendLine();
                sb.Append("Flow ").Append(flow).AppendLine(":");
                foreach (var i in bad)
                {
                    switch (i.State)
                    {
                        case EVmOutState.Missing:
                            sb.AppendLine(Loc.F("Vm.OutLineMissing", i.Name));
                            break;
                        case EVmOutState.Typo:
                            sb.AppendLine(Loc.F("Vm.OutLineTypo", i.Name, i.Suggest));
                            break;
                        case EVmOutState.WrongType:
                            sb.AppendLine(Loc.F("Vm.OutLineWrongType", i.Name, i.TypeText));
                            break;
                        case EVmOutState.Unbound:
                            sb.AppendLine(Loc.F("Vm.OutLineUnbound", i.Name));
                            break;
                    }
                }
            }

            var extras = Items.Where(i => i.State == EVmOutState.Extra).ToList();
            if (extras.Count > 0)
            {
                sb.AppendLine();
                sb.AppendLine(Loc.T("Vm.OutExtraHeader"));
                foreach (var flow in extras.Select(i => i.Flow).Distinct())
                    sb.Append("  · ").Append(flow).Append(": ")
                      .AppendLine(string.Join(", ", extras.Where(i => i.Flow == flow).Select(i => i.Name)));
            }

            foreach (var b in ScriptBlocks)
            {
                sb.AppendLine();
                sb.Append(b.TrimEnd()).AppendLine();
            }

            if (Notes.Count > 0)
            {
                sb.AppendLine();
                foreach (var n in Notes) sb.Append("· ").AppendLine(n);
            }

            if (Clean && Items.Count > 0) sb.AppendLine().Append(Loc.T("Vm.OutAllClean"));
            return sb.ToString().TrimEnd();
        }
    }

    /// <summary>
    /// Đối chiếu tab <b>Output Params</b> của từng Flow với hợp đồng — phần tính toán thuần, không
    /// đụng SDK, nên <c>--selftest</c> chạy được hết.
    ///
    /// <para>Vì sao cần: App chỉ đọc output <b>theo tên</b>, và tên không có thì SDK trả về rỗng y hệt
    /// như output khai sai kiểu. Lúc chạy thật, lỗi này hiện ra thành một câu "thiếu output X" ở giữa
    /// một bước hiệu chuẩn, mỗi lần một cái. Tool này đọc cả ba Flow một lượt, lúc máy đang đứng yên,
    /// và nói hết những gì phải sửa.</para>
    ///
    /// <para><b>Chỉ thấy được output cấp Flow.</b> Output Params của module Script nằm bên trong Flow —
    /// SDK không đọc tới. Nhưng đó đúng là chỗ hỏng: Script tính ra số mà không kéo lên tab Output
    /// Params của Flow thì App không lấy được, và tool này báo "thiếu".</para>
    /// </summary>
    public static class VmOutCheck
    {
        /// <summary>Mã kiểu SDK App đọc được thành số (<c>IMVS_GRAP_TYPE_INT</c> / <c>_FLOAT</c>).</summary>
        public const int TypeInt = 13;
        public const int TypeFloat = 14;

        public static string TypeName(int code)
        {
            switch (code)
            {
                case TypeInt: return "int";
                case TypeFloat: return "float";
                case 1: return Loc.T("Vm.TypeImage");
                case 15: return Loc.T("Vm.TypeString");
                case 25: return "double";
                default: return Loc.F("Vm.TypeCode", code);
            }
        }

        public static bool IsNumeric(int code) { return code == TypeInt || code == TypeFloat; }

        /// <summary>
        /// Tên thật của một <c>VmIO</c> do người dùng khai (Output Params của Flow, hai tab của Script, IO của
        /// Group) bị bọc dấu phần trăm: IDE hiện <c>TopX_mm</c> nhưng SDK trả <c>%TopX_mm%</c>. Bóc ra để so
        /// với hợp đồng. Tuấn phát hiện 21/09: báo cáo trên máy liệt kê mỗi output của Flow C2 hai lần,
        /// <c>PoseX_mm</c> (từ <c>GetAllOutputNameInfo</c>) và <c>%PoseX_mm%</c> (từ <c>VmModule.Outputs</c>),
        /// và không nhận ra 9 output của Script1 dù khai đúng tên.
        /// </summary>
        public static string CleanIoName(string raw)
        {
            if (raw != null && raw.Length > 2 && raw[0] == '%' && raw[raw.Length - 1] == '%')
                return raw.Substring(1, raw.Length - 2);
            return raw;
        }

        /// <summary>Bỏ đuôi đơn vị và dấu gạch, hạ chữ thường — để <c>HandEyeDx</c> nhận ra là
        /// <c>HandEyeDx_mm</c> gõ thiếu đuôi.</summary>
        public static string Normalize(string s)
        {
            if (string.IsNullOrEmpty(s)) return "";
            string t = s.Trim().ToLowerInvariant();
            if (t.EndsWith("_mm")) t = t.Substring(0, t.Length - 3);
            else if (t.EndsWith("_deg")) t = t.Substring(0, t.Length - 4);
            return t.Replace("_", "");
        }

        /// <summary>Khoảng cách sửa chữ (Levenshtein) — đủ để bắt kiểu gõ đảo chữ "Detal" ↔ "Delta".</summary>
        public static int EditDistance(string a, string b)
        {
            if (string.IsNullOrEmpty(a)) return string.IsNullOrEmpty(b) ? 0 : b.Length;
            if (string.IsNullOrEmpty(b)) return a.Length;

            var prev = new int[b.Length + 1];
            var cur = new int[b.Length + 1];
            for (int j = 0; j <= b.Length; j++) prev[j] = j;

            for (int i = 1; i <= a.Length; i++)
            {
                cur[0] = i;
                for (int j = 1; j <= b.Length; j++)
                {
                    int cost = a[i - 1] == b[j - 1] ? 0 : 1;
                    cur[j] = Math.Min(Math.Min(cur[j - 1] + 1, prev[j] + 1), prev[j - 1] + cost);
                }
                Array.Copy(cur, prev, cur.Length);
            }
            return prev[b.Length];
        }

        /// <summary>
        /// Tên đang thiếu nào giống <paramref name="declared"/> nhất — null nếu không đủ giống.
        /// Chỉ so với những tên ĐANG THIẾU: nhờ vậy <c>TopX_mm</c> (cách <c>TopY_mm</c> đúng một chữ)
        /// không bao giờ bị đem ra gợi ý, vì bản thân nó nằm trong hợp đồng và đã khớp rồi.
        /// </summary>
        public static string SuggestFor(string declared, IEnumerable<string> missing)
        {
            if (string.IsNullOrEmpty(declared) || missing == null) return null;
            string nd = Normalize(declared);

            string best = null;
            int bestDist = int.MaxValue;
            foreach (var m in missing)
            {
                string nm = Normalize(m);
                int d = nd == nm ? 0 : EditDistance(nd, nm);
                // Tên ngắn thì một chữ khác nhau đã là tên khác hẳn; tên dài mới cho sai hai chữ.
                int limit = nm.Length >= 5 ? 2 : 1;
                if (d > limit || d >= bestDist) continue;
                best = m;
                bestDist = d;
            }
            return best;
        }

        /// <summary>Đối chiếu một Flow, ghi thẳng kết quả vào <paramref name="report"/>.</summary>
        public static void CompareFlow(VmFlowExpect expect, VmFlowOutputs actual, VmOutCheckReport report)
        {
            if (expect == null || report == null) return;
            string flow = expect.Flow;

            if (actual == null || !string.IsNullOrEmpty(actual.Error))
            {
                report.FlowErrors.Add(flow + ": " + (actual == null ? Loc.T("Vm.FlowUnreadable") : actual.Error));
                return;
            }
            if (actual.Declared.Count == 0)
            {
                report.FlowErrors.Add(Loc.F("Vm.FlowNoOutputs", flow));
                return;
            }

            var byName = new Dictionary<string, VmDeclaredOut>(StringComparer.Ordinal);
            foreach (var d in actual.Declared)
                if (d != null && !string.IsNullOrEmpty(d.Name)) byName[d.Name] = d;

            var missing = new List<string>();
            foreach (var name in expect.Names)
            {
                VmDeclaredOut d;
                if (!byName.TryGetValue(name, out d))
                {
                    missing.Add(name);
                    report.Items.Add(new VmOutItem { Flow = flow, Name = name, State = EVmOutState.Missing });
                    continue;
                }
                report.Items.Add(new VmOutItem
                {
                    Flow = flow,
                    Name = name,
                    TypeText = d.TypeText,
                    State = d.Numeric ? EVmOutState.Ok : EVmOutState.WrongType
                });
            }

            foreach (var d in actual.Declared)
            {
                if (d == null || string.IsNullOrEmpty(d.Name)) continue;
                if (expect.Names.Contains(d.Name, StringComparer.Ordinal)) continue;
                if (!string.IsNullOrEmpty(expect.ImageName) && d.Name == expect.ImageName) continue;

                string suggest = SuggestFor(d.Name, missing);
                report.Items.Add(new VmOutItem
                {
                    Flow = flow,
                    Name = d.Name,
                    TypeText = d.TypeText,
                    State = suggest != null ? EVmOutState.Typo : EVmOutState.Extra,
                    Suggest = suggest
                });
            }

            // Hợp đồng bỏ Input Params từ 2026-09-14 — mọi số App gửi sang đều đi qua biến toàn cục.
            // Còn sót thì Flow vẫn chạy bình thường, chỉ là rác của bản cũ.
            if (actual.InputParamCount > 0)
                report.Notes.Add(Loc.F("Vm.FlowInputParamsLeft", flow, actual.InputParamCount));

            if (!string.IsNullOrEmpty(expect.Note) && missing.Count > 0)
                report.Notes.Add(flow + ": " + expect.Note);
        }

        /// <summary>
        /// Tìm module Script trong Flow: module khai được NHIỀU NHẤT các tên ở tab Output Params của
        /// hợp đồng. Không dò theo tên module vì tên đó người dùng đặt tuỳ ý (và Tool 2 Import từ
        /// <c>.gro</c> thường ra một tên khác).
        /// </summary>
        public static VmModuleIo FindScript(VmFlowOutputs flow, VmScriptExpect expect)
        {
            if (flow == null || expect == null) return null;

            VmModuleIo best = null;
            int bestHits = 0;
            foreach (var m in flow.Modules)
            {
                if (m == null) continue;
                int hits = m.Outputs.Count(o => expect.Outputs.Contains(o.Name, StringComparer.Ordinal));
                if (hits > bestHits) { best = m; bestHits = hits; }
            }
            return bestHits > 0 ? best : null;
        }

        /// <summary>
        /// Đối chiếu hai tab tham số của một Script. Ghi vào <paramref name="report"/> các dòng thiếu / gõ
        /// sai tên / <b>chưa bind</b>, kèm một khối bảng liệt kê đủ Input để đọc một lượt thay vì bấm từng
        /// dòng trong VisionMaster IDE.
        /// </summary>
        public static void CompareScript(VmScriptExpect expect, VmFlowOutputs flow, VmOutCheckReport report)
        {
            if (expect == null || report == null) return;
            if (flow == null || !string.IsNullOrEmpty(flow.Error)) return;   // lỗi Flow đã báo ở CompareFlow

            if (flow.Modules.Count == 0)
            {
                report.Notes.Add(Loc.F("Vm.ScriptNoModules", expect.Flow));
                return;
            }

            var script = FindScript(flow, expect);
            if (script == null)
            {
                // Kèm luôn tên output mà các module Script ĐANG khai — lần sau lệch cách gọi tên nào
                // nữa thì đọc báo cáo là thấy ngay, không phải đoán.
                var scriptLike = flow.Modules
                    .Where(m => m.Name != null && m.Name.IndexOf("Script", StringComparison.OrdinalIgnoreCase) >= 0)
                    .Select(m => Loc.F("Vm.ScriptDeclares", m.Name,
                                 m.Outputs.Count == 0 ? Loc.T("Vm.ScriptNoOutputs") : string.Join(", ", m.Outputs.Select(o => o.Name))))
                    .ToList();
                report.FlowErrors.Add(Loc.F("Vm.ScriptNotFound", expect.Flow, expect.ScriptName,
                                            string.Join(", ", flow.Modules.Select(m => m.Name))) +
                                      (scriptLike.Count == 0 ? "" : " — " + string.Join("; ", scriptLike)));
                return;
            }

            string scope = expect.Flow + " · Script " + script.Name;
            CompareIoTab(scope + " (Output Params)", expect.Outputs, script.Outputs, false, report);
            CompareIoTab(scope + " (Input Params)", expect.Inputs, script.Inputs, true, report);

            var sb = new StringBuilder();
            sb.AppendLine(Loc.F("Vm.ScriptBlockHead", script.Name, expect.Flow, script.Inputs.Count, script.Outputs.Count));
            foreach (var io in script.Inputs)
                sb.Append("    ").Append(Pad(io.Name, 16)).Append(Pad(io.TypeText, 7))
                  .AppendLine(io.Bound ? "← " + io.Source : Loc.T("Vm.ScriptNotBound"));
            report.ScriptBlocks.Add(sb.ToString());
        }

        private static string Pad(string s, int width)
        {
            s = s ?? "";
            return s.Length >= width ? s + " " : s.PadRight(width);
        }

        /// <summary>Một tab tham số: thiếu dòng nào, dòng nào gõ sai tên, dòng nào bỏ trống Subscribe.</summary>
        private static void CompareIoTab(string scope, List<string> expected, List<VmIoInfo> declared,
                                         bool checkBind, VmOutCheckReport report)
        {
            var byName = new Dictionary<string, VmIoInfo>(StringComparer.Ordinal);
            foreach (var d in declared)
                if (d != null && !string.IsNullOrEmpty(d.Name)) byName[d.Name] = d;

            var missing = new List<string>();
            foreach (var name in expected)
            {
                VmIoInfo d;
                if (!byName.TryGetValue(name, out d))
                {
                    missing.Add(name);
                    report.Items.Add(new VmOutItem { Flow = scope, Name = name, State = EVmOutState.Missing });
                    continue;
                }
                report.Items.Add(new VmOutItem
                {
                    Flow = scope,
                    Name = name,
                    TypeText = d.TypeText,
                    State = checkBind && !d.Bound ? EVmOutState.Unbound : EVmOutState.Ok
                });
            }

            foreach (var d in declared)
            {
                if (d == null || string.IsNullOrEmpty(d.Name)) continue;
                if (expected.Contains(d.Name, StringComparer.Ordinal)) continue;

                string suggest = SuggestFor(d.Name, missing);
                report.Items.Add(new VmOutItem
                {
                    Flow = scope,
                    Name = d.Name,
                    TypeText = d.TypeText,
                    State = suggest != null ? EVmOutState.Typo : EVmOutState.Extra,
                    Suggest = suggest
                });
            }
        }
    }
}
