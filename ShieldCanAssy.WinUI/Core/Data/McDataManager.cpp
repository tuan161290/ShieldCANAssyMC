#include "Data/McDataManager.h"

#include "Base/Text.h"
#include "Infor/AlarmManager.h"
#include "Infor/AxisId.h"
#include "Lang/Loc.h"
#include "SysConfig/AppPaths.h"
#include "Utility/Log/LogWorker.h"

#include <algorithm>
#include <format>
#include <mutex>
#include <stdexcept>

namespace sca
{
    namespace fs = std::filesystem;

    namespace
    {
        struct State
        {
            std::mutex mutex;                 // ba con trỏ + modelLoaded
            std::shared_ptr<SystemData> sys = SystemData::CreateDefault();
            std::shared_ptr<RunningData> run = std::make_shared<RunningData>();
            std::shared_ptr<ModelData> model = std::make_shared<ModelData>();
            bool modelLoaded{ false };
            std::recursive_mutex saveLock;    // _saveLock của bản C#
        };

        State& S()
        {
            static State s;
            return s;
        }

        void SetSys(std::shared_ptr<SystemData> v)
        {
            auto& s = S();
            std::lock_guard<std::mutex> lock(s.mutex);
            s.sys = std::move(v);
        }

        void SetRun(std::shared_ptr<RunningData> v)
        {
            auto& s = S();
            std::lock_guard<std::mutex> lock(s.mutex);
            s.run = std::move(v);
        }

        void SetModel(std::shared_ptr<ModelData> model, bool loaded)
        {
            auto& s = S();
            std::lock_guard<std::mutex> lock(s.mutex);
            s.model = std::move(model);
            s.modelLoaded = loaded;
        }

        std::string ReadFile(const fs::path& path)
        {
            std::string text;
            if (!base::ReadAllText(path, text)) throw std::runtime_error("cannot read " + base::PathToU8(path));
            return text;
        }

        /// <summary>
        /// Ghi qua file tạm rồi thay thế. Mất điện giữa lúc ghi sẽ để lại file cũ nguyên vẹn
        /// thay vì một file JSON cụt — với running.json thì đó là khác biệt giữa "mất tiến độ
        /// một ô" và "mất cả tray". UTF-8 có BOM như <c>new UTF8Encoding(true)</c> của bản C#.
        /// </summary>
        void WriteAtomic(const fs::path& path, const std::string& content)
        {
            std::lock_guard<std::recursive_mutex> lock(S().saveLock);
            if (path.has_parent_path()) fs::create_directories(path.parent_path());

            fs::path tmp = path;
            tmp += ".tmp";
            if (!base::WriteAllText(tmp, "\xEF\xBB\xBF" + content)) throw std::runtime_error("cannot write " + base::PathToU8(tmp));

            if (fs::exists(path)) fs::remove(path);
            fs::rename(tmp, path);
        }

        fs::path ModelPath(const std::string& name)
        {
            return AppPaths::ModelDir() / base::U8Path(name + ".json");
        }

        void Log(const std::string& msg, LogLevel level = LogLevel::Info)
        {
            LogWorker::Instance().RecordLog(msg, level, "Data");
        }

        /// <summary>Chỉ dựng lại danh sách ô khi kích thước thật sự đổi — trang Teaching đổi thứ tự quét lúc máy đang chạy
        /// thì luồng chu trình vẫn ghi vào đúng danh sách đang dùng.</summary>
        void Fit(TrayState& tray, int rows, int cols)
        {
            if (tray.Rows != rows || tray.Cols != cols || (int)tray.Cells.size() != rows * cols) tray.Resize(rows, cols);
        }
    }

    std::shared_ptr<SystemData> McDataManager::SysData()  { auto& s = S(); std::lock_guard<std::mutex> l(s.mutex); return s.sys; }
    std::shared_ptr<RunningData> McDataManager::RunData() { auto& s = S(); std::lock_guard<std::mutex> l(s.mutex); return s.run; }
    std::shared_ptr<ModelData> McDataManager::Model()     { auto& s = S(); std::lock_guard<std::mutex> l(s.mutex); return s.model; }
    bool McDataManager::ModelLoaded()                     { auto& s = S(); std::lock_guard<std::mutex> l(s.mutex); return s.modelLoaded; }

    // ───────────────────────── system.json ─────────────────────────

    void McDataManager::LoadSystemData()
    {
        try
        {
            if (fs::exists(AppPaths::SystemFile()))
            {
                auto loaded = SystemData::FromJsonText(ReadFile(AppPaths::SystemFile()));
                if (loaded)
                {
                    // Sắp lại theo đúng AxisId::All — vừa bổ sung trục còn thiếu (file cũ hơn),
                    // vừa tự sửa thứ tự nếu file đã lưu từ lúc AxisId::All còn thứ tự khác
                    // (Tuấn đổi thứ tự trục 2026-09-04), không cần xoá system.json thủ công.
                    std::vector<std::shared_ptr<AxisConfig>> axes;
                    for (const char* name : AxisId::All) axes.push_back(loaded->GetAxis(name));
                    loaded->Axes = std::move(axes);

                    // Cấu hình trục quay đổi mốc 2026-09-12 (quấn [0,360), 36000 xung/vòng
                    // đầu hút sau khi đổi động cơ sang 18000 xung/vòng). File cũ ghi 10000
                    // hoặc 20000 xung/vòng — chạy tiếp với con số đó thì trục xoay sai tỉ lệ
                    // góc mà không báo lỗi gì, nên phải ghi đè ngay lúc nạp.
                    auto migrated = loaded->MigrateRotaryAxes();
                    SetSys(loaded);

                    if (!migrated.empty())
                    {
                        SaveSystemData();
                        Log("Rotary axis config updated in system.json: " + base::Join(migrated, ", ") + " (wrap 0–360°, " +
                                Loc::FormatValue(SystemData::RotaryPulsePerDeg, "0.####") + " pulse/deg, iBdID per allocation table)",
                            LogLevel::Warning);
                    }

                    // Tên biến trục mang đơn vị từ 2026-09-15 — file cũ còn tên mặc định X / Y2 / A1 / A2.
                    auto renamed = loaded->VisionOutputs.MigrateAxisVarUnits();
                    if (!renamed.empty())
                    {
                        SaveSystemData();
                        Log("Axis variables in system.json renamed with units: " + base::Join(renamed, ", ") +
                                " — the VisionMaster solution must have matching variables (Setting > Create missing variables)",
                            LogLevel::Warning);
                    }

                    Log("system.json loaded");
                    return;
                }
            }

            SetSys(SystemData::CreateDefault());
            SaveSystemData();
            Log("No system.json — created defaults", LogLevel::Warning);
        }
        catch (const std::exception& ex)
        {
            SetSys(SystemData::CreateDefault());
            AlarmManager::Raise(AlarmCode::ConfigInvalid, "Data", ex.what());
        }
    }

    bool McDataManager::SaveSystemData()
    {
        try
        {
            WriteAtomic(AppPaths::SystemFile(), SysData()->ToJsonText());
            return true;
        }
        catch (const std::exception& ex)
        {
            AlarmManager::RaiseEvent(AlarmCode::ConfigInvalid, "Data", Loc::F("Data.SaveSystemFailed", ex.what()));
            return false;
        }
    }

    // ───────────────────────── running.json ─────────────────────────

    void McDataManager::LoadRunningData()
    {
        try
        {
            if (fs::exists(AppPaths::RunningFile()))
            {
                auto loaded = RunningData::FromJsonText(ReadFile(AppPaths::RunningFile()));
                if (loaded)
                {
                    // Không resume chu trình qua một lần mở lại app — bắt buộc Init lại
                    // toàn bộ máy (về gốc, tray trống) mỗi lần khởi động, kể cả khi phiên
                    // trước dừng dở dang. Bộ đếm OK/NG và model dùng gần nhất vẫn giữ
                    // nguyên vì đó là số liệu báo cáo/tiện ích, không phải trạng thái máy.
                    loaded->ResetCycleProgress();
                    SetRun(loaded);

                    Log(std::format("running.json loaded (counters OK {}/NG {}) — cycle always restarts from S00, "
                                    "previous session progress discarded",
                                    loaded->CountOk, loaded->CountNg));
                    return;
                }
            }
            SetRun(std::make_shared<RunningData>());
        }
        catch (const std::exception& ex)
        {
            SetRun(std::make_shared<RunningData>());
            Log(std::string("running.json read error: ") + ex.what(), LogLevel::Error);
        }
    }

    bool McDataManager::SaveRunningData()
    {
        try
        {
            auto run = RunData();
            run->SavedAt = std::chrono::system_clock::now();
            WriteAtomic(AppPaths::RunningFile(), run->ToJsonText());
            AlarmManager::ClearCondition(AlarmCode::RunningDataFailed);
            return true;
        }
        catch (const std::exception& ex)
        {
            AlarmManager::Raise(AlarmCode::RunningDataFailed, "Data", ex.what());
            return false;
        }
    }

    // ───────────────────────── model ─────────────────────────

    std::vector<std::string> McDataManager::ListModels()
    {
        std::vector<std::string> names;
        try
        {
            for (const auto& e : fs::directory_iterator(AppPaths::ModelDir()))
            {
                if (!e.is_regular_file()) continue;
                if (!base::EqualsIgnoreCase(base::PathToU8(e.path().extension()), ".json")) continue;
                names.push_back(base::PathToU8(e.path().stem()));
            }
            // OrderBy(n => n) của C# (so sánh theo văn hoá, gần như không phân biệt hoa thường).
            std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) {
                std::string la = base::ToLower(a), lb = base::ToLower(b);
                return la != lb ? la < lb : a < b;
            });
        }
        catch (...) { names.clear(); }
        return names;
    }

    bool McDataManager::LoadModel(const std::string& name)
    {
        fs::path path = ModelPath(name);
        try
        {
            if (!fs::exists(path))
            {
                AlarmManager::RaiseEvent(AlarmCode::ModelFileCorrupt, "Data", Loc::F("Data.ModelNotFound", base::PathToU8(path)));
                return false;
            }

            auto loaded = ModelData::FromJsonText(ReadFile(path));
            if (!loaded)
            {
                AlarmManager::RaiseEvent(AlarmCode::ModelFileCorrupt, "Data", base::PathToU8(path));
                return false;
            }

            SetModel(loaded, true);
            RunData()->RecentModelName = name;

            SyncRunTrays();

            Log(std::format("Model '{}' loaded: input tray {}×{} (tray 2 {}), assy tray {}×{}", name,
                            loaded->InputTray->Rows(), loaded->InputTray->Cols(),
                            loaded->InputTray2->IsTaught() ? "taught" : "not taught",
                            loaded->AssyTray->Rows(), loaded->AssyTray->Cols()));
            return true;
        }
        catch (const std::exception& ex)
        {
            AlarmManager::RaiseEvent(AlarmCode::ModelFileCorrupt, "Data", ex.what());
            return false;
        }
    }

    void McDataManager::UseModel(std::shared_ptr<ModelData> model, bool countsAsLoaded)
    {
        if (!model) return;

        SetModel(std::move(model), countsAsLoaded);
        SyncRunTrays();
    }

    void McDataManager::SyncRunTrays()
    {
        auto m = Model();
        if (!m) return;
        m->NormalizeInputTrays();
        int trays = m->InputTraysInUse();
        auto run = RunData();
        Fit(*run->InputTray, m->InputTray->Rows() * trays, m->InputTray->Cols());
        run->InputTray->Trays = trays;
        Fit(*run->AssyTray, m->AssyTray->Rows(), m->AssyTray->Cols());
        run->InputTray->ScanOrder = m->InputTray->ScanOrder;
        run->AssyTray->ScanOrder = m->AssyTray->ScanOrder;
    }

    bool McDataManager::SaveModel(const std::string& nameArg)
    {
        auto model = Model();
        std::string name = nameArg.empty() ? model->Name : nameArg;
        try
        {
            model->Name = name;
            model->ModifiedAt = std::chrono::system_clock::now();
            WriteAtomic(ModelPath(name), model->ToJsonText());
            Log("Model saved: " + name);
            return true;
        }
        catch (const std::exception& ex)
        {
            AlarmManager::RaiseEvent(AlarmCode::ModelSaveFailed, "Data", ex.what());
            return false;
        }
    }

    bool McDataManager::DeleteModel(const std::string& name)
    {
        try
        {
            fs::path path = ModelPath(name);
            if (fs::exists(path)) fs::remove(path);
            Log("Model deleted: " + name);
            return true;
        }
        catch (const std::exception& ex)
        {
            AlarmManager::RaiseEvent(AlarmCode::ModelSaveFailed, "Data", Loc::F("Data.DeleteModelFailed", ex.what()));
            return false;
        }
    }

    bool McDataManager::CopyModel(const std::string& sourceName, const std::string& newName)
    {
        try
        {
            auto data = ModelData::FromJsonText(ReadFile(ModelPath(sourceName)));
            if (!data) return false;

            data->Name = newName;
            data->ModifiedAt = std::chrono::system_clock::now();
            WriteAtomic(ModelPath(newName), data->ToJsonText());

            Log("Model copied: " + sourceName + " → " + newName);
            return true;
        }
        catch (const std::exception& ex)
        {
            AlarmManager::RaiseEvent(AlarmCode::ModelSaveFailed, "Data", Loc::F("Data.CopyModelFailed", ex.what()));
            return false;
        }
    }

    void McDataManager::LoadRecentModel()
    {
        std::string recent = RunData()->RecentModelName;
        if (!base::IsBlank(recent) && LoadModel(recent)) return;

        auto all = ListModels();
        if (!all.empty() && LoadModel(all[0])) return;

        SetModel(std::make_shared<ModelData>(), false);
        SaveModel("DEFAULT");
        LoadModel("DEFAULT");
    }
}
