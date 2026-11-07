#include "Data/Model/ModelData.h"

#include "Data/DataJson.h"
#include "Data/RunningData.h"
#include "Lang/Loc.h"

namespace sca
{
    using datajson::json;
    using jsonx::Get;

    // ───────────────────────── VisionCalib ─────────────────────────

    void VisionCalib::SetHandEyeTarget(int nozzle, double x, double y, double a, double axX, double axY)
    {
        if (nozzle == 1) { HandEye1TopX = x; HandEye1TopY = y; HandEye1TopA = a; HandEye1TopAxX = axX; HandEye1TopAxY = axY; }
        else { HandEye2TopX = x; HandEye2TopY = y; HandEye2TopA = a; HandEye2TopAxX = axX; HandEye2TopAxY = axY; }
    }

    bool VisionCalib::TryGetHandEyeMasterAxis(int nozzle, double& axX, double& axY) const
    {
        const std::optional<double>& px = nozzle == 1 ? HandEye1TopAxX : HandEye2TopAxX;
        const std::optional<double>& py = nozzle == 1 ? HandEye1TopAxY : HandEye2TopAxY;
        axX = px.value_or(0); axY = py.value_or(0);
        return px.has_value() && py.has_value();
    }

    bool VisionCalib::TryGetHandEyeTarget(int nozzle, double& x, double& y, double& a) const
    {
        const std::optional<double>& px = nozzle == 1 ? HandEye1TopX : HandEye2TopX;
        const std::optional<double>& py = nozzle == 1 ? HandEye1TopY : HandEye2TopY;
        const std::optional<double>& pa = nozzle == 1 ? HandEye1TopA : HandEye2TopA;
        x = px.value_or(0); y = py.value_or(0); a = pa.value_or(0);
        return px.has_value() && py.has_value() && pa.has_value();
    }

    void VisionCalib::SetHandEye(int nozzle, double dx, double dy, double angleRef, TimePoint at)
    {
        if (nozzle == 1) { HandEye1Dx = dx; HandEye1Dy = dy; HandEye1AngleRef = angleRef; HandEye1MeasuredAt = at; HandEye1YFixed = true; }
        else { HandEye2Dx = dx; HandEye2Dy = dy; HandEye2AngleRef = angleRef; HandEye2MeasuredAt = at; HandEye2YFixed = true; }
    }

    void VisionCalib::SetAlignVerified(int nozzle, int sign, double posX, double residualMm, TimePoint at)
    {
        if (nozzle == 1)
        {
            AlignAngleSign1 = sign; AlignVerifiedPosX1 = posX;
            AlignVerifyResidual1 = residualMm; AlignVerifiedAt1 = at;
        }
        else
        {
            AlignAngleSign2 = sign; AlignVerifiedPosX2 = posX;
            AlignVerifyResidual2 = residualMm; AlignVerifiedAt2 = at;
        }
    }

    bool VisionCalib::IsHandEyeOutdated(int nozzle) const
    {
        if (HandEyeMeasuredAt(nozzle) <= TimePoint{}) return false;
        bool hasAngle = (nozzle == 1 ? HandEye1AngleRef : HandEye2AngleRef).has_value();
        bool yFixed = nozzle == 1 ? HandEye1YFixed : HandEye2YFixed;
        return !hasAngle || !yFixed;
    }

    bool VisionCalib::IsExpired(int validDays) const
    {
        if (!IsUsable()) return false;
        TimePoint oldest = AlignVerifiedAt1 < AlignVerifiedAt2 ? AlignVerifiedAt1 : AlignVerifiedAt2;
        double days = std::chrono::duration<double>(std::chrono::system_clock::now() - oldest).count() / 86400.0;
        return days > validDays;
    }

    // ───────────────────────── ModelData ─────────────────────────

    std::shared_ptr<TrayLayout> ModelData::InputCell(const TrayState& run, int index, int& tray, int& row, int& col) const
    {
        run.RowCol(index, row, col);
        int rows = InputTray->Rows();
        tray = row >= rows ? 1 : 0;
        if (tray == 1) row -= rows;
        return InputLayout(tray);
    }

    void ModelData::NormalizeInputTrays()
    {
        if (!InputTray2) InputTray2 = TrayLayout::CreateInputDefault("InputTray2");
        auto& t1 = *InputTray;
        auto& t2 = *InputTray2;
        if (t2.Rows() != t1.Rows() || t2.Cols() != t1.Cols()) t2.Resize(t1.Rows(), t1.Cols());
        t2.ScanOrder = t1.ScanOrder;
        t2.AxisRow = t1.AxisRow;
        t2.AxisCol = t1.AxisCol;

        bool fresh = !t2.GeoZ1.P00.Taught && !t2.GeoZ2.P00.Taught && t2.Z_PickZ1 == 0 && t2.Z_PickZ2 == 0;
        if (fresh)
        {
            t2.Z_PickZ1 = t1.Z_PickZ1;
            t2.Z_PickZ2 = t1.Z_PickZ2;
            t2.Z_SlowZ1 = t1.Z_SlowZ1;
            t2.Z_SlowZ2 = t1.Z_SlowZ2;
            t2.Z_Safe = t1.Z_Safe;
            t2.VerifyThreshold = t1.VerifyThreshold;
        }
    }

    std::string ModelData::HandEyeJigProblem(int nozzle) const
    {
        if (!HandEyePick(nozzle).Taught)
            return Loc::F("Data.JigPickNotTaught", nozzle);
        if (!HandEyeCamPos.Taught)
            return Loc::T("Data.JigCamNotTaught");
        return {};
    }

    std::shared_ptr<ModelData> ModelData::CreateSimulation()
    {
        auto m = std::make_shared<ModelData>();
        m->Name = "SIM";
        m->Description = "Simulation model — made-up numbers, for cycle test runs only";

        // Mỗi Tool có lưới riêng (TrayGeometry) — Z2 cố ý lệch Z1 một lượng KHÔNG PHẢI một
        // shift X thuần tuý (−30 X, +0.4 Y) để mô phỏng vừa pitch cơ khí vừa sai lệch nhỏ
        // ngoài trục X, bắt được bài test nào lỡ giả định "Tool2 = Tool1 trừ hằng số X" thay
        // vì đọc đúng lưới riêng của Tool2 (Tuấn xác nhận 2026-09-05).
        constexpr double pitchX = 30.0, skewY = 0.4;

        // Lưới CỐ Ý NHỎ (Tuấn yêu cầu 2026-09-23): --selftest chạy chu trình Auto trên máy mô phỏng theo
        // thời gian thật, nên mỗi ô thêm vào là thêm một lượt chạy trục + một lần kích Flow ở MỌI kịch bản
        // có quét tray. Rút 4×5 → 2×5 và 5×6 → 3×4 cắt được hơn nửa thời gian chạy mà không bỏ phép kiểm
        // nào — mọi phép đếm trong self-test đều so theo CellCount chứ không ghi cứng số ô. Bước lưới vẫn
        // khác nhau theo hàng và theo cột (15/25 và 22/26 mm) để phép nội suy 3 điểm còn ý nghĩa.
        // 5 cột — số LẺ — là cố ý: chỉ khi số cột lẻ mới có cặp ô HÚT vắt qua hai hàng (ô cột cuối
        // hàng trên ghép với ô cột đầu hàng dưới), mà đó đúng là nhánh duy nhất luật giao ô hút theo
        // hình học sinh ra để xử lý (Tuấn chốt 2026-09-24). Để 4 cột thì nhánh ấy không bao giờ chạy
        // trong self-test và bài kiểm đạt rỗng — đừng đổi về chẵn. Vẫn giữ tray nhỏ: 2×5 = 10 ô.
        auto& in = *m->InputTray;
        in.SetRows(2); in.SetCols(5);
        in.GeoZ1.P00 = TeachPoint(150, 50);
        in.GeoZ1.P0C = TeachPoint(210, 50);      // 4 bước × 15 mm
        in.GeoZ1.PR0 = TeachPoint(150, 75);      // 1 bước × 25 mm
        in.GeoZ2.P00 = TeachPoint(150 - pitchX, 50 + skewY);
        in.GeoZ2.P0C = TeachPoint(210 - pitchX, 50 + skewY);
        in.GeoZ2.PR0 = TeachPoint(150 - pitchX, 75 + skewY);
        // Z dương là chiều ĐI XUỐNG, "an toàn" (Z_SAFE mặc định 50) là vị trí NHỎ (Tuấn xác
        // nhận 2026-09-04) — Z_Pick phải LỚN HƠN Z_SAFE để mô phỏng đúng "đã hạ xuống, chưa
        // an toàn", nếu không chốt chặn Z_SAFE không được bài test nào chạy qua thật sự. Z1/Z2
        // cố ý lệch nhau 1 mm — số giả, chỉ để bài test không vô tình phụ thuộc vào việc hai
        // tay hút luôn cùng cao độ (Tuấn xác nhận 2026-09-05: hai tay không nhất thiết bằng nhau).
        in.Z_PickZ1 = 90;
        in.Z_PickZ2 = 91;
        // Điểm giảm tốc nằm giữa cao độ an toàn (Z_SAFE mặc định 50) và cao độ hút: 5 mm cuối
        // chạy mức Auto thấp. Lệch nhau 1 mm giữa hai tay, cùng lý do với Z_Pick ở trên.
        in.Z_SlowZ1 = 85;
        in.Z_SlowZ2 = 86;

        auto& as = *m->AssyTray;
        as.SetRows(3); as.SetCols(4);
        // Quét zigzag: hàng chẵn chạy ngược, nên MỌI kịch bản Auto của self-test đi qua đúng nhánh mà
        // luật giao ô theo hình học sinh ra để xử lý (Tuấn chốt 2026-09-24). Để tuần tự thì nhánh ấy
        // không bao giờ chạy trong self-test — đừng đổi về.
        as.ScanOrder = EScanOrder::Serpentine;
        as.GeoZ1.P00 = TeachPoint(350, 60);
        as.GeoZ1.P0C = TeachPoint(416, 60);       // 3 bước × 22 mm
        as.GeoZ1.PR0 = TeachPoint(350, 112);      // 2 bước × 26 mm
        as.GeoZ2.P00 = TeachPoint(350 - pitchX, 60 + skewY);
        as.GeoZ2.P0C = TeachPoint(416 - pitchX, 60 + skewY);
        as.GeoZ2.PR0 = TeachPoint(350 - pitchX, 112 + skewY);
        as.Z_PickZ1 = 95;
        as.Z_PickZ2 = 96;
        as.Z_SlowZ1 = 90;
        as.Z_SlowZ2 = 91;

        // CAM bottom-up cố định ở X = 300 (toạ độ của đầu hút khi nằm giữa khung hình), teach
        // riêng từng Tool nên không suy ra từ pitch nữa.
        m->AlignPosX_Nozzle1 = 300;
        m->AlignPosX_Nozzle2 = 300 - pitchX;
        m->PickAngleA1 = 3.5;
        m->PickAngleA2 = 356.5;

        // Ba điểm dạy của cơ cấu kẹp (bước xác định hand-eye). Số CỐ Ý không trùng bất kỳ ô tray, mốc CAM
        // top-down hay vị trí soi CAM dưới nào ở trên: nếu trùng thì một routine vẫn đọc lưới tray cũng
        // làm phép kiểm đạt rỗng. Và hai điểm gắp cố ý lệch nhau 35 mm chứ KHÔNG phải pitchX, để không
        // bài test nào suy được tay #2 từ tay #1 bằng một hằng số.
        m->HandEyePick_Nozzle1 = TeachPoint(240, 100);
        m->HandEyePick_Nozzle2 = TeachPoint(205, 100.7);
        m->HandEyeCamPos = TeachPoint(282, 118);

        auto now = std::chrono::system_clock::now();
        m->Calib.SetHandEye(1, -40, 5, 0, now);
        // Tay #2 lắp cách tay #1 đúng pitchX theo X (lưới Z2 = Z1 − pitchX, + skewY): muốn đích tính từ
        // camera rơi đúng ô của lưới Z2 thì hand-eye tay #2 phải là (−40 − pitchX, 5 + skewY). Bản cũ để
        // −10 thì đích tay #2 lệch lưới 60 mm, mà Auto chặn đích lệch lưới teach (thiết kế R27).
        m->Calib.SetHandEye(2, -40 - pitchX, 5 + skewY, 0, now);

        // Dấu góc +1 = mô phỏng coi góc VisionMaster cùng chiều trục A. Trên máy thật con số
        // này PHẢI đo bằng phép thử vòng xoay, không được chép từ đây.
        m->Calib.SetAlignVerified(1, +1, m->AlignPosX_Nozzle1, 0, now);
        m->Calib.SetAlignVerified(2, +1, m->AlignPosX_Nozzle2, 0, now);

        // Mốc CAM top-down = vị trí camera khi soi Ô (1,1) của Assy Tray — giữ tương đương
        // công thức cũ (cellPos(0,0) trừ Hand-Eye tay #1) để hành vi mô phỏng không đổi. Ô khác
        // suy ra theo lưới TrayLayout::CamGridZAxis; hai lưới giả lập ở trên cùng bước ô nên đổi
        // lưới đó không làm số liệu mô phỏng đổi theo.
        as.CamTopDownAnchorX = as.GeoZ1.P00.X - m->Calib.HandEye1Dx;
        as.CamTopDownAnchorY = as.GeoZ1.P00.Y - m->Calib.HandEye1Dy;
        as.CamTopDownAnchorTaught = true;

        return m;
    }

    // ───────────────────────── JSON ─────────────────────────

    namespace
    {
        using datajson::Num;
        using datajson::GetNum;

        void PutOpt(json& j, const char* key, const std::optional<double>& v)
        {
            if (v) j[key] = Num(*v);   // null → bỏ khoá (NullValueHandling.Ignore)
        }

        void GetOpt(const json& j, const char* key, std::optional<double>& v)
        {
            auto it = j.find(key);
            if (it == j.end()) return;
            if (it->is_null()) { v.reset(); return; }   // null trong file → null (như Newtonsoft)
            if (it->is_number()) { v = it->get<double>(); return; }
            if (it->is_string())
            {
                const std::string& s = it->get_ref<const std::string&>();
                if (s == "NaN" || s == "Infinity" || s == "-Infinity")
                {
                    double d = 0;
                    GetNum(j, key, d);
                    v = d;
                }
            }
        }

        json ToJson(const VisionCalib& c)
        {
            json j = json::object();
            j["HandEye1Dx"] = Num(c.HandEye1Dx);
            j["HandEye1Dy"] = Num(c.HandEye1Dy);
            j["HandEye2Dx"] = Num(c.HandEye2Dx);
            j["HandEye2Dy"] = Num(c.HandEye2Dy);
            j["HandEye1MeasuredAt"] = ToIsoString(c.HandEye1MeasuredAt);
            j["HandEye2MeasuredAt"] = ToIsoString(c.HandEye2MeasuredAt);
            PutOpt(j, "HandEye1AngleRef", c.HandEye1AngleRef);
            PutOpt(j, "HandEye2AngleRef", c.HandEye2AngleRef);
            j["HandEye1YFixed"] = c.HandEye1YFixed;
            j["HandEye2YFixed"] = c.HandEye2YFixed;
            PutOpt(j, "HandEye1TopX", c.HandEye1TopX);
            PutOpt(j, "HandEye1TopY", c.HandEye1TopY);
            PutOpt(j, "HandEye1TopA", c.HandEye1TopA);
            PutOpt(j, "HandEye2TopX", c.HandEye2TopX);
            PutOpt(j, "HandEye2TopY", c.HandEye2TopY);
            PutOpt(j, "HandEye2TopA", c.HandEye2TopA);
            PutOpt(j, "HandEye1TopAxX", c.HandEye1TopAxX);
            PutOpt(j, "HandEye1TopAxY", c.HandEye1TopAxY);
            PutOpt(j, "HandEye2TopAxX", c.HandEye2TopAxX);
            PutOpt(j, "HandEye2TopAxY", c.HandEye2TopAxY);
            j["AlignAngleSign1"] = c.AlignAngleSign1;
            j["AlignAngleSign2"] = c.AlignAngleSign2;
            j["AlignVerifiedAt1"] = ToIsoString(c.AlignVerifiedAt1);
            j["AlignVerifiedAt2"] = ToIsoString(c.AlignVerifiedAt2);
            j["AlignVerifiedPosX1"] = Num(c.AlignVerifiedPosX1);
            j["AlignVerifiedPosX2"] = Num(c.AlignVerifiedPosX2);
            j["AlignVerifyResidual1"] = Num(c.AlignVerifyResidual1);
            j["AlignVerifyResidual2"] = Num(c.AlignVerifyResidual2);
            j["TopDownVerifiedAt"] = ToIsoString(c.TopDownVerifiedAt);
            j["IsTopDownVerified"] = c.IsTopDownVerified();   // chỉ-đọc: Newtonsoft vẫn ghi
            j["IsUsable"] = c.IsUsable();
            return j;
        }

        void FromJson(const json& j, VisionCalib& c)
        {
            GetNum(j, "HandEye1Dx", c.HandEye1Dx);
            GetNum(j, "HandEye1Dy", c.HandEye1Dy);
            GetNum(j, "HandEye2Dx", c.HandEye2Dx);
            GetNum(j, "HandEye2Dy", c.HandEye2Dy);
            jsonx::GetTime(j, "HandEye1MeasuredAt", c.HandEye1MeasuredAt);
            jsonx::GetTime(j, "HandEye2MeasuredAt", c.HandEye2MeasuredAt);
            GetOpt(j, "HandEye1AngleRef", c.HandEye1AngleRef);
            GetOpt(j, "HandEye2AngleRef", c.HandEye2AngleRef);
            Get(j, "HandEye1YFixed", c.HandEye1YFixed);
            Get(j, "HandEye2YFixed", c.HandEye2YFixed);
            GetOpt(j, "HandEye1TopX", c.HandEye1TopX);
            GetOpt(j, "HandEye1TopY", c.HandEye1TopY);
            GetOpt(j, "HandEye1TopA", c.HandEye1TopA);
            GetOpt(j, "HandEye2TopX", c.HandEye2TopX);
            GetOpt(j, "HandEye2TopY", c.HandEye2TopY);
            GetOpt(j, "HandEye2TopA", c.HandEye2TopA);
            GetOpt(j, "HandEye1TopAxX", c.HandEye1TopAxX);
            GetOpt(j, "HandEye1TopAxY", c.HandEye1TopAxY);
            GetOpt(j, "HandEye2TopAxX", c.HandEye2TopAxX);
            GetOpt(j, "HandEye2TopAxY", c.HandEye2TopAxY);
            Get(j, "AlignAngleSign1", c.AlignAngleSign1);
            Get(j, "AlignAngleSign2", c.AlignAngleSign2);
            jsonx::GetTime(j, "AlignVerifiedAt1", c.AlignVerifiedAt1);
            jsonx::GetTime(j, "AlignVerifiedAt2", c.AlignVerifiedAt2);
            GetNum(j, "AlignVerifiedPosX1", c.AlignVerifiedPosX1);
            GetNum(j, "AlignVerifiedPosX2", c.AlignVerifiedPosX2);
            GetNum(j, "AlignVerifyResidual1", c.AlignVerifyResidual1);
            GetNum(j, "AlignVerifyResidual2", c.AlignVerifyResidual2);
            jsonx::GetTime(j, "TopDownVerifiedAt", c.TopDownVerifiedAt);
        }

        void ReadLayout(const json& j, const char* key, std::shared_ptr<TrayLayout>& t, const char* defaultName)
        {
            auto it = j.find(key);
            if (it == j.end() || !it->is_object()) return;   // null → giữ đối tượng mặc định (C# sẽ đặt null)
            if (!t) t = TrayLayout::CreateInputDefault(defaultName);
            datajson::FromJson(*it, *t);
        }

        void ReadPoint(const json& j, const char* key, TeachPoint& p)
        {
            auto it = j.find(key);
            if (it != j.end() && it->is_object()) datajson::FromJson(*it, p);
        }
    }

    std::string ModelData::ToJsonText() const
    {
        json j = json::object();
        j["Name"] = Name;
        if (!Description.empty()) j["Description"] = Description;   // null → không ghi
        j["ModifiedAt"] = ToIsoString(ModifiedAt);
        if (InputTray) j["InputTray"] = datajson::ToJson(*InputTray);
        if (AssyTray) j["AssyTray"] = datajson::ToJson(*AssyTray);
        if (InputTray2) j["InputTray2"] = datajson::ToJson(*InputTray2);
        j["InputTraysInUse"] = InputTraysInUse();     // chỉ-đọc: Newtonsoft vẫn ghi
        j["UsableInputTrays"] = UsableInputTrays();
        j["Calib"] = ToJson(Calib);
        PutOpt(j, "ZSafe", ZSafe);
        if (!VisionSolutionPath.empty()) j["VisionSolutionPath"] = VisionSolutionPath;   // rỗng → không ghi
        j["AlignPosX_Nozzle1"] = Num(AlignPosX_Nozzle1);
        j["AlignPosX_Nozzle2"] = Num(AlignPosX_Nozzle2);
        j["PickAngleA1"] = Num(PickAngleA1);
        j["PickAngleA2"] = Num(PickAngleA2);
        j["HandEyePick_Nozzle1"] = datajson::ToJson(HandEyePick_Nozzle1);
        j["HandEyePick_Nozzle2"] = datajson::ToJson(HandEyePick_Nozzle2);
        j["HandEyeCamPos"] = datajson::ToJson(HandEyeCamPos);
        return datajson::DumpIndented(j);
    }

    std::shared_ptr<ModelData> ModelData::FromJsonText(const std::string& text)
    {
        json j;
        if (!jsonx::ParseObject(text, j)) return nullptr;

        auto m = std::make_shared<ModelData>();
        Get(j, "Name", m->Name);
        Get(j, "Description", m->Description);
        jsonx::GetTime(j, "ModifiedAt", m->ModifiedAt);
        ReadLayout(j, "InputTray", m->InputTray, "InputTray");
        ReadLayout(j, "AssyTray", m->AssyTray, "AssyTray");
        ReadLayout(j, "InputTray2", m->InputTray2, "InputTray2");
        if (auto it = j.find("Calib"); it != j.end() && it->is_object()) FromJson(*it, m->Calib);
        GetOpt(j, "ZSafe", m->ZSafe);
        if (m->ZSafe && !std::isfinite(*m->ZSafe)) m->ZSafe.reset();   // số hỏng = chưa có, lần nạp sau nhận lại của máy
        Get(j, "VisionSolutionPath", m->VisionSolutionPath);
        GetNum(j, "AlignPosX_Nozzle1", m->AlignPosX_Nozzle1);
        GetNum(j, "AlignPosX_Nozzle2", m->AlignPosX_Nozzle2);
        GetNum(j, "PickAngleA1", m->PickAngleA1);
        GetNum(j, "PickAngleA2", m->PickAngleA2);
        ReadPoint(j, "HandEyePick_Nozzle1", m->HandEyePick_Nozzle1);
        ReadPoint(j, "HandEyePick_Nozzle2", m->HandEyePick_Nozzle2);
        ReadPoint(j, "HandEyeCamPos", m->HandEyeCamPos);
        return m;
    }
}
