using System;
using System.Collections;
using System.Collections.Generic;
using System.Drawing;
using GlobalVariableModuleCs;
using IMVSNPointCalibModuCs;
using TranslationCalibModuCs;
using VM.Core;
using VM.ExtensionMethod;
using VM.PlatformSDKCS;
using ShieldCanAssy.Core.Communication;
using ShieldCanAssy.Core.Lang;

namespace ShieldCanAssy.Lib.VisionMaster
{
    /// <summary>Kết quả một lần Run(): số output float theo đúng thứ tự yêu cầu, kèm ảnh nếu có
    /// khai tên output ảnh.</summary>
    public class VisionRunResult
    {
        public float[] Floats;
        public Bitmap Image;
    }

    /// <summary>Tên + mã kiểu (<c>IMVS_MODULE_BASE_DATA_TYPE</c>) các output một Flow đang khai, kèm số
    /// Input Params còn sót. Lớp này giữ nguyên số của SDK, không dịch nghĩa — việc đó của hợp đồng.</summary>
    public class VisionFlowIo
    {
        public readonly List<KeyValuePair<string, int>> Outputs = new List<KeyValuePair<string, int>>();
        public int InputCount = -1;

        /// <summary>Các module bên trong Flow, kể cả module nằm trong Group.</summary>
        public readonly List<VisionModuleIo> Modules = new List<VisionModuleIo>();
    }

    /// <summary>Một dòng của tab Input Params / Output Params của một module. <see cref="Source"/> rỗng =
    /// ô Subscribe bỏ trống.</summary>
    public class VisionIoInfo
    {
        public string Name;
        public string TypeText;
        public string Source;
    }

    public class VisionModuleIo
    {
        public string Name;
        public readonly List<VisionIoInfo> Inputs = new List<VisionIoInfo>();
        public readonly List<VisionIoInfo> Outputs = new List<VisionIoInfo>();
    }

    /// <summary>Kết quả một lần Run() theo tên output — bắt buộc thì có mặt, tuỳ chọn thì có thể thiếu.</summary>
    public class VisionRunValues
    {
        public readonly Dictionary<string, float> Values = new Dictionary<string, float>(StringComparer.Ordinal);
        public readonly List<string> Missing = new List<string>();
        public Bitmap Image;
    }

    /// <summary>
    /// Bọc mỏng HIKROBOT VisionMaster 4.4.0 PlatformSDK (<c>VM.Core</c> / <c>VM.PlatformSDKCS</c>)
    /// — PLAN.md §2.1, §6.
    ///
    /// <para><b>Cách gọi lấy nguyên từ mẫu chính hãng cài kèm SDK</b> tại
    /// <c>C:\Program Files\VisionMaster4.4.0\Development\V4.x\Samples\C#\PlatformSDKSampleCS\</c>
    /// — cụ thể <c>GetResultControl\Form1.cs</c> (nạp solution, chạy Flow, đọc output) và
    /// <c>SolutionControlCS_WPF\MainWindow.xaml.cs</c> (nạp/đóng solution từ WPF). KHÔNG suy đoán
    /// từ <c>VM.PlatformSDKCS.ServerSDKAPI</c> — lớp P/Invoke thô của SDK không có ví dụ dùng đúng
    /// kèm theo, còn <c>VM.Core.VmSolution</c>/<c>VmProcedure</c> ở đây là lớp bọc OOP chính hãng
    /// dùng trong toàn bộ mẫu C#/WPF của hãng.</para>
    ///
    /// <para><b>Chưa chạy thử với solution thật.</b> API tồn tại và biên dịch được — SDK đã cài
    /// sẵn trên máy dev, tham chiếu thẳng DLL thật (xem ShieldCanAssy.Core.csproj) — nhưng chưa
    /// đối chiếu với solution VisionMaster thật vì solution đó chưa được dựng.</para>
    ///
    /// <para><b>Tên Flow và tên biến output KHÔNG nằm trong lớp này.</b> Lớp này chỉ biết chạy
    /// một Flow theo tên truyền vào và đọc các output theo tên truyền vào —
    /// <see cref="ShieldCanAssy.Core.Data.SystemData.VisionOutputs"/> (sửa được ở trang Setting)
    /// mới là nơi giữ tên thật, để đổi tên Flow/output trong solution không cần build lại app —
    /// PLAN.md §6.8, giống màn hình nhập tên Output ở app HousingCorrosion.</para>
    /// </summary>
    public class VisionMasterDevice
    {
        public bool IsSolutionLoaded { get; private set; }

        /// <summary>Nạp solution — <see cref="VmSolution"/> là singleton toàn tiến trình nên chỉ
        /// một solution được nạp tại một thời điểm (mẫu <c>SolutionControlCS_WPF</c>).</summary>
        public void LoadSolution(string path, string password)
        {
            _globalVars = null;      // solution mới thì module cũ không còn hiệu lực
            VmSolution.Load(path, password ?? "");

            // Sau khi nạp xong, tắt callback kết quả của mọi module để đỡ tốn tài nguyên — ta chỉ
            // cần Run() rồi đọc output của Flow bằng GetFloatOutputResult, không cần callback
            // (mẫu GetResultControl\Form1.cs làm y hệt ngay sau LoadSolution).
            VmSolution.Instance.DisableModulesCallback();
            IsSolutionLoaded = true;
        }

        /// <summary>
        /// Chạy một Flow (đồng bộ, CHẶN luồng gọi — lớp trên phải tự bọc Task.Run + timeout) rồi
        /// đọc các output float đã đặt tên theo đúng thứ tự tham số. Ném <see cref="VmException"/>
        /// nếu Flow không tồn tại, chạy lỗi, hoặc thiếu output đã khai — lớp trên (VisionConnect)
        /// chịu trách nhiệm bắt và quy đổi thành alarm/kết quả "không tìm thấy".
        /// </summary>
        public float[] RunAndGetFloats(string flowName, params string[] outputNames)
        {
            var pro = GetProcedure(flowName);
            pro.Run();
            return ReadFloats(pro, flowName, outputNames);
        }

        private static VmProcedure GetProcedure(string flowName)
        {
            var pro = VmSolution.Instance[flowName] as VmProcedure;
            if (pro == null)
                throw new VmException(-1, Loc.F("Vm.DevFlowNotFound", flowName));
            return pro;
        }

        /// <summary>
        /// Đọc các output float đã đặt tên theo đúng thứ tự yêu cầu.
        ///
        /// <para>GetFloatOutputResult(string) của VmProcedure đã DEPRECATED trong bản SDK cài trên
        /// máy — dùng thay thế theo đúng khuyến cáo của trình biên dịch: ModuResult.GetOutputFloat.
        /// Chỉ lấy phần tử [0] của mỗi output, nên module định vị trong solution phải đặt "Max
        /// Number to Find = 1"; nhiều kết quả thì App lặng lẽ dùng cái đầu tiên.</para>
        /// </summary>
        private static float[] ReadFloats(VmProcedure pro, string flowName, string[] outputNames)
        {
            var moduResult = pro.ModuResult;
            var results = new float[outputNames.Length];
            for (int i = 0; i < outputNames.Length; i++)
            {
                if (TryReadNumber(moduResult, outputNames[i], out results[i])) continue;
                throw new VmException(-1, Loc.F("Vm.DevMissingOutputVar", flowName, outputNames[i]));
            }
            return results;
        }

        /// <summary>
        /// Đọc một output số, chấp nhận <b>cả hai</b> kiểu mà VisionMaster dùng: thử
        /// <c>GetOutputFloat</c> trước, không có thì thử <c>GetOutputInt</c>.
        ///
        /// <para>Vì sao cần cả hai: nhiều kết quả tự nhiên của VisionMaster là <b>Int</b> —
        /// <c>Module Status</c> (0 = NG, 1 = OK), <c>Match Num</c>, <c>CalibPointNum</c> — và
        /// giao diện khai output của Flow <b>lọc danh sách Subscribe theo kiểu</b>: để ô Type là
        /// float thì những nguồn Int đó biến mất khỏi danh sách, không bind được. Trước đây App
        /// chỉ gọi <c>GetOutputFloat</c>, nên cách duy nhất là chèn thêm một module
        /// <c>Variable Calculation</c> chuyển Int→float vào TỪNG Flow — thừa việc, và quên một
        /// chỗ là hỏng theo kiểu khó đoán nhất.</para>
        ///
        /// <para>Khó đoán ở chỗ: output sai kiểu làm <c>GetOutputFloat</c> trả rỗng, giống hệt
        /// như output không tồn tại, nên App báo <b>"không tìm thấy"</b> trong khi VisionMaster
        /// khớp mẫu hoàn toàn bình thường và đang hiện toạ độ trên màn hình. Tuấn dính đúng lỗi
        /// này 2026-09-12 khi khai <c>Found</c> ← <c>Module Status</c> (kiểu Int).</para>
        /// </summary>
        private static bool TryReadNumber(ProcedureResult moduResult, string name, out float value)
        {
            value = 0f;

            try
            {
                // FloatDataArray/IntDataArray là struct — không so sánh với null được.
                var f = moduResult.GetOutputFloat(name);
                if (f.nValueNum >= 1 && f.pFloatVal != null && f.pFloatVal.Length >= 1)
                {
                    value = f.pFloatVal[0];
                    return true;
                }
            }
            catch { /* output không phải kiểu float — thử kiểu int bên dưới */ }

            try
            {
                var n = moduResult.GetOutputInt(name);
                if (n.nValueNum >= 1 && n.pIntVal != null && n.pIntVal.Length >= 1)
                {
                    value = n.pIntVal[0];
                    return true;
                }
            }
            catch { /* không đọc được kiểu nào — coi như thiếu output */ }

            return false;
        }

        /// <summary>
        /// Như <see cref="RunAndGetFloats"/> nhưng CHỈ chạy Flow MỘT LẦN rồi đọc cả output float
        /// lẫn ảnh từ đúng lần chạy đó — chạy hai lần (một để lấy số, một để lấy ảnh) sẽ kích
        /// camera thật chụp hai lần cho một lần trigger logic, tốn thời gian và có thể đọc phải
        /// hai khung hình khác nhau nếu vật vừa dịch giữa hai lần chụp.
        ///
        /// <para><c>EnableResultCallback()</c> theo đúng mẫu SDK
        /// <c>GetResultControl\Form1.cs</c> — không gọi thì <c>GetOutputImageV2</c> không có dữ
        /// liệu để trả, dù output float vẫn đọc được bình thường.</para>
        ///
        /// <para>Ảnh không bắt buộc: <paramref name="imageOutputName"/> để trống/null thì bỏ qua
        /// hẳn bước lấy ảnh; solution chưa khai output ảnh (hoặc lấy ảnh lỗi) thì
        /// <see cref="VisionRunResult.Image"/> là null nhưng KHÔNG ném lỗi — đo tư thế để đặt
        /// hàng không phụ thuộc vào việc có lấy được ảnh hiển thị hay không.</para>
        /// </summary>
        public VisionRunResult RunAndGetFloatsWithImage(string flowName, string imageOutputName, params string[] outputNames)
        {
            var pro = GetProcedure(flowName);
            bool wantImage = !string.IsNullOrEmpty(imageOutputName);
            if (wantImage) pro.EnableResultCallback();

            pro.Run();

            float[] results = ReadFloats(pro, flowName, outputNames);
            return new VisionRunResult { Floats = results, Image = wantImage ? TryReadImage(pro, imageOutputName) : null };
        }

        /// <summary>
        /// Đọc danh sách output Flow đang khai — <c>ProcedureResult.GetAllOutputNameInfo()</c>, đúng những
        /// tên hiện trong tab Output Params của VisionMaster IDE. <b>Không gọi Run()</b>: đây là phần khai
        /// báo của Flow, không phải kết quả một lần chạy.
        ///
        /// <para>Kèm theo số Input Params (<c>ProcedureParam.GetAllInputNameInfo()</c>) — hợp đồng bỏ Input
        /// Params từ 2026-09-14 nên còn sót là rác của bản cũ, không phải lỗi.</para>
        /// </summary>
        public VisionFlowIo GetFlowIo(string flowName)
        {
            var pro = GetProcedure(flowName);
            var io = new VisionFlowIo();

            // Nguồn 1 — danh sách output của kết quả Flow. Dò trên ba solution thử 19/09: nguồn này trả 0
            // với Flow chưa chạy lần nào — chưa phân biệt được là do Flow đó không khai output hay do chưa
            // chạy, nên không để nó làm nguồn duy nhất.
            var seen = new HashSet<string>(StringComparer.Ordinal);
            var moduResult = pro.ModuResult;
            if (moduResult != null)
            {
                var names = moduResult.GetAllOutputNameInfo();
                if (names != null)
                    // IoNameInfo là struct — không so sánh với null được.
                    foreach (var n in names)
                        if (!string.IsNullOrEmpty(n.Name) && seen.Add(n.Name))
                            io.Outputs.Add(new KeyValuePair<string, int>(n.Name, (int)n.TypeName));
            }

            // Nguồn 2 — chính Flow cũng là một VmModule, và tab Output Params của nó nằm ở
            // Outputs. Có mặt kể cả khi Flow chưa chạy. Bỏ mấy output máy móc nào cũng có.
            var self = TryGetModule(flowName);
            if (self != null)
                foreach (var vio in EnumIo(self.Outputs))
                {
                    // Tên ở nguồn này bọc %…% — bóc ra thì mới trùng với nguồn 1 và với hợp đồng.
                    string name = VmOutCheck.CleanIoName(vio.Name);
                    if (!IsBuiltInIo(name) && seen.Add(name))
                        io.Outputs.Add(new KeyValuePair<string, int>(name, TypeCodeOf(vio.TypeName)));
                }

            try
            {
                var prm = pro.ModuParams;
                var ins = prm == null ? null : prm.GetAllInputNameInfo();
                io.InputCount = ins == null ? -1 : ins.Count;
            }
            catch { io.InputCount = -1; }

            FillModules(pro, flowName, io);
            return io;
        }

        /// <summary>Kiểu của module Group trong VisionMaster — <c>GetAllModuleList</c> trả về nó
        /// xen giữa danh sách, rồi mới tới các module nằm trong nó.</summary>
        private const string GroupModuleType = "IMVSGroup";

        /// <summary>Đọc hai tab tham số (<c>VmModule.Inputs</c> / <c>Outputs</c>) của từng module trong Flow.</summary>
        private static void FillModules(VmProcedure pro, string flowName, VisionFlowIo io)
        {
            foreach (var gm in GetFlowModules(pro, flowName))
            {
                var m = gm.Value;
                var mi = new VisionModuleIo
                {
                    Name = gm.Key == null ? m.Name : gm.Key + " · " + m.Name
                };
                AddIo(m.Inputs, mi.Inputs);
                AddIo(m.Outputs, mi.Outputs);
                io.Modules.Add(mi);
            }
        }

        /// <summary>
        /// Bấm <b>"Clear Point"</b> của mọi module hiệu chuẩn trong Flow — N-point Calibration (CAM top-down, §6.2) và
        /// Translation Calibration (CAM bottom-up, §6.4) — qua <c>ModuParams.DoClearPoint()</c> (Tuấn yêu cầu 2026-10-09).
        /// Không chạy Flow. Trả tên các module đã xoá.
        ///
        /// <para>Ném <see cref="VmException"/> nếu Flow không có module nào thuộc hai loại đó, hoặc một module báo
        /// lỗi. <c>DoClearPoint</c> tự bắt <c>VmException</c> rồi trả mã lỗi, 0 là xoá được (đọc IL của DLL ngày
        /// 09/10). Chưa thử trên solution thật.</para>
        /// </summary>
        public List<string> ClearCalibPoints(string flowName)
        {
            var pro = GetProcedure(flowName);
            var cleared = new List<string>();
            foreach (var gm in GetFlowModules(pro, flowName))
            {
                int rc;
                var nPoint = gm.Value as IMVSNPointCalibModuTool;
                var trans = gm.Value as TranslationCalibModuTool;
                if (nPoint != null) rc = nPoint.ModuParams.DoClearPoint();
                else if (trans != null) rc = trans.ModuParams.DoClearPoint();
                else continue;

                if (rc != 0)
                    throw new VmException(rc, Loc.F("Vm.DevClearPointFail",
                        gm.Value.Name, flowName, rc));
                cleared.Add(gm.Value.Name);
            }

            if (cleared.Count == 0)
                throw new VmException(-1, Loc.F("Vm.DevNoCalibModule", flowName));
            return cleared;
        }

        /// <summary>
        /// Mọi module bên trong Flow — <b>kể cả module nằm trong Group</b> — kèm tên Group chứa nó (Key, null = tầng
        /// ngoài).
        ///
        /// <para>Ba điều dưới đây dò được trên SDK thật ngày 19/09 (solution có Group), không phải suy
        /// từ chữ ký hàm:</para>
        /// <list type="bullet">
        /// <item><c>VmSolution.GetFullModuleList</c> ném lỗi "Array cannot be null" — không dùng được.</item>
        /// <item><c>VmProcedure.GetProcedureModuleList</c> chỉ trả module ở tầng ngoài: Group có tên
        /// trong danh sách nhưng ba module bên trong thì không. Phải dùng <c>GetAllModuleList</c>.</item>
        /// <item>Module nằm trong Group lấy bằng khoá <c>"Flow.Group.Module"</c>; <c>"Flow.Module"</c>
        /// trả null. Một Group trong solution thử có hai module không lấy được bằng cả hai khoá — chưa rõ vì
        /// sao; khi đó module đó vắng mặt trong danh sách và phần kiểm Script báo không thấy Script.</item>
        /// <item>Module ở tầng ngoài (không Group) lấy bằng <c>"Flow.Module"</c> — cách Tuấn dựng
        /// AlignBottom_ToolN (21/09).</item>
        /// </list>
        /// </summary>
        private static List<KeyValuePair<string, VmModule>> GetFlowModules(VmProcedure pro, string flowName)
        {
            var list = new List<KeyValuePair<string, VmModule>>();
            ModuleInfoList all;
            try { all = pro.GetAllModuleList(); }
            catch { return list; }
            if (all.astModuleInfo == null) return list;

            string group = null;
            for (int i = 0; i < all.nNum && i < all.astModuleInfo.Length; i++)
            {
                string name = all.astModuleInfo[i].strDisplayName;
                if (string.IsNullOrEmpty(name)) continue;

                if (all.astModuleInfo[i].strModuleName == GroupModuleType) { group = name; continue; }

                // Tên trần trước (module tầng ngoài), rồi mới thử trong Group gần nhất.
                var m = TryGetModule(flowName + "." + name);
                string where = null;
                if (m == null && group != null)
                {
                    m = TryGetModule(flowName + "." + group + "." + name);
                    where = group;
                }
                if (m == null) continue;

                list.Add(new KeyValuePair<string, VmModule>(where, m));
            }
            return list;
        }

        /// <summary>Output mà module nào của VisionMaster cũng có — không phải thứ người dùng khai.</summary>
        private static bool IsBuiltInIo(string name)
        {
            switch (name)
            {
                case "fArrivalTimeStampLow":
                case "fArrivalTimeStampHigh":
                case "fLeaveTimeStampLow":
                case "fLeaveTimeStampHigh":
                case "ModuRunTime":
                case "ModuStatus":
                case "LoopIndex":
                    return true;
                default:
                    return false;
            }
        }

        /// <summary>Đổi tên kiểu của <c>VmIO</c> ("float", "int"…) sang mã <c>IMVS_MODULE_BASE_DATA_TYPE</c>.</summary>
        private static int TypeCodeOf(string typeName)
        {
            if (string.Equals(typeName, "float", StringComparison.OrdinalIgnoreCase))
                return (int)IMVS_MODULE_BASE_DATA_TYPE.IMVS_GRAP_TYPE_FLOAT;
            if (string.Equals(typeName, "int", StringComparison.OrdinalIgnoreCase))
                return (int)IMVS_MODULE_BASE_DATA_TYPE.IMVS_GRAP_TYPE_INT;
            if (string.Equals(typeName, "string", StringComparison.OrdinalIgnoreCase))
                return (int)IMVS_MODULE_BASE_DATA_TYPE.IMVS_GRAP_TYPE_STRING;
            if (string.Equals(typeName, "IMAGE", StringComparison.OrdinalIgnoreCase))
                return (int)IMVS_MODULE_BASE_DATA_TYPE.IMVS_BASE_TYPE_IMAGE_DATA;
            return (int)IMVS_MODULE_BASE_DATA_TYPE.IMVS_BASE_TYPE_UNDEFINED;
        }

        private static IEnumerable<VmIO> EnumIo(VmIOCollection col)
        {
            if (col == null) yield break;
            foreach (var io in col)
                if (io != null && !string.IsNullOrEmpty(io.Name)) yield return io;
        }

        private static VmModule TryGetModule(string key)
        {
            if (string.IsNullOrEmpty(key)) return null;
            try { return VmSolution.Instance[key] as VmModule; }
            catch { return null; }
        }

        private static void AddIo(VmIOCollection src, List<VisionIoInfo> dst)
        {
            foreach (var io in EnumIo(src))
            {
                string name = VmOutCheck.CleanIoName(io.Name);
                if (!IsBuiltInIo(name))
                    dst.Add(new VisionIoInfo { Name = name, TypeText = io.TypeName, Source = BindSource(io) });
            }
        }

        /// <summary>
        /// Ô Subscribe của một dòng tham số trỏ vào đâu — null nếu bỏ trống. Dò trên máy 19/09:
        /// <c>SubscriptionInfo</c> là chỗ có số, dạng <c>"1 Image Source1.Image Data[]"</c>; dòng chưa
        /// bind trả về chuỗi RỖNG (không phải null). <c>AutoSubscribeName</c> luôn rỗng. Giữ thêm hai
        /// đường kia làm dự phòng cho kiểu bind khác.
        /// </summary>
        private static string BindSource(VmIO io)
        {
            try { if (!string.IsNullOrEmpty(io.SubscriptionInfo)) return io.SubscriptionInfo; }
            catch { }
            try { if (!string.IsNullOrEmpty(io.AutoSubscribeName)) return io.AutoSubscribeName; }
            catch { }
            try
            {
                var r = io.RelateIO;
                if (r != null && !string.IsNullOrEmpty(r.Name))
                    return string.IsNullOrEmpty(r.ModulePathName) ? r.Name : r.ModulePathName + " · " + r.Name;
            }
            catch { }
            return null;
        }

        private static Bitmap TryReadImage(VmProcedure pro, string imageOutputName)
        {
            try
            {
                var imgData = pro.ModuResult.GetOutputImageV2(imageOutputName);
                return imgData == null ? null : imgData.ToBitmap();
            }
            catch
            {
                // Ảnh chỉ để hiển thị, không phải số dùng để tính lượng bù — im lặng bỏ qua.
                return null;
            }
        }

        // ───────────────────────── biến toàn cục của solution ─────────────────────────

        private GlobalVariableModuleTool _globalVars;

        /// <summary>
        /// Ghi vị trí trục thật vào <b>biến toàn cục</b> của solution (Tuấn chốt 2026-09-12).
        ///
        /// <para>Đây là cách nạp toạ độ vật lý cho module hiệu chuẩn: module đặt <i>Calibration
        /// Points Input = By Coordinate</i> rồi subscribe <i>Physical Coordinate X/Y</i> và
        /// <i>World Rotate Angle</i> vào các biến toàn cục này; App ghi giá trị ngay trước khi kích
        /// Flow. Chính Hikrobot dùng khuôn đó trong tài liệu "Calibration and Locating Theory"
        /// tr.54 (các biến <c>BottomX/BottomY/BottomR</c> kiểu float).</para>
        ///
        /// <para>Hơn hẳn cách khai input riêng cho từng Flow ở chỗ: một chỗ duy nhất giữ trạng thái
        /// trục của máy, Flow nào cần trục nào thì subscribe trục đó — không phải khai lại bộ input
        /// cho mỗi Flow hiệu chuẩn, và không Flow nào có thể "quên" nhận toạ độ.</para>
        ///
        /// <para>Tên biến chưa có trong solution thì ném lỗi kèm danh sách biến đang có — thà dừng
        /// còn hơn nạp một điểm hiệu chuẩn với toạ độ cũ mà không ai biết.</para>
        /// </summary>
        public void SetGlobalFloats(IDictionary<string, double> vars, string moduleNameHint)
        {
            if (vars == null || vars.Count == 0) return;

            var tool = GetGlobalVarModule(moduleNameHint);
            var declared = GetGlobalVarTypeMap(tool);

            foreach (var kv in vars)
            {
                if (string.IsNullOrEmpty(kv.Key)) continue;

                if (declared.Count > 0 && !declared.ContainsKey(kv.Key))
                    throw new VmException(-1, Loc.F("Vm.DevVarNotDeclaredList", kv.Key, string.Join(", ", declared.Keys)));

                string type;
                declared.TryGetValue(kv.Key, out type);
                WriteVar(tool, kv.Key, kv.Value, type);
            }
        }

        /// <summary>Ghi một biến theo đúng kiểu đang khai: <c>int</c> thì SetVarInt (làm tròn), còn lại SetVarFloat. Cờ
        /// <c>IsTriggerLocate</c> khai int cho Module rẽ nhánh (Tuấn 2026-10-08); các biến khác đều float.</summary>
        private static void WriteVar(GlobalVariableModuleTool tool, string name, double value, string type)
        {
            if (string.Equals(type, "int", StringComparison.OrdinalIgnoreCase))
                tool.SetVarInt(name, new[] { (int)Math.Round(value) });
            else
                tool.SetVarFloat(name, new[] { (float)value });
        }

        private static string StripPercent(string name)
        {
            return name.Length > 1 && name[0] == '%' && name[name.Length - 1] == '%' ? name.Substring(1, name.Length - 2) : name;
        }

        /// <summary>
        /// Ghi biến toàn cục cho chu trình mới — NGHIÊM: tên trống, không đọc được danh sách biến, tên chưa
        /// khai, hay giá trị không hữu hạn đều ném lỗi và KHÔNG ghi biến nào. Khác <see cref="SetGlobalFloats"/>
        /// (đường nạp điểm hiệu chuẩn cũ) vốn bỏ qua đối chiếu khi không đọc được danh sách.
        /// </summary>
        public void SetGlobalFloatsStrict(IDictionary<string, double> vars, string moduleNameHint)
        {
            if (vars == null || vars.Count == 0) return;

            var tool = GetGlobalVarModule(moduleNameHint);
            var declared = new Dictionary<string, string>(StringComparer.Ordinal);
            foreach (var kv in GetGlobalVarTypes(moduleNameHint))
                declared[StripPercent(kv.Key)] = kv.Value;

            foreach (var kv in vars)
            {
                if (string.IsNullOrEmpty(kv.Key))
                    throw new VmException(-1, Loc.T("Vm.DevVarNameEmpty"));
                if (!declared.ContainsKey(kv.Key))
                    throw new VmException(-1, Loc.F("Vm.DevVarNotDeclared", kv.Key));
                if (double.IsNaN(kv.Value) || double.IsInfinity(kv.Value))
                    throw new VmException(-1, Loc.F("Vm.DevValueNotFinite", kv.Key));
            }

            foreach (var kv in vars)
                WriteVar(tool, kv.Key, kv.Value, declared[kv.Key]);
        }

        /// <summary>
        /// Gán Input Params (nếu có), chạy một Flow rồi đọc output theo tên. Thiếu output bắt buộc thì ném lỗi; thiếu
        /// output tuỳ chọn thì ghi vào <see cref="VisionRunValues.Missing"/> — tư thế khớp mẫu thường rỗng khi không thấy
        /// vật. Input không hữu hạn hoặc tên không có trong tab Input Params (khi SDK đọc được danh sách) thì ném lỗi;
        /// SDK không đọc được danh sách thì Flow phải trả lại số qua output Echo để App đối chiếu.
        /// </summary>
        public VisionRunValues RunAndRead(string flowName, string imageOutputName,
                                          IList<string> required, IList<string> optional)
        {
            var pro = GetProcedure(flowName);
            bool wantImage = !string.IsNullOrEmpty(imageOutputName);
            if (wantImage) pro.EnableResultCallback();

            pro.Run();

            var result = new VisionRunValues();
            var moduResult = pro.ModuResult;
            if (required != null)
            {
                foreach (var name in required)
                {
                    float v;
                    if (!TryReadNumber(moduResult, name, out v))
                        throw new VmException(-1, Loc.F("Vm.DevMissingOutputParam", flowName, name));
                    result.Values[name] = v;
                }
            }
            if (optional != null)
            {
                foreach (var name in optional)
                {
                    float v;
                    if (TryReadNumber(moduResult, name, out v)) result.Values[name] = v;
                    else result.Missing.Add(name);
                }
            }
            if (wantImage) result.Image = TryReadImage(pro, imageOutputName);
            return result;
        }

        /// <summary>
        /// Tìm module Global Variable của solution đang nạp.
        ///
        /// <para>Ưu tiên tên/đường dẫn khai ở trang Setting; để trống thì quét
        /// <c>VmSolution.Instance.Modules</c> lấy module đầu tiên đúng kiểu — tên hiển thị của
        /// module này đổi theo ngôn ngữ cài đặt của VisionMaster nên không hardcode được.</para>
        /// </summary>
        private GlobalVariableModuleTool GetGlobalVarModule(string moduleNameHint)
        {
            if (_globalVars != null) return _globalVars;

            if (!string.IsNullOrEmpty(moduleNameHint))
                _globalVars = VmSolution.Instance[moduleNameHint] as GlobalVariableModuleTool;

            if (_globalVars == null)
            {
                var modules = VmSolution.Instance.Modules as IEnumerable;
                if (modules != null)
                {
                    foreach (object m in modules)
                    {
                        var gv = m as GlobalVariableModuleTool;
                        if (gv != null) { _globalVars = gv; break; }
                    }
                }
            }

            if (_globalVars == null)
                throw new VmException(-1, string.IsNullOrEmpty(moduleNameHint)
                    ? Loc.T("Vm.DevNoGvModule")
                    : Loc.F("Vm.DevNoGvModuleNamed", moduleNameHint));

            return _globalVars;
        }

        /// <summary>Tên → kiểu các biến toàn cục đang khai. Trả rỗng nếu không đọc được — khi đó
        /// bỏ qua phép đối chiếu tên thay vì chặn cả phép hiệu chuẩn (và ghi float như trước).</summary>
        private static Dictionary<string, string> GetGlobalVarTypeMap(GlobalVariableModuleTool tool)
        {
            var map = new Dictionary<string, string>(StringComparer.Ordinal);
            try
            {
                var all = tool.GetAllGlobalVar();
                if (all == null) return map;
                foreach (var info in all)
                    if (!string.IsNullOrEmpty(info.strValueName)) map[StripPercent(info.strValueName)] = info.strValueType ?? "";
            }
            catch
            {
                map.Clear();
            }
            return map;
        }

        /// <summary>
        /// Tên → kiểu (<c>float</c>/<c>int</c>/<c>string</c>) của mọi biến toàn cục đang khai. Khác
        /// <see cref="GetGlobalVarNames"/>: không đọc được thì NÉM LỖI — dùng để quyết định tạo biến
        /// nên không được coi "không đọc được" là "chưa có biến nào".
        /// </summary>
        public Dictionary<string, string> GetGlobalVarTypes(string moduleNameHint)
        {
            var tool = GetGlobalVarModule(moduleNameHint);
            var all = tool.GetAllGlobalVar();
            var d = new Dictionary<string, string>();
            if (all == null) throw new VmException(-1, Loc.T("Vm.DevGvListNull"));
            foreach (var info in all)
                if (!string.IsNullOrEmpty(info.strValueName)) d[info.strValueName] = info.strValueType ?? "";
            return d;
        }

        /// <summary>
        /// Tạo một biến toàn cục mới với giá trị trung tính (0 hoặc chuỗi rỗng).
        ///
        /// <para><b>Không có trong tài liệu SDK.</b> Tài liệu .NET/C++ của VM 4.4 chỉ có đọc/ghi giá
        /// trị; hàm này là đường mà chính cửa sổ Global Variable của VisionMaster dùng
        /// (<c>VM.ExtensionMethod.GlobalVariableHelper.AddVar</c>, nằm cùng thư mục Assembly của SDK).
        /// Đã đọc mã: ghi cấu trúc biến vào tham số <c>AddVar</c> của module Global Variable
        /// (ID 13000) rồi thêm output tương ứng. Kiểm trên bản sao .solw trước khi dùng thật.</para>
        /// </summary>
        public void AddGlobalVar(string name, string type, string remark)
        {
            GlobalVariableHelper.AddVar(name, type, type == "string" ? "" : "0", remark ?? "");
        }

        /// <summary>Ghi giá trị đầu cho biến vừa tạo, qua hàm ghi theo kiểu (không qua chuỗi số theo
        /// culture Windows).</summary>
        public void SetGlobalVarValue(string moduleNameHint, string name, string type, string invariantValue)
        {
            var tool = GetGlobalVarModule(moduleNameHint);
            switch (type)
            {
                case "float":
                    tool.SetVarFloat(name, new[] { float.Parse(invariantValue, System.Globalization.CultureInfo.InvariantCulture) });
                    break;
                case "int":
                    tool.SetVarInt(name, new[] { int.Parse(invariantValue, System.Globalization.CultureInfo.InvariantCulture) });
                    break;
                default:
                    tool.SetVarString(name, new[] { invariantValue ?? "" });
                    break;
            }
        }

        /// <summary>Lưu solution đang nạp vào chính file .solw của nó.</summary>
        public string SaveSolution()
        {
            return VmSolution.Save();
        }

        public void CloseSolution()
        {
            _globalVars = null;
            try { VmSolution.Instance?.CloseSolution(); } catch { }
            IsSolutionLoaded = false;
        }

        public void Shutdown()
        {
            _globalVars = null;
            try { VmSolution.Instance?.Dispose(); } catch { }
            IsSolutionLoaded = false;
        }
    }
}
