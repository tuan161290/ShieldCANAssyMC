using System;
using System.Collections.Generic;
using System.Globalization;
using System.Threading;
using System.Threading.Tasks;
using ShieldCanAssy.Core.Communication;
using ShieldCanAssy.Core.Data;
using ShieldCanAssy.Core.Data.Model;
using ShieldCanAssy.Core.Infor;
using ShieldCanAssy.Core.Lang;
using ShieldCanAssy.Utility.Log;

namespace ShieldCanAssy.Core.Controller
{
    /// <summary>Kết quả phép kiểm tra hiệu chuẩn CAM top-down (§6.2b).</summary>
    public class TopDownVerifyResult
    {
        public bool Ok { get; set; }
        public string Reason { get; set; }

        /// <summary>Quãng đường đã chạy thử, mm.</summary>
        public double StepMm { get; set; }

        /// <summary>Sai lệch thang đo: (lượng Flow báo) − (lượng trục đã chạy), mm. 0 là hoàn hảo.</summary>
        public double ErrorX { get; set; }
        public double ErrorY { get; set; }

        /// <summary>Lẫn trục: chạy X mà Flow báo dịch theo Y (và ngược lại), mm.</summary>
        public double CrossX { get; set; }
        public double CrossY { get; set; }

        /// <summary>Dấu của từng trục có đúng không — sai dấu là đặt lệch gấp đôi, không phải lệch ít.</summary>
        public bool SignXOk { get; set; }
        public bool SignYOk { get; set; }

        /// <summary>Phán quyết của App (<c>VmStatus.TopDownVerify</c>); int.MinValue nếu chưa đo đủ ba chỗ.</summary>
        public int Status { get; set; } = int.MinValue;

        /// <summary>Ghi chú sau khi đạt: lưu bản ghi vào model.</summary>
        public string Note { get; set; }
    }

    /// <summary>Kết quả phép thử vòng xoay của một tay hút (§6.4b).</summary>
    public class AlignVerifyResult
    {
        public bool Ok { get; set; }
        public string Reason { get; set; }

        /// <summary>Dấu quy đổi góc VisionMaster → trục A: +1, −1, hoặc 0 nếu không kết luận được.</summary>
        public int Sign { get; set; }

        /// <summary>Góc đã xoay thử, độ.</summary>
        public double TestAngleDeg { get; set; }

        /// <summary>Góc Flow AlignVerify_ToolN đo được giữa hai lần đo (<c>RawDTheta</c>), độ.</summary>
        public double MeasuredDeltaThetaDeg { get; set; }

        /// <summary>Sai số dư: lượng dịch còn lại sau khi quay về, mm. Khác 0 nghĩa là tâm xoay
        /// hoặc chiều hệ toạ độ (handedness) trong file hiệu chuẩn bottom-up chưa đúng.</summary>
        public double ResidualMm { get; set; }

        /// <summary>Độ lệch tâm của sản phẩm lúc thử, mm — quá nhỏ thì phép thử không kết luận
        /// được về tâm xoay.</summary>
        public double EccentricityMm { get; set; }

        /// <summary>Vị trí trục X lúc thử — phải trùng vị trí soi lúc chạy Auto.</summary>
        public double PosX { get; set; }

        /// <summary>Status của Flow AlignVerify_ToolN; int.MinValue nếu chưa gọi tới Flow.</summary>
        public int Status { get; set; } = int.MinValue;

        /// <summary>Ghi chú sau khi đạt: lưu solution, lưu model.</summary>
        public string Note { get; set; }
    }

    /// <summary>Kết quả chuỗi nạp điểm hiệu chuẩn Align (§6.4).</summary>
    public class AlignCalibSequenceResult
    {
        public bool Ok { get; set; }
        public string Reason { get; set; }
        public int PointsCaptured { get; set; }
    }

    public class HandEyeCommitResult
    {
        public bool Ok { get; set; }
        public string Reason { get; set; }
        public int Status { get; set; } = int.MinValue;
        public double Dx { get; set; }
        public double Dy { get; set; }
        /// <summary>Mốc góc giữa hai camera, độ — <see cref="VisionCalib.HandEyeAngleRef"/>.</summary>
        public double AngleRef { get; set; }
        public string Note { get; set; }
    }

    /// <summary>Kết quả chạy khô đặt sản phẩm mẫu lên dấu chuẩn (Bước 5b).</summary>
    public class DryRunPlaceResult
    {
        public bool Ok { get; set; }
        public string Reason { get; set; }
        public double CamX { get; set; }
        public double CamY { get; set; }
        public double CmdX { get; set; }
        public double CmdY2 { get; set; }
        public double CmdA { get; set; }

        /// <summary>Đối chiếu Group Cmd với phép tính lại của App (<see cref="VmCycle.TraceAlignBottom"/>), hoặc null.</summary>
        public string CmdCheck { get; set; }
    }

    /// <summary>
    /// Kết quả MỘT nút của khu "Chạy kiểm từng bước" trên trang Vision Edit — chạy tay đúng trình tự mà Auto chạy
    /// tự động (soi ô → hút → soi sản phẩm → tới điểm đặt), mỗi bước một nút để nhìn được số của từng bước.
    /// </summary>
    public class StepRunResult
    {
        public bool Ok { get; set; }
        public string Reason { get; set; }

        /// <summary>Dòng số liệu của đúng bước vừa chạy — hiện thẳng trên trang, không phải hộp thoại bấm rồi mất.</summary>
        public string Text { get; set; }

        /// <summary>Bước 5: đã cất được lệnh, bước 6 chạy được.</summary>
        public bool HasCommand { get; set; }

        /// <summary>Bước 5: lệnh nằm ngoài hàng rào lưới teach (chạy Auto thì bỏ ô) — vẫn cất để chạy kiểm,
        /// nhưng bước 6 phải hỏi lại người bấm.</summary>
        public string GuardWarn { get; set; }

        /// <summary>
        /// Điểm đích Group Cmd tính ra — <b>chỉ nút 8 và nút 9 của bước xác định hand-eye</b> (phép thử tổng quát,
        /// <see cref="Routine_Calib.HandEyeCheckTargetAsync"/>, <see cref="Routine_Calib.HandEyeGoToTargetAsync"/>)
        /// điền; mọi bước khác để 0.
        ///
        /// <para>Mang ra ngoài vì đây là <b>kết luận</b> của phép thử: nó phải bằng encoder lúc gắp. Không có nó thì
        /// bài kiểm chỉ khẳng định được <c>Ok</c>, mà <c>Ok</c> đạt cả khi con số sai.</para>
        /// </summary>
        public double TargetX { get; set; }
        public double TargetY2 { get; set; }
        public double TargetA { get; set; }
    }

    /// <summary>
    /// Hiệu chuẩn vision — PLAN.md §6.
    ///
    /// <para><b>App không còn tự fit ma trận nào</b> (Tuấn chốt 2026-09-12). Phép hiệu chuẩn làm
    /// trong VisionMaster (N-point cho CAM top-down, hiệu chuẩn có xoay cho CAM bottom-up), App
    /// chỉ còn hai việc mà VisionMaster không tự làm được vì nó không điều khiển trục:</para>
    /// <list type="number">
    /// <item><b>Nạp điểm</b>: chạy trục tới từng vị trí/góc rồi kích Flow hiệu chuẩn — mỗi lần
    /// chạy Flow là module hiệu chuẩn nạp thêm một điểm (chế độ Trigger Acquisition).</item>
    /// <item><b>Chạy trục cho hai phép kiểm tra</b> (Bước 2, 4): App lấy trung bình N ảnh rồi <b>tự phán</b> —
    /// <see cref="VmCycle.JudgeTopDown"/> và <see cref="VmCycle.JudgeAlignSign"/>. Không Flow nào tính hộ.</item>
    /// <item><b>Đo hand-eye</b> (Bước 5): hai lần đọc encoder rồi trừ — không gọi Flow nào, tâm canh bằng mắt
    /// (Tuấn chốt 2026-09-13).</item>
    /// </list>
    ///
    /// <para>Bước nào đạt thì lớp này ghi hằng số và ngày đo vào model rồi lưu model — cổng Auto dựa vào bản ghi đó,
    /// và trước mỗi lần soi sản phẩm App ghi hằng số của đúng tay vào biến toàn cục cho Group <c>Cmd</c> dùng.</para>
    /// </summary>
    public class Routine_Calib
    {
        private readonly MotionConnect _motion;
        private readonly McFunction _func;
        private readonly Func<SystemData> _sys;
        private readonly Func<IVisionDevice> _vision;

        public Routine_Calib(MotionConnect motion, McFunction func, Func<SystemData> sys, Func<IVisionDevice> vision)
        {
            _motion = motion;
            _func = func;
            _sys = sys;
            _vision = vision;
        }

        // ───────────────────────── §6.2 CAM top-down: lưới 9 điểm ─────────────────────────

        /// <summary>
        /// Thứ tự chạy 9 điểm của "Auto Calib", theo số trên bàn phím numpad.
        ///
        /// <para>Hình chữ S, KHÔNG phải quét dòng: module N-point của VisionMaster tự sinh toạ độ
        /// vật lý cho các điểm theo quỹ đạo chữ S (Help "Physical Coordinate Parameters", tham số
        /// Movement First + Commutation Number). Chạy theo thứ tự dòng 1→9 thì điểm thứ 4 và thứ 6
        /// của App rơi vào đúng chỗ ngược với điểm thứ 4 và thứ 6 mà VisionMaster giả định — ma
        /// trận vẫn fit ra được, chỉ méo, và không có thông báo lỗi nào cả.</para>
        /// </summary>
        public static readonly int[] TopDownGridSOrder = { 1, 2, 3, 6, 5, 4, 7, 8, 9 };

        /// <summary>Xoá điểm cũ trong module hiệu chuẩn của Flow ứng với <paramref name="procedure"/> — bước đầu của
        /// mọi chuỗi nạp điểm (Tuấn yêu cầu 2026-10-09). Trả null nếu xoá được, lý do nếu không.</summary>
        public async Task<string> ClearCalibPointsAsync(EVisionProcedure procedure)
        {
            string err = await _vision().ClearCalibPointsAsync(procedure).ConfigureAwait(false);
            if (err != null)
                LogWorker.Instance.RecordLog("Hiệu chuẩn: không xoá được điểm cũ (" + procedure + ") — " + err,
                    Utility.Log.LogLevel.Warning, "Calib");
            return err;
        }

        /// <summary>Di chuyển tới (x, y2) rồi kích nhánh hiệu chuẩn của Flow LocateTop (IsTriggerLocate = 0) — mỗi lần gọi
        /// là nạp một điểm vào module N-point. Trả về false nếu di chuyển thất bại hoặc Flow báo không tìm thấy.</summary>
        public async Task<bool> MoveAndTriggerTopDownCalibAsync(double x, double y2, CancellationToken ct)
        {
            if (!await _func.MoveToCellAsync(AxisId.Y2, y2, x, ct).ConfigureAwait(false))
                return false;

            return await TriggerTopDownCalibFlowAsync(ct).ConfigureAwait(false);
        }

        /// <summary>Kích nhánh hiệu chuẩn của Flow LocateTop tại vị trí hiện tại, không di chuyển — chụp lại một
        /// điểm lẻ. <b>Cũng nạp thêm một điểm</b> vào module N-point, không phải nút "test vô hại".</summary>
        public async Task<bool> TriggerTopDownCalibFlowAsync(CancellationToken ct)
        {
            await _func.WaitSettleAsync(EVmCamera.TopDown, ct).ConfigureAwait(false);
            var snap = Snapshot();
            var pose = await WithCamLampAsync(EVmCamera.TopDown, ct,
                () => _vision().TriggerCalibAsync(EVisionProcedure.TopDownCalib, snap, ct))
                .ConfigureAwait(false);
            LogCalibPoint(_sys().VisionOutputs.FlowLocateTop + " (hiệu chuẩn)", snap, 0, pose.Found);
            return pose.Found;
        }

        /// <summary>
        /// Bật đèn CAM tương ứng, <b>chờ đèn sáng đủ</b> (<see cref="SystemData.LampMsFor"/>), chạy việc cần
        /// camera, rồi TẮT đèn — kể cả khi Flow lỗi, quá giờ hay bị huỷ giữa lúc chờ đèn (try/finally).
        /// Bottom-up là đèn CAM1, top-down là đèn CAM2; Flow chỉ tính thì không bật đèn nào và cũng không chờ.
        /// Trang hiệu chuẩn trước 2026-09-23 gọi thẳng vision nên các nút Trigger chụp trong lúc đèn tắt
        /// (Tuấn yêu cầu sửa); chuỗi Auto đã làm thế này từ 2026-09-04, xem <c>Routine_Auto.CallVmAsync</c>.
        /// </summary>
        private async Task<T> WithCamLampAsync<T>(EVmCamera cam, CancellationToken ct, Func<Task<T>> run)
        {
            int lampBit = cam == EVmCamera.BottomUp ? IoMap.DoLampCam1
                        : cam == EVmCamera.TopDown ? IoMap.DoLampCam2 : -1;
            if (lampBit >= 0) _func.Io?.SetDo(lampBit, true);
            try
            {
                int ms = _sys().LampMsFor(cam);
                if (lampBit >= 0 && ms > 0) await Task.Delay(ms, ct).ConfigureAwait(false);
                return await run().ConfigureAwait(false);
            }
            finally
            {
                if (lampBit >= 0) _func.Io?.SetDo(lampBit, false);
            }
        }

        /// <summary>Gọi một Flow kèm đèn CAM của đúng camera Flow đó dùng (<see cref="VmCallRequest.Camera"/>).</summary>
        private Task<VmCallResult> CallVmAsync(VmCallRequest req, CancellationToken ct)
        {
            return WithCamLampAsync(req.Camera, ct, () => _vision().CallFlowAsync(req, ct));
        }

        /// <summary>
        /// Vị trí THẬT của các trục, đọc từ encoder ngay trước khi nạp một điểm hiệu chuẩn — App
        /// ghi bộ số này vào biến toàn cục của solution để module hiệu chuẩn dùng làm toạ độ vật lý
        /// của điểm đó (§6.2, §6.4).
        ///
        /// <para>Đọc <c>CurrentPos</c> chứ không dùng vị trí LỆNH: sai lệch bám của servo, khe hở
        /// cơ khí và bù nhiệt đều nằm ở hiệu số giữa hai thứ đó, và chính sai lệch ấy là cái phép
        /// hiệu chuẩn phải học (PLAN.md §6.1 quy tắc 3).</para>
        /// </summary>
        private AxisSnapshot Snapshot()
        {
            return new AxisSnapshot
            {
                X = _motion[AxisId.X]?.CurrentPos ?? 0,
                Y2 = _motion[AxisId.Y2]?.CurrentPos ?? 0,
                A1 = _motion[AxisId.A1]?.CurrentPos ?? 0,
                A2 = _motion[AxisId.A2]?.CurrentPos ?? 0
            };
        }

        // ───────────────────────── §6.4 CAM bottom-up: nạp điểm hiệu chuẩn ─────────────────────────

        /// <summary>
        /// Chạy hết chuỗi nạp điểm cho module hiệu chuẩn bottom-up của tay
        /// <paramref name="nozzle"/>: xoá điểm cũ trong module, rồi các vị trí X lệch so với vị trí soi (phần tịnh
        /// tiến), sau đó các góc của trục A tại đúng vị trí soi (phần xoay), mỗi bước kích nhánh hiệu chuẩn của
        /// Flow AlignBottom_ToolN (IsTriggerLocate = 0) một lần.
        ///
        /// <para><b>Kết thúc bằng việc đưa trục A về 0°</b> để máy ở trạng thái xác định trước bước
        /// tiếp theo.</para>
        ///
        /// <para>Mốc góc giữa hai camera KHÔNG lấy ở đây, và cũng không cần "A = 0°": camera dưới đo hướng
        /// thật của sản phẩm chứ không đo giá trị tuyệt đối của A, và Auto cũng không bao giờ đưa A về 0°
        /// (nó chạy tới <c>CmdA</c> = góc lúc chụp + góc bù). Mốc góc đo ở bước xác định hand-eye
        /// (<see cref="HandEyeCommitAsync"/>), điều kiện duy nhất là <b>không xoay A giữa lúc hút con can
        /// lắp đúng và lúc chụp</b>.</para>
        /// </summary>
        public async Task<AlignCalibSequenceResult> RunAlignCalibSequenceAsync(
            int nozzle, double alignPosX, IList<double> xOffsets, IList<double> anglesDeg,
            Action<string> progress, CancellationToken ct)
        {
            var result = new AlignCalibSequenceResult();
            try
            {
                string axisName = nozzle == 1 ? AxisId.A1 : AxisId.A2;
                var proc = nozzle == 1 ? EVisionProcedure.AlignCalibTool1 : EVisionProcedure.AlignCalibTool2;

                if (anglesDeg == null || anglesDeg.Count < 3)
                {
                    result.Reason = Loc.T("Calib.NeedThreeAngles");
                    return result;
                }

                // Xoá điểm của lần trước trước khi chạy trục nào — chạy chồng lên danh sách cũ là ra file hiệu chuẩn trộn
                // hai lần nạp, không có thông báo lỗi nào.
                string clearErr = await ClearCalibPointsAsync(proc).ConfigureAwait(false);
                if (clearErr != null)
                { result.Reason = Loc.F("Calib.ClearPointsFailed", clearErr); return result; }

                if (!await MoveZToBottomCaptureAsync(nozzle, ct).ConfigureAwait(false))
                { result.Reason = ZCaptureFail(nozzle); return result; }

                string flow = _sys().VisionOutputs.FlowAlignBottom(nozzle) + " (hiệu chuẩn)";
                int total = (xOffsets == null ? 0 : xOffsets.Count) + anglesDeg.Count;
                LogWorker.Instance.RecordLog(string.Format(CultureInfo.InvariantCulture,
                    "Hiệu chuẩn Align tay #{0}: bắt đầu — {1} {2:0.000}, vị trí soi X {3:0.000}, lệch X [{4}] mm, góc {5} [{6}]°",
                    nozzle, ZName(nozzle), Pos(ZName(nozzle)), alignPosX, Join(xOffsets), axisName, Join(anglesDeg)),
                    Utility.Log.LogLevel.Info, "Calib");

                // Phần tịnh tiến chụp ở MỘT góc cố định — góc đầu của dãy. Vừa xoay vừa tịnh tiến
                // thì module hiệu chuẩn không tách được hai thành phần.
                double firstAngle = anglesDeg[0];
                if (!await MoveAsync(axisName, firstAngle, ct).ConfigureAwait(false))
                { result.Reason = Loc.F("Calib.RotateToFirstAngleFailed", axisName); return result; }

                if (xOffsets != null)
                {
                    for (int i = 0; i < xOffsets.Count; i++)
                    {
                        double x = alignPosX + xOffsets[i];
                        if (progress != null) progress(Loc.F("Calib.ProgressShift", i + 1, xOffsets.Count, x));

                        if (!await MoveAsync(AxisId.X, x, ct).ConfigureAwait(false))
                        { result.Reason = Loc.F("Calib.MoveXToFailed", x); return result; }

                        await _func.WaitSettleAsync(EVmCamera.BottomUp, ct).ConfigureAwait(false);
                        var snap = Snapshot();
                        var pose = await WithCamLampAsync(EVmCamera.BottomUp, ct,
                            () => _vision().TriggerCalibAsync(proc, snap, ct)).ConfigureAwait(false);
                        LogCalibPoint(string.Format("{0} điểm {1}/{2}", flow, result.PointsCaptured + 1, total), snap, nozzle, pose.Found);
                        if (!pose.Found)
                        { result.Reason = Loc.F("Calib.ShiftPointNotFound", x); return result; }
                        result.PointsCaptured++;
                    }
                }

                // Phần xoay: luôn tại ĐÚNG vị trí soi của tay này — chạy thật cũng dừng ở đó
                // (PDF Hikrobot tr.45: vị trí chụp lúc chạy phải trùng vị trí chụp lúc hiệu chuẩn).
                if (!await MoveAsync(AxisId.X, alignPosX, ct).ConfigureAwait(false))
                { result.Reason = Loc.T("Calib.MoveXToAlignPosFailed"); return result; }

                for (int i = 0; i < anglesDeg.Count; i++)
                {
                    double angle = anglesDeg[i];
                    if (progress != null) progress(Loc.F("Calib.ProgressAngle", i + 1, anglesDeg.Count, angle));

                    if (!await MoveAsync(axisName, angle, ct).ConfigureAwait(false))
                    { result.Reason = Loc.F("Calib.RotateToFailed", angle); return result; }

                    await _func.WaitSettleAsync(EVmCamera.BottomUp, ct).ConfigureAwait(false);
                    var snap = Snapshot();
                    var pose = await WithCamLampAsync(EVmCamera.BottomUp, ct,
                        () => _vision().TriggerCalibAsync(proc, snap, ct)).ConfigureAwait(false);
                    LogCalibPoint(string.Format("{0} điểm {1}/{2}", flow, result.PointsCaptured + 1, total), snap, nozzle, pose.Found);
                    if (!pose.Found)
                    { result.Reason = Loc.F("Calib.AngleNotFound", angle); return result; }
                    result.PointsCaptured++;
                }

                if (!await MoveAsync(axisName, 0, ct).ConfigureAwait(false))
                { result.Reason = Loc.F("Calib.AxisToZeroFailed", axisName); return result; }

                result.Ok = true;
                LogWorker.Instance.RecordLog(
                    string.Format(CultureInfo.InvariantCulture, "Hiệu chuẩn Align tay #{0}: đã nạp {1} điểm ở {2} {3:0.000}, trục A về 0°",
                        nozzle, result.PointsCaptured, ZName(nozzle), Pos(ZName(nozzle))),
                    Utility.Log.LogLevel.Info, "Calib");
                return result;
            }
            catch (OperationCanceledException)
            {
                result.Reason = Loc.T("Calib.Cancelled");
                return result;
            }
            catch (Exception ex)
            {
                AlarmManager.RaiseEvent(AlarmCode.RoutineCrashed, "Calib", ex.Message);
                result.Reason = Loc.F("Calib.UnexpectedError", ex.Message);
                return result;
            }
        }

        /// <summary>Xoay trục A của tay <paramref name="nozzle"/> tới <paramref name="angleDeg"/>
        /// rồi kích nhánh hiệu chuẩn Flow AlignBottom của đúng tay đó — nạp một điểm lẻ.</summary>
        public async Task<bool> MoveAndTriggerAlignCalibAsync(int nozzle, double angleDeg, CancellationToken ct)
        {
            string axisName = nozzle == 1 ? AxisId.A1 : AxisId.A2;
            if (_motion[axisName] == null) return false;

            if (!await MoveAsync(axisName, angleDeg, ct).ConfigureAwait(false))
                return false;

            return await TriggerAlignCalibFlowAsync(nozzle, ct).ConfigureAwait(false);
        }

        /// <summary>Kích nhánh hiệu chuẩn Flow AlignBottom của tay <paramref name="nozzle"/> tại góc hiện tại,
        /// không xoay — nạp thêm một điểm tại chỗ.</summary>
        public async Task<bool> TriggerAlignCalibFlowAsync(int nozzle, CancellationToken ct)
        {
            var proc = nozzle == 1 ? EVisionProcedure.AlignCalibTool1 : EVisionProcedure.AlignCalibTool2;
            if (!await MoveZToBottomCaptureAsync(nozzle, ct).ConfigureAwait(false)) return false;
            await _func.WaitSettleAsync(EVmCamera.BottomUp, ct).ConfigureAwait(false);
            var snap = Snapshot();
            var pose = await WithCamLampAsync(EVmCamera.BottomUp, ct,
                () => _vision().TriggerCalibAsync(proc, snap, ct)).ConfigureAwait(false);
            LogCalibPoint(_sys().VisionOutputs.FlowAlignBottom(nozzle) + " (hiệu chuẩn) nạp một điểm tại chỗ", snap, nozzle, pose.Found);
            return pose.Found;
        }

        /// <summary>Đưa trục A của tay <paramref name="nozzle"/> về 0° — tư thế xác định sau chuỗi nạp điểm
        /// hiệu chuẩn bottom-up.</summary>
        public async Task<bool> MoveAlignAxisToZeroAsync(int nozzle, CancellationToken ct)
        {
            string axisName = nozzle == 1 ? AxisId.A1 : AxisId.A2;
            if (_motion[axisName] == null) return false;
            return await MoveAsync(axisName, 0, ct).ConfigureAwait(false);
        }

        // ───────────────────────── §6.2b, §6.4b, §6.5 — đo hằng số ─────────────────────────

        /// <summary>Kết quả kiểm tra hiệu chuẩn gần nhất (nút ở trang Vision Edit, chạy khô, sau mỗi bước đo).</summary>
        public VmCalibGate LastCalibGate { get; private set; }

        /// <summary>Cổng hiệu chuẩn như lúc START — Bước 2/4/5, vị trí soi, ngưỡng. Chỉ đọc model của App.</summary>
        public VmCalibGate CheckCalib()
        {
            var g = VmCycle.EvaluateCalib(McDataManager.Model, _sys());
            LastCalibGate = g;
            return g;
        }

        /// <summary>
        /// Chụp N ảnh bằng LocateTop (<paramref name="tool"/> = 0) hoặc AlignBottom_ToolN không kèm ô đích, rồi App lấy
        /// trung bình: X, Y cộng thẳng, góc theo sin/cos để không gãy ở ±180°, vị trí trục lúc chụp từ encoder. Mỗi ảnh
        /// phải thấy vật và echo khớp — lệch là huỷ cả chuỗi.
        /// </summary>
        private async Task<VmMeanCapture> CaptureMeanAsync(int tool, int shots, CancellationToken ct)
        {
            var res = new VmMeanCapture { Shots = shots };
            var names = _sys().VisionOutputs;
            res.Reason = VmCycle.AxisVarProblem(names);
            if (res.Reason != null) return res;
            if (tool != 0 && !await MoveZToBottomCaptureAsync(tool, ct).ConfigureAwait(false))
            { res.Reason = ZCaptureFail(tool); return res; }

            double sx = 0, sy = 0, ss = 0, sc = 0, ax = 0, ay = 0, as_ = 0, ac = 0;
            // Dao động giữa các ảnh (lớn nhất − nhỏ nhất) — để log phân biệt được số đo rung với số đo lệch đều.
            double minX = double.MaxValue, maxX = double.MinValue, minY = double.MaxValue, maxY = double.MinValue;
            double firstA = 0, minDA = 0, maxDA = 0;
            string flow = tool == 0 ? "LocateTop" : "AlignBottom_Tool" + tool;
            for (int i = 1; i <= shots; i++)
            {
                if (ct.IsCancellationRequested) { res.Reason = Loc.T("Calib.Cancelled"); return res; }

                await _func.WaitSettleAsync(tool == 0 ? EVmCamera.TopDown : EVmCamera.BottomUp, ct).ConfigureAwait(false);
                int rid = VmRunIds.Next();
                var g = VmCycle.AxisGlobals(_motion, names, rid);
                VmCallRequest req;
                if (tool == 0) req = VmCycle.LocateRequest(names, g);
                else
                {
                    VmCycle.AddPlaceGlobals(g, null, McDataManager.Model?.Calib, tool);
                    req = VmCycle.AlignBottomRequest(names, tool, g, false);
                }
                var r = await CallVmAsync(req, ct).ConfigureAwait(false);
                var read = tool == 0 ? VmCycle.ReadLocate(r, g, names) : VmCycle.ReadAlignBottom(r, req, names, tool);
                if (tool == 0) VisionReadout.TopDown(read);
                else VisionReadout.Tool(tool, read, null);
                if (tool != 0)
                    LogWorker.Instance.RecordLog(VmCycle.TraceAlignBottom(r, req, names, tool, Pos(ZName(tool))).Line,
                        Utility.Log.LogLevel.Info, "Calib");

                string what = Loc.F("Calib.ShotPrefix", flow, i, shots);
                if (!read.CallOk) { res.Reason = what + read.CallError; return res; }
                if (!read.EchoOk) { res.Reason = what + read.EchoIssue; return res; }
                if (!read.Found) { res.Reason = what + Loc.T(tool == 0 ? "Calib.NoObjectSeen" : "Calib.NoProductSeen"); return res; }
                if (!read.VmValid) { res.Reason = what + read.VmIssue; return res; }

                sx += read.X;
                sy += read.Y;
                minX = Math.Min(minX, read.X); maxX = Math.Max(maxX, read.X);
                minY = Math.Min(minY, read.Y); maxY = Math.Max(maxY, read.Y);
                if (i == 1) firstA = read.A;
                double dA = AxisAngle.ShortestDelta(firstA, read.A);
                minDA = Math.Min(minDA, dA); maxDA = Math.Max(maxDA, dA);
                double rad = read.A * Math.PI / 180.0;
                ss += Math.Sin(rad);
                sc += Math.Cos(rad);
                ax += VmCycle.Val(g, names.GvAxisX);
                ay += VmCycle.Val(g, names.GvAxisY2);
                double axisA = VmCycle.Val(g, tool == 2 ? names.GvAxisA2 : names.GvAxisA1) * Math.PI / 180.0;
                as_ += Math.Sin(axisA);
                ac += Math.Cos(axisA);
            }

            res.X = sx / shots;
            res.Y = sy / shots;
            res.A = VmCycle.MeanAngleDeg(ss, sc);
            res.AxX = ax / shots;
            res.AxY = ay / shots;
            res.AxA = VmCycle.MeanAngleDeg(as_, ac);
            res.Ok = true;

            string spread = string.Format(CultureInfo.InvariantCulture, "dao động X {0:0.000}, Y {1:0.000} mm, góc {2:0.00}°",
                maxX - minX, maxY - minY, maxDA - minDA);
            LogWorker.Instance.RecordLog(tool == 0
                ? string.Format(CultureInfo.InvariantCulture,
                    "{0}, trung bình {1} ảnh: vật tại X {2:0.000}, Y {3:0.000} mm, góc {4:0.00}° ({5}); trục X {6:0.000}, Y2 {7:0.000}",
                    flow, shots, res.X, res.Y, res.A, spread, res.AxX, res.AxY)
                : string.Format(CultureInfo.InvariantCulture,
                    "{0}, trung bình {1} ảnh: sản phẩm lệch tâm xoay X {2:0.000}, Y {3:0.000} mm, góc {4:0.00}° ({5}); trục X {6:0.000}, {7} {8:0.000}°, {9} {10:0.000}",
                    flow, shots, res.X, res.Y, res.A, spread, res.AxX, AName(tool), res.AxA, ZName(tool), Pos(ZName(tool))),
                Utility.Log.LogLevel.Info, "Calib");
            return res;
        }

        private static string SaveModelNote()
        {
            if (!McDataManager.ModelLoaded) return Loc.T("Calib.ModelNotLoadedNote");
            return McDataManager.SaveModel()
                ? Loc.F("Calib.SavedToModel", McDataManager.Model.Name)
                : Loc.T("Calib.SaveModelFailed");
        }

        /// <summary>
        /// Bước 2 (§6.2b): soi dấu chuẩn (sản phẩm mẫu đặt cố định trong ô tray) ở ba chỗ bằng LocateTop — gốc, sau khi
        /// chạy X, sau khi chạy Y2 — rồi App so vị trí
        /// (<see cref="VmCycle.JudgeTopDown"/>). Dấu chuẩn đứng yên trong hệ máy nên <c>TopX/TopY</c> phải không đổi khi trục
        /// chạy — bắt được sai thang đo, <b>sai dấu</b> và lẫn trục X ↔ Y. Đạt thì ghi ngày vào
        /// bản ghi của App. Mọi lần trả trục về đều được kiểm.
        /// </summary>
        public async Task<TopDownVerifyResult> VerifyTopDownAsync(double stepMm, CancellationToken ct)
        {
            var result = new TopDownVerifyResult { StepMm = stepMm };
            try
            {
                if (Math.Abs(stepMm) < 0.5)
                {
                    result.Reason = Loc.T("Calib.StepTooSmall");
                    return result;
                }

                var sys = _sys();
                int shots = Math.Max(1, sys.CalibShotsPerPoint);
                double x0 = _motion[AxisId.X]?.CurrentPos ?? 0;
                double y0 = _motion[AxisId.Y2]?.CurrentPos ?? 0;

                var s0 = await CaptureMeanAsync(0, shots, ct).ConfigureAwait(false);
                if (!s0.Ok) { result.Reason = s0.Reason; return result; }

                if (!await MoveAsync(AxisId.X, x0 + stepMm, ct).ConfigureAwait(false))
                { result.Reason = Loc.F("Calib.AxisMoveFailed", AxisId.X); return result; }
                var s1 = await CaptureMeanAsync(0, shots, ct).ConfigureAwait(false);
                bool backX = await MoveAsync(AxisId.X, x0, ct).ConfigureAwait(false);
                if (!s1.Ok) { result.Reason = s1.Reason; return result; }
                if (!backX) { result.Reason = Loc.F("Calib.AxisBackToStartFailed", AxisId.X); return result; }

                if (!await MoveAsync(AxisId.Y2, y0 + stepMm, ct).ConfigureAwait(false))
                { result.Reason = Loc.F("Calib.AxisMoveFailed", AxisId.Y2); return result; }
                var s2 = await CaptureMeanAsync(0, shots, ct).ConfigureAwait(false);
                bool backY = await MoveAsync(AxisId.Y2, y0, ct).ConfigureAwait(false);
                if (!s2.Ok) { result.Reason = s2.Reason; return result; }
                if (!backY) { result.Reason = Loc.F("Calib.AxisBackToStartFailed", AxisId.Y2); return result; }

                var j = VmCycle.JudgeTopDown(s0, s1, s2, sys.CalibMaxResidualThresholdMm);
                result.Status = j.Status;
                result.ErrorX = j.ErrX;
                result.ErrorY = j.ErrY;
                result.CrossX = j.CrossX;
                result.CrossY = j.CrossY;
                result.SignXOk = j.Status != VmStatus.Code8;
                result.SignYOk = result.SignXOk;
                result.Ok = j.Status == VmStatus.Ok;
                if (!result.Ok) result.Reason = Loc.F("Calib.TopDownVerifyFail", VmStatus.TopDownVerify(j.Status));

                if (result.Ok)
                {
                    var calib = McDataManager.Model.Calib;
                    calib.TopDownVerifiedAt = DateTime.Now;
                    result.Note = SaveModelNote();
                }

                LogWorker.Instance.RecordLog(string.Format(CultureInfo.InvariantCulture,
                    "Kiểm tra CAM top-down (chạy {0:0.0} mm): Status {1}, lệch X {2:0.000}, Y {3:0.000}, lẫn trục {4:0.000}/{5:0.000} mm — {6}",
                    stepMm, result.Status, result.ErrorX, result.ErrorY, result.CrossX, result.CrossY, result.Ok ? "ĐẠT" : "KHÔNG ĐẠT"),
                    result.Ok ? Utility.Log.LogLevel.Info : Utility.Log.LogLevel.Warning, "Calib");
                return result;
            }
            catch (OperationCanceledException)
            {
                result.Reason = Loc.T("Calib.Cancelled");
                return result;
            }
            catch (Exception ex)
            {
                AlarmManager.RaiseEvent(AlarmCode.RoutineCrashed, "Calib", ex.Message);
                result.Reason = Loc.F("Calib.UnexpectedError", ex.Message);
                return result;
            }
        }

        /// <summary>
        /// Bước 4 (§6.4b), sản phẩm đang trên tay tại vị trí soi: App lấy trung bình N ảnh AlignBottom_ToolN trước và sau
        /// khi xoay thử (<c>HasTarget = 0</c> nên Group Cmd nghỉ), rồi <b>tự phán</b> bằng
        /// <see cref="VmCycle.JudgeAlignSign"/> — không Flow nào tham gia. Đạt thì ghi dấu góc, vị trí soi và sai số dư
        /// vào bản ghi của App. Sai số dư khác 0 nghĩa là gốc toạ độ của file bottom-up không nằm đúng tâm xoay, hoặc
        /// chiều hệ toạ độ bị lật.
        /// </summary>
        public async Task<AlignVerifyResult> VerifyAlignRotationAsync(int nozzle, double testAngleDeg, CancellationToken ct)
        {
            var result = new AlignVerifyResult { TestAngleDeg = testAngleDeg };
            try
            {
                var sys = _sys();
                var names = sys.VisionOutputs;
                string axisName = nozzle == 1 ? AxisId.A1 : AxisId.A2;
                if (Math.Abs(testAngleDeg) < 1.0)
                { result.Reason = Loc.T("Calib.TestAngleTooSmall"); return result; }
                if (_motion[axisName] == null)
                { result.Reason = Loc.F("Calib.NoAxis", axisName); return result; }

                double a0 = _motion[axisName].CurrentPos;
                int shots = Math.Max(1, sys.CalibShotsPerPoint);

                var s0 = await CaptureMeanAsync(nozzle, shots, ct).ConfigureAwait(false);
                if (!s0.Ok) { result.Reason = s0.Reason; return result; }
                result.PosX = s0.AxX;

                if (!await MoveAsync(axisName, a0 + testAngleDeg, ct).ConfigureAwait(false))
                { result.Reason = Loc.F("Calib.RotateAxisFailed", axisName); return result; }
                var s1 = await CaptureMeanAsync(nozzle, shots, ct).ConfigureAwait(false);
                bool back = await MoveAsync(axisName, a0, ct).ConfigureAwait(false);
                if (!s1.Ok) { result.Reason = s1.Reason; return result; }
                if (!back) { result.Reason = Loc.F("Calib.AxisBackToStartAngleFailed", axisName); return result; }
                var j = VmCycle.JudgeAlignSign(s0, s1, sys.CalibMaxResidualThresholdMm, sys.AlignVerifyAngleTolDeg,
                                               sys.AlignVerifyMinEccMm, sys.AlignVerifyMinTurnDeg);
                result.Status = j.Status;
                result.Sign = j.Sign;
                result.MeasuredDeltaThetaDeg = j.RawDTheta;
                result.ResidualMm = j.Residual;
                result.EccentricityMm = j.Ecc;
                result.Ok = j.Status == VmStatus.Ok && j.Sign != 0;

                // Số đo đi kèm kết luận, cả trên bảng báo lẫn trong log: chỉ đọc "KHÔNG ĐẠT" thì không biết lệch bao nhiêu
                // và lệch ở đâu (Tuấn yêu cầu 2026-09-22).
                string numbers = string.Format(CultureInfo.InvariantCulture, Loc.T("Calib.AlignVerifyNumbers"),
                    axisName, j.EncDA, AxisAngle.ShortestDelta(s0.A, s1.A), Math.Abs(Math.Abs(j.RawDTheta) - Math.Abs(j.EncDA)),
                    sys.AlignVerifyAngleTolDeg, j.Residual, sys.CalibMaxResidualThresholdMm, j.Ecc, sys.AlignVerifyMinEccMm,
                    s0.AxX, ZName(nozzle), Pos(ZName(nozzle)));
                if (!result.Ok) result.Reason = Loc.F("Calib.AlignVerifyFail", nozzle, VmStatus.AlignVerify(j.Status), numbers);

                if (result.Ok)
                {
                    var calib = McDataManager.Model.Calib;
                    calib.SetAlignVerified(nozzle, result.Sign, result.PosX, result.ResidualMm, DateTime.Now);
                    result.Note = SaveModelNote();
                }

                LogWorker.Instance.RecordLog(string.Format(CultureInfo.InvariantCulture,
                    "Phép thử vòng xoay tay #{0}: Status {1} ({2}), dấu {3}; {4}; sản phẩm trước ({5:0.000}, {6:0.000}, {7:0.00}°) → sau ({8:0.000}, {9:0.000}, {10:0.00}°)",
                    nozzle, result.Status, VmStatus.AlignVerify(j.Status), result.Sign, numbers,
                    s0.X, s0.Y, s0.A, s1.X, s1.Y, s1.A),
                    result.Ok ? Utility.Log.LogLevel.Info : Utility.Log.LogLevel.Warning, "Calib");
                return result;
            }
            catch (OperationCanceledException)
            {
                result.Reason = Loc.T("Calib.Cancelled");
                return result;
            }
            catch (Exception ex)
            {
                AlarmManager.RaiseEvent(AlarmCode.RoutineCrashed, "Calib", ex.Message);
                result.Reason = Loc.F("Calib.UnexpectedError", ex.Message);
                return result;
            }
        }

        // ───────────────────────── xác định hand-eye (§6.5) ─────────────────────────
        //
        // Bảy nút, mỗi nút một việc (Tuấn yêu cầu 2026-09-25). 26/09: nút tính điểm đích tách thành 7 · tới vị trí soi
        // CAM dưới và 8 · Trigger AlignBottom, thêm 9 · tới điểm đích vừa tính.
        // Trước đó cả bước là hai nút và vị trí gắp lấy theo
        // hàng/cột của tray lắp ráp. Nay gắp trên CƠ CẤU KẸP giữ chi tiết đích đứng yên, ở vị trí Jog-dạy riêng —
        // xem ModelData.HandEyePick_Nozzle1 để biết vì sao không dùng ô tray.
        //
        // Trạng thái giữa các nút nằm ở ĐÂY, không ở ViewModel — cùng lý do với khu chạy kiểm từng bước:
        // --selftest gọi thẳng lớp này, nên mọi cổng chặn đều được kiểm chứ không nằm ở chỗ test không tới được.

        /// <summary>Tiến trình của bước xác định hand-eye đang dở; null = chưa bấm nút nào.</summary>
        private HandEyeProgress _he;

        private class HandEyeProgress
        {
            public int Nozzle;

            /// <summary>Đã gắp xong. Phải có cờ riêng: 0.000 là toạ độ hợp lệ, nên không suy được "đã gắp chưa"
            /// từ chính <see cref="PickX"/>/<see cref="PickY"/>.</summary>
            public bool Picked;

            /// <summary>Encoder X, Y2 đọc ĐÚNG LÚC gắp, khi tay còn đứng trên cơ cấu kẹp — vào thẳng phép trừ.</summary>
            public double PickX, PickY;

            /// <summary>Encoder trục A của tay này đọc cùng lúc với <see cref="PickX"/>. Nút 5 xoay A về đúng góc này
            /// trước khi chụp — xem <see cref="HandEyeCommitAsync"/>.</summary>
            public double PickA;

            /// <summary>Số ảnh của lần soi chi tiết đích ở vị trí soi đã dạy; 0 = chưa soi.</summary>
            public int Shots;
            public double TopX, TopY, TopA;
            /// <summary>Encoder X/Y2 trung bình lúc chụp bước 3.</summary>
            public double TopAxX, TopAxY;
        }

        /// <summary>
        /// Trạng thái của <b>phép thử tổng quát</b> (nút 6 → 9) — tách khỏi <see cref="_he"/> (Tuấn yêu cầu
        /// 2026-09-26): hand-eye đã ghi vào model thì thử lúc nào cũng được, kể cả sau khi mở lại App, không phải
        /// đo lại từ nút 1. null = chưa soi.
        /// </summary>
        private HandEyeTest _heTest;

        private class HandEyeTest
        {
            public int Nozzle;

            /// <summary>Lần soi của nút 6.</summary>
            public double HereTopX, HereTopY, HereTopA, HereAxX, HereAxY;

            /// <summary>Điểm đích nút 8 tính từ pose của lần soi gần nhất — nút 9 chạy tới đây. Soi lại (nút 6) là
            /// xoá, vì điểm đích tính từ pose cũ không còn là của lần soi mới.</summary>
            public bool HasTarget;
            public double TargetX, TargetY, TargetA;
        }

        /// <summary>
        /// Bỏ tiến trình đang dở. Đường chạy thật tự dọn ở hai chỗ — nút 1 xoá sạch khi bắt đầu lại, và nút trả xoá
        /// khi đã nhả xong — nên KHÔNG có hook nào gọi hàm này; nó để <c>--selftest</c> dựng lại trạng thái đầu
        /// giữa các kịch bản. Cố ý không gọi khi đổi model hay rời trang: xoá sổ lúc tay còn cầm con can là mở
        /// đường cho lần hạ sau đưa một tay "trống" xuống chỗ đã có hàng.
        /// </summary>
        public void HandEyeClear() { _he = null; _heTest = null; }

        /// <summary>
        /// Cổng chặn của phép thử tổng quát — KHÔNG đòi nút 1 → 5 trong phiên này, chỉ đòi hand-eye của tay này đã
        /// ghi trong model: 0 = nút 6, 7 · 1 = đã soi (nút 8) · 2 = đã tính điểm đích (nút 9).
        /// </summary>
        private string HandEyeTestNeeds(int nozzle, int stage)
        {
            var model = McDataManager.Model;
            if (model == null) return Loc.T("Calib.NoModel");
            if (!model.Calib.IsHandEyeMeasured(nozzle))
                return Loc.F("Calib.HandEyeNotRecorded", nozzle);
            if (stage >= 1 && (_heTest == null || _heTest.Nozzle != nozzle))
                return Loc.F("Calib.NeedLocateHere", nozzle);
            if (stage >= 2 && !_heTest.HasTarget)
                return Loc.T("Calib.NeedTarget");
            return null;
        }

        /// <summary>Lệch của điểm đích so với chỗ đã gắp — chỉ có nghĩa khi con can trên tay được gắp bằng nút 2 trong
        /// phiên này; hút lên cách khác (trang Manual, hay còn trên tay từ trước khi mở App) thì không có chỗ gắp nào
        /// để so, trả chuỗi rỗng.</summary>
        private string HandEyeVsPick(int nozzle, double x, double y, string rowAxis)
        {
            if (_he == null || _he.Nozzle != nozzle || !_he.Picked) return "";
            return string.Format(CultureInfo.InvariantCulture, Loc.T("Calib.VsPick"),
                x - _he.PickX, rowAxis, y - _he.PickY);
        }

        /// <summary>
        /// Cổng chặn dùng chung: tiến trình đang dở phải là của tay này và đã qua ít nhất <paramref name="stage"/>
        /// mốc — 1 đã tới vị trí gắp · 2 đã gắp · 3 đã soi chi tiết. Nút 6 → 9 dùng <see cref="HandEyeTestNeeds"/>.
        ///
        /// <para>Trả CÂU nói thiếu bước nào / tay nào, không trả <c>bool</c>: nút xám không nói gì, mà câu
        /// "tay hút #1 chưa gắp con can trên cơ cấu kẹp — bấm 2 trước" mới là thứ người vận hành cần. Cũng vì thế
        /// mà chặn lúc chạy chứ không dùng <c>CanExecute</c> (xem VisionCalibViewModel).</para>
        /// </summary>
        private string HandEyeNeeds(int nozzle, int stage)
        {
            if (_he == null)
                return Loc.T("Calib.HeNotStarted");
            if (_he.Nozzle != nozzle)
                return Loc.F("Calib.HeOtherNozzle", _he.Nozzle, nozzle);
            if (stage >= 2 && !_he.Picked)
                return Loc.F("Calib.HeNotPicked", nozzle);
            if (stage >= 3 && _he.Shots <= 0)
                return Loc.T("Calib.HeNotLocated");
            return null;
        }

        /// <summary>
        /// Bước 1 — tay đang chọn tới vị trí gắp đã dạy trên cơ cấu kẹp, trục A về góc hút. KHÔNG chạm Z.
        /// Bấm lại là làm lại từ đầu: mọi số đo của lần trước bị bỏ.
        /// </summary>
        public Task<StepRunResult> HandEyeGoToPickAsync(int nozzle, CancellationToken ct)
        {
            return StepGuard("Tới vị trí gắp", () => HandEyeGoToPickCore(nozzle, ct), "Xác định hand-eye");
        }

        private async Task<StepRunResult> HandEyeGoToPickCore(int nozzle, CancellationToken ct)
        {
            var res = new StepRunResult();
            var model = McDataManager.Model;
            if (model == null) { res.Reason = Loc.T("Calib.NoModel"); return res; }
            res.Reason = model.HandEyeJigProblem(nozzle);
            if (res.Reason != null) return res;
            if (_func.HasProduct(nozzle))
            { res.Reason = Loc.F("Calib.NozzleBusyRelease", nozzle); return res; }

            _he = null;   // bấm nút 1 là bỏ hết số đo cũ, kể cả của tay kia
            var tray = model.AssyTray;
            var pick = model.HandEyePick(nozzle);
            if (!await _func.EnsureZSafeAsync(ct).ConfigureAwait(false))
            { res.Reason = Loc.T("Calib.ZSafeFailed"); return res; }
            if (!await _func.MoveToCellForPickAsync(nozzle, model.PickAngle(nozzle), tray.AxisRow, pick.Y, pick.X, ct).ConfigureAwait(false))
            { res.Reason = Loc.T("Calib.GoToJigPickFailed"); return res; }

            _he = new HandEyeProgress { Nozzle = nozzle };
            res.Ok = true;
            res.Text = string.Format(CultureInfo.InvariantCulture, Loc.T("Calib.HeAtPick"),
                nozzle, pick.X, tray.AxisRow, pick.Y, AName(nozzle), model.PickAngle(nozzle));
            return res;
        }

        /// <summary>
        /// Bước 2 — hạ Z xuống gắp con can mẫu trên cơ cấu kẹp, rồi nâng lên.
        ///
        /// <para><b>Cao độ hạ và điểm giảm tốc lấy của tray LẮP RÁP</b> (Tuấn chốt 2026-09-25), nên cơ cấu kẹp phải
        /// giữ con can ở đúng cao độ như một ô tray — cao hơn thì đầu hút ấn xuống con can, thấp hơn thì gắp hụt.</para>
        ///
        /// <para>Ra lệnh lại tới đúng vị trí gắp trước khi hạ, chứ không tin là tay còn đứng đó: người bấm có thể jog
        /// đi đâu đó sau khi bấm nút 1, và hạ Z ở chỗ khác là hạ vào chỗ không có gì đỡ.</para>
        ///
        /// <para>Encoder đọc khi tay <b>còn đứng trên</b> cơ cấu kẹp — hai con số đó vào thẳng phép trừ của hand-eye,
        /// đọc sau khi chạy đi là mất nghĩa.</para>
        /// </summary>
        public Task<StepRunResult> HandEyePickAsync(int nozzle, CancellationToken ct)
        {
            return StepGuard("Gắp con can mẫu", () => HandEyePickCore(nozzle, ct), "Xác định hand-eye");
        }

        private async Task<StepRunResult> HandEyePickCore(int nozzle, CancellationToken ct)
        {
            var res = new StepRunResult();
            var model = McDataManager.Model;
            if (model == null) { res.Reason = Loc.T("Calib.NoModel"); return res; }
            res.Reason = HandEyeNeeds(nozzle, 1);
            if (res.Reason != null) return res;
            res.Reason = model.HandEyeJigProblem(nozzle);
            if (res.Reason != null) return res;
            if (_func.HasProduct(nozzle))
            { res.Reason = Loc.F("Calib.NozzleBusyRelease", nozzle); return res; }
            string zName = ZName(nozzle), aName = AName(nozzle);
            if (_motion[AxisId.X] == null || _motion[AxisId.Y2] == null || _motion[aName] == null)
            { res.Reason = Loc.F("Calib.CannotReadAxes", aName); return res; }

            var tray = model.AssyTray;
            var pick = model.HandEyePick(nozzle);

            if (!await _func.EnsureZSafeAsync(ct).ConfigureAwait(false))
            { res.Reason = Loc.T("Calib.ZSafeFailed"); return res; }
            if (!await _func.MoveToCellForPickAsync(nozzle, model.PickAngle(nozzle), tray.AxisRow, pick.Y, pick.X, ct).ConfigureAwait(false))
            { res.Reason = Loc.T("Calib.GoToJigPickFailed"); return res; }

            double pickX = _motion[AxisId.X].CurrentPos, pickY = _motion[AxisId.Y2].CurrentPos;
            double pickA = _motion[aName].CurrentPos;

            if (!await _func.MoveZDownAsync(zName, tray.ZPickFor(zName), tray.ZSlowFor(zName), ct).ConfigureAwait(false))
            { res.Reason = Loc.F("Calib.ZDownAssyPlaceFailed", zName); return res; }

            _func.SetVacuum(nozzle, true);
            bool gripped = await WaitVacuumAsync(nozzle, true, ct).ConfigureAwait(false);
            await SettleVacuumAsync(gripped, ct).ConfigureAwait(false);

            if (!await _func.MoveZUpAsync(zName, _motion.ZSafe, tray.ZSlowFor(zName), ct).ConfigureAwait(false))
            { res.Reason = Loc.F("Calib.ZUpFailed", zName); return res; }
            if (!gripped)
            {
                _func.SetVacuum(nozzle, false);
                res.Reason = Loc.T("Calib.HePickMissed");
                return res;
            }

            // Ghi sổ: cảm biến đang báo có hàng thì phần mềm phải biết, không thì lần START sau ResumeIntegrityOk
            // thấy hai bên lệch nhau, phát alarm và bắt RESET. Ô nguồn để −1 vì con can này KHÔNG đến từ ô tray nào —
            // đúng nghĩa mà Routine_Auto.ReturnToSourceAsync gán cho −1: không có chỗ đúng để trả, phải người lấy ra.
            var run = McDataManager.RunData;
            var nz = nozzle == 1 ? run.Nozzle1 : run.Nozzle2;
            nz.HasProduct = true;
            nz.Aligned = false;
            nz.SourceCellIndex = -1;
            McDataManager.SaveRunningData();

            _he.Picked = true;
            _he.PickX = pickX;
            _he.PickY = pickY;
            _he.PickA = pickA;
            res.Ok = true;
            res.Text = string.Format(CultureInfo.InvariantCulture, Loc.T("Calib.HePicked"),
                nozzle, pickX, tray.AxisRow, pickY, aName, pickA, tray.ZPickFor(zName));
            return res;
        }

        /// <summary>
        /// Bước 3 — CAM top-down tới vị trí soi đã dạy rồi soi <b>chi tiết đích vừa lộ ra</b> bằng <c>LocateTop</c>
        /// N ảnh.
        ///
        /// <para><b>Gắp trước, soi sau</b>: sản phẩm gồm hai mảnh nên chưa nhấc con can ra thì camera trên chỉ thấy
        /// con can, mà số cần trừ là vị trí <b>chi tiết</b> — đúng thứ Auto lấy làm pose ô đích. Cơ cấu kẹp giữ chi
        /// tiết đứng yên nên nhấc con can ra không làm chi tiết xê dịch, đó là cả lý do nó tồn tại.</para>
        ///
        /// <para><b>Không xoay A</b> từ đây tới hết bước: mốc góc giữa hai camera và lượng lệch tâm đều phải đọc ở
        /// đúng tư thế vừa gắp. Thanh Jog của trang có A1/A2, nên nút 5 không tin điều này mà tự xoay A về góc lúc
        /// gắp trước khi chụp.</para>
        /// </summary>
        public Task<StepRunResult> HandEyeLocateAsync(int nozzle, CancellationToken ct)
        {
            return StepGuard("Soi chi tiết đích", () => HandEyeLocateCore(nozzle, false, ct), "Xác định hand-eye");
        }

        /// <summary>
        /// Bước 6 — <b>phép thử tổng quát</b> (Tuấn nêu 2026-09-25): Jog X/Y2 tuỳ ý miễn chi tiết còn trong khung
        /// hình, rồi kích <c>LocateTop</c> <b>tại chỗ</b>, không chạy trục. Chạy được vì <c>TopX_mm/TopY_mm</c> là
        /// toạ độ máy tuyệt đối — Script <c>TopAbs</c> cộng encoder X/Y2 mà App ghi vào biến toàn cục — nên pose ra
        /// y nhau bất kể Jog tới đâu.
        ///
        /// <para><b>Nó chứng minh gì:</b> pose top-down bất biến khắp khung hình (chạy lại ở nhiều chỗ Jog), camera
        /// dưới lặp lại được, sổ góc A đúng, Group Cmd nối đúng — xem <see cref="HandEyeCheckTargetAsync"/>.</para>
        ///
        /// <para><b>Nó KHÔNG chứng minh</b> giá trị tuyệt đối của hand-eye: <c>Dx</c> đo từ chính con can ấy ở chính
        /// chỗ ấy, nên mọi sai chung cho cả lần đo và lần thử đều triệt tiêu — chi tiết xê dịch lúc gắp, hay con can
        /// ngồi lệch chi tiết, đều cho <c>Cmd</c> ra đúng <c>X_gắp</c> trong khi hand-eye sai đều. Đừng trình bày
        /// phép này như nghiệm thu hand-eye; cái tuyệt đối chỉ có nhìn hàng thật sau khi đặt, hoặc nhìn
        /// xuống sau khi chạy khô, nói được.</para>
        ///
        /// <para><b>Chọn ô tray lắp ráp</b> (Tuấn thêm 2026-09-26): có ô thì CAM top-down tới vị trí soi ô đó — cùng
        /// <c>TrayLayout.CamPosFor</c> với S05 — rồi mới kích. Kích <b>một</b> ảnh, không lấy trung bình như nút 3:
        /// nút này để xem, số của nó không ghi vào đâu.</para>
        /// </summary>
        public Task<StepRunResult> HandEyeLocateHereAsync(int nozzle, CancellationToken ct)
        {
            return HandEyeLocateHereAsync(nozzle, -1, -1, ct);
        }

        /// <param name="row">Hàng ô tray lắp ráp (từ 0); âm = soi tại chỗ, không chạy trục.</param>
        public Task<StepRunResult> HandEyeLocateHereAsync(int nozzle, int row, int col, CancellationToken ct)
        {
            return StepGuard(row < 0 ? "Soi chi tiết tại chỗ" : "Soi chi tiết ở ô tray lắp ráp",
                () => HandEyeLocateCore(nozzle, true, ct, row, col), "Xác định hand-eye");
        }

        private async Task<StepRunResult> HandEyeLocateCore(int nozzle, bool here, CancellationToken ct, int row = -1, int col = -1)
        {
            var res = new StepRunResult();
            var sys = _sys();
            res.Reason = VmCycle.AxisVarProblem(sys.VisionOutputs);
            if (res.Reason != null) return res;
            var model = McDataManager.Model;
            if (model == null) { res.Reason = Loc.T("Calib.NoModel"); return res; }
            // Nút 6 chỉ đòi hand-eye đã ghi trong model, không đòi nút 1 → 5 trong phiên này (Tuấn 26/09), và không
            // đòi tay cầm hàng — nó chỉ soi bằng CAM trên; nút 8 mới cần con can.
            res.Reason = here ? HandEyeTestNeeds(nozzle, 0) : HandEyeNeeds(nozzle, 2);
            if (res.Reason != null) return res;
            if (!here && !_func.HasProduct(nozzle))
            { res.Reason = Loc.F("Calib.NozzleLostCan", nozzle); return res; }

            if (!here)
            {
                res.Reason = model.HandEyeJigProblem(nozzle);
                if (res.Reason != null) return res;
                var cam = model.HandEyeCamPos;
                if (!await _func.MoveToCellAsync(model.AssyTray.AxisRow, cam.Y, cam.X, ct).ConfigureAwait(false))
                { res.Reason = Loc.T("Calib.GoToTargetCamFailed"); return res; }
            }
            else if (row >= 0)
            {
                var tray = model.AssyTray;
                if (row >= tray.Rows || col < 0 || col >= tray.Cols)
                { res.Reason = Loc.F("Calib.CellOutsideAssyGrid", tray.Rows, tray.Cols); return res; }
                if (!tray.CamTopDownAnchorTaught)
                { res.Reason = Loc.T("Calib.AssyAnchorNotTaught"); return res; }
                var cam = tray.CamPosFor(row, col);
                if (!await _func.MoveToCellAsync(tray.AxisRow, cam.Y, cam.X, ct).ConfigureAwait(false))
                { res.Reason = Loc.F("Calib.GoToCellCamFailedAt", row + 1, col + 1); return res; }
            }

            int shots = here ? 1 : Math.Max(1, sys.CalibShotsPerPoint);
            var top = await CaptureMeanAsync(0, shots, ct).ConfigureAwait(false);
            if (!top.Ok) { res.Reason = top.Reason; return res; }

            res.Ok = true;
            if (!here)
            {
                _he.Shots = shots;
                _he.TopX = top.X; _he.TopY = top.Y; _he.TopA = top.A;
                _he.TopAxX = top.AxX; _he.TopAxY = top.AxY;
                res.Text = string.Format(CultureInfo.InvariantCulture, Loc.T("Calib.HeLocated"),
                    shots, top.X, top.Y, top.A);
            }
            else
            {
                // Soi mới là bỏ điểm đích cũ — nó tính từ pose của lần soi trước.
                _heTest = new HandEyeTest
                {
                    Nozzle = nozzle,
                    HereTopX = top.X, HereTopY = top.Y, HereTopA = top.A,
                    HereAxX = top.AxX, HereAxY = top.AxY
                };
                res.Text = string.Format(CultureInfo.InvariantCulture, Loc.T("Calib.HeLocatedHere"),
                    row >= 0 ? Loc.F("Calib.AtCell", row + 1, col + 1) : Loc.T("Calib.InPlace"),
                    top.AxX, model.AssyTray.AxisRow, top.AxY, top.X, top.Y, top.A);
                // ΔX/ΔY/Δθ so với điểm teach hand-eye đã lưu (Tuấn 2026-09-30) — cả khi soi ô tray lắp ráp: khi đó Δ gồm
                // cả khoảng cách từ cơ cấu kẹp tới ô. Bản ghi cũ chưa lưu điểm teach thì so với bước 3 của phiên này.
                double tx, ty, ta;
                if (model.Calib.TryGetHandEyeTarget(nozzle, out tx, out ty, out ta))
                    res.Text += string.Format(CultureInfo.InvariantCulture, Loc.T("Calib.VsTeachTarget"),
                        nozzle, top.X - tx, top.Y - ty, AxisAngle.ShortestDelta(ta, top.A));
                else if (row < 0 && _he != null && _he.Nozzle == nozzle && _he.Shots > 0)
                    res.Text += string.Format(CultureInfo.InvariantCulture, Loc.T("Calib.VsStep3"),
                        top.X - _he.TopX, top.Y - _he.TopY, AxisAngle.ShortestDelta(_he.TopA, top.A));
                else
                    res.Text += Loc.F("Calib.NoTeachTarget", nozzle);
            }
            return res;
        }

        /// <summary>
        /// Bước 4 — tay ra vị trí soi CAM bottom-up. Dùng chung lõi với nút 4 của khu chạy kiểm từng bước, để không
        /// có bản sao nào lệch đi. <b>Không xoay A.</b>
        /// </summary>
        public Task<StepRunResult> HandEyeGoToAlignAsync(int nozzle, CancellationToken ct)
        {
            return StepGuard("Tới vị trí soi CAM dưới", () => HandEyeGoToAlignCore(nozzle, "4", ct), "Xác định hand-eye");
        }

        /// <summary>
        /// Bước 7 của phép thử tổng quát — y như nút 4, đặt lại dưới nút 6 cho đúng thứ tự bấm: soi chi tiết → tới vị
        /// trí soi CAM dưới → Trigger AlignBottom → tới điểm đích (Tuấn tách 26/09, cùng khuôn nút 4 → 5 → 6 của khu
        /// chạy kiểm từng bước).
        /// </summary>
        public Task<StepRunResult> HandEyeGoToAlignForTestAsync(int nozzle, CancellationToken ct)
        {
            return StepGuard("Tới vị trí soi CAM dưới", () => HandEyeGoToAlignCore(nozzle, "7", ct), "Xác định hand-eye");
        }

        private async Task<StepRunResult> HandEyeGoToAlignCore(int nozzle, string step, CancellationToken ct)
        {
            var res = new StepRunResult();
            res.Reason = step == "7" ? HandEyeTestNeeds(nozzle, 0) : HandEyeNeeds(nozzle, 3);
            if (res.Reason != null) return res;
            return await StepMoveToAlignCore(nozzle, ct, step).ConfigureAwait(false);
        }

        /// <summary>
        /// Bước 5 — tay đang cầm chính con can vừa gắp và đứng ở vị trí soi bottom-up. App chụp
        /// <c>AlignBottom_ToolN</c> N ảnh với <c>HasTarget = 0</c> để đọc <c>ObjX_mm / ObjY_mm</c> — lượng can lệch
        /// khỏi gốc hệ bottom-up — rồi ghi:
        /// <code>HandEyeDx_mm = X_gắp + ObjX_mm − TopX_mm</code>
        ///
        /// <para>Gắp lệch tâm bao nhiêu không sao: camera dưới đọc đúng lượng lệch đó, và <c>X_gắp + ObjX</c> vẫn là
        /// chỗ tâm con can thật sự nằm. Chỉ chặn khi lệch quá ngưỡng lệch tâm — lúc ấy lần gắp không đại diện cho lần
        /// gắp thật. Thứ KHÔNG được xê dịch là <b>chi tiết đích</b>, và đó là việc của cơ cấu kẹp.</para>
        ///
        /// <para><b>Mốc góc</b> đo cùng lúc, trên cùng con can: <code>mốc góc = ObjA_deg + TopA_deg</code>
        /// Góc đích lúc đặt dựng ngược lại bằng <c>mốc góc − TopA_deg</c>, nên con can lắp đúng đặt lại đúng chỗ cũ
        /// cho góc bù 0. Đây là việc nút Create Reference làm ở các module Single Point Grab/Rectify; Single Point
        /// Alignment của VM 4.4 không có nút đó (Tuấn duyệt 2026-09-22).</para>
        ///
        /// <para><b>CỘNG hai góc, không trừ</b> (sửa 2026-09-23): hai camera nhìn ngược chiều nhau nên đo góc ngược
        /// dấu. Công thức trừ cũ chỉ đúng ở đúng cái góc đem ra đo — ở đó hai công thức cho cùng một số — nên đo một
        /// điểm không phát hiện được, chỉ lộ ra khi chi tiết đích nằm lệch so với lúc đo mốc, sai bằng hai lần độ
        /// lệch đó. Xem <see cref="VisionCalib.HandEye1AngleRef"/>.</para>
        ///
        /// <para><b>Xoay A về đúng encoder lúc gắp trước khi chụp</b> (thêm 2026-09-25). Jog A đi δ độ giữa nút 2 và
        /// nút 5 thì mốc góc lệch đúng δ, và mọi lần đặt của Auto xoay lệch đều δ — lệch mà kiểm tra top-down, phép
        /// thử vòng xoay lẫn phép thử tổng quát đều không thấy, vì nó nằm chung trong lần đo và lần thử. Chỉ
        /// nhìn hàng thật sau khi đặt hay nhìn xuống sau chạy khô mới lộ ra.</para>
        /// </summary>
        public async Task<HandEyeCommitResult> HandEyeCommitAsync(int nozzle, CancellationToken ct)
        {
            var res = new HandEyeCommitResult();
            try
            {
                res.Reason = HandEyeNeeds(nozzle, 3);
                if (res.Reason != null) return res;
                if (!_func.HasProduct(nozzle))
                { res.Reason = Loc.F("Calib.NozzleLostCan", nozzle); return res; }

                var sys = _sys();
                var model = McDataManager.Model;
                if (_motion[AxisId.X] == null) { res.Reason = Loc.T("Calib.CannotReadX"); return res; }

                // X phải ĐANG ở vị trí soi bottom-up, không chỉ "đã bấm nút 4": người bấm có thể jog đi sau đó, và
                // lượng lệch tâm đọc ở chỗ khác là số của chỗ khác.
                double alignX = model.AlignPosX(nozzle), nowX = _motion[AxisId.X].CurrentPos;
                if (Math.Abs(nowX - alignX) > Math.Max(0.001, sys.AlignXTolMm))
                {
                    res.Reason = string.Format(CultureInfo.InvariantCulture, Loc.T("Calib.XNotAtAlignPos4"), nowX, nozzle, alignX);
                    return res;
                }

                // A phải ĐANG ở góc lúc gắp. Khác X, ở đây ra lệnh lại chứ không từ chối: xoay A tại chỗ soi là việc
                // phép thử vòng xoay vẫn làm, và không có ngưỡng nào để cấu hình.
                if (!await MoveZToBottomCaptureAsync(nozzle, ct).ConfigureAwait(false))
                { res.Reason = ZCaptureFail(nozzle); return res; }
                if (!await MoveAsync(AName(nozzle), AxisAngle.Wrap(_he.PickA), ct).ConfigureAwait(false))
                {
                    res.Reason = string.Format(CultureInfo.InvariantCulture, Loc.T("Calib.RotateToPickAngleFailed"),
                        AName(nozzle), _he.PickA);
                    return res;
                }

                int shots = Math.Max(1, sys.CalibShotsPerPoint);
                var m = await CaptureMeanAsync(nozzle, shots, ct).ConfigureAwait(false);
                if (!m.Ok) { res.Reason = m.Reason; return res; }

                double ecc = Math.Sqrt(m.X * m.X + m.Y * m.Y);
                if (ecc > Math.Max(0.1, sys.MaxCompensationDistance))
                {
                    res.Status = VmStatus.NoConclusion;
                    res.Reason = string.Format(CultureInfo.InvariantCulture, Loc.T("Calib.EccTooLarge"),
                        ecc, sys.MaxCompensationDistance);
                    return res;
                }

                // Y camera dưới ngược chiều Y2 của máy — lệch tâm Y quy về chiều máy bằng cách đổi dấu (VmCycle.BottomYSign,
                // Tuấn 2026-09-30). X đã cùng chiều.
                double eccYMachine = VmCycle.BottomYSign * m.Y;
                res.Dx = _he.PickX + m.X - _he.TopX;
                res.Dy = _he.PickY + eccYMachine - _he.TopY;
                res.AngleRef = AxisAngle.Wrap(m.A + _he.TopA);    // ObjA + TopA, về [0, 360)
                if (!VmMath.IsFinite(res.Dx) || !VmMath.IsFinite(res.Dy) || !VmMath.IsFinite(m.A) || !VmMath.IsFinite(_he.TopA))
                { res.Status = VmStatus.Invalid; res.Reason = Loc.F("Calib.HandEyeFail", VmStatus.AlignVerify(VmStatus.Invalid)); return res; }

                res.Status = VmStatus.Ok;
                res.Ok = true;
                var calib = model.Calib;
                calib.SetHandEye(nozzle, res.Dx, res.Dy, res.AngleRef, DateTime.Now);
                calib.SetHandEyeTarget(nozzle, _he.TopX, _he.TopY, _he.TopA, _he.TopAxX, _he.TopAxY);
                res.Note = SaveModelNote();
                if (_heTest != null && _heTest.Nozzle == nozzle) _heTest = null;   // điểm đích cũ tính bằng hand-eye cũ

                LogWorker.Instance.RecordLog(string.Format(CultureInfo.InvariantCulture,
                    "Hand-eye tay #{0}: dx {1:0.000}, dy {2:0.000} mm = gắp ({3:0.000}, {4:0.000}) + lệch tâm theo chiều máy ({5:0.000}, {6:0.000}) − chi tiết ({7:0.000}, {8:0.000}) [camera dưới đo ObjY {12:0.000}, Y ngược chiều máy]; mốc góc {9:0.00}° = góc can {10:0.00}° + góc chi tiết {11:0.00}°",
                    nozzle, res.Dx, res.Dy, _he.PickX, _he.PickY, m.X, eccYMachine, _he.TopX, _he.TopY, res.AngleRef, m.A, _he.TopA, m.Y),
                    Utility.Log.LogLevel.Info, "Calib");
                return res;
            }
            catch (OperationCanceledException)
            {
                res.Reason = Loc.T("Calib.Cancelled");
                return res;
            }
            catch (Exception ex)
            {
                AlarmManager.RaiseEvent(AlarmCode.RoutineCrashed, "Calib", ex.Message);
                res.Reason = Loc.F("Calib.UnexpectedError", ex.Message);
                return res;
            }
        }

        /// <summary>
        /// Bước 8 của <b>phép thử tổng quát</b> — tay đã đứng ở vị trí soi CAM dưới (nút 7), <c>AlignBottom_ToolN</c>
        /// soi con can đang cầm, Group Cmd tính điểm đặt lên pose mà nút 6 vừa đo. Không chạy X (Tuấn tách khỏi bước
        /// tới vị trí soi 26/09, cùng khuôn nút 4 → 5 của khu chạy kiểm từng bước).
        ///
        /// <para>Soi lại chính chi tiết đã gắp thì phải ra đúng encoder lúc gắp, vì <c>Cmd = TopX_mới + Dx − ObjX_mới</c>
        /// và <c>Dx = X_gắp + ObjX − TopX</c>. Giới hạn của phép thử ghi ở <see cref="HandEyeLocateHereAsync"/> — đọc
        /// trước khi dựa vào nó.</para>
        ///
        /// <para><b>KHÔNG chạy tới lệnh</b>: chỉ tính và ghi lại. Nút 9 (<see cref="HandEyeGoToTargetAsync"/>) chạy tới
        /// đúng điểm này.</para>
        ///
        /// <para><b>Tắt hàng rào lưới teach</b>: phép thử không nhắm vào ô nào nên hàng rào "điểm đặt phải nằm trong
        /// lưới đã teach" không có nghĩa ở đây — cùng cách chạy khô làm.</para>
        /// </summary>
        public Task<StepRunResult> HandEyeCheckTargetAsync(int nozzle, CancellationToken ct)
        {
            return StepGuard("Trigger AlignBottom, tính điểm đích", () => HandEyeCheckTargetCore(nozzle, ct), "Xác định hand-eye");
        }

        private async Task<StepRunResult> HandEyeCheckTargetCore(int nozzle, CancellationToken ct)
        {
            var res = new StepRunResult();
            var sys = _sys();
            var names = sys.VisionOutputs;
            res.Reason = VmCycle.AxisVarProblem(names);
            if (res.Reason != null) return res;
            var model = McDataManager.Model;
            if (model == null) { res.Reason = Loc.T("Calib.NoModel"); return res; }
            res.Reason = HandEyeTestNeeds(nozzle, 1);
            if (res.Reason != null) return res;
            if (!_func.HasProduct(nozzle))
            { res.Reason = Loc.F("Calib.NozzleEmptyPickFirst", nozzle); return res; }

            // Không chạy X — nút 7 đưa tay tới vị trí soi (Tuấn tách 26/09). Nhưng X phải ĐANG ở đó: người bấm có thể
            // jog đi sau nút 7, và AlignBottom chụp ở chỗ khác là số của chỗ khác. Cùng phép xét với nút 5.
            if (_motion[AxisId.X] == null) { res.Reason = Loc.T("Calib.CannotReadX"); return res; }
            double alignX = model.AlignPosX(nozzle), nowX = _motion[AxisId.X].CurrentPos;
            if (Math.Abs(nowX - alignX) > Math.Max(0.001, sys.AlignXTolMm))
            {
                res.Reason = string.Format(CultureInfo.InvariantCulture, Loc.T("Calib.XNotAtAlignPos7"), nowX, nozzle, alignX);
                return res;
            }
            if (!await MoveZToBottomCaptureAsync(nozzle, ct).ConfigureAwait(false))
            { res.Reason = ZCaptureFail(nozzle); return res; }

            await _func.WaitSettleAsync(EVmCamera.BottomUp, ct).ConfigureAwait(false);
            int rid = VmRunIds.Next();
            var g = VmCycle.AxisGlobals(_motion, names, rid);
            VmCycle.AddPlaceGlobals(g, new PlacedTopRecord { X = _heTest.HereTopX, Y = _heTest.HereTopY, A = _heTest.HereTopA },
                                    model.Calib, nozzle);
            double aSent = VmCycle.Val(g, nozzle == 1 ? names.GvAxisA1 : names.GvAxisA2);
            var req = VmCycle.AlignBottomRequest(names, nozzle, g, true);
            var r = await CallVmAsync(req, ct).ConfigureAwait(false);
            var read = VmCycle.ReadAlignBottom(r, req, names, nozzle);
            VisionReadout.Tool(nozzle, read, null);
            var trace = VmCycle.TraceAlignBottom(r, req, names, nozzle, Pos(ZName(nozzle)));
            LogWorker.Instance.RecordLog("Phép thử tổng quát tay #" + nozzle + " — " + trace.Line,
                trace.Checked && !trace.Match ? Utility.Log.LogLevel.Warning : Utility.Log.LogLevel.Info, "Calib");
            if (!read.CallOk) { res.Reason = "AlignBottom: " + read.CallError; return res; }
            if (!read.EchoOk) { res.Reason = "AlignBottom: " + read.EchoIssue; return res; }
            if (!read.Found) { res.Reason = Loc.F("Calib.AlignNoCan", nozzle); return res; }
            if (!read.VmValid) { res.Reason = "AlignBottom: " + read.VmIssue; return res; }

            var cmd = VmCycle.CheckPlace(r, req, nozzle, aSent, 0, 0, 0, sys.MaxCompensationAngleDeg,
                sys.MaxCompensationDistance, sys.GetAxis(AxisId.X).InSoftLimit, sys.GetAxis(AxisId.Y2).InSoftLimit, false);
            VisionReadout.Tool(nozzle, read, cmd);
            if (cmd.Route != EVmRoute.Ok) { res.Reason = cmd.Route + ": " + cmd.Why; return res; }

            res.Ok = true;
            res.TargetX = cmd.CmdX;
            res.TargetY2 = cmd.CmdY2;
            res.TargetA = cmd.CmdA;
            _heTest.HasTarget = true;
            _heTest.TargetX = cmd.CmdX;
            _heTest.TargetY = cmd.CmdY2;
            _heTest.TargetA = cmd.CmdA;
            res.Text = string.Format(CultureInfo.InvariantCulture, Loc.T("Calib.HeTarget"),
                cmd.CmdX, model.AssyTray.AxisRow, cmd.CmdY2, AName(nozzle), cmd.CmdA, cmd.DiagDA) +
                HandEyeVsPick(nozzle, cmd.CmdX, cmd.CmdY2, model.AssyTray.AxisRow) +
                Loc.T("Calib.HeTargetHint");
            return res;
        }

        /// <summary>
        /// Bước 9 của phép thử tổng quát (Tuấn yêu cầu 2026-09-26) — chạy X, Y2, A tới đúng điểm đích nút 8 vừa tính,
        /// rồi hạ Z tới <b>điểm giảm tốc</b> của tray lắp ráp và dừng ở đó: con can lơ lửng ngay trên chi tiết, không
        /// chạm, đủ gần để nhìn bằng mắt nó có nằm đúng trên chi tiết không. Không nhả hút; xong thì bấm "Trả con can
        /// về cơ cấu kẹp" như thường.
        ///
        /// <para><b>Chỉ tới điểm giảm tốc, không xuống cao độ đặt</b> (Tuấn chọn): điểm tính sai thì hạ hết là ấn con
        /// can vào chi tiết. Chưa teach điểm giảm tốc thì từ chối, không tự bịa một cao độ nào.</para>
        ///
        /// <para><b>Không chặn theo khoảng cách tới chỗ đã gắp</b>: cơ cấu kẹp gá nhiều sản phẩm, và Tuấn soi sang một
        /// sản phẩm KHÁC ngay sau khi ghi hand-eye (26/09) — điểm đích cách chỗ gắp vài chục mm là đúng.</para>
        ///
        /// <para>Soi lại chính chi tiết đã gắp thì hand-eye sai đều vẫn cho con can nằm đúng trên nó (sai chung triệt
        /// tiêu — xem <see cref="HandEyeLocateHereAsync"/>). Soi sang sản phẩm khác thì cái sai đó hiện ra thành
        /// khoảng lệch giữa con can và chi tiết mới.</para>
        /// </summary>
        public Task<StepRunResult> HandEyeGoToTargetAsync(int nozzle, CancellationToken ct)
        {
            return StepGuard("Tới điểm đích vừa tính", () => HandEyeGoToTargetCore(nozzle, ct), "Xác định hand-eye");
        }

        private async Task<StepRunResult> HandEyeGoToTargetCore(int nozzle, CancellationToken ct)
        {
            var res = new StepRunResult();
            var model = McDataManager.Model;
            if (model == null) { res.Reason = Loc.T("Calib.NoModel"); return res; }
            res.Reason = HandEyeTestNeeds(nozzle, 2);
            if (res.Reason != null) return res;
            if (!_func.HasProduct(nozzle))
            { res.Reason = Loc.F("Calib.NozzleEmptyPickFirst", nozzle); return res; }
            var t = _heTest;

            var tray = model.AssyTray;
            string zName = ZName(nozzle);
            if (!tray.ZSlowValid(zName))
            {
                res.Reason = Loc.F("Calib.NoZSlowTaught", zName);
                return res;
            }
            double zHover = tray.ZSlowFor(zName);

            // Y2 tới điểm đích ở mức Auto thấp như chặng đặt của Auto (Tuấn 01/10).
            if (!await _func.MoveToCellForPickAsync(nozzle, t.TargetA, tray.AxisRow, t.TargetY, t.TargetX, ct, true).ConfigureAwait(false))
            { res.Reason = Loc.F("Calib.MoveToTargetFailed", tray.AxisRow, AName(nozzle)); return res; }
            if (!await MoveAsync(zName, zHover, ct).ConfigureAwait(false))
            { res.Reason = Loc.F("Calib.ZToSlowFailed", zName); return res; }

            res.Ok = true;
            res.TargetX = t.TargetX;
            res.TargetY2 = t.TargetY;
            res.TargetA = t.TargetA;
            res.Text = string.Format(CultureInfo.InvariantCulture, Loc.T("Calib.HeAtTarget"),
                nozzle, t.TargetX, tray.AxisRow, t.TargetY, AName(nozzle), t.TargetA, zName, zHover) +
                HandEyeVsPick(nozzle, t.TargetX, t.TargetY, tray.AxisRow) +
                Loc.T("Calib.HeAtTargetHint");
            return res;
        }

        /// <summary>
        /// Trả con can mẫu về cơ cấu kẹp: về vị trí gắp, hạ Z, nhả hút và thổi, nâng Z.
        ///
        /// <para><b>Xoay A về góc hút</b> cùng lúc chạy X/Y2 — cùng lệnh nút 1 và 2 dùng. Con can được nhấc lên ở góc
        /// đó; ai jog A trong lúc thử mà hạ xuống ở góc khác thì con can không ngồi vào chi tiết, đầu hút ấn nó
        /// xuống.</para>
        ///
        /// <para><b>Không đòi bước nào trước đó</b>, để bỏ dở ở đâu cũng gọi được — chỉ đòi tay đang cầm hàng và
        /// vị trí gắp đã dạy (không biết chỗ gắp thì không có chỗ nào để trả về).</para>
        ///
        /// <para>Xoá sổ <b>chỉ khi</b> cảm biến thật sự nhả. Còn báo có hàng thì nói cho người vận hành, KHÔNG phát
        /// alarm: đây là trang hiệu chuẩn, người đang đứng ngay đó; Auto mới phải dừng máy.</para>
        /// </summary>
        public Task<StepRunResult> HandEyeReturnAsync(int nozzle, CancellationToken ct)
        {
            return StepGuard("Trả con can về cơ cấu kẹp", () => HandEyeReturnCore(nozzle, ct), "Xác định hand-eye");
        }

        private async Task<StepRunResult> HandEyeReturnCore(int nozzle, CancellationToken ct)
        {
            var res = new StepRunResult();
            var model = McDataManager.Model;
            if (model == null) { res.Reason = Loc.T("Calib.NoModel"); return res; }
            if (!_func.HasProduct(nozzle))
            { res.Reason = Loc.F("Calib.NozzleHoldsNothing", nozzle); return res; }
            res.Reason = model.HandEyeJigProblem(nozzle);
            if (res.Reason != null) return res;

            var tray = model.AssyTray;
            var pick = model.HandEyePick(nozzle);
            string zName = ZName(nozzle);

            if (!await _func.EnsureZSafeAsync(ct).ConfigureAwait(false))
            { res.Reason = Loc.T("Calib.ZSafeFailed"); return res; }
            if (!await _func.MoveToCellForPickAsync(nozzle, model.PickAngle(nozzle), tray.AxisRow, pick.Y, pick.X, ct).ConfigureAwait(false))
            { res.Reason = Loc.T("Calib.GoToJigPickFailed"); return res; }
            if (!await _func.MoveZDownAsync(zName, tray.ZPickFor(zName), tray.ZSlowFor(zName), ct).ConfigureAwait(false))
            { res.Reason = Loc.F("Calib.ZDownPlaceFailed", zName); return res; }

            _func.SetVacuum(nozzle, false);
            _func.SetBlow(nozzle, true);
            bool released = await WaitVacuumAsync(nozzle, false, ct).ConfigureAwait(false);
            await Task.Delay(_sys().BlowTimeMs, ct).ConfigureAwait(false);
            _func.SetBlow(nozzle, false);

            if (!await _func.MoveZUpAsync(zName, _motion.ZSafe, tray.ZSlowFor(zName), ct).ConfigureAwait(false))
            { res.Reason = Loc.F("Calib.ZUpAfterReturnFailed", zName); return res; }

            if (!released)
            {
                // Dừng TRƯỚC khi ghi sổ: ghi tay hút thành trống trong lúc con can còn dính trên đầu hút là để lần
                // hạ sau đưa một tay "trống" đang cầm hàng xuống chỗ đã có hàng.
                res.Reason = Loc.T("Calib.StillHolding");
                return res;
            }

            var run = McDataManager.RunData;
            var nz = nozzle == 1 ? run.Nozzle1 : run.Nozzle2;
            nz.HasProduct = false;
            nz.SourceCellIndex = -1;
            nz.Aligned = false;
            McDataManager.SaveRunningData();
            if (_he != null && _he.Nozzle == nozzle) _he = null;
            if (_heTest != null && _heTest.Nozzle == nozzle) _heTest = null;

            res.Ok = true;
            res.Text = string.Format(CultureInfo.InvariantCulture, Loc.T("Calib.Returned"),
                nozzle, pick.X, tray.AxisRow, pick.Y, AName(nozzle), model.PickAngle(nozzle), tray.ZPickFor(zName));
            return res;
        }

        /// <summary>
        /// Bước 5b — chạy khô với sản phẩm mẫu đang trên tay: <c>LocateTop</c> soi dấu chuẩn và coi dấu là ô đích; tay tới
        /// vị trí soi, <c>AlignBottom_ToolN</c> soi sản phẩm rồi Group Cmd tính lệnh đặt lên dấu trong cùng lần chạy; App
        /// đưa tay tới lệnh ở Z an toàn. Tâm sản phẩm phải nằm trên dấu chuẩn. Không hạ Z. Đây là phép duy nhất bắt được
        /// Group Cmd nối sai hoặc hand-eye đo sai ở bước xác định hand-eye.
        /// </summary>
        public async Task<DryRunPlaceResult> DryRunPlaceAlignAsync(int nozzle, CancellationToken ct)
        {
            var res = new DryRunPlaceResult();
            try
            {
                var sys = _sys();
                var names = sys.VisionOutputs;
                var model = McDataManager.Model;
                res.Reason = VmCycle.AxisVarProblem(names);
                if (res.Reason != null) return res;

                CheckCalib();   // cổng Auto — cập nhật dòng trạng thái của trang
                var gate = VmCycle.EvaluateCalib(model, sys, nozzle);
                if (gate.Block != null) { res.Reason = Loc.F("Calib.CalibNotReady", nozzle, gate.Block); return res; }

                await _func.WaitSettleAsync(EVmCamera.TopDown, ct).ConfigureAwait(false);
                int rid = VmRunIds.Next();
                var g = VmCycle.AxisGlobals(_motion, names, rid);
                var markCall =await CallVmAsync(VmCycle.LocateRequest(names, g), ct).ConfigureAwait(false);
                var mark = VmCycle.ReadLocate(markCall, g, names);
                VisionReadout.TopDown(mark);
                if (!mark.CallOk) { res.Reason = "LocateTop: " + mark.CallError; return res; }
                if (!mark.EchoOk) { res.Reason = "LocateTop: " + mark.EchoIssue; return res; }
                if (!mark.Found) { res.Reason = Loc.T("Calib.LocateNoMark"); return res; }
                if (!mark.VmValid) { res.Reason = "LocateTop: " + mark.VmIssue; return res; }
                res.CamX = mark.X;
                res.CamY = mark.Y;

                if (!await _func.EnsureZSafeAsync(ct).ConfigureAwait(false))
                { res.Reason = Loc.T("Calib.ZSafeFailed"); return res; }
                if (!await MoveZToBottomCaptureAsync(nozzle, ct).ConfigureAwait(false))
                { res.Reason = ZCaptureFail(nozzle); return res; }
                if (!await MoveAsync(AxisId.X, model.AlignPosX(nozzle), ct).ConfigureAwait(false))
                { res.Reason = Loc.F("Calib.GoToAlignPosFailed", nozzle); return res; }

                await _func.WaitSettleAsync(EVmCamera.BottomUp, ct).ConfigureAwait(false);
                int rid2 = VmRunIds.Next();
                var g2 = VmCycle.AxisGlobals(_motion, names, rid2);
                VmCycle.AddPlaceGlobals(g2, new PlacedTopRecord { X = mark.X, Y = mark.Y, A = mark.A }, model.Calib, nozzle);
                double aSent = VmCycle.Val(g2, nozzle == 1 ? names.GvAxisA1 : names.GvAxisA2);
                var req = VmCycle.AlignBottomRequest(names, nozzle, g2, true);
                var r = await CallVmAsync(req, ct).ConfigureAwait(false);
                var read = VmCycle.ReadAlignBottom(r, req, names, nozzle);
                VisionReadout.Tool(nozzle, read, null);
                var trace = VmCycle.TraceAlignBottom(r, req, names, nozzle, Pos(ZName(nozzle)));
                LogWorker.Instance.RecordLog("Chạy khô tay #" + nozzle + " — " + trace.Line,
                    trace.Checked && !trace.Match ? Utility.Log.LogLevel.Warning : Utility.Log.LogLevel.Info, "Calib");
                res.CmdCheck = trace.Check;
                if (!read.CallOk) { res.Reason = "AlignBottom: " + read.CallError; return res; }
                if (!read.EchoOk) { res.Reason = "AlignBottom: " + read.EchoIssue; return res; }
                if (!read.Found) { res.Reason = Loc.F("Calib.AlignNoProduct", nozzle); return res; }
                if (!read.VmValid) { res.Reason = "AlignBottom: " + read.VmIssue; return res; }

                var cmd = VmCycle.CheckPlace(r, req, nozzle, aSent, 0, 0, 0, sys.MaxCompensationAngleDeg, sys.MaxCompensationDistance,
                    sys.GetAxis(AxisId.X).InSoftLimit, sys.GetAxis(AxisId.Y2).InSoftLimit, false);
                VisionReadout.Tool(nozzle, read, cmd);

                // Ba góc làm nên góc bù — đi kèm mọi kết cục, cả trên bảng báo lẫn trong log (Tuấn gặp 2026-09-22: góc bù
                // 179.81° mà bảng chỉ có một con số, không biết góc nào lệch).
                string angles = string.Format(CultureInfo.InvariantCulture, Loc.T("Calib.DryRunAngles"),
                    VmCycle.Val(g2, VmVars.CellA), mark.A, model.Calib.HandEyeAngleRef(nozzle), read.A, AName(nozzle), aSent);
                LogWorker.Instance.RecordLog(string.Format(CultureInfo.InvariantCulture,
                    "Chạy khô tay #{0}: dấu chuẩn ({1:0.000}, {2:0.000}); sản phẩm lệch tâm xoay ({3:0.000}, {4:0.000}); {5}; Group Cmd Status {6}, góc bù {7:+0.00;-0.00}°, lệnh X {8:0.000}, Y2 {9:0.000}, A {10:0.00} — {11}",
                    nozzle, mark.X, mark.Y, read.X, read.Y, angles, cmd.Status, cmd.DiagDA, cmd.CmdX, cmd.CmdY2, cmd.CmdA,
                    cmd.Route == EVmRoute.Ok ? "chạy tới lệnh" : cmd.Route + ": " + cmd.Why),
                    cmd.Route == EVmRoute.Ok ? Utility.Log.LogLevel.Info : Utility.Log.LogLevel.Warning, "Calib");
                if (cmd.Route != EVmRoute.Ok)
                {
                    res.Reason = cmd.Route + ": " + cmd.Why + "\n" + angles + (trace.Check == null ? "" : "\n" + trace.Check);
                    return res;
                }

                res.CmdX = cmd.CmdX;
                res.CmdY2 = cmd.CmdY2;
                res.CmdA = cmd.CmdA;

                string aName = nozzle == 1 ? AxisId.A1 : AxisId.A2;
                if (!await MoveAsync(aName, AxisAngle.Wrap(cmd.CmdA), ct).ConfigureAwait(false))
                { res.Reason = Loc.F("Calib.RotateAxisToCmdFailed", aName); return res; }
                if (!await _func.MoveToCellAsync(AxisId.Y2, cmd.CmdY2, cmd.CmdX, ct, true).ConfigureAwait(false))
                { res.Reason = Loc.F("Calib.MoveXYToCmdFailed", AxisId.Y2); return res; }

                res.Ok = true;
                LogWorker.Instance.RecordLog(string.Format(CultureInfo.InvariantCulture,
                    "Chạy khô đặt tay #{0}: dấu ({1:0.000}, {2:0.000}, {6:0.00}°); sản phẩm lệch tâm xoay ({7:0.000}, {8:0.000}), góc {9:0.00}°, chụp ở X {10:0.000}, {11} {12:0.000}°, {13} {14:0.000}; góc bù {15:+0.00;-0.00}° → lệnh X {3:0.000}, Y2 {4:0.000}, A {5:0.00}",
                    nozzle, res.CamX, res.CamY, res.CmdX, res.CmdY2, res.CmdA, mark.A, read.X, read.Y, read.A,
                    VmCycle.Val(g2, names.GvAxisX), AName(nozzle), aSent, ZName(nozzle), Pos(ZName(nozzle)), cmd.DiagDA),
                    Utility.Log.LogLevel.Info, "Calib");
                return res;
            }
            catch (OperationCanceledException)
            {
                res.Reason = Loc.T("Calib.Cancelled");
                return res;
            }
            catch (Exception ex)
            {
                AlarmManager.RaiseEvent(AlarmCode.RoutineCrashed, "Calib", ex.Message);
                res.Reason = Loc.F("Calib.UnexpectedError", ex.Message);
                return res;
            }
        }

        // ───────────────────────── Chạy kiểm từng bước (trang Vision Edit) ─────────────────────────
        //
        // Sáu nút chạy đúng trình tự của một vòng Auto — S05 (soi ô) → S1x (hút) → S20 (soi sản phẩm, tính lệnh)
        // → S3x (tới điểm đặt) — nhưng mỗi bước một nút, dừng lại giữa các bước để đọc số. KHÔNG hạ Z và KHÔNG
        // nhả hút ở bước cuối: chỗ nguy hiểm nhất của chu trình là lần hạ xuống đặt, và mọi sai lệch cần nhìn đều
        // đọc được ở trên cao rồi (Tuấn yêu cầu 2026-09-14).
        //
        // Phép tính đi đúng đường của Auto, không có đường tắt nào: cùng AddPlaceGlobals (kèm bù chung + bù riêng của ô),
        // cùng AlignBottomRequest, cùng CheckPlace kể cả hàng rào lưới teach — chạy kiểm mà đi đường khác thì
        // không kết luận được gì về đường chạy thật.

        private PlacedTopRecord _stepTop;
        private int _stepRow = -1, _stepCol = -1;
        private VmPlaceCommand _stepCmd;
        private int _stepCmdNozzle;
        private double _stepCmdACap;

        /// <summary>Đang cất số LocateTop của một ô (bước 2 đã chạy) — trang Vision Edit dùng để bật/tắt nút.</summary>
        public bool StepHasTop { get { return _stepTop != null; } }
        public bool StepHasCommand { get { return _stepCmd != null; } }

        /// <summary>Bước 5 vừa cất một lệnh nằm NGOÀI hàng rào lưới teach (chạy Auto thì bỏ ô) — trang Vision Edit
        /// hỏi lại trước khi bước 6 chạy tới đó. Rỗng nghĩa là lệnh nằm trong hàng rào.</summary>
        public string StepGuardWarn { get; private set; }

        /// <summary>Bỏ số đo đang cất — camera đổi chỗ, đổi ô, hoặc người bấm muốn làm lại từ bước 1.</summary>
        public void StepClear()
        {
            _stepTop = null;
            _stepRow = -1;
            _stepCol = -1;
            _stepCmd = null;
            _stepCmdNozzle = 0;
            StepGuardWarn = null;
        }

        /// <summary>Bước 1 — CAM top-down tới đúng vị trí soi ô (hàng, cột) của tray lắp ráp, y như S05 lúc chạy Auto.</summary>
        public Task<StepRunResult> StepMoveCamToCellAsync(int row, int col, CancellationToken ct)
        {
            return StepGuard("CAM tới ô lắp ráp", () => StepMoveCamCore(row, col, ct));
        }

        /// <summary>Bước 2 — kích LocateTop tại chỗ và cất TopX/TopY/TopA làm pose ô đích cho bước 5.</summary>
        public Task<StepRunResult> StepLocateTopAsync(int row, int col, CancellationToken ct)
        {
            return StepGuard("Trigger LocateTop", () => StepLocateCore(row, col, ct));
        }

        /// <summary>Bước 3 — tay hút tới ô tray đầu vào, hạ, hút, nâng. Ghi sổ tay đang cầm hàng đúng như S1x.</summary>
        public Task<StepRunResult> StepPickAsync(int nozzle, int row, int col, CancellationToken ct)
        {
            return StepGuard("Hút hàng", () => StepPickCore(nozzle, row, col, ct));
        }

        /// <summary>Bước 4 — chạy X tới vị trí soi CAM bottom-up của tay đang chọn.</summary>
        public Task<StepRunResult> StepMoveToAlignAsync(int nozzle, CancellationToken ct)
        {
            return StepGuard("Tới vị trí soi CAM dưới", () => StepMoveToAlignCore(nozzle, ct));
        }

        /// <summary>Bước 5 — AlignBottom_ToolN: soi sản phẩm trên tay, Group Cmd tính lệnh đặt vào ô đã soi ở bước 2.</summary>
        public Task<StepRunResult> StepAlignBottomAsync(int nozzle, CancellationToken ct)
        {
            return StepGuard("Trigger AlignBottom", () => StepAlignCore(nozzle, ct));
        }

        /// <summary>
        /// Nút "Z1 → Z_SAFE" / "Z2 → Z_SAFE" ở thanh Jog. Trước 2026-09-23 hai nút này chạy VỀ GỐC trục Z;
        /// Tuấn đổi sang lệnh chạy tới Z_SAFE — giống nút "Về gốc" của trang Teaching, tức một lệnh Move tới
        /// toạ độ chứ không phải chu trình về gốc. Origin đã về gốc Z rồi, nên nút về gốc lẻ ở trang hiệu
        /// chuẩn chỉ tổ bấm nhầm giữa chừng. Đổi lại: nút này đòi đã về gốc (cổng OrgOk của trang).
        /// </summary>
        public Task<StepRunResult> StepMoveZSafeAsync(int nozzle, CancellationToken ct)
        {
            return StepGuard(ZName(nozzle) + " về Z an toàn", () => StepMoveZSafeCore(nozzle, ct));
        }

        private async Task<StepRunResult> StepMoveZSafeCore(int nozzle, CancellationToken ct)
        {
            var res = new StepRunResult();
            if (!await MoveZToBottomCaptureAsync(nozzle, ct).ConfigureAwait(false))
            { res.Reason = Loc.F("Calib.ZSafeNotReached", ZName(nozzle)); return res; }

            res.Ok = true;
            res.Text = string.Format(CultureInfo.InvariantCulture, Loc.T("Calib.ZAtSafe"),
                ZName(nozzle), Pos(ZName(nozzle)));
            return res;
        }

        /// <summary>Bước 6 — chạy A, X, Y tới lệnh vừa tính, giữ nguyên Z an toàn (không hạ, không nhả).</summary>
        public Task<StepRunResult> StepMoveToCommandAsync(int nozzle, CancellationToken ct)
        {
            return StepGuard("Tới điểm đặt", () => StepMoveToCommandCore(nozzle, ct));
        }

        private async Task<StepRunResult> StepMoveCamCore(int row, int col, CancellationToken ct)
        {
            var res = new StepRunResult();
            var model = McDataManager.Model;
            if (model == null) { res.Reason = Loc.T("Calib.NoModel"); return res; }
            var tray = model.AssyTray;
            if (row < 0 || row >= tray.Rows || col < 0 || col >= tray.Cols)
            { res.Reason = Loc.F("Calib.CellOutsideAssyGrid", tray.Rows, tray.Cols); return res; }
            if (!tray.CamTopDownAnchorTaught)
            { res.Reason = Loc.T("Calib.AssyAnchorNotTaught"); return res; }

            // Cùng phép tính với S05 — gọi chung TrayLayout.CamPosFor để không có bản sao nào lệch đi.
            var cam = tray.CamPosFor(row, col);
            double camX = cam.X, camY = cam.Y;

            if (!await _func.MoveToCellAsync(tray.AxisRow, camY, camX, ct).ConfigureAwait(false))
            { res.Reason = Loc.T("Calib.GoToCellCamFailed"); return res; }

            // Camera đổi chỗ thì số LocateTop cũ là của ô khác — bỏ luôn, kèm lệnh đã tính từ nó.
            StepClear();
            res.Ok = true;
            res.Text = string.Format(CultureInfo.InvariantCulture,
                Loc.T("Calib.StepCamAtCell"), row + 1, col + 1, camX, tray.AxisRow, camY);
            return res;
        }

        private async Task<StepRunResult> StepLocateCore(int row, int col, CancellationToken ct)
        {
            var res = new StepRunResult();
            var names = _sys().VisionOutputs;
            res.Reason = VmCycle.AxisVarProblem(names);
            if (res.Reason != null) return res;
            var model = McDataManager.Model;
            if (model == null) { res.Reason = Loc.T("Calib.NoModel"); return res; }
            var tray = model.AssyTray;
            if (row < 0 || row >= tray.Rows || col < 0 || col >= tray.Cols)
            { res.Reason = Loc.F("Calib.CellOutsideAssyGrid", tray.Rows, tray.Cols); return res; }

            await _func.WaitSettleAsync(EVmCamera.TopDown, ct).ConfigureAwait(false);
            int rid = VmRunIds.Next();
            var g = VmCycle.AxisGlobals(_motion, names, rid);
            var call = await CallVmAsync(VmCycle.LocateRequest(names, g), ct).ConfigureAwait(false);
            var read = VmCycle.ReadLocate(call, g, names);
            VisionReadout.TopDown(read, row, col);
            if (!read.CallOk) { res.Reason = "LocateTop: " + read.CallError; return res; }
            if (!read.EchoOk) { res.Reason = "LocateTop: " + read.EchoIssue; return res; }
            if (!read.Found) { res.Reason = Loc.T("Calib.LocateNoTargetInCell"); return res; }
            if (!read.VmValid) { res.Reason = "LocateTop: " + read.VmIssue; return res; }

            _stepTop = new PlacedTopRecord { X = read.X, Y = read.Y, A = read.A };
            _stepRow = row;
            _stepCol = col;
            _stepCmd = null;
            res.Ok = true;
            res.Text = string.Format(CultureInfo.InvariantCulture, Loc.T("Calib.StepLocated"),
                row + 1, col + 1, read.X, read.Y, read.A);
            return res;
        }

        private async Task<StepRunResult> StepPickCore(int nozzle, int row, int col, CancellationToken ct)
        {
            var res = new StepRunResult();
            var model = McDataManager.Model;
            if (model == null) { res.Reason = Loc.T("Calib.NoModel"); return res; }
            var tray = model.InputTray;
            if (row < 0 || row >= tray.Rows || col < 0 || col >= tray.Cols)
            { res.Reason = Loc.F("Calib.CellOutsideInputGrid", tray.Rows, tray.Cols); return res; }
            if (_func.HasProduct(nozzle))
            { res.Reason = Loc.F("Calib.NozzleBusyPick", nozzle); return res; }

            string zName = nozzle == 1 ? AxisId.Z1 : AxisId.Z2;
            var cell = tray.CellPos(zName, row, col);
            if (!await _func.MoveToCellForPickAsync(nozzle, model.PickAngle(nozzle), tray.AxisRow, cell.Y, cell.X, ct).ConfigureAwait(false))
            { res.Reason = Loc.T("Calib.GoToPickCellFailed"); return res; }
            if (!await _func.MoveZDownAsync(zName, tray.ZPickFor(zName), tray.ZSlowFor(zName), ct).ConfigureAwait(false))
            { res.Reason = Loc.F("Calib.ZDownPickFailed", zName); return res; }

            _func.SetVacuum(nozzle, true);
            bool gripped = await WaitVacuumAsync(nozzle, true, ct).ConfigureAwait(false);
            await SettleVacuumAsync(gripped, ct).ConfigureAwait(false);

            if (!await _func.MoveZUpAsync(zName, _motion.ZSafe, tray.ZSlowFor(zName), ct).ConfigureAwait(false))
            { res.Reason = Loc.F("Calib.ZUpFailed", zName); return res; }
            if (!gripped)
            {
                _func.SetVacuum(nozzle, false);
                res.Reason = Loc.T("Calib.PickMissed");
                return res;
            }

            // Ghi sổ đúng như S1x. Không ghi thì lần START sau ResumeIntegrityOk thấy cảm biến khác phần mềm,
            // phát alarm và bắt RESET — sản phẩm do chạy kiểm để lại trên tay là trạng thái thật của máy.
            var run = McDataManager.RunData;
            var nz = nozzle == 1 ? run.Nozzle1 : run.Nozzle2;
            int index = CellIndexOf(run.InputTray, row, col);
            nz.HasProduct = true;
            nz.Aligned = false;
            nz.SourceCellIndex = index;
            if (index >= 0) run.InputTray.Set(index, ECellState.Processing);
            McDataManager.SaveRunningData();

            res.Ok = true;
            res.Text = string.Format(CultureInfo.InvariantCulture, Loc.T("Calib.StepPicked"),
                nozzle, row + 1, col + 1, cell.X, tray.AxisRow, cell.Y, tray.ZPickFor(zName));
            return res;
        }

        private async Task<StepRunResult> StepMoveToAlignCore(int nozzle, CancellationToken ct, string step = "4")
        {
            var res = new StepRunResult();
            var model = McDataManager.Model;
            if (model == null) { res.Reason = Loc.T("Calib.NoModel"); return res; }

            double alignX = model.AlignPosX(nozzle);
            if (!await _func.EnsureZSafeAsync(ct).ConfigureAwait(false))
            { res.Reason = Loc.T("Calib.ZSafeFailed"); return res; }
            if (!await MoveZToBottomCaptureAsync(nozzle, ct).ConfigureAwait(false))
            { res.Reason = ZCaptureFail(nozzle); return res; }
            if (!await MoveAsync(AxisId.X, alignX, ct).ConfigureAwait(false))
            { res.Reason = Loc.F("Calib.GoToAlignPosFailed", nozzle); return res; }

            res.Ok = true;
            res.Text = string.Format(CultureInfo.InvariantCulture,
                Loc.T("Calib.StepAtAlign"), nozzle, alignX,
                nozzle == 1 ? AxisId.Z1 : AxisId.Z2, _motion.ZSafe, step);
            return res;
        }

        private async Task<StepRunResult> StepAlignCore(int nozzle, CancellationToken ct)
        {
            var res = new StepRunResult();
            var sys = _sys();
            var names = sys.VisionOutputs;
            res.Reason = VmCycle.AxisVarProblem(names);
            if (res.Reason != null) return res;
            var model = McDataManager.Model;
            if (model == null) { res.Reason = Loc.T("Calib.NoModel"); return res; }
            if (_stepTop == null) { res.Reason = Loc.T("Calib.NeedTopPose"); return res; }

            string zAxis = nozzle == 1 ? AxisId.Z1 : AxisId.Z2;
            var trim = model.AssyTray.PlaceTrim(zAxis, _stepRow, _stepCol);

            if (!await MoveZToBottomCaptureAsync(nozzle, ct).ConfigureAwait(false))
            { res.Reason = ZCaptureFail(nozzle); return res; }
            await _func.WaitSettleAsync(EVmCamera.BottomUp, ct).ConfigureAwait(false);
            int rid = VmRunIds.Next();
            var g = VmCycle.AxisGlobals(_motion, names, rid);
            VmCycle.AddPlaceGlobals(g, _stepTop, model.Calib, nozzle, trim);
            double aSent = VmCycle.Val(g, nozzle == 1 ? names.GvAxisA1 : names.GvAxisA2);
            var req = VmCycle.AlignBottomRequest(names, nozzle, g, true);
            var r = await CallVmAsync(req, ct).ConfigureAwait(false);
            var read = VmCycle.ReadAlignBottom(r, req, names, nozzle);
            VisionReadout.Tool(nozzle, read, null, _stepRow, _stepCol);
            var trace = VmCycle.TraceAlignBottom(r, req, names, nozzle, Pos(zAxis));
            LogWorker.Instance.RecordLog("Chạy kiểm từng bước — 5. " + trace.Line,
                trace.Checked && !trace.Match ? Utility.Log.LogLevel.Warning : Utility.Log.LogLevel.Info, "Calib");
            if (!read.CallOk) { res.Reason = "AlignBottom: " + read.CallError; return res; }
            if (!read.EchoOk) { res.Reason = "AlignBottom: " + read.EchoIssue; return res; }
            if (!read.Found) { res.Reason = Loc.F("Calib.AlignNoProduct", nozzle); return res; }
            if (!read.VmValid) { res.Reason = "AlignBottom: " + read.VmIssue; return res; }

            var cellPos = model.AssyTray.CellPos(zAxis, _stepRow, _stepCol);
            double guardX = cellPos.X + (trim == null ? 0 : trim.PlaceDx);
            double guardY = cellPos.Y + (trim == null ? 0 : trim.PlaceDy);
            string yName = model.AssyTray.AxisRow;
            var cmd = VmCycle.CheckPlace(r, req, nozzle, aSent, guardX, guardY, sys.PlaceTargetGuardMm,
                sys.MaxCompensationAngleDeg, sys.MaxCompensationDistance,
                sys.GetAxis(AxisId.X).InSoftLimit, sys.GetAxis(yName).InSoftLimit, true);
            VisionReadout.Tool(nozzle, read, cmd, _stepRow, _stepCol);

            _stepCmd = null;
            StepGuardWarn = null;
            if (cmd.Route != EVmRoute.Ok && cmd.Route != EVmRoute.Skip)
            { res.Reason = cmd.Why + (trace.Check == null ? "" : "\n" + trace.Check); return res; }

            // Nhánh lệch lưới (G) dừng CheckPlace ngay ở phép chặn (f), chưa tới phép soft limit (g) — mà bước 6
            // vẫn cho chạy tới lệnh đó nếu người bấm xác nhận, nên phải tự kiểm ở đây. Không có chỗ nào khác.
            if (cmd.Route == EVmRoute.Skip)
            {
                if (!sys.GetAxis(AxisId.X).InSoftLimit(cmd.CmdX) || !sys.GetAxis(yName).InSoftLimit(cmd.CmdY2))
                { res.Reason = Loc.F("Calib.GuardOutsideSoftLimit", cmd.Why); return res; }
                res.GuardWarn = cmd.Why;
                StepGuardWarn = cmd.Why;
            }

            _stepCmd = cmd;
            _stepCmdNozzle = nozzle;
            _stepCmdACap = aSent;
            res.Ok = true;
            res.HasCommand = true;
            res.Text = string.Format(CultureInfo.InvariantCulture, Loc.T("Calib.StepAligned"),
                nozzle, read.X, read.Y, read.A, cmd.CmdX, yName, cmd.CmdY2, cmd.CmdA, cmd.DiagDA, cmd.DiagEcc,
                cmd.DiagTarX, cmd.DiagTarY) + (trace.Check == null ? "" : "\n" + trace.Check);
            return res;
        }

        private async Task<StepRunResult> StepMoveToCommandCore(int nozzle, CancellationToken ct)
        {
            var res = new StepRunResult();
            var model = McDataManager.Model;
            if (model == null) { res.Reason = Loc.T("Calib.NoModel"); return res; }
            if (_stepCmd == null || _stepCmdNozzle != nozzle)
            { res.Reason = Loc.F("Calib.NeedCommand", nozzle); return res; }

            // CmdA là góc TUYỆT ĐỐI tính từ góc lúc chụp: trục A xoay sau lúc chụp là lệnh hết đúng (nhánh R của S3x).
            string aName = nozzle == 1 ? AxisId.A1 : AxisId.A2;
            double aNow = _motion[aName] == null ? double.NaN : _motion[aName].CurrentPos;
            if (!VmMath.IsFinite(aNow) || AxisAngle.Distance(aNow, _stepCmdACap) > _sys().ACapTolDeg)
            {
                res.Reason = string.Format(CultureInfo.InvariantCulture, Loc.T("Calib.ARotatedSinceCapture"),
                    aName, _stepCmdACap, aNow);
                return res;
            }

            string yName = model.AssyTray.AxisRow;
            if (!await _func.EnsureZSafeAsync(ct).ConfigureAwait(false))
            { res.Reason = Loc.T("Calib.ZSafeFailed"); return res; }
            if (!await MoveAsync(aName, AxisAngle.Wrap(_stepCmd.CmdA), ct).ConfigureAwait(false))
            { res.Reason = Loc.F("Calib.RotateAxisToCmdFailed", aName); return res; }
            if (!await _func.MoveToCellAsync(yName, _stepCmd.CmdY2, _stepCmd.CmdX, ct, true).ConfigureAwait(false))
            { res.Reason = Loc.F("Calib.MoveXYToCmdFailed", yName); return res; }

            // Trục A vừa bị CHÍNH bước này xoay tới lệnh; không dời mốc thì bấm lại bước 6 lần nữa là tự mình
            // chặn mình. Lệnh góc là góc tuyệt đối nên chạy lại đúng lệnh đó không đổi kết quả; jog tay sau đó
            // vẫn bị bắt như cũ.
            _stepCmdACap = AxisAngle.Wrap(_stepCmd.CmdA);

            res.Ok = true;
            res.Text = string.Format(CultureInfo.InvariantCulture, Loc.T("Calib.StepAtCommand"),
                nozzle, _motion[AxisId.X].CurrentPos, yName, _motion[yName].CurrentPos, aName, _motion[aName].CurrentPos);
            return res;
        }

        // ───────────────────────── tiện ích ─────────────────────────

        /// <summary>Vỏ chung của các nút chạy từng bước: bắt lỗi như mọi routine khác, và ghi log MỘT dòng cho mỗi
        /// lần bấm — chuỗi log đó chính là biên bản của lần chạy. <paramref name="logTag"/> để hai khu nút (chạy kiểm
        /// cả chu trình, và xác định hand-eye) đọc tách được nhau trong log.</summary>
        private async Task<StepRunResult> StepGuard(string what, Func<Task<StepRunResult>> body,
                                                    string logTag = "Chạy kiểm từng bước")
        {
            StepRunResult res;
            try
            {
                res = await body().ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
                res = new StepRunResult { Reason = Loc.T("Calib.Cancelled") };
            }
            catch (Exception ex)
            {
                AlarmManager.RaiseEvent(AlarmCode.RoutineCrashed, "Calib", ex.Message);
                res = new StepRunResult { Reason = Loc.F("Calib.UnexpectedError", ex.Message) };
            }

            LogWorker.Instance.RecordLog(logTag + " — " + (res.Ok ? res.Text : what + " dừng: " + res.Reason),
                res.Ok ? Utility.Log.LogLevel.Info : Utility.Log.LogLevel.Warning, "Calib");
            return res;
        }

        /// <summary>
        /// Chờ lực hút lên đủ sau khi cảm biến đã báo hút được, rồi mới cho nâng Z
        /// (<see cref="SystemData.VacuumSettleMs"/>). Hút hụt thì không chờ — đằng nào cũng đi thử lại.
        /// </summary>
        private async Task SettleVacuumAsync(bool gripped, CancellationToken ct)
        {
            int ms = _sys().VacuumSettleMs;
            if (gripped && ms > 0) await Task.Delay(ms, ct).ConfigureAwait(false);
        }

        /// <summary>Chờ cảm biến chân không đạt trạng thái mong muốn — bản dùng cho bước hút của khu chạy kiểm.</summary>
        private async Task<bool> WaitVacuumAsync(int nozzle, bool want, CancellationToken ct)
        {
            int limit = _sys().VacuumTimeoutMs, waited = 0;
            while (waited < limit)
            {
                if (_func.HasProduct(nozzle) == want) return true;
                await Task.Delay(10, ct).ConfigureAwait(false);
                waited += 10;
            }
            return _func.HasProduct(nozzle) == want;
        }

        /// <summary>Số thứ tự ô trong tray theo (hàng, cột) — phải dò vì thứ tự quét có thể là hình rắn.</summary>
        private static int CellIndexOf(TrayState tray, int row, int col)
        {
            if (tray == null) return -1;
            for (int i = 0; i < tray.CellCount; i++)
            {
                int r, c;
                tray.RowCol(i, out r, out c);
                if (r == row && c == col) return i;
            }
            return -1;
        }

        private async Task<bool> MoveAsync(string axisName, double pos, CancellationToken ct)
        {
            var cfg = _sys().GetAxis(axisName);
            return await _func.MoveAsync(axisName, pos, cfg.MoveSpeed, ct).ConfigureAwait(false);
        }

        /// <summary>
        /// Cao độ soi CAM bottom-up: Z của tay đang soi về đúng Z_SAFE — đúng chỗ chu trình Auto nâng Z lên
        /// sau khi hút rồi soi (<c>Routine_Auto.SafeZ</c>). Mọi ảnh bottom-up, từ nạp điểm hiệu chuẩn tới chạy thật, phải
        /// chụp ở cùng một cao độ: đổi khoảng cách tới camera là đổi thang mm/pixel và tâm xoay VisionMaster đã học.
        /// Trước đây các bước hiệu chuẩn chụp ở chỗ Z đang đứng (Tuấn chốt 2026-09-22 dùng Z_SAFE, không thêm ô cài riêng).
        /// </summary>
        private Task<bool> MoveZToBottomCaptureAsync(int nozzle, CancellationToken ct)
        {
            return MoveAsync(nozzle == 1 ? AxisId.Z1 : AxisId.Z2, _motion.ZSafe, ct);
        }

        private static string ZCaptureFail(int nozzle)
        {
            return Loc.F("Calib.ZCaptureFail", ZName(nozzle));
        }

        private static string ZName(int nozzle) { return nozzle == 1 ? AxisId.Z1 : AxisId.Z2; }
        private static string AName(int nozzle) { return nozzle == 1 ? AxisId.A1 : AxisId.A2; }

        private double Pos(string axis)
        {
            var a = _motion[axis];
            return a == null ? double.NaN : a.CurrentPos;
        }

        private static string Join(IList<double> values)
        {
            if (values == null || values.Count == 0) return "—";
            var parts = new string[values.Count];
            for (int i = 0; i < values.Count; i++) parts[i] = values[i].ToString("0.###", CultureInfo.InvariantCulture);
            return string.Join(", ", parts);
        }

        /// <summary>Một dòng log cho mỗi lần kích Flow nạp điểm hiệu chuẩn: vị trí trục lúc chụp (đúng bộ số App ghi vào
        /// biến toàn cục) và Flow có thấy vật không. <paramref name="nozzle"/> = 0 là CAM top-down.</summary>
        private void LogCalibPoint(string what, AxisSnapshot s, int nozzle, bool found)
        {
            string text = nozzle == 0
                ? string.Format(CultureInfo.InvariantCulture, "{0}: X {1:0.000}, Y2 {2:0.000}", what, s.X, s.Y2)
                : string.Format(CultureInfo.InvariantCulture, "{0}: X {1:0.000}, {2} {3:0.000}°, {4} {5:0.000}",
                    what, s.X, AName(nozzle), nozzle == 1 ? s.A1 : s.A2, ZName(nozzle), Pos(ZName(nozzle)));
            LogWorker.Instance.RecordLog(text + (found ? " — thấy vật" : " — Flow báo KHÔNG thấy vật"),
                found ? Utility.Log.LogLevel.Info : Utility.Log.LogLevel.Warning, "Calib");
        }
    }
}
