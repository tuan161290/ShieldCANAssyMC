using System;
using System.IO;
using System.Linq;
using System.Text;
using Newtonsoft.Json;
using ShieldCanAssy.Core.Data.Model;
using ShieldCanAssy.Core.Infor;
using ShieldCanAssy.Core.Lang;
using ShieldCanAssy.SysConfig;
using ShieldCanAssy.Utility.Log;

namespace ShieldCanAssy.Core.Data
{
    /// <summary>Nạp và lưu toàn bộ dữ liệu bền vững: system.json, model, running.json.</summary>
    public static class McDataManager
    {
        private static readonly JsonSerializerSettings JsonOpt = new JsonSerializerSettings
        {
            Formatting = Formatting.Indented,
            NullValueHandling = NullValueHandling.Ignore,
            DateFormatHandling = DateFormatHandling.IsoDateFormat
        };

        private static readonly object _saveLock = new object();

        public static SystemData SysData { get; private set; } = SystemData.CreateDefault();
        public static RunningData RunData { get; private set; } = new RunningData();
        public static ModelData Model { get; private set; } = new ModelData();

        public static bool ModelLoaded { get; private set; }

        // ───────────────────────── system.json ─────────────────────────

        public static void LoadSystemData()
        {
            try
            {
                if (File.Exists(AppPaths.SystemFile))
                {
                    var loaded = JsonConvert.DeserializeObject<SystemData>(File.ReadAllText(AppPaths.SystemFile));
                    if (loaded != null)
                    {
                        // Sắp lại theo đúng AxisId.All — vừa bổ sung trục còn thiếu (file cũ hơn),
                        // vừa tự sửa thứ tự nếu file đã lưu từ lúc AxisId.All còn thứ tự khác
                        // (Tuấn đổi thứ tự trục 2026-09-04), không cần xoá system.json thủ công.
                        loaded.Axes = AxisId.All.Select(loaded.GetAxis).ToList();

                        // Cấu hình trục quay đổi mốc 2026-09-12 (quấn [0,360), 36000 xung/vòng
                        // đầu hút sau khi đổi động cơ sang 18000 xung/vòng). File cũ ghi 10000
                        // hoặc 20000 xung/vòng — chạy tiếp với con số đó thì trục xoay sai tỉ lệ
                        // góc mà không báo lỗi gì, nên phải ghi đè ngay lúc nạp.
                        var migrated = loaded.MigrateRotaryAxes();
                        SysData = loaded;

                        if (migrated.Count > 0)
                        {
                            SaveSystemData();
                            LogWorker.Instance.RecordLog(
                                "Đã cập nhật cấu hình trục quay trong system.json: " +
                                string.Join(", ", migrated) + " (quấn 0–360°, " +
                                SystemData.RotaryPulsePerDeg.ToString("0.####") + " xung/độ, iBdID theo bảng phân bổ)",
                                Utility.Log.LogLevel.Warning, "Data");
                        }

                        // Tên biến trục mang đơn vị từ 2026-09-15 — file cũ còn tên mặc định X / Y2 / A1 / A2.
                        if (loaded.VisionOutputs == null) loaded.VisionOutputs = new VisionOutputNames();
                        var renamed = loaded.VisionOutputs.MigrateAxisVarUnits();
                        if (renamed.Count > 0)
                        {
                            SaveSystemData();
                            LogWorker.Instance.RecordLog(
                                "Đã đổi tên biến trục trong system.json sang tên có đơn vị: " + string.Join(", ", renamed) +
                                " — solution VisionMaster phải có biến cùng tên (trang Setting → Tạo biến còn thiếu)",
                                Utility.Log.LogLevel.Warning, "Data");
                        }

                        LogWorker.Instance.RecordLog("Đã nạp system.json", Utility.Log.LogLevel.Info, "Data");
                        return;
                    }
                }

                SysData = SystemData.CreateDefault();
                SaveSystemData();
                LogWorker.Instance.RecordLog("Chưa có system.json — tạo bản mặc định", Utility.Log.LogLevel.Warning, "Data");
            }
            catch (Exception ex)
            {
                SysData = SystemData.CreateDefault();
                AlarmManager.Raise(AlarmCode.ConfigInvalid, "Data", ex.Message);
            }
        }

        public static bool SaveSystemData()
        {
            try
            {
                lock (_saveLock)
                    WriteAtomic(AppPaths.SystemFile, JsonConvert.SerializeObject(SysData, JsonOpt));
                return true;
            }
            catch (Exception ex)
            {
                AlarmManager.RaiseEvent(AlarmCode.ConfigInvalid, "Data", Loc.F("Data.SaveSystemFailed", ex.Message));
                return false;
            }
        }

        // ───────────────────────── running.json ─────────────────────────

        public static void LoadRunningData()
        {
            try
            {
                if (File.Exists(AppPaths.RunningFile))
                {
                    var loaded = JsonConvert.DeserializeObject<RunningData>(File.ReadAllText(AppPaths.RunningFile));
                    if (loaded != null)
                    {
                        RunData = loaded;

                        // Không resume chu trình qua một lần mở lại app — bắt buộc Init lại
                        // toàn bộ máy (về gốc, tray trống) mỗi lần khởi động, kể cả khi phiên
                        // trước dừng dở dang. Bộ đếm OK/NG và model dùng gần nhất vẫn giữ
                        // nguyên vì đó là số liệu báo cáo/tiện ích, không phải trạng thái máy.
                        RunData.ResetCycleProgress();

                        LogWorker.Instance.RecordLog(
                            $"Đã nạp running.json (bộ đếm OK {loaded.CountOk}/NG {loaded.CountNg}) — " +
                            "chu trình luôn bắt đầu lại từ S00, bỏ tiến độ dở dang của phiên trước",
                            Utility.Log.LogLevel.Info, "Data");
                        return;
                    }
                }
                RunData = new RunningData();
            }
            catch (Exception ex)
            {
                RunData = new RunningData();
                LogWorker.Instance.RecordLog("Đọc running.json lỗi: " + ex.Message, Utility.Log.LogLevel.Error, "Data");
            }
        }

        /// <summary>
        /// Ghi tiến độ. Gọi sau mỗi ô hoàn tất và ngay khi vào EMG/ALARM — phục vụ log/điều tra
        /// sự cố và giữ bộ đếm OK/NG, KHÔNG dùng để resume chu trình: <see cref="LoadRunningData"/>
        /// luôn xoá tiến độ (AutoState/tray/tay hút) đọc được từ file này ngay sau khi nạp.
        /// </summary>
        public static bool SaveRunningData()
        {
            try
            {
                RunData.SavedAt = DateTime.Now;
                lock (_saveLock)
                    WriteAtomic(AppPaths.RunningFile, JsonConvert.SerializeObject(RunData, JsonOpt));
                AlarmManager.ClearCondition(AlarmCode.RunningDataFailed);
                return true;
            }
            catch (Exception ex)
            {
                AlarmManager.Raise(AlarmCode.RunningDataFailed, "Data", ex.Message);
                return false;
            }
        }

        // ───────────────────────── model ─────────────────────────

        public static string[] ListModels()
        {
            try
            {
                return Directory.GetFiles(AppPaths.ModelDir, "*.json")
                                .Select(Path.GetFileNameWithoutExtension)
                                .OrderBy(n => n)
                                .ToArray();
            }
            catch { return new string[0]; }
        }

        public static bool LoadModel(string name)
        {
            string path = Path.Combine(AppPaths.ModelDir, name + ".json");
            try
            {
                if (!File.Exists(path))
                {
                    AlarmManager.RaiseEvent(AlarmCode.ModelFileCorrupt, "Data", Loc.F("Data.ModelNotFound", path));
                    return false;
                }

                var loaded = JsonConvert.DeserializeObject<ModelData>(File.ReadAllText(path));
                if (loaded == null)
                {
                    AlarmManager.RaiseEvent(AlarmCode.ModelFileCorrupt, "Data", path);
                    return false;
                }

                Model = loaded;
                ModelLoaded = true;
                RunData.RecentModelName = name;

                // Lưới ô của tray là thuộc tính của MODEL; tiến độ phải khớp kích thước đó.
                RunData.InputTray.Resize(Model.InputTray.Rows, Model.InputTray.Cols);
                RunData.AssyTray.Resize(Model.AssyTray.Rows, Model.AssyTray.Cols);
                // ScanOrder tương tự — TrayState (RunData) là bản sao riêng của TrayLayout
                // (Model), không tự đồng bộ, phải copy tay mỗi lần nạp model.
                RunData.InputTray.ScanOrder = Model.InputTray.ScanOrder;
                RunData.AssyTray.ScanOrder = Model.AssyTray.ScanOrder;

                LogWorker.Instance.RecordLog(
                    $"Nạp model '{name}': tray in {Model.InputTray.Rows}×{Model.InputTray.Cols}, " +
                    $"tray assy {Model.AssyTray.Rows}×{Model.AssyTray.Cols}",
                    Utility.Log.LogLevel.Info, "Data");
                return true;
            }
            catch (Exception ex)
            {
                AlarmManager.RaiseEvent(AlarmCode.ModelFileCorrupt, "Data", ex.Message);
                return false;
            }
        }

        /// <summary>
        /// Dùng một model dựng sẵn trong bộ nhớ, không đọc từ file — hiện chỉ dùng cho model mô
        /// phỏng. Đi qua đây thay vì gán thẳng <see cref="Model"/> để bước đồng bộ kích thước
        /// lưới tiến độ không bị bỏ sót: quên nó thì lưới trên trang Main và con trỏ ô sẽ lệch
        /// kích thước với model, và triệu chứng là máy chạy sang ô không tồn tại.
        /// </summary>
        public static void UseModel(ModelData model, bool countsAsLoaded = false)
        {
            if (model == null) return;

            Model = model;
            ModelLoaded = countsAsLoaded;
            RunData.InputTray.Resize(model.InputTray.Rows, model.InputTray.Cols);
            RunData.AssyTray.Resize(model.AssyTray.Rows, model.AssyTray.Cols);
            RunData.InputTray.ScanOrder = model.InputTray.ScanOrder;
            RunData.AssyTray.ScanOrder = model.AssyTray.ScanOrder;
        }

        public static bool SaveModel(string name = null)
        {
            name = name ?? Model.Name;
            string path = Path.Combine(AppPaths.ModelDir, name + ".json");
            try
            {
                Model.Name = name;
                Model.ModifiedAt = DateTime.Now;
                lock (_saveLock)
                    WriteAtomic(path, JsonConvert.SerializeObject(Model, JsonOpt));
                LogWorker.Instance.RecordLog("Đã lưu model " + name, Utility.Log.LogLevel.Info, "Data");
                return true;
            }
            catch (Exception ex)
            {
                AlarmManager.RaiseEvent(AlarmCode.ModelSaveFailed, "Data", ex.Message);
                return false;
            }
        }

        /// <summary>
        /// Xoá một model khỏi đĩa. Không đụng tới <see cref="Model"/> đang nạp trong bộ nhớ dù
        /// tên trùng — gọi hàm này xong màn hình vẫn hiển thị dữ liệu cũ cho tới khi nạp model
        /// khác, tránh cảnh đang teach dở mà dữ liệu biến mất ngay dưới tay.
        /// </summary>
        public static bool DeleteModel(string name)
        {
            string path = Path.Combine(AppPaths.ModelDir, name + ".json");
            try
            {
                if (File.Exists(path)) File.Delete(path);
                LogWorker.Instance.RecordLog("Đã xoá model " + name, Utility.Log.LogLevel.Info, "Data");
                return true;
            }
            catch (Exception ex)
            {
                AlarmManager.RaiseEvent(AlarmCode.ModelSaveFailed, "Data", Loc.F("Data.DeleteModelFailed", ex.Message));
                return false;
            }
        }

        /// <summary>
        /// Sao chép model trên đĩa sang tên mới — đọc thẳng từ file nguồn, không qua
        /// <see cref="Model"/> đang nạp, để sao chép một model KHÔNG PHẢI model đang mở cũng
        /// đúng dữ liệu.
        /// </summary>
        public static bool CopyModel(string sourceName, string newName)
        {
            string srcPath = Path.Combine(AppPaths.ModelDir, sourceName + ".json");
            try
            {
                var data = JsonConvert.DeserializeObject<ModelData>(File.ReadAllText(srcPath));
                if (data == null) return false;

                data.Name = newName;
                data.ModifiedAt = DateTime.Now;
                string dstPath = Path.Combine(AppPaths.ModelDir, newName + ".json");
                lock (_saveLock)
                    WriteAtomic(dstPath, JsonConvert.SerializeObject(data, JsonOpt));

                LogWorker.Instance.RecordLog("Đã sao chép model " + sourceName + " → " + newName,
                    Utility.Log.LogLevel.Info, "Data");
                return true;
            }
            catch (Exception ex)
            {
                AlarmManager.RaiseEvent(AlarmCode.ModelSaveFailed, "Data", Loc.F("Data.CopyModelFailed", ex.Message));
                return false;
            }
        }

        /// <summary>Nạp lại model dùng gần nhất; nếu chưa có model nào thì tạo bản mặc định.</summary>
        public static void LoadRecentModel()
        {
            string recent = RunData.RecentModelName;
            if (!string.IsNullOrWhiteSpace(recent) && LoadModel(recent)) return;

            var all = ListModels();
            if (all.Length > 0 && LoadModel(all[0])) return;

            Model = new ModelData();
            ModelLoaded = false;
            SaveModel("DEFAULT");
            LoadModel("DEFAULT");
        }

        // ───────────────────────── tiện ích ─────────────────────────

        /// <summary>
        /// Ghi qua file tạm rồi thay thế. Mất điện giữa lúc ghi sẽ để lại file cũ nguyên vẹn
        /// thay vì một file JSON cụt — với running.json thì đó là khác biệt giữa "mất tiến độ
        /// một ô" và "mất cả tray".
        /// </summary>
        private static void WriteAtomic(string path, string content)
        {
            string dir = Path.GetDirectoryName(path);
            if (!string.IsNullOrEmpty(dir)) Directory.CreateDirectory(dir);

            string tmp = path + ".tmp";
            File.WriteAllText(tmp, content, new UTF8Encoding(true));

            if (File.Exists(path)) File.Delete(path);
            File.Move(tmp, path);
        }
    }
}
