using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text;
using ShieldCanAssy.Core.Data;
using ShieldCanAssy.Core.Data.Model;
using ShieldCanAssy.Core.Lang;

namespace ShieldCanAssy.Core.Communication
{
    /// <summary>Nhóm biến toàn cục theo ai ghi. Từ 2026-09-13 chỉ còn một nhóm: App ghi tất cả.</summary>
    public enum EGlobalVarGroup
    {
        /// <summary>App ghi ngay trước mỗi lần gọi Flow.</summary>
        AppWrites
    }

    /// <summary>Một biến toàn cục cần có trong solution.</summary>
    public class GlobalVarSpec
    {
        public string Name { get; set; }
        /// <summary><c>float</c>, <c>int</c> hoặc <c>string</c> — ba kiểu module Global Variable tạo được qua SDK.</summary>
        public string Type { get; set; }
        /// <summary>Giá trị đầu, viết theo InvariantCulture.</summary>
        public string InitialValue { get; set; }
        public string Remark { get; set; }
        public EGlobalVarGroup Group { get; set; }
    }

    public enum EGlobalVarState { Existing, WrongType, Missing, Created, Failed, InvalidName }

    public class GlobalVarSyncItem
    {
        public GlobalVarSpec Spec { get; set; }
        public EGlobalVarState State { get; set; }
        public string Detail { get; set; }
    }

    public class GlobalVarSyncReport
    {
        /// <summary>true khi đã thực sự thử tạo biến (không phải chỉ kiểm tra).</summary>
        public bool Applied { get; set; }
        public bool Saved { get; set; }
        /// <summary>Lỗi chặn cả lượt (chưa nạp solution, không đọc được danh sách biến, lưu hỏng...).</summary>
        public string Error { get; set; }
        public List<GlobalVarSyncItem> Items { get; } = new List<GlobalVarSyncItem>();

        public int Count(EGlobalVarState state) { return Items.Count(i => i.State == state); }

        public static GlobalVarSyncReport Fail(string error) { return new GlobalVarSyncReport { Error = error }; }

        /// <summary>Bản tóm tắt để hiện trên trang Setting và ghi log.</summary>
        public string ToText()
        {
            var sb = new StringBuilder();
            if (!string.IsNullOrEmpty(Error)) sb.AppendLine(Loc.F("Vm.ReportError", Error));
            if (Items.Count == 0) return sb.ToString().TrimEnd();

            sb.Append(Loc.F("Vm.GvExisting", Count(EGlobalVarState.Existing)));
            if (Applied) sb.Append(Loc.F("Vm.GvCreated", Count(EGlobalVarState.Created)));
            sb.Append(Loc.F("Vm.CountMissing", Count(EGlobalVarState.Missing)));
            sb.Append(Loc.F("Vm.CountWrongType", Count(EGlobalVarState.WrongType)));
            if (Count(EGlobalVarState.InvalidName) > 0) sb.Append(Loc.F("Vm.GvInvalidNameCount", Count(EGlobalVarState.InvalidName)));
            if (Count(EGlobalVarState.Failed) > 0) sb.Append(Loc.F("Vm.GvFailedCount", Count(EGlobalVarState.Failed)));
            sb.AppendLine();
            if (Applied) sb.AppendLine(Saved ? Loc.T("Vm.GvSaved") : Loc.T("Vm.GvNotSaved"));

            AppendList(sb, Loc.T("Vm.GvListWrongType"), EGlobalVarState.WrongType, true);
            AppendList(sb, Loc.T("Vm.GvListInvalidName"), EGlobalVarState.InvalidName, true);
            AppendList(sb, Loc.T("Vm.GvListFailed"), EGlobalVarState.Failed, true);
            AppendList(sb, Loc.T("Vm.GvListMissing"), EGlobalVarState.Missing, false);
            return sb.ToString().TrimEnd();
        }

        private void AppendList(StringBuilder sb, string title, EGlobalVarState state, bool withDetail)
        {
            var list = Items.Where(i => i.State == state).ToList();
            if (list.Count == 0) return;
            const int max = 15;
            var shown = list.Take(max).Select(i => withDetail && !string.IsNullOrEmpty(i.Detail)
                ? i.Spec.Name + " (" + i.Detail + ")" : i.Spec.Name);
            sb.Append(title).Append(": ").Append(string.Join(", ", shown));
            if (list.Count > max) sb.Append(Loc.F("Vm.GvMoreVars", list.Count - max));
            sb.AppendLine();
        }
    }

    /// <summary>Kho biến toàn cục của solution — VisionMaster thật hoặc bản giả lập.</summary>
    public interface IGlobalVarStore
    {
        /// <summary>Tên → kiểu của mọi biến đang khai. Không đọc được thì ném lỗi, KHÔNG trả rỗng.</summary>
        IDictionary<string, string> ReadAll();

        /// <summary>Tạo biến với giá trị trung tính (0 hoặc chuỗi rỗng).</summary>
        void Add(string name, string type, string remark);

        /// <summary>Ghi giá trị đầu (chuỗi InvariantCulture) bằng hàm ghi theo kiểu.</summary>
        void SetValue(string name, string type, string invariantValue);

        /// <summary>Lưu solution, trả thông báo của SDK.</summary>
        string Save();
    }

    /// <summary>
    /// Đối chiếu danh mục biến với solution và tạo biến còn thiếu (Tuấn yêu cầu 2026-09-13).
    ///
    /// <para><b>Chỉ thêm, không sửa.</b> Biến đã có giữ nguyên giá trị. Biến sai kiểu chỉ báo: đổi
    /// kiểu trong VisionMaster gỡ luôn các subscribe đang trỏ vào biến đó.</para>
    ///
    /// <para><b>Giá trị đầu ghi riêng.</b> Hàm tạo biến của VisionMaster đọc số theo culture Windows,
    /// máy dùng dấu phẩy thập phân sẽ từ chối <c>0.05</c>. Vì vậy biến được tạo với 0 rồi mới ghi
    /// giá trị thật bằng hàm ghi float/int — hai hàm này nhận số nhị phân, không qua chuỗi.</para>
    /// </summary>
    public static class GlobalVarSync
    {
        private static readonly char[] ForbiddenChars = "?;./\\`{}[]<>()".ToCharArray();

        /// <summary>Luật đặt tên của module Global Variable (VariableManager.CheckVarName, VM 4.4).</summary>
        public static bool IsValidName(string name, out string why)
        {
            why = null;
            if (string.IsNullOrWhiteSpace(name)) { why = Loc.T("Vm.NameEmpty"); return false; }
            if (name.Trim() != name) { why = Loc.T("Vm.NameSpaces"); return false; }
            if (Encoding.UTF8.GetByteCount(name) + 1 > 126) { why = Loc.T("Vm.NameTooLong"); return false; }
            if (name.IndexOfAny(ForbiddenChars) >= 0) { why = Loc.T("Vm.NameForbiddenChars"); return false; }
            return true;
        }

        public static GlobalVarSyncReport Run(IGlobalVarStore store, IList<GlobalVarSpec> specs, bool apply)
        {
            var report = new GlobalVarSyncReport();
            if (store == null || specs == null) return GlobalVarSyncReport.Fail(Loc.T("Vm.GvNoStore"));

            IDictionary<string, string> existing;
            try { existing = Normalize(store.ReadAll()); }
            catch (Exception ex) { return GlobalVarSyncReport.Fail(Loc.F("Vm.GvReadFail", ex.Message)); }

            var seen = new HashSet<string>(StringComparer.Ordinal);
            foreach (var spec in specs)
            {
                if (spec == null || !seen.Add(spec.Name ?? "")) continue;
                var item = new GlobalVarSyncItem { Spec = spec };
                string why, type;
                if (!IsValidName(spec.Name, out why)) { item.State = EGlobalVarState.InvalidName; item.Detail = why; }
                else if (existing.TryGetValue(spec.Name, out type))
                {
                    if (string.Equals(type, spec.Type, StringComparison.OrdinalIgnoreCase)) item.State = EGlobalVarState.Existing;
                    else { item.State = EGlobalVarState.WrongType; item.Detail = Loc.F("Vm.GvTypeIs", type, spec.Type); }
                }
                else item.State = EGlobalVarState.Missing;
                report.Items.Add(item);
            }

            if (!apply) return report;
            report.Applied = true;

            var attempted = report.Items.Where(i => i.State == EGlobalVarState.Missing).ToList();
            foreach (var item in attempted)
            {
                try
                {
                    store.Add(item.Spec.Name, item.Spec.Type, item.Spec.Remark ?? "");
                    if (!string.IsNullOrEmpty(item.Spec.InitialValue))
                        store.SetValue(item.Spec.Name, item.Spec.Type, item.Spec.InitialValue);
                }
                catch (Exception ex)
                {
                    item.State = EGlobalVarState.Failed;
                    item.Detail = ex.Message;
                }
            }

            IDictionary<string, string> after;
            try { after = Normalize(store.ReadAll()); }
            catch (Exception ex)
            {
                foreach (var item in attempted.Where(i => i.State == EGlobalVarState.Missing))
                {
                    item.State = EGlobalVarState.Failed;
                    item.Detail = Loc.T("Vm.GvRereadFail");
                }
                report.Error = Loc.F("Vm.GvRereadFailAfterCreate", ex.Message);
                return report;
            }

            foreach (var item in attempted.Where(i => i.State == EGlobalVarState.Missing))
            {
                string type;
                if (after.TryGetValue(item.Spec.Name, out type) && string.Equals(type, item.Spec.Type, StringComparison.OrdinalIgnoreCase))
                    item.State = EGlobalVarState.Created;
                else
                {
                    item.State = EGlobalVarState.Failed;
                    item.Detail = type == null ? Loc.T("Vm.GvNotFoundAfterCreate") : Loc.F("Vm.GvCreatedAsType", type);
                }
            }

            if (report.Count(EGlobalVarState.Created) > 0)
            {
                try { store.Save(); report.Saved = true; }
                catch (Exception ex) { report.Error = Loc.F("Vm.GvSaveFail", ex.Message); }
            }
            return report;
        }

        /// <summary>VisionMaster có thể trả tên bọc <c>%tên%</c> (dấu "không dịch" của giao diện).</summary>
        private static IDictionary<string, string> Normalize(IDictionary<string, string> raw)
        {
            var d = new Dictionary<string, string>(StringComparer.Ordinal);
            if (raw == null) return d;
            foreach (var kv in raw)
            {
                string name = kv.Key ?? "";
                if (name.Length > 1 && name[0] == '%' && name[name.Length - 1] == '%') name = name.Substring(1, name.Length - 2);
                if (name.Length > 0) d[name] = (kv.Value ?? "").Trim();
            }
            return d;
        }
    }

    /// <summary>
    /// Danh mục biến toàn cục — 13 biến, <b>App ghi hết</b> (Tuấn chốt 2026-09-13); tất cả kiểu float trừ cờ
    /// <see cref="VmVars.IsTriggerLocate"/> kiểu int (2026-10-08).
    /// Bốn biến trục (tên cấu hình ở trang Setting) và <c>RunId</c> ghi trước mọi Flow; tám biến còn lại
    /// ghi trước <c>AlignBottom_ToolN</c>. Hằng số hiệu chuẩn nằm ở model của App, solution không cất
    /// bản sao — nên giá trị đầu của mọi biến đều là 0.
    /// </summary>
    public static class VisionGlobalVarCatalog
    {
        public static List<GlobalVarSpec> Build(SystemData sys)
        {
            var list = new List<GlobalVarSpec>();
            var names = sys?.VisionOutputs ?? new VisionOutputNames();

            void Add(string name, string remark, string type = "float")
            {
                if (string.IsNullOrEmpty(name) || list.Any(s => s.Name == name)) return;
                list.Add(new GlobalVarSpec
                {
                    Group = EGlobalVarGroup.AppWrites, Name = name, Type = type, InitialValue = "0", Remark = remark
                });
            }

            const string every = "App ghi trước mọi Flow";
            Add(names.GvAxisX, every + " — trục X");
            Add(names.GvAxisY2, every + " — trục Y2");
            Add(names.GvAxisA1, every + " — trục A1");
            Add(names.GvAxisA2, every + " — trục A2");
            Add(VmVars.RunId, every + " — số của lần gọi, Flow trả lại");

            const string place = "App ghi trước AlignBottom_ToolN";
            Add(VmVars.HasTarget, place + " — 1 có ô đích, 0 chỉ soi");
            Add(VmVars.CellX, place + " — pose ô đích X (LocateTop đo lúc quét)");
            Add(VmVars.CellY, place + " — pose ô đích Y");
            Add(VmVars.CellA, place + " — pose ô đích, góc");
            Add(VmVars.HandEyeDx, place + " — hand-eye X của tay đang gọi (bước xác định hand-eye)");
            Add(VmVars.HandEyeDy, place + " — hand-eye Y của tay đang gọi");
            Add(VmVars.AlignSign, place + " — dấu góc của tay đang gọi (phép thử vòng xoay), +1 hoặc −1");

            // int vì Module rẽ nhánh của VisionMaster nhận int (Tuấn 2026-10-08).
            Add(VmVars.IsTriggerLocate, "App ghi trước mọi Flow — 1 soi, 0 nạp điểm hiệu chuẩn (Module rẽ nhánh)", "int");

            return list;
        }
    }
}
