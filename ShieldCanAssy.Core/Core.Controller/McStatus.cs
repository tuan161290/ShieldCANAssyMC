using ShieldCanAssy.Core.Infor;
using ShieldCanAssy.Core.Lang;
using ShieldCanAssy.Utility;

namespace ShieldCanAssy.Core.Controller
{
    /// <summary>
    /// Trạng thái máy hiện thời, bind thẳng lên UI. Chỉ McControl và SafetyMonitor được ghi;
    /// mọi nơi khác chỉ đọc.
    /// </summary>
    public class McStatus : Bindable
    {
        public McStatus()
        {
            // Chữ trạng thái/bước/tên model chưa nạp dựng lúc đọc theo ngôn ngữ đang chọn — đổi ngôn ngữ thì báo UI đọc lại.
            Loc.LanguageChanged += () =>
            {
                Notify(nameof(StateText));
                Notify(nameof(AutoStepText));
                Notify(nameof(ModelName));
            };
        }

        private EMcState _state = EMcState.Init;
        public EMcState State { get => _state; set { if (SetProperty(ref _state, value)) Notify(nameof(StateText)); } }

        private EMcMode _mode = EMcMode.Manual;
        public EMcMode Mode { get => _mode; set => SetProperty(ref _mode, value); }

        /// <summary>
        /// Máy đang ở chế độ <b>chạy rà</b> — bấm START sẽ chạy <see cref="Routine_DryCycle"/> (không sản phẩm,
        /// chỉ chạy trục) thay cho chu trình thật. Đặt qua <c>McControl.SetDryRun</c>.
        ///
        /// <para><b>Cố ý KHÔNG lưu xuống đĩa.</b> Mở lại app là về false. Một cờ đổi hẳn việc máy làm mà sống sót
        /// qua lần tắt máy thì sớm muộn sẽ có ca sản xuất chạy rà cả buổi mà không ai biết.</para>
        /// </summary>
        public bool DryRun { get => _dryRun; set => SetProperty(ref _dryRun, value); }
        private bool _dryRun;

        private EOriginState _originStep = EOriginState.O0_PreCheck;
        public EOriginState OriginStep { get => _originStep; set => SetProperty(ref _originStep, value); }

        private bool _orgOk;
        public bool OrgOk { get => _orgOk; set => SetProperty(ref _orgOk, value); }

        private EAutoState _autoStep = EAutoState.S00_Ready;
        public EAutoState AutoStep { get => _autoStep; set { if (SetProperty(ref _autoStep, value)) Notify(nameof(AutoStepText)); } }

        /// <summary>Lượng bù của sản phẩm vừa đặt — để nhìn ra vision đang trôi trước khi thành NG.</summary>
        private double _lastDeltaTheta;
        public double LastDeltaTheta { get => _lastDeltaTheta; set => SetProperty(ref _lastDeltaTheta, value); }

        private double _lastPlaceError;
        public double LastPlaceError { get => _lastPlaceError; set => SetProperty(ref _lastPlaceError, value); }

        // ── An toàn ──
        private bool _emgOk;
        public bool EmgOk { get => _emgOk; set => SetProperty(ref _emgOk, value); }

        private bool _lightCurtainOk = true;
        public bool LightCurtainOk { get => _lightCurtainOk; set => SetProperty(ref _lightCurtainOk, value); }

        private bool _airOk;
        public bool AirOk { get => _airOk; set => SetProperty(ref _airOk, value); }

        private bool _safetyChainOk;
        public bool SafetyChainOk { get => _safetyChainOk; set => SetProperty(ref _safetyChainOk, value); }

        // ── Kết nối ──
        private bool _motionConnected;
        public bool MotionConnected { get => _motionConnected; set => SetProperty(ref _motionConnected, value); }

        private bool _ioConnected;
        public bool IoConnected { get => _ioConnected; set => SetProperty(ref _ioConnected, value); }

        private bool _visionConnected;
        public bool VisionConnected { get => _visionConnected; set => SetProperty(ref _visionConnected, value); }

        private bool _simulation;
        public bool Simulation { get => _simulation; set => SetProperty(ref _simulation, value); }

        // ── Hiển thị ──
        private ETowerLamp _lamp = ETowerLamp.Off;
        public ETowerLamp TowerLamp { get => _lamp; set => SetProperty(ref _lamp, value); }

        private bool _buzzer;
        public bool Buzzer { get => _buzzer; set => SetProperty(ref _buzzer, value); }

        private bool _buzzerMuted;
        public bool BuzzerMuted { get => _buzzerMuted; set => SetProperty(ref _buzzerMuted, value); }

        /// <summary>null = chưa nạp model — hiện chữ "(chưa nạp)" theo ngôn ngữ đang chọn.</summary>
        private string _modelName;
        public string ModelName { get => _modelName ?? Loc.T("Status.ModelNotLoaded"); set => SetProperty(ref _modelName, value); }

        private string _message = "";
        /// <summary>Dòng nhắc hiện dưới nút bấm — nói rõ vì sao lệnh vừa rồi không thực hiện được.</summary>
        public string Message { get => _message; set => SetProperty(ref _message, value); }

        public string AutoStepText
        {
            get
            {
                switch (AutoStep)
                {
                    case EAutoState.S00_Ready:     return Loc.T("Status.StepS00");
                    case EAutoState.S05_ScanAssy:  return Loc.T("Status.StepS05");
                    case EAutoState.S10_Pick1:     return Loc.T("Status.StepS10");
                    case EAutoState.S11_Pick2:     return Loc.T("Status.StepS11");
                    // Tay #2 gần CAM1 hơn nên soi trước (Tuấn xác nhận 2026-09-05) — S20 vẫn là
                    // BƯỚC đầu của cặp align, không phải "tay #1", xem Routine_Auto.
                    case EAutoState.S20_Align1:    return Loc.T("Status.StepS20");
                    case EAutoState.S21_Align2:    return Loc.T("Status.StepS21");
                    case EAutoState.S30_Locate1:   return Loc.T("Status.StepS30");
                    case EAutoState.S31_Place1:    return Loc.T("Status.StepS31");
                    case EAutoState.S32_Locate2:   return Loc.T("Status.StepS32");
                    case EAutoState.S33_Place2:    return Loc.T("Status.StepS33");
                    case EAutoState.S40_Index:     return Loc.T("Status.StepS40");
                    case EAutoState.S50_TrayCheck: return Loc.T("Status.StepS50");
                    default:                       return AutoStep.ToString();
                }
            }
        }

        public string StateText
        {
            get
            {
                switch (State)
                {
                    case EMcState.Init:         return Loc.T("Status.StateInit");
                    case EMcState.NotHomed:     return Loc.T("Status.StateNotHomed");
                    case EMcState.Homing:       return Loc.T("Status.StateHoming");
                    case EMcState.Idle:         return Loc.T("Status.StateIdle");
                    case EMcState.Armed:        return Loc.T("Status.StateArmed");
                    case EMcState.Running:      return Loc.T("Status.StateRunning");
                    case EMcState.Pausing:      return Loc.T("Status.StatePausing");
                    case EMcState.Paused:       return Loc.T("Status.StatePaused");
                    case EMcState.Held:         return Loc.T("Status.StateHeld");
                    case EMcState.WaitOperator: return Loc.T("Status.StateWaitOperator");
                    case EMcState.Alarm:        return Loc.T("Status.StateAlarm");
                    case EMcState.Emg:          return Loc.T("Status.StateEmg");
                    default:                    return State.ToString();
                }
            }
        }
    }
}
