using System;
using System.Collections.Generic;
using System.IO;
using Newtonsoft.Json;
using Newtonsoft.Json.Converters;
using ShieldCanAssy.SysConfig;
using ShieldCanAssy.Utility.Log;

namespace ShieldCanAssy.Core.Infor
{
    /// <summary>
    /// Nội dung tĩnh của một mã alarm, nạp từ Config\alarms.json. Tên JSON viết thường tường
    /// minh để lần ghi lại (khi hoà bản mẫu) không đổi kiểu chữ của cả file.
    /// </summary>
    public class AlarmDefinition
    {
        // Ghi ra dạng chuỗi ("Error") chứ không phải số: file này tồn tại để người sửa được
        // bằng Notepad, mà "level": 2 thì không ai đoán ra là gì. Đọc vào vẫn nhận cả hai kiểu.
        [JsonProperty("level")]
        [JsonConverter(typeof(StringEnumConverter))]
        public EAlarmLevel Level { get; set; } = EAlarmLevel.Error;
        [JsonProperty("vi")] public string Vi { get; set; }
        [JsonProperty("en")] public string En { get; set; }
        [JsonProperty("cause")] public string Cause { get; set; }
        /// <summary>Nguyên nhân bằng tiếng Anh — hiện khi App chọn English (2026-10-09).</summary>
        [JsonProperty("causeEn")] public string CauseEn { get; set; }
        [JsonProperty("remedy")] public string Remedy { get; set; }
        /// <summary>Cách xử lý bằng tiếng Anh — hiện khi App chọn English (2026-10-09).</summary>
        [JsonProperty("remedyEn")] public string RemedyEn { get; set; }
    }

    /// <summary>Cấu trúc file alarms.json.</summary>
    public class AlarmCatalogFile
    {
        [JsonProperty("_comment", Order = -1)]
        public string Comment { get; set; }

        /// <summary>
        /// Mẫu dùng chung cho mọi trục, khoá là offset ("0".."8" — xem <see cref="AxisFault"/>).
        /// Chuỗi <c>{AXIS}</c> trong nội dung được thay bằng tên trục lúc phát alarm.
        /// Nhờ mẫu này bảng chỉ có 9 dòng thay vì 63 dòng lặp lại cho 7 trục.
        /// </summary>
        [JsonProperty("axisFaults", Order = 0)]
        public Dictionary<string, AlarmDefinition> AxisFaults { get; set; } = new Dictionary<string, AlarmDefinition>();

        /// <summary>Mã cụ thể → định nghĩa. Khoá là chuỗi số ("1100").</summary>
        [JsonProperty("codes", Order = 1)]
        public Dictionary<string, AlarmDefinition> Codes { get; set; } = new Dictionary<string, AlarmDefinition>();
    }

    /// <summary>
    /// Tra cứu nội dung alarm. Nạp từ <c>%Root%\Config\alarms.json</c>; nếu chưa có thì lấy bản
    /// mẫu cạnh exe và chép sang Config để người dùng sửa được lời văn mà không cần build lại.
    ///
    /// <para><b>Ai sở hữu cái gì.</b> Lời văn (<c>vi</c>, <c>en</c>, <c>cause</c>, <c>causeEn</c>, <c>remedy</c>,
    /// <c>remedyEn</c>) thuộc về người dùng — sửa thoải mái, phần mềm không bao giờ ghi đè; chỉ ô tiếng Anh còn TRỐNG
    /// mới được điền từ bản mẫu (file cũ có trước khi App có tiếng Anh). Còn <c>level</c> quyết
    /// định máy có dừng hay không, nên nó thuộc về phần mềm và luôn được lấy lại theo bản mẫu.
    /// Nếu mức nằm trong tay file cấu hình thì một lần sửa nhầm trong Notepad là đủ hạ một lỗi
    /// Fatal xuống Warning và máy chạy tiếp qua sự cố.</para>
    /// </summary>
    public static class AlarmCatalog
    {
        private static AlarmCatalogFile _file = new AlarmCatalogFile();

        public static bool Loaded { get; private set; }

        public static void Load()
        {
            string target = AppPaths.AlarmsFile;
            string template = Path.Combine(AppPaths.ExeDir ?? "", "Config", "alarms.json");

            try
            {
                if (!File.Exists(target) && File.Exists(template))
                {
                    // Chép qua đường đọc-ghi chứ không File.Copy, để bản nằm trên máy chạy LUÔN
                    // có BOM. Không có BOM thì Notepad và PowerShell trên Windows đọc file theo
                    // bảng mã ANSI và toàn bộ tiếng Việt thành ký tự rác — mà sửa lời văn bằng
                    // Notepad ngay trên máy chính là lý do file này tồn tại.
                    Directory.CreateDirectory(Path.GetDirectoryName(target));
                    File.WriteAllText(target, File.ReadAllText(template),
                        new System.Text.UTF8Encoding(true));
                }

                if (File.Exists(target))
                {
                    _file = ReadFile(target) ?? new AlarmCatalogFile();
                    Loaded = true;

                    var tpl = File.Exists(template) ? ReadFile(template) : null;
                    if (tpl != null && Merge(_file, tpl)) Save(target);

                    LogWorker.Instance.RecordLog(
                        $"Nạp bảng alarm: {_file.Codes.Count} mã + {_file.AxisFaults.Count} mẫu lỗi trục",
                        LogLevel.Info, "Alarm");
                    return;
                }

                LogWorker.Instance.RecordLog("Không tìm thấy alarms.json — alarm sẽ hiện dạng mã trần",
                    LogLevel.Warning, "Alarm");
            }
            catch (Exception ex)
            {
                LogWorker.Instance.RecordLog("Đọc alarms.json lỗi: " + ex.Message, LogLevel.Error, "Alarm");
            }

            _file = new AlarmCatalogFile();
            Loaded = false;
        }

        private static AlarmCatalogFile ReadFile(string path)
            => JsonConvert.DeserializeObject<AlarmCatalogFile>(File.ReadAllText(path));

        /// <summary>
        /// Hoà bản mẫu vào file của người dùng. Trả về true nếu có thay đổi cần ghi lại.
        ///
        /// <para>Không có bước này thì mã alarm thêm ở phiên bản mới KHÔNG BAO GIỜ tới được máy
        /// đã chạy — file Config đã tồn tại nên nhánh chép bản mẫu không chạy nữa, và alarm mới
        /// sẽ hiện ra dạng "chưa định nghĩa" đúng lúc người vận hành cần đọc cách xử lý nhất.</para>
        /// </summary>
        private static bool Merge(AlarmCatalogFile user, AlarmCatalogFile tpl)
        {
            bool changed = false;
            changed |= MergeSection(user.Codes, tpl.Codes, "mã");
            changed |= MergeSection(user.AxisFaults, tpl.AxisFaults, "mẫu lỗi trục");
            return changed;
        }

        private static bool MergeSection(Dictionary<string, AlarmDefinition> user,
                                         Dictionary<string, AlarmDefinition> tpl, string what)
        {
            if (user == null || tpl == null) return false;

            bool changed = false;
            int filledEn = 0;
            foreach (var kv in tpl)
            {
                AlarmDefinition mine;
                if (!user.TryGetValue(kv.Key, out mine) || mine == null)
                {
                    user[kv.Key] = kv.Value;
                    changed = true;
                    LogWorker.Instance.RecordLog(
                        $"alarms.json: bổ sung {what} {kv.Key} từ bản mẫu của phiên bản mới",
                        LogLevel.Info, "Alarm");
                    continue;
                }

                // Bản tiếng Anh (2026-10-09): file của máy có từ trước chưa có causeEn/remedyEn — chỉ điền ô TRỐNG, chữ
                // người dùng đã viết thì giữ nguyên.
                bool filled = false;
                if (string.IsNullOrEmpty(mine.En) && !string.IsNullOrEmpty(kv.Value.En)) { mine.En = kv.Value.En; filled = true; }
                if (string.IsNullOrEmpty(mine.CauseEn) && !string.IsNullOrEmpty(kv.Value.CauseEn)) { mine.CauseEn = kv.Value.CauseEn; filled = true; }
                if (string.IsNullOrEmpty(mine.RemedyEn) && !string.IsNullOrEmpty(kv.Value.RemedyEn)) { mine.RemedyEn = kv.Value.RemedyEn; filled = true; }
                if (filled) { filledEn++; changed = true; }

                if (mine.Level != kv.Value.Level)
                {
                    LogWorker.Instance.RecordLog(
                        $"alarms.json: {what} {kv.Key} lấy mức {kv.Value.Level} theo phần mềm " +
                        $"(file đang ghi {mine.Level}) — mức alarm là hành vi máy, không sửa qua file",
                        LogLevel.Warning, "Alarm");
                    mine.Level = kv.Value.Level;
                    changed = true;
                }
            }

            if (filledEn > 0)
                LogWorker.Instance.RecordLog(
                    $"alarms.json: điền bản tiếng Anh còn trống cho {filledEn} {what} từ bản mẫu",
                    LogLevel.Info, "Alarm");
            return changed;
        }

        private static void Save(string path)
        {
            try
            {
                // Sắp lại theo thứ tự mã để file đọc được sau khi bổ sung, thay vì dồn mã mới
                // xuống cuối theo thứ tự nạp.
                var ordered = new AlarmCatalogFile
                {
                    Comment = _file.Comment,
                    Codes = SortByCode(_file.Codes),
                    AxisFaults = SortByCode(_file.AxisFaults)
                };
                File.WriteAllText(path,
                    JsonConvert.SerializeObject(ordered, Formatting.Indented),
                    new System.Text.UTF8Encoding(true));
                _file = ordered;
            }
            catch (Exception ex)
            {
                // Ghi không được thì vẫn chạy tiếp bằng bản đã hoà trong bộ nhớ.
                LogWorker.Instance.RecordLog("Không ghi lại được alarms.json: " + ex.Message,
                    LogLevel.Warning, "Alarm");
            }
        }

        private static Dictionary<string, AlarmDefinition> SortByCode(Dictionary<string, AlarmDefinition> src)
        {
            var result = new Dictionary<string, AlarmDefinition>();
            if (src == null) return result;

            var keys = new List<string>(src.Keys);
            keys.Sort((a, b) =>
            {
                int x, y;
                bool ax = int.TryParse(a, out x), by = int.TryParse(b, out y);
                if (ax && by) return x.CompareTo(y);
                return string.CompareOrdinal(a, b);
            });

            foreach (string k in keys) result[k] = src[k];
            return result;
        }

        /// <summary>
        /// Tra định nghĩa của một mã. Mã trục (2000–2699) rơi về mẫu <c>AxisFaults</c> nếu không
        /// có bản ghi riêng — cho phép khai riêng vài mã đặc biệt mà vẫn dùng mẫu cho phần còn lại.
        /// Không tra được thì trả về định nghĩa "chưa định nghĩa" chứ không trả null: một mã
        /// thiếu trong bảng vẫn phải hiện lên màn hình, không được nuốt mất.
        /// </summary>
        public static AlarmDefinition Get(int code)
        {
            AlarmDefinition def;
            if (_file.Codes != null && _file.Codes.TryGetValue(code.ToString(), out def) && def != null)
                return def;

            string axis;
            AxisFault fault;
            if (AlarmCode.TryParseAxis(code, out axis, out fault))
            {
                if (_file.AxisFaults != null &&
                    _file.AxisFaults.TryGetValue(((int)fault).ToString(), out def) && def != null)
                {
                    return new AlarmDefinition
                    {
                        Level = def.Level,
                        Vi = Fill(def.Vi, axis),
                        En = Fill(def.En, axis),
                        Cause = Fill(def.Cause, axis),
                        CauseEn = Fill(def.CauseEn, axis),
                        Remedy = Fill(def.Remedy, axis),
                        RemedyEn = Fill(def.RemedyEn, axis)
                    };
                }
            }

            return new AlarmDefinition
            {
                Level = EAlarmLevel.Error,
                Vi = $"Alarm chưa định nghĩa: {code}",
                En = $"Undefined alarm: {code}",
                Cause = "Mã alarm này chưa có trong Config\\alarms.json.",
                CauseEn = "This alarm code is not in Config\\alarms.json.",
                Remedy = "Bổ sung mã vào alarms.json rồi khởi động lại app.",
                RemedyEn = "Add the code to alarms.json, then restart the app."
            };
        }

        private static string Fill(string s, string axis)
            => string.IsNullOrEmpty(s) ? s : s.Replace("{AXIS}", axis);
    }
}
