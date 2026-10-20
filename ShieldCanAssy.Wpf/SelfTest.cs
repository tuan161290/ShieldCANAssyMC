using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using ShieldCanAssy.Core.Communication.Sim;
using ShieldCanAssy.Core.Controller;
using ShieldCanAssy.Core.Infor;
using ShieldCanAssy.SysConfig;
using AxisAngle = ShieldCanAssy.Core.Communication.AxisAngle;
using EVmCamera = ShieldCanAssy.Core.Communication.EVmCamera;
using EVmOutState = ShieldCanAssy.Core.Communication.EVmOutState;
using VisionOutputNames = ShieldCanAssy.Core.Data.VisionOutputNames;
using VmDeclaredOut = ShieldCanAssy.Core.Communication.VmDeclaredOut;
using VmFlowExpect = ShieldCanAssy.Core.Communication.VmFlowExpect;
using VmFlowOutputs = ShieldCanAssy.Core.Communication.VmFlowOutputs;
using VmIoInfo = ShieldCanAssy.Core.Communication.VmIoInfo;
using VmModuleIo = ShieldCanAssy.Core.Communication.VmModuleIo;
using VmOutCheck = ShieldCanAssy.Core.Communication.VmOutCheck;
using VmOutCheckReport = ShieldCanAssy.Core.Communication.VmOutCheckReport;
using VmScriptContract = ShieldCanAssy.Core.Communication.VmScriptContract;
using VisionGlobalVarCatalog = ShieldCanAssy.Core.Communication.VisionGlobalVarCatalog;
using VmCallRequest = ShieldCanAssy.Core.Communication.VmCallRequest;
using VmCallResult = ShieldCanAssy.Core.Communication.VmCallResult;
using VmOut = ShieldCanAssy.Core.Communication.VmOut;
using VmRunIds = ShieldCanAssy.Core.Communication.VmRunIds;
using VmStatus = ShieldCanAssy.Core.Communication.VmStatus;
using VmVars = ShieldCanAssy.Core.Communication.VmVars;

namespace ShieldCanAssy.Wpf
{
    /// <summary>
    /// Tự kiểm tra không cần giao diện: chạy bằng <c>ShieldCanAssy.Wpf.exe --selftest</c>.
    ///
    /// <para>Kiểm chứng những đường mà bấm tay rất khó lặp lại đúng: chốt chặn Z_SAFE,
    /// phân cấp alarm và quy tắc RESET. Chạy trên tầng mô phỏng nên lặp lại
    /// được và không cần phần cứng — dùng làm bài kiểm tra hồi quy sau mỗi lần sửa
    /// SafetyMonitor/Routine_Origin ở các phase sau.</para>
    /// </summary>
    public static class SelfTest
    {
        private static readonly List<string> _report = new List<string>();
        private static int _pass, _fail;

        public static int Run()
        {
            Log("===== TỰ KIỂM TRA — Shield CAN Assy MC =====");
            Log("Thư mục dữ liệu: " + AppPaths.Root);
            Log("");

            // Các bài so chữ hiển thị theo bản tiếng Việt — chạy self-test luôn ở tiếng Việt, dù system.json đang chọn gì.
            Core.Lang.Loc.SetLanguage(Core.Lang.ELanguage.Vi);

            // CHẨN ĐOÁN TẠM: ghi file ngay sau mỗi bài test (không đợi hết toàn bộ) — để nếu một
            // bài bị treo thì vẫn xem được đã dừng ở bài nào, không phải đoán mò qua Task Manager.
            var tests = new Action[]
            {
                TestStartupState, TestZSafeInterlock, TestOriginSequence, TestEventAlarmsAlwaysResettable,
                TestEmergencyStop, TestLightCurtain, TestStopCancelsManualSequences, TestVacuumHoldsThroughEmg,
                TestHardwareAssumptions, TestResetRules, TestTrayGridMath,
                TestSinglePointAlignMath, TestModelManagement, TestAxisScale, TestServoModbusWrites, TestMoveCompletionRule, TestMoveIncrement,
                TestRotaryWrap, TestPlanarHomeRequiresZHomed, TestZDecelPoint, TestSettleBeforeCapture, TestTrayGridZigzag,
                TestRoutineCalib, TestGlobalVarCreate, TestVmFlowOutputCheck,
                TestAutoCycle, TestAssyPreScan, TestResumeIntegrity, TestTrayChangeover, TestTwoInputTrays, TestCellSelection,
                TestAssyTrayFull, TestDryRun,
                TestVmContract, TestVmTwinMath, TestVmCalib, TestVmAutoCycle, TestVmStepCheck, TestVmFaults, TestVmHalts, TestVmGate,
                TestTwoHandGate, TestBrakeInterlock, TestEmergencyBrakeRelease, TestLanguageTables
            };

            // Thời gian từng bài — để biết cắt ở đâu thì thật sự nhanh lên. Số MỤC kiểm không quyết định
            // thời gian chạy: một bài toán thuần có hàng chục Check mà tốn vài ms, còn một kịch bản Auto
            // chỉ vài Check nhưng phải chờ trục mô phỏng chạy hết chu trình (Tuấn hỏi 2026-09-23).
            // Chờ đèn CAM sáng (SystemData.BottomLampMs/TopLampMs, mặc định 100 ms) là việc của đèn thật; máy mô phỏng
            // không có đèn nên khoảng chờ đó chỉ là thời gian chết, mà self-test kích Flow hàng trăm lần.
            // Tắt ở đây, còn việc App CÓ chờ hay không thì bài đèn CAM trong TestVmCalib tự đặt số và đo.
            //
            // Từ 2026-09-29 (Tuấn) bỏ HẾT khoảng chờ và cho trục giả chạy nhanh nhất: trục tới đích trong một vòng quét,
            // không chờ ổn định trước khi chụp, không chờ chân không/thổi, Flow giả trả ngay. Máy mô phỏng không rung, không
            // có khí, không có camera — mấy khoảng chờ đó chỉ là thời gian chết. Bài nào kiểm chính khoảng chờ thì tự đặt số.
            var simSys = Core.Data.McDataManager.SysData;
            simSys.BottomLampMs = simSys.TopLampMs = 0;
            simSys.BottomSettleMs = simSys.TopSettleMs = 0;
            simSys.VacuumSettleMs = 0;
            simSys.BlowTimeMs = 0;
            simSys.VacuumTimeoutMs = 50;      // cảm biến chân không giả đổi ngay khi bật van — chỉ hút hụt mới chờ hết số này
            FakeAxis.SpeedFactor = 1000;
            if (McControl.Vision is FakeVision simVision) simVision.ProcessTimeMs = 0;

            var spent = new List<KeyValuePair<string, double>>();
            var all = System.Diagnostics.Stopwatch.StartNew();

            foreach (var t in tests)
            {
                Log("[BẮT ĐẦU] " + t.Method.Name);
                FlushReport();
                var sw = System.Diagnostics.Stopwatch.StartNew();
                t();
                sw.Stop();
                spent.Add(new KeyValuePair<string, double>(t.Method.Name, sw.Elapsed.TotalSeconds));
                Log($"[XONG   ] {t.Method.Name} — {sw.Elapsed.TotalSeconds:0.0} s");
                FlushReport();
            }
            all.Stop();

            Log("");
            Log("── Thời gian: bài lâu nhất ──");
            foreach (var kv in spent.OrderByDescending(k => k.Value).Take(10))
                Log($"  {kv.Value,6:0.0} s  {(kv.Value / all.Elapsed.TotalSeconds),5:0.0%}  {kv.Key}");

            Log("");
            Log($"===== KẾT QUẢ: {_pass} đạt, {_fail} không đạt — {all.Elapsed.TotalSeconds:0} s =====");
            FlushReport();

            return _fail == 0 ? 0 : 1;
        }

        private static void FlushReport()
        {
            string path = Path.Combine(AppPaths.Root, "selftest.txt");
            try { File.WriteAllText(path, string.Join(Environment.NewLine, _report), new UTF8Encoding(true)); }
            catch { }
        }

        // ─────────────────────────────────────────────

        private static void TestStartupState()
        {
            Section("Khởi động");
            Check("Đã khởi tạo McControl", McControl.IsInitialized);
            Check("7 trục được tạo", McControl.Motion.AxisList.Count == 7);
            Check("Mọi trục đã kết nối", McControl.Motion.AllConnected);
            Check("Card I/O đã kết nối", McControl.Io.IsConnected);
            Check("Trạng thái ban đầu là CHƯA VỀ GỐC", McControl.Status.State == EMcState.NotHomed);
            Check("Chưa về gốc thì OrgOk = false", !McControl.Status.OrgOk);
            Check("Bảng alarm nạp được", AlarmCatalog.Loaded);
            Check("Mã 2105 tra ra đúng trục và loại lỗi",
                AlarmCatalog.Get(2105).Vi.Contains("Z1"),
                AlarmCatalog.Get(2105).Vi);
        }

        private static void TestZSafeInterlock()
        {
            Section("Chốt chặn Z_SAFE");

            var z1 = (FakeAxis)McControl.Motion[AxisId.Z1];
            var z2 = (FakeAxis)McControl.Motion[AxisId.Z2];

            WaitUntil(() => McControl.Motion.AllConnected, 2000);
            foreach (var a in McControl.Motion.AxisList) a.SetServoOnAsync(true).Wait();

            // Z1/Z2 không tự nhả phanh chỉ vì ServoOn — mô phỏng nghi thức thao tác tay (Manual →
            // SVO on → "Mở phanh") rồi đợi qua cửa sổ nhả phanh (McFunction.CanMove → BrakeReady)
            // trước khi test, không thì mọi CanMove(Z1/Z2) dưới đây bị chặn nhầm vì phanh, không
            // phải vì Z_SAFE (Tuấn yêu cầu, 2026-09-04).
            McControl.Func.RequestBrakeRelease(AxisId.Z1);
            McControl.Func.RequestBrakeRelease(AxisId.Z2);
            WaitUntil(() => McControl.Func.CanMove(AxisId.Z1, out _), 1000);

            // Z dương là chiều ĐI XUỐNG, "an toàn" là vị trí NHỎ — xem MotionConnect.ZSafeOk
            // (Tuấn xác nhận 2026-09-04). Hạ cả hai Z xuống dưới bằng jog(+1) rồi thử chạy trục
            // ngang.
            z1.Jog(1, 200); z2.Jog(1, 200);
            WaitUntil(() => z1.CurrentPos > McControl.Motion.ZSafeLimit + 1 &&
                            z2.CurrentPos > McControl.Motion.ZSafeLimit + 1, 4000);
            z1.Jog(0, 0); z2.Jog(0, 0);

            Check("Z đã hạ xuống thì ZSafeOk = false", !McControl.Motion.ZSafeOk,
                $"Z1={z1.CurrentPos:0.0} Z2={z2.CurrentPos:0.0}");

            string reason;
            bool canMoveX = McControl.Func.CanMove(AxisId.X, out reason);
            Check("Z đã hạ xuống thì X bị chặn", !canMoveX, reason);

            bool canMoveZ = McControl.Func.CanMove(AxisId.Z1, out _);
            Check("Z đã hạ xuống nhưng Z1 vẫn được phép chạy (để rút lên)", canMoveZ);

            // Rút một trục thôi — vẫn phải chặn, vì điều kiện là CẢ HAI.
            z1.Jog(-1, 200);
            WaitUntil(() => z1.CurrentPos < McControl.Motion.ZSafe - 1, 4000);
            z1.Jog(0, 0);
            Check("Chỉ rút Z1 lên thì X vẫn bị chặn", !McControl.Func.CanMove(AxisId.X, out _),
                $"Z1={z1.CurrentPos:0.0} Z2={z2.CurrentPos:0.0}");

            z2.Jog(-1, 200);
            WaitUntil(() => McControl.Motion.ZSafeOk, 4000);
            z2.Jog(0, 0);
            Check("Rút đủ cả hai Z lên thì ZSafeOk = true", McControl.Motion.ZSafeOk);
        }

        private static void TestOriginSequence()
        {
            Section("Chuỗi về gốc");

            var z1 = McControl.Motion[AxisId.Z1];
            var z2 = McControl.Motion[AxisId.Z2];

            // Chưa qua nghi thức thao tác tay (Manual → SVO on → "Mở phanh") thì Về gốc phải bị
            // PreCheck chặn ngay, KHÔNG được tự bật servo/nhả phanh hộ operator (Tuấn yêu cầu,
            // 2026-09-04). Ép ServoOff và ĐỢI vòng quét thật sự khoá lại phanh trước khi gọi
            // RequestOrigin() — không thì có thể đua với UpdateBrakeOutputs() và PreCheck đọc
            // phải cờ "đã nhả" còn sót lại từ TestZSafeInterlock ngay trong vòng quét đầu tiên.
            z1.SetServoOnAsync(false).Wait();
            z2.SetServoOnAsync(false).Wait();
            WaitUntil(() => !McControl.Func.BrakeReady(AxisId.Z1) && !McControl.Func.BrakeReady(AxisId.Z2), 500);

            McControl.RequestOrigin();
            // Status.State đã LÀ NotHomed từ trước khi gọi (chưa test nào Về gốc thành công) và
            // PreCheck thất bại gần như tức thời (toàn kiểm tra đồng bộ, không I/O) nên trạng
            // thái có thể lướt qua Homing NHANH HƠN chu kỳ 20ms của WaitUntil — không cố bắt
            // đúng lúc đang Homing, chỉ chờ KẾT QUẢ cuối cùng ổn định.
            bool blocked = WaitUntil(() => McControl.Status.Message != null && McControl.Status.Message.Contains("phanh")
                && McControl.Status.State == EMcState.NotHomed, 2000);
            Check("Chưa qua nghi thức tay (SVO on + Mở phanh Z1/Z2) thì Về gốc bị PreCheck chặn, không tự chạy",
                blocked && !McControl.Status.OrgOk,
                "State=" + McControl.Status.State + " Message=" + McControl.Status.Message);

            // Giờ mô phỏng đúng nghi thức: vào Manual, bật servo Z1/Z2, bấm "Mở phanh" — rồi mới
            // cho Về gốc chạy thật.
            z1.SetServoOnAsync(true).Wait();
            z2.SetServoOnAsync(true).Wait();
            McControl.Func.RequestBrakeRelease(AxisId.Z1);
            McControl.Func.RequestBrakeRelease(AxisId.Z2);
            WaitUntil(() => McControl.Func.CanMove(AxisId.Z1, out _) && McControl.Func.CanMove(AxisId.Z2, out _), 1000);

            McControl.RequestOrigin();
            WaitUntil(() => McControl.Status.State == EMcState.Homing, 2000);
            bool done = WaitUntil(() => McControl.Status.State != EMcState.Homing, 40000);

            Check("Chuỗi về gốc kết thúc trong thời gian cho phép", done);
            Check("Về gốc thành công", McControl.Status.OrgOk,
                "bước cuối: " + McControl.Status.OriginStep + " — " + McControl.Status.Message);
            Check("Cả 7 trục báo đã về gốc", McControl.Motion.AllHomed,
                string.Join(", ", McControl.Motion.AxisList.Select(a => $"{a.Name}={a.OrgOk}")));
            Check("Sau khi về gốc, trạng thái là SẴN SÀNG", McControl.Status.State == EMcState.Idle);
            Check("Về gốc xong Z vẫn ở trên cao độ an toàn", McControl.Motion.ZSafeOk);
            Check("Về gốc xong không phát sinh alarm", !AlarmManager.HasError,
                string.Join("; ", AlarmManager.ActiveAlarms.Select(a => a.Code + " " + a.MessageVi)));

            // Tay hút còn hàng — kể cả khi đang có yêu cầu lấy hàng ra — vẫn về gốc được (Tuấn bỏ chặn 2026-09-26).
            // Về gốc không đụng tới yêu cầu đó; nó được gỡ ở lần bấm chạy kế tiếp (TestAssyTrayFull).
            var runOrg = Core.Data.McDataManager.RunData;
            McControl.Func.SetVacuum(1, true);
            WaitUntil(() => McControl.Func.HasProduct(1), 500);
            runOrg.NozzleClearRequired = true;
            runOrg.NozzleClearReason = "selftest";
            McControl.RequestOrigin();
            WaitUntil(() => McControl.Status.State == EMcState.Homing, 2000);
            bool doneWithProduct = WaitUntil(() => McControl.Status.State != EMcState.Homing, 40000);
            Check("Tay hút #1 còn hàng và đang có yêu cầu lấy hàng ra → vẫn về gốc được",
                doneWithProduct && McControl.Status.OrgOk && McControl.Func.HasProduct(1),
                "bước cuối: " + McControl.Status.OriginStep + " — " + McControl.Status.Message);
            Check("Về gốc không đụng tới yêu cầu lấy hàng ra", runOrg.NozzleClearRequired);
            McControl.Func.SetVacuum(1, false);
            WaitUntil(() => !McControl.Func.HasProduct(1), 500);
            runOrg.NozzleClearRequired = false;
            runOrg.NozzleClearReason = null;
        }

        private static void TestEventAlarmsAlwaysResettable()
        {
            Section("Alarm sự kiện một lần (HomeTimeout...) không được kẹt RESET vĩnh viễn");

            // Bug thật gặp 2026-09-09: Z1 chạm hard limit lúc về gốc → alarm HomeTimeout (Error)
            // không có gì tự ClearCondition cho nó → HasError khoá SafeToMove CẢ MÁY vĩnh viễn →
            // Jog để rời hard limit cũng bị chặn theo → bế tắc, RESET không bao giờ thoát được.
            // Fix: AlarmManager.RaiseAxisEvent tự ClearCondition ngay sau Raise cho các mã "sự
            // kiện một lần" (MoveTimeout/SoftLimit/HomeTimeout/NotHomed/ServoOffUnexpected) —
            // bài này xác nhận đúng cơ chế đó, không phải chạy thật một lần Home thất bại (chờ
            // đủ HomeTimeoutMs sẽ quá chậm cho self-test).
            Check("Trước khi bơm lỗi: không alarm nào active", AlarmManager.ActiveCount == 0,
                string.Join("; ", AlarmManager.ActiveAlarms.Select(a => a.Code)));
            Check("Trước khi bơm lỗi: SafeToMove", McControl.Safety.SafeToMove, McControl.Safety.BlockReason);

            int code = AlarmCode.ForAxis(AxisId.Z1, AxisFault.HomeTimeout);
            AlarmManager.RaiseAxisEvent(AxisId.Z1, AxisFault.HomeTimeout, "test — mô phỏng về gốc thất bại");

            Check("Alarm HomeTimeout được ghi nhận active", AlarmManager.IsActive(code));
            Check("HomeTimeout là mức Error", AlarmManager.Get(code)?.Level == EAlarmLevel.Error);
            Check("RaiseAxisEvent tự ClearCondition ngay — không chờ điều kiện tiếp diễn nào khác",
                AlarmManager.Get(code)?.ConditionActive == false);

            WaitUntil(() => !McControl.Safety.SafeToMove, 500);
            Check("Còn alarm Error thì SafeToMove khoá cả máy (đúng thiết kế — chưa RESET thì chưa được chạy)",
                !McControl.Safety.SafeToMove);

            var stuck = AlarmManager.Reset();
            Check("RESET gỡ được HomeTimeout — không cần bơm lại hay chờ điều kiện nào thêm",
                !AlarmManager.IsActive(code) && stuck.All(a => a.Code != code),
                "còn kẹt: " + string.Join(", ", stuck.Select(a => a.Code)));

            WaitUntil(() => McControl.Safety.SafeToMove, 500);
            Check("Sau RESET, SafeToMove hồi phục — không còn bế tắc như bug 2026-09-09",
                McControl.Safety.SafeToMove, McControl.Safety.BlockReason);
        }

        private static void TestEmergencyStop()
        {
            Section("EMG và mạch an toàn");

            var io = (FakeIo)McControl.Io;

            io.SimSet(IoMap.EmgOk, false);          // NC: 0 = đã nhấn / đứt dây
            WaitUntil(() => McControl.Status.State == EMcState.Emg, 2000);

            Check("Nhấn EMG thì máy vào trạng thái EMG", McControl.Status.State == EMcState.Emg);
            Check("Có alarm 1100", AlarmManager.IsActive(AlarmCode.EmergencyStop));
            Check("EMG chặn mọi chuyển động", !McControl.Safety.SafeToMove, McControl.Safety.BlockReason);
            Check("Token huỷ đã được kích hoạt", McControl.Safety.AbortToken.IsCancellationRequested);

            // Bấm RESET khi EMG còn nhấn: KHÔNG được xoá gì (§7.4).
            var stuck = AlarmManager.Reset();
            Check("RESET khi EMG còn nhấn thì không xoá được alarm",
                stuck.Any(a => a.Code == AlarmCode.EmergencyStop), $"{stuck.Count} alarm còn lại");

            io.SimSet(IoMap.EmgOk, true);
            WaitUntil(() => !AlarmManager.ActiveAlarms.Any(a => a.Code == AlarmCode.EmergencyStop && a.ConditionActive), 2000);

            Check("Nhả EMG thì alarm hết nguyên nhân nhưng VẪN còn trong danh sách",
                AlarmManager.IsActive(AlarmCode.EmergencyStop));

            McControl.RequestReset();
            WaitUntil(() => !AlarmManager.IsActive(AlarmCode.EmergencyStop), 2000);
            Check("Nhả EMG rồi RESET thì alarm được xoá", !AlarmManager.IsActive(AlarmCode.EmergencyStop));
            Check("Không tự chạy lại sau EMG — vẫn phải ở trạng thái đứng yên",
                McControl.Status.State == EMcState.Idle || McControl.Status.State == EMcState.NotHomed,
                McControl.Status.State.ToString());
        }

        private static void TestLightCurtain()
        {
            Section("Rèm sáng");

            var io = (FakeIo)McControl.Io;
            var sys = Core.Data.McDataManager.SysData;

            // Mặc định HasLightCurtain = false (coi như chưa có rèm, chờ quyết định §11 #18).
            // Bật tạm để vẫn kiểm chứng được mạch đấu nối đã viết đúng — không phải vì máy có
            // rèm ngay bây giờ.
            bool savedHasCurtain = sys.HasLightCurtain;
            sys.HasLightCurtain = true;

            // Bài EMG ngay trước kết thúc bằng RESET; vòng quét có thể chưa kịp đánh giá lại nên SafeToMove còn false với lý do
            // cũ "còn alarm chưa xoá" — WaitUntil(!SafeToMove) bên dưới trả về ngay và đọc nhầm lý do đó (lỗi chập chờn 10/2026).
            Check("Trước khi cắt rèm: SafeToMove", WaitUntil(() => McControl.Safety.SafeToMove, 2000), McControl.Safety.BlockReason);

            io.SimSet(IoMap.LightCurtainOk, false);      // NC: 0 = bị cắt / đứt dây

            WaitUntil(() => !McControl.Safety.SafeToMove, 2000);

            // Tuấn 2026-10-01: rèm bị cắt chỉ dừng và chặn chuyển động, không alarm, không RESET — Auto tạm dừng như STOP
            // rồi bấm Start chạy tiếp (bài trong phần chạy Auto).
            Check("Cắt rèm sáng thì không phát alarm nào — không bắt RESET", AlarmManager.ActiveCount == 0,
                string.Join("; ", AlarmManager.ActiveAlarms.Select(a => a.Code)));
            Check("Rèm bị cắt thì chặn chuyển động", !McControl.Safety.SafeToMove, McControl.Safety.BlockReason);
            Check("Lý do chặn nói rõ là rèm sáng",
                McControl.Safety.BlockReason.IndexOf("rèm", StringComparison.OrdinalIgnoreCase) >= 0,
                McControl.Safety.BlockReason);

            // Rèm bị cắt phải chặn ở MỌI chế độ, kể cả lệnh về gốc do thợ chủ động bấm.
            McControl.RequestOrigin();
            Thread.Sleep(200);
            Check("Rèm bị cắt thì không cho chạy về gốc",
                McControl.Status.State != EMcState.Homing, McControl.Status.Message);

            io.SimSet(IoMap.LightCurtainOk, true);
            Check("Rèm thông trở lại thì chạy lại được, không cần RESET",
                WaitUntil(() => McControl.Safety.SafeToMove, 2000), McControl.Safety.BlockReason);

            sys.HasLightCurtain = savedHasCurtain;

            // Với cờ đã tắt (trạng thái mặc định thật sự đang chạy — khoá lại ở
            // TestHardwareAssumptions), cắt DI rèm sáng KHÔNG được
            // ảnh hưởng gì — đúng nghĩa "coi như không có rèm chắn bây giờ".
            io.SimSet(IoMap.LightCurtainOk, false);
            Thread.Sleep(100);
            Check("Tắt HasLightCurtain thì cắt DI rèm không chặn chuyển động",
                McControl.Safety.SafeToMove, McControl.Safety.BlockReason);
            Check("Tắt HasLightCurtain thì cắt DI rèm không phát alarm 1105",
                !AlarmManager.IsActive(AlarmCode.LightCurtainBroken));
            io.SimSet(IoMap.LightCurtainOk, true);
        }

        private static void TestStopCancelsManualSequences()
        {
            Section("STOP huỷ chuỗi chạy tay ở trang Vision");

            // Tuấn 2026-10-08: STOP trang Vision trước chỉ dừng trục, chuỗi hiệu chuẩn/hand-eye phát tiếp bước sau.
            var token = McControl.ManualToken;
            var running = System.Threading.Tasks.Task.Delay(Timeout.Infinite, token);
            var safetyBefore = McControl.Safety.AbortToken;
            McControl.RequestStop();
            Check("STOP huỷ chuỗi đang chạy trên ManualToken",
                WaitUntil(() => running.IsCanceled, 2000), running.Status.ToString());
            Check("STOP không huỷ token Safety — chu trình Auto đang tạm dừng không bị kéo theo",
                !safetyBefore.IsCancellationRequested);
            Check("Sau STOP, chuỗi mới lấy được token chưa huỷ", !McControl.ManualToken.IsCancellationRequested);
        }

        private static void TestVacuumHoldsThroughEmg()
        {
            Section("Van hút giữ nguyên khi EMG");

            var io = (FakeIo)McControl.Io;

            McControl.Func.SetVacuum(1, true);
            Thread.Sleep(50);
            Check("Bật hút thì cảm biến chân không báo có sản phẩm", McControl.Func.HasProduct(1));

            io.SimSet(IoMap.EmgOk, false);
            WaitUntil(() => McControl.Status.State == EMcState.Emg, 2000);

            // Đây là chỗ dễ sai nhất: một dòng "tắt hết ngõ ra cho an toàn" ở đường dừng khẩn là
            // đủ để sản phẩm rơi vào trong máy (§3.1 quy tắc 3, §11 #11).
            Check("EMG KHÔNG được tắt van hút", io.GetDo(IoMap.DoVacuum1));
            Check("Sản phẩm vẫn nằm trên tay hút sau EMG", McControl.Func.HasProduct(1));

            io.SimSet(IoMap.EmgOk, true);
            McControl.RequestReset();
            WaitUntil(() => !AlarmManager.IsActive(AlarmCode.EmergencyStop), 2000);

            McControl.Func.SetVacuum(1, false);
            Thread.Sleep(50);
            Check("Tắt hút thì cảm biến nhả", !McControl.Func.HasProduct(1));
        }

        private static void TestHardwareAssumptions()
        {
            Section("Ràng buộc phần cứng đã chốt");

            var sys = Core.Data.McDataManager.SysData;

            Check("Máy không dùng encoder tuyệt đối (§11 #14)", !sys.HasAbsoluteEncoder);

            // Không có encoder tuyệt đối thì bỏ qua Origin sau EMG là không được phép, và điều
            // đó phải được code bảo vệ chứ không chỉ ghi chú trong file cấu hình.
            sys.RequireHomeAfterEmg = false;
            Check("Không có encoder tuyệt đối thì không tắt được RequireHomeAfterEmg",
                sys.RequireHomeAfterEmg);
            sys.RequireHomeAfterEmg = true;

            Check("A1/A2 về gốc song song (§11 #10)", !sys.HomeRotateSequential);
            Check("Rèm sáng tạm tắt, chờ quyết định §11 #18", !sys.HasLightCurtain);

            foreach (string name in AxisId.ZAxes)
            {
                var cfg = sys.GetAxis(name);
                // Z dương là chiều ĐI XUỐNG, về gốc luôn đi LÊN (Tuấn xác nhận 2026-09-04) — dưới
                // quy ước đó, đi lên là chiều ÂM nên HomeDir = -1 (không phải +1).
                Check(name + " về gốc theo chiều đi lên (HomeDir = -1)", cfg.HomeDir == -1,
                    "HomeDir = " + cfg.HomeDir);
                Check(name + " về gốc bắt xung Z chứ không dừng ở cạnh DOG", cfg.HomeUseZPhase);
            }
        }

        private static void TestResetRules()
        {
            Section("Phân cấp alarm và lỗi gốc");

            AlarmManager.ForceClearAll();

            AlarmManager.Raise(AlarmCode.ForAxis(AxisId.X, AxisFault.Disconnected), AxisId.X);
            Thread.Sleep(20);
            AlarmManager.Raise(AlarmCode.ForAxis(AxisId.X, AxisFault.MoveTimeout), AxisId.X);
            Thread.Sleep(20);
            AlarmManager.Raise(AlarmCode.ProductDropped, "Auto");

            var root = AlarmManager.RootCause;
            Check("Alarm đầu tiên của cụm được đánh dấu là nguyên nhân gốc",
                root != null && root.Code == 2000 && root.IsRootCause,
                $"root = {root?.Code}, IsRootCause = {root?.IsRootCause}");

            int rootCount = new[] { 2000, 2002, 5002 }.Count(c => AlarmManager.Get(c)?.IsRootCause == true);
            Check("Alarm phát sau trong cùng cụm không bị đánh dấu là gốc", rootCount == 1,
                rootCount + " alarm gốc");

            Check("2000 là mức Fatal", AlarmManager.Get(2000)?.Level == EAlarmLevel.Fatal);
            Check("5002 là mức Error", AlarmManager.Get(5002)?.Level == EAlarmLevel.Error);

            // Hết nguyên nhân thì alarm mức nào cũng nằm lại chờ RESET, kể cả Warning (Tuấn chốt 2026-09-26 —
            // trước đó Warning tự biến mất).
            AlarmManager.Raise(AlarmCode.TrayEmpty, "Auto");
            Check("5020 là mức Warning", AlarmManager.Get(5020)?.Level == EAlarmLevel.Warning);
            Check("Warning còn nguyên nhân thì chưa tính là chờ RESET",
                AlarmManager.PendingReset == null || AlarmManager.PendingReset.Code != AlarmCode.TrayEmpty);
            AlarmManager.ClearCondition(AlarmCode.TrayEmpty);
            Check("Warning hết nguyên nhân vẫn nằm lại chờ RESET",
                AlarmManager.IsActive(AlarmCode.TrayEmpty) && AlarmManager.PendingReset != null);

            AlarmManager.ClearCondition(2000);
            var stillStuck = AlarmManager.Reset();
            Check("Chỉ alarm đã hết nguyên nhân mới bị RESET xoá",
                !AlarmManager.IsActive(2000) && AlarmManager.IsActive(2002),
                $"còn {stillStuck.Count} alarm");
            Check("RESET xoá cả Warning đã hết nguyên nhân", !AlarmManager.IsActive(AlarmCode.TrayEmpty));

            AlarmManager.ForceClearAll();
        }

        /// <summary>
        /// Lưới tray vẽ ở trang Main phải là <b>ảnh của cái tray</b>, không phải ảnh của đường máy đi.
        /// Trước 2026-09-24 lưới dựng theo thứ tự ghé ô nên tray quét zigzag bị vẽ lộn hàng chẵn (Tuấn phát hiện).
        /// </summary>
        private static void TestTrayGridZigzag()
        {
            Section("Lưới tray trên trang Main khi quét zigzag");

            foreach (var order in new[] { EScanOrder.RowMajor, EScanOrder.Serpentine })
            {
                var tray = new Core.Data.TrayState { ScanOrder = order };
                tray.Resize(5, 8);   // Resize mới cấp phát danh sách ô; ResetAll chỉ gán lại cái đã có
                tray.ResetAll(ECellState.Empty);
                string tag = order == EScanOrder.Serpentine ? "zigzag" : "một chiều";

                // IndexOf phải là nghịch đảo ĐÚNG của RowCol ở mọi ô — nếu không, lưới vẽ ra sẽ gán nhầm
                // trạng thái của ô này sang chỗ của ô khác.
                bool inverse = true, onto = true;
                var hit = new bool[tray.CellCount];
                for (int i = 0; i < tray.CellCount; i++)
                {
                    int r, c;
                    tray.RowCol(i, out r, out c);
                    int back = tray.IndexOf(r, c);
                    if (back != i) inverse = false;
                    if (back >= 0 && back < hit.Length) hit[back] = true;
                }
                foreach (bool h in hit) if (!h) onto = false;
                Check($"IndexOf là nghịch đảo đúng của RowCol ({tag})", inverse);
                Check($"Mỗi ô được dùng đúng một lần, không sót không trùng ({tag})", onto);

                // Lưới trang Main: ô thứ k trong danh sách phải nằm ở hàng k/Cols, cột k%Cols.
                var vm = new ViewModels.TrayGridViewModel("kiểm tra", "", null);
                vm.Sync(tray);
                var grid = vm.Groups[0].Cells;
                bool placed = true;
                for (int k = 0; k < grid.Count; k++)
                    if (grid[k].Row != k / tray.Cols || grid[k].Col != k % tray.Cols) placed = false;
                Check($"Lưới vẽ theo vị trí hình học, hàng trên xuống dưới trái sang phải ({tag})", placed);

                // Trạng thái phải bám đúng Ô, không bám vị trí trong danh sách. Đánh dấu ô ĐẦU TIÊN máy ghé
                // của hàng thứ hai: quét zigzag thì nó nằm ở CỘT CUỐI, không phải cột đầu.
                int mark = tray.IndexOf(1, tray.Cols - 1);
                tray.Set(mark, ECellState.Ok);
                vm.Sync(tray);
                var atRight = grid[1 * tray.Cols + (tray.Cols - 1)];
                var atLeft = grid[1 * tray.Cols];
                Check($"Trạng thái hiện đúng ô hình học, không lệch sang ô khác ({tag})",
                    atRight.State == ECellState.Ok && atLeft.State != ECellState.Ok,
                    $"phải {atRight.State}, trái {atLeft.State}");
            }

            // Bằng chứng trực tiếp cho cái Tuấn thấy: quét zigzag thì ô máy ghé NGAY SAU khi xong hàng đầu
            // nằm ở CỘT CUỐI của hàng hai — không phải cột đầu.
            var zig = new Core.Data.TrayState { ScanOrder = EScanOrder.Serpentine };
            zig.Resize(5, 8);
            zig.ResetAll(ECellState.Empty);
            int r1, c1;
            zig.RowCol(zig.Cols, out r1, out c1);
            // Giao ô theo HÌNH HỌC: tay #1 nằm lùi 63 mm so với tay #2, xa hơn một bước ô, nên điểm X xa
            // nhất cả vòng là chỗ đứng để tay #1 lên ô của nó — giao cho tay #1 ô GẦN thì gantry với ngắn
            // lại đúng một bước ô, trả hai lần mỗi vòng (Tuấn chốt 2026-09-24).
            Check("Hàng chạy xuôi: ô ghé trước là ô gần → tay #1 nhận ô ghé trước",
                Routine_Auto.Nozzle1TakesFirstCell(483.585, 508.572));
            Check("Hàng chạy NGƯỢC của zigzag: ô ghé trước là ô xa → tay #1 nhận ô ghé sau",
                !Routine_Auto.Nozzle1TakesFirstCell(508.572, 483.585));
            Check("Hai ô cùng X (cặp vắt qua hàng) thì theo thứ tự ghé, không đổi chỗ vô ích",
                Routine_Auto.Nozzle1TakesFirstCell(483.585, 483.585));

            // Gương của luật trên, soi ĐẦU KIA của vòng: đầu gần do ô HÚT quyết, vì tray đầu vào nằm cuối
            // bên trái. Số lấy từ lưới đã teach trên máy thật (Tuấn chốt 2026-09-24).
            Check("Cặp ô liền nhau trong hàng: giữ nguyên thứ tự ghé như trước",
                Routine_Auto.Nozzle1TakesFirstPickCell(90.715, 27.475, 114.724, 51.458));
            Check("Cặp VẮT QUA HAI HÀNG: tay #1 nhận ô cột đầu hàng sau, không phải ô ghé trước",
                !Routine_Auto.Nozzle1TakesFirstPickCell(186.751, 123.407, 90.587, 27.457));
            Check("Cặp ô trùng vị trí thì theo thứ tự ghé, không đổi chỗ vô ích",
                Routine_Auto.Nozzle1TakesFirstPickCell(90.715, 27.475, 90.715, 27.475));

            Check("Zigzag: ô ghé ngay sau hàng đầu nằm ở cột CUỐI của hàng hai",
                r1 == 1 && c1 == zig.Cols - 1, $"hàng {r1 + 1}, cột {c1 + 1}");
        }

        private static void TestTrayGridMath()
        {
            Section("Nội suy lưới tray từ 3 điểm");

            const string Z1 = Core.Infor.AxisId.Z1;

            var t = new Core.Data.Model.TrayLayout { Name = "Test", Rows = 4, Cols = 5 };
            t.GeoZ1.P00 = new Core.Data.Model.TeachPoint(10, 20);
            t.GeoZ1.P0C = new Core.Data.Model.TeachPoint(50, 20);   // 4 bước cột → u = (10, 0)
            t.GeoZ1.PR0 = new Core.Data.Model.TeachPoint(10, 50);   // 3 bước hàng → v = (0, 10)

            var p11 = t.CellPos(Z1, 1, 1);
            Check("P(1,1) = P00 + u + v", Near(p11.X, 20) && Near(p11.Y, 30), $"({p11.X:0.###}, {p11.Y:0.###})");

            var pLast = t.CellPos(Z1, 3, 4);
            Check("P(3,4) đúng ô góc đối diện", Near(pLast.X, 50) && Near(pLast.Y, 50), $"({pLast.X:0.###}, {pLast.Y:0.###})");

            // Tray đặt xoay: u và v là vector 2 chiều nên phải hấp thụ được góc nghiêng.
            var r = new Core.Data.Model.TrayLayout { Name = "Rot", Rows = 3, Cols = 3 };
            r.GeoZ1.P00 = new Core.Data.Model.TeachPoint(0, 0);
            r.GeoZ1.P0C = new Core.Data.Model.TeachPoint(20, 2);    // cột lệch 2 mm theo Y
            r.GeoZ1.PR0 = new Core.Data.Model.TeachPoint(-2, 20);   // hàng lệch −2 mm theo X
            var rc = r.CellPos(Z1, 1, 1);
            Check("Tray đặt xoay vẫn nội suy đúng", Near(rc.X, 9) && Near(rc.Y, 11), $"({rc.X:0.###}, {rc.Y:0.###})");

            // Điểm kiểm chứng bắt lỗi teach nhầm thứ tự P0C / PR0.
            t.GeoZ1.PRC_Verify = new Core.Data.Model.TeachPoint(50, 50);
            Check("Điểm kiểm chứng đúng thì đạt", t.VerifyGrid(Z1, out double e1), $"lệch {e1:0.###}");
            t.GeoZ1.PRC_Verify = new Core.Data.Model.TeachPoint(50, 53);
            Check("Điểm kiểm chứng lệch 3 mm thì báo lỗi", !t.VerifyGrid(Z1, out double e2), $"lệch {e2:0.###}");

            // Lưới 1 hàng / 1 cột không được chia cho 0.
            var one = new Core.Data.Model.TrayLayout { Name = "One", Rows = 1, Cols = 1 };
            one.GeoZ1.P00 = new Core.Data.Model.TeachPoint(7, 8);
            var op = one.CellPos(Z1, 0, 0);
            Check("Lưới 1×1 không chia cho 0", Near(op.X, 7) && Near(op.Y, 8), $"({op.X}, {op.Y})");

            // Đổi kích thước phải giữ lại offset của phần lưới trùng nhau.
            t.SetOffset(Z1, 0, 0, 0.11, 0.22);
            t.SetOffset(Z1, 2, 3, 0.33, 0.44);
            t.Resize(6, 7);
            var keep0 = t.GetOffset(Z1, 0, 0);
            var keep1 = t.GetOffset(Z1, 2, 3);
            Check("Đổi kích thước lưới vẫn giữ offset cũ",
                keep0 != null && Near(keep0.Dx, 0.11) && keep1 != null && Near(keep1.Dy, 0.44));

            // Bù đặt nằm chung ô nhớ với offset teach nhưng KHÁC nghĩa: offset teach sửa lưới nội suy,
            // bù đặt chỉ cộng vào đích gửi VisionMaster. Ghi cái này không được xoá cái kia, và bù đặt
            // tuyệt đối không được chui vào toạ độ lưới — điểm đặt do camera đo, cộng vào đây là bù hai lần.
            t.SetPlaceTrim(Z1, 0, 0, 0.05, -0.07, 0.9);
            var mix = t.GetOffset(Z1, 0, 0);
            Check("Một ô giữ được cả offset teach lẫn bù đặt",
                mix != null && Near(mix.Dx, 0.11) && Near(mix.Dy, 0.22) && Near(mix.PlaceDx, 0.05) && Near(mix.PlaceDa, 0.9));

            t.SetOffset(Z1, 0, 0, 0.11, 0.22);
            mix = t.GetOffset(Z1, 0, 0);
            Check("Teach lại offset KHÔNG xoá bù đặt của ô", mix != null && Near(mix.PlaceDy, -0.07));

            var trimRaw = t.CellPosRaw(Z1, 0, 0);
            var trimPos = t.CellPos(Z1, 0, 0);
            Check("Bù đặt KHÔNG chui vào toạ độ lưới (chỉ offset teach mới dịch lưới)",
                Near(trimPos.X - trimRaw.X, 0.11) && Near(trimPos.Y - trimRaw.Y, 0.22),
                $"({trimPos.X - trimRaw.X:0.###}, {trimPos.Y - trimRaw.Y:0.###})");

            t.Resize(4, 5);
            mix = t.GetOffset(Z1, 0, 0);
            Check("Đổi kích thước lưới giữ cả bù đặt", mix != null && Near(mix.PlaceDx, 0.05) && Near(mix.PlaceDa, 0.9));

            t.SetPlaceTrim(Z1, 0, 0, 0, 0, 0);
            mix = t.GetOffset(Z1, 0, 0);
            Check("Xoá bù đặt KHÔNG xoá offset teach", mix != null && Near(mix.Dx, 0.11) && !mix.HasPlaceTrim);

            // Bù chung: cộng cho mọi ô của đúng Tool, dồn với bù riêng của ô, không lây sang Tool kia.
            t.SetPlaceTrim(Z1, 0, 0, 0.05, -0.07, 0.9);
            t.SetPlaceTrimAll(Z1, 0.2, 0.1, -1.0);
            var sum00 = t.PlaceTrim(Z1, 0, 0);
            Check("Bù đặt tổng = bù chung + bù riêng của ô",
                Near(sum00.PlaceDx, 0.25) && Near(sum00.PlaceDy, 0.03) && Near(sum00.PlaceDa, -0.1),
                $"({sum00.PlaceDx:0.###}, {sum00.PlaceDy:0.###}, {sum00.PlaceDa:0.###})");
            var sum23 = t.PlaceTrim(Z1, 2, 3);
            Check("Ô không có bù riêng vẫn nhận đủ bù chung",
                Near(sum23.PlaceDx, 0.2) && Near(sum23.PlaceDy, 0.1) && Near(sum23.PlaceDa, -1.0));
            Check("Bù chung của Tool1 không lây sang Tool2", !t.PlaceTrim(Core.Infor.AxisId.Z2, 0, 0).HasPlaceTrim);

            // CellPosRaw — phần "lý thuyết" không cộng offset, trang Teaching dùng nó để tính
            // Set Offset (§9). Sai chỗ này thì mỗi lần Set Offset lại cộng dồn offset cũ một
            // lần nữa thay vì thay thế nó — offset sẽ trôi xa dần thay vì hội tụ về đúng.
            var g = new Core.Data.Model.TrayLayout { Name = "Raw", Rows = 3, Cols = 3 };
            g.GeoZ1.P00 = new Core.Data.Model.TeachPoint(0, 0);
            g.GeoZ1.P0C = new Core.Data.Model.TeachPoint(20, 0);
            g.GeoZ1.PR0 = new Core.Data.Model.TeachPoint(0, 20);
            g.SetOffset(Z1, 1, 1, 2.0, -3.0);

            var raw11 = g.CellPosRaw(Z1, 1, 1);
            var full11 = g.CellPos(Z1, 1, 1);
            Check("CellPosRaw KHÔNG cộng offset", Near(raw11.X, 10) && Near(raw11.Y, 10),
                $"({raw11.X:0.###}, {raw11.Y:0.###})");
            Check("CellPos = CellPosRaw + offset",
                Near(full11.X, raw11.X + 2.0) && Near(full11.Y, raw11.Y - 3.0),
                $"raw=({raw11.X:0.###},{raw11.Y:0.###}) full=({full11.X:0.###},{full11.Y:0.###})");

            var raw00 = g.CellPosRaw(Z1, 0, 0);
            Check("Ô chưa có offset thì CellPosRaw = CellPos",
                Near(raw00.X, g.CellPos(Z1, 0, 0).X) && Near(raw00.Y, g.CellPos(Z1, 0, 0).Y));

            // Mỗi Tool có lưới riêng, độc lập hoàn toàn — teach Z2 không được ảnh hưởng Z1 và
            // ngược lại (bắt lỗi kiểu "share cùng một TrayGeometry instance").
            const string Z2 = Core.Infor.AxisId.Z2;
            var dual = new Core.Data.Model.TrayLayout { Name = "Dual", Rows = 2, Cols = 2 };
            dual.GeoZ1.P00 = new Core.Data.Model.TeachPoint(0, 0);
            dual.GeoZ1.P0C = new Core.Data.Model.TeachPoint(10, 0);
            dual.GeoZ1.PR0 = new Core.Data.Model.TeachPoint(0, 10);
            dual.GeoZ2.P00 = new Core.Data.Model.TeachPoint(100, 100);
            dual.GeoZ2.P0C = new Core.Data.Model.TeachPoint(110, 100);
            dual.GeoZ2.PR0 = new Core.Data.Model.TeachPoint(100, 110);
            var z1cell = dual.CellPos(Z1, 1, 1);
            var z2cell = dual.CellPos(Z2, 1, 1);
            Check("Lưới Tool1/Tool2 độc lập, không lẫn vào nhau",
                Near(z1cell.X, 10) && Near(z1cell.Y, 10) && Near(z2cell.X, 110) && Near(z2cell.Y, 110),
                $"Z1=({z1cell.X:0.#},{z1cell.Y:0.#}) Z2=({z2cell.X:0.#},{z2cell.Y:0.#})");
            Check("IsTaughtFor chỉ true cho Tool đã teach",
                dual.IsTaughtFor(Z1) && dual.IsTaughtFor(Z2) && !new Core.Data.Model.TrayLayout().IsTaughtFor(Z1));

            // CreateSimulation() cố ý lệch lưới Tool2 khỏi Tool1 một lượng KHÔNG PHẢI shift X thuần tuý (xem
            // ModelData), nên gantry X thật sự dùng cho Tool2 phải khác Tool1 và khác đúng bằng lưới riêng của nó.
            var sim = Core.Data.Model.ModelData.CreateSimulation();
            var z1Pick = sim.InputTray.CellPos(Z1, 0, 0);
            var z2Pick = sim.InputTray.CellPos(Z2, 0, 0);
            Check("Tool1/Tool2 dùng lưới Input Tray độc lập, không suy ra bằng pitch cố định",
                !Near(z1Pick.X, z2Pick.X) && !Near(z1Pick.Y, z2Pick.Y),
                $"Z1=({z1Pick.X:0.#},{z1Pick.Y:0.#}) Z2=({z2Pick.X:0.#},{z2Pick.Y:0.#})");
        }

        private static void TestSinglePointAlignMath()
        {
            Section("Công thức Single Point Alignment của VisionMaster — PLAN.md §6.6");

            // Đây là công thức VisionMaster dùng để tính lượng bù (App không tính nữa, Tuấn chốt
            // 2026-09-12). Khoá lại bằng dữ liệu biết trước đáp số: nếu một ngày số của
            // VisionMaster lệch khỏi những khẳng định dưới đây thì lỗi nằm ở CẤU HÌNH solution
            // (đơn vị, dấu góc, gốc toạ độ), không phải ở chỗ khác trong app.
            double dx, dy, dTheta;

            // Sản phẩm lệch 1 mm theo X so với tâm xoay, phải xoay 90°: xoay làm nó dịch sang
            // (0, 1), nên lệnh phải lùi lại đúng lượng đó. Đây chính là hiệu ứng mà bỏ qua sẽ làm
            // sai số đặt tỉ lệ với độ lệch tâm và không bù được bằng một offset cố định.
            Core.Controller.SinglePointAlignMath.Solve(0, 0, 90, 1, 0, 0, out dx, out dy, out dTheta);
            Check("Xoay 90° thì lệch tâm 1 mm theo X quay thành 1 mm theo Y, lệnh trừ đi đúng lượng đó",
                Near(dTheta, 90) && Near(dx, 0) && Near(dy, -1),
                $"Δ=({dx:0.###}, {dy:0.###}), Δθ={dTheta:0.##}°");

            // Sản phẩm nằm đúng tâm xoay: xoay bao nhiêu cũng không phải dịch.
            Core.Controller.SinglePointAlignMath.Solve(10, 20, 45, 0, 0, 0, out dx, out dy, out dTheta);
            Check("Sản phẩm đúng tâm xoay thì lệnh = đúng toạ độ đích, không có lượng bù",
                Near(dx, 10) && Near(dy, 20) && Near(dTheta, 45));

            // Vòng xoay khép kín — chính là phép thử vòng xoay (§6.4b): xoay sản phẩm đi một góc
            // rồi hỏi cách quay lại, câu trả lời đúng là "xoay ngược lại, KHÔNG dịch gì cả".
            const double alpha = 10.0;
            double rad = alpha * Math.PI / 180.0;
            double p0x = 1.5, p0y = -0.8;
            double p1x = p0x * Math.Cos(rad) - p0y * Math.Sin(rad);
            double p1y = p0x * Math.Sin(rad) + p0y * Math.Cos(rad);

            Core.Controller.SinglePointAlignMath.Solve(p0x, p0y, 0, p1x, p1y, alpha, out dx, out dy, out dTheta);
            Check("Phép thử vòng xoay: xoay +10° rồi quay lại thì Δθ = −10° và lượng dịch = 0",
                Near(dTheta, -alpha) && Math.Abs(dx) < 1e-9 && Math.Abs(dy) < 1e-9,
                $"Δ=({dx:E2}, {dy:E2}), Δθ={dTheta:0.##}°");

            // Handedness sai (camera dưới nhìn từ dưới lên là ẢNH GƯƠNG) làm điểm quay một đằng
            // còn góc báo một nẻo — sai số dư của phép thử vòng xoay phải nổi lên, đó là cách duy
            // nhất bắt được lỗi này trước khi máy đặt lệch hàng loạt (§6.4b).
            double mirroredX = p0x * Math.Cos(-rad) - p0y * Math.Sin(-rad);
            double mirroredY = p0x * Math.Sin(-rad) + p0y * Math.Cos(-rad);
            Core.Controller.SinglePointAlignMath.Solve(p0x, p0y, 0, mirroredX, mirroredY, alpha, out dx, out dy, out dTheta);
            Check("Hệ toạ độ bị lật thì phép thử vòng xoay còn sai số dư khác 0 (bắt được lỗi)",
                Math.Sqrt(dx * dx + dy * dy) > 0.1,
                $"sai số dư = {Math.Sqrt(dx * dx + dy * dy):0.000} mm");

            // Góc phải chuẩn hoá về (−180, 180]: 170° và −170° cách nhau 20°, không phải 340°.
            Core.Controller.SinglePointAlignMath.Solve(0, 0, 170, 0, 0, -170, out dx, out dy, out dTheta);
            Check("Góc chuẩn hoá: đích 170°, đối tượng −170° → xoay −20° chứ không phải +340°",
                Near(dTheta, -20), $"{dTheta:0.##}°");
        }

        private static void TestModelManagement()
        {
            Section("Quản lý model trên đĩa (tạo / sao chép / xoá)");

            // Model đang nạp (SIM, chỉ có trong bộ nhớ) không được đụng tới — các test chạy sau
            // (TestAutoCycle...) cần nó nguyên vẹn. Lưu lại để phục hồi ở cuối.
            //
            // QUAN TRỌNG: LoadModel() có tác dụng phụ ghi RunData.RecentModelName. Quên phục hồi
            // giá trị này thì sau khi test xoá __SELFTEST_B__, running.json trên đĩa còn trỏ tới
            // một model không còn tồn tại — LẦN CHẠY APP KẾ TIẾP (kể cả GUI thật) sẽ tự phát
            // alarm 6001 "File model hỏng" ngay lúc khởi động, và alarm đó kéo AlarmManager.HasError
            // = true suốt phần còn lại của lần chạy, làm rất nhiều test không liên quan đỏ theo.
            // Đây chính là nguyên nhân của một đợt lỗi dây chuyền đã xảy ra khi viết test này.
            var savedModel = Core.Data.McDataManager.Model;
            bool savedLoaded = Core.Data.McDataManager.ModelLoaded;
            string savedRecentName = Core.Data.McDataManager.RunData.RecentModelName;

            const string nameA = "__SELFTEST_A__";
            const string nameB = "__SELFTEST_B__";
            Core.Data.McDataManager.DeleteModel(nameA);   // dọn rác của lần chạy trước lỡ dở
            Core.Data.McDataManager.DeleteModel(nameB);

            var model = new Core.Data.Model.ModelData { Name = nameA, Description = "model tạm của --selftest" };

            // Ba điểm dạy của cơ cấu kẹp: model mới phải là CHƯA teach, và toạ độ + cờ Taught phải sống qua vòng
            // lưu → nạp lại. Trước 25/09 self-test chưa có bài nào khẳng định một con số teach sống qua JSON, nên
            // một điểm rơi mất khi ghi đĩa sẽ không bài nào thấy.
            Check("Model mới: ba điểm cơ cấu kẹp đều CHƯA teach",
                !model.HandEyePick_Nozzle1.Taught && !model.HandEyePick_Nozzle2.Taught && !model.HandEyeCamPos.Taught);
            Check("Chưa teach thì bước xác định hand-eye từ chối, câu nói đúng điểm còn thiếu",
                model.HandEyeJigProblem(1) != null && model.HandEyeJigProblem(1).Contains("vị trí gắp") &&
                model.HandEyeJigProblem(2) != null, model.HandEyeJigProblem(1));
            model.HandEyePick_Nozzle1 = new Core.Data.Model.TeachPoint(123.456, 78.901);
            model.HandEyePick_Nozzle2 = new Core.Data.Model.TeachPoint(61.234, 79.012);
            model.HandEyeCamPos = new Core.Data.Model.TeachPoint(150.987, 90.123);
            Check("Teach đủ ba điểm thì không còn câu thiếu",
                model.HandEyeJigProblem(1) == null && model.HandEyeJigProblem(2) == null);

            Core.Data.McDataManager.UseModel(model, countsAsLoaded: true);
            bool savedOk = Core.Data.McDataManager.SaveModel(nameA);
            Check("Lưu model mới thành công", savedOk);
            Check("Model mới xuất hiện trong danh sách trên đĩa",
                Core.Data.McDataManager.ListModels().Contains(nameA));

            bool copyOk = Core.Data.McDataManager.CopyModel(nameA, nameB);
            Check("Sao chép model thành công", copyOk);
            Check("Bản sao xuất hiện trong danh sách", Core.Data.McDataManager.ListModels().Contains(nameB));

            bool loadCopyOk = Core.Data.McDataManager.LoadModel(nameB);
            Check("Nạp lại được bản sao", loadCopyOk);
            Check("Bản sao mang đúng tên mới, không phải tên nguồn",
                Core.Data.McDataManager.Model.Name == nameB, Core.Data.McDataManager.Model.Name);
            Check("Bản sao giữ nguyên nội dung (mô tả) của model nguồn",
                Core.Data.McDataManager.Model.Description == model.Description);

            var reloadedJig = Core.Data.McDataManager.Model;
            Check("Ba điểm cơ cấu kẹp sống qua vòng lưu → nạp lại: cả toạ độ lẫn cờ Taught",
                reloadedJig.HandEyePick_Nozzle1.Taught && reloadedJig.HandEyePick_Nozzle2.Taught &&
                reloadedJig.HandEyeCamPos.Taught &&
                Near(reloadedJig.HandEyePick_Nozzle1.X, 123.456) && Near(reloadedJig.HandEyePick_Nozzle1.Y, 78.901) &&
                Near(reloadedJig.HandEyePick_Nozzle2.X, 61.234) && Near(reloadedJig.HandEyeCamPos.X, 150.987) &&
                Near(reloadedJig.HandEyeCamPos.Y, 90.123),
                $"gắp #1 ({reloadedJig.HandEyePick_Nozzle1.X:0.###}, {reloadedJig.HandEyePick_Nozzle1.Y:0.###}), " +
                $"gắp #2 X {reloadedJig.HandEyePick_Nozzle2.X:0.###}, soi ({reloadedJig.HandEyeCamPos.X:0.###}, {reloadedJig.HandEyeCamPos.Y:0.###})");
            Check("Hai điểm gắp đọc lại vẫn KHÁC nhau — không bị gộp thành một đối tượng khi ghi/đọc JSON",
                !Near(reloadedJig.HandEyePick_Nozzle1.X, reloadedJig.HandEyePick_Nozzle2.X));

            bool deleteAOk = Core.Data.McDataManager.DeleteModel(nameA);
            Check("Xoá model nguồn thành công", deleteAOk);
            Check("Model nguồn hết trong danh sách sau khi xoá",
                !Core.Data.McDataManager.ListModels().Contains(nameA));
            Check("Xoá model nguồn KHÔNG ảnh hưởng bản sao đã tách ra",
                Core.Data.McDataManager.ListModels().Contains(nameB));

            Core.Data.McDataManager.DeleteModel(nameB);
            Check("Dọn sạch: cả hai model tạm đều hết trên đĩa",
                !Core.Data.McDataManager.ListModels().Contains(nameA) &&
                !Core.Data.McDataManager.ListModels().Contains(nameB));

            Core.Data.McDataManager.UseModel(savedModel, savedLoaded);
            Core.Data.McDataManager.RunData.RecentModelName = savedRecentName;
            Check("Phục hồi đúng RecentModelName — không để lại tham chiếu treo cho lần chạy sau",
                Core.Data.McDataManager.RunData.RecentModelName == savedRecentName);
        }

        private static void TestAxisScale()
        {
            Section("Quy đổi đơn vị trục (mm ⇄ xung, mm/s ⇄ rpm)");

            // Vít me bước 10 mm, encoder 10000 xung/vòng → 1000 xung/mm.
            var cfg = new Core.Communication.AxisConfig
            {
                Name = "TEST", Unit = "mm",
                PulsePerUnit = 1000, UnitPerRev = 10, MaxRpm = 3000
            };
            var s = new Core.Communication.AxisScale(cfg);

            Check("20 mm → 20000 xung", s.ToPulse(20) == 20000, s.ToPulse(20).ToString());
            Check("−20 mm → −20000 xung", s.ToPulse(-20) == -20000);
            Check("20000 xung → 20 mm", Near(s.ToUnit(20000), 20));
            Check("Đi rồi về là bằng chính nó", Near(s.ToUnit(s.ToPulse(37.123)), 37.123));

            // Đúng ví dụ đã chốt: người vận hành gõ 12.345 mm ở ô nhập trên trang Manual, số
            // NGUYÊN gửi xuống Modbus (ServoService.MoveAbsolute) phải là 12345 xung. Đây là
            // toàn bộ điều mà PanasonicAxis.MoveAbsAsync làm trước khi gọi xuống ServoControl —
            // khoá lại bằng đúng ví dụ người dùng đưa ra để không ai lỡ tay sửa mất.
            Check("12.345 mm → 12345 xung (ví dụ đã chốt)", s.ToPulse(12.345) == 12345,
                s.ToPulse(12.345).ToString());

            // Làm tròn phải ra số gần nhất, không cắt cụt: cắt cụt tích luỹ thành sai vị trí.
            Check("0.0006 mm làm tròn thành 1 xung, không cụt về 0", s.ToPulse(0.0006) == 1,
                s.ToPulse(0.0006).ToString());
            Check("−0.0006 mm làm tròn thành −1 xung", s.ToPulse(-0.0006) == -1);

            // Tràn int biến lệnh đi xa thành lệnh đi ngược hết cỡ — phải cắt ở biên.
            Check("Giá trị quá lớn bị cắt ở biên int, không tràn thành số âm",
                s.ToPulse(1e12) == int.MaxValue && s.ToPulse(-1e12) == int.MinValue);

            // 100 mm/s với bước 10 mm/vòng = 10 vòng/s = 600 rpm.
            Check("100 mm/s → 600 rpm", s.ToRpm(100) == 600, s.ToRpm(100).ToString());
            Check("Tốc độ âm lấy theo độ lớn", s.ToRpm(-100) == 600);
            Check("Vượt MaxRpm thì kẹp lại", s.ToRpm(10000) == 3000, s.ToRpm(10000).ToString());

            // Kẹp sàn ở 1: rpm = 0 bị driver hiểu là đứng yên, lệnh treo tới khi timeout.
            Check("Tốc độ rất nhỏ vẫn ra ít nhất 1 rpm, không thành 0",
                s.ToRpm(0.0001) == 1, s.ToRpm(0.0001).ToString());
            Check("600 rpm → 100 mm/s", Near(s.FromRpm(600), 100));

            // Fastech nhận xung/giây chứ không phải rpm.
            Check("100 mm/s → 100000 xung/s", s.ToPulsePerSec(100) == 100000u,
                s.ToPulsePerSec(100).ToString());
            Check("Tốc độ rất nhỏ vẫn ra ít nhất 1 xung/s", s.ToPulsePerSec(0) == 1u);

            // Trục xoay: 18000 xung/vòng ĐỘNG CƠ qua hộp số 1/2 → 36000 xung cho một vòng đầu hút.
            var rot = new Core.Communication.AxisConfig
            {
                Name = "A1", Unit = "deg", WrapDegrees = true,
                PulsePerUnit = Core.Data.SystemData.RotaryPulsePerDeg,
                UnitPerRev = Core.Data.SystemData.RotaryDegPerMotorRev, MaxRpm = 3000
            };
            var r = new Core.Communication.AxisScale(rot);
            Check("Trục xoay: 100 xung/độ", Near(r.PulsePerUnit, 36000.0 / 360.0),
                r.PulsePerUnit.ToString());
            Check("Trục xoay: 360° → 36000 xung", r.ToPulse(360) == 36000, r.ToPulse(360).ToString());
            Check("Trục xoay: 90° → 9000 xung", r.ToPulse(90) == 9000, r.ToPulse(90).ToString());

            // PulsePerRev phải TRÒN: mọi phép chọn vòng gần nhất cộng trừ bội số của nó, lệch
            // một chút là sau vài chục vòng thành lệch góc thật.
            Check("Một vòng đầu hút = đúng 36000 xung", r.PulsePerRev == 36000, r.PulsePerRev.ToString());

            // 180°/s đầu hút = 1 vòng động cơ/giây = 18000 xung/s.
            Check("Trục xoay: 180°/s → 18000 xung/s", r.ToPulsePerSec(180) == 18000u,
                r.ToPulsePerSec(180).ToString());
            Check("Trục xoay: 180°/s → 60 rpm động cơ", r.ToRpm(180) == 60, r.ToRpm(180).ToString());

            // Cấu hình hỏng (chia cho 0) không được làm sập vòng điều khiển.
            var bad = new Core.Communication.AxisScale(
                new Core.Communication.AxisConfig { Name = "BAD", PulsePerUnit = 0, UnitPerRev = 0 });
            Check("PulsePerUnit = 0 không gây chia cho 0", bad.ToPulse(5) == 5 && Near(bad.ToUnit(5), 5));

            // PulsePerUnit là HẰNG SỐ ĐÃ CHỐT (electronic gear cấu hình trong driver), không
            // phải số đo được — nên default trong SystemData phải khớp đúng nó. Bắt lỗi ở đây
            // nếu sau này ai sửa nhầm một số trong DefaultAxis mà không cập nhật driver theo.
            var sys = Core.Data.SystemData.CreateDefault();
            foreach (string name in new[] { AxisId.X, AxisId.Z1, AxisId.Z2, AxisId.Y1, AxisId.Y2 })
                Check(name + " mặc định đúng 1000 xung/mm", Near(sys.GetAxis(name).PulsePerUnit, 1000.0),
                    sys.GetAxis(name).PulsePerUnit.ToString());
            foreach (string name in new[] { AxisId.A1, AxisId.A2 })
                Check(name + " mặc định đúng 36000 xung/360° (18000 xung/vòng động cơ, hộp số 1/2)",
                    Near(sys.GetAxis(name).PulsePerUnit, 36000.0 / 360.0),
                    sys.GetAxis(name).PulsePerUnit.ToString());

            // Số hiệu block là quy ước đã chốt của ServoControl.ServoAxisConfig (Block0=ORG,
            // Block1=MoveAbs, Block2/3=Jog+/−), không phải số tuỳ chọn. Sai một số ở đây thì
            // StartHome có thể vô tình kích block Jog và trục chạy không điểm dừng.
            foreach (string name in new[] { AxisId.X, AxisId.Z1, AxisId.Z2, AxisId.Y1, AxisId.Y2 })
            {
                var a = sys.GetAxis(name);
                Check(name + " Block0 = về gốc", a.HomeBlock == 0, a.HomeBlock.ToString());
                Check(name + " Block1 = MoveAbs", a.AbsoluteBlock == 1, a.AbsoluteBlock.ToString());
                Check(name + " Block2 = Jog dương", a.JogPositiveBlock == 2, a.JogPositiveBlock.ToString());
                Check(name + " Block3 = Jog âm", a.JogNegativeBlock == 3, a.JogNegativeBlock.ToString());
            }
        }

        /// <summary>
        /// Lệnh chạy chỉ gửi xuống driver thứ driver chưa có (Tuấn 2026-09-25: ghi đi ghi lại các thanh ghi
        /// làm tăng độ trễ). Mỗi giao dịch trước STB là thời gian chết giữa "ra lệnh" và "trục chạy".
        ///
        /// <para>Mỗi mục kiểm cả hai phía: SỐ giao dịch trước STB (đã bớt ghi chưa) và bảng thanh ghi driver
        /// chụp ở cạnh STB (bớt ghi rồi driver có còn chạy đúng tốc độ, ramp, block, đích không). Chỉ đếm thì
        /// một bộ nhớ đệm nói dối — tưởng driver đang giữ một số mà thật ra không — vẫn đạt.</para>
        /// </summary>
        /// <summary>
        /// Luật xong / chờ / gửi lại / bỏ của một lệnh chạy servo Panasonic (Tuấn 2026-10-01, theo AutoScrews): không chờ
        /// INP; gửi lại khi driver không nhận lệnh — nhưng không bao giờ gửi chồng lúc trục đang chạy hay đang về đích.
        /// </summary>
        private static void TestMoveCompletionRule()
        {
            Section("Lệnh chạy servo: xong khi vào dung sai + hết BUSY, gửi lại khi driver không nhận lệnh");

            Func<bool, bool, int, bool, int, bool, Core.Communication.PanasonicAxis.EMoveWait> j = Core.Communication.PanasonicAxis.JudgeMove;
            var Done = Core.Communication.PanasonicAxis.EMoveWait.Done;
            var Wait = Core.Communication.PanasonicAxis.EMoveWait.Wait;
            var Resend = Core.Communication.PanasonicAxis.EMoveWait.Resend;
            var Lost = Core.Communication.PanasonicAxis.EMoveWait.Lost;
            int max = Core.Communication.PanasonicAxis.MaxMoveResends;

            Check("Vào dung sai, hết BUSY là xong — không chờ INP", j(true, false, 5, true, 0, true) == Done);
            Check("Vào dung sai mà còn BUSY thì chờ", j(true, true, 5, true, 0, true) == Wait);
            Check("Đang BUSY thì chờ, không gửi lại", j(false, true, 10, false, 0, true) == Wait);
            Check("Chưa đủ hai lượt đọc sau lúc gửi thì chờ — số cũ là của trước lệnh", j(false, false, 1, false, 0, true) == Wait);
            Check("Hết BUSY, chưa tới mà trục đã nhúc nhích (đang về đích) thì chờ, không gửi lại", j(false, false, 5, true, 0, true) == Wait);
            Check("Hai lượt đọc mới: không BUSY, chưa tới, chưa nhúc nhích → gửi lại", j(false, false, 2, false, 0, true) == Resend);
            Check("Gửi lại tối đa " + max + " lần rồi báo lỗi ngay, không đợi hết 30 s",
                j(false, false, 2, false, max - 1, true) == Resend && j(false, false, 2, false, max, true) == Lost);
            // 15/10: thư viện nay gửi cả lệnh 0.01 mm — nằm gọn trong dung sai "đã tới" 0.05 mm.
            Check("Lệnh nhỏ trong dung sai đã xuống driver: chưa đủ hai lượt đọc thì CHỜ, không coi là xong lúc trục chưa nhích",
                j(true, false, 0, false, 0, true) == Wait && j(true, false, 1, false, 0, true) == Wait);
            Check("…đủ hai lượt đọc, hết BUSY thì xong", j(true, false, 2, false, 0, true) == Done);
            Check("Thư viện không gửi (đã ở đích): xong ngay, không tốn thêm lượt đọc", j(true, false, 0, false, 0, false) == Done);
        }

        private static void TestServoModbusWrites()
        {
            Section("Lệnh Modbus xuống driver A6SF — không ghi lại thứ driver đang giữ (ServoService)");

            var port = new FakeA6sfPort();
            var servo = new ServoControl.Services.ServoService();
            string lastLog = "";
            servo.OnLog += s => lastLog = s;

            // Đúng bảng block máy này: block ABS đọc ô tốc độ 0, Jog đọc ô 1, cả hai chung ô acc/dec 0.
            // Ramp 300 ms ở 1800 rpm = Z1 đang cài trên máy (Auto cao 300 mm/s, 10 mm/vòng).
            var ax = new ServoControl.Models.ServoAxisConfig
            {
                Name = "Z1", UnitId = 1, AbsoluteVelocityIndex = 0, VelocityIndex = 1,
                AbsoluteAccelMs = 300, AbsoluteDecelMs = 300, AbsoluteRampRpm = 1800
            };
            var cfg = new ServoControl.Models.ServoConfig();
            cfg.Axes.Add(ax);
            Check("Nối ServoService vào driver giả", servo.Connect(cfg, port));
            var st = servo.GetAxisState(1);
            st.SON = true;                 // CanMove
            st.ActualPosition = -999999;   // xa mọi đích — MoveAbsolute không thoát sớm vì "đã ở đó"

            const ushort Vel0 = 0x4600, Vel1 = 0x4601, Acc0 = 0x4610, Dec0 = 0x4620, Pos = 0x4806, Blk = 0x4414;

            // Chạy một lệnh trên luồng nền (ServoService không ConfigureAwait, gọi chặn từ luồng UI sẽ treo)
            // rồi trả về các giao dịch TRƯỚC khi STB bật.
            Func<Func<System.Threading.Tasks.Task>, List<FakeA6sfPort.Tx>> run = cmd =>
            {
                int from = port.Transactions.Count;
                System.Threading.Tasks.Task.Run(cmd).GetAwaiter().GetResult();
                return port.Transactions.Skip(from)
                    .TakeWhile(t => !(t.Fc == 0x05 && t.Address == FakeA6sfPort.Stb)).ToList();
            };
            Func<Dictionary<ushort, ushort>> last = () => port.Starts[port.Starts.Count - 1];
            Func<Dictionary<ushort, ushort>, ushort, ushort> at = (s, a) => { ushort v; return s.TryGetValue(a, out v) ? v : (ushort)0; };
            Func<Dictionary<ushort, ushort>, int> posOf = s => (int)(at(s, Pos) | (uint)at(s, (ushort)(Pos + 1)) << 16);
            Func<List<FakeA6sfPort.Tx>, string> show = txs => txs.Count + ": " + string.Join(" · ", txs);

            // Z hạ hai chặng rồi nâng hai chặng: cao → thấp → thấp → cao, đúng nhịp máy chạy.
            var tx = run(() => servo.MoveAbsolute(1, 44770, 1800));
            var s0 = last();
            Check("Lệnh đầu: driver chạy block ABS 1800 rpm, ramp 500 (= 300 ms ở 1800 rpm), tới 44770",
                port.Starts.Count == 1 && at(s0, Blk) == 1 && at(s0, Vel0) == 1800 && at(s0, Acc0) == 500
                && at(s0, Dec0) == 500 && posOf(s0) == 44770, lastLog);
            Check("Lệnh đầu ghi đủ tốc độ, acc, dec, vị trí, số block — 5 giao dịch trước STB", tx.Count == 5, show(tx));
            Check("Vị trí 32 bit đi trong MỘT lần ghi nhiều thanh ghi (0x10), không tách hai lần 0x06",
                tx.Count(t => t.Fc == 0x10 && t.Address == Pos && t.Values.Length == 2) == 1
                && !tx.Any(t => t.Fc == 0x06 && (t.Address == Pos || t.Address == Pos + 1)), show(tx));

            tx = run(() => servo.MoveAbsolute(1, 55150, 36));
            var s1 = last();
            Check("Chặng chậm 36 rpm: ramp GIỮ 500 — cùng gia tốc chặng nhanh, không nhân lại theo tốc độ từng lệnh",
                at(s1, Vel0) == 36 && at(s1, Acc0) == 500 && at(s1, Dec0) == 500 && at(s1, Blk) == 1
                && posOf(s1) == 55150, lastLog);
            Check("Đổi tốc độ chỉ ghi tốc độ + vị trí — 2 giao dịch trước STB",
                tx.Count == 2 && tx.Any(t => t.Address == Vel0) && tx.Any(t => t.Address == Pos), show(tx));

            tx = run(() => servo.MoveAbsolute(1, 44770, 36));
            Check("Cùng tốc độ lệnh trước: chỉ còn vị trí — 1 giao dịch trước STB",
                tx.Count == 1 && tx[0].Fc == 0x10, show(tx));
            Check("…và driver vẫn chạy block ABS 36 rpm tới 44770",
                at(last(), Blk) == 1 && at(last(), Vel0) == 36 && posOf(last()) == 44770);

            // Jog ghi ramp của nó vào CHÍNH ô acc/dec 0 mà block ABS đọc.
            run(() => servo.JogPositive(1, 60));
            var sj = last();
            Check("Jog+ chạy block Jog với ramp riêng (JogAccelMs 100) ở ô tốc độ 1",
                at(sj, Blk) == 2 && at(sj, Acc0) == 100 && at(sj, Dec0) == 100 && at(sj, Vel1) == 60, lastLog);
            System.Threading.Tasks.Task.Run(() => servo.DecelerateStop(1)).GetAwaiter().GetResult();
            tx = run(() => servo.MoveAbsolute(1, 20000, 36));
            var s2 = last();
            Check("Sau Jog, lệnh ABS ghi lại ramp ABS và số block — không chạy nhầm ramp của Jog",
                at(s2, Acc0) == 500 && at(s2, Dec0) == 500 && at(s2, Blk) == 1 && at(s2, Vel0) == 36
                && posOf(s2) == 20000, show(tx));
            Check("…mà không ghi lại ô tốc độ ABS Jog không đụng tới — 4 giao dịch",
                tx.Count == 4 && !tx.Any(t => t.Address == Vel0), show(tx));

            // Driver bật lại nguồn: bảng block trong RAM về 0, App phải về gốc lại.
            port.PowerCycle();
            run(() => servo.StartHome(1));
            Check("Về gốc kích block 0", at(last(), Blk) == 0);
            tx = run(() => servo.MoveAbsolute(1, 30000, 36));
            var s3 = last();
            Check("Driver bật lại nguồn rồi về gốc: lệnh ABS đầu ghi lại đủ, chạy đúng 36 rpm, ramp 500, block ABS",
                at(s3, Vel0) == 36 && at(s3, Acc0) == 500 && at(s3, Dec0) == 500 && at(s3, Blk) == 1
                && posOf(s3) == 30000, show(tx));

            int starts = port.Starts.Count;
            port.FailNext = 1;
            run(() => servo.MoveAbsolute(1, 40000, 1800));
            Check("Driver trả lỗi giữa lệnh: không kích STB, có báo lỗi",
                port.Starts.Count == starts && lastLog.Contains("MoveAbsolute error"), lastLog);
            tx = run(() => servo.MoveAbsolute(1, 40000, 1800));
            var s4 = last();
            Check("Lệnh sau lỗi ghi lại đủ 5 thứ và chạy đúng — không tin những gì tưởng driver đang giữ",
                tx.Count == 5 && at(s4, Vel0) == 1800 && at(s4, Acc0) == 500 && at(s4, Blk) == 1
                && posOf(s4) == 40000, show(tx));

            run(() => servo.MoveAbsolute(1, -1234, 1800));
            Check("Vị trí âm −1234 xung tới driver nguyên vẹn qua hai word", posOf(last()) == -1234,
                posOf(last()).ToString());

            // Dự án khác dùng chung ServoControl để AbsoluteRampRpm = 0: thanh ghi ramp phải y như trước.
            ax.AbsoluteRampRpm = 0;
            run(() => servo.MoveAbsolute(1, 10000, 1800));
            Check("Chế độ cũ (AbsoluteRampRpm = 0): 1800 rpm ra ramp 500 như trước", at(last(), Acc0) == 500);
            tx = run(() => servo.MoveAbsolute(1, 11000, 36));
            Check("Chế độ cũ: 36 rpm vẫn nhân lại theo tốc độ — 300×3000/36 chặn ở 10000, đúng số trước đây",
                at(last(), Acc0) == 10000 && at(last(), Dec0) == 10000, show(tx));
            tx = run(() => servo.MoveAbsolute(1, 12000, 36));
            Check("Chế độ cũ cũng bớt ghi: cùng tốc độ lệnh trước chỉ còn vị trí", tx.Count == 1, show(tx));

            // 15/10: Y1 đứng 46.470 (tay #2 vừa hút), tay #1 cần 46.520 — lệch ĐÚNG 50 xung = PositionTolerance. Thư viện bỏ
            // qua lệnh vì "đã tới", App so bằng mm thì 0.0500000004 > 0.05 là chưa tới → gửi lại 3 lần → 2302. Nay thư viện chỉ
            // bỏ qua trong SkipMoveTolerance (App đặt 5 xung) — Tuấn: "nhận lệnh chính xác 0.01".
            st.ActualPosition = 46470;
            Func<int, bool> sends = p => System.Threading.Tasks.Task.Run(() => servo.MoveAbsolute(1, p, 36)).GetAwaiter().GetResult();
            Check("SkipMoveTolerance chưa đặt: vẫn bỏ qua trong " + cfg.PositionTolerance + " xung như cũ — dự án khác dùng chung thư viện không đổi",
                !sends(46520) && sends(46521), lastLog);

            var axCfg = new Core.Communication.AxisConfig { PulsePerUnit = 1000.0, UnitPerRev = 10.0, PositionTolerancePulse = 50 };
            var scale = new Core.Communication.AxisScale(axCfg);
            cfg.SkipMoveTolerance = Core.Communication.PanasonicAxis.LibrarySkipTolerance(axCfg);
            Check("App đặt ngưỡng bỏ qua 5 xung (0.005 mm) dù dung sai \"đã tới\" là 50", cfg.SkipMoveTolerance == 5,
                cfg.SkipMoveTolerance.ToString());

            int startsBefore = port.Starts.Count;
            bool sentY1 = sends(scale.ToPulse(46.520));
            Check("Ca máy thật: Y1 ở 46.470 → 46.520 (0.05 mm): lệnh xuống driver, chạy tới 46520",
                sentY1 && port.Starts.Count == startsBefore + 1 && posOf(last()) == 46520, lastLog);
            Check("Lệnh 0.01 mm (10 xung) xuống driver, cả hai chiều",
                sends(scale.ToPulse(46.480)) && posOf(last()) == 46480 && sends(scale.ToPulse(46.460)) && posOf(last()) == 46460);
            Check("Lệnh 0.006 mm (6 xung) vẫn xuống driver", sends(46476));
            Check("Trong 5 xung (0.005 mm) là đã ở đích — không gửi gì", !sends(46475) && !sends(46465) && !sends(46470));

            // Thư viện bỏ lệnh ⇒ App phải thấy đã tới; ngược lại App sẽ gửi lại lệnh mà thư viện cứ bỏ → 2302 như 15/10.
            bool agree = true;
            string why = "";
            foreach (int d in new[] { 0, 1, 4, 5, 6, 10, 49, 50, 51, -5, -6, -50, -51 })
            {
                bool skipped = !sends(46470 + d);
                bool there = Core.Communication.PanasonicAxis.WithinPulses(46470, 46470 + d, axCfg.PositionTolerancePulse);
                if (skipped && !there) { agree = false; why += " Δ" + d; }
            }
            Check("Thư viện bỏ qua lệnh nào thì App cũng coi lệnh đó là đã tới", agree, why);
            var wide = new Core.Communication.AxisConfig { PositionTolerancePulse = 50, SkipMoveTolerancePulse = 80 };
            var tight = new Core.Communication.AxisConfig { PositionTolerancePulse = 3, SkipMoveTolerancePulse = 5 };
            Check("Ngưỡng bỏ qua không bao giờ vượt dung sai \"đã tới\" (đặt 80/50 → 50, 5/3 → 3)",
                Core.Communication.PanasonicAxis.LibrarySkipTolerance(wide) == 50
                && Core.Communication.PanasonicAxis.LibrarySkipTolerance(tight) == 3);
            Check("App so \"đã tới\" bằng xung: 46.470 so với 46.520 là trong 50 xung (phép so mm cũ ra 0.0500000004 > 0.05)",
                Core.Communication.PanasonicAxis.WithinPulses(scale.ToPulse(46.470), scale.ToPulse(46.520), 50)
                && Math.Abs(46.470 - 46.520) > scale.ToUnit(50), Math.Abs(46.470 - 46.520).ToString("R"));

            servo.Dispose();
        }

        private static void TestMoveIncrement()
        {
            Section("Inching / Move Increment (McFunction.MoveIncAsync) — nút Move −/Move + ở trang Manual");

            if (!McControl.Status.OrgOk)
            {
                McControl.RequestOrigin();
                WaitUntil(() => McControl.Status.State == EMcState.Homing, 2000);
                WaitUntil(() => McControl.Status.State != EMcState.Homing, 40000);
            }
            Check("Đã về gốc trước khi kiểm Inching", McControl.Status.OrgOk);

            var x = McControl.Motion[AxisId.X];
            double start = x.CurrentPos;
            double speed = x.Config.MoveSpeed;

            bool okPlus = McControl.Func.MoveIncAsync(AxisId.X, 5.0, speed, CancellationToken.None)
                .GetAwaiter().GetResult();
            Check("Inch +5 chạy xong", okPlus);
            Check("Inch +5 dừng cách vị trí CŨ đúng 5mm — tương đối, không phải toạ độ tuyệt đối 5",
                Near(x.CurrentPos, start + 5.0), $"start={start:0.###} now={x.CurrentPos:0.###}");

            bool okMinus = McControl.Func.MoveIncAsync(AxisId.X, -5.0, speed, CancellationToken.None)
                .GetAwaiter().GetResult();
            Check("Inch −5 chạy xong", okMinus);
            Check("Inch −5 quay lại đúng vị trí ban đầu (đi rồi về là bằng chính nó)",
                Near(x.CurrentPos, start), $"start={start:0.###} now={x.CurrentPos:0.###}");

            // Soft limit của Inching phải kiểm theo đích TUYỆT ĐỐI quy ra từ vị trí hiện tại +
            // delta (IAxis.MoveIncAsync), không phải theo độ lớn delta — một bước rất lớn đẩy
            // vượt SoftLimitPos phải bị chặn hẳn, không đi lụi ra ngoài hành trình.
            double overshoot = x.Config.SoftLimitPos + 50 - x.CurrentPos;
            bool blocked = McControl.Func.MoveIncAsync(AxisId.X, overshoot, speed, CancellationToken.None)
                .GetAwaiter().GetResult();
            Check("Bước nhảy đẩy đích vượt soft limit thì bị chặn, đứng nguyên",
                !blocked && Near(x.CurrentPos, start), $"now={x.CurrentPos:0.###}");

            // AxisFault.SoftLimit vừa bơm ở trên là mức Error → còn nằm lại chờ RESET
            // (AlarmManager.ClearCondition không tự xoá được Error/Fatal), và SafetyMonitor chặn
            // SafeToMove hễ còn alarm Error nào active (AlarmManager.HasError) — không dọn thì
            // MỌI bài test chạy SAU sẽ bị chặn di chuyển oan, y hệt quy ước dọn dẹp ở cuối
            // TestResetRules.
            AlarmManager.ForceClearAll();
        }

        private static void TestRotaryWrap()
        {
            Section("Trục quay A1/A2 — toạ độ quấn 0–360°, offset gốc, đường ngắn");

            // ── Số học góc ──
            Check("Quấn: −10° → 350°", Near(AxisAngle.Wrap(-10), 350));
            Check("Quấn: 370° → 10°", Near(AxisAngle.Wrap(370), 10));
            Check("Quấn: 360° → 0° (không kẹt ở 360)", Near(AxisAngle.Wrap(360), 0),
                AxisAngle.Wrap(360).ToString());
            Check("Quấn: −0.0000001° → 0°, không thành 359.9999999",
                Near(AxisAngle.Wrap(-1e-7), 0), AxisAngle.Wrap(-1e-7).ToString("0.#######"));
            Check("Quấn: 720° → 0°", Near(AxisAngle.Wrap(720), 0));
            Check("Giá trị hỏng (NaN) trả 0 thay vì lan xuống lệnh chạy",
                Near(AxisAngle.Wrap(double.NaN), 0));

            // Chỗ dễ sai nhất của trục quay: đi từ 350° tới 10° là +20°, không phải −340°.
            Check("Đường ngắn 350° → 10° là +20°", Near(AxisAngle.ShortestDelta(350, 10), 20),
                AxisAngle.ShortestDelta(350, 10).ToString());
            Check("Đường ngắn 10° → 350° là −20°", Near(AxisAngle.ShortestDelta(10, 350), -20));
            Check("Đúng nửa vòng thì đi chiều DƯƠNG (quy ước hoà cố định)",
                Near(AxisAngle.ShortestDelta(0, 180), 180), AxisAngle.ShortestDelta(0, 180).ToString());
            Check("Khoảng cách 359° tới 1° là 2°, không phải 358°",
                Near(AxisAngle.Distance(359, 1), 2), AxisAngle.Distance(359, 1).ToString());

            // ── Cấu hình ──
            var sys = Core.Data.SystemData.CreateDefault();
            foreach (string name in new[] { AxisId.A1, AxisId.A2 })
            {
                var a = sys.GetAxis(name);
                Check(name + " bật quấn vòng", a.WrapDegrees);
                Check(name + " dải hiển thị 0…360", Near(a.SoftLimitNeg, 0) && Near(a.SoftLimitPos, 360));
                Check(name + " 180° đầu hút cho mỗi vòng động cơ", Near(a.UnitPerRev, 180),
                    a.UnitPerRev.ToString());
                // Trục quay tự do: không góc nào bị hàng rào hành trình chặn, kể cả giá trị
                // chưa quấn mà phép bù của vision đẩy vượt 360.
                Check(name + " không chặn góc nào (370° vẫn hợp lệ)", a.InSoftLimit(370) && a.InSoftLimit(-5));
            }
            Check("X vẫn giữ hàng rào hành trình như cũ",
                !sys.GetAxis(AxisId.X).InSoftLimit(sys.GetAxis(AxisId.X).SoftLimitPos + 1));

            // ── Tự sửa system.json viết trước 2026-09-12 ──
            var old = Core.Data.SystemData.CreateDefault();
            foreach (string name in new[] { AxisId.A1, AxisId.A2 })
            {
                var a = old.GetAxis(name);
                a.WrapDegrees = false;
                a.PulsePerUnit = 10000.0 / 360.0;      // con số cũ: quên mất hộp số 1/2
                a.UnitPerRev = 360;
                a.SoftLimitNeg = -360;
                a.HomeOffset = 137.5;                  // số ĐO ĐƯỢC — không được đụng vào
            }
            var oldA1 = old.GetAxis(AxisId.A1);

            var migrated = old.MigrateRotaryAxes();
            Check("Nhận ra file cũ và sửa đúng 2 trục quay", migrated.Count == 2,
                string.Join(",", migrated));
            Check("Sửa lại thành 100 xung/độ", Near(oldA1.PulsePerUnit, 36000.0 / 360.0),
                oldA1.PulsePerUnit.ToString());
            Check("Bật quấn vòng và kéo dải về 0…360",
                oldA1.WrapDegrees && Near(oldA1.SoftLimitNeg, 0));
            Check("GIỮ NGUYÊN offset gốc đã đo được", Near(oldA1.HomeOffset, 137.5),
                oldA1.HomeOffset.ToString());
            Check("Chạy lần hai không sửa gì thêm (một lần là đủ)",
                old.MigrateRotaryAxes().Count == 0);

            // ── Tự sửa file ĐÃ migrate một lần nhưng tỉ lệ xung đã lỗi thời ──
            // Đây là cái bẫy của lần đổi động cơ 2026-09-12 (10000 → 18000 xung/vòng): file trên
            // máy đã bật WrapDegrees từ lần migrate trước, nên nếu chỉ xét cờ đó thì file không
            // bao giờ được sửa nữa — 55.5556 xung/độ nằm lại trong system.json và ghi đè hằng số
            // mới NGAY LÚC NẠP. Trục vẫn chạy, vẫn báo "tới nơi", chỉ xoay 55.56% góc được lệnh.
            var stale = Core.Data.SystemData.CreateDefault();
            foreach (string name in new[] { AxisId.A1, AxisId.A2 })
            {
                var a = stale.GetAxis(name);
                a.PulsePerUnit = 20000.0 / 360.0;      // tỉ lệ của động cơ 10000 xung/vòng
                a.HomeOffset = 42.25;                  // số ĐO ĐƯỢC — không được đụng vào
            }
            var staleA1 = stale.GetAxis(AxisId.A1);

            var fixedAxes = stale.MigrateRotaryAxes();
            Check("File đã quấn vòng nhưng tỉ lệ xung cũ vẫn được nhận ra", fixedAxes.Count == 2,
                string.Join(",", fixedAxes));
            Check("Tỉ lệ xung cũ bị ghi đè bằng hằng số hiện hành",
                Near(staleA1.PulsePerUnit, Core.Data.SystemData.RotaryPulsePerDeg),
                staleA1.PulsePerUnit.ToString());
            Check("Sửa tỉ lệ xung vẫn GIỮ NGUYÊN offset gốc đã đo được",
                Near(staleA1.HomeOffset, 42.25), staleA1.HomeOffset.ToString());
            Check("File đúng hằng số hiện hành thì không bị đụng tới",
                Core.Data.SystemData.CreateDefault().MigrateRotaryAxes().Count == 0);

            // ── iBdID trùng nhau: file cũ không có trường BoardId nên A2 đọc ra 0, trùng đúng ô của A1 ──
            // Hai module Fastech chung một iBdID thì FAS_Connect của cái sau đè lên cái trước (Connect gọi
            // FAS_Close(bdID) ngay đầu), và trục này đọc ra trạng thái của trục kia — không lỗi nào hiện ra.
            var sameBoard = Core.Data.SystemData.CreateDefault();
            sameBoard.GetAxis(AxisId.A2).BoardId = sameBoard.GetAxis(AxisId.A1).BoardId;
            var boardFixed = sameBoard.MigrateRotaryAxes();
            Check("Hai trục quay trùng iBdID → nhận ra và sửa", boardFixed.Contains(AxisId.A2),
                string.Join(",", boardFixed));
            Check("Sửa xong iBdID của A1 và A2 khác nhau, đúng bảng phân bổ trong mã",
                sameBoard.GetAxis(AxisId.A1).BoardId == ShieldCanAssy.Lib.Fastech.FastechBoardId.AxisA1 &&
                sameBoard.GetAxis(AxisId.A2).BoardId == ShieldCanAssy.Lib.Fastech.FastechBoardId.AxisA2,
                sameBoard.GetAxis(AxisId.A1).BoardId + " / " + sameBoard.GetAxis(AxisId.A2).BoardId);
            Check("iBdID không đụng tới hai card I/O (10 và 11)",
                ShieldCanAssy.Lib.Fastech.FastechBoardId.AxisA1 != ShieldCanAssy.Lib.Fastech.FastechBoardId.Io32Di &&
                ShieldCanAssy.Lib.Fastech.FastechBoardId.AxisA2 != ShieldCanAssy.Lib.Fastech.FastechBoardId.Io32Do);

            // ── Hành vi thật của trục ──
            if (!McControl.Status.OrgOk)
            {
                McControl.RequestOrigin();
                WaitUntil(() => McControl.Status.State == EMcState.Homing, 2000);
                WaitUntil(() => McControl.Status.State != EMcState.Homing, 40000);
            }
            Check("Đã về gốc trước khi kiểm trục quay", McControl.Status.OrgOk);

            var a1 = McControl.Motion[AxisId.A1];
            double speed = a1.Config.MoveSpeed;

            Check("Sau khi về gốc, toạ độ A1 = offset gốc",
                Near(a1.CurrentPos, AxisAngle.Wrap(a1.Config.HomeOffset)), a1.CurrentPos.ToString("0.###"));

            bool ok350 = McControl.Func.MoveAsync(AxisId.A1, 350, speed, CancellationToken.None)
                .GetAwaiter().GetResult();
            Check("Xoay tới 350° chạy xong", ok350);
            Check("Toạ độ hiện 350°, không phải −10°", AxisAngle.Distance(a1.CurrentPos, 350) < 0.05,
                a1.CurrentPos.ToString("0.###"));

            // Đây là bài chính: nếu ai bỏ phép chọn vòng gần nhất thì lệnh này vẫn tới đúng 10°
            // nhưng đi ngược gần trọn vòng — sai lệch chỉ lộ ra ở thời gian chu kỳ và ở dây khí.
            var sw = Stopwatch.StartNew();
            bool ok10 = McControl.Func.MoveAsync(AxisId.A1, 10, speed, CancellationToken.None)
                .GetAwaiter().GetResult();
            sw.Stop();
            double expectedShortMs = 20.0 / speed * 1000.0;
            Check("Xoay 350° → 10° chạy xong", ok10);
            Check("Toạ độ hiện 10°", AxisAngle.Distance(a1.CurrentPos, 10) < 0.05,
                a1.CurrentPos.ToString("0.###"));
            Check("Đi đường ngắn +20°, không tháo ngược −340°",
                sw.ElapsedMilliseconds < expectedShortMs * 4 + 500,
                $"{sw.ElapsedMilliseconds}ms, đường ngắn cỡ {expectedShortMs:0}ms");

            // Toạ độ không bao giờ được ra ngoài [0, 360) dù lệnh đưa vào là số chưa quấn — đúng
            // tình huống Routine_Auto.PlaceAsync tạo ra khi cộng CurrentPos + Δθ.
            bool okOver = McControl.Func.MoveAsync(AxisId.A1, 370, speed, CancellationToken.None)
                .GetAwaiter().GetResult();
            Check("Lệnh 370° không bị chặn oan mà quấn thành 10°",
                okOver && AxisAngle.Distance(a1.CurrentPos, 10) < 0.05, a1.CurrentPos.ToString("0.###"));
            Check("Toạ độ luôn nằm trong [0, 360)",
                a1.CurrentPos >= 0 && a1.CurrentPos < 360, a1.CurrentPos.ToString("0.###"));

            bool okNeg = McControl.Func.MoveAsync(AxisId.A1, -30, speed, CancellationToken.None)
                .GetAwaiter().GetResult();
            Check("Lệnh −30° quấn thành 330°",
                okNeg && AxisAngle.Distance(a1.CurrentPos, 330) < 0.05, a1.CurrentPos.ToString("0.###"));

            AlarmManager.ForceClearAll();
        }

        private static void TestPlanarHomeRequiresZHomed()
        {
            Section("Về gốc/Move Abs của X/Y1/Y2 đòi Z1/Z2 đã về gốc THẬT (McFunction.CanMoveAutonomous)");

            Check("Trước khi bơm lỗi: Z đã về gốc + an toàn nên X được phép Về gốc/Move Abs",
                McControl.Func.CanMoveAutonomous(AxisId.X, out _));

            // Tắt/bật servo KHÔNG làm mất gốc (Tuấn bỏ quy tắc "mất servo là mất gốc" 2026-09-26): driver vẫn
            // đếm encoder khi tắt servo.
            var z1 = McControl.Motion[AxisId.Z1];
            z1.SetServoOnAsync(false).Wait();
            bool keptWhileOff = z1.OrgOk;
            z1.SetServoOnAsync(true).Wait();
            Check("Tắt rồi bật servo Z1 vẫn giữ gốc — không phải về gốc lại", keptWhileOff && z1.OrgOk);

            // Z1 mất gốc nhưng KHÔNG đổi CurrentPos — ZSafeOk theo vị trí vẫn đúng. Đây đúng là lỗ hổng cần
            // chặn riêng: con số đọc được "đang trong vùng an toàn" nhưng không đáng tin vì Z1 chưa neo lại gốc
            // thật. Làm mất gốc bằng một lần về gốc bị huỷ ngay từ đầu — về gốc dở không bao giờ được coi là đã
            // về gốc (§4.4).
            using (var cancelled = new CancellationTokenSource())
            {
                cancelled.Cancel();
                z1.HomeAsync(cancelled.Token).GetAwaiter().GetResult();
            }
            Check("Về gốc Z1 bị huỷ giữa chừng thì mất OrgOk", !z1.OrgOk);
            Check("ZSafeOk theo vị trí vẫn đúng dù Z1 vừa mất gốc", McControl.Motion.ZSafeOk);

            string reason;
            bool blocked = McControl.Func.CanMoveAutonomous(AxisId.X, out reason);
            Check("Z1 mất gốc thì X bị chặn Về gốc/Move Abs dù ZSafeOk vẫn đúng",
                !blocked && reason != null && reason.Contains("về gốc"), reason);

            Check("Jog KHÔNG bị chặn bởi điều kiện này — vẫn dùng CanMove trơn, không phải CanMoveAutonomous",
                McControl.Func.CanMove(AxisId.X, out _));
            Check("Inching KHÔNG bị chặn bởi điều kiện này — MoveIncAsync cũng dùng CanMove trơn",
                McControl.Func.MoveIncAsync(AxisId.X, 1.0, McControl.Motion[AxisId.X].Config.MoveSpeed, CancellationToken.None)
                    .GetAwaiter().GetResult());
            McControl.Func.MoveIncAsync(AxisId.X, -1.0, McControl.Motion[AxisId.X].Config.MoveSpeed, CancellationToken.None)
                .GetAwaiter().GetResult();

            // Khôi phục: về gốc lại Z1, RỒI nhả phanh lại — SetServoOnAsync(false)
            // ở trên làm phanh khoá lại (UpdateBrakeOutputs chỉ nhả khi ServoOn) và reset đồng hồ
            // đếm nhả phanh; bật servo lại KHÔNG tự nhả phanh, phải bấm lại "Mở phanh"
            // (RequestBrakeRelease) như đúng nghi thức thật. Thiếu bước này thì CanMove(Z1) —
            // dùng bởi McFunction.MoveAsync trong Routine_Auto — sẽ kẹt ở lý do "đang khoá phanh"
            // suốt các bài chạy SAU, treo cả TestAutoCycle (lỗi thật đã gặp, Tuấn phát hiện lúc
            // review PR — self-test hang ở TestAutoCycle vì thiếu đúng bước này).
            z1.SetServoOnAsync(true).Wait();
            bool rehomed = z1.HomeAsync(CancellationToken.None).GetAwaiter().GetResult();
            Check("Về gốc lại Z1 để dọn sạch trạng thái cho các bài sau", rehomed && z1.OrgOk);
            McControl.Func.RequestBrakeRelease(AxisId.Z1);
            bool brakeReady = WaitUntil(() => McControl.Func.CanMove(AxisId.Z1, out _), 1000);
            Check("Nhả lại phanh Z1 để các bài chạy sau di chuyển được bình thường", brakeReady);
            Check("X được phép Về gốc/Move Abs trở lại sau khi Z1 về gốc xong",
                McControl.Func.CanMoveAutonomous(AxisId.X, out _));
        }

        private static void TestZDecelPoint()
        {
            Section("Điểm giảm tốc của Z khi hạ xuống và khi nâng lên (McFunction.MoveZDownAsync / MoveZUpAsync)");

            // Luật chọn hai chặng — khoá bằng bảng chân trị chứ không đo thời gian chạy.
            Check("Chưa teach điểm giảm tốc (0) thì hạ một mạch",
                !McFunction.UseTwoStageZ(49, 0, 90));
            Check("Điểm giảm tốc nằm giữa chỗ đứng và đích thì chạy hai chặng",
                McFunction.UseTwoStageZ(49, 85, 90));
            Check("Teach nhầm xuống DƯỚI cả cao độ hút thì bỏ qua, hạ một mạch",
                !McFunction.UseTwoStageZ(49, 95, 90));
            Check("Z đã ở dưới điểm giảm tốc rồi thì hạ nốt một mạch",
                !McFunction.UseTwoStageZ(86, 85, 90));

            // Chiều LÊN là gương của chiều xuống (Tuấn yêu cầu 2026-09-24): chậm từ cao độ hút/đặt lên tới
            // điểm giảm tốc rồi mới tăng tốc. Cùng một bảng chân trị, đọc ngược.
            Check("Nâng: chưa teach điểm giảm tốc (0) thì nâng một mạch",
                !McFunction.UseTwoStageZUp(90, 0, 49));
            Check("Nâng: điểm giảm tốc nằm giữa cao độ hút và đích thì chạy hai chặng",
                McFunction.UseTwoStageZUp(90, 85, 49));
            Check("Nâng: teach nhầm điểm giảm tốc xuống DƯỚI chỗ đang đứng thì nâng một mạch",
                !McFunction.UseTwoStageZUp(90, 95, 49));
            Check("Nâng: Z đã ở TRÊN điểm giảm tốc rồi thì nâng nốt một mạch",
                !McFunction.UseTwoStageZUp(84, 85, 49));
            // Cảm biến "có tray" gắn cố định ở vị trí chờ, tray chạy vào vùng làm việc là mất tín hiệu
            // (Tuấn nêu 2026-09-24). Ngoài vị trí chờ thì OFF chỉ có nghĩa "không đo được", không phải
            // "thiếu tray" — xem Routine_Auto.TrayMissingAt.
            Check("Ở vị trí chờ, cảm biến ON → có tray",
                !Routine_Auto.TrayMissingAt(20.0, 20.0, true));
            Check("Ở vị trí chờ, cảm biến OFF → kết luận THIẾU tray, chặn chạy",
                Routine_Auto.TrayMissingAt(20.0, 20.0, false));
            Check("Sai lệch nhỏ hơn cửa sổ vẫn coi là đang ở vị trí chờ",
                Routine_Auto.TrayMissingAt(20.5, 20.0, false));
            Check("Tray ĐÃ vào vùng làm việc, cảm biến OFF → KHÔNG kết luận gì, không dừng máy",
                !Routine_Auto.TrayMissingAt(60.0, 20.0, false));
            Check("Tray đã vào vùng làm việc mà cảm biến ON thì cũng không chặn",
                !Routine_Auto.TrayMissingAt(60.0, 20.0, true));

            Check("Nâng và hạ dùng chung một luật, chỉ đổi chỗ chỗ-đứng và đích",
                McFunction.UseTwoStageZUp(90, 85, 49) == McFunction.UseTwoStageZ(49, 85, 90) &&
                McFunction.UseTwoStageZUp(84, 85, 49) == McFunction.UseTwoStageZ(49, 85, 84));

            var cfg = new ShieldCanAssy.Core.Communication.AxisConfig { MoveSpeed = 100 };
            Check("Auto vừa chưa đặt thì suy ra 50% mức cao",
                Math.Abs(cfg.MoveSpeedMid - 50) < 1e-9, cfg.MoveSpeedMid.ToString("0.###"));
            Check("Auto thấp chưa đặt thì suy ra 20% mức cao",
                Math.Abs(cfg.MoveSpeedLow - 20) < 1e-9, cfg.MoveSpeedLow.ToString("0.###"));
            cfg.MoveSpeedLow = 12;
            Check("Đặt rồi thì giữ đúng số đã đặt, không suy ra nữa",
                Math.Abs(cfg.MoveSpeedLow - 12) < 1e-9, cfg.MoveSpeedLow.ToString("0.###"));

            var tray = Core.Data.McDataManager.Model.InputTray;
            Check("Model mô phỏng có sẵn điểm giảm tốc hợp lệ cho cả hai tay",
                tray.ZSlowValid(AxisId.Z1) && tray.ZSlowValid(AxisId.Z2),
                tray.Z_SlowZ1.ToString("0.###") + " / " + tray.Z_SlowZ2.ToString("0.###"));
            Check("ZSlowFor trả đúng số của từng tay",
                Math.Abs(tray.ZSlowFor(AxisId.Z1) - tray.Z_SlowZ1) < 1e-9 &&
                Math.Abs(tray.ZSlowFor(AxisId.Z2) - tray.Z_SlowZ2) < 1e-9);

            // 4 ô "Trục Z qua điểm giảm tốc" ở Setting (Tuấn 2026-10-07): tắt thì chặng đó nhận 0 = chạy một mạch.
            var fresh = new Core.Data.SystemData();
            Check("Setting Z qua điểm giảm tốc: mặc định đúng nhịp 30/09 (đầu vào hạ một mạch, nâng chậm; lắp ráp chậm cả hai chiều)",
                !fresh.InputZSlowDown && fresh.InputZSlowUp && fresh.AssyZSlowDown && fresh.AssyZSlowUp);
            Check("Setting Z qua điểm giảm tốc: tắt thì điểm giảm tốc của chặng = 0 (một mạch), bật thì đúng số đã teach",
                tray.ZSlowFor(AxisId.Z1, false) == 0 && Math.Abs(tray.ZSlowFor(AxisId.Z1, true) - tray.Z_SlowZ1) < 1e-9 &&
                !McFunction.UseTwoStageZ(0, tray.ZSlowFor(AxisId.Z1, false), tray.Z_PickZ1));

            // Chạy thật một lần: qua hai chặng vẫn phải dừng đúng cao độ hút, không dừng lại ở
            // điểm giảm tốc giữa đường.
            var z1 = McControl.Motion[AxisId.Z1];
            bool down = McControl.Func
                .MoveZDownAsync(AxisId.Z1, tray.Z_PickZ1, tray.Z_SlowZ1, CancellationToken.None)
                .GetAwaiter().GetResult();
            Check("Hạ hai chặng xong dừng đúng cao độ hút, không dừng ở điểm giảm tốc",
                down && Math.Abs(z1.CurrentPos - tray.Z_PickZ1) < 0.05, z1.CurrentPos.ToString("0.###"));

            // Nâng qua hai chặng: phải dừng đúng Z_SAFE, không mắc lại ở điểm giảm tốc giữa đường.
            bool up = McControl.Func
                .MoveZUpAsync(AxisId.Z1, McControl.Motion.ZSafe, tray.Z_SlowZ1, CancellationToken.None)
                .GetAwaiter().GetResult();
            Check("Nâng hai chặng xong dừng đúng Z_SAFE, không dừng ở điểm giảm tốc",
                up && Math.Abs(z1.CurrentPos - McControl.Motion.ZSafe) < 0.05, z1.CurrentPos.ToString("0.###"));
            Check("Nâng Z1 lên đúng Z_SAFE thì qua chốt chặn (dung sai 1 mm), các bài sau chạy được",
                up && McControl.Motion.ZSafeOk, z1.CurrentPos.ToString("0.###"));
        }

        private static void TestSettleBeforeCapture()
        {
            Section("Chờ ổn định trước khi đọc encoder và chụp (McFunction.WaitSettleAsync)");

            Check("Chưa chạy lệnh nào thì không chờ",
                McFunction.SettleRemainingMs(long.MinValue, 10000, 200) == 0);
            Check("Tắt bằng 0 thì không chờ",
                McFunction.SettleRemainingMs(1000, 1000, 0) == 0);
            Check("Vừa dừng xong thì chờ đủ cả khoảng",
                McFunction.SettleRemainingMs(1000, 1000, 200) == 200);
            Check("Đã trôi 150 ms thì chờ nốt 50 ms",
                McFunction.SettleRemainingMs(1000, 1150, 200) == 50);
            Check("Đã trôi quá khoảng chờ thì đi tiếp ngay — ảnh thứ hai trở đi không mất gì",
                McFunction.SettleRemainingMs(1000, 1500, 200) == 0);
            Check("Trục đứng yên từ lâu thì không chờ",
                McFunction.SettleRemainingMs(1000, 9999999, 200) == 0);

            // Mỗi camera một số chờ ổn định và chờ đèn (Tuấn 2026-10-07); Flow chỉ tính thì không chờ.
            var camSys = new Core.Data.SystemData { BottomSettleMs = 11, TopSettleMs = 22, BottomLampMs = 33, TopLampMs = 44 };
            Check("Chờ ổn định lấy đúng số của từng camera",
                camSys.SettleMsFor(EVmCamera.BottomUp) == 11 && camSys.SettleMsFor(EVmCamera.TopDown) == 22
                && camSys.SettleMsFor(EVmCamera.None) == 0);
            Check("Chờ đèn lấy đúng số của từng camera",
                camSys.LampMsFor(EVmCamera.BottomUp) == 33 && camSys.LampMsFor(EVmCamera.TopDown) == 44
                && camSys.LampMsFor(EVmCamera.None) == 0);
            var camJson = Newtonsoft.Json.JsonConvert.SerializeObject(camSys);
            var camBack = Newtonsoft.Json.JsonConvert.DeserializeObject<Core.Data.SystemData>(
                "{\"VisionSettleMs\":150,\"LampSettleMs\":80}");
            Check("system.json cũ (VisionSettleMs/LampSettleMs) vẫn nạp vào số của CAM bottom-up",
                camBack.BottomSettleMs == 150 && camBack.BottomLampMs == 80 && camJson.Contains("\"TopLampMs\":44"),
                camBack.BottomSettleMs + "/" + camBack.BottomLampMs);

            // Vị trí đọc NGAY sau lệnh chạy phải là số của đích, không phải số cũ của vòng quét.
            var x = McControl.Motion[AxisId.X];
            double home = x.CurrentPos;
            double target = home + 5.0;
            bool moved = McControl.Func.MoveAsync(AxisId.X, target, x.Config.MoveSpeed, CancellationToken.None)
                .GetAwaiter().GetResult();
            Check("Đọc CurrentPos ngay sau khi lệnh chạy trả về đã là vị trí MỚI",
                moved && Math.Abs(x.CurrentPos - target) < 0.05, x.CurrentPos.ToString("0.###"));

            McControl.Func.MoveAsync(AxisId.X, home, x.Config.MoveSpeed, CancellationToken.None)
                .GetAwaiter().GetResult();
            Check("Trả trục X về chỗ cũ cho các bài sau", Math.Abs(x.CurrentPos - home) < 0.05,
                x.CurrentPos.ToString("0.###"));
        }

        private static void TestRoutineCalib()
        {
            Section("Wizard hiệu chuẩn vision (Routine_Calib) — PLAN.md §6.2–§6.5, chạy trên FakeVision");

            if (!McControl.Status.OrgOk)
            {
                McControl.RequestOrigin();
                WaitUntil(() => McControl.Status.State == EMcState.Homing, 2000);
                WaitUntil(() => McControl.Status.State != EMcState.Homing, 40000);
            }
            Check("Đã về gốc trước khi chạy wizard hiệu chuẩn", McControl.Status.OrgOk);

            // ── §6.2b Kiểm tra hiệu chuẩn CAM top-down qua Flow TopDownVerify ──
            //
            // FakeVision đang tắt quang học giả: lượng dịch khớp mẫu là nhiễu ngẫu nhiên quanh 0, KHÔNG gắn với
            // trục, nên vị trí dấu chuẩn tính ra chạy theo trục thay vì đứng yên. Phép kiểm tra phải KHÔNG ĐẠT —
            // đúng hành vi cần khoá lại. Nhánh "đạt" khoá ở TestVmCalib với quang học giả bật.
            double x0 = McControl.Motion[AxisId.X].CurrentPos;
            double y0 = McControl.Motion[AxisId.Y2].CurrentPos;

            var td = McControl.Calib.VerifyTopDownAsync(5.0, CancellationToken.None).GetAwaiter().GetResult();
            Check("VerifyTopDownAsync chạy xong, không treo/crash", td != null);
            Check("Dữ liệu vision mô phỏng không gắn với trục thật → phép kiểm tra top-down TỪ CHỐI",
                td != null && !td.Ok, td?.Reason);
            Check("Kiểm tra xong thì trả trục X và Y2 về đúng vị trí ban đầu",
                Near(McControl.Motion[AxisId.X].CurrentPos, x0) && Near(McControl.Motion[AxisId.Y2].CurrentPos, y0),
                $"X={McControl.Motion[AxisId.X].CurrentPos:0.###} (đầu {x0:0.###}), Y2={McControl.Motion[AxisId.Y2].CurrentPos:0.###} (đầu {y0:0.###})");

            var tdSmall = McControl.Calib.VerifyTopDownAsync(0.1, CancellationToken.None).GetAwaiter().GetResult();
            Check("Quãng chạy thử quá nhỏ thì từ chối ngay, không chạy trục vô ích", !tdSmall.Ok, tdSmall.Reason);

            // ── §6.4 Chuỗi nạp điểm hiệu chuẩn Align: tịnh tiến X rồi xoay A, kết thúc ở 0° ──
            var a1 = McControl.Motion[AxisId.A1];
            double alignPosX = Core.Data.McDataManager.Model.AlignPosX(1);

            // Mọi ảnh bottom-up chụp ở cùng một cao độ — đúng Z_SAFE, cũng là chỗ Auto soi (Tuấn chốt 2026-09-22).
            // Đặt Z1 ở chỗ khác trước để thấy bước hiệu chuẩn tự đưa về, không chụp ở chỗ Z đang đứng.
            var z1 = McControl.Motion[AxisId.Z1];
            double zCapture = McControl.Motion.ZSafe;
            bool z1Away = McControl.Func.MoveAsync(AxisId.Z1, 0, z1.Config.MoveSpeed, CancellationToken.None).GetAwaiter().GetResult()
                          && !Near(z1.CurrentPos, zCapture);

            var seq = McControl.Calib.RunAlignCalibSequenceAsync(
                1, alignPosX, new List<double> { -1, 0, 1 }, new List<double> { 0, 60, 120 },
                null, CancellationToken.None).GetAwaiter().GetResult();

            Check("RunAlignCalibSequenceAsync chạy hết chuỗi trên FakeVision", seq.Ok, seq.Reason);
            Check("Nạp đúng số điểm đã yêu cầu (3 tịnh tiến + 3 góc)", seq.PointsCaptured == 6, seq.PointsCaptured.ToString());
            Check("Chuỗi kết thúc với trục A ở 0° — tư thế chuẩn để chụp ảnh mẫu, không đứng ở góc cuối",
                Near(a1.CurrentPos, 0), $"A1={a1.CurrentPos:0.###}");
            Check("Chuỗi kết thúc tại đúng vị trí soi của tay hút (PDF Hikrobot tr.45)",
                Near(McControl.Motion[AxisId.X].CurrentPos, alignPosX),
                $"X={McControl.Motion[AxisId.X].CurrentPos:0.###}, vị trí soi {alignPosX:0.###}");
            Check("Chuỗi hiệu chuẩn bottom-up tự đưa Z1 về cao độ soi (Z_SAFE) như Auto",
                z1Away && Near(z1.CurrentPos, zCapture), $"Z1={z1.CurrentPos:0.###}, cao độ soi {zCapture:0.###}");

            var seqFewAngles = McControl.Calib.RunAlignCalibSequenceAsync(
                1, alignPosX, new List<double> { 0 }, new List<double> { 0, 90 },
                null, CancellationToken.None).GetAwaiter().GetResult();
            Check("Dưới 3 góc xoay thì từ chối (VisionMaster cần tối thiểu 3 lần xoay)", !seqFewAngles.Ok, seqFewAngles.Reason);

            // ── Chuỗi nạp điểm tự bấm "Clear Point" của module hiệu chuẩn trước (Tuấn yêu cầu 2026-10-09) ──
            var fakeClear = McControl.Vision as FakeVision;
            Check("Chuỗi Align xoá điểm cũ trong module hiệu chuẩn của AlignBottom_Tool1 trước điểm đầu tiên",
                fakeClear != null && fakeClear.LastCalibClear == Core.Communication.EVisionProcedure.AlignCalibTool1
                && fakeClear.CalibPointsSinceClear == seq.PointsCaptured,
                $"xoá {fakeClear?.LastCalibClear}, nạp sau lần xoá {fakeClear?.CalibPointsSinceClear}, chuỗi nạp {seq.PointsCaptured}");
            if (fakeClear != null)
            {
                int pointsBeforeFail = fakeClear.CalibPointCount;
                double xBeforeFail = McControl.Motion[AxisId.X].CurrentPos;
                fakeClear.FailCalibClear = true;
                var seqNoClear = McControl.Calib.RunAlignCalibSequenceAsync(
                    1, alignPosX, new List<double> { -1, 0, 1 }, new List<double> { 0, 60, 120 },
                    null, CancellationToken.None).GetAwaiter().GetResult();
                fakeClear.FailCalibClear = false;
                Check("Không xoá được điểm cũ thì dừng chuỗi Align trước khi chạy trục hay nạp điểm nào",
                    !seqNoClear.Ok && fakeClear.CalibPointCount == pointsBeforeFail
                    && Near(McControl.Motion[AxisId.X].CurrentPos, xBeforeFail), seqNoClear.Reason);
            }

            bool alignCalibOk = McControl.Calib
                .MoveAndTriggerAlignCalibAsync(1, 60, CancellationToken.None)
                .GetAwaiter().GetResult();
            Check("MoveAndTriggerAlignCalibAsync xoay A1 tới đúng góc rồi trigger nhánh hiệu chuẩn AlignBottom_Tool1",
                alignCalibOk && Near(a1.CurrentPos, 60), $"A1={a1.CurrentPos:0.###}");

            // ── Mỗi điểm hiệu chuẩn phải mang theo VỊ TRÍ TRỤC THẬT (§6.2, §6.4) ──
            //
            // Solution thật nhận bộ số này qua biến toàn cục, module hiệu chuẩn subscribe vào đó
            // làm toạ độ vật lý của điểm vừa nạp. Gửi thiếu, gửi sai trục, hay gửi vị trí LỆNH thay
            // vì encoder đều cho ra một ma trận hiệu chuẩn méo mà không có thông báo lỗi nào — nên
            // khoá lại ở đây, chỗ duy nhất kiểm được mà không cần máy thật.
            var fake = McControl.Vision as FakeVision;
            var sent = fake?.LastCalibAxes;
            Check("Nạp điểm hiệu chuẩn có gửi kèm vị trí trục sang VisionMaster", sent != null);
            if (sent != null)
            {
                Check("Gửi đúng góc trục A1 ĐANG đứng, đọc từ encoder chứ không phải góc lệnh",
                    Near(sent.A1, a1.CurrentPos), $"gửi A1={sent.A1:0.###}, encoder={a1.CurrentPos:0.###}");
                Check("Gửi đúng vị trí trục X đang đứng",
                    Near(sent.X, McControl.Motion[AxisId.X].CurrentPos),
                    $"gửi X={sent.X:0.###}, encoder={McControl.Motion[AxisId.X].CurrentPos:0.###}");
                Check("Gửi đủ bốn trục (X, Y2, A1, A2) chứ không chỉ trục vừa chạy — Flow nào " +
                    "cần trục nào thì tự subscribe, đổi cách hiệu chuẩn không phải sửa code",
                    Near(sent.Y2, McControl.Motion[AxisId.Y2].CurrentPos) &&
                    Near(sent.A2, McControl.Motion[AxisId.A2].CurrentPos),
                    $"Y2={sent.Y2:0.###} A2={sent.A2:0.###}");
            }

            int calibPointsBefore = fake?.CalibPointCount ?? 0;
            McControl.Calib.TriggerTopDownCalibFlowAsync(CancellationToken.None).GetAwaiter().GetResult();
            Check("Hiệu chuẩn top-down (nhánh hiệu chuẩn LocateTop) cũng đi qua đường nạp điểm kèm toạ độ, không phải trigger trần",
                (fake?.CalibPointCount ?? 0) == calibPointsBefore + 1,
                $"trước {calibPointsBefore}, sau {fake?.CalibPointCount}");

            z1Away = McControl.Func.MoveAsync(AxisId.Z1, 0, z1.Config.MoveSpeed, CancellationToken.None).GetAwaiter().GetResult()
                     && !Near(z1.CurrentPos, zCapture);
            bool alignCalibFlowOk = McControl.Calib
                .TriggerAlignCalibFlowAsync(1, CancellationToken.None)
                .GetAwaiter().GetResult();
            Check("TriggerAlignCalibFlowAsync trigger được nhánh hiệu chuẩn AlignBottom_Tool1 tại góc hiện tại, không xoay",
                alignCalibFlowOk && Near(a1.CurrentPos, 60), $"A1={a1.CurrentPos:0.###}");
            Check("Nạp một điểm tại chỗ cũng chụp ở cao độ soi, không ở chỗ Z đang đứng",
                z1Away && Near(z1.CurrentPos, zCapture), $"Z1={z1.CurrentPos:0.###}, cao độ soi {zCapture:0.###}");

            bool toZeroOk = McControl.Calib.MoveAlignAxisToZeroAsync(1, CancellationToken.None)
                .GetAwaiter().GetResult();
            // So bằng KHOẢNG CÁCH GÓC, không bằng phép trừ: trục dừng quanh đích chứ không đúng
            // đích, nên "về 0°" trong thực tế là 359.996° hoặc 0.004° — hai con số cách nhau
            // 359.99 nếu trừ thẳng, nhưng cách nhau 0.008° thật. Mọi chỗ so góc của A1/A2 đều
            // phải dùng AxisAngle.Distance vì lý do này.
            Check("MoveAlignAxisToZeroAsync đưa trục A về 0°",
                toZeroOk && AxisAngle.Distance(a1.CurrentPos, 0) < 0.05, $"A1={a1.CurrentPos:0.###}");

            // ── §6.4b Phép thử vòng xoay ──
            //
            // Lại cùng lý do: FakeVision trả góc ngẫu nhiên, không gắn với trục A thật, nên góc
            // đo được sẽ không khớp góc đã xoay → phải TỪ CHỐI. Quan trọng hơn cả kết quả: trục
            // phải được trả về đúng góc ban đầu kể cả khi phép thử không đạt.
            double a1Before = a1.CurrentPos;
            var av = McControl.Calib.VerifyAlignRotationAsync(1, 10.0, CancellationToken.None)
                .GetAwaiter().GetResult();
            Check("VerifyAlignRotationAsync chạy xong, không treo/crash", av != null);
            Check("Góc mô phỏng không gắn với trục thật → phép thử vòng xoay TỪ CHỐI, không ghi dấu bừa",
                av != null && !av.Ok, av?.Reason);
            Check("Thử xong thì trả trục A về đúng góc ban đầu", Near(a1.CurrentPos, a1Before),
                $"A1={a1.CurrentPos:0.###} (đầu {a1Before:0.###})");

            var avSmall = McControl.Calib.VerifyAlignRotationAsync(1, 0.2, CancellationToken.None)
                .GetAwaiter().GetResult();
            Check("Góc thử quá nhỏ thì từ chối ngay, không xoay trục vô ích", !avSmall.Ok, avSmall.Reason);

            // ── §6.5 Xác định hand-eye — gắp trên cơ cấu kẹp, vị trí Jog-dạy riêng (cách 25/09) ──
            //
            // KHÔNG hỏi "có từ chối không": câu đó đạt cả khi routine từ chối vì lý do khác (chưa về gốc, model
            // null, tay còn hàng). Hỏi ĐÚNG LÝ DO của từng biên, khẳng định trục KHÔNG nhúc nhích, và chạy cả
            // đường THUẬN trong cùng bài để biết những câu từ chối trên không phải "từ chối tất".
            var heModel = Core.Data.McDataManager.Model;
            var keepPick1 = heModel.HandEyePick_Nozzle1;
            var keepCam = heModel.HandEyeCamPos;
            double heX0 = McControl.Motion[AxisId.X].CurrentPos, heY0 = McControl.Motion[AxisId.Y2].CurrentPos;

            heModel.HandEyePick_Nozzle1 = new Core.Data.Model.TeachPoint();
            var heNoPick = McControl.Calib.HandEyeGoToPickAsync(1, CancellationToken.None).GetAwaiter().GetResult();
            Check("Chưa teach vị trí gắp trên cơ cấu kẹp → từ chối, lý do nêu ĐÚNG điểm thiếu và chỗ teach",
                !heNoPick.Ok && heNoPick.Reason != null && heNoPick.Reason.Contains("vị trí gắp") &&
                heNoPick.Reason.Contains("tay #1") && heNoPick.Reason.Contains("Teaching"), heNoPick.Reason);

            heModel.HandEyePick_Nozzle1 = keepPick1;
            heModel.HandEyeCamPos = new Core.Data.Model.TeachPoint();
            var heNoCam = McControl.Calib.HandEyeGoToPickAsync(1, CancellationToken.None).GetAwaiter().GetResult();
            Check("Chưa teach vị trí soi chi tiết → nút 1 cũng từ chối, lý do nêu đúng điểm đó",
                !heNoCam.Ok && heNoCam.Reason != null && heNoCam.Reason.Contains("soi chi tiết"), heNoCam.Reason);
            Check("Hai lần từ chối vì chưa teach đều KHÔNG chạy trục",
                Near(McControl.Motion[AxisId.X].CurrentPos, heX0) && Near(McControl.Motion[AxisId.Y2].CurrentPos, heY0),
                $"X={McControl.Motion[AxisId.X].CurrentPos:0.###} (đầu {heX0:0.###}), Y2={McControl.Motion[AxisId.Y2].CurrentPos:0.###} (đầu {heY0:0.###})");
            heModel.HandEyeCamPos = keepCam;

            // Bấm sai thứ tự: mỗi biên một lý do riêng, nêu TÊN nút còn thiếu.
            McControl.Calib.HandEyeClear();
            var heNoStart = McControl.Calib.HandEyePickAsync(1, CancellationToken.None).GetAwaiter().GetResult();
            Check("Bấm 2 trước 1 → từ chối, nêu tên nút 1", !heNoStart.Ok && heNoStart.Reason != null &&
                heNoStart.Reason.Contains("Tới vị trí gắp"), heNoStart.Reason);
            var heNoLoc = McControl.Calib.HandEyeLocateAsync(1, CancellationToken.None).GetAwaiter().GetResult();
            Check("Bấm 3 khi chưa bắt đầu → từ chối, nêu tên nút 1", !heNoLoc.Ok && heNoLoc.Reason != null &&
                heNoLoc.Reason.Contains("Tới vị trí gắp"), heNoLoc.Reason);
            var heNoCommit = McControl.Calib.HandEyeCommitAsync(1, CancellationToken.None).GetAwaiter().GetResult();
            Check("Bấm 5 khi chưa bắt đầu → không ghi hand-eye", !heNoCommit.Ok, heNoCommit.Reason);
            var heNoTarget = McControl.Calib.HandEyeCheckTargetAsync(1, CancellationToken.None).GetAwaiter().GetResult();
            Check("Bấm 7 khi chưa bắt đầu → từ chối", !heNoTarget.Ok, heNoTarget.Reason);
            var heNoHold = McControl.Calib.HandEyeReturnAsync(1, CancellationToken.None).GetAwaiter().GetResult();
            Check("Bấm nút trả khi tay không cầm gì → từ chối, nói đúng lý do đó",
                !heNoHold.Ok && heNoHold.Reason != null && heNoHold.Reason.Contains("không cầm gì"), heNoHold.Reason);

            // Đường THUẬN: nút 1 chạy được, và đứng đúng điểm đã dạy — KHÔNG phải ô tray nào.
            var heGo = McControl.Calib.HandEyeGoToPickAsync(1, CancellationToken.None).GetAwaiter().GetResult();
            var heAssyCell = heModel.AssyTray.CellPos(AxisId.Z1, 0, 0);
            var heInputCell = heModel.InputTray.CellPos(AxisId.Z1, 0, 0);
            Check("Nút 1 tới ĐÚNG điểm gắp đã dạy trên cơ cấu kẹp", heGo.Ok &&
                Near(McControl.Motion[AxisId.X].CurrentPos, keepPick1.X) &&
                Near(McControl.Motion[AxisId.Y2].CurrentPos, keepPick1.Y),
                heGo.Reason ?? $"X={McControl.Motion[AxisId.X].CurrentPos:0.###}/{keepPick1.X:0.###}, Y2={McControl.Motion[AxisId.Y2].CurrentPos:0.###}/{keepPick1.Y:0.###}");
            Check("Điểm gắp cơ cấu kẹp KHÔNG trùng ô nào của hai tray, và hai tay KHÔNG suy từ nhau",
                !Near(keepPick1.X, heAssyCell.X) && !Near(keepPick1.X, heInputCell.X) &&
                !Near(keepPick1.X, heModel.HandEyePick_Nozzle2.X) && !Near(keepPick1.Y, heModel.HandEyePick_Nozzle2.Y),
                $"gắp #1 ({keepPick1.X:0.###}, {keepPick1.Y:0.###}), ô assy X {heAssyCell.X:0.###}, ô đầu vào X {heInputCell.X:0.###}, gắp #2 ({heModel.HandEyePick_Nozzle2.X:0.###}, {heModel.HandEyePick_Nozzle2.Y:0.###})");

            // Đổi tay giữa chuỗi: lý do phải nêu TÊN TAY đang dở, không phải câu chung.
            var heWrongTool = McControl.Calib.HandEyePickAsync(2, CancellationToken.None).GetAwaiter().GetResult();
            Check("Đổi sang tay kia giữa chuỗi → từ chối, lý do nêu tay đang dở",
                !heWrongTool.Ok && heWrongTool.Reason != null && heWrongTool.Reason.Contains("tay #1"), heWrongTool.Reason);
            McControl.Calib.HandEyeClear();
        }

        /// <summary>
        /// Tạo biến toàn cục cho solution từ App (Tuấn yêu cầu 2026-09-13): danh mục đúng luật đặt tên
        /// của VisionMaster, chỉ tạo biến thiếu, không đổi biến đã có, lưu solution đúng một lần.
        /// </summary>
        /// <summary>
        /// Tool "Kiểm tra Output Flow": hợp đồng dựng đúng từ các hàm tạo lời gọi, và phép đối chiếu
        /// gọi đúng tên từng loại sai — thiếu, gõ sai tên, sai kiểu, khai thêm.
        /// </summary>
        private static void TestVmFlowOutputCheck()
        {
            Section("Kiểm tra Output Params của các Flow (VmOutCheck)");

            var names = new VisionOutputNames();
            var expects = Core.Controller.VmCycle.ExpectedFlowOutputs(names);
            Check("Hợp đồng phủ đúng ba Flow: LocateTop, AlignBottom_Tool1, AlignBottom_Tool2 (Tuấn 2026-10-08)",
                expects.Count == 3 && expects.Select(e => e.Flow).OrderBy(f => f).SequenceEqual(
                    new[] { names.FlowLocateTop, names.FlowAlignBottomTool1, names.FlowAlignBottomTool2 }.OrderBy(f => f)),
                string.Join(", ", expects.Select(e => e.Flow)));

            var locate = expects.FirstOrDefault(e => e.Flow == names.FlowLocateTop);
            Check("LocateTop đòi 9 output (Found + 3 echo + 5 của Script TopAbs)",
                locate != null && locate.Names.Count == 9, locate == null ? "không có" : locate.Names.Count.ToString());

            var align = expects.FirstOrDefault(e => e.Flow == names.FlowAlignBottomTool1);
            Check("AlignBottom_Tool1 đòi 17 output (Found + 3 echo + 5 tư thế + 8 của Group Cmd)",
                align != null && align.Names.Count == 17, align == null ? "không có" : align.Names.Count.ToString());
            Check("Trong đó có Found — cẩm nang từng đếm thiếu ô này",
                align != null && align.Names.Contains(names.OutFound));

            Check("Nhánh hiệu chuẩn chỉ đọc Found — đã nằm trong hợp đồng của cả ba Flow",
                expects.All(e => e.Names.Contains(names.OutFound)));

            // Bản khai đúng hết → sạch.
            var scripts = VmScriptContract.Build(names);
            Check("Hợp đồng Script phủ ba Script (TopAbs + Cmd hai tay)", scripts.Count == 3, scripts.Count.ToString());
            Check("TopAbs: 7 Input, 5 Output",
                scripts[0].Inputs.Count == 7 && scripts[0].Outputs.Count == 5);
            Check("Cmd: 12 Input, 9 Output",
                scripts[1].Inputs.Count == 12 && scripts[1].Outputs.Count == 9);

            var clean = new VmOutCheckReport();
            foreach (var e in expects)
            {
                var fake = Core.Controller.VmCycle.FakeFlowOutputs(e.Flow, names);
                VmOutCheck.CompareFlow(e, fake, clean);
                foreach (var sc in scripts) if (sc.Flow == e.Flow) VmOutCheck.CompareScript(sc, fake, clean);
            }
            Check("Solution khai đủ thì báo cáo sạch", clean.Clean && clean.Count(EVmOutState.Missing) == 0,
                clean.Count(EVmOutState.Ok) + " đạt");
            Check("Báo cáo in ra bảng Input của cả ba Script", clean.ScriptBlocks.Count == 3,
                clean.ScriptBlocks.Count.ToString());

            // Bản khai có lỗi: thiếu Found, gõ HandEyeDx thiếu đuôi, Status khai kiểu chuỗi, thừa một tên lạ.
            var expect = new VmFlowExpect { Flow = "AlignBottom_Tool1", ImageName = "ImageData" };
            foreach (var n in new[] { "Found", "ObjX_mm", "Status", "HandEyeDx_mm", "TopX_mm", "TopY_mm" })
                expect.Names.Add(n);

            var actual = new VmFlowOutputs { Flow = "AlignBottom_Tool1", InputParamCount = 3 };
            Action<string, int> declare = (nm, code) => actual.Declared.Add(new VmDeclaredOut
            {
                Name = nm, TypeCode = code, TypeText = VmOutCheck.TypeName(code), Numeric = VmOutCheck.IsNumeric(code)
            });
            declare("ObjX_mm", VmOutCheck.TypeFloat);
            declare("Status", 15);                    // chuỗi — App đọc không ra
            declare("HandEyeDx", VmOutCheck.TypeFloat);   // thiếu đuôi _mm
            declare("TopX_mm", VmOutCheck.TypeFloat);
            declare("ImageData", 1);                  // ảnh — không phải thừa
            declare("Score", VmOutCheck.TypeFloat);   // thừa thật, không giống tên nào

            var bad = new VmOutCheckReport();
            VmOutCheck.CompareFlow(expect, actual, bad);

            // HandEyeDx_mm cũng tính là thiếu: ô đang khai tên HandEyeDx, App đọc theo tên nên không lấy được.
            Check("Bắt được output còn thiếu (Found, HandEyeDx_mm, TopY_mm)", bad.Count(EVmOutState.Missing) == 3,
                bad.Count(EVmOutState.Missing).ToString());
            var typo = bad.Items.FirstOrDefault(i => i.State == EVmOutState.Typo);
            Check("Gọi đúng tên gõ thiếu đuôi đơn vị: HandEyeDx → HandEyeDx_mm",
                typo != null && typo.Name == "HandEyeDx" && typo.Suggest == "HandEyeDx_mm",
                typo == null ? "không thấy" : typo.Name + " → " + typo.Suggest);
            Check("Bắt được output khai sai kiểu (Status kiểu chuỗi)",
                bad.Count(EVmOutState.WrongType) == 1);
            Check("Output ảnh không bị kể là thừa",
                !bad.Items.Any(i => i.Name == "ImageData"));
            Check("Tên lạ không giống ai thì chỉ liệt kê, không đổ lỗi",
                bad.Items.Any(i => i.Name == "Score" && i.State == EVmOutState.Extra));
            Check("TopX_mm không bị đem gợi ý cho TopY_mm đang thiếu — nó nằm trong hợp đồng, đã khớp",
                bad.Items.Any(i => i.Name == "TopX_mm" && i.State == EVmOutState.Ok));
            Check("Input Params còn sót thì nhắc, không tính là lỗi",
                bad.Notes.Any(n => n.Contains("Input Params")));
            Check("Có lỗi thì báo cáo không sạch", !bad.Clean);

            // ── Script hỏng: bỏ trống Subscribe, gõ sai tên, thiếu dòng ──
            var sExpect = VmScriptContract.Build(names).First(e => e.Flow == names.FlowAlignBottomTool1);
            var flowIo = new VmFlowOutputs { Flow = names.FlowAlignBottomTool1 };
            var mod = new VmModuleIo { Name = "Script1" };   // tên module khác cẩm nang — vẫn phải tìm ra
            foreach (var nm in VmScriptContract.CmdIn)
            {
                if (nm == "SpaStatus") continue;                                     // thiếu hẳn dòng này
                if (nm == "DeltaTheta_deg")
                { mod.Inputs.Add(new VmIoInfo { Name = "DetalTheta_deg", TypeText = "float", Source = "Single Point Alignment1 · DeltaTheta" }); continue; }
                mod.Inputs.Add(new VmIoInfo
                {
                    Name = nm,
                    TypeText = "float",
                    Source = nm == "AlignSign" ? null : "module In · " + nm        // AlignSign bỏ trống ô Subscribe
                });
            }
            foreach (var nm in VmScriptContract.CmdOut) mod.Outputs.Add(new VmIoInfo { Name = nm, TypeText = "float" });
            flowIo.Modules.Add(new VmModuleIo { Name = "Echo0" });
            flowIo.Modules.Add(mod);

            var sRep = new VmOutCheckReport();
            VmOutCheck.CompareScript(sExpect, flowIo, sRep);

            Check("Tìm ra module Script dù tên module khác cẩm nang",
                VmOutCheck.FindScript(flowIo, sExpect) == mod);
            Check("Bắt được Input bỏ trống ô Subscribe (AlignSign)",
                sRep.Items.Any(i => i.Name == "AlignSign" && i.State == EVmOutState.Unbound));
            Check("Bắt được dòng Input thiếu hẳn (SpaStatus)",
                sRep.Items.Any(i => i.Name == "SpaStatus" && i.State == EVmOutState.Missing));
            Check("Gọi đúng tên gõ đảo chữ: DetalTheta_deg → DeltaTheta_deg",
                sRep.Items.Any(i => i.Name == "DetalTheta_deg" && i.State == EVmOutState.Typo &&
                                    i.Suggest == "DeltaTheta_deg"));
            Check("Output của Script khai đủ thì không kêu",
                !sRep.Items.Any(i => i.Flow.Contains("Output Params") && i.State != EVmOutState.Ok));
            Check("Chưa bind cũng làm báo cáo không sạch", !sRep.Clean);
            Check("Bảng Input in rõ dòng chưa bind",
                sRep.ScriptBlocks.Count == 1 && sRep.ScriptBlocks[0].Contains("CHƯA BIND"));

            // Tên VmIO do người dùng khai bị bọc %…% (máy thật 21/09).
            Check("Bóc %…% khỏi tên output: %TopX_mm% → TopX_mm", VmOutCheck.CleanIoName("%TopX_mm%") == "TopX_mm");
            Check("Tên không bọc thì để nguyên", VmOutCheck.CleanIoName("InputImage") == "InputImage");
            Check("Một dấu % lẻ không bị cắt bừa", VmOutCheck.CleanIoName("%") == "%" &&
                                                   VmOutCheck.CleanIoName("50%") == "50%");

            // Không tìm ra Script thì báo cáo in luôn tên output mà module Script đang khai.
            var odd = new VmFlowOutputs { Flow = names.FlowAlignBottomTool2 };
            var oddScript = new VmModuleIo { Name = "Script1" };
            oddScript.Outputs.Add(new VmIoInfo { Name = "Foo", TypeText = "float" });
            odd.Modules.Add(oddScript);
            var oddRep = new VmOutCheckReport();
            VmOutCheck.CompareScript(VmScriptContract.Build(names).First(e => e.Flow == names.FlowAlignBottomTool2), odd, oddRep);
            Check("Không thấy Script thì báo cáo nói module Script đang khai những gì",
                oddRep.FlowErrors.Count == 1 && oddRep.FlowErrors[0].Contains("Script1 khai: Foo"),
                oddRep.FlowErrors.Count == 0 ? "không có dòng lỗi" : oddRep.FlowErrors[0]);

            // Không duyệt được module thì nói thẳng, không quy thành thiếu 21 dòng.
            var noMod = new VmOutCheckReport();
            VmOutCheck.CompareScript(sExpect, new VmFlowOutputs { Flow = names.FlowAlignBottomTool1 }, noMod);
            Check("Không duyệt được module thì báo một dòng, không kể là thiếu",
                noMod.Items.Count == 0 && noMod.Notes.Count == 1);

            // Flow không có trong solution.
            var gone = new VmOutCheckReport();
            VmOutCheck.CompareFlow(expect, VmFlowOutputs.Fail("AlignBottom_Tool1", "không tìm thấy Flow"), gone);
            Check("Flow không có trong solution thì báo riêng, không kể là thiếu 6 output",
                gone.FlowErrors.Count == 1 && gone.Items.Count == 0);

            Check("Chữ ký gợi ý tên: đảo chữ Detal ↔ Delta vẫn nhận ra",
                VmOutCheck.SuggestFor("DetalTheta_deg", new[] { "DeltaTheta_deg" }) == "DeltaTheta_deg");
            Check("Tên khác hẳn thì không gợi ý bừa",
                VmOutCheck.SuggestFor("CmdX_mm", new[] { "PlacedRunId" }) == null);
        }

        private static void TestGlobalVarCreate()
        {
            Section("Tạo biến toàn cục cho solution VisionMaster");

            var sys = Core.Data.McDataManager.SysData;
            var specs = Core.Communication.VisionGlobalVarCatalog.Build(sys);
            var names = specs.Select(s => s.Name).ToList();

            Check("Danh mục không trùng tên", names.Distinct(StringComparer.Ordinal).Count() == names.Count, names.Count + " biến");
            string why = null;
            var badName = specs.FirstOrDefault(s => !Core.Communication.GlobalVarSync.IsValidName(s.Name, out why));
            Check("Mọi tên đúng luật đặt tên của VisionMaster", badName == null, badName == null ? "" : badName.Name + ": " + why);
            Check("Mọi biến kiểu float, riêng cờ IsTriggerLocate kiểu int (Module rẽ nhánh); giá trị đầu 0",
                specs.All(s => s.Type == (s.Name == VmVars.IsTriggerLocate ? "int" : "float") && s.InitialValue == "0"));
            var vo = sys.VisionOutputs;
            Check("Danh mục đúng 13 biến", specs.Count == 13, specs.Count + " biến: " + string.Join(", ", names));
            Check("Có cờ IsTriggerLocate để Flow rẽ nhánh soi / hiệu chuẩn", names.Contains(VmVars.IsTriggerLocate));
            Check("Có đủ bốn biến trục và RunId App ghi trước mọi Flow",
                new[] { vo.GvAxisX, vo.GvAxisY2, vo.GvAxisA1, vo.GvAxisA2, VmVars.RunId }.All(n => names.Contains(n)));
            Check("Có đủ tám biến của lần đặt", VmVars.Place.All(n => names.Contains(n)));
            Check("Không còn biến nào theo tay (hằng số nằm ở model)",
                !names.Any(n => n.EndsWith("1", StringComparison.Ordinal) || n.EndsWith("2", StringComparison.Ordinal))
                || names.Where(n => n.EndsWith("1", StringComparison.Ordinal) || n.EndsWith("2", StringComparison.Ordinal))
                        .All(n => n == vo.GvAxisA1 || n == vo.GvAxisA2 || n == vo.GvAxisY2),
                string.Join(", ", names));

            var fv = McControl.Vision as FakeVision;
            if (fv == null) { Check("Chạy với vision giả", false); return; }

            fv.GlobalVars.Clear();
            fv.GlobalVars[vo.GvAxisX] = new KeyValuePair<string, string>("float", "12.5");
            fv.GlobalVars[vo.GvAxisY2] = new KeyValuePair<string, string>("float", "0");
            fv.GlobalVars[vo.GvAxisA1] = new KeyValuePair<string, string>("int", "0");     // sai kiểu
            int saveBefore = fv.SolutionSaveCount;

            var dry = McControl.SyncVisionGlobalVariablesAsync(false).GetAwaiter().GetResult();
            Check("Kiểm tra không tạo gì và không lưu solution",
                !dry.Applied && fv.GlobalVars.Count == 3 && fv.SolutionSaveCount == saveBefore, dry.ToText());
            Check("Kiểm tra báo biến sai kiểu",
                dry.Items.Any(i => i.Spec.Name == vo.GvAxisA1 && i.State == Core.Communication.EGlobalVarState.WrongType));
            Check("Kiểm tra đếm đúng số biến thiếu",
                dry.Count(Core.Communication.EGlobalVarState.Missing) == specs.Count - 3,
                dry.Count(Core.Communication.EGlobalVarState.Missing) + " / " + (specs.Count - 3));

            var made = McControl.SyncVisionGlobalVariablesAsync(true).GetAwaiter().GetResult();
            Check("Tạo đủ mọi biến còn thiếu, không lỗi",
                made.Count(Core.Communication.EGlobalVarState.Created) == specs.Count - 3 &&
                made.Count(Core.Communication.EGlobalVarState.Failed) == 0 && string.IsNullOrEmpty(made.Error), made.ToText());
            Check("Không đổi giá trị biến đã có", fv.GlobalVars[vo.GvAxisX].Value == "12.5", fv.GlobalVars[vo.GvAxisX].Value);
            Check("Không tự sửa biến sai kiểu, chỉ báo", fv.GlobalVars[vo.GvAxisA1].Key == "int");
            Check("Lưu solution đúng một lần", fv.SolutionSaveCount == saveBefore + 1, "lưu " + (fv.SolutionSaveCount - saveBefore) + " lần");

            Check("Biến mới tạo đúng kiểu danh mục, giá trị đầu 0 — solution không cất hằng số nào",
                specs.Where(s => s.Name != vo.GvAxisX && s.Name != vo.GvAxisY2 && s.Name != vo.GvAxisA1)
                     .All(s => fv.GlobalVars[s.Name].Key == s.Type && fv.GlobalVars[s.Name].Value == "0"));

            var again = McControl.SyncVisionGlobalVariablesAsync(true).GetAwaiter().GetResult();
            Check("Chạy lần hai không tạo thêm và không lưu lại",
                again.Count(Core.Communication.EGlobalVarState.Created) == 0 && fv.SolutionSaveCount == saveBefore + 1, again.ToText());

            // Trả lại solution giả đã hiệu chuẩn cho các bài chạy Auto phía sau.
            fv.SeedVmSolution(specs);

            // Cờ rẽ nhánh (Tuấn 2026-10-08): ba Flow, lần soi ghi 1, lần nạp điểm hiệu chuẩn ghi 0.
            fv.SetGlobal(VmVars.IsTriggerLocate, 7);
            var locate = McControl.Vision.CallFlowAsync(VmCycle.LocateRequest(vo, VmCycle.AxisGlobals(McControl.Motion, vo, 9101)),
                CancellationToken.None).GetAwaiter().GetResult();
            Check("Soi LocateTop ghi IsTriggerLocate = 1", locate.Ok && fv.GetGlobal(VmVars.IsTriggerLocate) == 1,
                (locate.Ok ? "" : locate.Error + " — ") + fv.GetGlobal(VmVars.IsTriggerLocate));
            McControl.Vision.TriggerCalibAsync(Core.Communication.EVisionProcedure.TopDownCalib, new Core.Communication.AxisSnapshot(),
                CancellationToken.None).GetAwaiter().GetResult();
            Check("Hiệu chuẩn top-down ghi IsTriggerLocate = 0", fv.GetGlobal(VmVars.IsTriggerLocate) == 0,
                fv.GetGlobal(VmVars.IsTriggerLocate).ToString());
            Check("Cờ khai int lưu số nguyên", fv.GlobalVars[VmVars.IsTriggerLocate].Key == "int" &&
                fv.GlobalVars[VmVars.IsTriggerLocate].Value == "0", fv.GlobalVars[VmVars.IsTriggerLocate].ToString());

            var alignG = VmCycle.AxisGlobals(McControl.Motion, vo, 9102);
            VmCycle.AddPlaceGlobals(alignG, null, null, 1);
            var align = McControl.Vision.CallFlowAsync(VmCycle.AlignBottomRequest(vo, 1, alignG, false),
                CancellationToken.None).GetAwaiter().GetResult();
            Check("Soi AlignBottom_Tool1 ghi IsTriggerLocate = 1", align.Ok && fv.GetGlobal(VmVars.IsTriggerLocate) == 1,
                (align.Ok ? "" : align.Error + " — ") + fv.GetGlobal(VmVars.IsTriggerLocate));
            McControl.Vision.TriggerCalibAsync(Core.Communication.EVisionProcedure.AlignCalibTool1, new Core.Communication.AxisSnapshot(),
                CancellationToken.None).GetAwaiter().GetResult();
            Check("Hiệu chuẩn tâm xoay tay #1 ghi IsTriggerLocate = 0", fv.GetGlobal(VmVars.IsTriggerLocate) == 0,
                fv.GetGlobal(VmVars.IsTriggerLocate).ToString());
        }

        private static void TestAutoCycle()
        {
            Section("Chu trình Auto");

            var io = (FakeIo)McControl.Io;

            // Nhóm kiểm tra trước cố tình để lại alarm. Vào trạng thái ALARM rồi thì chỉ RESET
            // mới ra được — đúng quy tắc §7.4, và cũng đúng thao tác của operator ngoài máy.
            AlarmManager.ForceClearAll();
            McControl.RequestReset();
            WaitUntil(() => McControl.Status.State != EMcState.Alarm, 3000);
            Check("RESET đưa máy ra khỏi trạng thái LỖI",
                McControl.Status.State != EMcState.Alarm, McControl.Status.StateText);

            if (!McControl.Status.OrgOk)
            {
                McControl.RequestOrigin();
                WaitUntil(() => McControl.Status.State == EMcState.Homing, 2000);
                WaitUntil(() => McControl.Status.State != EMcState.Homing, 40000);
            }
            Check("Đã về gốc trước khi chạy Auto", McControl.Status.OrgOk);

            var model = Core.Data.McDataManager.Model;
            Check("Chế độ mô phỏng đã nạp model chạy được",
                model.InputTray.IsTaught && model.AssyTray.IsTaught && model.Calib.IsUsable, model.Name);

            var run = Core.Data.McDataManager.RunData;
            run.ResetCounters();
            run.InputTray.ResetAll(ECellState.Loaded);
            run.AssyTray.ResetAll(ECellState.Empty);
            run.Nozzle1.HasProduct = false;
            run.Nozzle2.HasProduct = false;
            run.AutoState = EAutoState.S00_Ready;

            // START khi chìa còn ở MANUAL thì phải từ chối, không âm thầm chạy.
            McControl.RequestStart();
            Thread.Sleep(200);
            Check("Chìa ở MANUAL thì START bị từ chối",
                McControl.Status.State != EMcState.Running, McControl.Status.Message);
            McControl.Status.Message = "";
            io.SimPress(IoMap.BtnGoB2);
            Thread.Sleep(200);
            Check("Ở MANUAL thì nút Start cứng không chạy máy, không vào sẵn sàng — bảo chọn AUTO",
                McControl.Status.State != EMcState.Running && McControl.Status.State != EMcState.Armed &&
                (McControl.Status.Message ?? "").Contains("AUTO"), McControl.Status.StateText + " — " + McControl.Status.Message);

            McControl.RequestSetMode(EMcMode.Auto);
            WaitUntil(() => McControl.Status.Mode == EMcMode.Auto, 2000);
            Check("Chọn AUTO là vào luôn trạng thái sẵn sàng, không phải bấm thêm nút (Tuấn 2026-09-30)",
                WaitUntil(() => McControl.Status.State == EMcState.Armed, 1000),
                McControl.Status.StateText + " — " + McControl.Status.Message);

            // Đầu hút có gờ ôm lấy sản phẩm: hạ xuống hút trong lúc A còn ở góc xoay của lệnh đặt trước đó thì
            // gờ không ăn khớp, chân không không đạt và không nhấc được sản phẩm (Tuấn nêu 2026-09-23). Đọc góc
            // NGAY LÚC van hút mở, không canh giờ — xem McFunction.MoveToCellForPickAsync. So với GÓC HÚT đã
            // teach của đúng tay đó, không so với 0: góc hút là số cơ khí, không có lý do rơi đúng vào 0.
            int pickCount = 0;
            double worstPickA = 0, worstReleaseA = 0;
            io.AfterSetDo = (bit, on) =>
            {
                int nz = bit == IoMap.DoVacuum1 ? 1 : bit == IoMap.DoVacuum2 ? 2 : 0;
                if (nz == 0) return;
                double a = McControl.Motion[nz == 1 ? AxisId.A1 : AxisId.A2].CurrentPos;
                if (on)
                {
                    pickCount++;
                    double off = AxisAngle.Distance(a, model.PickAngle(nz));
                    if (off > worstPickA) worstPickA = off;
                }
                else
                {
                    double off = AxisAngle.Distance(a, 0);
                    if (off > worstReleaseA) worstReleaseA = off;
                }
            };

            McControl.RequestStart();
            bool started = WaitUntil(() => McControl.Status.State == EMcState.Running, 3000);
            Check("Chìa AUTO + START thì chu trình chạy", started, McControl.Status.Message);

            // Đèn ba nút Start: báo trạng thái hoạt động, sáng khi đang chạy Auto (Tuấn yêu
            // cầu, 2026-09-04) — xem McControl.UpdateOutputs. DO này do UpdateOutputs() ghi mỗi
            // tick quét, có thể trễ tối đa một tick sau khi Status.State đổi (Routine_Auto đổi
            // State độc lập với vòng quét) — WaitUntil thay vì đọc tức thời, tránh test giả-lỗi.
            Check("Đèn ba nút Start sáng khi đang chạy Auto",
                WaitUntil(() => io.GetDo(IoMap.DoLampStartBtn) && io.GetDo(IoMap.DoLampStartBtnB) &&
                                io.GetDo(IoMap.DoLampStartBtnB2), 500));
            // Đèn nút STOP sáng mọi lúc trừ khi đang chạy (Tuấn đổi 2026-09-26).
            Check("Đèn nút STOP tắt khi đang chạy Auto",
                WaitUntil(() => !io.GetDo(IoMap.DoLampResetBtn), 500));

            // Đèn CAM1 (bottom-up) / CAM2 (top-down) bật lúc trigger Vision, tắt sau khi có kết
            // quả (Tuấn yêu cầu, 2026-09-04) — xem Routine_Auto.CallVmAsync.
            bool camLampSeen = WaitUntil(
                () => io.GetDo(IoMap.DoLampCam1) || io.GetDo(IoMap.DoLampCam2), 5000);
            Check("Đèn CAM bật lên lúc trigger Vision trong chu trình Auto", camLampSeen);

            bool made = WaitUntil(() => run.CountOk >= 2, 120000);
            Check("Đặt xong 2 sản phẩm đầu tiên", made,
                $"OK={run.CountOk} NG={run.CountNg} bước={run.AutoState} — {McControl.Status.Message}");
            Check("Không phát sinh alarm trong lúc chạy", !AlarmManager.HasError,
                string.Join("; ", AlarmManager.ActiveAlarms.Select(a => a.Code + " " + a.MessageVi)));

            int okCells = run.AssyTray.Cells.ToArray().Count(c => c == ECellState.Ok);
            Check("Ô tray lắp ráp được đánh dấu OK", okCells >= 2, okCells + " ô");
            Check("Ô tray đầu vào đã dùng được đánh dấu",
                run.InputTray.Cells.ToArray().Count(c => c == ECellState.Ok) >= 2);
            Check("Bộ đếm theo tray (card tray trang Main) có cộng khi đặt xong",
                run.InputCountOk >= 2 && run.AssyCountOk >= 2,
                $"đầu vào OK {run.InputCountOk}, lắp ráp OK {run.AssyCountOk}");

            // Ô tray lắp ráp quét trước OK mà còn trống thì trang Main tô xanh nhạt như ô có hàng (Tuấn 2026-10-07).
            var scanned = McControl.AutoRoutine.AssyScannedCells;
            var scanVm = new ViewModels.TrayGridViewModel("kiểm tra", "", null);
            scanVm.Sync(run.AssyTray, scanned);
            // ViewModel chỉ ra trạng thái hiển thị (Look), màu do converter TrayCellBrush của App.xaml quyết (MVVM, 2026-10-08).
            var scanCells = scanVm.Groups[0].Cells;
            var scannedEmpty = scanCells.Where(c => c.State == ECellState.Empty && c.IsScanned).ToList();
            Check("Ô tray lắp ráp đã quét OK, còn trống → Look = Scanned",
                scannedEmpty.Count > 0 && scannedEmpty.All(c => c.Look == Models.ETrayCellLook.Scanned),
                $"{scanned.Length} ô đã quét, {scannedEmpty.Count} ô còn trống");
            Check("Ô đã đặt OK vẫn là Look = Ok dù đã quét",
                scanCells.Where(c => c.State == ECellState.Ok).All(c => c.Look == Models.ETrayCellLook.Ok));
            var cellBrush = System.Windows.Application.Current?.TryFindResource("TrayCellBrush") as System.Windows.Data.IValueConverter;
            System.Windows.Media.Color? Paint(Models.ETrayCellLook look) =>
                (cellBrush?.Convert(look, typeof(System.Windows.Media.Brush), null, System.Globalization.CultureInfo.InvariantCulture)
                    as System.Windows.Media.SolidColorBrush)?.Color;
            var lookColors = Enum.GetValues(typeof(Models.ETrayCellLook)).Cast<Models.ETrayCellLook>()
                .ToDictionary(l => l, Paint);
            Check("Converter TrayCellBrush có màu cho mọi trạng thái ô",
                cellBrush != null && lookColors.Values.All(c => c.HasValue),
                string.Join(", ", lookColors.Select(kv => kv.Key + "=" + kv.Value)));
            Check("Ô đã quét tô xanh nhạt như ô có hàng, khác ô trống và ô OK",
                lookColors[Models.ETrayCellLook.Scanned] == lookColors[Models.ETrayCellLook.Loaded] &&
                lookColors[Models.ETrayCellLook.Scanned] != lookColors[Models.ETrayCellLook.Empty] &&
                lookColors[Models.ETrayCellLook.Scanned] != lookColors[Models.ETrayCellLook.Ok]);
            Check("Cycle time có được ghi lại", run.LastCycleTimeSec > 0,
                $"{run.LastCycleTimeSec:0.00} s");
            Check("Cycle time một sản phẩm và trung bình có được ghi lại",
                run.LastProductTimeSec > 0 && run.AvgProductTimeSec > 0 && run.LastProductTimeSec <= run.LastCycleTimeSec,
                $"sp trước {run.LastProductTimeSec:0.00} s, TB {run.AvgProductTimeSec:0.00} s ({run.RecentProductTimes.Count} sp), lượt {run.LastCycleTimeSec:0.00} s");

            // Trung bình CycleAvgWindow sản phẩm (Tuấn chốt 2026-09-28, 25 → 40 ngày 2026-10-06): thời gian lượt ÷ số sản
            // phẩm, chỉ giữ ngần ấy cái gần nhất.
            int win = Core.Data.RunningData.CycleAvgWindow;
            var ct = new Core.Data.RunningData();
            for (int i = 0; i < win / 2 + 1; i++) ct.RecordCycle(20, 2);
            ct.RecordCycle(30, 1);
            ct.RecordCycle(5, 0);                       // lượt không ra sản phẩm nào: bỏ qua
            Check("CT trung bình: lượt 2 sp chia đôi, giữ đúng " + win + " sp gần nhất, lượt 0 sp không tính",
                ct.RecentProductTimes.Count == win && Math.Abs(ct.LastProductTimeSec - 30) < 1e-9 &&
                Math.Abs(ct.AvgProductTimeSec - ((win - 1) * 10 + 30) / (double)win) < 1e-9,
                $"{ct.RecentProductTimes.Count} sp, sp trước {ct.LastProductTimeSec:0.00}, TB {ct.AvgProductTimeSec:0.000}");
            // Bộ đếm theo tray (Tuấn 2026-10-07): nạp lại tray không xoá, chỉ Clear sản lượng mới xoá.
            ct.InputCountOk = 5; ct.InputCountNg = 1; ct.AssyCountOk = 5; ct.AssyCountNg = 2;
            ct.InputTray.ResetAll(ECellState.Loaded);
            ct.AssyTray.ResetAll(ECellState.Empty);
            Check("Nạp lại tray không xoá bộ đếm theo tray",
                ct.InputCountOk == 5 && ct.InputCountNg == 1 && ct.AssyCountOk == 5 && ct.AssyCountNg == 2);
            ct.ResetCounters();
            Check("Clear bộ đếm thì xoá cả cycle time trung bình",
                ct.RecentProductTimes.Count == 0 && ct.AvgProductTimeSec == 0 && ct.LastProductTimeSec == 0);
            Check("Clear bộ đếm thì xoá bộ đếm theo tray",
                ct.InputCountOk == 0 && ct.InputCountNg == 0 && ct.AssyCountOk == 0 && ct.AssyCountNg == 0);

            io.AfterSetDo = null;
            Check("Mọi lần hút đều xảy ra lúc trục A ở đúng GÓC HÚT đã teach của tay đó",
                pickCount > 0 && worstPickA < 0.05,
                $"{pickCount} lần hút, lệch nhiều nhất {worstPickA:0.###}° (A1 teach {model.PickAngle(1):0.##}°, A2 teach {model.PickAngle(2):0.##}°)");
            // Không có dòng này thì bài trên có thể đạt một cách rỗng — nếu chu trình chẳng bao giờ xoay A thì
            // lúc nào A cũng ở 0 sẵn, chẳng chứng minh được gì.
            Check("Có đặt ở góc bù khác 0, nên phép kiểm trên không rỗng",
                worstReleaseA > 1.0, $"góc xa 0 nhất lúc nhả: {worstReleaseA:0.###}°");

            // STOP (bit RUN, Tuấn 2026-09-29): mọi trục dừng ngay, chu trình đứng nguyên chỗ; Start chạy tiếp đúng chỗ dở.
            McControl.RequestStop();
            Check("STOP khi đang chạy thì TẠM DỪNG ngay, không chạy nốt tới checkpoint",
                WaitUntil(() => McControl.Status.State == EMcState.Held, 1000), McControl.Status.StateText);
            Thread.Sleep(300);
            var heldPos = McControl.Motion.AxisList.Select(a => a.CurrentPos).ToArray();
            int okHeld = run.CountOk;
            var stepHeld = run.AutoState;
            Thread.Sleep(800);
            bool still = McControl.Motion.AxisList.Select((a, i) => Math.Abs(a.CurrentPos - heldPos[i]) < 1e-6).All(b => b);
            Check("Tạm dừng: không trục nào nhúc nhích, chu trình không làm bước mới",
                still && !McControl.Motion.AnyBusy && run.CountOk == okHeld && run.AutoState == stepHeld &&
                McControl.Status.State == EMcState.Held, $"bước {stepHeld} → {run.AutoState}, OK {okHeld} → {run.CountOk}");
            Check("Tạm dừng: đèn nút STOP sáng, đèn tháp vàng",
                WaitUntil(() => io.GetDo(IoMap.DoLampResetBtn) && McControl.Status.TowerLamp == ETowerLamp.Yellow, 500),
                McControl.Status.TowerLamp.ToString());
            io.SimPress(IoMap.BtnArmA1);     // nút cụm A — bốn nút Start như nhau (2026-10-16)
            Check("Tạm dừng rồi bấm Start (nút cụm A #1) thì chạy tiếp",
                WaitUntil(() => McControl.Status.State == EMcState.Running, 1000), McControl.Status.Message);
            Check("Chạy tiếp làm ra thêm sản phẩm, không báo lỗi nào",
                WaitUntil(() => run.CountOk > okHeld, 120000) && !AlarmManager.HasError,
                $"OK {okHeld} → {run.CountOk} — {McControl.Status.Message}");

            // Rèm sáng bị cắt lúc đang chạy (Tuấn 2026-10-01): mọi trục dừng như STOP, không alarm; rèm còn cắt thì Start
            // không chạy tiếp; rèm thông rồi bấm Start thì chạy tiếp đúng chỗ dở, không cần RESET.
            var sysCurtain = Core.Data.McDataManager.SysData;
            bool savedCurtain = sysCurtain.HasLightCurtain;
            sysCurtain.HasLightCurtain = true;
            io.SimSet(IoMap.LightCurtainOk, false);
            Check("Rèm sáng bị cắt khi đang chạy thì TẠM DỪNG ngay, không alarm",
                WaitUntil(() => McControl.Status.State == EMcState.Held, 1000) && !AlarmManager.HasError && AlarmManager.ActiveCount == 0,
                McControl.Status.StateText + " — " + McControl.Status.Message);
            Thread.Sleep(300);
            var curtainPos = McControl.Motion.AxisList.Select(a => a.CurrentPos).ToArray();
            int okCurtain = run.CountOk;
            var stepCurtain = run.AutoState;
            Thread.Sleep(500);
            Check("Rèm bị cắt: không trục nào nhúc nhích, chu trình không làm bước mới",
                McControl.Motion.AxisList.Select((a, i) => Math.Abs(a.CurrentPos - curtainPos[i]) < 1e-6).All(b => b) &&
                !McControl.Motion.AnyBusy && run.CountOk == okCurtain && run.AutoState == stepCurtain,
                $"bước {stepCurtain} → {run.AutoState}");
            io.SimPress(IoMap.BtnArmA2);
            Thread.Sleep(300);
            Check("Rèm còn bị cắt thì Start không chạy tiếp, báo rõ vì rèm",
                McControl.Status.State == EMcState.Held && McControl.Status.Message.Contains("rèm"), McControl.Status.Message);
            io.SimSet(IoMap.LightCurtainOk, true);
            Thread.Sleep(200);
            Check("Rèm thông trở lại thì máy KHÔNG tự chạy — vẫn tạm dừng chờ Start",
                McControl.Status.State == EMcState.Held && run.AutoState == stepCurtain, McControl.Status.StateText);
            io.SimPress(IoMap.BtnGoB2);
            Check("Rèm thông rồi bấm Start thì chạy tiếp đúng chỗ dở, không cần RESET",
                WaitUntil(() => McControl.Status.State == EMcState.Running, 1000) &&
                WaitUntil(() => run.AutoState != stepCurtain || run.CountOk > okCurtain, 30000) && !AlarmManager.HasError,
                $"bước {stepCurtain} → {run.AutoState} — {McControl.Status.Message}");
            sysCurtain.HasLightCurtain = savedCurtain;

            // Dừng ở checkpoint (đường của chuyển MANUAL lúc đang chạy): chạy nốt bước đang làm rồi thoát, KHÔNG dừng giữa chừng.
            McControl.RequestPause();
            Check("Dừng ở checkpoint: chuyển sang trạng thái đang dừng",
                WaitUntil(() => McControl.Status.State == EMcState.Pausing ||
                                McControl.Status.State == EMcState.Paused, 2000),
                McControl.Status.StateText);

            bool paused = WaitUntil(() => McControl.Status.State == EMcState.Paused, 30000);
            Check("Chạy nốt tới checkpoint rồi mới dừng", paused, McControl.Status.Message);
            Check("Dừng ở ranh giới giữa hai bước, không dừng giữa hành trình",
                !run.UnsafePause);
            Check("Đèn ba nút Start tắt khi chu trình đã dừng ở checkpoint",
                WaitUntil(() => !io.GetDo(IoMap.DoLampStartBtn) && !io.GetDo(IoMap.DoLampStartBtnB) &&
                                !io.GetDo(IoMap.DoLampStartBtnB2), 500));
            Check("Đèn nút STOP sáng lại khi chu trình đã dừng",
                WaitUntil(() => io.GetDo(IoMap.DoLampResetBtn), 500));
            Check("Dừng thì đèn tháp vàng",
                WaitUntil(() => McControl.Status.TowerLamp == ETowerLamp.Yellow, 500), McControl.Status.TowerLamp.ToString());
            Check("Tiến độ được ghi xuống đĩa", Core.Data.McDataManager.RunData.SavedAt > DateTime.MinValue);

            // Tray đã vào vùng làm việc thì cảm biến "có tray" mất tín hiệu — đó là chuyện BÌNH THƯỜNG của
            // máy này, không phải mất tray (Tuấn nêu 2026-09-24). Dựng lại đúng tình huống đó: dời vị trí chờ
            // ra xa chỗ hai trục hàng đang đứng, rồi tắt cả hai cảm biến. Trước khi sửa, START ở đây sẽ dừng
            // ngay với "không có tray đầu vào".
            string rowIn = model.InputTray.AxisRow, rowAssy = model.AssyTray.AxisRow;
            var cfgIn = Core.Data.McDataManager.SysData.GetAxis(rowIn);
            var cfgAssy = Core.Data.McDataManager.SysData.GetAxis(rowAssy);
            double sbIn = cfgIn.StandbyPos, sbAssy = cfgAssy.StandbyPos;
            cfgIn.StandbyPos = McControl.Motion[rowIn].CurrentPos + 50;
            cfgAssy.StandbyPos = McControl.Motion[rowAssy].CurrentPos + 50;
            io.SimSet(IoMap.TrayIn1Present, false);
            io.SimSet(IoMap.TrayAssyPresent, false);

            int okBefore = run.CountOk;
            McControl.RequestStart();
            Check("Bấm START lại thì chạy tiếp",
                WaitUntil(() => McControl.Status.State == EMcState.Running, 3000), McControl.Status.Message);
            Check("Tray đã vào vùng làm việc, cảm biến tray OFF — máy KHÔNG dừng oan",
                McControl.Status.State == EMcState.Running &&
                !(McControl.Status.Message ?? "").Contains("không có tray"), McControl.Status.Message);
            Check("Chạy tiếp làm ra thêm sản phẩm",
                WaitUntil(() => run.CountOk > okBefore, 120000),
                $"trước {okBefore}, sau {run.CountOk}");

            cfgIn.StandbyPos = sbIn;
            cfgAssy.StandbyPos = sbAssy;
            io.SimSet(IoMap.TrayIn1Present, true);
            io.SimSet(IoMap.TrayAssyPresent, true);

            McControl.RequestPause();
            WaitUntil(() => McControl.Status.State == EMcState.Paused, 30000);
            Check("Đèn CAM tắt lại khi không có trigger Vision nào đang chạy",
                !io.GetDo(IoMap.DoLampCam1) && !io.GetDo(IoMap.DoLampCam2));

            // Hút hụt lần đầu rồi hút được ở lần thử lại: 5001 phải hết nguyên nhân. Trước 26/09 cờ "còn nguyên nhân"
            // của 5001 chỉ được gỡ khi hết cả số lần thử, nên hút được ở lần sau — hay bấm STOP giữa lúc đang
            // hút lại, đúng tình huống Tuấn gặp — thì 5001 kẹt lại và RESET không bao giờ xoá được.
            int vacOns = 0;
            bool shownWhileRetrying = false;
            io.PickSuccessRate = 0;
            io.AfterSetDo = (bit, on) =>
            {
                if (!on || (bit != IoMap.DoVacuum1 && bit != IoMap.DoVacuum2)) return;
                vacOns++;
                if (vacOns == 1) io.PickSuccessRate = 1.0;      // chỉ hụt đúng lần đầu
                else if (vacOns == 2) shownWhileRetrying = AlarmManager.IsActive(AlarmCode.PickFailed);
            };
            McControl.RequestStart();
            WaitUntil(() => McControl.Status.State == EMcState.Running, 3000);
            bool retried = WaitUntil(() => vacOns >= 2, 60000);
            McControl.RequestPause();
            WaitUntil(() => McControl.Status.State == EMcState.Paused, 30000);
            io.AfterSetDo = null;
            io.PickSuccessRate = 1.0;
            Check("Hút hụt thì báo 5001 trong lúc hút lại", retried && shownWhileRetrying,
                vacOns + " lần mở van hút — " + McControl.Status.Message);
            // Máy tự chạy tiếp sau hút hụt nên 5001 tự rời danh sách, không cần RESET (Tuấn 2026-09-29).
            Check("Hết bước hút thì 5001 tự rời danh sách, không chờ RESET",
                !AlarmManager.IsActive(AlarmCode.PickFailed) && AlarmManager.PendingReset == null,
                AlarmManager.PendingReset?.Code.ToString());

            // Warning làm máy dừng thì vẫn phải RESET xác nhận rồi mới chạy tiếp (Tuấn chốt 2026-09-26) — 5003 đặt hỏng.
            // Không vào Armed thật: thoát Armed phải gạt về Manual, máy ra Idle chứ không về Paused như bài sau cần.
            AlarmManager.RaiseEvent(AlarmCode.PlaceFailed, "SelfTest");
            McControl.RequestArm();
            Check("Còn lỗi chưa RESET thì nút START mềm bị chặn, báo bấm RESET",
                WaitUntil(() => (McControl.Status.Message ?? "").Contains("bấm RESET trước"), 2000) &&
                McControl.Status.State == EMcState.Paused, McControl.Status.Message);
            McControl.Status.Message = "";
            io.SimPress(IoMap.BtnGoB1);
            Check("Còn lỗi chưa RESET thì nút Start cứng lúc máy đứng cũng bị chặn — không vào Armed, không chạy",
                WaitUntil(() => (McControl.Status.Message ?? "").Contains("bấm RESET trước"), 2000) &&
                McControl.Status.State == EMcState.Paused, McControl.Status.StateText + " — " + McControl.Status.Message);
            McControl.RequestReset();
            Check("RESET xoá 5003", WaitUntil(() => !AlarmManager.IsActive(AlarmCode.PlaceFailed), 2000));
            Check("RESET xong thì không còn lỗi nào chờ xác nhận", AlarmManager.PendingReset == null,
                AlarmManager.PendingReset?.Code.ToString());

            // Tạm dừng rồi chuyển MANUAL: bỏ chu trình — ở Manual còn jog được, Start sau đó mà phát lại lệnh dở là va.
            McControl.RequestStart();
            WaitUntil(() => McControl.Status.State == EMcState.Running, 3000);
            // Trục giả chạy nhanh tối đa: chờ lâu thì chu trình chạy hết tray, sang chờ đổi tray trước khi STOP tới.
            Thread.Sleep(50);
            McControl.RequestStop();
            WaitUntil(() => McControl.Status.State == EMcState.Held, 1000);
            McControl.RequestSetMode(EMcMode.Manual);
            Check("Tạm dừng rồi chuyển MANUAL thì bỏ chu trình, về ĐÃ DỪNG, bit RUN bật lại",
                WaitUntil(() => McControl.Status.State == EMcState.Paused, 3000) && run.UnsafePause && McControl.Func.RunBit,
                McControl.Status.StateText + " — " + McControl.Status.Message);
            Thread.Sleep(300);

            // Bỏ giữa hành trình thì phải Init (nút cạnh Về gốc) trước khi chạy tiếp (Tuấn 2026-09-29). Tay hút để trống cả
            // sổ lẫn cảm biến, để lần START dưới chỉ vướng đúng yêu cầu Init chứ không vướng phép đối chiếu tay hút.
            McControl.Func.SetVacuum(1, false);
            McControl.Func.SetVacuum(2, false);
            run.Nozzle1.HasProduct = false;
            run.Nozzle2.HasProduct = false;
            // Chưa Init thì chọn AUTO bị từ chối ngay: ở lại MANUAL, báo bấm Init, bật hộp thoại (Tuấn 2026-09-30).
            string refused = null;
            Action<string> onRefused = s => refused = s;
            McControl.AutoModeRefused += onRefused;
            McControl.Status.Message = "";
            McControl.RequestSetMode(EMcMode.Auto);
            bool gotRefused = WaitUntil(() => refused != null, 2000);
            McControl.AutoModeRefused -= onRefused;
            Thread.Sleep(100);
            Check("Chưa Init thì chọn AUTO bị từ chối: ở lại MANUAL, báo bấm Init, có sự kiện bật hộp thoại",
                gotRefused && refused.Contains("Init") && McControl.Status.Mode == EMcMode.Manual &&
                McControl.Status.State != EMcState.Armed, refused ?? McControl.Status.Message);
            McControl.RequestStart();
            Thread.Sleep(200);
            Check("Chưa Init thì START cũng không chạy",
                McControl.Status.State != EMcState.Running, McControl.Status.Message);
            McControl.RequestInit();
            WaitUntil(() => McControl.Status.State == EMcState.Homing, 2000);
            bool inited = WaitUntil(() => McControl.Status.State != EMcState.Homing, 60000);
            bool atStandby = McControl.Motion.AxisList.All(a =>
                a.Config.WrapDegrees ? AxisAngle.Distance(a.CurrentPos, a.Config.StandbyPos) < 0.02
                                     : Math.Abs(a.CurrentPos - a.Config.StandbyPos) < 0.02);
            Check("Init: nâng Z rồi đưa mọi trục về vị trí chờ, không về gốc lại, gỡ yêu cầu Init",
                inited && atStandby && McControl.Motion.ZSafeOk && McControl.Status.OrgOk && !run.UnsafePause,
                McControl.Status.StateText + " — " + McControl.Status.Message);

            McControl.RequestSetMode(EMcMode.Auto);
            Check("Init xong chọn AUTO thì vào được sẵn sàng",
                WaitUntil(() => McControl.Status.Mode == EMcMode.Auto && McControl.Status.State == EMcState.Armed, 2000),
                McControl.Status.StateText + " — " + McControl.Status.Message);
        }

        /// <summary>
        /// Xác nhận việc quét trước (S05_ScanAssy, PLAN.md §5.2b) THẬT SỰ bớt trigger LocateTop,
        /// chứ không chỉ vẫn đặt đúng theo cách cũ (một trigger mỗi lần đặt) mà tình cờ đạt các
        /// Check khác. Đặt liền một mạch từ sản phẩm #3 lên #5 mà không tốn thêm trigger nào —
        /// vì lúc đó cả tray đã được quét xong từ trước — là bằng chứng trực tiếp cho tối ưu này.
        /// </summary>
        private static void TestAssyPreScan()
        {
            Section("Quét trước tray lắp ráp (S05_ScanAssy)");

            var run = Core.Data.McDataManager.RunData;
            var vision = McControl.Vision as FakeVision;

            // Bài trước kết thúc bằng chọn AUTO sau Init — máy đang sẵn sàng (Armed), không phải ĐÃ DỪNG.
            WaitUntil(() => McControl.Status.State == EMcState.Paused || McControl.Status.State == EMcState.Armed, 30000);

            run.ResetCounters();
            run.InputTray.ResetAll(ECellState.Loaded);
            run.AssyTray.ResetAll(ECellState.Empty);
            McControl.InvalidateAssyScan();
            run.Nozzle1.HasProduct = false;
            run.Nozzle2.HasProduct = false;
            run.AutoState = EAutoState.S00_Ready;
            Thread.Sleep(50);

            McControl.RequestStart();
            bool started = WaitUntil(() => McControl.Status.State == EMcState.Running, 3000);
            Check("Chạy lại được sau khi nạp tray lắp ráp mới để kiểm tra quét trước",
                started, McControl.Status.Message);

            bool madeThree = WaitUntil(() => run.CountOk >= 2, 120000);
            Check("Đặt được ít nhất 2 sản phẩm (đủ để chắc S05 đã quét xong)", madeThree,
                $"OK={run.CountOk} bước={run.AutoState} — {McControl.Status.Message}");

            int locateAt3 = vision?.LocateTopCount ?? -1;

            bool madeFive = WaitUntil(() => run.CountOk >= 3, 120000);
            Check("Đặt tiếp lên ít nhất 3 sản phẩm", madeFive,
                $"OK={run.CountOk} bước={run.AutoState} — {McControl.Status.Message}");

            int locateAt5 = vision?.LocateTopCount ?? -2;

            Check("Đặt thêm sản phẩm KHÔNG làm tăng số lần trigger LocateTop — dùng lại kết quả " +
                "quét trước thay vì chụp lại từng ô lúc đặt",
                locateAt5 == locateAt3, $"LocateTop lúc OK=2: {locateAt3}, lúc OK=3: {locateAt5}");

            McControl.RequestPause();
            WaitUntil(() => McControl.Status.State == EMcState.Paused, 30000);

            // ── Tuỳ chọn quét TỪNG HÀNG (Tuấn yêu cầu 23/09) ──
            // Chạy Y2 hết chiều dài tray có thể làm xê dịch chính tray lắp ráp, nên Tuấn muốn soi
            // một hàng rồi hút/đặt hết hàng đó. Bằng chứng: đặt vài sản phẩm đầu chỉ tốn cỡ MỘT HÀNG
            // trigger LocateTop, trong khi tắt tuỳ chọn thì phải tốn đủ cả tray trước khi hút.
            var sysData = Core.Data.McDataManager.SysData;
            sysData.ScanAssyByRow = true;
            try
            {
                run.ResetCounters();
                run.InputTray.ResetAll(ECellState.Loaded);
                run.AssyTray.ResetAll(ECellState.Empty);
                McControl.InvalidateAssyScan();
                run.Nozzle1.HasProduct = false;
                run.Nozzle2.HasProduct = false;
                run.AutoState = EAutoState.S00_Ready;
                Thread.Sleep(50);

                int locateBeforeRow = vision?.LocateTopCount ?? 0;
                McControl.RequestStart();
                bool placedRow = WaitUntil(() => run.CountOk >= 2, 120000);
                int locatedRow = (vision?.LocateTopCount ?? 0) - locateBeforeRow;
                int cols = run.AssyTray.Cols, cells = run.AssyTray.CellCount;
                Check("Quét từng hàng: đặt xong sản phẩm đầu chỉ tốn cỡ một hàng trigger LocateTop, không cả tray",
                    placedRow && locatedRow >= cols && locatedRow <= 2 * cols && locatedRow < cells,
                    $"LocateTop +{locatedRow}; một hàng {cols} ô, cả tray {cells} ô, OK={run.CountOk}");

                McControl.RequestPause();
                WaitUntil(() => McControl.Status.State == EMcState.Paused, 30000);

                // Init giữa hàng rồi Start lại (Tuấn 2026-10-16): tray đã ra vị trí chờ nên bỏ số quét của hàng đang làm —
                // lưới hết ô xanh nhạt ngay — và chụp lại hàng đó từ ô trống đầu tiên. Trước đó Init giữ nguyên số quét cũ.
                int scannedBeforeInit = McControl.AutoRoutine.AssyScannedCells.Length;
                McControl.RequestInit();
                WaitUntil(() => McControl.Status.State == EMcState.Homing, 2000);
                bool initDone = WaitUntil(() => McControl.Status.State != EMcState.Homing, 60000);
                Check("Init: bỏ số quét tray lắp ráp — lưới hết ô đã quét",
                    initDone && scannedBeforeInit > 0 && McControl.AutoRoutine.AssyScannedCells.Length == 0,
                    $"trước Init {scannedBeforeInit} ô đã quét, sau {McControl.AutoRoutine.AssyScannedCells.Length} — {McControl.Status.Message}");

                var assy = run.AssyTray;
                int firstFree = Enumerable.Range(0, assy.CellCount).Where(i => assy.Get(i) == ECellState.Empty)
                    .DefaultIfEmpty(-1).First();
                int freeRow = -1, unusedCol;
                if (firstFree >= 0) assy.RowCol(firstFree, out freeRow, out unusedCol);
                int rowFree = Enumerable.Range(0, assy.CellCount).Count(i =>
                {
                    int r, c;
                    assy.RowCol(i, out r, out c);
                    return r == freeRow && assy.Get(i) == ECellState.Empty;
                });
                int okBeforeRescan = run.CountOk;
                int locateBeforeRescan = vision?.LocateTopCount ?? 0;
                McControl.RequestStart();
                bool placedAfterInit = WaitUntil(() => run.CountOk > okBeforeRescan, 120000);
                int rescanned = (vision?.LocateTopCount ?? 0) - locateBeforeRescan;
                var rescan = McControl.AutoRoutine.AssyScannedCells;
                Check("Init xong Start lại: chụp lại hàng đang làm từ ô trống đầu tiên, đủ mọi ô trống của hàng rồi mới đặt",
                    placedAfterInit && firstFree >= 0 && rescan.Length > 0 && rescan[0] == firstFree &&
                    rescanned >= rowFree && rescanned <= rowFree + cols,
                    $"ô trống đầu {firstFree} (hàng {freeRow}, {rowFree} ô trống), ô quét đầu {(rescan.Length > 0 ? rescan[0] : -1)}, " +
                    $"LocateTop +{rescanned}, OK={run.CountOk} — {McControl.Status.Message}");

                McControl.RequestPause();
                WaitUntil(() => McControl.Status.State == EMcState.Paused, 30000);
            }
            finally { sysData.ScanAssyByRow = false; }

            // ── Chỉ một tay hút (Tuấn 2026-09-29) ──
            // Chọn chỉ tay #2: tay #1 không mở van hút lần nào mà máy vẫn đặt ra sản phẩm; đang chạy thì không đổi được.
            var io = (FakeIo)McControl.Io;
            try
            {
                McControl.SetOnlyNozzle(2);
                Check("Chọn chỉ tay #2 lúc máy dừng thì nhận", sysData.OnlyNozzle == 2, McControl.Status.Message);

                run.ResetCounters();
                run.InputTray.ResetAll(ECellState.Loaded);
                run.AssyTray.ResetAll(ECellState.Empty);
                McControl.InvalidateAssyScan();
                run.Nozzle1.HasProduct = false;
                run.Nozzle2.HasProduct = false;
                run.AutoState = EAutoState.S00_Ready;
                Thread.Sleep(50);

                int vac1 = 0, vac2 = 0;
                io.AfterSetDo = (bit, on) =>
                {
                    if (!on) return;
                    if (bit == IoMap.DoVacuum1) vac1++;
                    else if (bit == IoMap.DoVacuum2) vac2++;
                };
                McControl.RequestStart();
                WaitUntil(() => McControl.Status.State == EMcState.Running, 3000);
                McControl.SetOnlyNozzle(0);
                Check("Đang chạy thì không đổi được tay hút", sysData.OnlyNozzle == 2, McControl.Status.Message);

                bool placedOne = WaitUntil(() => run.CountOk >= 2, 120000);
                McControl.RequestPause();
                WaitUntil(() => McControl.Status.State == EMcState.Paused, 30000);
                Check("Chỉ tay #2: vẫn đặt ra sản phẩm, tay #1 không hút lần nào",
                    placedOne && vac1 == 0 && vac2 >= 2,
                    $"OK={run.CountOk}, van hút #1 mở {vac1} lần, #2 mở {vac2} lần — {McControl.Status.Message}");
            }
            finally
            {
                io.AfterSetDo = null;
                McControl.SetOnlyNozzle(0);
                sysData.OnlyNozzle = 0;       // phòng khi máy chưa kịp dừng nên lệnh trên bị từ chối
            }
        }

        private static void TestResumeIntegrity()
        {
            Section("Kiểm tra toàn vẹn khi chạy lại");

            var run = Core.Data.McDataManager.RunData;
            WaitUntil(() => McControl.Status.State == EMcState.Paused, 30000);

            // Giả lập: máy dừng khi đang giữ sản phẩm, rồi có người gỡ nó ra bằng tay. Cảm biến chân không là nguồn sự
            // thật (Tuấn 2026-09-30): START ghi sổ theo cảm biến rồi chạy luôn — không alarm 5030, không bắt RESET.
            run.InputTray.Set(0, ECellState.Processing);
            run.Nozzle1.HasProduct = true;
            run.Nozzle1.SourceCellIndex = 0;
            McControl.Func.SetVacuum(1, false);
            Thread.Sleep(50);

            Check("Dựng được tình huống lệch: phần mềm ghi có, cảm biến báo không",
                run.Nozzle1.HasProduct && !McControl.Func.HasProduct(1));

            McControl.RequestStart();
            bool startedEmpty = WaitUntil(() => McControl.Status.State == EMcState.Running, 3000);
            Check("Sổ ghi có hàng mà cảm biến báo trống → START ghi sổ theo cảm biến rồi chạy luôn, không alarm 5030, không chờ RESET",
                startedEmpty && !AlarmManager.IsActive(AlarmCode.ResumeIntegrity) && AlarmManager.PendingReset == null,
                McControl.Status.StateText + " — " + McControl.Status.Message);
            // Chu trình đã chạy (trục giả nhanh) nên tay #1 có thể đã hút con khác — chỉ khẳng định con cũ đã bị gỡ khỏi sổ.
            Check("Hàng đã bị lấy ra: ô nguồn thành trống, sổ tay #1 không còn trỏ về ô đó",
                run.InputTray.Get(0) == ECellState.Empty && run.Nozzle1.SourceCellIndex != 0,
                "ô 0 " + run.InputTray.Get(0) + ", ô nguồn tay #1 " + run.Nozzle1.SourceCellIndex);

            McControl.RequestPause();
            WaitUntil(() => McControl.Status.State == EMcState.Paused, 30000);

            // Chiều ngược lại: tay CÒN hàng mà sổ ghi trống (mở lại App, sau Origin…) — không phải lỗi: nhận hàng và
            // chạy tiếp, coi như đã qua bước hút (Tuấn đổi 2026-09-26).
            McControl.Func.SetVacuum(1, true);
            run.Nozzle1.HasProduct = false;
            run.Nozzle1.SourceCellIndex = -1;
            Thread.Sleep(50);
            McControl.RequestStart();
            bool startedHeld = WaitUntil(() => McControl.Status.State == EMcState.Running, 3000);
            Check("Tay còn hàng mà sổ ghi trống → START chạy luôn, không alarm 5030, sổ nhận hàng trên tay",
                startedHeld && !AlarmManager.IsActive(AlarmCode.ResumeIntegrity) && run.Nozzle1.HasProduct,
                McControl.Status.StateText + " — " + McControl.Status.Message);
            McControl.RequestPause();
            WaitUntil(() => McControl.Status.State == EMcState.Paused, 30000);

            // Dọn cho bài sau: tay trống, sổ trống, chạy lại từ S00.
            McControl.Func.SetVacuum(1, false);
            run.Nozzle1.HasProduct = false;
            run.Nozzle1.SourceCellIndex = -1;
            run.Nozzle1.Aligned = false;
            run.AutoState = EAutoState.S00_Ready;
            Thread.Sleep(50);
        }

        /// <summary>
        /// Nạp tray bằng nút Start cứng — bốn nút như nhau (khách hàng, Tuấn chốt 2026-10-16): nút nào cũng nạp mọi tray
        /// cảm biến thấy ở vị trí chờ, máy tự biết đang chờ tray nào. Thiếu tray nào thì bước cần tray đó đứng chờ — chưa có
        /// tray lắp ráp thì không soi, chưa có tray đầu vào thì hai tay hút không làm gì (Tuấn chốt 2026-09-26). Bài dựng
        /// từng cảnh bằng cảm biến tray, không bằng nút: TH1 chỉ có tray đầu vào — tray đi vào rồi chờ tray lắp ráp; TH2
        /// chỉ có tray lắp ráp — soi hàng đầu rồi chờ tray đầu vào. Hết tray đầu vào / đầy tray lắp ráp thì tray đó ra vị
        /// trí chờ, Start nạp lại.
        /// </summary>
        private static void TestTrayChangeover()
        {
            Section("Nạp tray bằng nút Start, hết tray và đổi tray");

            var run = Core.Data.McDataManager.RunData;
            var io = (FakeIo)McControl.Io;
            var vision = McControl.Vision as FakeVision;
            var model = Core.Data.McDataManager.Model;
            var sys = Core.Data.McDataManager.SysData;
            string rowIn = model.InputTray.AxisRow, rowAssy = model.AssyTray.AxisRow;
            Func<string, bool> atStandby = axis =>
                Math.Abs(McControl.Motion[axis].CurrentPos - sys.GetAxis(axis).StandbyPos) < 0.05;
            Func<string, string> posText = axis =>
                $"{axis} {McControl.Motion[axis].CurrentPos:0.00} / chờ {sys.GetAxis(axis).StandbyPos:0.00}";
            Func<string> msg = () => McControl.Status.Message ?? "";
            Func<bool> waitingOp = () => McControl.Status.State == EMcState.WaitOperator;

            // Đếm lần mở van hút — "hai tay không hút" phải thấy bằng van, không suy từ bộ đếm.
            int vacOn = 0;
            io.AfterSetDo = (bit, on) => { if (on && (bit == IoMap.DoVacuum1 || bit == IoMap.DoVacuum2)) vacOn++; };

            Action clearNozzles = () =>
            {
                run.Nozzle1.HasProduct = false;
                run.Nozzle2.HasProduct = false;
                McControl.Func.SetVacuum(1, false);
                McControl.Func.SetVacuum(2, false);
            };
            Action resetAndArm = () =>
            {
                McControl.RequestReset();
                WaitUntil(() => AlarmManager.PendingReset == null, 2000);
                McControl.RequestArm();
                WaitUntil(() => McControl.Status.State == EMcState.Armed, 3000);
            };
            Action origin = () =>
            {
                McControl.RequestOrigin();
                WaitUntil(() => McControl.Status.State == EMcState.Homing, 2000);
                WaitUntil(() => McControl.Status.State != EMcState.Homing, 40000);
            };
            // Dựng kịch bản: sổ hai tray, tay hút trống, chu trình về S00.
            Action<ECellState, ECellState> prep = (inputCells, assyCells) =>
            {
                run.InputTray.ResetAll(inputCells);
                run.AssyTray.ResetAll(assyCells);
                McControl.InvalidateAssyScan();
                clearNozzles();
                run.AutoState = EAutoState.S00_Ready;
                Thread.Sleep(50);
            };

            try
            {
                WaitUntil(() => McControl.Status.State == EMcState.Paused, 30000);

                // ── Hết tray đầu vào giữa chừng: tray ra vị trí chờ, chờ Start ──
                prep(ECellState.Ng, ECellState.Empty);
                run.InputTray.CurrentIndex = 0;
                run.InputTray.Set(0, ECellState.Loaded);

                McControl.RequestStart();
                WaitUntil(() => McControl.Status.State == EMcState.Running, 3000);
                bool waiting = WaitUntil(waitingOp, 120000);
                Check("Hết tray đầu vào thì vào CHỜ THAO TÁC", waiting, VmState());
                Check("Chờ đổi tray chỉ là thông báo — không có alarm 5020, không alarm nào chờ RESET (Tuấn 2026-09-30)",
                    !AlarmManager.IsActive(AlarmCode.TrayEmpty) && AlarmManager.PendingReset == null,
                    AlarmManager.PendingReset?.Code.ToString());
                Check("Hết tray đầu vào: trục " + rowIn + " đưa tray ra vị trí chờ", atStandby(rowIn), posText(rowIn));
                Check("Thông báo bảo thay tray đầu vào rồi bấm Start",
                    msg().Contains("tray đầu vào") && msg().Contains("bấm Start"), msg());

                // STOP lúc đang chờ tray: chu trình thoát khỏi vòng chờ, máy phải về DỪNG. Lỗi cũ để nguyên
                // CHỜ THAO TÁC nên nút Start chỉ còn "xác nhận" cho một chu trình đã thoát — máy kẹt.
                McControl.RequestStop();
                Check("STOP lúc chờ tray thì máy về DỪNG, không kẹt ở CHỜ THAO TÁC",
                    WaitUntil(() => McControl.Status.State == EMcState.Paused, 5000), VmState());
                Check("Huỷ chờ không nạp tray, vẫn nằm ở bước hút",
                    run.AutoState == EAutoState.S10_Pick1 &&
                    run.InputTray.Cells.ToArray().Count(c => c == ECellState.Loaded) == 0,
                    run.AutoState + ", " + run.InputTray.Cells.ToArray().Count(c => c == ECellState.Loaded) + " ô còn hàng");

                // Có lỗi Error lúc tray đang chờ đổi (máy thật 30/09: A2 báo alarm driver): RESET phải đưa máy ra khỏi LỖI.
                // Lỗi cũ: 5020 còn nguyên nhân tới lúc bấm Start nên RESET không gỡ được, máy kẹt ở LỖI, Start bị từ chối.
                AlarmManager.RaiseEvent(AlarmCode.RoutineCrashed, "SelfTest");
                bool inAlarm = WaitUntil(() => McControl.Status.State == EMcState.Alarm, 2000);
                resetAndArm();
                Check("Lỗi lúc chờ đổi tray: vào LỖI, RESET xong nút START mềm vào lại được trạng thái sẵn sàng",
                    inAlarm && McControl.Status.State == EMcState.Armed, VmState());
                Check("Sẵn sàng: bảo đặt tray rồi bấm Start", msg().Contains("bấm Start"), msg());
                // Đèn ba nút Start chỉ sáng khi đang chạy, không nhấp nháy (Tuấn 2026-10-16): Armed thì tắt suốt — soi đủ
                // lâu hơn một nhịp nháy 1 Hz cũ mà không lần nào sáng.
                var lampDo = new[] { IoMap.DoLampStartBtn, IoMap.DoLampStartBtnB, IoMap.DoLampStartBtnB2 };
                Func<int, int> lampOnCount = ms =>
                {
                    int on = 0;
                    var sw = Stopwatch.StartNew();
                    while (sw.ElapsedMilliseconds < ms)
                    {
                        if (lampDo.Any(b => io.GetDo(b))) on++;
                        Thread.Sleep(10);
                    }
                    return on;
                };
                int armedLampOn = lampOnCount(1200);
                Check("Armed: đèn ba nút Start tắt suốt, không nhấp nháy (Tuấn 2026-10-16)",
                    armedLampOn == 0 && McControl.Status.State == EMcState.Armed,
                    $"đèn Start sáng {armedLampOn} lần trong 1.2 s — {VmState()}");
                Check("Armed: đèn tháp xanh nhấp nháy (Tuấn chốt 2026-09-26)",
                    WaitUntil(() => McControl.Status.TowerLamp == ETowerLamp.GreenBlink, 500), McControl.Status.TowerLamp.ToString());

                // Start lúc sẵn sàng mà cảm biến chưa thấy tray đầu vào: tray lắp ráp vẫn ở trong máy nên chu trình chạy —
                // nhưng hai tay không hút, chờ tray đầu vào. Bấm nút cụm A (DI 0x0E) — trước 2026-10-16 nút này chỉ Arm.
                io.SimSet(IoMap.TrayIn1Present, false);
                vacOn = 0;
                io.SimPress(IoMap.BtnArmA1);
                Check("Start (nút cụm A) lúc sẵn sàng khởi động chu trình, không thấy tray nào để nạp cũng chạy",
                    WaitUntil(() => McControl.Status.State != EMcState.Armed, 3000), VmState());
                Check("Chưa có tray đầu vào: tự chờ tray đầu vào, hai tay không hút",
                    WaitUntil(() => waitingOp() && msg().Contains("tray đầu vào"), 30000) && vacOn == 0 && atStandby(rowIn),
                    VmState() + " — van hút mở " + vacOn + " lần, " + posText(rowIn));

                // Chờ tray giữa chừng: đèn ba nút Start tắt (Tuấn 2026-10-16 — chỉ sáng khi đang chạy; trước đó nháy nhanh
                // 0.1 s gọi người nạp). Đèn STOP vẫn tắt vì chờ tray vẫn là đang chạy Auto (Tuấn 01/10).
                int lampStopOn = 0;
                var swLamp = Stopwatch.StartNew();
                while (swLamp.ElapsedMilliseconds < 600)
                {
                    if (io.GetDo(IoMap.DoLampResetBtn)) lampStopOn++;
                    Thread.Sleep(10);
                }
                int waitLampOn = lampOnCount(600);
                Check("Chờ tray: đèn ba nút Start tắt, không nháy; đèn STOP tắt (Tuấn 01/10, 2026-10-16)",
                    waitingOp() && waitLampOn == 0 && lampStopOn == 0,
                    $"đèn Start sáng {waitLampOn} lần, STOP sáng {lampStopOn} lần trong 0.6 s — {VmState()}");

                // Lỗi Error NGAY LÚC chu trình đang chờ tray (máy thật 30/09 15:42: A1 báo alarm driver): chu trình phải bị bỏ,
                // không nằm chờ ngầm sau RESET. Lỗi cũ: RESET xong máy về DỪNG mà chu trình vẫn sống, AUTO/nút START báo
                // "Đang có chu trình chạy dở" mãi tới khi tắt App.
                AlarmManager.RaiseEvent(AlarmCode.RoutineCrashed, "SelfTest");
                bool alarmWhileWaiting = WaitUntil(() => McControl.Status.State == EMcState.Alarm, 2000);
                Thread.Sleep(300);
                resetAndArm();
                Check("Lỗi ngay lúc đang chờ tray: chu trình bị bỏ, RESET xong nút START mềm vào lại được sẵn sàng (Tuấn 30/09)",
                    alarmWhileWaiting && McControl.Status.State == EMcState.Armed, VmState());
                io.SimPress(IoMap.BtnGoB2);
                Check("Start khởi động lại chu trình, lại chờ tray đầu vào",
                    WaitUntil(() => waitingOp() && msg().Contains("tray đầu vào"), 30000), VmState());

                // STOP huỷ chu trình chạy ngầm (Tuấn 30/09): dựng lại đúng cảnh máy thật hôm đó — chu trình còn chờ tray
                // mà trạng thái đã về DỪNG.
                McControl.Status.State = EMcState.Idle;
                McControl.RequestStop();
                Thread.Sleep(300);
                McControl.RequestArm();
                Check("Chu trình chạy ngầm khi máy đang DỪNG: STOP huỷ nó, nút START mềm vào lại được sẵn sàng",
                    WaitUntil(() => McControl.Status.State == EMcState.Armed, 3000), VmState());
                io.SimPress(IoMap.BtnGoB2);
                WaitUntil(() => waitingOp() && msg().Contains("tray đầu vào"), 30000);

                // Đang chờ mà cảm biến vẫn chưa thấy tray đầu vào: Start báo chưa thấy, vẫn chờ. Tray lắp ráp đã ở trong máy
                // thì không làm gì với nó.
                McControl.Status.Message = "";
                io.SimPress(IoMap.BtnArmA2);
                Thread.Sleep(300);
                Check("Chờ tray đầu vào mà cảm biến chưa thấy tray: Start báo chưa thấy tray đầu vào, vẫn chờ",
                    waitingOp() && msg().Contains("Chưa thấy tray đầu vào"), VmState());
                io.SimSet(IoMap.TrayIn1Present, true);

                io.SimPress(IoMap.BtnGoB1);                     // nút cứng DI 0x10
                Check("Đặt tray đầu vào rồi bấm Start: nạp tray đầu vào — hai tay hút lại",
                    WaitUntil(() => vacOn > 0, 30000), VmState());
                Check("Tray đầu vào hết theo sổ nên là tray mới: đầy trở lại",
                    run.InputTray.Cells.ToArray().Count(c => c == ECellState.Loaded) >= run.InputTray.CellCount - 2,
                    run.InputTray.Cells.ToArray().Count(c => c == ECellState.Loaded) + "/" + run.InputTray.CellCount + " ô");
                Check("Nạp tray xong không còn alarm nào chờ RESET",
                    !AlarmManager.IsActive(AlarmCode.TrayEmpty) && AlarmManager.PendingReset == null,
                    AlarmManager.PendingReset?.Code.ToString());
                McControl.RequestPause();
                WaitUntil(() => McControl.Status.State == EMcState.Paused, 30000);

                // ── TH1: về gốc (hai tray ra khỏi máy), chỉ có tray đầu vào ──
                origin();
                Check("Về gốc xong, hai trục hàng ở vị trí chờ",
                    McControl.Status.OrgOk && atStandby(rowIn) && atStandby(rowAssy), posText(rowIn) + ", " + posText(rowAssy));
                prep(ECellState.Loaded, ECellState.Empty);

                // Máy đứng ở AUTO: một lần bấm Start là vào sẵn sàng rồi chạy luôn, không qua nút START mềm (Tuấn 2026-10-16).
                McControl.RequestReset();
                WaitUntil(() => AlarmManager.PendingReset == null, 2000);
                var stateBefore = McControl.Status.State;
                io.SimSet(IoMap.TrayAssyPresent, false);
                int locate1 = vision?.LocateTopCount ?? 0;
                int ok1 = run.CountOk;
                vacOn = 0;
                io.SimPress(IoMap.BtnGoB1);
                bool th1Wait = WaitUntil(() => waitingOp() && msg().Contains("tray lắp ráp"), 30000);
                Check("Máy đứng ở AUTO: bấm Start một lần là chạy, không phải vào sẵn sàng trước (Tuấn 2026-10-16)",
                    (stateBefore == EMcState.Idle || stateBefore == EMcState.Paused) && th1Wait, stateBefore + " → " + VmState());
                Check("TH1 — chỉ có tray đầu vào: tray đầu vào đi vào, rồi tự chờ tray lắp ráp",
                    th1Wait && !atStandby(rowIn), VmState() + " — " + posText(rowIn));
                Check("TH1 — chưa có tray lắp ráp: không soi, không hút, tray lắp ráp nằm ở vị trí chờ",
                    (vision?.LocateTopCount ?? 0) == locate1 && vacOn == 0 && atStandby(rowAssy),
                    "LocateTop +" + ((vision?.LocateTopCount ?? 0) - locate1) + ", van hút " + vacOn + ", " + posText(rowAssy));
                io.SimSet(IoMap.TrayAssyPresent, true);
                io.SimPress(IoMap.BtnArmA1);
                Check("TH1 — đặt tray lắp ráp rồi bấm Start: quét tray lắp ráp rồi hút, đặt",
                    WaitUntil(() => run.CountOk > ok1, 120000), $"OK {ok1}→{run.CountOk} — {VmState()}");
                McControl.RequestPause();
                WaitUntil(() => McControl.Status.State == EMcState.Paused, 30000);

                // ── TH2: về gốc, chỉ có tray lắp ráp ──
                origin();
                prep(ECellState.Loaded, ECellState.Empty);

                resetAndArm();
                io.SimSet(IoMap.TrayIn1Present, false);
                int locate2 = vision?.LocateTopCount ?? 0;
                int align2 = vision?.AlignBottomCount ?? 0;
                int ok2 = run.CountOk;
                vacOn = 0;
                io.SimPress(IoMap.BtnArmA2);
                bool th2Wait = WaitUntil(() => waitingOp() && msg().Contains("tray đầu vào"), 60000);
                int scanned2 = (vision?.LocateTopCount ?? 0) - locate2;
                int cols = run.AssyTray.Cols;
                Check("TH2 — chỉ có tray lắp ráp: chỉ soi hàng đầu tray lắp ráp rồi tự chờ tray đầu vào",
                    th2Wait && vision != null && scanned2 == cols,
                    $"LocateTop +{scanned2}, một hàng {cols} ô — {VmState()}");
                Check("TH2 — chưa có tray đầu vào: hai tay không hút, không soi CAM dưới, tray đầu vào ở vị trí chờ",
                    vacOn == 0 && (vision?.AlignBottomCount ?? 0) == align2 && atStandby(rowIn),
                    "van hút " + vacOn + ", AlignBottom +" + ((vision?.AlignBottomCount ?? 0) - align2) + ", " + posText(rowIn));
                io.SimSet(IoMap.TrayIn1Present, true);
                io.SimPress(IoMap.BtnGoB2);
                Check("TH2 — đặt tray đầu vào rồi bấm Start: hút và đặt",
                    WaitUntil(() => run.CountOk > ok2, 120000), $"OK {ok2}→{run.CountOk} — {VmState()}");
                McControl.RequestPause();
                WaitUntil(() => McControl.Status.State == EMcState.Paused, 30000);

                // ── Tay còn hàng, chưa có tray đầu vào: soi và đặt nốt chỉ với tray lắp ráp, không chờ tray đầu vào ──
                // (Tuấn chốt 2026-09-26: soi hỏng mà không trả về được thì báo lỗi, người lấy hàng ra — chờ tray đầu vào
                // cũng vô ích, tray mới đầy hàng không đặt ngược lại được.)
                origin();
                prep(ECellState.Loaded, ECellState.Empty);
                run.InputTray.Set(0, ECellState.Processing);
                McControl.Func.SetVacuum(1, true);
                run.Nozzle1.HasProduct = true;
                run.Nozzle1.SourceCellIndex = 0;

                resetAndArm();
                io.SimSet(IoMap.TrayIn1Present, false);
                int ok3 = run.CountOk;
                vacOn = 0;
                io.SimPress(IoMap.BtnGoB2);
                Check("Tay còn hàng, chỉ có tray lắp ráp: vẫn soi và đặt nốt, không chờ tray đầu vào",
                    WaitUntil(() => run.CountOk > ok3, 60000) && vacOn == 0 && atStandby(rowIn),
                    $"OK {ok3}→{run.CountOk}, van hút {vacOn}, {posText(rowIn)} — {VmState()}");
                Check("Đặt xong hàng đang cầm thì chờ tray đầu vào",
                    WaitUntil(() => waitingOp() && msg().Contains("tray đầu vào"), 30000), VmState());
                McControl.RequestPause();
                WaitUntil(() => McControl.Status.State == EMcState.Paused, 30000);

                // Soi hỏng lúc chưa có tray đầu vào: Y1 không được chạy tới tray đang ở vị trí chờ — báo 5031, lấy hàng ra.
                McControl.Func.SetVacuum(1, true);
                run.Nozzle1.HasProduct = true;
                run.Nozzle1.SourceCellIndex = 1;
                run.InputTray.Set(1, ECellState.Processing);
                run.AutoState = EAutoState.S00_Ready;
                Thread.Sleep(50);
                bool raised5031;
                if (vision != null) vision.BottomFoundRate = 0.0;
                try
                {
                    resetAndArm();
                    io.SimPress(IoMap.BtnGoB2);
                    raised5031 = WaitUntil(() => AlarmManager.IsActive(AlarmCode.CannotReturnProduct), 60000);
                }
                finally { if (vision != null) vision.BottomFoundRate = 1.0; }
                Check("Soi hỏng lúc chưa có tray đầu vào: báo 5031, bảo lấy sản phẩm ra",
                    raised5031 && msg().Contains("lấy sản phẩm ra"), VmState() + " — " + VmAlarm(AlarmCode.CannotReturnProduct));
                Check("Không chạy Y1 tới tray đầu vào ở vị trí chờ, tay #1 vẫn giữ hàng",
                    atStandby(rowIn) && run.Nozzle1.HasProduct && McControl.Func.HasProduct(1), posText(rowIn));
                Check("Bật yêu cầu lấy sản phẩm ra", run.NozzleClearRequired);

                // Người vận hành lấy hàng ra rồi RESET.
                McControl.Func.SetVacuum(1, false);
                McControl.RequestReset();
                Check("Lấy hàng ra rồi RESET thì xoá được 5031 và 4004",
                    WaitUntil(() => !AlarmManager.IsActive(AlarmCode.CannotReturnProduct) &&
                                    !AlarmManager.IsActive(AlarmCode.VisionNotFound), 3000), VmState());
                run.Nozzle1.HasProduct = false;
                run.Nozzle1.SourceCellIndex = -1;
                run.NozzleClearRequired = false;
                run.NozzleClearReason = null;
                io.SimSet(IoMap.TrayIn1Present, true);

                // ── Tray lắp ráp đầy: tray lắp ráp ra vị trí chờ, Start nạp lại ──
                prep(ECellState.Loaded, ECellState.Ok);
                McControl.RequestStart();
                Check("Tray lắp ráp đầy thì vào CHỜ THAO TÁC", WaitUntil(waitingOp, 60000), VmState());
                Check("Chờ thay tray lắp ráp: trục " + rowAssy + " đưa tray ra vị trí chờ", atStandby(rowAssy), posText(rowAssy));
                McControl.RequestReset();
                WaitUntil(() => AlarmManager.PendingReset == null, 2000);
                // Operator đang thay: tray đầy đã nhấc ra, chưa đặt tray mới — cảm biến không thấy tray.
                io.SimSet(IoMap.TrayAssyPresent, false);
                McControl.Status.Message = "";
                io.SimPress(IoMap.BtnGoB1);
                Thread.Sleep(300);
                Check("Chờ tray lắp ráp mà cảm biến chưa thấy tray: Start báo chưa thấy tray lắp ráp, vẫn chờ",
                    waitingOp() && msg().Contains("Chưa thấy tray lắp ráp"), VmState());
                io.SimSet(IoMap.TrayAssyPresent, true);
                io.SimPress(IoMap.BtnArmA1);
                Check("Đặt tray lắp ráp mới rồi bấm Start: nạp tray lắp ráp — chạy tiếp",
                    WaitUntil(() => McControl.Status.State == EMcState.Running, 5000), VmState());
                Check("Đổi tray lắp ráp xong thì tray lắp ráp trống trở lại",
                    run.AssyTray.Cells.ToArray().Count(c => c == ECellState.Ok) <= 2,
                    run.AssyTray.Cells.ToArray().Count(c => c == ECellState.Ok) + " ô OK");

                McControl.RequestPause();
                WaitUntil(() => McControl.Status.State == EMcState.Paused, 30000);
            }
            finally
            {
                io.AfterSetDo = null;
                io.SimSet(IoMap.TrayIn1Present, true);
                io.SimSet(IoMap.TrayAssyPresent, true);
                McControl.RequestSetMode(EMcMode.Manual);
            }
        }

        /// <summary>
        /// Hai tray đầu vào (Tuấn chốt 2026-10-15): Tray 1 phía trong (DI 0x0C), Tray 2 phía ngoài (DI 0x0A), lưới và cao độ
        /// teach riêng, cùng kích thước với Tray 1. Tray 2 chưa teach thì máy như một tray. Start nạp tray nào cảm biến
        /// thấy; Tray 1 trước; hết cả hai mới ra vị trí chờ.
        /// </summary>
        private static void TestTwoInputTrays()
        {
            Section("Hai tray đầu vào");

            var run = Core.Data.McDataManager.RunData;
            var io = (FakeIo)McControl.Io;
            var model = Core.Data.McDataManager.Model;
            var sys = Core.Data.McDataManager.SysData;
            var t1 = model.InputTray;
            string rowIn = t1.AxisRow;
            Func<string> msg = () => McControl.Status.Message ?? "";
            Func<bool> waitingOp = () => McControl.Status.State == EMcState.WaitOperator;
            Func<int, int, int> countIn = (from, to) =>
            {
                var cells = run.InputTray.Cells.ToArray();
                int k = 0;
                for (int i = from; i < to && i < cells.Length; i++) if (cells[i] == ECellState.Loaded) k++;
                return k;
            };

            // ── Phần tính, không chạy máy ──
            Check("Tray 2 chưa teach: sổ tray đầu vào chỉ có Tray 1 — máy như trước khi có Tray 2",
                model.InputTraysInUse == 1 && run.InputTray.Trays == 1 && run.InputTray.Rows == t1.Rows,
                $"{model.InputTraysInUse} tray, sổ {run.InputTray.Rows}×{run.InputTray.Cols}, {run.InputTray.Trays} tray");
            var t2 = model.InputTray2;
            Check("Tray 2 theo Tray 1 về kích thước, thứ tự quét và trục; model cũ lấy sẵn cao độ Z của Tray 1",
                t2.Rows == t1.Rows && t2.Cols == t1.Cols && t2.ScanOrder == t1.ScanOrder && t2.AxisRow == t1.AxisRow &&
                t2.Z_PickZ1 == t1.Z_PickZ1 && t2.Z_PickZ2 == t1.Z_PickZ2 && t2.Z_SlowZ1 == t1.Z_SlowZ1,
                $"Tray 2 {t2.Rows}×{t2.Cols} {t2.ScanOrder} {t2.AxisRow}, Z hút {t2.Z_PickZ1}/{t2.Z_PickZ2}");

            // Card tray đầu vào trang Main: hai khối lưới xếp chồng, Tray 1 trên (Tuấn 2026-10-16).
            var gridVm = new ViewModels.TrayGridViewModel("kiểm tra", "", null, Core.Data.Model.ModelData.InputTraySlots);
            gridVm.Sync(run.InputTray);
            Check("Card tray đầu vào: hai khối lưới cỡ một tray; Tray 2 chưa teach thì khối dưới mờ, ô trống",
                gridVm.Groups.Count == 2 && gridVm.Groups.All(g => g.Rows == t1.Rows && g.Cells.Count == t1.CellCount) &&
                gridVm.Groups[0].IsTaught && !gridVm.Groups[1].IsTaught &&
                gridVm.Groups[1].Cells.All(c => c.State == ECellState.Empty),
                $"{gridVm.Groups.Count} khối, Tray 2 IsTaught {(gridVm.Groups.Count > 1 && gridVm.Groups[1].IsTaught)}");

            const double shiftY = 60;    // Tray 2 nằm ngoài Tray 1 theo Y1 — số giả
            Func<Core.Data.Model.TeachPoint, Core.Data.Model.TeachPoint> shifted =
                p => new Core.Data.Model.TeachPoint(p.X, p.Y + shiftY);
            double tray2FromY = t1.GeoZ1.PR0.Y + shiftY / 2;     // Y1 quá mốc này là đang ở Tray 2

            // Y1 và Z của đúng tay đó LÚC MỞ VAN HÚT — biết lần hút ở tray nào, hạ tới đâu, không phải canh giờ.
            var pickY = new List<double>();
            var zBad = new List<string>();
            bool tray2PickBeforeTray1Done = false;
            int n = 0;
            io.AfterSetDo = (bit, on) =>
            {
                if (!on || (bit != IoMap.DoVacuum1 && bit != IoMap.DoVacuum2)) return;
                string zAxis = bit == IoMap.DoVacuum1 ? AxisId.Z1 : AxisId.Z2;
                double y = McControl.Motion[rowIn].CurrentPos;
                double z = McControl.Motion[zAxis].CurrentPos;
                bool onTray2 = y > tray2FromY;
                var layout = onTray2 ? model.InputTray2 : t1;
                lock (pickY)
                {
                    pickY.Add(y);
                    if (Math.Abs(z - layout.ZPickFor(zAxis)) > 0.05)
                        zBad.Add($"{zAxis} {z:0.00} ở Tray {(onTray2 ? 2 : 1)} (Z hút {layout.ZPickFor(zAxis)})");
                }
                if (onTray2 && countIn(0, n) > 0) tray2PickBeforeTray1Done = true;
            };

            Action resetAndArm = () =>
            {
                McControl.RequestReset();
                WaitUntil(() => AlarmManager.PendingReset == null, 2000);
                McControl.RequestArm();
                WaitUntil(() => McControl.Status.State == EMcState.Armed, 3000);
            };
            // Máy chờ tray đầu vào với tray lắp ráp trống trong máy, sổ tray đầu vào đã dùng hết. Start bấm lúc cảm biến tray
            // đầu vào chưa thấy tray — nút nào cũng nạp mọi tray cảm biến thấy (2026-10-16) — xong bật lại cảm biến Tray 1,
            // Tray 2 để vắng.
            Action waitForInput = () =>
            {
                McControl.RequestOrigin();
                WaitUntil(() => McControl.Status.State == EMcState.Homing, 2000);
                WaitUntil(() => McControl.Status.State != EMcState.Homing, 40000);
                run.InputTray.ResetAll(ECellState.Ng);
                run.AssyTray.ResetAll(ECellState.Empty);
                McControl.InvalidateAssyScan();
                run.Nozzle1.HasProduct = run.Nozzle2.HasProduct = false;
                McControl.Func.SetVacuum(1, false);
                McControl.Func.SetVacuum(2, false);
                run.AutoState = EAutoState.S00_Ready;
                Thread.Sleep(50);
                resetAndArm();
                io.SimSet(IoMap.TrayIn1Present, false);
                io.SimSet(IoMap.TrayIn2Present, false);
                io.SimPress(IoMap.BtnGoB2);
                WaitUntil(() => waitingOp() && msg().Contains("tray đầu vào"), 60000);
                io.SimSet(IoMap.TrayIn1Present, true);
            };

            try
            {
                WaitUntil(() => McControl.Status.State == EMcState.Paused, 30000);
                McControl.RequestSetMode(EMcMode.Auto);
                WaitUntil(() => McControl.Status.Mode == EMcMode.Auto, 2000);

                // ── Tray 2 chưa teach mà cảm biến Tray 2 thấy tray: không nhận Start ──
                waitForInput();
                io.SimSet(IoMap.TrayIn2Present, true);
                io.SimPress(IoMap.BtnGoB1);
                Thread.Sleep(300);
                Check("Cảm biến Tray 2 thấy tray mà Tray 2 chưa teach: Start không nhận, nói rõ lý do",
                    waitingOp() && msg().Contains("Tray đầu vào 2 chưa teach"), VmState());

                // ── Teach Tray 2: lưới Tray 1 dời Y1, cao độ hút riêng ──
                t2.GeoZ1.P00 = shifted(t1.GeoZ1.P00); t2.GeoZ1.P0C = shifted(t1.GeoZ1.P0C); t2.GeoZ1.PR0 = shifted(t1.GeoZ1.PR0);
                t2.GeoZ2.P00 = shifted(t1.GeoZ2.P00); t2.GeoZ2.P0C = shifted(t1.GeoZ2.P0C); t2.GeoZ2.PR0 = shifted(t1.GeoZ2.PR0);
                t2.Z_PickZ1 = t1.Z_PickZ1 + 0.5;
                t2.Z_PickZ2 = t1.Z_PickZ2 + 0.5;
                Core.Data.McDataManager.SyncRunTrays();
                n = run.InputTray.CellsPerTray;
                Check("Teach xong Tray 2: sổ nối thêm Tray 2 — gấp đôi số hàng, Tray 1 ở nửa đầu",
                    model.InputTraysInUse == 2 && run.InputTray.Trays == 2 && run.InputTray.Rows == 2 * t1.Rows && n == t1.CellCount,
                    $"sổ {run.InputTray.Rows}×{run.InputTray.Cols}, {run.InputTray.Trays} tray, {n} ô/tray");
                gridVm.Sync(run.InputTray);
                var tray2Cells = gridVm.Groups[1].Cells.Select(c => c.Index).ToList();
                Check("Teach xong Tray 2: khối dưới của card là Tray 2 — đủ các ô nửa sau của sổ, mỗi ô một lần",
                    gridVm.Groups[1].IsTaught && tray2Cells.All(i => i >= n && i < 2 * n) && tray2Cells.Distinct().Count() == n,
                    $"{tray2Cells.Count} ô, chỉ số {tray2Cells.DefaultIfEmpty(-1).Min()}..{tray2Cells.DefaultIfEmpty(-1).Max()}");

                int ta, ra, ca, tb, rb, cb;
                var ga = model.InputCell(run.InputTray, n - 1, out ta, out ra, out ca);
                var gb = model.InputCell(run.InputTray, n, out tb, out rb, out cb);
                Check("Ô cuối của Tray 1 theo lưới Tray 1; ô kế tiếp là hàng đầu Tray 2, theo lưới Tray 2",
                    ta == 0 && ga == t1 && ra == t1.Rows - 1 && tb == 1 && gb == t2 && rb == 0 &&
                    Math.Abs(gb.CellPos(AxisId.Z1, rb, cb).Y - t1.CellPos(AxisId.Z1, 0, cb).Y - shiftY) < 1e-9,
                    $"ô {n - 1} → Tray {ta + 1} ({ra},{ca}), ô {n} → Tray {tb + 1} ({rb},{cb})");

                // ── Chỉ Tray 2 có: Start nạp mỗi Tray 2, hút theo lưới Tray 2 ──
                io.SimSet(IoMap.TrayIn1Present, false);
                lock (pickY) pickY.Clear();
                int okB = run.CountOk;
                io.SimPress(IoMap.BtnArmA1);
                Check("Chỉ Tray 2 có: Start nạp mỗi Tray 2 — ô Tray 1 trống, ô Tray 2 có hàng",
                    WaitUntil(() => run.CountOk > okB, 60000) && countIn(0, n) == 0 &&
                    run.InputTray.Cells.Take(n).All(c => c == ECellState.Empty),
                    $"Tray 1 còn {countIn(0, n)} ô có hàng — {VmState()}");
                bool doneB = WaitUntil(() => waitingOp() && msg().Contains("tray đầu vào"), 120000);
                double minY2 = double.MaxValue, maxY2 = double.MinValue;
                for (int r = 0; r < t2.Rows; r++)
                    for (int c = 0; c < t2.Cols; c++)
                        foreach (var z in new[] { AxisId.Z1, AxisId.Z2 })
                        {
                            double y = t2.CellPos(z, r, c).Y;
                            minY2 = Math.Min(minY2, y);
                            maxY2 = Math.Max(maxY2, y);
                        }
                double[] ysB;
                lock (pickY) ysB = pickY.ToArray();
                Check("Hút hết Tray 2 rồi chờ tray đầu vào; mọi lần hút đều nằm trên lưới Tray 2",
                    doneB && ysB.Length >= n && ysB.All(y => y >= minY2 - 0.01 && y <= maxY2 + 0.01) &&
                    run.InputTray.Cells.Skip(n).All(c => c == ECellState.Ok || c == ECellState.Ng),
                    $"{ysB.Length} lần hút, Y {(ysB.Length > 0 ? ysB.Min() : 0):0.0}..{(ysB.Length > 0 ? ysB.Max() : 0):0.0}, " +
                    $"lưới Tray 2 {minY2:0.0}..{maxY2:0.0} — {VmState()}");
                McControl.RequestPause();
                WaitUntil(() => McControl.Status.State == EMcState.Paused, 30000);

                // ── Cả hai tray: Tray 1 trước, hết Tray 1 sang Tray 2 mà không ra vị trí chờ ──
                waitForInput();
                io.SimSet(IoMap.TrayIn2Present, true);
                lock (pickY) { pickY.Clear(); zBad.Clear(); }
                tray2PickBeforeTray1Done = false;
                io.SimPress(IoMap.BtnGoB1);
                // Tray 2 đầy hàng suốt lúc máy làm Tray 1 — thấy đủ n ô là đã nạp cả hai (Tray 1 đang vơi dần). Chờ cả lúc
                // máy rời CHỜ THAO TÁC: sổ đổi trước, trạng thái đổi sau một nhịp trên luồng chu trình — không chờ thì vòng
                // dưới đọc phải trạng thái chờ của chính lần nạp này (self-test 2026-10-17 trượt một lần vì vậy).
                Check("Cả hai tray có: Start nạp cả hai",
                    WaitUntil(() => countIn(n, 2 * n) == n && !waitingOp(), 5000),
                    $"Tray 1 {countIn(0, n)}, Tray 2 {countIn(n, 2 * n)} ô có hàng");
                bool sawWait = false;
                bool reachedTray2 = WaitUntil(() =>
                {
                    if (waitingOp()) sawWait = true;
                    double[] ys;
                    lock (pickY) ys = pickY.ToArray();
                    return ys.Any(y => y >= minY2 - 0.01);
                }, 180000);
                Check("Hút hết Tray 1 rồi mới sang Tray 2, không đưa bàn ra vị trí chờ giữa hai tray",
                    reachedTray2 && !tray2PickBeforeTray1Done && !sawWait && countIn(0, n) == 0,
                    $"tới Tray 2: {reachedTray2}, hút Tray 2 khi Tray 1 còn hàng: {tray2PickBeforeTray1Done}, " +
                    $"chờ thao tác giữa chừng: {sawWait} — {VmState()}");

                McControl.RequestPause();
                WaitUntil(() => McControl.Status.State == EMcState.Paused, 30000);

                // Tray 2 teach Z hút lệch Tray 1 0.5 mm: mỗi lần hút phải hạ đúng cao độ của tray chứa ô đó.
                string[] bad;
                lock (pickY) bad = zBad.ToArray();
                Check("Mỗi lần hút hạ Z tới đúng cao độ hút của tray chứa ô đó (Tray 2 có Z riêng)",
                    bad.Length == 0, string.Join("; ", bad.Take(3)));
            }
            finally
            {
                io.AfterSetDo = null;
                io.SimSet(IoMap.TrayIn1Present, true);
                io.SimSet(IoMap.TrayIn2Present, false);
                // Trả model về như chưa có Tray 2 — các bài sau chạy một tray (Normalize lại chép Z của Tray 1 sang).
                model.InputTray2 = Core.Data.Model.TrayLayout.CreateInputDefault("InputTray2");
                Core.Data.McDataManager.SyncRunTrays();
                McControl.RequestSetMode(EMcMode.Manual);
            }
        }

        /// <summary>
        /// Chọn ô ở lưới tray trang Main lúc AUTO (Tuấn chốt 2026-10-16): máy chỉ hút ô đã chọn của tray đầu vào, chỉ soi và
        /// đặt vào ô đã chọn của tray lắp ráp; làm hết thì coi như xong tray — ô không chọn thành bỏ qua, lựa chọn xoá, tray ra
        /// vị trí chờ. Chỉ chọn được ở AUTO và chỉ ô còn việc, trừ lúc tray đã hết: chọn trước cho tray sắp đặt vào.
        /// </summary>
        private static void TestCellSelection()
        {
            Section("Chọn ô ở lưới tray");

            var run = Core.Data.McDataManager.RunData;
            var io = (FakeIo)McControl.Io;
            var vision = McControl.Vision as FakeVision;
            var sys = Core.Data.McDataManager.SysData;
            Func<string> msg = () => McControl.Status.Message ?? "";
            Func<bool> waitingOp = () => McControl.Status.State == EMcState.WaitOperator;
            Func<string, bool> atStandby = axis =>
                Math.Abs(McControl.Motion[axis].CurrentPos - sys.GetAxis(axis).StandbyPos) < 0.05;
            Func<Core.Data.TrayState, ECellState, int[]> cellsIn = (t, s) =>
                Enumerable.Range(0, t.CellCount).Where(i => t.Get(i) == s).ToArray();
            Func<Core.Data.TrayState, string> trayText = t =>
                string.Join("", Enumerable.Range(0, t.CellCount).Select(i => t.Get(i).ToString()[0]));

            WaitUntil(() => McControl.Status.State == EMcState.Paused, 30000);
            WaitUntil(() => McControl.Status.Mode == EMcMode.Manual, 2000);
            McControl.ClearCellSelection(true);
            McControl.ClearCellSelection(false);
            McControl.ToggleCellSelection(true, 0);
            Check("Ở MANUAL bấm ô không chọn được", McControl.Status.Mode == EMcMode.Manual && !run.InputTray.HasSelection,
                McControl.Status.Mode.ToString());

            McControl.RequestSetMode(EMcMode.Auto);
            WaitUntil(() => McControl.Status.Mode == EMcMode.Auto, 2000);
            try
            {
                // Về gốc: hai tray ra vị trí chờ. Sổ: tray đầu vào đầy hàng, tray lắp ráp trống, tay hút rảnh.
                McControl.RequestOrigin();
                WaitUntil(() => McControl.Status.State == EMcState.Homing, 2000);
                WaitUntil(() => McControl.Status.State != EMcState.Homing, 40000);
                run.InputTray.ResetAll(ECellState.Loaded);
                run.AssyTray.ResetAll(ECellState.Empty);
                McControl.InvalidateAssyScan();
                run.Nozzle1.HasProduct = run.Nozzle2.HasProduct = false;
                McControl.Func.SetVacuum(1, false);
                McControl.Func.SetVacuum(2, false);
                run.AutoState = EAutoState.S00_Ready;
                Thread.Sleep(50);
                McControl.RequestReset();
                WaitUntil(() => AlarmManager.PendingReset == null, 2000);

                // Ô đã xong thì không chọn được — bấm nhầm một ô đã làm không được thành "xong tray".
                run.InputTray.Set(0, ECellState.Ok);
                run.AssyTray.Set(0, ECellState.Ok);
                McControl.Status.Message = "";
                McControl.ToggleCellSelection(true, 0);
                string inputRefused = msg();
                McControl.ToggleCellSelection(false, 0);
                Check("Tray còn việc: ô đã xong không chọn được, báo rõ",
                    !run.InputTray.HasSelection && !run.AssyTray.HasSelection &&
                    inputRefused.Contains("không còn hàng") && msg().Contains("không còn trống"), inputRefused + " | " + msg());
                run.InputTray.Set(0, ECellState.Loaded);
                run.AssyTray.Set(0, ECellState.Empty);

                // Hai ô mỗi tray, không liền nhau. Bấm lại là bỏ chọn; Bỏ chọn xoá hết.
                int[] pickSel = { 3, 6 }, placeSel = { 2, 9 };
                foreach (int i in pickSel) McControl.ToggleCellSelection(true, i);
                McControl.ToggleCellSelection(false, 4);
                McControl.ClearCellSelection(false);
                foreach (int i in placeSel) McControl.ToggleCellSelection(false, i);
                McControl.ToggleCellSelection(false, 5);
                McControl.ToggleCellSelection(false, 5);
                Check("Ở AUTO bấm ô là chọn, bấm lại là bỏ, Bỏ chọn xoá hết",
                    run.InputTray.Selected.OrderBy(i => i).SequenceEqual(pickSel) &&
                    run.AssyTray.Selected.OrderBy(i => i).SequenceEqual(placeSel),
                    "đầu vào " + string.Join(",", run.InputTray.Selected) + ", lắp ráp " + string.Join(",", run.AssyTray.Selected));

                var gridVm = new ViewModels.TrayGridViewModel("kiểm tra", "", null, Core.Data.Model.ModelData.InputTraySlots);
                gridVm.Sync(run.InputTray);
                var shown = gridVm.Groups.SelectMany(g => g.Cells).Where(c => c.Index < run.InputTray.CellCount).ToList();
                Check("Lưới trang Main: ô đã chọn viền đậm, ô ngoài lựa chọn vẽ mờ, nút Bỏ chọn ghi số ô",
                    gridVm.HasSelection && gridVm.SelectedCount == pickSel.Length &&
                    shown.All(c => c.IsSelected == pickSel.Contains(c.Index) && c.IsOutOfScope == !pickSel.Contains(c.Index)),
                    $"{gridVm.SelectedCount} ô chọn, " + string.Join(",", shown.Where(c => c.IsSelected).Select(c => c.Index)));

                int locate0 = vision?.LocateTopCount ?? 0;
                int ok0 = run.CountOk;
                io.SimPress(IoMap.BtnGoB1);
                bool done = WaitUntil(() => waitingOp() && !run.InputTray.HasSelection && !run.AssyTray.HasSelection, 120000);
                Check("Chọn 2 ô mỗi tray: làm hết thì coi như xong cả hai tray — chờ thay tray, lựa chọn đã xoá",
                    done && msg().Contains("Hết tray đầu vào") && msg().Contains("Đầy tray lắp ráp"), VmState());
                Check("Chỉ hút đúng các ô đã chọn của tray đầu vào, ô không chọn đánh dấu bỏ qua",
                    cellsIn(run.InputTray, ECellState.Ok).SequenceEqual(pickSel) &&
                    cellsIn(run.InputTray, ECellState.Skipped).Length == run.InputTray.CellCount - pickSel.Length,
                    trayText(run.InputTray));
                Check("Chỉ đặt vào đúng các ô đã chọn của tray lắp ráp, ô không chọn đánh dấu bỏ qua",
                    cellsIn(run.AssyTray, ECellState.Ok).SequenceEqual(placeSel) &&
                    cellsIn(run.AssyTray, ECellState.Skipped).Length == run.AssyTray.CellCount - placeSel.Length &&
                    run.CountOk - ok0 == placeSel.Length,
                    trayText(run.AssyTray) + $", OK +{run.CountOk - ok0}");
                Check("Chỉ soi các ô đã chọn của tray lắp ráp",
                    vision == null || (vision.LocateTopCount - locate0) == placeSel.Length,
                    "LocateTop +" + ((vision?.LocateTopCount ?? 0) - locate0));
                Check("Xong tray theo lựa chọn: hai tray ra vị trí chờ",
                    atStandby(Core.Data.McDataManager.Model.InputTray.AxisRow) &&
                    atStandby(Core.Data.McDataManager.Model.AssyTray.AxisRow));

                // Tray đã hết, đang chờ thay: ô nào cũng chọn được — chọn trước cho tray sắp đặt vào, nạp tray mới không xoá.
                McControl.ToggleCellSelection(true, 0);
                McControl.ToggleCellSelection(false, 5);
                Check("Tray đã hết, chờ thay: chọn trước được cả ô đã xong",
                    run.InputTray.IsSelected(0) && run.AssyTray.IsSelected(5), msg());
                io.SimPress(IoMap.BtnArmA1);
                bool done2 = WaitUntil(() => waitingOp() && !run.InputTray.HasSelection && !run.AssyTray.HasSelection &&
                                             run.AssyTray.Get(5) == ECellState.Ok, 120000);
                Check("Nạp tray mới vẫn giữ ô chọn trước: tray mới chỉ làm đúng ô đó rồi lại coi như xong",
                    done2 && cellsIn(run.InputTray, ECellState.Ok).SequenceEqual(new[] { 0 }) &&
                    cellsIn(run.AssyTray, ECellState.Ok).SequenceEqual(new[] { 5 }),
                    trayText(run.InputTray) + " / " + trayText(run.AssyTray) + " — " + VmState());

                McControl.RequestPause();
                WaitUntil(() => McControl.Status.State == EMcState.Paused, 30000);
            }
            finally
            {
                McControl.ClearCellSelection(true);
                McControl.ClearCellSelection(false);
                McControl.RequestSetMode(EMcMode.Manual);
            }
        }

        /// <summary>
        /// Tray lắp ráp hết chỗ (PLAN.md §5.2, Tuấn chốt 2026-09-13): chỉ hút khi còn chỗ đặt — còn
        /// đúng một ô thì chỉ tay #1 hút; đang cầm hàng mà hết ô thì giữ trên tay, báo Warning, đổi
        /// tray xong quét tray mới rồi soi lại mới đặt; nạp lại tray giữa vòng cũng chạy lại như vậy;
        /// phải trả về mà không rõ ô nguồn thì dừng, RESET gỡ được; tay còn hàng thì bấm chạy là chạy tiếp, bỏ bước
        /// hút (Tuấn đổi 2026-09-26 — trước đó bắt lấy sản phẩm ra trước khi chạy).
        /// </summary>
        private static void TestAssyTrayFull()
        {
            Section("Tray lắp ráp hết chỗ");

            var run = Core.Data.McDataManager.RunData;
            WaitUntil(() => McControl.Status.State == EMcState.Paused, 30000);

            McControl.RequestSetMode(EMcMode.Auto);
            WaitUntil(() => McControl.Status.Mode == EMcMode.Auto, 2000);

            // ── Tình huống 1: tray lắp ráp còn đúng một ô trống, cả hai tay đều rảnh ──
            run.ResetCounters();
            run.InputTray.ResetAll(ECellState.Loaded);
            run.AssyTray.ResetAll(ECellState.Ok);
            run.AssyTray.Set(run.AssyTray.CellCount - 1, ECellState.Empty);
            McControl.InvalidateAssyScan();
            McControl.Func.SetVacuum(1, false);
            McControl.Func.SetVacuum(2, false);
            run.Nozzle1.HasProduct = false;
            run.Nozzle2.HasProduct = false;
            run.AutoState = EAutoState.S00_Ready;
            run.AssyTrayStartedAt = null;
            var vision = McControl.Vision as FakeVision;
            int alignBefore1 = vision?.AlignBottomCount ?? 0;
            Thread.Sleep(50);

            McControl.RequestStart();
            WaitUntil(() => McControl.Status.State == EMcState.Running, 3000);
            Check("Máy chạy với tray lắp ráp còn ô trống thì đồng hồ thực tế của tray bắt đầu đếm",
                WaitUntil(() => run.AssyTrayStartedAt != null || run.AssyTrayDone, 3000));

            bool waiting = WaitUntil(() => McControl.Status.State == EMcState.WaitOperator, 120000);
            Check("Còn đúng 1 ô lắp ráp: đặt xong ô đó thì chờ đổi tray", waiting,
                McControl.Status.StateText + " — " + McControl.Status.Message);
            // Tray lắp ráp hết ô trống: dừng đồng hồ, chốt thời gian thực tế (tính cả chờ/dừng) và số ô đặt OK vào số của tray
            // đang làm (Tuấn 2026-10-08, 2026-10-17).
            Check("Tray lắp ráp đầy thì dừng đồng hồ, chốt thời gian thực tế của tray, chia cho đúng số ô đặt OK",
                run.AssyTrayDone && run.AssyTrayDoneSec > 0 && run.AssyTrayDoneProducts == run.AssyTray.CellCount &&
                run.AssyTrayStartedAt == null &&
                Math.Abs(run.AssyTrayDonePerProductSec - run.AssyTrayDoneSec / run.AssyTray.CellCount) < 1e-9,
                $"{run.AssyTrayDoneSec:0.00} s / {run.AssyTrayDoneProducts} sp, mốc {run.AssyTrayStartedAt}");
            // ResetCounters ở đầu bài xoá Last — tray xong chưa được chép xuống Last, tới lúc Start với tray mới.
            Check("Tray xong: Cycle Time / Tack Time giữ số vừa chốt, Last chưa đổi (Tuấn 2026-10-17)",
                run.LastAssyTrayTimeSec == 0 && run.LastAssyTrayProducts == 0,
                $"Last {run.LastAssyTrayTimeSec:0.00} s / {run.LastAssyTrayProducts} sp");
            double doneSec = run.AssyTrayDoneSec;
            int doneProducts = run.AssyTrayDoneProducts;
            Check("Chỉ đặt đúng 1 sản phẩm", run.CountOk == 1, "OK=" + run.CountOk + " NG=" + run.CountNg);
            // Soát trạng thái cuối thôi thì không phân biệt được "tay #2 không hút" với "tay #2 hút
            // rồi trả lại" — nhánh trả về ô nguồn che mất cổng ở S1x. Đếm số lần soi bottom-up.
            int aligned1 = (vision?.AlignBottomCount ?? 0) - alignBefore1;
            Check("Chỉ tay #1 hút — CAM bottom-up soi đúng 1 lần, tay #2 không hút rồi trả lại",
                vision != null && aligned1 == 1, "AlignBottom +" + aligned1);

            Check("Lúc chờ đổi tray không tay nào cầm hàng",
                !run.Nozzle1.HasProduct && !run.Nozzle2.HasProduct &&
                !McControl.Func.HasProduct(1) && !McControl.Func.HasProduct(2),
                "tay1=" + run.Nozzle1.HasProduct + " tay2=" + run.Nozzle2.HasProduct);
            Check("Tray đầu vào chỉ tốn đúng 1 ô",
                run.InputTray.Cells.ToArray().Count(c => c == ECellState.Ok) == 1 &&
                run.InputTray.Cells.ToArray().Count(c => c == ECellState.Loaded) == run.InputTray.CellCount - 1,
                run.InputTray.Cells.ToArray().Count(c => c == ECellState.Loaded) + "/" + run.InputTray.CellCount + " ô còn hàng");
            Check("Thông báo đòi đổi tray lắp ráp",
                WaitUntil(() => McControl.Status.Message.Contains("lắp ráp"), 1000), McControl.Status.Message);

            // Xác nhận đổi tray rồi dừng lại để dựng tình huống 2.
            McControl.RequestStart();
            WaitUntil(() => McControl.Status.State == EMcState.Running, 5000);
            Check("Start với tray lắp ráp mới: số tray vừa xong chuyển xuống Last, đồng hồ đếm lại cho tray mới",
                WaitUntil(() => run.AssyTrayStartedAt != null, 3000) && !run.AssyTrayDone &&
                run.LastAssyTrayTimeSec == doneSec && run.LastAssyTrayProducts == doneProducts,
                $"Last {run.LastAssyTrayTimeSec:0.00} s / {run.LastAssyTrayProducts} sp, vừa xong {doneSec:0.00} s / {doneProducts} sp");
            McControl.RequestPause();
            WaitUntil(() => McControl.Status.State == EMcState.Paused, 60000);

            // ── Tình huống 2: tay #1 đang cầm hàng mà tray lắp ráp không còn ô nào ──
            // Giữ hàng trên tay, chờ đổi tray (Tuấn chốt lại 2026-09-13; từ 30/09 không alarm). Đổi tray xong
            // phải quét hết tray mới TRƯỚC, rồi soi lại sản phẩm ở CAM bottom-up, rồi mới đặt.
            run.InputTray.ResetAll(ECellState.Loaded);
            run.InputTray.Set(0, ECellState.Processing);
            run.AssyTray.ResetAll(ECellState.Ok);
            McControl.InvalidateAssyScan();
            McControl.Func.SetVacuum(2, false);
            run.Nozzle2.HasProduct = false;
            run.Nozzle2.SourceCellIndex = -1;
            McControl.Func.SetVacuum(1, true);
            run.Nozzle1.HasProduct = true;
            run.Nozzle1.SourceCellIndex = 0;
            run.AutoState = EAutoState.S30_Locate1;
            int ngBefore = run.CountNg;
            Thread.Sleep(50);

            McControl.RequestStart();
            WaitUntil(() => McControl.Status.State == EMcState.Running, 3000);

            bool waiting2 = WaitUntil(() => McControl.Status.State == EMcState.WaitOperator, 120000);
            Check("Đang cầm hàng mà hết ô lắp ráp thì chờ đổi tray", waiting2,
                McControl.Status.StateText + " — " + McControl.Status.Message);
            Check("Chờ đổi tray chỉ là thông báo — không alarm 5020, không alarm Error",
                !AlarmManager.IsActive(AlarmCode.TrayEmpty) && !AlarmManager.HasError);
            Check("Sản phẩm vẫn nằm trên tay #1 trong lúc chờ đổi tray",
                run.Nozzle1.HasProduct && McControl.Func.HasProduct(1));
            Check("Không trả về ô nguồn, không tính NG",
                run.InputTray.Get(0) == ECellState.Processing && run.CountNg == ngBefore,
                run.InputTray.Get(0) + ", NG trước " + ngBefore + ", sau " + run.CountNg);

            // Xác nhận đã đổi tray lắp ráp: phải quét hết tray mới rồi mới soi lại sản phẩm.
            int okBefore2 = run.CountOk;
            int locateBefore2 = vision?.LocateTopCount ?? 0;
            int alignBefore2 = vision?.AlignBottomCount ?? 0;
            McControl.RequestStart();
            bool realigned2 = WaitUntil(() => (vision?.AlignBottomCount ?? 0) > alignBefore2, 60000);
            int locatedAtAlign2 = (vision?.LocateTopCount ?? 0) - locateBefore2;
            Check("Đổi tray xong: quét hết tray lắp ráp mới trước khi soi lại ở CAM bottom-up",
                realigned2 && locatedAtAlign2 == run.AssyTray.CellCount,
                "LocateTop +" + locatedAtAlign2 + " / " + run.AssyTray.CellCount + " ô");
            Check("Sản phẩm đang cầm được đặt vào tray mới",
                WaitUntil(() => run.InputTray.Get(0) == ECellState.Ok, 60000) && run.CountOk > okBefore2,
                "ô nguồn " + run.InputTray.Get(0) + ", OK trước " + okBefore2 + ", sau " + run.CountOk);

            McControl.RequestPause();
            WaitUntil(() => McControl.Status.State == EMcState.Paused, 60000);

            // ── Tình huống 3: nạp lại tray lắp ráp lúc máy dừng ở S10, ô trống duy nhất không có chi tiết đích ──
            // Qua cả hai nhánh của cổng S1x: có ô chưa quét thì về S05 trước khi đếm, và hết ô (held = 0)
            // thì đi đổi tray mà không hút. Bỏ nhánh thứ nhất thì tay #1 hút, soi hỏng rồi tính NG.
            int last = run.AssyTray.CellCount - 1;
            run.InputTray.ResetAll(ECellState.Loaded);
            run.AssyTray.ResetAll(ECellState.Ok);
            run.AssyTray.Set(last, ECellState.Empty);
            McControl.InvalidateAssyScan();
            McControl.Func.SetVacuum(1, false);
            McControl.Func.SetVacuum(2, false);
            run.Nozzle1.HasProduct = false;
            run.Nozzle2.HasProduct = false;
            run.AutoState = EAutoState.S10_Pick1;
            int ngBefore3 = run.CountNg;
            int locateBefore3 = vision?.LocateTopCount ?? 0;
            int alignBefore3 = vision?.AlignBottomCount ?? 0;
            Thread.Sleep(50);

            bool waiting3;
            if (vision != null) vision.FoundRate = 0.0;
            try
            {
                McControl.RequestStart();
                WaitUntil(() => McControl.Status.State == EMcState.Running, 3000);
                waiting3 = WaitUntil(() => McControl.Status.State == EMcState.WaitOperator, 120000);
            }
            finally
            {
                if (vision != null) vision.FoundRate = 1.0;
            }

            int located3 = (vision?.LocateTopCount ?? 0) - locateBefore3;
            int aligned3 = (vision?.AlignBottomCount ?? 0) - alignBefore3;
            Check("Nạp lại tray giữa vòng: quét ô chưa quét trước khi quyết định hút",
                vision != null && located3 == 1, "LocateTop +" + located3);
            // LocateTop Found = 0 → ô NG, lưới trang Main tô đỏ như ô NG (Tuấn 2026-10-17; trước đó Skipped).
            var notFoundVm = new ViewModels.TrayGridViewModel("kiểm tra", "", null);
            notFoundVm.Sync(run.AssyTray);
            var notFoundCell = notFoundVm.Groups[0].Cells.FirstOrDefault(c => c.Index == last);
            Check("Ô không thấy chi tiết đích thành NG, lưới tô màu NG (đỏ)",
                run.AssyTray.Get(last) == ECellState.Ng && notFoundCell != null && notFoundCell.Look == Models.ETrayCellLook.Ng,
                run.AssyTray.Get(last) + " / " + notFoundCell?.Look);
            Check("Không còn ô đặt thì đi đổi tray mà không tay nào hút",
                waiting3 && aligned3 == 0 && !run.Nozzle1.HasProduct && !run.Nozzle2.HasProduct &&
                McControl.Status.Message.Contains("lắp ráp"),
                "AlignBottom +" + aligned3 + " — " + McControl.Status.StateText + " — " + McControl.Status.Message);
            Check("Tray đầu vào còn nguyên, không tính NG",
                run.InputTray.Cells.ToArray().All(c => c == ECellState.Loaded) && run.CountNg == ngBefore3,
                "NG trước " + ngBefore3 + ", sau " + run.CountNg);

            McControl.RequestStart();
            WaitUntil(() => McControl.Status.State == EMcState.Running, 5000);
            McControl.RequestPause();
            WaitUntil(() => McControl.Status.State == EMcState.Paused, 60000);

            // ── Tình huống 4: tay #1 cầm hàng không rõ ô nguồn (vừa đồng bộ theo cảm biến) và soi hỏng ──
            // Phải trả về ô nguồn mà không biết ô nào: dừng, 5031 dạng sự kiện (RESET gỡ được), bắt lấy
            // sản phẩm ra khỏi tay hút trước khi chạy tiếp (Tuấn chốt 2026-09-13, an toàn). Chỉ CAM bottom-up soi
            // hỏng: từ bản rút gọn, soi cần pose ô đích nên chu trình quét tray lắp ráp trước — LocateTop phải thấy ô.
            run.InputTray.ResetAll(ECellState.Loaded);
            run.AssyTray.ResetAll(ECellState.Empty);
            McControl.InvalidateAssyScan();
            McControl.Func.SetVacuum(2, false);
            run.Nozzle2.HasProduct = false;
            run.Nozzle2.SourceCellIndex = -1;
            McControl.Func.SetVacuum(1, true);
            run.Nozzle1.HasProduct = true;
            run.Nozzle1.SourceCellIndex = -1;
            run.AutoState = EAutoState.S21_Align2;
            Thread.Sleep(50);

            bool raised4;
            if (vision != null) vision.BottomFoundRate = 0.0;
            try
            {
                McControl.RequestStart();
                WaitUntil(() => McControl.Status.State == EMcState.Running, 3000);
                raised4 = WaitUntil(() => AlarmManager.IsActive(AlarmCode.CannotReturnProduct), 60000);
            }
            finally
            {
                if (vision != null) vision.BottomFoundRate = 1.0;
            }

            Check("Soi hỏng mà không rõ ô nguồn: dừng, báo 5031 và bảo lấy sản phẩm ra",
                raised4 && (McControl.Status.Message ?? "").Contains("lấy sản phẩm ra"), McControl.Status.Message);
            Check("5031 phát dạng sự kiện (RESET gỡ được)",
                AlarmManager.Get(AlarmCode.CannotReturnProduct)?.ConditionActive == false);
            Check("Tay #1 vẫn giữ sản phẩm, không thả bừa",
                run.Nozzle1.HasProduct && McControl.Func.HasProduct(1));
            Check("Tray đầu vào không bị ghi sổ nhầm",
                run.InputTray.Cells.ToArray().All(c => c == ECellState.Loaded));
            Check("Không rõ ô nguồn thì bật yêu cầu lấy sản phẩm ra", run.NozzleClearRequired);

            // Cảnh báo "không tìm thấy" của lần soi hỏng phải hết nguyên nhân dù bước trả về không xong — trước
            // 26/09 nó kẹt lại và RESET không xoá được.
            Check("Soi hỏng mà không trả về được: 4004 vẫn hết nguyên nhân",
                AlarmManager.Get(AlarmCode.VisionNotFound)?.ConditionActive != true);
            WaitUntil(() => McControl.Status.State == EMcState.Alarm, 3000);
            McControl.RequestReset();
            Check("RESET gỡ được 5031 và 4004 — máy không bị khoá tới lúc mở lại app",
                WaitUntil(() => !AlarmManager.IsActive(AlarmCode.CannotReturnProduct) &&
                                !AlarmManager.IsActive(AlarmCode.VisionNotFound), 3000),
                McControl.Status.Message);
            Check("Sau RESET không còn alarm Error nào chặn chuyển động",
                WaitUntil(() => !AlarmManager.HasError, 3000), McControl.Status.Message);

            // Tay vẫn còn hàng: bấm chạy là gỡ yêu cầu, nhận hàng và chạy tiếp — coi như đã qua bước hút (Tuấn đổi
            // 2026-09-26; trước đó bắt lấy hàng ra).
            Thread.Sleep(300);
            McControl.RequestStart();
            bool started4 = WaitUntil(() => McControl.Status.State == EMcState.Running, 1500);
            Check("Tay còn hàng sau sự cố: START chạy tiếp, gỡ yêu cầu lấy hàng ra, sổ vẫn ghi tay #1 có hàng",
                started4 && !run.NozzleClearRequired && run.Nozzle1.HasProduct,
                McControl.Status.StateText + " — " + McControl.Status.Message);
            McControl.RequestPause();
            WaitUntil(() => McControl.Status.State == EMcState.Paused, 60000);

            // Người vận hành đã lấy hàng ra sau sự cố — phần mềm gỡ yêu cầu, tự ghi sổ theo cảm biến.
            McControl.Func.SetVacuum(1, false);
            run.Nozzle1.HasProduct = true;       // sổ còn ghi có hàng như lúc sự cố
            run.Nozzle1.SourceCellIndex = -1;
            run.NozzleClearRequired = true;
            run.NozzleClearReason = "selftest — hàng đã được lấy ra bằng tay";
            Thread.Sleep(300);
            McControl.RequestStart();
            Check("Tay hút trống rồi thì chạy lại được",
                WaitUntil(() => McControl.Status.State == EMcState.Running, 5000), McControl.Status.Message);
            Check("Gỡ yêu cầu và ghi tay #1 là trống",
                !run.NozzleClearRequired && !run.Nozzle1.HasProduct);
            McControl.RequestPause();
            WaitUntil(() => McControl.Status.State == EMcState.Paused, 60000);

            // ── Tình huống 5: nạp lại tray lắp ráp lúc tay #1 còn cầm hàng (máy dừng ở S11) ──
            // Không bắt lấy hàng ra: chạy lại từ S00 — quét tray mới, soi lại, rồi đặt (Tuấn chốt 2026-09-13).
            run.InputTray.ResetAll(ECellState.Loaded);
            run.InputTray.Set(0, ECellState.Processing);
            McControl.Func.SetVacuum(2, false);
            run.Nozzle2.HasProduct = false;
            run.Nozzle2.SourceCellIndex = -1;
            McControl.Func.SetVacuum(1, true);
            run.Nozzle1.HasProduct = true;
            run.Nozzle1.SourceCellIndex = 0;
            run.AutoState = EAutoState.S11_Pick2;
            Thread.Sleep(50);

            McControl.ResetAssyTray();
            Check("Nạp lại tray lắp ráp khi tay còn hàng không bắt lấy hàng ra",
                !run.NozzleClearRequired, run.NozzleClearReason);

            int okBefore5 = run.CountOk;
            int locateBefore5 = vision?.LocateTopCount ?? 0;
            int alignBefore5 = vision?.AlignBottomCount ?? 0;
            McControl.RequestStart();
            Check("Nạp lại tray xong chạy lại được ngay",
                WaitUntil(() => McControl.Status.State == EMcState.Running, 5000), McControl.Status.Message);
            bool realigned5 = WaitUntil(() => (vision?.AlignBottomCount ?? 0) > alignBefore5, 60000);
            int locatedAtAlign5 = (vision?.LocateTopCount ?? 0) - locateBefore5;
            Check("Nạp lại tray: quét hết tray mới trước khi soi lại ở CAM bottom-up",
                realigned5 && locatedAtAlign5 == run.AssyTray.CellCount,
                "LocateTop +" + locatedAtAlign5 + " / " + run.AssyTray.CellCount + " ô");
            Check("Sản phẩm đang cầm được đặt vào tray vừa nạp lại",
                WaitUntil(() => run.InputTray.Get(0) == ECellState.Ok, 60000) && run.CountOk > okBefore5,
                "ô nguồn " + run.InputTray.Get(0) + ", OK trước " + okBefore5 + ", sau " + run.CountOk);

            McControl.RequestPause();
            WaitUntil(() => McControl.Status.State == EMcState.Paused, 60000);

            // ── Tình huống cuối: bật quét TỪNG HÀNG, tray lắp ráp còn đúng một ô ──
            // Luật "còn đúng một ô thì chỉ tay #1 hút" phải giữ nguyên (Tuấn nhắc 2026-09-23). Chỗ dễ hỏng:
            // cổng hút đếm ô trống, mà bật tuỳ chọn thì các hàng sau chưa soi — đếm cả chúng là hút hai sản
            // phẩm rồi ôm một cái không có chỗ đặt. CountFreeCells vì thế chỉ đếm ô ĐÃ SOI THẤY chi tiết đích.
            var sysRow = Core.Data.McDataManager.SysData;
            sysRow.ScanAssyByRow = true;
            try
            {
                run.ResetCounters();
                run.InputTray.ResetAll(ECellState.Loaded);
                run.AssyTray.ResetAll(ECellState.Ok);
                run.AssyTray.Set(run.AssyTray.CellCount - 1, ECellState.Empty);
                McControl.InvalidateAssyScan();
                McControl.Func.SetVacuum(1, false);
                McControl.Func.SetVacuum(2, false);
                run.Nozzle1.HasProduct = false;
                run.Nozzle2.HasProduct = false;
                run.AutoState = EAutoState.S00_Ready;
                int alignBeforeRow = vision?.AlignBottomCount ?? 0;
                Thread.Sleep(50);

                McControl.RequestStart();
                WaitUntil(() => McControl.Status.State == EMcState.Running, 5000);
                bool waitingRow = WaitUntil(() => McControl.Status.State == EMcState.WaitOperator, 120000);
                int alignedRow = (vision?.AlignBottomCount ?? 0) - alignBeforeRow;
                Check("Quét từng hàng, còn đúng 1 ô lắp ráp: vẫn chỉ tay #1 hút, chỉ đặt 1 sản phẩm",
                    waitingRow && run.CountOk == 1 && alignedRow == 1,
                    $"{McControl.Status.StateText} — OK={run.CountOk} NG={run.CountNg}, AlignBottom +{alignedRow}");
                Check("Quét từng hàng: chờ đổi tray mà không tay nào ôm hàng thừa",
                    !run.Nozzle1.HasProduct && !run.Nozzle2.HasProduct,
                    "tay1=" + run.Nozzle1.HasProduct + " tay2=" + run.Nozzle2.HasProduct);
            }
            finally { sysRow.ScanAssyByRow = false; }

            // Thoát trạng thái chờ đổi tray: RequestStop không gỡ được CHỜ THAO TÁC, phải xác nhận rồi dừng.
            McControl.RequestPause();
            if (!WaitUntil(() => McControl.Status.State == EMcState.Paused, 15000))
            {
                McControl.RequestStart();
                WaitUntil(() => McControl.Status.State == EMcState.Running, 5000);
                McControl.RequestPause();
                WaitUntil(() => McControl.Status.State == EMcState.Paused, 60000);
            }
            McControl.RequestSetMode(EMcMode.Manual);
        }

        // ═════════════════════════ chu trình mới — App chấp hành, VisionMaster tính bù ═════════════════════════

        private static FakeVision VmSetup()
        {
            var sys = Core.Data.McDataManager.SysData;
            // Model mô phỏng mới tinh: hand-eye tay #2 của nó khớp lưới Z2, nên phép chặn lưới không loại oan.
            Core.Data.McDataManager.UseModel(Core.Data.Model.ModelData.CreateSimulation());
            McControl.InvalidateVisionCache();
            var fv = McControl.Vision as FakeVision;
            if (fv == null) return null;
            VmClearFaults(fv);
            fv.SeedVmSolution(VisionGlobalVarCatalog.Build(sys));
            fv.StampCalibrated(Core.Data.McDataManager.Model.Calib);
            return fv;
        }

        private static void VmClearFaults(FakeVision fv)
        {
            fv.SimulateOptics = false;
            fv.BottomAngleSign = 1;
            fv.MarkA = 0;
            fv.ForcedObjPose = null;
            fv.ForcePlaceStatusTimes = 0;
            fv.ForceDAngleTimes = 0;
            fv.TargetShiftTimes = 0;
            fv.CorruptEchoOnce = false;
            fv.AfterFlow = null;
            fv.FoundRate = 1.0;
            fv.BottomFoundRate = 1.0;
        }

        private static void VmRestore()
        {
            var fv = McControl.Vision as FakeVision;
            if (fv != null)
            {
                VmClearFaults(fv);
                var model = Core.Data.McDataManager.Model;
                if (model != null && model.Calib.IsUsable) fv.StampCalibrated(model.Calib);
            }
            McControl.InvalidateVisionCache();
        }

        private static void VmStopRoutine()
        {
            var s = McControl.Status.State;
            if (s != EMcState.Running && s != EMcState.Pausing && s != EMcState.WaitOperator) return;
            McControl.RequestPause();
            WaitUntil(() => McControl.Status.State != EMcState.Running && McControl.Status.State != EMcState.Pausing &&
                            McControl.Status.State != EMcState.WaitOperator, 60000);
        }

        /// <summary>Máy về gốc, AUTO, không alarm, hai tray đầy/trống, tay hút trống, bước S00.</summary>
        private static void VmPrepareRun()
        {
            VmStopRoutine();
            AlarmManager.ForceClearAll();
            McControl.RequestReset();
            WaitUntil(() => McControl.Status.State != EMcState.Alarm && !AlarmManager.HasError, 3000);
            if (!McControl.Status.OrgOk)
            {
                McControl.RequestOrigin();
                WaitUntil(() => McControl.Status.State == EMcState.Homing, 2000);
                WaitUntil(() => McControl.Status.State != EMcState.Homing, 40000);
            }
            McControl.RequestSetMode(EMcMode.Auto);
            WaitUntil(() => McControl.Status.Mode == EMcMode.Auto, 2000);

            var run = Core.Data.McDataManager.RunData;
            run.ResetCounters();
            run.InputTray.ResetAll(ECellState.Loaded);
            run.AssyTray.ResetAll(ECellState.Empty);
            McControl.InvalidateAssyScan();
            McControl.Func.SetVacuum(1, false);
            McControl.Func.SetVacuum(2, false);
            run.Nozzle1.HasProduct = false;
            run.Nozzle2.HasProduct = false;
            run.Nozzle1.SourceCellIndex = -1;
            run.Nozzle2.SourceCellIndex = -1;
            run.NozzleClearRequired = false;
            run.NozzleClearReason = null;
            run.UnsafePause = false;
            run.AutoState = EAutoState.S00_Ready;
            Thread.Sleep(100);
        }

        private static bool VmStart()
        {
            McControl.RequestStart();
            return WaitUntil(() => McControl.Status.State == EMcState.Running, 5000);
        }

        private static bool VmResetAndStart()
        {
            McControl.RequestReset();
            WaitUntil(() => McControl.Status.State != EMcState.Alarm && !AlarmManager.HasError, 3000);
            Thread.Sleep(150);
            return VmStart();
        }

        private static void VmWaitHalted()
        {
            WaitUntil(() => McControl.Status.State == EMcState.Alarm || McControl.Status.State == EMcState.Paused ||
                            McControl.Status.State == EMcState.Idle, 15000);
        }

        private static bool VmHolding()
        {
            var run = Core.Data.McDataManager.RunData;
            return (run.Nozzle1.HasProduct && McControl.Func.HasProduct(1)) || (run.Nozzle2.HasProduct && McControl.Func.HasProduct(2));
        }

        private static string VmAlarm(int code)
        {
            var a = AlarmManager.Get(code);
            return a == null ? "(không có alarm " + code + ")" : a.Code + " — " + a.Detail;
        }

        private static string VmState() { return McControl.Status.StateText + " — " + McControl.Status.Message; }

        /// <summary>Hợp đồng biến/input/output, hàm chặn và cổng hiệu chuẩn — thuần logic, không chạy máy.</summary>
        /// <summary>
        /// Chế độ <b>chạy rà</b> — máy chạy liên tục KHÔNG SẢN PHẨM để rà cơ khí (Tuấn yêu cầu 2026-09-23).
        ///
        /// <para>Ba điều phải khoá lại: nút gạt <b>không</b> tự chạy máy (luật an toàn hai tay 2026-08-20),
        /// chu trình rà <b>không</b> đụng van hút/van thổi và không kích Flow nào, và nó <b>không</b> để lại
        /// dấu vết gì trong sổ sách của chu trình thật.</para>
        /// </summary>
        private static void TestDryRun()
        {
            Section("Chạy rà (không sản phẩm, chỉ chạy trục)");

            // ── Chia ô cho hai tay đúng luật của Auto, thuần tính toán (Tuấn 2026-10-15: đổi tray đầu vào sang zigzag thì
            //    chạy rà hết "ưu tiên X") — ở hàng chạy NGƯỢC, ô ghé trước có X lớn hơn mà tay #1 vẫn phải lấy ô X nhỏ ──
            {
                var sim = Core.Data.Model.ModelData.CreateSimulation();
                var inRun = Core.Data.TrayState.Create("in", sim.InputTray.Rows, sim.InputTray.Cols);
                inRun.ScanOrder = EScanOrder.Serpentine;
                var asRun = Core.Data.TrayState.Create("assy", sim.AssyTray.Rows, sim.AssyTray.Cols);
                asRun.ScanOrder = EScanOrder.Serpentine;
                Func<Core.Data.TrayState, Core.Data.Model.TrayLayout, int, double> xOf = (t, l, i) =>
                {
                    int r, c;
                    t.RowCol(i, out r, out c);
                    return l.CellPos(AxisId.Z1, r, c).X;
                };

                // Tray đầu vào 2×5: hàng 2 ghé 5..9 từ phải sang — cặp (6, 7) là cột 4 rồi cột 3.
                int pi1 = Routine_DryCycle.PickCellFor(sim, inRun, 6, 1), pi2 = Routine_DryCycle.PickCellFor(sim, inRun, 6, 2);
                Check("Chạy rà, tray đầu vào zigzag, hàng chạy ngược: Tool 1 hút ô X nhỏ hơn, như Auto",
                    pi1 != pi2 && xOf(inRun, sim.InputTray, pi1) < xOf(inRun, sim.InputTray, pi2),
                    $"Tool 1 ô {pi1} X {xOf(inRun, sim.InputTray, pi1):0.0}, Tool 2 ô {pi2} X {xOf(inRun, sim.InputTray, pi2):0.0}");

                // Tray lắp ráp 3×4: hàng 2 ghé 4..7 từ phải sang — cặp (4, 5) là cột 4 rồi cột 3.
                int pa1 = Routine_DryCycle.PlaceCellFor(sim, asRun, 4, 1), pa2 = Routine_DryCycle.PlaceCellFor(sim, asRun, 4, 2);
                Check("Chạy rà, tray lắp ráp zigzag, hàng chạy ngược: Tool 1 đặt ô X nhỏ hơn, như Auto",
                    pa1 != pa2 && xOf(asRun, sim.AssyTray, pa1) < xOf(asRun, sim.AssyTray, pa2),
                    $"Tool 1 ô {pa1} X {xOf(asRun, sim.AssyTray, pa1):0.0}, Tool 2 ô {pa2} X {xOf(asRun, sim.AssyTray, pa2):0.0}");

                // Cặp vắt hàng CÙNG CỘT của zigzag (ô cuối hàng 1, ô đầu hàng 2): tray nghiêng chút khi teach làm X hai ô lệch
                // vài phần trăm mm. Không có ngưỡng thì phần lệch đó giao ô hàng SAU cho Tool 1 — Tool 1 hút trước nên Y1 chạy
                // xuống, lùi lên cho Tool 2, rồi lại xuống (Tuấn 2026-10-15, ô 5 và 6 trên máy thật: lợi 0.027 mm X).
                var skew = Core.Data.Model.ModelData.CreateSimulation();
                var p1 = skew.InputTray.GeoZ1.PR0; var p2 = skew.InputTray.GeoZ2.PR0;
                skew.InputTray.GeoZ1.PR0 = new Core.Data.Model.TeachPoint(p1.X - 0.36, p1.Y);
                skew.InputTray.GeoZ2.PR0 = new Core.Data.Model.TeachPoint(p2.X - 0.08, p2.Y);
                bool noTie = Routine_Auto.Nozzle1TakesFirstPickCell(
                    skew.InputTray.CellPos(AxisId.Z1, 0, 4).X, skew.InputTray.CellPos(AxisId.Z2, 0, 4).X,
                    skew.InputTray.CellPos(AxisId.Z1, 1, 4).X, skew.InputTray.CellPos(AxisId.Z2, 1, 4).X);
                int ps1 = Routine_DryCycle.PickCellFor(skew, inRun, 4, 1), ps2 = Routine_DryCycle.PickCellFor(skew, inRun, 4, 2);
                Check("Cặp cùng cột vắt hàng (zigzag, tray hơi nghiêng): Tool 1 lấy ô ghé trước — Y chỉ đi tới, không chạy lùi",
                    !noTie && ps1 == 4 && ps2 == 5,
                    $"so tuyệt đối thì Tool 1 lấy ô {(noTie ? 4 : 5)}; ngưỡng nửa bước cột {skew.InputTray.HalfColPitch:0.0} mm: " +
                    $"Tool 1 ô {ps1}, Tool 2 ô {ps2}");
                Check("Tray lắp ráp: cặp cùng cột cũng giữ thứ tự ghé, khác cột vẫn theo X",
                    Routine_Auto.Nozzle1TakesFirstCell(400.03, 400.0, skew.AssyTray.HalfColPitch) &&
                    !Routine_Auto.Nozzle1TakesFirstCell(400.03, 400.0) &&
                    !Routine_Auto.Nozzle1TakesFirstCell(422.0, 400.0, skew.AssyTray.HalfColPitch),
                    $"nửa bước cột {skew.AssyTray.HalfColPitch:0.0} mm");
            }

            var io = (FakeIo)McControl.Io;
            var run = Core.Data.McDataManager.RunData;
            var vision = McControl.Vision as FakeVision;

            AlarmManager.ForceClearAll();
            McControl.RequestReset();
            WaitUntil(() => McControl.Status.State != EMcState.Alarm, 3000);
            if (!McControl.Status.OrgOk)
            {
                McControl.RequestOrigin();
                WaitUntil(() => McControl.Status.State == EMcState.Homing, 2000);
                WaitUntil(() => McControl.Status.State != EMcState.Homing, 40000);
            }

            McControl.Func.SetVacuum(1, false);
            McControl.Func.SetVacuum(2, false);
            McControl.SyncNozzleStateFromSensor();
            AlarmManager.ForceClearAll();
            McControl.RequestReset();
            WaitUntil(() => McControl.Status.State != EMcState.Alarm, 3000);
            McControl.RequestSetMode(EMcMode.Auto);
            WaitUntil(() => McControl.Status.Mode == EMcMode.Auto, 2000);

            // ── Tay còn hàng thì không bật được ──
            McControl.Func.SetVacuum(1, true);
            WaitUntil(() => McControl.Func.HasProduct(1), 2000);
            if (McControl.Func.HasProduct(1))
            {
                McControl.SetDryRun(true);
                Check("Tay hút còn sản phẩm thì từ chối bật chạy rà",
                    !McControl.Status.DryRun, McControl.Status.Message);
            }
            McControl.Func.SetVacuum(1, false);
            WaitUntil(() => !McControl.Func.HasProduct(1), 2000);
            McControl.SyncNozzleStateFromSensor();
            AlarmManager.ForceClearAll();
            McControl.RequestReset();
            WaitUntil(() => McControl.Status.State != EMcState.Alarm, 3000);

            // ── Nút gạt chỉ CHỌN chế độ, không chạy máy ──
            McControl.SetDryRun(true);
            Thread.Sleep(300);
            Check("Bật chạy rà thì máy KHÔNG tự chạy — vẫn phải bấm Start",
                McControl.Status.DryRun && McControl.Status.State != EMcState.Running,
                McControl.Status.StateText);

            // Đi ĐÚNG đường của người vận hành: vào Armed rồi bấm nút Start cứng. Bài này trước đây bấm
            // RequestStart — đường CHỈ self-test dùng — nên không phát hiện được nhánh chạy rà chưa gắn vào
            // DoGo, và trên máy thật bật chạy rà vẫn chạy chu trình thật (Tuấn phát hiện 24/09). Test phải
            // đi đúng đường máy đi, không đi đường riêng của test.
            Action armAndGo = () =>
            {
                // Lỗi nào cũng phải RESET xác nhận trước khi vào chạy (Tuấn chốt 2026-09-26) — các bài trước
                // cố ý để lại cảnh báo, operator cũng bấm RESET trước như thế.
                McControl.RequestReset();
                WaitUntil(() => AlarmManager.PendingReset == null, 2000);
                McControl.RequestArm();
                WaitUntil(() => McControl.Status.State == EMcState.Armed, 3000);
                McControl.RequestGo();
            };

            // ── Chạy một nhịp ──
            int ok0 = run.CountOk, ng0 = run.CountNg;
            int flow0 = vision == null ? 0 : vision.VmCallCount;
            var cells0 = run.AssyTray.Cells.ToArray();
            bool airTouched = false;
            io.AfterSetDo = (bit, on) =>
            {
                if (!on) return;
                if (bit == IoMap.DoVacuum1 || bit == IoMap.DoVacuum2 ||
                    bit == IoMap.DoBlow1 || bit == IoMap.DoBlow2) airTouched = true;
            };

            armAndGo();
            // Không đủ nếu chỉ hỏi "có chạy không" — chạy nhầm chu trình THẬT thì cũng là Running. Đồng hồ của
            // Routine_DryCycle chỉ chạy khi đúng chu trình rà được khởi động, nên đó mới là bằng chứng.
            bool running = WaitUntil(() => McControl.Status.State == EMcState.Running, 3000);
            Check("Start chạy đúng chu trình RÀ, không phải chu trình thật",
                running && WaitUntil(() => McControl.DryRoutine.Elapsed.TotalSeconds > 0, 3000),
                McControl.Status.Message);

            // Lấy luôn vòng chờ này làm chỗ lấy mẫu góc A: A giữ nguyên góc đặt suốt chặng hạ/nâng Z
            // nên cửa sổ đọc rất rộng, không phải canh giờ.
            // Đo RIÊNG từng trục A. Đo gộp "góc xa 0 nhất của cả hai" là chưa đủ: một tay đứng yên ở 0°
            // suốt mà tay kia xoay thì phép đo gộp vẫn đạt, và đó đúng là lỗi Tuấn phát hiện 24/09 —
            // A1 không bao giờ nhúc nhích, A2 chỉ quanh quẩn ±ngưỡng.
            // Chờ 4 nhịp (2 vòng) vì mỗi tay đặt một lần mỗi nhịp, mà bước đầu của dải góc là 0°.
            double maxA1 = 0, maxA2 = 0;
            bool ran = WaitUntil(() =>
            {
                // Đọc LỆNH góc đặt, không đọc encoder: encoder còn mang cả GÓC HÚT, nên tay đứng yên ở góc
                // hút vẫn cho ra số khác 0 và phép kiểm lọt (đúng lỗi 24/09 — A1 báo 3.5° chính là góc hút).
                double a1 = Math.Abs(McControl.DryRoutine.LastPlaceAngle(1));
                double a2 = Math.Abs(McControl.DryRoutine.LastPlaceAngle(2));
                if (a1 > maxA1) maxA1 = a1;
                if (a2 > maxA2) maxA2 = a2;
                return McControl.DryRoutine.Strokes >= 4;
            }, 120000);
            Check("Chạy rà đi hết hai nhịp đủ hai tay", ran,
                McControl.DryRoutine.Strokes + " nhịp — " + McControl.Status.Message);

            McControl.RequestPause();
            bool stopped = WaitUntil(() => McControl.Status.State == EMcState.Paused, 30000);
            io.AfterSetDo = null;
            Check("Dừng ở checkpoint dừng được chạy rà", stopped, McControl.Status.StateText);
            Check("Dừng xong Z ở cao độ an toàn", McControl.Motion.ZSafeOk);

            // Chặng quét tray lắp ráp bằng CAM top-down là chặng gantry chạy nhiều nhất trong chu trình
            // thật; chạy rà thiếu nó thì không rà đúng quãng đường máy đi (Tuấn hỏi 2026-09-24).
            Check("Chạy rà có quét tray lắp ráp bằng CAM top-down",
                McControl.DryRoutine.Sweeps >= 1, McControl.DryRoutine.Sweeps + " lượt quét");
            // Quét CẢ TRAY một lượt ở đầu mỗi lượt tray, không quét lại mỗi nhịp — tray mô phỏng 3×4 nên
            // một lượt tray là 6 nhịp, mà bài này mới chạy 1 nhịp.
            Check("Quét cả tray một lượt cho cả lượt tray, không quét lại mỗi nhịp",
                Core.Data.McDataManager.SysData.ScanAssyByRow || McControl.DryRoutine.Sweeps == 1,
                McControl.DryRoutine.Sweeps + " lượt quét sau " + McControl.DryRoutine.Strokes + " nhịp");

            Check("Chạy rà không đụng van hút và van thổi", !airTouched);
            Check("Chạy rà không kích Flow nào",
                vision == null || vision.VmCallCount == flow0,
                vision == null ? "không có vision giả" : flow0 + " → " + vision.VmCallCount);
            Check("Chạy rà không cộng bộ đếm OK/NG",
                run.CountOk == ok0 && run.CountNg == ng0,
                $"OK {ok0}→{run.CountOk}, NG {ng0}→{run.CountNg}");
            Check("Chạy rà không đổi trạng thái ô tray lắp ráp",
                run.AssyTray.Cells.ToArray().SequenceEqual(cells0));
            // Không có dòng này thì chạy rà có thể chẳng bao giờ xoay A mà vẫn đạt hết các bài trên.
            double maxComp = Math.Abs(Core.Data.McDataManager.SysData.MaxCompensationAngleDeg);
            Check("Chạy rà xoay CẢ HAI trục A, không để tay nào đứng yên ở 0°",
                maxA1 > 0.5 && maxA2 > 0.5, $"A1 {maxA1:0.##}°, A2 {maxA2:0.##}°");
            // Không hỏi "có nằm trong ngưỡng không" — câu đó đạt cả khi chạy rà chẳng xoay mấy. Hỏi đúng con
            // số mong đợi: một phần ba ngưỡng, ở CẢ HAI tay. Đặt DrySwingFraction về 0 là bài này đổ ngay.
            double span = maxComp / 3.0;
            Check("Góc bù chạy rà đúng bằng một phần ba ngưỡng, ở cả hai tay",
                Math.Abs(maxA1 - span) <= 0.05 && Math.Abs(maxA2 - span) <= 0.05,
                $"A1 {maxA1:0.##}°, A2 {maxA2:0.##}°, mong đợi {span:0.##}° (ngưỡng bù {maxComp:0.##}°)");
            Check("Chạy rà KHÔNG đẩy gờ đầu hút tới biên ngưỡng",
                Math.Max(maxA1, maxA2) < maxComp - 0.05,
                $"xa 0 nhất {Math.Max(maxA1, maxA2):0.##}°, ngưỡng bù {maxComp:0.##}°");

            // ── Đang chạy thì không đổi được chế độ ──
            armAndGo();
            WaitUntil(() => McControl.Status.State == EMcState.Running, 3000);
            McControl.SetDryRun(false);
            Check("Đang chạy thì không gạt được chế độ chạy rà",
                McControl.Status.DryRun, McControl.Status.Message);
            McControl.RequestPause();
            WaitUntil(() => McControl.Status.State == EMcState.Paused, 30000);

            // ── Tắt đi thì trở lại chu trình thật ──
            McControl.SetDryRun(false);
            Check("Tắt chạy rà thì trở lại chu trình thật", !McControl.Status.DryRun);
        }

        private static void TestVmContract()
        {
            Section("App chấp hành — hợp đồng biến, output, hàm chặn và hai phép phán (thuần logic)");

            var sys = Core.Data.McDataManager.SysData;
            var names = sys.VisionOutputs;
            var catalog = VisionGlobalVarCatalog.Build(sys).Select(s => s.Name).ToList();
            var appVars = new[] { names.GvAxisX, names.GvAxisY2, names.GvAxisA1, names.GvAxisA2, VmVars.RunId };
            Check("Danh mục biến toàn cục đúng 13 biến (thêm cờ IsTriggerLocate 2026-10-08)", catalog.Count == 13,
                catalog.Count + ": " + string.Join(", ", catalog));
            Check("Mọi biến App ghi trước mọi Flow đều có trong danh mục", appVars.All(catalog.Contains));
            Check("Tám biến của lần đặt có trong danh mục", VmVars.Place.All(catalog.Contains));

            int a = VmRunIds.Next(), b = VmRunIds.Next();
            Check("RunId nằm trong [1, 9 999 999] và tăng dần",
                a >= 1 && a <= VmRunIds.Max && b >= 1 && b <= VmRunIds.Max && (b == a + 1 || (a == VmRunIds.Max && b == 1)), a + " → " + b);

            // ── hàm chặn: một kết quả AlignBottom_Tool1 kèm ô đích hợp lệ, rồi làm hỏng từng chỗ ──
            var calib = new Core.Data.Model.VisionCalib();
            calib.SetHandEye(1, -40, 5, 0, DateTime.Now);
            calib.SetAlignVerified(1, 1, 300, 0, DateTime.Now);
            var g = new Dictionary<string, double>
            {
                { names.GvAxisX, 300 }, { names.GvAxisY2, 80 }, { names.GvAxisA1, 100 }, { names.GvAxisA2, 0 }, { VmVars.RunId, 4242 }
            };
            VmCycle.AddPlaceGlobals(g, new PlacedTopRecord { X = 390, Y = 60, A = 1.5 }, calib, 1);
            var req = VmCycle.AlignBottomRequest(names, 1, g, true);
            Check("Pose ô đích và hằng số của tay đi qua biến toàn cục, không còn Input Params",
                g.Count == 13 && g[VmVars.IsTriggerLocate] == 1 && g[VmVars.HasTarget] == 1 && g[VmVars.CellX] == 390 &&
                g[VmVars.HandEyeDx] == -40 && g[VmVars.AlignSign] == 1 && req.OptionalOutputs.Contains(VmOut.CmdX),
                string.Join(", ", g.Keys));
            var gNo = new Dictionary<string, double>(g, StringComparer.Ordinal);
            VmCycle.AddPlaceGlobals(gNo, null, calib, 1);
            Check("Chỉ soi (phép thử vòng xoay): HasTarget = 0 và pose ô về 0",
                gNo[VmVars.HasTarget] == 0 && gNo[VmVars.CellX] == 0 && gNo[VmVars.CellA] == 0);

            // Bù đặt của ô: App cộng thẳng vào pose đích rồi mới gửi — Group Cmd không biết có lượng bù nào.
            var gTrim = new Dictionary<string, double>();
            VmCycle.AddPlaceGlobals(gTrim, new PlacedTopRecord { X = 390, Y = 60, A = 359.5 }, calib, 1,
                new Core.Data.Model.CellOffset { PlaceDx = 0.05, PlaceDy = -0.07, PlaceDa = 1.0 });
            // Y gửi đi đổi dấu: Y camera dưới ngược chiều Y2 của máy, Group Cmd tính trong hệ camera dưới (Tuấn 2026-09-30).
            Check("Bù đặt cộng vào CellX_mm / CellY_mm gửi đi (Y đổi dấu sang hệ camera dưới)",
                Near(gTrim[VmVars.CellX], 390.05) && Near(gTrim[VmVars.CellY], -59.93),
                $"CellX_mm {gTrim[VmVars.CellX]:0.###}, CellY_mm {gTrim[VmVars.CellY]:0.###}");
            Check("HandEyeDy_mm gửi đi đổi dấu, HandEyeDx_mm giữ nguyên",
                Near(gTrim[VmVars.HandEyeDy], -5) && Near(gTrim[VmVars.HandEyeDx], -40),
                $"HandEyeDx_mm {gTrim[VmVars.HandEyeDx]:0.###}, HandEyeDy_mm {gTrim[VmVars.HandEyeDy]:0.###}");
            Check("Bù góc quấn về [0, 360) trước khi gửi", Near(gTrim[VmVars.CellA], 1.5), $"CellA_deg {gTrim[VmVars.CellA]:0.###}");

            var gNoTrim = new Dictionary<string, double>();
            VmCycle.AddPlaceGlobals(gNoTrim, new PlacedTopRecord { X = 390, Y = 60, A = 12 }, calib, 1, null);
            Check("Ô chưa nhập bù thì vị trí gửi đi nguyên văn số quét được, góc thì lật quanh mốc góc",
                Near(gNoTrim[VmVars.CellX], 390) && Near(gNoTrim[VmVars.CellY], -60) && Near(gNoTrim[VmVars.CellA], 348));

            // Mốc góc giữa hai camera (thay Create Reference): CellA_deg = mốc góc − góc LocateTop + bù góc của ô,
            // theo đúng tay đang gọi. TRỪ góc LocateTop vì hai camera đo góc ngược chiều nhau (đo trên máy 2026-09-23).
            var calibRef = new Core.Data.Model.VisionCalib();
            calibRef.SetHandEye(1, -40, 5, 5.0, DateTime.Now);
            calibRef.SetHandEye(2, -40, 5, -3.0, DateTime.Now);
            var gRef1 = new Dictionary<string, double>();
            VmCycle.AddPlaceGlobals(gRef1, new PlacedTopRecord { X = 390, Y = 60, A = 358.0 }, calibRef, 1,
                new Core.Data.Model.CellOffset { PlaceDa = 1.0 });
            var gRef2 = new Dictionary<string, double>();
            VmCycle.AddPlaceGlobals(gRef2, new PlacedTopRecord { X = 390, Y = 60, A = 1.0 }, calibRef, 2);
            Check("CellA_deg = mốc góc của tay − góc LocateTop + bù góc của ô, quấn về [0, 360)",
                Near(gRef1[VmVars.CellA], 8.0) && Near(gRef2[VmVars.CellA], 356.0),
                $"tay 1 {gRef1[VmVars.CellA]:0.###} (chờ 8), tay 2 {gRef2[VmVars.CellA]:0.###} (chờ 356)");

            // Chiều — đúng cái mà bản trước sai và đo một điểm không thấy được: xoay chi tiết đích đi +10° thì góc đích
            // gửi cho Flow phải đổi −10°. Công thức cộng cũ cho +10°, trùng đáp số ở đúng góc đem ra đo mốc nên lọt.
            var gDir0 = new Dictionary<string, double>();
            var gDir10 = new Dictionary<string, double>();
            VmCycle.AddPlaceGlobals(gDir0, new PlacedTopRecord { X = 390, Y = 60, A = 37.0 }, calibRef, 1);
            VmCycle.AddPlaceGlobals(gDir10, new PlacedTopRecord { X = 390, Y = 60, A = 47.0 }, calibRef, 1);
            Check("Chi tiết đích xoay +10° thì góc đích gửi đi đổi −10° (hai camera ngược chiều)",
                Near(AxisAngle.ShortestDelta(gDir0[VmVars.CellA], gDir10[VmVars.CellA]), -10.0),
                $"Δ góc đích {AxisAngle.ShortestDelta(gDir0[VmVars.CellA], gDir10[VmVars.CellA]):0.###} (chờ −10)");

            // Con can lắp đúng đặt lại đúng chỗ cũ phải ra góc bù 0 — điều kiện định nghĩa nên mốc góc.
            var calibHe = new Core.Data.Model.VisionCalib();
            const double topAtCommit = 37.07, objAtCommit = -179.67;
            calibHe.SetHandEye(1, -40, 5, AxisAngle.Wrap(objAtCommit + topAtCommit), DateTime.Now);
            var gHe = new Dictionary<string, double>();
            VmCycle.AddPlaceGlobals(gHe, new PlacedTopRecord { X = 390, Y = 60, A = topAtCommit }, calibHe, 1);
            Check("Đặt lại đúng tư thế lúc đo mốc góc → góc đích trùng góc con can, góc bù 0",
                Near(AxisAngle.ShortestDelta(gHe[VmVars.CellA], objAtCommit), 0.0),
                $"góc đích {gHe[VmVars.CellA]:0.###}, góc con can {AxisAngle.Wrap(objAtCommit):0.###}");
            var gRefNo = new Dictionary<string, double>();
            VmCycle.AddPlaceGlobals(gRefNo, null, calibRef, 1);
            Check("Chỉ soi (HasTarget = 0) thì mốc góc không tạo ra góc đích giả", gRefNo[VmVars.CellA] == 0);

            var gTrimOff = new Dictionary<string, double>();
            VmCycle.AddPlaceGlobals(gTrimOff, null, calib, 1, new Core.Data.Model.CellOffset { PlaceDx = 0.05 });
            Check("Chỉ soi (HasTarget = 0) thì bù đặt không được tạo ra đích giả",
                gTrimOff[VmVars.HasTarget] == 0 && gTrimOff[VmVars.CellX] == 0);

            // Vết một lần kích AlignBottom: ghi mọi số, App tính lại Group Cmd để đối chiếu (không thay số của Flow).
            var gT = new Dictionary<string, double>(StringComparer.Ordinal)
            {
                { names.GvAxisX, 291.018 }, { names.GvAxisY2, 58.629 }, { names.GvAxisA1, 3.0 }, { names.GvAxisA2, 0 }, { VmVars.RunId, 77 }
            };
            VmCycle.AddPlaceGlobals(gT, new PlacedTopRecord { X = 420.0, Y = 0.03, A = 37.2 }, calib, 1);
            var reqT = VmCycle.AlignBottomRequest(names, 1, gT, true);
            double tdx, tdy, tdt;
            SinglePointAlignMath.Solve(gT[VmVars.CellX] + gT[VmVars.HandEyeDx], gT[VmVars.CellY] + gT[VmVars.HandEyeDy], gT[VmVars.CellA],
                -0.1, 0.45, 40.0, out tdx, out tdy, out tdt);
            Func<Action<VmCallResult>, VmAlignTrace> traceOf = change =>
            {
                var r = new VmCallResult { Ok = true };
                r.Values[VmOut.EchoRunId] = 77; r.Values[VmOut.EchoX] = 291.018; r.Values[VmOut.EchoA] = 3.0;
                r.Values[names.OutFound] = 1; r.Values[VmOut.ObjFound] = 1; r.Values[VmOut.ObjRunId] = 77;
                r.Values[VmOut.ObjX] = -0.1; r.Values[VmOut.ObjY] = 0.45; r.Values[VmOut.ObjA] = 40.0;
                r.Values[VmOut.Status] = 1;
                r.Values[VmOut.CmdX] = tdx; r.Values[VmOut.CmdY2] = tdy;
                r.Values[VmOut.CmdA] = AxisAngle.Wrap(3.0 + tdt); r.Values[VmOut.DiagDA] = tdt;
                r.Values[VmOut.DiagEcc] = Math.Sqrt(0.01 + 0.2025);
                r.Values[VmOut.DiagTarX] = gT[VmVars.CellX] + gT[VmVars.HandEyeDx];
                r.Values[VmOut.DiagTarY] = gT[VmVars.CellY] + gT[VmVars.HandEyeDy];
                change?.Invoke(r);
                return VmCycle.TraceAlignBottom(r, reqT, names, 1, 0.0);
            };
            var trOk = traceOf(null);
            Check("Vết AlignBottom ghi vị trí trục, biến App ghi, output Flow trả; Group Cmd đúng phép tính → KHỚP",
                trOk.Checked && trOk.Match && trOk.Line.Contains(AxisId.Z1) && trOk.Line.Contains(VmVars.CellA) &&
                trOk.Line.Contains(VmVars.HandEyeDx) && trOk.Line.Contains(VmOut.DiagTarX) && trOk.Line.Contains("KHỚP"), trOk.Line);
            var trX = traceOf(r => r.Values[VmOut.CmdX] += 0.1);
            Check("Group Cmd ra CmdX lệch phép tính 0.1 mm → vết báo LỆCH", trX.Checked && !trX.Match && trX.Line.Contains("LỆCH"), trX.Check);
            var trA = traceOf(r => { r.Values[VmOut.DiagDA] = -tdt; r.Values[VmOut.CmdA] = AxisAngle.Wrap(3.0 - tdt); });
            Check("Group Cmd quên nhân dấu góc → vết báo LỆCH", trA.Checked && !trA.Match, trA.Check);
            var gT0 = new Dictionary<string, double>(gT, StringComparer.Ordinal);
            VmCycle.AddPlaceGlobals(gT0, null, calib, 1);
            var tr0 = VmCycle.TraceAlignBottom(new VmCallResult { Ok = true }, VmCycle.AlignBottomRequest(names, 1, gT0, false), names, 1, 0.0);
            Check("Chỉ soi (HasTarget = 0) → vết ghi số, không đối chiếu", !tr0.Checked && tr0.Line.Contains("Group Cmd nghỉ"), tr0.Line);


            Func<Action<Dictionary<string, double>>, bool, VmPlaceCommand> guard = (change, grid) =>
            {
                var v = new Dictionary<string, double>
                {
                    { VmOut.Status, 1 },
                    // Y của Flow nằm trong hệ camera dưới (ngược chiều máy) — App đọc về rồi đổi dấu.
                    { VmOut.CmdX, 348.8 }, { VmOut.CmdY2, -65.6 }, { VmOut.CmdA, 100.7 }, { VmOut.DiagDA, 0.7 }, { VmOut.DiagEcc, 1.34 },
                    { VmOut.DiagTarX, 350 }, { VmOut.DiagTarY, -65 },
                    { VmOut.EchoRunId, 4242 }, { VmOut.ObjRunId, 4242 }
                };
                change?.Invoke(v);
                var r = new VmCallResult { Ok = true };
                foreach (var kv in v) r.Values[kv.Key] = kv.Value;
                return VmCycle.CheckPlace(r, req, 1, 100, 350, 65, 1.5, 15, 3, x => x >= -1 && x <= 600, y => y >= -1 && y <= 400, grid);
            };
            Func<VmPlaceCommand, string> show = c => c.Route + ": " + c.Why;

            var ok = guard(null, true);
            Check("Kết quả hợp lệ → cất đúng lệnh Flow, Y2 đổi dấu về chiều máy",
                ok.Route == EVmRoute.Ok && ok.CmdX == 348.8 && Near(ok.CmdY2, 65.6) && Near(ok.DiagTarY, 65) && ok.CmdA == 100.7,
                show(ok) + $" — CmdY2 {ok.CmdY2:0.###}, DiagTarY {ok.DiagTarY:0.###}");
            var cY = guard(v => v[VmOut.DiagTarY] = -67, true);
            Check("Đích lệch lưới teach 2 mm theo Y → bỏ ô (G)", cY.Route == EVmRoute.Skip, show(cY));
            var c1 = guard(v => v[VmOut.EchoRunId] = 4243, true);
            Check("EchoRunId của lần chạy khác → dừng (E)", c1.Route == EVmRoute.Error, show(c1));
            var c2 = guard(v => v[VmOut.ObjRunId] = 4241, true);
            Check("ObjRunId khác RunId (Script dừng giữa chừng) → dừng (E)", c2.Route == EVmRoute.Error, show(c2));
            var c4 = guard(v => v.Remove(VmOut.Status), true);
            Check("Thiếu Status → dừng (E), không coi là OK", c4.Route == EVmRoute.Error, show(c4));
            var c5 = guard(v => v[VmOut.DiagDA] = -15.5, true);
            var c6 = guard(v => v[VmOut.DiagEcc] = 3.2, true);
            Check("Góc bù hoặc lệch tâm vượt ngưỡng Setting → trả về ô nguồn, NG (N)",
                c5.Route == EVmRoute.Ng && c6.Route == EVmRoute.Ng, show(c5) + " | " + show(c6));
            var c10 = guard(v => v[VmOut.Status] = 6, true);
            var c11 = guard(v => v[VmOut.Status] = 0, true);
            Check("Status 6 (số sai / SPA báo NG) hoặc 0 (HasTarget không tới Flow) → dừng (E)",
                c10.Route == EVmRoute.Error && c11.Route == EVmRoute.Error, show(c10) + " | " + show(c11));
            var c12 = guard(v => v[VmOut.CmdA] = double.NaN, true);
            var c13 = guard(v => v[VmOut.CmdA] = 361, true);
            Check("CmdA_deg không hữu hạn hoặc ngoài [0, 360] → dừng (E)", c12.Route == EVmRoute.Error && c13.Route == EVmRoute.Error, show(c12) + " | " + show(c13));
            var c16 = guard(v => v[VmOut.DiagTarX] = 352, true);
            Check("Đích lệch lưới teach 2 mm → bỏ ô (G)", c16.Route == EVmRoute.Skip, show(c16));
            var c17 = guard(v => v[VmOut.CmdX] = 355, true);
            Check("Lệnh X lệch ô quá ngưỡng lưới + lệch tâm → bỏ ô (G)", c17.Route == EVmRoute.Skip, show(c17));
            var c18 = guard(v => v[VmOut.CmdX] = 700, false);
            Check("Lệnh X ngoài soft limit → dừng (E)", c18.Route == EVmRoute.Error, show(c18));
            var c19 = guard(v => v[VmOut.CmdA] = 120, true);
            Check("Góc lệnh cách góc lúc chụp quá ngưỡng góc bù + 1° → dừng (E)", c19.Route == EVmRoute.Error, show(c19));
            var failed = VmCycle.CheckPlace(VmCallResult.Fail("quá thời gian chờ", true), req, 1, 100, 350, 65, 1.5, 15, 3, null, null, true);
            Check("Flow quá giờ → dừng (E), không coi là không thấy", failed.Route == EVmRoute.Error, show(failed));

            // ── cổng hiệu chuẩn phía App ──
            var model = Core.Data.Model.ModelData.CreateSimulation();
            model.Calib.TopDownVerifiedAt = DateTime.Now;
            var g0 = VmCycle.EvaluateCalib(model, sys);
            Check("Đủ kiểm tra top-down, thử vòng xoay, hand-eye → cổng mở", g0.Block == null, g0.Block);
            Check("Cổng lấy hằng số từ model để hiện", g0.Ready && Near(g0.HandEyeDx1, model.Calib.HandEyeDx(1)), g0.Describe());
            var alignAt1 = model.Calib.AlignVerifiedAt1;
            model.Calib.AlignVerifiedAt1 = DateTime.MinValue;
            var g1 = VmCycle.EvaluateCalib(model, sys);
            var g1Tool2 = VmCycle.EvaluateCalib(model, sys, 2);
            var g1Tool1 = VmCycle.EvaluateCalib(model, sys, 1);
            model.Calib.AlignVerifiedAt1 = alignAt1;
            Check("Dấu góc tay #1 chưa đo → cổng chặn, nói rõ tay nào", g1.Block != null && g1.Block.Contains("dấu góc tay #1"), g1.Block);
            Check("Cổng của riêng tay #2 (chạy khô) không bị tay #1 chưa hiệu chuẩn chặn", g1Tool2.Block == null, g1Tool2.Block);
            Check("Cổng của riêng tay #1 vẫn chặn đúng tay đó", g1Tool1.Block != null && g1Tool1.Block.Contains("dấu góc tay #1"), g1Tool1.Block);
            model.AlignPosX_Nozzle1 += 0.2;
            var g3 = VmCycle.EvaluateCalib(model, sys);
            model.AlignPosX_Nozzle1 -= 0.2;
            Check("Vị trí soi đã teach khác vị trí đã xác minh → cổng chặn", g3.Block != null && g3.Block.Contains("vị trí soi tay #1"), g3.Block);
            var topAt = model.Calib.TopDownVerifiedAt;
            model.Calib.TopDownVerifiedAt = DateTime.MinValue;
            var g4 = VmCycle.EvaluateCalib(model, sys);
            model.Calib.TopDownVerifiedAt = topAt;
            Check("Kiểm tra top-down chưa đạt → cổng chặn", g4.Block != null && g4.Block.Contains("kiểm tra top-down"), g4.Block);

            // ── đọc LocateTop ──
            var gl = new Dictionary<string, double> { { names.GvAxisX, 300 }, { names.GvAxisY2, 80 }, { VmVars.RunId, 55 } };
            Func<Action<VmCallResult>, VmCameraRead> locate = change =>
            {
                var r = new VmCallResult { Ok = true };
                r.Values[names.OutFound] = 1;
                r.Values[VmOut.EchoRunId] = 55; r.Values[VmOut.EchoX] = 300; r.Values[VmOut.EchoY2] = 80;
                r.Values[VmOut.TopFound] = 1; r.Values[VmOut.TopX] = 300.2; r.Values[VmOut.TopY] = 79.9; r.Values[VmOut.TopA] = 3;
                r.Values[VmOut.TopRunId] = 55;
                change?.Invoke(r);
                return VmCycle.ReadLocate(r, gl, names);
            };
            var l0 = locate(null);
            Check("LocateTop hợp lệ → cất TopX_mm/TopY_mm/TopA_deg nguyên văn", l0.Found && l0.VmValid && l0.X == 300.2 && l0.A == 3, l0.VmIssue);
            var l1 = locate(r => r.Values[VmOut.EchoY2] = 82);
            Check("Flow subscribe nhầm trục (EchoY2_mm khác số vừa ghi) → echo sai", !l1.EchoOk && !l1.VmValid, l1.EchoIssue);
            var l3 = locate(r => { r.Values[names.OutFound] = 0; r.Values.Remove(VmOut.TopX); r.Values.Remove(VmOut.TopRunId); });
            Check("Không thấy mẫu → không đòi output của Script", !l3.Found && l3.VmValid, l3.VmIssue);
            var l4 = locate(r => r.Values[VmOut.TopRunId] = 54);
            Check("TopRunId là số của lần chạy trước → không hợp lệ", !l4.VmValid, l4.VmIssue);

            // ── đọc AlignBottom ──
            var reqNoTarget = VmCycle.AlignBottomRequest(names, 1, gNo, false);
            Func<Action<VmCallResult>, VmCameraRead> align = change =>
            {
                var r = new VmCallResult { Ok = true };
                r.Values[names.OutFound] = 1;
                r.Values[VmOut.EchoRunId] = 4242; r.Values[VmOut.EchoX] = 300; r.Values[VmOut.EchoA] = 100;
                r.Values[VmOut.ObjFound] = 1; r.Values[VmOut.ObjX] = 1.2; r.Values[VmOut.ObjY] = -0.6; r.Values[VmOut.ObjA] = 0.8;
                r.Values[VmOut.ObjRunId] = 4242;
                change?.Invoke(r);
                return VmCycle.ReadAlignBottom(r, reqNoTarget, names, 1);
            };
            var b0 = align(null);
            Check("AlignBottom hợp lệ → đọc được tư thế sản phẩm", b0.Found && b0.VmValid && b0.X == 1.2, b0.VmIssue);
            var b1 = align(r => r.Values[VmOut.EchoA] = 0);
            Check("Echo0 của Tool2 còn subscribe A1_deg (EchoA_deg khác góc vừa ghi) → echo sai", !b1.EchoOk && !b1.VmValid, b1.EchoIssue);
            var b2 = align(r => r.Values[VmOut.ObjRunId] = 4241);
            Check("ObjRunId của lần chạy trước → không hợp lệ", b2.EchoOk && !b2.VmValid, b2.VmIssue);

            // ── Bước 2: App tự phán ──
            Func<double, double, double, double, VmMeanCapture> cap = (cx, cy, ax, ay) => new VmMeanCapture { Ok = true, X = cx, Y = cy, AxX = ax, AxY = ay };
            var j0 = VmCycle.JudgeTopDown(cap(300.3, 79.8, 300, 80), cap(300.31, 79.81, 305, 80), cap(300.29, 79.79, 300, 85), 0.05);
            Check("Dấu chuẩn đứng yên khi X, Y2 chạy → kiểm tra top-down đạt", j0.Status == VmStatus.Ok, "Status " + j0.Status);
            var j1 = VmCycle.JudgeTopDown(cap(300.3, 79.8, 300, 80), cap(305.3, 79.8, 305, 80), cap(300.3, 79.8, 300, 85), 0.05);
            Check("Dấu chuẩn chạy cùng trục X → SAI DẤU (8)", j1.Status == VmStatus.Code8, "Status " + j1.Status);
            var j2 = VmCycle.JudgeTopDown(cap(300.3, 79.8, 300, 80), cap(300.38, 79.8, 305, 80), cap(300.3, 79.8, 300, 85), 0.05);
            Check("Lệch thang đo 0.08 mm → vượt dung sai (4)", j2.Status == VmStatus.EccOut, "Status " + j2.Status);
            var j3 = VmCycle.JudgeTopDown(cap(300.3, 79.8, 300, 80), cap(300.3, 79.8, 305, 80.1), cap(300.3, 79.8, 300, 85), 0.05);
            Check("Trục Y2 chạy trong lúc đo bước X → không kết luận (7)", j3.Status == VmStatus.AxisMoved, "Status " + j3.Status);

            // ── Bước 4: App tự phán, thay Flow AlignVerify_ToolN ──
            Func<double, double, double, double, VmMeanCapture> bot =
                (px, py, pa, axA) => new VmMeanCapture { Ok = true, X = px, Y = py, A = pa, AxX = 300, AxA = axA };
            Func<double, double, double[]> rot = (r, deg) =>
            {
                double th = deg * Math.PI / 180.0;
                return new[] { r * Math.Cos(th), r * Math.Sin(th) };
            };
            var p0 = bot(2.0, 0, 0, 0);
            var p1Plus = bot(rot(2.0, 10)[0], rot(2.0, 10)[1], 10, 10);      // hệ bottom-up quay cùng chiều trục A
            var p1Minus = bot(rot(2.0, -10)[0], rot(2.0, -10)[1], -10, 10);  // quay ngược chiều

            var a0 = VmCycle.JudgeAlignSign(p0, p1Plus, 0.05, 1.5, 0.5, 1.0);
            Check("Xoay +10° mà tư thế đo cũng quay +10° → dấu +1, sai số dư ≈ 0",
                a0.Status == VmStatus.Ok && a0.Sign == 1 && Math.Abs(a0.Residual) < 0.01,
                $"Status {a0.Status} dấu {a0.Sign} dư {a0.Residual:0.0000} góc đo {a0.RawDTheta:0.00}");
            var a1 = VmCycle.JudgeAlignSign(p0, p1Minus, 0.05, 1.5, 0.5, 1.0);
            Check("Tư thế đo quay ngược chiều trục → dấu −1", a1.Status == VmStatus.Ok && a1.Sign == -1,
                $"Status {a1.Status} dấu {a1.Sign}");
            var a2 = VmCycle.JudgeAlignSign(bot(0.1, 0, 0, 0), bot(rot(0.1, 10)[0], rot(0.1, 10)[1], 10, 10), 0.05, 1.5, 0.5, 1.0);
            Check("Sản phẩm gần trùng tâm xoay → không kết luận được (2), không ghi dấu",
                a2.Status == VmStatus.NoConclusion, "Status " + a2.Status);
            var a3 = VmCycle.JudgeAlignSign(p0, bot(2.0, 0, 3, 10), 0.05, 1.5, 0.5, 1.0);
            Check("Góc đo khác góc encoder quá dung sai → không đạt (3)", a3.Status == VmStatus.AngleOut, "Status " + a3.Status);
            var a4 = VmCycle.JudgeAlignSign(p0, bot(rot(2.0, 10)[0] + 0.5, rot(2.0, 10)[1], 10, 10), 0.05, 1.5, 0.5, 1.0);
            Check("Sai số dư vượt ngưỡng (gốc không nằm ở tâm xoay) → không đạt (4)", a4.Status == VmStatus.EccOut,
                $"Status {a4.Status} dư {a4.Residual:0.000}");
            var a5 = VmCycle.JudgeAlignSign(p0, bot(rot(2.0, 0.2)[0], rot(2.0, 0.2)[1], 0.2, 0.2), 0.05, 1.5, 0.5, 1.0);
            Check("Góc thử nhỏ hơn MinVerifyTurnDeg → không kết luận (8)", a5.Status == VmStatus.Code8, "Status " + a5.Status);
            var a6 = VmCycle.JudgeAlignSign(p0, new VmMeanCapture { Ok = true, X = rot(2.0, 10)[0], Y = rot(2.0, 10)[1], A = 10, AxX = 305, AxA = 10 },
                0.05, 1.5, 0.5, 1.0);
            Check("Trục X đã chạy giữa hai lần chụp → không kết luận (7)", a6.Status == VmStatus.AxisMoved, "Status " + a6.Status);
        }

        /// <summary>
        /// Bản sao Script của AlignBottom_ToolN (FakeVision) phải ra đúng lệnh của công thức đặt — đích = pose ô + hand-eye;
        /// Single Point Alignment; nhân dấu góc; A tuyệt đối = góc lúc chụp + góc bù — viết lại độc lập ở đây làm đáp số.
        /// </summary>
        private static void TestVmTwinMath()
        {
            Section("App chấp hành — AlignBottom_ToolN kèm ô đích ra đúng lệnh theo công thức đặt");
            var fv = VmSetup();
            if (fv == null) { Check("Chạy với vision giả", false); return; }
            try
            {
                var names = Core.Data.McDataManager.SysData.VisionOutputs;
                var calib = Core.Data.McDataManager.Model.Calib;
                var rng = new Random(20260913);
                int compared = 0, mismatch = 0, diagBad = 0;
                string first = null;

                for (int i = 0; i < 60; i++)
                {
                    int tool = 1 + i % 2;
                    double cellX = 300 + rng.NextDouble() * 150, cellY = 40 + rng.NextDouble() * 150, cellA = (rng.NextDouble() - 0.5) * 340;
                    double objX = (rng.NextDouble() - 0.5) * 5, objY = (rng.NextDouble() - 0.5) * 5, objA = (rng.NextDouble() - 0.5) * 340;
                    double aNow = rng.NextDouble() * 360;

                    // đáp số — mọi số đi qua float32 như SDK
                    double fCellX = (float)cellX, fCellY = (float)cellY, fCellA = (float)cellA;
                    double fObjX = (float)objX, fObjY = (float)objY, fObjA = (float)objA, fA = (float)aNow;
                    // Đích Y quy về hệ camera dưới (ngược chiều máy) — Flow thấy đúng số này (VmCycle.BottomYSign).
                    double tarX = fCellX + calib.HandEyeDx(tool), tarY = VmCycle.BottomYSign * (fCellY + calib.HandEyeDy(tool));
                    // Góc đích quy về hệ góc camera dưới: mốc góc − góc LocateTop (hai camera đo góc ngược chiều).
                    double tarA = AxisAngle.Wrap(calib.HandEyeAngleRef(tool) - fCellA);
                    double dx, dy, dt;
                    SinglePointAlignMath.Solve(tarX, tarY, tarA, fObjX, fObjY, fObjA, out dx, out dy, out dt);
                    double dA = SinglePointAlignMath.NormalizeDeg(calib.AlignAngleSign(tool) * dt);
                    double aExpect = AxisAngle.Wrap(fA + dA);
                    double eccExpect = Math.Sqrt(fObjX * fObjX + fObjY * fObjY);

                    // App gửi nguyên văn pose ô, Flow soi và tính
                    var gl = new Dictionary<string, double>
                    {
                        { names.GvAxisX, 300 }, { names.GvAxisY2, 80 },
                        { names.GvAxisA1, tool == 1 ? aNow : 0 }, { names.GvAxisA2, tool == 2 ? aNow : 0 }, { VmVars.RunId, VmRunIds.Next() }
                    };
                    fv.ForcedObjPose = new[] { objX, objY, objA };
                    VmCycle.AddPlaceGlobals(gl, new PlacedTopRecord { X = cellX, Y = cellY, A = cellA }, calib, tool);
                    var req = VmCycle.AlignBottomRequest(names, tool, gl, true);
                    var r = fv.CallFlowAsync(req, CancellationToken.None).GetAwaiter().GetResult();
                    if (!r.Ok) { first = first ?? r.Error; continue; }
                    compared++;

                    double eX = Math.Abs(r.Get(VmOut.CmdX) - dx), eY = Math.Abs(r.Get(VmOut.CmdY2) - dy);
                    double eA = AxisAngle.Distance(r.Get(VmOut.CmdA), aExpect);
                    if (!(eX <= 1e-3 && eY <= 1e-3 && eA <= 1e-3))
                    {
                        mismatch++;
                        first = first ?? $"#{i} tay {tool}: lệch X {eX:0.00000} Y {eY:0.00000} A {eA:0.00000}";
                    }

                    if (r.GetInt(VmOut.Status) != VmStatus.Ok || AxisAngle.Distance(r.Get(VmOut.DiagDA), dA) > 1e-3 ||
                        Math.Abs(r.Get(VmOut.DiagEcc) - eccExpect) > 1e-3)
                    {
                        diagBad++;
                        first = first ?? $"#{i}: Status {r.GetInt(VmOut.Status)}, góc bù {r.Get(VmOut.DiagDA):0.000}/{dA:0.000}, lệch tâm {r.Get(VmOut.DiagEcc):0.000}/{eccExpect:0.000}";
                    }
                }

                Check("Chạy đủ 60 bộ số ngẫu nhiên qua Flow giả", compared == 60, first);
                Check("CmdX_mm / CmdY2_mm / CmdA_deg của Flow trùng đáp số (≤ 0.001 mm, ≤ 0.001°)", mismatch == 0, first);
                Check("Góc bù và lệch tâm Flow báo (App so với ngưỡng Setting) trùng đáp số", diagBad == 0, first);
                Check("Flow đọc pose ô và hằng số từ BIẾN TOÀN CỤC, đúng của tay vừa gọi",
                    fv.LastPlaceGlobals != null && fv.LastPlaceGlobals[VmVars.HasTarget] == 1 &&
                    Near(fv.LastPlaceGlobals[VmVars.HandEyeDx], calib.HandEyeDx(2)),
                    fv.LastPlaceGlobals == null ? "(không có)" : string.Join(", ", fv.LastPlaceGlobals.Select(kv => kv.Key + "=" + kv.Value.ToString("0.###"))));

                // Bù đặt: lệnh phải dịch đúng bằng lượng bù, không hơn không kém. Chạy hai lần cùng một
                // ảnh giả, chỉ khác lượng bù, rồi trừ nhau — tách hẳn ảnh hưởng của bù ra khỏi phép tính đặt.
                fv.ForcedObjPose = new[] { 0.3, -0.2, 4.0 };
                var target = new PlacedTopRecord { X = 380, Y = 90, A = 12 };
                Func<Core.Data.Model.CellOffset, VmCallResult> runTrim = trim =>
                {
                    var gt = new Dictionary<string, double>
                    {
                        { names.GvAxisX, 300 }, { names.GvAxisY2, 80 }, { names.GvAxisA1, 30 }, { names.GvAxisA2, 0 },
                        { VmVars.RunId, VmRunIds.Next() }
                    };
                    VmCycle.AddPlaceGlobals(gt, target, calib, 1, trim);
                    return fv.CallFlowAsync(VmCycle.AlignBottomRequest(names, 1, gt, true), CancellationToken.None)
                             .GetAwaiter().GetResult();
                };

                var cmd0 = runTrim(null);
                var cmdXy = runTrim(new Core.Data.Model.CellOffset { PlaceDx = 0.05, PlaceDy = -0.07 });
                var cmdA = runTrim(new Core.Data.Model.CellOffset { PlaceDa = 0.5 });

                Check("Bù X/Y dịch lệnh đúng bằng lượng bù, không đụng góc",
                    cmd0.Ok && cmdXy.Ok &&
                    Math.Abs((cmdXy.Get(VmOut.CmdX) - cmd0.Get(VmOut.CmdX)) - 0.05) < 1e-3 &&
                    Math.Abs(VmCycle.BottomYSign * (cmdXy.Get(VmOut.CmdY2) - cmd0.Get(VmOut.CmdY2)) + 0.07) < 1e-3 &&
                    AxisAngle.Distance(cmdXy.Get(VmOut.CmdA), cmd0.Get(VmOut.CmdA)) < 1e-3,
                    $"ΔX {cmdXy.Get(VmOut.CmdX) - cmd0.Get(VmOut.CmdX):0.0000}, ΔY (chiều máy) {VmCycle.BottomYSign * (cmdXy.Get(VmOut.CmdY2) - cmd0.Get(VmOut.CmdY2)):0.0000}");

                Check("Bù góc dịch lệnh góc đúng bằng lượng bù × dấu góc của tay",
                    cmdA.Ok && Math.Abs(AxisAngle.ShortestDelta(cmd0.Get(VmOut.CmdA), cmdA.Get(VmOut.CmdA)) - 0.5 * calib.AlignAngleSign(1)) < 1e-3,
                    $"ΔA {AxisAngle.ShortestDelta(cmd0.Get(VmOut.CmdA), cmdA.Get(VmOut.CmdA)):0.0000}");
                fv.ForcedObjPose = null;
            }
            finally
            {
                VmRestore();
            }
        }

        /// <summary>Bước 2, 4, 5 — FakeVision bật quang học giả để phép đo đạt được.</summary>
        private static void TestVmCalib()
        {
            Section("App chấp hành — kiểm tra top-down, thử vòng xoay App tự phán; hand-eye đo bằng cách hút con can trong ô (quang học giả)");

            VmStopRoutine();
            AlarmManager.ForceClearAll();
            McControl.RequestReset();
            WaitUntil(() => McControl.Status.State != EMcState.Alarm, 3000);
            if (!McControl.Status.OrgOk)
            {
                McControl.RequestOrigin();
                WaitUntil(() => McControl.Status.State == EMcState.Homing, 2000);
                WaitUntil(() => McControl.Status.State != EMcState.Homing, 40000);
            }
            Check("Đã về gốc trước khi hiệu chuẩn", McControl.Status.OrgOk);

            var fv = VmSetup();
            if (fv == null) { Check("Chạy với vision giả", false); VmRestore(); return; }
            var sys = Core.Data.McDataManager.SysData;
            var names = sys.VisionOutputs;
            var model = Core.Data.McDataManager.Model;
            var calib = model.Calib;
            var x = McControl.Motion[AxisId.X];
            var y2 = McControl.Motion[AxisId.Y2];
            var ct = CancellationToken.None;

            try
            {
                fv.SimulateOptics = true;

                // ── Bước 2 ──
                calib.TopDownVerifiedAt = DateTime.MinValue;
                fv.MarkX = x.CurrentPos + 0.3;
                fv.MarkY = y2.CurrentPos - 0.2;
                double x0 = x.CurrentPos, y0 = y2.CurrentPos;
                var td = McControl.Calib.VerifyTopDownAsync(5.0, ct).GetAwaiter().GetResult();
                Check("Kiểm tra top-down: dấu chuẩn đứng yên trong hệ máy → App phán đạt",
                    td.Ok && td.Status == VmStatus.Ok, td.Reason + " | " + td.Note);
                Check("Đạt thì ghi ngày vào bản ghi của App", calib.IsTopDownVerified);
                Check("Kiểm tra xong trả X và Y2 về chỗ cũ", Near(x.CurrentPos, x0) && Near(y2.CurrentPos, y0));

                // ── Đèn CAM quanh mỗi lần chụp ──
                // Trang hiệu chuẩn trước 23/09 gọi thẳng vision nên các nút Trigger chụp trong lúc đèn còn TẮT
                // (Tuấn phát hiện). AfterFlow chạy ngay giữa lần chụp nên đọc được trạng thái đèn đúng lúc đó.
                var ioDev = McControl.Func.Io;
                bool camTopOn = false, camBottomOn = false;
                fv.AfterFlow = _ => { camTopOn = ioDev.GetDo(IoMap.DoLampCam2); camBottomOn = ioDev.GetDo(IoMap.DoLampCam1); };

                // Chờ đèn sáng đủ rồi mới kích Flow (Tuấn yêu cầu 2026-09-23). Đo CẬN DƯỚI: đặt 300 ms rồi đòi
                // lần chụp phải mất ít nhất 250 ms. Cận dưới chỉ hỏng khi App bỏ qua khoảng chờ, không phụ thuộc
                // máy nhanh hay chậm — cận trên thì có, nên không kiểm. Mỗi camera một số (Tuấn 2026-10-07): đặt 300 cho
                // camera đang chụp, 0 cho camera kia — App lấy nhầm số của camera kia thì không chờ và bài đổ.
                int bottomLampSaved = sys.BottomLampMs, topLampSaved = sys.TopLampMs;
                sys.TopLampMs = 300;
                sys.BottomLampMs = 0;
                var lampSw = System.Diagnostics.Stopwatch.StartNew();
                McControl.Calib.TriggerTopDownCalibFlowAsync(ct).GetAwaiter().GetResult();
                lampSw.Stop();
                Check("Nạp điểm top-down: đèn CAM2 sáng lúc chụp, CAM1 không", camTopOn && !camBottomOn,
                    $"CAM2 {camTopOn}, CAM1 {camBottomOn}");
                Check("Đặt chờ đèn CAM top-down 300 ms thì App chờ đèn sáng rồi mới kích Flow, không kích ngay",
                    lampSw.ElapsedMilliseconds >= 250, lampSw.ElapsedMilliseconds + " ms");
                Check("Chụp top-down xong thì tắt cả hai đèn CAM",
                    !ioDev.GetDo(IoMap.DoLampCam1) && !ioDev.GetDo(IoMap.DoLampCam2));

                camTopOn = camBottomOn = false;
                sys.TopLampMs = 0;
                sys.BottomLampMs = 300;
                lampSw.Restart();
                McControl.Calib.TriggerAlignCalibFlowAsync(1, ct).GetAwaiter().GetResult();
                lampSw.Stop();
                sys.BottomLampMs = bottomLampSaved;
                sys.TopLampMs = topLampSaved;
                fv.AfterFlow = null;
                Check("Đặt chờ đèn CAM bottom-up 300 ms thì App chờ theo số của CAM bottom-up, không lấy số top-down",
                    lampSw.ElapsedMilliseconds >= 250, lampSw.ElapsedMilliseconds + " ms");
                Check("Nạp điểm bottom-up: đèn CAM1 sáng lúc chụp, CAM2 không", camBottomOn && !camTopOn,
                    $"CAM1 {camBottomOn}, CAM2 {camTopOn}");
                Check("Chụp bottom-up xong thì tắt cả hai đèn CAM",
                    !ioDev.GetDo(IoMap.DoLampCam1) && !ioDev.GetDo(IoMap.DoLampCam2));

                // ── Nút "Z1 → Z_SAFE" ở thanh Jog ──
                // Trước 23/09 là nút về gốc trục Z; Tuấn đổi sang lệnh chạy tới Z_SAFE.
                var z1 = McControl.Motion[AxisId.Z1];
                McControl.Func.MoveAsync(AxisId.Z1, sys.ZSafe + 5, sys.GetAxis(AxisId.Z1).MoveSpeed, ct).GetAwaiter().GetResult();
                var zsafe = McControl.Calib.StepMoveZSafeAsync(1, ct).GetAwaiter().GetResult();
                Check("Nút Z1 → Z_SAFE đưa Z1 về đúng Z_SAFE",
                    zsafe.Ok && Near(z1.CurrentPos, sys.ZSafe), zsafe.Reason ?? $"Z1 {z1.CurrentPos:0.###} / Z_SAFE {sys.ZSafe:0.###}");

                // ── Bước 4 — App tự phán, không Flow nào ghi hằng số ──
                double alignX = model.AlignPosX(1);
                McControl.Func.MoveAsync(AxisId.X, alignX, sys.GetAxis(AxisId.X).MoveSpeed, ct).GetAwaiter().GetResult();
                calib.SetAlignVerified(1, 0, 0, 0, DateTime.MinValue);
                var a1 = McControl.Motion[AxisId.A1];
                double aStart = a1.CurrentPos;
                int saves0 = fv.SolutionSaveCount;
                var av = McControl.Calib.VerifyAlignRotationAsync(1, 10.0, ct).GetAwaiter().GetResult();
                Check("Thử vòng xoay: App so hai tư thế, đạt, sai số dư gần 0",
                    av.Ok && av.ResidualMm < 0.02, av.Reason + " | dư " + av.ResidualMm.ToString("0.0000") + " | " + av.Note);
                Check("Ghi dấu góc và X đã xác minh vào bản ghi của App",
                    calib.AlignAngleSign1 == 1 && Near(calib.AlignVerifiedPosX1, alignX),
                    $"dấu {calib.AlignAngleSign1}, X {calib.AlignVerifiedPosX1:0.###}");
                Check("Không ghi gì vào solution — hằng số chỉ nằm ở model", fv.SolutionSaveCount == saves0,
                    "lưu " + (fv.SolutionSaveCount - saves0) + " lần");
                Check("Thử xong trả trục A về góc xuất phát", AxisAngle.Distance(a1.CurrentPos, aStart) < 0.05,
                    $"A1={a1.CurrentPos:0.###} (đầu {aStart:0.###})");

                var avNeg = McControl.Calib.VerifyAlignRotationAsync(1, -10.0, ct).GetAwaiter().GetResult();
                Check("Góc thử âm vẫn ra đúng dấu (công thức theo encoder)", avNeg.Ok && avNeg.Sign == av.Sign, avNeg.Reason);

                fv.BottomAngleSign = -1;
                var avRev = McControl.Calib.VerifyAlignRotationAsync(1, 10.0, ct).GetAwaiter().GetResult();
                Check("Ảnh bottom-up quay ngược chiều trục A → dấu −1, App ghi theo",
                    avRev.Ok && avRev.Sign == -1 && calib.AlignAngleSign1 == -1, avRev.Reason);
                fv.BottomAngleSign = 1;

                // Output của Flow nối nhầm sang Flow khác: A xoay mà góc sản phẩm đứng yên. Bảng báo phải kèm số đo.
                fv.ForcedObjPose = new[] { 0.8, -0.6, 7.0 };
                var avStuck = McControl.Calib.VerifyAlignRotationAsync(1, 10.0, ct).GetAwaiter().GetResult();
                Check("Góc sản phẩm đứng yên khi A xoay → không đạt, bảng báo kèm góc encoder, góc đo, sai số dư, cao độ Z",
                    !avStuck.Ok && avStuck.Reason != null && avStuck.Reason.Contains("theo encoder") &&
                    avStuck.Reason.Contains("góc sản phẩm đổi +0.00°") && avStuck.Reason.Contains("sai số dư") &&
                    avStuck.Reason.Contains(AxisId.Z1), avStuck.Reason);
                fv.ForcedObjPose = null;
                calib.SetAlignVerified(1, 1, alignX, 0, DateTime.Now);

                // ── Xác định hand-eye: gắp trên CƠ CẤU KẸP, bảy nút (cách 25/09) ──
                //
                // Quang học giả: LocateTop luôn báo chi tiết ở (MarkX, MarkY) trong hệ MÁY, bất kể trục đứng đâu —
                // đúng tính chất mà Script TopAbs có trên máy thật, và cũng là thứ phép thử tổng quát dựa vào.
                // ForcedObjPose đóng vai camera bottom-up: lượng can lệch khỏi gốc hệ bottom-up sau khi gắp.
                // Hai camera đo góc ngược chiều nhau: camera trên báo chi tiết 2°, camera dưới báo con can 7° —
                // mốc góc là TỔNG, phải ra 9°.
                var jigPick = model.HandEyePick(1);
                var jigCam = model.HandEyeCamPos;
                fv.MarkX = jigCam.X + 0.4;
                fv.MarkY = jigCam.Y - 0.3;
                fv.MarkA = 2.0;
                fv.ForcedObjPose = new[] { 0.8, -0.6, 7.0 };
                int heShots = Math.Max(1, sys.CalibShotsPerPoint);

                McControl.Calib.HandEyeClear();
                var he1 = McControl.Calib.HandEyeGoToPickAsync(1, ct).GetAwaiter().GetResult();
                var he2 = McControl.Calib.HandEyePickAsync(1, ct).GetAwaiter().GetResult();
                Check("Nút 1 → 2: gắp được con can mẫu trên cơ cấu kẹp",
                    he1.Ok && he2.Ok, (he1.Reason ?? "") + " | " + (he2.Reason ?? ""));
                Check("Ngay sau khi gắp, trục còn đứng ĐÚNG điểm gắp đã dạy — encoder vào phép trừ là số của chỗ đó",
                    Near(x.CurrentPos, jigPick.X) && Near(y2.CurrentPos, jigPick.Y),
                    $"X={x.CurrentPos:0.###}/{jigPick.X:0.###}, Y2={y2.CurrentPos:0.###}/{jigPick.Y:0.###}");
                Check("Gắp xong phần mềm ghi sổ tay đang cầm hàng", McControl.Func.HasProduct(1));
                var heBusy = McControl.Calib.HandEyeGoToPickAsync(1, ct).GetAwaiter().GetResult();
                Check("Tay còn cầm hàng thì không cho bắt đầu đo lần nữa",
                    !heBusy.Ok && heBusy.Reason != null && heBusy.Reason.Contains("còn sản phẩm"), heBusy.Reason);

                // Nút 3 phải kích LocateTop KHI GANTRY ĐANG Ở điểm soi đã dạy. Chụp trong hook AfterFlow — đọc
                // encoder sau khi lệnh trả về thì nút 4 đã kịp chạy X, và bài kiểm thành đạt rỗng.
                double atFlowX = double.NaN, atFlowY = double.NaN;
                int locBefore = fv.LocateTopCount;
                fv.AfterFlow = flow => { if (flow == names.FlowLocateTop) { atFlowX = x.CurrentPos; atFlowY = y2.CurrentPos; } };
                var he3 = McControl.Calib.HandEyeLocateAsync(1, ct).GetAwaiter().GetResult();
                fv.AfterFlow = null;
                Check("Nút 3 soi được chi tiết đích bằng LocateTop", he3.Ok, he3.Reason);
                Check("Lúc kích LocateTop, gantry đứng ĐÚNG vị trí soi đã dạy — không phải chỗ soi ô tray",
                    Near(atFlowX, jigCam.X) && Near(atFlowY, jigCam.Y) &&
                    !Near(jigCam.X, model.AssyTray.CamPosFor(0, 0).X),
                    $"lúc kích ({atFlowX:0.###}, {atFlowY:0.###}), điểm soi đã dạy ({jigCam.X:0.###}, {jigCam.Y:0.###}), chỗ soi ô (1,1) X {model.AssyTray.CamPosFor(0, 0).X:0.###}");
                Check("Nút 3 kích LocateTop đúng số ảnh cấu hình",
                    fv.LocateTopCount - locBefore == heShots, (fv.LocateTopCount - locBefore) + "/" + heShots);

                // Nút 5 trước nút 4: X chưa ở vị trí soi CAM dưới. Chặn theo VỊ TRÍ THẬT chứ không theo "đã bấm
                // nút 4" — người bấm có thể jog đi sau đó.
                var heEarlyX = McControl.Calib.HandEyeCommitAsync(1, ct).GetAwaiter().GetResult();
                Check("X chưa ở vị trí soi CAM dưới thì không ghi hand-eye, lý do nêu đúng chỗ đó",
                    !heEarlyX.Ok && heEarlyX.Reason != null && heEarlyX.Reason.Contains("vị trí soi CAM dưới"), heEarlyX.Reason);

                var he4 = McControl.Calib.HandEyeGoToAlignAsync(1, ct).GetAwaiter().GetResult();
                Check("Nút 4 đưa tay tới vị trí soi CAM bottom-up",
                    he4.Ok && Near(x.CurrentPos, model.AlignPosX(1)), he4.Reason ?? $"X={x.CurrentPos:0.###}/{model.AlignPosX(1):0.###}");

                // Jog A1 đi 4° giữa nút 4 và nút 5 — thanh Jog của trang cho làm việc này. ForcedObjPose trả góc con
                // can cố định bất kể A, nên bài mốc góc = 9° ở dưới KHÔNG bắt được lỗi này: phải chụp A1 ngay lúc kích
                // AlignBottom, và đếm số lần kích để hook không im lặng vì sai tên Flow.
                double heAPick = model.PickAngle(1);
                bool heJogA = McControl.Func.MoveAsync(AxisId.A1, AxisAngle.Wrap(heAPick + 4.0), a1.Config.MoveSpeed, ct)
                    .GetAwaiter().GetResult() && AxisAngle.Distance(a1.CurrentPos, heAPick) > 3.0;
                int heAlignShots = 0;
                double heWorstA = 0;
                fv.AfterFlow = flow =>
                {
                    if (flow != names.FlowAlignBottom(1)) return;
                    heAlignShots++;
                    heWorstA = Math.Max(heWorstA, AxisAngle.Distance(a1.CurrentPos, heAPick));
                };
                // Lệch tâm Y của camera dưới (−0.6) quy về chiều máy bằng cách đổi dấu (VmCycle.BottomYSign, 2026-09-30).
                double expectDx = jigPick.X + 0.8 - fv.MarkX, expectDy = jigPick.Y + VmCycle.BottomYSign * -0.6 - fv.MarkY;
                var hePick = McControl.Calib.HandEyeCommitAsync(1, ct).GetAwaiter().GetResult();
                fv.AfterFlow = null;
                Check("Jog A1 đi 4° trước nút 5 → nút 5 xoay A1 về đúng góc lúc gắp rồi mới kích AlignBottom",
                    heJogA && heAlignShots == heShots && heWorstA < 0.05,
                    $"jog {(heJogA ? "được" : "KHÔNG được")}, kích {heAlignShots}/{heShots} ảnh, A1 lệch góc lúc gắp tối đa {heWorstA:0.###}°");
                Check("Hand-eye = encoder lúc gắp + lệch tâm bottom-up (Y đổi dấu về chiều máy) − vị trí chi tiết top-down",
                    hePick.Ok && Near(hePick.Dx, expectDx) && Near(hePick.Dy, expectDy) && Near(calib.HandEye1Dx, expectDx),
                    hePick.Reason ?? $"dx {hePick.Dx:0.###}/{expectDx:0.###}, dy {hePick.Dy:0.###}/{expectDy:0.###}");
                Check("Mốc góc = góc con can ở camera dưới + góc chi tiết ở camera trên, đo cùng lần gắp, ghi vào model",
                    hePick.Ok && Math.Abs(hePick.AngleRef - 9.0) < 0.05 && calib.HandEye1AngleRef.HasValue && Math.Abs(calib.HandEye1AngleRef.Value - 9.0) < 0.05,
                    $"mốc góc {hePick.AngleRef:0.###} (chờ 9)");
                Check("Ghi hand-eye xong KHÔNG xoá sổ tay cầm hàng — con can còn nằm đó thật", McControl.Func.HasProduct(1));

                // Đặt lại chính con can đó vào chỗ vừa gắp, A chưa xoay: Single Point Alignment phải ra góc bù 0 và
                // đưa tâm xoay về đúng chỗ gắp — đúng việc Create Reference lẽ ra làm.
                var gBack = new Dictionary<string, double>();
                VmCycle.AddPlaceGlobals(gBack, new PlacedTopRecord { X = fv.MarkX, Y = fv.MarkY, A = fv.MarkA }, calib, 1);
                // Tính đúng như Group Cmd: mọi số lấy từ bộ biến App gửi (Y ở hệ camera dưới), rồi đổi dấu Y lệnh về chiều máy.
                double sdx, sdy, sdt;
                SinglePointAlignMath.Solve(gBack[VmVars.CellX] + gBack[VmVars.HandEyeDx], gBack[VmVars.CellY] + gBack[VmVars.HandEyeDy],
                    gBack[VmVars.CellA], 0.8, -0.6, 7.0, out sdx, out sdy, out sdt);
                sdy *= VmCycle.BottomYSign;
                Check("Đặt lại chính con can đó vào chỗ vừa gắp: góc bù 0, tâm xoay về đúng chỗ gắp",
                    Math.Abs(AxisAngle.ShortestDelta(0, sdt)) < 0.05 && Math.Abs(sdx - jigPick.X) < 0.05 && Math.Abs(sdy - jigPick.Y) < 0.05,
                    $"góc bù {sdt:0.###}, X {sdx:0.###}/{jigPick.X:0.###}, Y2 {sdy:0.###}/{jigPick.Y:0.###}");

                // ── Phép thử tổng quát (nút 6 → 7 → 8) ở HAI chỗ Jog khác nhau ──
                //
                // Không khẳng định trên Ok: Ok đạt cả khi con số sai. Khẳng định trên GIÁ TRỊ điểm đích, và ở hai
                // chỗ Jog khác nhau phải ra CÙNG một số. Rồi BƠM SAI LỆCH vào pose top-down để chắc con số thật sự
                // chảy qua phép so, chứ không phải một hằng số nào đó tình cờ khớp.
                var heTargetEarly = McControl.Calib.HandEyeCheckTargetAsync(1, ct).GetAwaiter().GetResult();
                Check("Bấm 8 khi chưa bấm 6 → từ chối, nêu tên nút 6", !heTargetEarly.Ok &&
                    heTargetEarly.Reason != null && heTargetEarly.Reason.Contains("6 · Soi chi tiết"), heTargetEarly.Reason);

                var jogSpots = new[] { new[] { jigCam.X + 7.0, jigCam.Y + 5.0 }, new[] { jigCam.X - 9.0, jigCam.Y - 4.0 } };
                var cmdX = new List<double>();
                var cmdY = new List<double>();
                bool jogRan = true, sixMovedNothing = true, eightStayedAtAlign = true, eightNeedsAlign = false;
                string eightNeedsAlignWhy = null;
                for (int k = 0; k < jogSpots.Length; k++)
                {
                    jogRan &= McControl.Func.MoveToCellAsync(AxisId.Y2, jogSpots[k][1], jogSpots[k][0], ct).GetAwaiter().GetResult();
                    double bx = x.CurrentPos, by = y2.CurrentPos;
                    int vmBefore6 = fv.VmCallCount;
                    var h6 = McControl.Calib.HandEyeLocateHereAsync(1, ct).GetAwaiter().GetResult();
                    sixMovedNothing &= h6.Ok && Near(x.CurrentPos, bx) && Near(y2.CurrentPos, by)
                                       && fv.VmCallCount - vmBefore6 == 1;
                    if (k == 0)
                    {
                        // Nút 8 không chạy X nữa (tách 26/09): tay chưa ra vị trí soi thì phải từ chối và nêu nút 7,
                        // không chụp AlignBottom ở chỗ Jog.
                        var h8Here = McControl.Calib.HandEyeCheckTargetAsync(1, ct).GetAwaiter().GetResult();
                        eightNeedsAlign = !h8Here.Ok && h8Here.Reason != null && h8Here.Reason.Contains("Tới vị trí soi CAM dưới") &&
                                          Near(x.CurrentPos, bx);
                        eightNeedsAlignWhy = h8Here.Reason;
                    }
                    var h7 = McControl.Calib.HandEyeGoToAlignForTestAsync(1, ct).GetAwaiter().GetResult();
                    var h8 = McControl.Calib.HandEyeCheckTargetAsync(1, ct).GetAwaiter().GetResult();
                    if (!h7.Ok || !h8.Ok) { Check("Nút 7 → 8 chạy được ở chỗ Jog thứ " + (k + 1), false, h7.Reason ?? h8.Reason); break; }
                    eightStayedAtAlign &= Near(x.CurrentPos, model.AlignPosX(1)) && !Near(x.CurrentPos, h8.TargetX);
                    cmdX.Add(h8.TargetX);
                    cmdY.Add(h8.TargetY2);
                }
                Check("Nút 6 kích LocateTop TẠI CHỖ — không chạy trục, đúng MỘT ảnh (Tuấn bỏ lấy trung bình 26/09)", jogRan && sixMovedNothing);
                Check("Tay chưa ra vị trí soi CAM dưới → nút 8 từ chối, nêu nút 7, không chạy trục", eightNeedsAlign, eightNeedsAlignWhy);
                Check("Nút 8 KHÔNG chạy tới lệnh — tay vẫn đứng ở vị trí soi CAM dưới", eightStayedAtAlign);
                Check("Nút 8 ra điểm đích đúng chỗ đã gắp, ở CẢ HAI chỗ Jog khác nhau",
                    cmdX.Count == 2 && Near(cmdX[0], jigPick.X) && Near(cmdX[1], jigPick.X) &&
                    Near(cmdY[0], jigPick.Y) && Near(cmdY[1], jigPick.Y),
                    cmdX.Count == 2 ? $"X {cmdX[0]:0.###} và {cmdX[1]:0.###} / gắp {jigPick.X:0.###}; Y2 {cmdY[0]:0.###} và {cmdY[1]:0.###} / gắp {jigPick.Y:0.###}"
                                    : "chỉ chạy được " + cmdX.Count + " chỗ");

                // Nút 6 có chọn ô tray lắp ráp (Tuấn thêm 2026-09-26): CAM tới vị trí soi ô — cùng CamPosFor với S05 —
                // rồi kích MỘT lần. Xong đưa tay về vị trí soi CAM dưới cho các bài bên dưới bấm thẳng nút 8.
                var cellCam = model.AssyTray.CamPosFor(1, 2);
                var rowAxis = McControl.Motion[model.AssyTray.AxisRow];
                int vmBeforeCell = fv.VmCallCount;
                var h6Cell = McControl.Calib.HandEyeLocateHereAsync(1, 1, 2, ct).GetAwaiter().GetResult();
                Check("Nút 6 chọn ô (2,3): CAM tới vị trí soi ô đó rồi kích LocateTop một lần",
                    h6Cell.Ok && Near(x.CurrentPos, cellCam.X) && Near(rowAxis.CurrentPos, cellCam.Y) && fv.VmCallCount - vmBeforeCell == 1,
                    h6Cell.Reason ?? $"X {x.CurrentPos:0.###}/{cellCam.X:0.###}, {model.AssyTray.AxisRow} {rowAxis.CurrentPos:0.###}/{cellCam.Y:0.###}, " +
                                     $"{fv.VmCallCount - vmBeforeCell} lần gọi VM");
                var h6Out = McControl.Calib.HandEyeLocateHereAsync(1, model.AssyTray.Rows, 0, ct).GetAwaiter().GetResult();
                Check("Nút 6 chọn ô ngoài lưới tray lắp ráp → từ chối",
                    !h6Out.Ok && h6Out.Reason != null && h6Out.Reason.Contains("ngoài lưới"), h6Out.Reason);
                McControl.Calib.HandEyeGoToAlignForTestAsync(1, ct).GetAwaiter().GetResult();

                // Bơm 0.2 mm vào pose top-down: lệch phải ra ĐÚNG 0.2, không phải 0. Nếu phép so lấy số ở đâu khác
                // (hằng số, hay chính encoder hiện tại) thì bài này đổ.
                fv.MarkX += 0.2;
                var h6Shift = McControl.Calib.HandEyeLocateHereAsync(1, ct).GetAwaiter().GetResult();
                var h7Shift = McControl.Calib.HandEyeCheckTargetAsync(1, ct).GetAwaiter().GetResult();
                Check("Pose top-down lệch 0.2 mm → nút 8 báo điểm đích lệch đúng 0.2 mm so với chỗ đã gắp",
                    h6Shift.Ok && h7Shift.Ok && Math.Abs((h7Shift.TargetX - jigPick.X) - 0.2) < 0.01,
                    (h7Shift.Reason ?? "") + $" lệch {h7Shift.TargetX - jigPick.X:0.###} (chờ 0.200)");
                double teachX, teachY, teachA;
                Check("Nút 5 lưu điểm teach hand-eye (pose bước 3); nút 6 in ΔX +0.200, ΔY +0.000 so với điểm đó (Tuấn 30/09)",
                    calib.TryGetHandEyeTarget(1, out teachX, out teachY, out teachA) && Near(teachX, fv.MarkX - 0.2) &&
                    DeltaNear(h6Shift.Text, "so với điểm teach hand-eye tay #1: ", 0.2, 0),
                    h6Shift.Text);
                // Nút 8 chạy sau nút 6 nhưng không soi top-down, nên khung top-down vẫn giữ kết quả của nút 6. Khung đó in Δ
                // TRONG ẢNH so với ảnh master: nút 6 soi ở vị trí soi CAM dưới (nút 7 vừa đưa X tới), không phải chỗ chụp
                // master, nên Δ = 0.2 trừ quãng trục đã chạy từ chỗ chụp master (Tuấn 01/10).
                double mAxX, mAxY;
                bool hasMasterAxis = calib.TryGetHandEyeMasterAxis(1, out mAxX, out mAxY);
                double expDx = 0.2 - (x.CurrentPos - mAxX), expDy = -(rowAxis.CurrentPos - mAxY);
                Check("Khung top-down trang Main in Δ trong ảnh so với ảnh master tay #1 — trừ quãng trục đã chạy (Tuấn 01/10)",
                    hasMasterAxis && Near(mAxX, model.HandEyeCamPos.X) && Near(mAxY, model.HandEyeCamPos.Y) &&
                    Math.Abs(expDx - 0.2) > 1 && DeltaNear(VisionReadout.Top, "So với master hand-eye tay #1: ", expDx, expDy),
                    $"master trục ({mAxX:0.###}, {mAxY:0.###}), chờ ΔX {expDx:+0.000;-0.000} ΔY {expDy:+0.000;-0.000} — " + VisionReadout.Top);
                fv.MarkX -= 0.2;

                // Đầu hút rộng (Tuấn 2026-09-30): con can xê dịch trên đầu hút giữa lúc đo hand-eye và lúc đặt. Camera dưới
                // đo X cùng chiều máy, Y NGƯỢC chiều máy — con can dịch +0.3 mm theo X và Y2 thì ObjX tăng 0.3, ObjY GIẢM 0.3.
                // Điểm đích phải lùi đúng 0.3 mm cả hai trục theo chiều máy. Trước khi sửa, Y2 tiến 0.3 thay vì lùi.
                var poseKeep = fv.ForcedObjPose;
                fv.ForcedObjPose = new[] { poseKeep[0] + 0.3, poseKeep[1] - 0.3, poseKeep[2] };
                var h6Slip = McControl.Calib.HandEyeLocateHereAsync(1, ct).GetAwaiter().GetResult();
                var h8Slip = McControl.Calib.HandEyeCheckTargetAsync(1, ct).GetAwaiter().GetResult();
                fv.ForcedObjPose = poseKeep;
                Check("Con can xê dịch trên đầu hút +0.3 mm theo X và Y2 (camera dưới đo ObjY −0.3) → điểm đích lùi 0.3 mm cả X lẫn Y2",
                    h6Slip.Ok && h8Slip.Ok && Math.Abs((h8Slip.TargetX - jigPick.X) + 0.3) < 0.01 &&
                    Math.Abs((h8Slip.TargetY2 - jigPick.Y) + 0.3) < 0.01,
                    (h8Slip.Reason ?? h6Slip.Reason ?? "") +
                    $" lệch X {h8Slip.TargetX - jigPick.X:0.###}, Y2 {h8Slip.TargetY2 - jigPick.Y:0.###} (chờ −0.300, −0.300)");

                // Hàng rào lưới teach phải TẮT ở phép thử: không nhắm vào ô nào nên hàng rào vô nghĩa. Đẩy pose ra
                // xa hẳn lưới rồi khẳng định nút 8 vẫn chạy (Auto thì đúng ô đó bị bỏ).
                double markXKeep = fv.MarkX, markYKeep = fv.MarkY;
                fv.MarkX = jigCam.X + 25.0;
                fv.MarkY = jigCam.Y + 18.0;
                var h6Far = McControl.Calib.HandEyeLocateHereAsync(1, ct).GetAwaiter().GetResult();
                var h7Far = McControl.Calib.HandEyeCheckTargetAsync(1, ct).GetAwaiter().GetResult();
                Check("Điểm đích nằm xa lưới teach vẫn tính được — phép thử không bật hàng rào lưới",
                    h6Far.Ok && h7Far.Ok, (h6Far.Reason ?? "") + " | " + (h7Far.Reason ?? ""));
                fv.MarkX = markXKeep;
                fv.MarkY = markYKeep;

                // Gắp lệch quá ngưỡng lệch tâm: không ghi, vì lần gắp đó không đại diện cho lần gắp thật.
                fv.ForcedObjPose = new[] { 9.0, 0.0, 0.0 };
                double dxBefore = calib.HandEye1Dx;
                var heFar = McControl.Calib.HandEyeCommitAsync(1, ct).GetAwaiter().GetResult();
                Check("Con can lệch tâm quá ngưỡng → không ghi hand-eye",
                    !heFar.Ok && Near(calib.HandEye1Dx, dxBefore), heFar.Reason);
                fv.ForcedObjPose = new[] { 0.8, -0.6, 7.0 };

                // ── Nút 9: tới điểm đích vừa tính, Z dừng ở điểm giảm tốc (Tuấn yêu cầu 2026-09-26) ──
                // Soi lại (nút 6) làm điểm đích cũ hết hiệu lực: nút 9 phải đòi bấm 8, không chạy tới số của pose cũ.
                var h6For9 = McControl.Calib.HandEyeLocateHereAsync(1, ct).GetAwaiter().GetResult();
                var h9Stale = McControl.Calib.HandEyeGoToTargetAsync(1, ct).GetAwaiter().GetResult();
                Check("Soi lại (nút 6) mà chưa tính lại (nút 8) → nút 9 từ chối, nêu tên nút 8",
                    h6For9.Ok && !h9Stale.Ok && h9Stale.Reason != null && h9Stale.Reason.Contains("Trigger AlignBottom"), h9Stale.Reason);

                // Chưa teach điểm giảm tốc thì không có cao độ nào để dừng lơ lửng: từ chối trước khi chạy trục nào.
                var h8For9 = McControl.Calib.HandEyeCheckTargetAsync(1, ct).GetAwaiter().GetResult();
                var assy = model.AssyTray;
                double slowKeep = assy.Z_SlowZ1, xBeforeNoSlow = x.CurrentPos, zBeforeNoSlow = z1.CurrentPos;
                assy.Z_SlowZ1 = 0;
                var h9NoSlow = McControl.Calib.HandEyeGoToTargetAsync(1, ct).GetAwaiter().GetResult();
                assy.Z_SlowZ1 = slowKeep;
                Check("Chưa teach điểm giảm tốc của Z1 → nút 9 từ chối, không trục nào nhúc nhích",
                    h8For9.Ok && !h9NoSlow.Ok && Near(x.CurrentPos, xBeforeNoSlow) && Near(z1.CurrentPos, zBeforeNoSlow),
                    h9NoSlow.Reason ?? h8For9.Reason);

                // Cơ cấu kẹp gá nhiều sản phẩm: soi sang sản phẩm khác cách 50 mm thì điểm đích cách chỗ gắp 50 mm, và nút 9
                // phải chạy tới đó — không có hàng rào nào theo khoảng cách tới chỗ đã gắp (Tuấn làm vậy trên máy 26/09).
                fv.MarkX += 50.0;
                var h6Other = McControl.Calib.HandEyeLocateHereAsync(1, ct).GetAwaiter().GetResult();
                var h8Other = McControl.Calib.HandEyeCheckTargetAsync(1, ct).GetAwaiter().GetResult();
                int gentle9 = ((FakeAxis)y2).GentleMoves;
                var h9 = McControl.Calib.HandEyeGoToTargetAsync(1, ct).GetAwaiter().GetResult();
                fv.MarkX -= 50.0;
                double yLow9 = Core.Data.McDataManager.SysData.GetAxis(AxisId.Y2).MoveSpeedLow;
                Check("Nút 9: Y2 tới điểm đích ở mức Auto thấp, gia tốc giảm theo — như Auto (Tuấn 01/10)",
                    ((FakeAxis)y2).GentleMoves - gentle9 == 1 && Near(((FakeAxis)y2).LastGentleSpeed, yLow9),
                    $"{((FakeAxis)y2).GentleMoves - gentle9} lệnh dịu, tốc độ {((FakeAxis)y2).LastGentleSpeed:0.###}/{yLow9:0.###}");
                double zSlow1 = assy.ZSlowFor(AxisId.Z1);
                Check("Nút 9 tới điểm đích của sản phẩm khác cách 50 mm: X, Y2, A1 đúng điểm nút 8 tính; Z1 dừng ở điểm giảm tốc, chưa xuống cao độ đặt, vẫn giữ hút",
                    h6Other.Ok && h8Other.Ok && h9.Ok && Math.Abs((h8Other.TargetX - jigPick.X) - 50.0) < 0.01 &&
                    Near(x.CurrentPos, h8Other.TargetX) && Near(y2.CurrentPos, h8Other.TargetY2) &&
                    AxisAngle.Distance(a1.CurrentPos, AxisAngle.Wrap(h8Other.TargetA)) < 0.05 &&
                    Near(z1.CurrentPos, zSlow1) && zSlow1 < assy.ZPickFor(AxisId.Z1) && McControl.Func.HasProduct(1),
                    h9.Reason ?? h8Other.Reason ?? $"X {x.CurrentPos:0.###}/{h8Other.TargetX:0.###}, Y2 {y2.CurrentPos:0.###}/{h8Other.TargetY2:0.###}, " +
                                 $"A1 {a1.CurrentPos:0.###}/{h8Other.TargetA:0.###}, Z1 {z1.CurrentPos:0.###}/{zSlow1:0.###}");
                // Nút trả bên dưới chạy từ ĐÚNG trạng thái nút 9 để lại — Z1 đang ở điểm giảm tốc trên sản phẩm khác.

                // ── Đường trả hàng: HẠ Z rồi mới nhả ──
                // Chỉ khẳng định HasProduct == false thì rơi hàng từ trên cao cũng đạt, vì DI của FakeIo đi theo DO.
                // Chụp Z đúng lúc van hút đổi trạng thái.
                // Jog A1 đi 6° trong lúc thử rồi mới bấm trả: hạ xuống ở góc khác góc đã nhấc lên thì con can không
                // ngồi vào chi tiết. Chụp A1 cùng lúc với Z, đúng lúc van hút đổi.
                bool heJogARet = McControl.Func.MoveAsync(AxisId.A1, AxisAngle.Wrap(heAPick - 6.0), a1.Config.MoveSpeed, ct)
                    .GetAwaiter().GetResult() && AxisAngle.Distance(a1.CurrentPos, heAPick) > 5.0;
                double zAtRelease = double.NaN, aAtRelease = double.NaN;
                var heIo = (FakeIo)McControl.Io;
                heIo.AfterSetDo = (bit, on) =>
                {
                    if (bit == IoMap.DoVacuum1 && !on) { zAtRelease = z1.CurrentPos; aAtRelease = a1.CurrentPos; }
                };
                var heRet = McControl.Calib.HandEyeReturnAsync(1, ct).GetAwaiter().GetResult();
                heIo.AfterSetDo = null;
                Check("Nút trả: hạ Z xuống cao độ đặt của tray lắp ráp RỒI mới nhả hút",
                    heRet.Ok && Near(zAtRelease, model.AssyTray.ZPickFor(AxisId.Z1)),
                    heRet.Reason ?? $"Z lúc nhả {zAtRelease:0.###}, cao độ đặt {model.AssyTray.ZPickFor(AxisId.Z1):0.###}");
                Check("Jog A1 đi 6° trước khi trả → nút trả xoay A1 về góc hút rồi mới nhả",
                    heJogARet && heRet.Ok && AxisAngle.Distance(aAtRelease, heAPick) < 0.05,
                    $"jog {(heJogARet ? "được" : "KHÔNG được")}, A1 lúc nhả {aAtRelease:0.###}°, góc hút {heAPick:0.###}°");
                Check("Trả xong: về vị trí gắp đã dạy, Z lên cao độ an toàn, sổ tay hút xoá",
                    !McControl.Func.HasProduct(1) && Near(z1.CurrentPos, McControl.Motion.ZSafe) &&
                    Near(x.CurrentPos, jigPick.X) && Near(y2.CurrentPos, jigPick.Y),
                    $"Z1={z1.CurrentPos:0.###}, X={x.CurrentPos:0.###}/{jigPick.X:0.###}, Y2={y2.CurrentPos:0.###}/{jigPick.Y:0.###}");

                // ── Hand-eye đã ghi thì nút 6 → 9 dùng được ngay, không làm lại nút 1 → 5 (Tuấn 26/09) ──
                // Mô phỏng mở lại App: xoá hết tiến trình, hút con can bằng trang Manual — không qua nút 2.
                McControl.Calib.HandEyeClear();
                McControl.Func.SetVacuum(1, true);
                var r6 = McControl.Calib.HandEyeLocateHereAsync(1, ct).GetAwaiter().GetResult();
                var r7 = McControl.Calib.HandEyeGoToAlignForTestAsync(1, ct).GetAwaiter().GetResult();
                var r8 = McControl.Calib.HandEyeCheckTargetAsync(1, ct).GetAwaiter().GetResult();
                var r9 = McControl.Calib.HandEyeGoToTargetAsync(1, ct).GetAwaiter().GetResult();
                Check("Hand-eye đã ghi, mở lại App (không bấm 1 → 5): nút 6 → 7 → 8 → 9 chạy được, tay tới đúng điểm nút 8 tính",
                    r6.Ok && r7.Ok && r8.Ok && r9.Ok && Near(x.CurrentPos, r8.TargetX) && Near(y2.CurrentPos, r8.TargetY2),
                    r6.Reason ?? r7.Reason ?? r8.Reason ?? r9.Reason);
                Check("Con can không gắp bằng nút 2 → không in lệch so với chỗ đã gắp (không có chỗ gắp nào để so)",
                    r8.Ok && r8.Text != null && !r8.Text.Contains("chỗ đã gắp"), r8.Text);
                McControl.Func.EnsureZSafeAsync(ct).GetAwaiter().GetResult();

                fv.ForcedObjPose = null;
                fv.MarkA = 0;
                McControl.Calib.HandEyeClear();
                McControl.Func.SetVacuum(1, false);
                Core.Data.McDataManager.RunData.Nozzle1.HasProduct = false;
                Core.Data.McDataManager.RunData.Nozzle1.SourceCellIndex = -1;

                // ── Kiểm tra hiệu chuẩn (cổng) ──
                fv = VmSetup();
                var gate = McControl.Calib.CheckCalib();
                Check("Bản ghi đủ kiểm tra top-down, thử vòng xoay, hand-eye → hiệu chuẩn sẵn sàng", gate.Ready, gate.Describe());
                var mcal = Core.Data.McDataManager.Model.Calib;
                var savedRef2 = mcal.HandEye2AngleRef;
                mcal.HandEye2AngleRef = null;
                var gateOld = McControl.Calib.CheckCalib();
                mcal.HandEye2AngleRef = savedRef2;
                Check("Hand-eye tay #2 đo bằng bản cũ, chưa có mốc góc → cổng chặn, bảo chạy lại bước xác định hand-eye",
                    !gateOld.Ready && gateOld.Block != null && gateOld.Block.Contains("hand-eye tay #2 là bản ghi cũ"), gateOld.Describe());
                // Hand-eye đo trước khi sửa chiều Y camera dưới (30/09): HandEyeDy mang lệch tâm Y ngược dấu → chặn.
                mcal.HandEye2YFixed = false;
                var gateOldY = McControl.Calib.CheckCalib();
                mcal.HandEye2YFixed = true;
                Check("Hand-eye tay #2 đo trước khi sửa chiều Y camera dưới → cổng chặn, bảo chạy lại bước xác định hand-eye",
                    !gateOldY.Ready && gateOldY.Block != null && gateOldY.Block.Contains("hand-eye tay #2 là bản ghi cũ"), gateOldY.Describe());
                var savedSign2 = Core.Data.McDataManager.Model.Calib.AlignAngleSign2;
                Core.Data.McDataManager.Model.Calib.SetAlignVerified(2, 0, 0, 0, DateTime.MinValue);
                var gate3 = McControl.Calib.CheckCalib();
                Core.Data.McDataManager.Model.Calib.SetAlignVerified(2, savedSign2, Core.Data.McDataManager.Model.AlignPosX(2), 0, DateTime.Now);
                Check("Xoá dấu góc tay #2 trong bản ghi → cổng chặn",
                    !gate3.Ready && gate3.Block != null && gate3.Block.Contains("dấu góc tay #2"), gate3.Describe());
            }
            finally
            {
                VmRestore();
            }
        }

        /// <summary>App chấp hành chạy Auto: lệnh tính lúc soi, trục tới đúng lệnh Flow, không còn phép bù ở App.</summary>
        private static void TestVmAutoCycle()
        {
            Section("App chấp hành — chạy Auto");
            var fv = VmSetup();
            if (fv == null) { Check("Chạy với vision giả", false); VmRestore(); return; }
            var run = Core.Data.McDataManager.RunData;
            try
            {
                VmPrepareRun();

                // Bù đặt trên MỌI ô, cả hai tay — một phần là bù chung, phần còn lại là bù riêng của từng ô: chu trình
                // phải chạy trọn như thường và đích gửi đi cộng đủ cả hai. Hàng rào lưới teach ±PlaceTargetGuardMm phải
                // dời tâm theo lượng bù tổng, không thì bù nhiều là chính hàng rào bỏ ô.
                const double allDx = 0.20, allDy = -0.15;
                const double trimDx = 0.30, trimDy = -0.25;   // tổng = bù chung + bù riêng
                var assy = Core.Data.McDataManager.Model.AssyTray;
                assy.SetPlaceTrimAll(AxisId.Z1, allDx, allDy, 0);
                assy.SetPlaceTrimAll(AxisId.Z2, allDx, allDy, 0);
                for (int r = 0; r < assy.Rows; r++)
                    for (int c = 0; c < assy.Cols; c++)
                    {
                        assy.SetPlaceTrim(AxisId.Z1, r, c, trimDx - allDx, trimDy - allDy, 0);
                        assy.SetPlaceTrim(AxisId.Z2, r, c, trimDx - allDx, trimDy - allDy, 0);
                    }

                // Đèn CAM phải sáng suốt cả nhóm bước, không nháy từng lần kích Flow (Tuấn yêu cầu 2026-09-25).
                // Đếm SƯỜN LÊN chứ không đếm số lần ghi DO: ghi lại đúng giá trị cũ không phải một nháy mới.
                var lampIo = (FakeIo)McControl.Io;
                int lampOn1 = 0, lampOn2 = 0;
                bool cam1 = false, cam2 = false, bothLampsOn = false;
                lampIo.AfterSetDo = (bit, on) =>
                {
                    if (bit == IoMap.DoLampCam1) { if (on && !cam1) lampOn1++; cam1 = on; }
                    if (bit == IoMap.DoLampCam2) { if (on && !cam2) lampOn2++; cam2 = on; }
                    if (cam1 && cam2) bothLampsOn = true;
                };
                int vm0 = fv.VmCallCount;

                int comp0 = fv.PlaceComputeCount;
                string rowAxis = Core.Data.McDataManager.Model.AssyTray.AxisRow;
                var yAssy = (FakeAxis)McControl.Motion[rowAxis];
                int gentle0 = yAssy.GentleMoves;
                Check("START chạy được", VmStart(), VmState());
                bool made = WaitUntil(() => run.CountOk >= 4, 180000);
                Check("Đặt xong 4 sản phẩm", made, $"OK={run.CountOk} NG={run.CountNg} bước={run.AutoState} — {McControl.Status.Message}");
                Check("Không phát sinh alarm Error", !AlarmManager.HasError,
                    string.Join("; ", AlarmManager.ActiveAlarms.Select(a => a.Code + " " + a.Detail)));
                Check("Mỗi lần đặt, lệnh do AlignBottom_ToolN tính lúc soi (kèm ô đích)",
                    fv.PlaceComputeCount - comp0 >= 4, "tính lệnh +" + (fv.PlaceComputeCount - comp0));
                Check("Không tính NG, không bỏ ô", run.CountNg == 0 && run.AssyTray.Cells.ToArray().All(c => c != ECellState.Skipped),
                    "NG=" + run.CountNg + ", bỏ " + run.AssyTray.Cells.ToArray().Count(c => c == ECellState.Skipped) + " ô");

                lampIo.AfterSetDo = null;
                int vmCalls = fv.VmCallCount - vm0;
                int nhip = (run.CountOk + 1) / 2;
                // Không hỏi "có bật đèn không" — câu đó đạt cả với hành vi nháy cũ. Hỏi SỐ LẦN bật so với số lần
                // kích Flow: hành vi cũ cho hai con số BẰNG nhau, hành vi mới phải ít hơn hẳn.
                Check("Đèn CAM sáng suốt cả nhóm bước, không nháy từng lần kích Flow",
                    lampOn1 > 0 && lampOn2 > 0 && lampOn1 + lampOn2 < vmCalls,
                    $"bật CAM1 {lampOn1} lần, CAM2 {lampOn2} lần / {vmCalls} lần kích Flow");
                Check("Hai lần soi CAM dưới trong một nhịp chỉ bật đèn MỘT lần",
                    lampOn1 <= nhip, $"bật CAM1 {lampOn1} lần cho {nhip} nhịp ({run.CountOk} sản phẩm)");
                Check("Không bao giờ để hai đèn CAM cùng sáng", !bothLampsOn);

                var mv = McControl.AutoRoutine.LastPlaceMove;
                Check("Trục tới đúng lệnh Flow trả — X, Y2 và góc A tuyệt đối",
                    mv != null && Near(mv.ActualX, mv.CmdX) && Near(mv.ActualY, mv.CmdY) && AxisAngle.Distance(mv.ActualA, mv.ATarget) < 0.05,
                    mv == null ? "chưa có lần đặt" : $"X {mv.ActualX:0.###}/{mv.CmdX:0.###} Y {mv.ActualY:0.###}/{mv.CmdY:0.###} A {mv.ActualA:0.###}/{mv.ATarget:0.###}");
                double yLow = Core.Data.McDataManager.SysData.GetAxis(rowAxis).MoveSpeedLow;
                Check(rowAxis + " tới điểm bù ở mức Auto thấp, gia tốc giảm theo (Tuấn 30/09)",
                    yAssy.GentleMoves - gentle0 >= 4 && Near(yAssy.LastGentleSpeed, yLow),
                    $"{yAssy.GentleMoves - gentle0} lệnh dịu, tốc độ {yAssy.LastGentleSpeed:0.###}/{yLow:0.###}");
                string ro1 = VisionReadout.Tool1, roTop = VisionReadout.Top;
                Check("Khung CAM tay #1 in ô được gán, độ lệch, điểm đích; khung top-down in Δ so với master hand-eye (Tuấn 01/10)",
                    ro1.Contains("Ô (") && ro1.Contains("Lệch X") && ro1.Contains("Đích X") && roTop.Contains("Chi tiết X") &&
                    roTop.Contains("Tay #1: chưa có ảnh master") && roTop.Contains("Tay #2: chưa có ảnh master"),
                    ro1.Replace("\n", " | ") + " ‖ " + roTop.Replace("\n", " | "));
                // Cùng kết quả đó chia thành bảng cho trang Main (Tuấn 2026-10-07): mỗi hàng đủ số cột, tay chưa có master
                // thì hàng Δ để "—" và ghi chú nhắc chạy lại hand-eye.
                var card1 = VisionReadout.Tool1Card; var cardTop = VisionReadout.TopCard;
                Check("Bảng khung CAM: tay #1 có hàng Lệch và Đích đủ 4 cột; top-down có Chi tiết, Δ tay #1, Δ tay #2 đủ 3 cột",
                    card1 != null && cardTop != null &&
                    card1.Rows.Select(r => r.Label).SequenceEqual(new[] { "Lệch", "Đích" }) &&
                    card1.Rows.All(r => r.Values.Length == card1.Columns.Length) && card1.Cell.StartsWith("Ô (") &&
                    cardTop.Rows.Select(r => r.Label).SequenceEqual(new[] { "Chi tiết", "Δ tay #1", "Δ tay #2" }) &&
                    cardTop.Rows.All(r => r.Values.Length == cardTop.Columns.Length) &&
                    cardTop.Rows[1].Values[0] == "—" && (cardTop.Note ?? "").Contains("chưa có ảnh master"),
                    card1 == null || cardTop == null ? "chưa có bảng"
                        : string.Join(" | ", card1.Rows.Select(r => r.Label + " " + string.Join(" ", r.Values))) + " ‖ " +
                          string.Join(" | ", cardTop.Rows.Select(r => r.Label + " " + string.Join(" ", r.Values))) + " ‖ " + cardTop.Note);
                // Bảng tách Đích (bấm tiêu đề bảng, Tuấn 2026-10-07): Teaching + Vision + Bù chung + Bù riêng = Đích ở cả X, Y, θ.
                var bd = card1?.Breakdown;
                Func<string, double> num = v => double.Parse(v, System.Globalization.CultureInfo.InvariantCulture);
                bool sums = bd != null && bd.IsBreakdown && bd.Rows.Length == 3 && bd.Rows.All(r => r.Values.Length == 5) &&
                    bd.Rows.Take(2).All(r => Math.Abs(r.Values.Take(4).Sum(num) - num(r.Values[4])) <= 0.0025) &&
                    AxisAngle.Distance(bd.Rows[2].Values.Take(4).Sum(num), num(bd.Rows[2].Values[4])) <= 0.025;
                Check("Bảng tách Đích: Teaching + Vision + Bù chung + Bù riêng cộng lại đúng bằng Đích (X, Y, θ)", sums,
                    bd == null ? "không có bảng tách" : string.Join(" | ", bd.Rows.Select(r => r.Label + " " + string.Join(" ", r.Values))));
                var sent = fv.LastPlaceGlobals;
                var heldCalib = Core.Data.McDataManager.Model.Calib;
                Check("Pose ô đích và hằng số của tay đặt đi qua biến toàn cục (HasTarget = 1)",
                    sent != null && sent[VmVars.HasTarget] == 1 && sent.ContainsKey(VmVars.CellX) &&
                    (Near(sent[VmVars.HandEyeDx], heldCalib.HandEyeDx(1)) || Near(sent[VmVars.HandEyeDx], heldCalib.HandEyeDx(2))),
                    sent == null ? "(không có)" : string.Join(", ", sent.Select(kv => kv.Key + "=" + kv.Value.ToString("0.###"))));
                var placed = Enumerable.Range(0, run.AssyTray.CellCount).Where(i => run.AssyTray.Get(i) == ECellState.Ok)
                    .Select(i => McControl.AutoRoutine.GetPlacedTop(i)).ToList();
                Check("Mỗi ô đã đặt giữ số quét ô (pose ô đích lúc đặt)",
                    placed.Count >= 4 && placed.All(p => p != null), placed.Count + " ô");
                Check("Bù chung + bù riêng của ô đi vào pose gửi cho Flow (đúng một phép dịch)",
                    sent != null && placed.Any(p => p != null &&
                        Near(sent[VmVars.CellX] - trimDx, p.X) && Near(VmCycle.BottomYSign * sent[VmVars.CellY] - trimDy, p.Y)),
                    sent == null ? "(không có)" : $"CellX_mm {sent[VmVars.CellX]:0.###}, CellY_mm {sent[VmVars.CellY]:0.###}");
            }
            finally
            {
                VmStopRoutine();
                VmRestore();
            }
        }

        /// <summary>
        /// Khu "Chạy kiểm từng bước" ở trang Vision Edit: sáu nút chạy đúng đường của Auto, đúng thứ tự, và từ chối
        /// khi bấm sai thứ tự. Bước cuối dừng ở Z an toàn — không hạ, không nhả.
        /// </summary>
        private static void TestVmStepCheck()
        {
            Section("App chấp hành — chạy kiểm từng bước cả chu trình (trang Vision Edit)");

            var fv = VmSetup();
            if (fv == null) { Check("Chạy với vision giả", false); VmRestore(); return; }
            try
            {
                VmPrepareRun();
                var calib = McControl.Calib;
                var sys = Core.Data.McDataManager.SysData;
                var model = Core.Data.McDataManager.Model;
                var run = Core.Data.McDataManager.RunData;
                var assy = model.AssyTray;
                var ct = CancellationToken.None;
                var x = McControl.Motion[AxisId.X];
                string yName = assy.AxisRow;
                var yAxis = McControl.Motion[yName];
                calib.StepClear();

                // ── Bấm sai thứ tự: không có số của bước trước thì không chạy trục nào ──
                double x0 = x.CurrentPos, y0 = yAxis.CurrentPos;
                var early5 = calib.StepAlignBottomAsync(1, ct).GetAwaiter().GetResult();
                Check("Chưa soi ô đích mà bấm Trigger AlignBottom → từ chối, trục đứng yên",
                    !early5.Ok && early5.Reason != null && early5.Reason.Contains("Trigger LocateTop") &&
                    Near(x.CurrentPos, x0) && Near(yAxis.CurrentPos, y0), early5.Reason);
                var early6 = calib.StepMoveToCommandAsync(1, ct).GetAwaiter().GetResult();
                Check("Chưa có lệnh mà bấm Tới điểm đặt → từ chối",
                    !early6.Ok && early6.Reason != null && early6.Reason.Contains("Trigger AlignBottom"), early6.Reason);
                var outside = calib.StepMoveCamToCellAsync(assy.Rows, 0, ct).GetAwaiter().GetResult();
                Check("Ô ngoài lưới tray → từ chối", !outside.Ok && outside.Reason.Contains("ngoài lưới"), outside.Reason);

                // ── Bước 1: CAM top-down tới mốc soi ô (1,1) — đúng phép tính của S05 ──
                var s1 = calib.StepMoveCamToCellAsync(0, 0, ct).GetAwaiter().GetResult();
                Check("CAM tới ô lắp ráp: đúng mốc soi ô (1,1)",
                    s1.Ok && Near(x.CurrentPos, assy.CamTopDownAnchorX) && Near(yAxis.CurrentPos, assy.CamTopDownAnchorY),
                    s1.Reason ?? $"X {x.CurrentPos:0.###}/{assy.CamTopDownAnchorX:0.###}, {yName} {yAxis.CurrentPos:0.###}/{assy.CamTopDownAnchorY:0.###}");

                // ── Bước 2: LocateTop một lần, cất pose ô đích ──
                int locate0 = fv.LocateTopCount;
                var s2 = calib.StepLocateTopAsync(0, 0, ct).GetAwaiter().GetResult();
                Check("Trigger LocateTop: Flow chạy đúng một lần và cất pose ô đích",
                    s2.Ok && fv.LocateTopCount == locate0 + 1 && calib.StepHasTop, s2.Reason);

                // ── Bước 3: hút ô (1,1) tray đầu vào, ghi sổ như S1x ──
                // Đặt A lệch hẳn 40° rồi mới bấm hút: nút này phải tự đưa A về GÓC HÚT đã teach trước khi hạ Z.
                var a1ax = McControl.Motion[AxisId.A1];
                McControl.Func.MoveAsync(AxisId.A1, 40, Core.Data.McDataManager.SysData.GetAxis(AxisId.A1).MoveSpeed, ct)
                    .GetAwaiter().GetResult();
                var s3 = calib.StepPickAsync(1, 0, 0, ct).GetAwaiter().GetResult();
                Check("Hút hàng: A đang lệch 40° thì xoay về đúng góc hút đã teach rồi mới hạ Z",
                    AxisAngle.Distance(a1ax.CurrentPos, model.PickAngle(1)) < 0.05,
                    $"A1 {a1ax.CurrentPos:0.###}°, góc hút teach {model.PickAngle(1):0.##}°");
                Check("Góc hút teach khác 0 — nếu code quay lại hằng số 0 thì bài trên lộ ra ngay",
                    Math.Abs(model.PickAngle(1)) > 0.5, $"{model.PickAngle(1):0.##}°");
                Check("Hút hàng: hút được và ghi sổ tay #1 đang cầm hàng của đúng ô nguồn",
                    s3.Ok && McControl.Func.HasProduct(1) && run.Nozzle1.HasProduct && run.Nozzle1.SourceCellIndex >= 0,
                    s3.Reason ?? "ô nguồn " + run.Nozzle1.SourceCellIndex);
                Check("Hút hàng: nâng Z về cao độ an toàn trước khi đi tiếp", McControl.Motion.ZSafeOk);
                var s3b = calib.StepPickAsync(1, 0, 1, ct).GetAwaiter().GetResult();
                Check("Tay còn hàng mà bấm hút tiếp → từ chối",
                    !s3b.Ok && s3b.Reason != null && s3b.Reason.Contains("còn sản phẩm"), s3b.Reason);

                // ── Bước 4: X tới vị trí soi của đúng tay ──
                var s4 = calib.StepMoveToAlignAsync(1, ct).GetAwaiter().GetResult();
                Check("Tới vị trí soi CAM dưới: X tới đúng vị trí soi CAM bottom-up của tay #1",
                    s4.Ok && Near(x.CurrentPos, model.AlignPosX(1)), s4.Reason ?? $"X {x.CurrentPos:0.###}/{model.AlignPosX(1):0.###}");

                // ── Bước 5: đi đúng đường của S20 — cùng biến toàn cục, cùng Group Cmd, cùng hàm chặn ──
                int comp0 = fv.PlaceComputeCount;
                var s5 = calib.StepAlignBottomAsync(1, ct).GetAwaiter().GetResult();
                Check("Trigger AlignBottom: Group Cmd tính lệnh, App chặn xong mới cất",
                    s5.Ok && s5.HasCommand && calib.StepHasCommand && fv.PlaceComputeCount == comp0 + 1, s5.Reason);
                var sent = fv.LastPlaceGlobals;
                Check("Trigger AlignBottom gửi pose ô đích và hằng số của đúng tay qua biến toàn cục",
                    sent != null && sent[VmVars.HasTarget] == 1 && Near(sent[VmVars.HandEyeDx], model.Calib.HandEyeDx(1)),
                    sent == null ? "(không có)" : $"HasTarget {sent[VmVars.HasTarget]}, HandEyeDx_mm {sent[VmVars.HandEyeDx]:0.###}");
                Check("Lệnh nằm trong hàng rào lưới teach (không cảnh báo lệch lưới)",
                    string.IsNullOrEmpty(calib.StepGuardWarn), calib.StepGuardWarn);

                // ── Bước 6: tới điểm đặt quanh ô teach, Z vẫn an toàn, hút vẫn giữ ──
                var yFake6 = (FakeAxis)yAxis;
                int gentle6 = yFake6.GentleMoves;
                var s6 = calib.StepMoveToCommandAsync(1, ct).GetAwaiter().GetResult();
                double yLow6 = sys.GetAxis(yName).MoveSpeedLow;
                Check(yName + " tới điểm đặt ở mức Auto thấp, gia tốc giảm theo — như Auto (Tuấn 01/10)",
                    yFake6.GentleMoves - gentle6 == 1 && Near(yFake6.LastGentleSpeed, yLow6),
                    $"{yFake6.GentleMoves - gentle6} lệnh dịu, tốc độ {yFake6.LastGentleSpeed:0.###}/{yLow6:0.###}");
                var cell = assy.CellPos(AxisId.Z1, 0, 0);
                double lim = sys.PlaceTargetGuardMm + sys.MaxCompensationDistance;
                Check("Tới điểm đặt: tay tới điểm đặt quanh ô teach (1,1)",
                    s6.Ok && Math.Abs(x.CurrentPos - cell.X) <= lim && Math.Abs(yAxis.CurrentPos - cell.Y) <= lim,
                    s6.Reason ?? $"X {x.CurrentPos:0.###}/{cell.X:0.###}, {yName} {yAxis.CurrentPos:0.###}/{cell.Y:0.###}");
                Check("Tới điểm đặt KHÔNG hạ Z và KHÔNG nhả hút — sản phẩm vẫn trên tay",
                    McControl.Motion.ZSafeOk && McControl.Func.HasProduct(1));

                var s6again = calib.StepMoveToCommandAsync(1, ct).GetAwaiter().GetResult();
                Check("Bấm lại Tới điểm đặt vẫn chạy được (lệnh góc là góc tuyệt đối)", s6again.Ok, s6again.Reason);

                // Jog trục A sau lúc chụp: lệnh góc hết đúng — đúng nhánh R của bước đặt lúc chạy Auto.
                McControl.Func.MoveIncAsync(AxisId.A1, 5.0, sys.GetAxis(AxisId.A1).MoveSpeed, ct).GetAwaiter().GetResult();
                var s6moved = calib.StepMoveToCommandAsync(1, ct).GetAwaiter().GetResult();
                Check("Trục A bị xoay sau lúc chụp → Tới điểm đặt từ chối chạy lệnh cũ",
                    !s6moved.Ok && s6moved.Reason != null && s6moved.Reason.Contains("xoay"), s6moved.Reason);

                // Bấm lại bước 1: bỏ hết số cũ, không để lệnh của ô trước sống sót sang ô sau.
                calib.StepMoveCamToCellAsync(0, 1, ct).GetAwaiter().GetResult();
                Check("Bấm lại CAM tới ô lắp ráp là bỏ pose ô và lệnh đang cất",
                    !calib.StepHasTop && !calib.StepHasCommand);

                // Trả máy về trạng thái sạch: nhả sản phẩm đang cầm rồi đồng bộ lại sổ tay hút.
                McControl.Func.SetVacuum(1, false);
                WaitUntil(() => !McControl.Func.HasProduct(1), 2000);
                McControl.SyncNozzleStateFromSensor();
                run.InputTray.ResetAll(ECellState.Loaded);
            }
            finally
            {
                VmRestore();
            }
        }

        /// <summary>Nhánh N, G: xử lý xong rồi chạy tiếp, không dừng máy.</summary>
        private static void TestVmFaults()
        {
            Section("App chấp hành — nhánh N / G xử lý xong rồi chạy tiếp");
            var fv = VmSetup();
            if (fv == null) { Check("Chạy với vision giả", false); VmRestore(); return; }
            var run = Core.Data.McDataManager.RunData;
            var names = Core.Data.McDataManager.SysData.VisionOutputs;
            try
            {
                // N — góc bù vượt ngưỡng
                VmPrepareRun();
                fv.ForceDAngleDeg = 20;
                fv.ForceDAngleTimes = 1;
                Check("START (tình huống N)", VmStart(), VmState());
                bool gotNg = WaitUntil(() => run.CountNg >= 1, 120000);
                Check("Góc bù vượt ngưỡng Setting → trả sản phẩm về ô nguồn ngay lúc soi, tính NG",
                    gotNg && run.InputTray.Cells.ToArray().Count(c => c == ECellState.Ng) == 1, "NG=" + run.CountNg);
                Check("Nhánh N không dừng máy — đặt tiếp sản phẩm khác", WaitUntil(() => run.CountOk >= 1, 120000) && !AlarmManager.HasError, VmState());
                VmStopRoutine();

                // G — đích lệch lưới một lần. Warning máy tự chạy tiếp nên rời danh sách active ngay (AlarmManager.Dismiss),
                // nên bắt lúc nó được phát thay vì hỏi IsActive sau đó.
                VmPrepareRun();
                AlarmData guardAlarm = null;
                Action<AlarmData> onRaise = al => { if (al.Code == AlarmCode.PlaceTargetGuard) guardAlarm = al; };
                AlarmManager.AlarmRaised += onRaise;
                fv.TargetShiftMm = 3.0;
                fv.TargetShiftTimes = 1;
                Check("START (tình huống G)", VmStart(), VmState());
                bool placedSkip = WaitUntil(() => run.CountOk >= 1, 120000);
                // Ô bỏ vì lệch lưới thành NG — lưới tô đỏ (Tuấn 2026-10-17; trước đó Skipped).
                int skipped = run.AssyTray.Cells.ToArray().Count(c => c == ECellState.Ng);
                Check("Đích lệch lưới 3 mm → bỏ đúng một ô (thành NG, tô đỏ), soi lại với ô kế, giữ sản phẩm",
                    placedSkip && skipped == 1 && run.CountNg == 0, $"bỏ {skipped} ô, OK={run.CountOk}, NG={run.CountNg}");
                AlarmManager.AlarmRaised -= onRaise;
                Check("Lệch lưới một lần phát 5005 mức Warning (vào lịch sử alarm), không dừng máy",
                    guardAlarm != null && guardAlarm.Level == EAlarmLevel.Warning && !AlarmManager.HasError,
                    guardAlarm == null ? "không có alarm 5005" : guardAlarm.Level + " — " + guardAlarm.Detail);
            }
            finally
            {
                VmStopRoutine();
                VmRestore();
            }
        }

        /// <summary>Nhánh E/C: dừng, giữ hàng, trả ô về trống, RESET rồi chạy lại từ S00.</summary>
        private static void TestVmHalts()
        {
            Section("App chấp hành — dừng vì lỗi: giữ hàng trên tay, RESET rồi chạy lại từ S00");
            var fv = VmSetup();
            if (fv == null) { Check("Chạy với vision giả", false); VmRestore(); return; }
            var run = Core.Data.McDataManager.RunData;
            var names = Core.Data.McDataManager.SysData.VisionOutputs;
            try
            {
                // Lệch lưới hai ô liên tiếp
                VmPrepareRun();
                fv.TargetShiftMm = 3.0;
                fv.TargetShiftTimes = 2;
                Check("START (lệch lưới lặp lại)", VmStart(), VmState());
                bool halted = WaitUntil(() => AlarmManager.IsActive(AlarmCode.VisionContract), 120000);
                VmWaitHalted();
                Check("Hai ô liên tiếp lệch lưới → dừng với alarm 4010", halted, VmAlarm(AlarmCode.VisionContract));
                Check("Dừng mà vẫn giữ sản phẩm, không ô nào kẹt Processing, không bắt lấy hàng ra",
                    VmHolding() && run.AssyTray.Cells.ToArray().All(c => c != ECellState.Processing) && !run.NozzleClearRequired,
                    $"tay1={run.Nozzle1.HasProduct} tay2={run.Nozzle2.HasProduct}");
                int ok0 = run.CountOk, align0 = fv.AlignBottomCount;
                Check("RESET rồi START chạy lại được", VmResetAndStart(), VmState());
                Check("Chạy lại từ S00: soi lại sản phẩm đang cầm rồi đặt",
                    WaitUntil(() => run.CountOk > ok0, 120000) && fv.AlignBottomCount > align0, $"OK {ok0} → {run.CountOk}");
                VmStopRoutine();

                // Echo sai
                VmPrepareRun();
                fv.CorruptEchoOnce = true;
                Check("START (echo sai)", VmStart(), VmState());
                bool haltedEcho = WaitUntil(() => AlarmManager.IsActive(AlarmCode.VisionContract), 120000);
                VmWaitHalted();
                Check("EchoRunId sai ở AlignBottom kèm ô đích → dừng 4010, chi tiết nói rõ",
                    haltedEcho && VmAlarm(AlarmCode.VisionContract).Contains("EchoRunId"), VmAlarm(AlarmCode.VisionContract));
                Check("Giữ hàng, ô đích trả về trống, không tính NG",
                    VmHolding() && run.AssyTray.Cells.ToArray().All(c => c != ECellState.Processing) && run.CountNg == 0);
                int ok1 = run.CountOk;
                Check("RESET rồi START chạy lại được (echo)", VmResetAndStart(), VmState());
                Check("... và đặt tiếp", WaitUntil(() => run.CountOk > ok1, 120000), VmState());
                VmStopRoutine();

                // Solution thiếu biến của lần đặt
                VmPrepareRun();
                KeyValuePair<string, string> savedCell;
                lock (fv.GlobalVars) { savedCell = fv.GlobalVars[VmVars.CellX]; fv.GlobalVars.Remove(VmVars.CellX); }
                Check("START (solution thiếu biến CellX_mm)", VmStart(), VmState());
                bool haltedIn = WaitUntil(() => AlarmManager.IsActive(AlarmCode.VisionContract), 120000);
                VmWaitHalted();
                Check("Solution thiếu biến CellX_mm → dừng 4010, chi tiết nêu tên biến, giữ hàng",
                    haltedIn && VmAlarm(AlarmCode.VisionContract).Contains(VmVars.CellX) && VmHolding(), VmAlarm(AlarmCode.VisionContract));
                lock (fv.GlobalVars) fv.GlobalVars[VmVars.CellX] = savedCell;
                Check("Tạo lại biến, RESET rồi START chạy lại được", VmResetAndStart(), VmState());
                VmStopRoutine();

                // Solution thiếu biến toàn cục
                VmPrepareRun();
                string a2 = names.GvAxisA2;
                KeyValuePair<string, string> saved;
                lock (fv.GlobalVars)
                {
                    saved = fv.GlobalVars[a2];
                    fv.GlobalVars.Remove(a2);
                }
                Check("START (thiếu biến)", VmStart(), VmState());
                bool haltedVar = WaitUntil(() => AlarmManager.IsActive(AlarmCode.VisionContract), 120000);
                VmWaitHalted();
                Check("Solution thiếu biến trục A2 → ghi nghiêm từ chối, dừng 4010",
                    haltedVar && VmAlarm(AlarmCode.VisionContract).Contains("\"" + a2 + "\""), VmAlarm(AlarmCode.VisionContract));
                lock (fv.GlobalVars) fv.GlobalVars[a2] = saved;
                Check("Khai lại biến, RESET rồi START chạy lại được", VmResetAndStart(), VmState());
                VmStopRoutine();
            }
            finally
            {
                VmStopRoutine();
                VmRestore();
            }
        }

        /// <summary>Cổng khi chạy tiếp giữa chừng (R13), và trục A xoay sau lúc chụp (nhánh R).</summary>
        private static void TestVmGate()
        {
            Section("App chấp hành — cổng khi chạy tiếp giữa chừng, trục A xoay sau lúc chụp");
            var fv = VmSetup();
            if (fv == null) { Check("Chạy với vision giả", false); VmRestore(); return; }
            var run = Core.Data.McDataManager.RunData;
            var names = Core.Data.McDataManager.SysData.VisionOutputs;
            var model = Core.Data.McDataManager.Model;
            double posX0 = model.AlignPosX_Nozzle1;
            try
            {
                // Dừng ở S30 với tay #1 đang cầm hàng, rồi teach lại vị trí soi tay #1.
                VmPrepareRun();
                run.InputTray.Set(0, ECellState.Processing);
                McControl.Func.SetVacuum(1, true);
                run.Nozzle1.HasProduct = true;
                run.Nozzle1.SourceCellIndex = 0;
                run.AutoState = EAutoState.S30_Locate1;
                model.AlignPosX_Nozzle1 = posX0 + 0.2;
                double x0 = McControl.Motion[AxisId.X].CurrentPos;
                int calls0 = fv.VmCallCount;
                Thread.Sleep(100);

                McControl.RequestStart();
                bool blocked = WaitUntil(() => AlarmManager.IsActive(AlarmCode.VisionCalibNotReady), 15000);
                VmWaitHalted();
                Check("Teach lại vị trí soi lúc đang dừng → START bị cổng chặn",
                    blocked && McControl.Status.Message.Contains("vị trí soi tay #1"), VmState());
                Check("Bị chặn trước khi trục X chạy", Near(McControl.Motion[AxisId.X].CurrentPos, x0),
                    $"X {x0:0.###} → {McControl.Motion[AxisId.X].CurrentPos:0.###}");
                Check("Bị chặn trước khi gọi Flow nào", fv.VmCallCount == calls0, "Flow +" + (fv.VmCallCount - calls0));

                model.AlignPosX_Nozzle1 = posX0;
                Check("Trả vị trí soi về đúng thì chạy tiếp được", VmResetAndStart(), VmState());
                Check("... soi lại rồi đặt sản phẩm đang cầm",
                    WaitUntil(() => run.InputTray.Get(0) == ECellState.Ok && run.CountOk >= 1, 120000), VmState());
                VmStopRoutine();

                // Trục A xoay 2° ngay sau lúc chụp bottom-up tay #1.
                VmPrepareRun();
                fv.ClearFlowHistory();
                bool moved = false;
                fv.AfterFlow = flow =>
                {
                    if (moved || flow != names.FlowAlignBottomTool1) return;
                    moved = true;
                    var a1 = McControl.Motion[AxisId.A1];
                    McControl.Func.MoveAsync(AxisId.A1, AxisAngle.Wrap(a1.CurrentPos + 2.0),
                        Core.Data.McDataManager.SysData.GetAxis(AxisId.A1).MoveSpeed, CancellationToken.None).GetAwaiter().GetResult();
                };
                Check("START (trục A bị xoay)", VmStart(), VmState());
                bool placedA = WaitUntil(() => run.CountOk >= 2, 180000);
                fv.AfterFlow = null;
                Check("Trục A xoay sau lúc chụp → soi lại rồi đặt, không dừng", moved && placedA && !AlarmManager.HasError, VmState());
                var hist = fv.FlowHistorySnapshot();
                int iA = hist.IndexOf(names.FlowAlignBottomTool1);
                int iNext = iA < 0 ? -1 : hist.FindIndex(iA + 1, f => f == names.FlowAlignBottomTool2);
                int end = iNext < 0 ? hist.Count : iNext;
                int between = iA < 0 ? -1 : hist.Skip(iA).Take(end - iA).Count(f => f == names.FlowAlignBottomTool1);
                Check("Soi lại tay #1 đúng một lần trước khi đặt", between == 2, "AlignBottom_Tool1 trước lượt soi tay #2 kế tiếp: " + between);
            }
            finally
            {
                model.AlignPosX_Nozzle1 = posX0;
                VmStopRoutine();
                VmRestore();
                McControl.RequestSetMode(EMcMode.Manual);
            }
        }

        /// <summary>Cổng an toàn 2 tay của Cặp B (PLAN.md §3.2, 2026-08-20) — thuần logic, không
        /// cần IO/động cơ nên kiểm chứng trực tiếp bằng TwoHandGate, giống hàm chặn VmCycle.CheckPlace.</summary>
        private static void TestTwoHandGate()
        {
            Section("Cổng an toàn 2 tay (TwoHandGate — Cặp B)");

            var g1 = new TwoHandGate(500);
            Check("2 nút lên cùng lúc thì trigger ngay", g1.Update(true, true));

            var g2 = new TwoHandGate(500);
            g2.Update(true, false);            // nút 1 lên trước
            bool r2 = g2.Update(true, true);   // nút 2 lên ngay sau, còn trong cửa sổ
            Check("Bấm lệch nhau nhưng trong cửa sổ thì vẫn trigger", r2);

            var g3 = new TwoHandGate(500);
            g3.Update(true, false);
            bool r3a = g3.Update(false, false);   // nhả nút 1 trước khi nút 2 kịp lên
            bool r3b = g3.Update(true, true);     // thử lại — không được bị khoá oan
            Check("Bấm hụt 1 nút rồi nhả thì không trigger, thử lại vẫn được", !r3a && r3b);

            var g4 = new TwoHandGate(20);   // cửa sổ ngắn để test không phải chờ lâu
            g4.Update(true, false);
            Thread.Sleep(60);               // vượt hẳn cửa sổ 20 ms
            bool r4a = g4.Update(true, true);     // nút 2 lên nhưng đã trễ quá cửa sổ
            bool r4b = g4.Update(true, true);     // vẫn giữ cả 2 — không tự trigger lại
            Check("Bấm lệch quá cửa sổ thì không trigger, giữ nguyên cũng không tự trigger sau đó",
                !r4a && !r4b);
            g4.Update(false, false);              // nhả hết
            bool r4c = g4.Update(true, true);     // bấm lại đồng thời — hợp lệ trở lại
            Check("Nhả hết cả 2 rồi bấm lại đồng thời thì trigger lại được", r4c);

            // Chống gian lận kiểu dán băng dính: giữ chặt 1 nút, chỉ tap/nhả nút còn lại liên
            // tục — chỉ trigger đúng 1 lần đầu, không lặp lại được vì gate không bao giờ thấy cả
            // hai nút cùng về 0 để mở khoá lại.
            var g5 = new TwoHandGate(500);
            bool r5a = g5.Update(true, true);
            bool r5b = g5.Update(true, false);
            bool r5c = g5.Update(true, true);
            bool r5d = g5.Update(true, false);
            bool r5e = g5.Update(true, true);
            Check("Giữ chặt 1 nút rồi tap nút kia liên tục — chỉ trigger đúng 1 lần đầu",
                r5a && !r5b && !r5c && !r5d && !r5e);
        }

        private static void TestBrakeInterlock()
        {
            Section("Phanh Z1 — nghi thức SVO on + \"Mở phanh\" (McFunction.RequestBrakeRelease/UpdateBrakeOutputs)");

            var z1 = (FakeAxis)McControl.Motion[AxisId.Z1];
            z1.SimDriverAlarm = false;

            z1.SetServoOnAsync(false).Wait();
            WaitUntil(() => !McControl.Io.GetDo(IoMap.DoBrakeReleaseZ1), 500);
            Check("Servo OFF thì DO nhả phanh Z1 = 0 (khoá)", !McControl.Io.GetDo(IoMap.DoBrakeReleaseZ1));

            string reason;
            McControl.Func.CanMove(AxisId.Z1, out reason);
            Check("Servo OFF thì CanMove(Z1) bị chặn vì chưa bật servo", reason != null && reason.Contains("servo"), reason);

            // Chỉ bật servo thôi — CHƯA bấm "Mở phanh" — thì phanh vẫn phải đứng khoá, không tự
            // nhả (Tuấn yêu cầu, 2026-09-04): đây là điểm khác với thiết kế trước đó.
            z1.SetServoOnAsync(true).Wait();
            System.Threading.Thread.Sleep(100);
            Check("Bật servo xong nhưng CHƯA bấm \"Mở phanh\" thì DO vẫn khoá (0)",
                !McControl.Io.GetDo(IoMap.DoBrakeReleaseZ1));
            McControl.Func.CanMove(AxisId.Z1, out reason);
            Check("Chưa bấm \"Mở phanh\" thì CanMove(Z1) vẫn bị chặn vì phanh",
                reason != null && reason.Contains("phanh"), reason);

            // Bấm "Mở phanh" — giờ mới thật sự nhả.
            McControl.Func.RequestBrakeRelease(AxisId.Z1);
            WaitUntil(() => McControl.Io.GetDo(IoMap.DoBrakeReleaseZ1), 500);
            Check("Bấm \"Mở phanh\" (đã ServoOn) thì DO nhả phanh Z1 lên 1", McControl.Io.GetDo(IoMap.DoBrakeReleaseZ1));

            bool blockedRightAway = !McControl.Func.CanMove(AxisId.Z1, out reason);
            Check("Vừa nhả phanh xong thì CanMove(Z1) vẫn bị chặn — chờ phanh nhả đủ lâu, không cho di chuyển ngay",
                blockedRightAway && reason != null && reason.Contains("phanh"), reason);

            bool readyLater = WaitUntil(() => McControl.Func.CanMove(AxisId.Z1, out reason), 1000);
            Check("Chờ đủ độ trễ nhả phanh thì CanMove(Z1) cho phép", readyLater, reason);

            z1.SimDriverAlarm = true;
            WaitUntil(() => !McControl.Io.GetDo(IoMap.DoBrakeReleaseZ1), 500);
            Check("Bơm Alarm thì DO nhả phanh Z1 lập tức về 0 (khoá lại ngay, không chờ)",
                !McControl.Io.GetDo(IoMap.DoBrakeReleaseZ1));

            // "Đảm bảo xử lý xong lỗi mới cho phép SON và nhả phanh" (Tuấn yêu cầu, 2026-09-04):
            // xoá Alarm ở driver rồi, KHÔNG bấm lại "Mở phanh" — phanh phải vẫn đứng khoá, vì
            // Alarm đã xoá luôn cờ yêu cầu, không tự khôi phục.
            z1.SimDriverAlarm = false;
            AlarmManager.ClearConditionAxis(AxisId.Z1, AxisFault.DriverAlarm);
            System.Threading.Thread.Sleep(100);
            Check("Hết Alarm nhưng chưa bấm lại \"Mở phanh\" thì DO vẫn khoá (0) — không tự khôi phục",
                !McControl.Io.GetDo(IoMap.DoBrakeReleaseZ1));

            // Bấm lại "Mở phanh" lần 2 — phải nhả lại được bình thường, không bị khoá vĩnh viễn.
            McControl.Func.RequestBrakeRelease(AxisId.Z1);
            WaitUntil(() => McControl.Io.GetDo(IoMap.DoBrakeReleaseZ1), 500);
            Check("Bấm lại \"Mở phanh\" sau khi hết Alarm thì nhả lại được bình thường",
                McControl.Io.GetDo(IoMap.DoBrakeReleaseZ1));
        }

        private static void TestEmergencyBrakeRelease()
        {
            Section("Nhả phanh khẩn cấp Z1 — toggle (McFunction.SetEmergencyBrakeRelease)");

            var z1 = (FakeAxis)McControl.Motion[AxisId.Z1];

            // Đúng lúc gặp sự cố nhất: đang Alarm VÀ Servo OFF — đường bình thường không dùng
            // được (đòi ServoOn), đây chính là lý do có đường khẩn cấp (Tuấn yêu cầu, 2026-09-04).
            z1.SimDriverAlarm = true;
            z1.SetServoOnAsync(false).Wait();
            WaitUntil(() => !McControl.Io.GetDo(IoMap.DoBrakeReleaseZ1), 500);
            Check("Trước khi bật: đang Alarm + Servo OFF thì DO khoá (0)", !McControl.Io.GetDo(IoMap.DoBrakeReleaseZ1));

            McControl.Func.SetEmergencyBrakeRelease(AxisId.Z1, true);
            WaitUntil(() => McControl.Io.GetDo(IoMap.DoBrakeReleaseZ1), 500);
            Check("Bật cờ khẩn cấp thì DO nhả phanh Z1 lên 1 DÙ đang Alarm + Servo OFF",
                McControl.Io.GetDo(IoMap.DoBrakeReleaseZ1));
            Check("IsEmergencyBrakeReleaseArmed(Z1) phản ánh đúng cờ vừa bật",
                McControl.Func.IsEmergencyBrakeReleaseArmed(AxisId.Z1));

            string reason;
            bool blocked = !McControl.Func.CanMove(AxisId.Z1, out reason);
            Check("Nhả phanh khẩn cấp KHÔNG cho phép CanMove(Z1) — chỉ mở phanh để đẩy tay, không phát lệnh chạy",
                blocked, reason);

            // Đổi từ dead-man sang TOGGLE thật (Tuấn yêu cầu, 2026-09-09, đã xác nhận rủi ro qua
            // AskUserQuestion): không còn tự khoá lại khi "ngừng ping" — phải GIỮ NGUYÊN qua thời
            // gian dù không có tương tác gì thêm, chỉ tắt khi gọi lại với armed=false.
            bool staysReleasedWithoutPing = WaitUntil(() => !McControl.Io.GetDo(IoMap.DoBrakeReleaseZ1), 800) == false;
            Check("Không còn dead-man: để yên không gọi gì thêm thì DO VẪN nhả, không tự khoá lại nữa",
                staysReleasedWithoutPing);

            McControl.Func.SetEmergencyBrakeRelease(AxisId.Z1, false);
            WaitUntil(() => !McControl.Io.GetDo(IoMap.DoBrakeReleaseZ1), 500);
            Check("Tắt cờ khẩn cấp (toggle lại) thì DO khoá lại ngay",
                !McControl.Io.GetDo(IoMap.DoBrakeReleaseZ1));

            // Hết alarm + bật lại servo cho các test sau, dọn sạch trạng thái.
            z1.SimDriverAlarm = false;
            AlarmManager.ClearConditionAxis(AxisId.Z1, AxisFault.DriverAlarm);
            z1.SetServoOnAsync(true).Wait();
        }

        /// <summary>
        /// Hai bảng chữ Việt/Anh (Core.Lang, Tuấn 2026-10-09): cùng bộ khoá, không ô tiếng Anh nào trống, và mẫu {n} của
        /// hai bản khớp nhau — lệch thì bấm sang tiếng Anh là câu mất số hoặc string.Format ném lỗi giữa chừng.
        /// </summary>
        private static void TestLanguageTables()
        {
            Section("Ngôn ngữ — hai bảng chữ Việt/Anh");

            var asm = typeof(Core.Lang.Loc).Assembly;
            var vi = ReadStrings(new System.Resources.ResourceManager("ShieldCanAssy.Core.Lang.StringsVi", asm));
            var en = ReadStrings(new System.Resources.ResourceManager("ShieldCanAssy.Core.Lang.StringsEn", asm));
            Check("Nạp được bảng tiếng Việt từ Core.dll", vi.Count > 0, vi.Count + " khoá");

            var onlyVi = vi.Keys.Where(k => !en.ContainsKey(k)).ToList();
            var onlyEn = en.Keys.Where(k => !vi.ContainsKey(k)).ToList();
            Check("Hai bảng cùng một bộ khoá", onlyVi.Count == 0 && onlyEn.Count == 0,
                "chỉ có ở vi: " + string.Join(", ", onlyVi.Take(5)) + " | chỉ có ở en: " + string.Join(", ", onlyEn.Take(5)));

            var emptyEn = en.Where(kv => string.IsNullOrWhiteSpace(kv.Value)).Select(kv => kv.Key).ToList();
            Check("Không ô tiếng Anh nào trống", emptyEn.Count == 0, string.Join(", ", emptyEn.Take(5)));

            var ph = new System.Text.RegularExpressions.Regex(@"\{(\d+)(?:[,:][^}]*)?\}");
            Func<string, string> holes = s => string.Join(",", ph.Matches(s ?? "").Cast<System.Text.RegularExpressions.Match>()
                .Select(m => m.Groups[1].Value).Distinct().OrderBy(x => x));
            var badHoles = vi.Keys.Where(k => en.ContainsKey(k) && holes(vi[k]) != holes(en[k])).ToList();
            Check("Mẫu {n} của bản Việt và bản Anh khớp nhau", badHoles.Count == 0, string.Join(", ", badHoles.Take(5)));

            string anyKey = vi.Keys.FirstOrDefault(k => en.ContainsKey(k) && en[k] != vi[k]);
            int itemChanged = 0;
            System.ComponentModel.PropertyChangedEventHandler h = (s, e) => { if (e.PropertyName == "Item[]") itemChanged++; };
            Core.Lang.Loc.Instance.PropertyChanged += h;
            try
            {
                Core.Lang.Loc.SetLanguage(Core.Lang.ELanguage.En);
                bool enOk = anyKey == null || Core.Lang.Loc.T(anyKey) == en[anyKey];
                Core.Lang.Loc.SetLanguage(Core.Lang.ELanguage.Vi);
                bool viOk = anyKey == null || Core.Lang.Loc.T(anyKey) == vi[anyKey];
                Check("Đổi ngôn ngữ thì Loc.T trả đúng bản, và báo nhãn XAML đọc lại (Item[])", enOk && viOk && itemChanged == 2,
                    anyKey + ", Item[] " + itemChanged + " lần");
            }
            finally
            {
                Core.Lang.Loc.Instance.PropertyChanged -= h;
                Core.Lang.Loc.SetLanguage(Core.Lang.ELanguage.Vi);
            }
            Check("Khoá không có trong bảng thì hiện chính tên khoá — nhìn màn hình là thấy chỗ quên",
                Core.Lang.Loc.T("Khoa.KhongCo") == "Khoa.KhongCo");
        }

        private static Dictionary<string, string> ReadStrings(System.Resources.ResourceManager rm)
        {
            var d = new Dictionary<string, string>(StringComparer.Ordinal);
            try
            {
                var set = rm.GetResourceSet(System.Globalization.CultureInfo.InvariantCulture, true, false);
                if (set != null)
                    foreach (System.Collections.DictionaryEntry e in set)
                        d[(string)e.Key] = e.Value as string;
            }
            catch (System.Resources.MissingManifestResourceException) { }
            return d;
        }

        // ───────────── tiện ích ─────────────

        private static bool Near(double a, double b) => Math.Abs(a - b) < 0.02;

        /// <summary>Chữ có "<paramref name="prefix"/>ΔX …, ΔY …" với hai số gần (dx, dy) — trục giả dừng lệch vài µm nên
        /// không so nguyên chuỗi được.</summary>
        private static bool DeltaNear(string text, string prefix, double dx, double dy)
        {
            var m = System.Text.RegularExpressions.Regex.Match(text ?? "",
                System.Text.RegularExpressions.Regex.Escape(prefix) + @"ΔX ([+-][0-9.]+), ΔY ([+-][0-9.]+)");
            var inv = System.Globalization.CultureInfo.InvariantCulture;
            return m.Success && Near(double.Parse(m.Groups[1].Value, inv), dx) && Near(double.Parse(m.Groups[2].Value, inv), dy);
        }

        private static bool WaitUntil(Func<bool> cond, int timeoutMs)
        {
            var sw = Stopwatch.StartNew();
            while (sw.ElapsedMilliseconds < timeoutMs)
            {
                if (cond()) return true;
                Thread.Sleep(20);
            }
            return cond();
        }

        private static void Section(string name)
        {
            Log("");
            Log("── " + name + " ──");
        }

        private static void Check(string what, bool ok, string detail = null)
        {
            if (ok) _pass++; else _fail++;
            Log($"  [{(ok ? "ĐẠT " : "LỖI ")}] {what}" + (string.IsNullOrEmpty(detail) ? "" : "   → " + detail));
        }

        private static void Log(string line)
        {
            _report.Add(line);
            Debug.WriteLine(line);
        }
    }
}
