#pragma once
#include "Communication/IVisionDevice.h"

#include <array>
#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <optional>

namespace sca
{
    struct VisionCalib;   // Data/Model/ModelData.h

    /// <summary>Một biến trong kho biến toàn cục giả: kiểu + giá trị viết theo InvariantCulture
    /// (KeyValuePair&lt;kiểu, giá trị&gt; của bản C#).</summary>
    struct FakeGlobalVar
    {
        std::string Type;
        std::string Value;
    };

    /// <summary>
    /// Vision mô phỏng. Trả về tư thế mm/độ có nhiễu ngẫu nhiên, kèm xác suất không tìm thấy để
    /// chạy được nhánh NG của chuỗi Auto (PLAN.md §5.4).
    ///
    /// <para><b>Đơn vị là mm ở MỌI Flow</b> (Tuấn chốt 2026-09-12): solution thật kết thúc bằng
    /// module Calibration Transformation nên không Flow nào còn trả pixel, mô phỏng phải giống
    /// đúng như vậy — nếu không, bài tự kiểm sẽ khoá lại một hợp đồng không tồn tại.</para>
    ///
    /// <para><b>Bản sao Script</b> (<see cref="CallFlow"/>): mỗi Flow chạy đúng phép tính của Script
    /// trong cẩm nang trên kho biến toàn cục giả, nên bài tự kiểm kiểm được App đối chiếu, chặn và điều hướng đúng
    /// với cùng những con số solution thật sẽ trả. Bản rút gọn 2026-09-13: AlignBottom_ToolN tính luôn lệnh đặt,
    /// không còn PlaceAlign/TopDownVerify/CalibStatus.</para>
    ///
    /// <para>Thuộc tính get/set của bản C# thành cặp <c>Tên()</c> / <c>SetTên(v)</c> (nguyên tử, đổi lúc đang chạy
    /// vẫn an toàn); thuộc tính chỉ đọc thành <c>Tên()</c>.</para>
    /// </summary>
    class FakeVision : public IVisionDevice
    {
    public:
        FakeVision();

        bool IsConnected() const override { return m_connected.load(); }
        bool IsSolutionLoaded() const override { return m_loaded.load(); }

#define SCA_FAKE_PROP(T, Name, init) \
        T Name() const { return m_##Name.load(); } \
        void Set##Name(T v) { m_##Name.store(v); } \
    private: std::atomic<T> m_##Name{ init }; public:

        /// <summary>Thời gian chụp + xử lý giả lập, ms.</summary>
        SCA_FAKE_PROP(int, ProcessTimeMs, 120)
        /// <summary>Xác suất tìm thấy đối tượng.</summary>
        SCA_FAKE_PROP(double, FoundRate, 1.0)
        /// <summary>Xác suất riêng của CAM bottom-up (AlignBottom_ToolN), lấy min với <see cref="FoundRate"/> — để bài
        /// test làm hỏng lần soi sản phẩm mà LocateTop vẫn quét được ô đích.</summary>
        SCA_FAKE_PROP(double, BottomFoundRate, 1.0)
        /// <summary>Độ lệch tối đa mô phỏng của CAM top-down quanh tâm khung hình, mm.</summary>
        SCA_FAKE_PROP(double, NoiseTopMm, 0.4)
        /// <summary>Độ lệch góc tối đa mô phỏng, độ.</summary>
        SCA_FAKE_PROP(double, NoiseAngle, 4.0)
        /// <summary>Nhiễu đo của CAM bottom-up, mm.</summary>
        SCA_FAKE_PROP(double, NoiseAlignMm, 0.05)
        /// <summary>
        /// Độ lệch tâm mô phỏng: sản phẩm KHÔNG bao giờ nằm đúng tâm xoay, nên Flow AlignBottom
        /// phải trả về một vector khác 0. Nhờ vậy chuỗi Auto mô phỏng thật sự đi qua phần "xoay
        /// làm tâm sản phẩm dịch đi" (§6.6) thay vì luôn nhận lượng bù bằng 0 — bỏ số này về 0 là
        /// mất luôn khả năng phát hiện lỗi ở đúng chỗ dễ sai nhất.
        /// </summary>
        SCA_FAKE_PROP(double, SimEccentricityMm, 1.5)

        /// <summary>Bài tự kiểm bật để giả module hiệu chuẩn báo lỗi khi xoá điểm.</summary>
        SCA_FAKE_PROP(bool, FailCalibClear, false)

        /// <summary>Bật quang học giả: LocateTop thấy dấu chuẩn cố định ở (<see cref="MarkX"/>,
        /// <see cref="MarkY"/>), AlignBottom thấy sản phẩm xoay theo trục A. Tắt thì lượng dịch khớp
        /// mẫu chỉ là nhiễu quanh 0, không gắn với trục — TestRoutineCalib dựa vào đó để thấy phép kiểm tra từ chối.</summary>
        SCA_FAKE_PROP(bool, SimulateOptics, false)
        SCA_FAKE_PROP(double, MarkX, 0.0)
        SCA_FAKE_PROP(double, MarkY, 0.0)
        /// <summary>Góc LocateTop báo cho dấu chuẩn khi bật quang học giả — mốc 0° của mẫu khớp camera trên
        /// không trùng mốc của camera dưới, bài hand-eye đo mốc góc dựa vào đó.</summary>
        SCA_FAKE_PROP(double, MarkA, 0.0)
        /// <summary>Chiều quay của ảnh bottom-up so với trục A khi bật quang học giả (±1).</summary>
        SCA_FAKE_PROP(int, BottomAngleSign, 1)

        // ── gây lỗi có chủ ý cho bài tự kiểm ──
        /// <summary>Ép <c>Status</c> phần tính lệnh của AlignBottom_ToolN trong <see cref="ForcePlaceStatusTimes"/> lần tới.</summary>
        SCA_FAKE_PROP(int, ForcePlaceStatus, 0)
        SCA_FAKE_PROP(int, ForcePlaceStatusTimes, 0)
        /// <summary>Ép góc bù (CmdA đi theo) bằng số này trong <see cref="ForceDAngleTimes"/> lần tính lệnh tới — dựng nhánh N.</summary>
        SCA_FAKE_PROP(double, ForceDAngleDeg, 0.0)
        SCA_FAKE_PROP(int, ForceDAngleTimes, 0)
        /// <summary>Cộng vào đích và lệnh X trong <see cref="TargetShiftTimes"/> lần tính lệnh tới — dựng nhánh G.</summary>
        SCA_FAKE_PROP(double, TargetShiftMm, 0.0)
        SCA_FAKE_PROP(int, TargetShiftTimes, 0)
        /// <summary>LocateTop không thấy chi tiết đích trong <see cref="TopMissTimes"/> lần chụp tới — dựng ô soi NG ở S05.</summary>
        SCA_FAKE_PROP(int, TopMissTimes, 0)
        /// <summary>Lần AlignBottom_ToolN kèm ô đích kế tiếp trả EchoRunId sai.</summary>
        SCA_FAKE_PROP(bool, CorruptEchoOnce, false)
#undef SCA_FAKE_PROP

        /// <summary>Số lần đã trigger từng Flow — để bài tự kiểm kiểm chứng những thay đổi kiểu
        /// "bớt chụp lại" (vd. S05_ScanAssy) thật sự bớt trigger chứ không chỉ vẫn ra đúng kết quả
        /// theo cách cũ (nhiều trigger hơn) mà tình cờ vẫn đạt.</summary>
        int AlignBottomCount() const { return m_alignBottomCount.load(); }
        int LocateTopCount() const { return m_locateTopCount.load(); }

        /// <summary>Vị trí trục kèm theo lần nạp điểm hiệu chuẩn gần nhất — thay cho các biến toàn
        /// cục của solution thật, để bài tự kiểm kiểm được App có gửi đúng toạ độ ENCODER hay
        /// không. nullopt nghĩa là chưa có lần nạp điểm nào.</summary>
        std::optional<AxisSnapshot> LastCalibAxes() const;

        /// <summary>Số lần đã nạp điểm hiệu chuẩn (mọi Flow calib).</summary>
        int CalibPointCount() const { return m_calibPointCount.load(); }

        /// <summary>Số lần đã xoá điểm hiệu chuẩn, phép của lần xoá gần nhất, và số điểm nạp từ lần xoá đó — để
        /// bài tự kiểm kiểm chuỗi nạp điểm xoá trước rồi mới nạp.</summary>
        int CalibClearCount() const { return m_calibClearCount.load(); }
        std::optional<EVisionProcedure> LastCalibClear() const;
        int CalibPointsSinceClear() const { return m_calibPointsSinceClear.load(); }

        // Mô phỏng không có camera thật để chụp — luôn nullptr, trang Main tự hiện chỗ trống thay
        // vì hình khi chạy bài tự kiểm/dev không có máy.
        std::shared_ptr<const ImageData> LastTopDownImage() const override { return nullptr; }
        std::shared_ptr<const ImageData> LastBottomUpImageTool1() const override { return nullptr; }
        std::shared_ptr<const ImageData> LastBottomUpImageTool2() const override { return nullptr; }

        bool Connect() override;
        void Disconnect() override;
        bool LoadSolution(const std::string& solutionPath) override;

        /// <summary>
        /// Mô phỏng nạp một điểm hiệu chuẩn: ghi lại vị trí trục "gửi sang VisionMaster" rồi báo thấy / không
        /// thấy theo <see cref="FoundRate"/>. Solution thật nhận toạ độ này qua biến toàn cục; ở đây chỉ giữ lại
        /// để bài tự kiểm đối chiếu. Bị huỷ trong lúc chờ thì ném <c>base::OperationCanceled</c>.
        /// </summary>
        VisionPose TriggerCalib(EVisionProcedure procedure, const AxisSnapshot& axes, std::stop_token ct) override;
        /// <summary>Đếm lần xoá, nhớ phép, xoá bộ đếm điểm từ lần xoá trước; <see cref="FailCalibClear"/> thì báo lỗi.</summary>
        std::string ClearCalibPoints(EVisionProcedure procedure) override;

        GlobalVarSyncReport SyncGlobalVariables(const std::vector<GlobalVarSpec>& specs, bool apply) override;
        VmFlowOutputs ReadFlowOutputs(const std::string& flow) override;
        /// <summary>Bị huỷ trong lúc chờ thì ném <c>base::OperationCanceled</c> (như bản C#).</summary>
        VmCallResult CallFlow(const VmCallRequest& request, std::stop_token ct) override;
        std::string SaveSolution() override;

        // ── kho biến toàn cục giả: tên → (kiểu, giá trị InvariantCulture) — bản C# để lộ thẳng Dictionary GlobalVars ──
        /// <summary>Bản chụp kho biến.</summary>
        std::map<std::string, FakeGlobalVar> GlobalVarsSnapshot() const;
        /// <summary>Đặt thẳng một biến (tạo mới hoặc ghi đè cả kiểu) — bài tự kiểm dựng sẵn biến trong này để kiểm
        /// luật "chỉ thêm, không sửa" (<c>GlobalVars[name] = new KeyValuePair(type, value)</c>).</summary>
        void PutGlobalVar(const std::string& name, const std::string& type, const std::string& value);
        void RemoveGlobalVar(const std::string& name);
        /// <summary><c>GlobalVars.Clear()</c>.</summary>
        void ClearGlobalVars();

        /// <summary>Số lần đã "lưu solution".</summary>
        int SolutionSaveCount() const { return m_solutionSaveCount.load(); }

        /// <summary>Ghi ngày Bước 2 vào bản ghi hiệu chuẩn — như máy đã chạy xong Bước 2 (model mô phỏng có sẵn Bước 4, 5).
        /// Chỉ dùng khi mô phỏng. nullptr thì không làm gì.</summary>
        void StampCalibrated(VisionCalib* calib);

        /// <summary>Tư thế sản phẩm ép cho AlignBottom (X, Y, A); nullopt thì theo quang học giả hoặc nhiễu — TestVmTwinMath
        /// dựng đáp số từ đây.</summary>
        void SetForcedObjPose(std::optional<std::array<double, 3>> pose);
        std::optional<std::array<double, 3>> ForcedObjPose() const;

        /// <summary>Số lần AlignBottom_ToolN được gọi kèm ô đích (tính lệnh đặt).</summary>
        int PlaceComputeCount() const { return m_placeComputeCount.load(); }

        /// <summary>Biến toàn cục của lần AlignBottom_ToolN kèm ô đích gần nhất (nullopt = chưa có).</summary>
        std::optional<std::map<std::string, double>> LastPlaceGlobals() const;

        /// <summary>Thứ tự các Flow đã gọi (tối đa 5000 lần gần nhất).</summary>
        std::vector<std::string> FlowHistorySnapshot() const;
        void ClearFlowHistory();
        int VmCallCount() const { return m_vmCallCount.load(); }

        /// <summary>Dựng kho biến theo danh mục tạo biến.</summary>
        void SeedVmSolution(const std::vector<GlobalVarSpec>& specs);
        /// <summary>Đọc một biến giả (NaN nếu không có) — cho bài tự kiểm.</summary>
        double GetGlobal(const std::string& name) const;
        /// <summary>Ghi một biến giả giữ nguyên kiểu (không có biến thì bỏ qua) — cho bài tự kiểm.</summary>
        void SetGlobal(const std::string& name, double value);

        // ── móc nối (đặt trước khi dùng; đổi lúc đang chạy vẫn an toàn) ──
        /// <summary>Tên Flow/biến trục đang cấu hình — McControl gắn vào <c>SystemData.VisionOutputs</c>
        /// (<c>Names</c> của bản C#). Rỗng → bộ tên mặc định.</summary>
        void SetNames(std::function<VisionOutputNames()> names);
        /// <summary>Nguồn bản khai output khi mô phỏng — McControl gắn vào <c>VmCycle.FakeFlowOutputs</c>;
        /// bài tự kiểm thay bằng bản có lỗi để kiểm tool báo đúng chỗ (<c>FlowOutputSource</c> của bản C#).</summary>
        void SetFlowOutputSource(std::function<VmFlowOutputs(const std::string&)> source);
        /// <summary>Gọi sau khi Flow chạy xong, trước khi App đọc output — để bài test chen một chuyển động vào; và ngay
        /// lúc "chụp" của TriggerCalib với tên phép (<see cref="ToString(EVisionProcedure)"/>) — soi trạng thái đèn CAM
        /// (<c>AfterFlow</c> của bản C#).</summary>
        void SetAfterFlow(std::function<void(const std::string&)> afterFlow);

    private:
        /// System.Random của .NET Framework — cùng hạt giống thì cùng dãy số với bản C#.
        class DotNetRandom
        {
        public:
            explicit DotNetRandom(int seed);
            double NextDouble();
        private:
            int m_seedArray[56]{};
            int m_inext{ 0 };
            int m_inextp{ 21 };
        };

        double Noise(double amplitude);
        double G(const std::string& name) const;
        int Gi(const std::string& name) const;
        void SetG(const std::string& name, double value);
        void RunTopCamera(const VisionOutputNames& n, std::map<std::string, double>& o);
        void RunAlignBottom(const VisionOutputNames& n, std::map<std::string, double>& o, int tool);

        std::atomic<bool> m_connected{ false };
        std::atomic<bool> m_loaded{ false };
        std::atomic<int> m_alignBottomCount{ 0 }, m_locateTopCount{ 0 }, m_calibPointCount{ 0 };
        std::atomic<int> m_calibClearCount{ 0 }, m_calibPointsSinceClear{ 0 };
        std::atomic<int> m_solutionSaveCount{ 0 }, m_placeComputeCount{ 0 }, m_vmCallCount{ 0 };

        mutable std::recursive_mutex m_lock;   // lock (GlobalVars) của bản C#: kho biến, lịch sử, rng, móc nối
        std::map<std::string, FakeGlobalVar> m_globalVars;
        std::vector<std::string> m_flowHistory;
        std::optional<AxisSnapshot> m_lastCalibAxes;
        std::optional<EVisionProcedure> m_lastCalibClear;
        std::optional<std::map<std::string, double>> m_lastPlaceGlobals;
        std::optional<std::array<double, 3>> m_forcedObjPose;
        DotNetRandom m_rng;
        std::function<VisionOutputNames()> m_names;
        std::function<VmFlowOutputs(const std::string&)> m_flowOutputSource;
        std::function<void(const std::string&)> m_afterFlow;
    };
}
