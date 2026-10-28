#pragma once
// Cầu nhỏ sang phía .NET của các control VisionMaster đang chạy TRONG tiến trình App.
//
// Control VM (VmGlobalToolControl, VmMainViewConfigControl, VmRenderControl) là control .NET đăng ký COM (regasm): tạo
// control đầu tiên là CLR v4 được nạp vào tiến trình, và VM.Core.dll của VisionMaster nằm trong AppDomain mặc định. App
// nạp solution bằng SDK C++ (::LoadSolution) nên đối tượng .NET VM.Core.VmSolution.Instance KHÔNG biết đường dẫn file:
// nút Save trên thanh công cụ (VMControls.WPF.ToolControl.SaveSolution) thấy SolutionPath rỗng và chuyển sang Save As
// (Tuấn báo 2026-10-28; đọc IL của VMControls.WPF.dll / VM.Core.dll bản 4.4.0). Bản WPF nạp bằng VmSolution.Load
// nên không gặp.
#include <string>

namespace ui
{
    /// <summary>
    /// Ghi <paramref name="solutionFileUtf8"/> vào <c>VM.Core.VmSolution.Instance.SolutionPath</c> (setter private, gọi
    /// qua reflection) của mọi bản VM.Core đã nạp, rồi đọc lại để kiểm. Sau đó nút Save của thanh công cụ lưu thẳng vào
    /// file đó như bản .NET. Đường đi: CLRCreateInstance → runtime v4 ĐÃ nạp (không tự khởi động CLR) → AppDomain mặc
    /// định → GetAssemblies → Type.InvokeMember. Không ném.
    /// </summary>
    /// <param name="updated">Số bản VM.Core đã ghi và đọc lại khớp.</param>
    /// <returns>Rỗng khi ghi được ít nhất một bản; lý do (kèm HRESULT) khi không.</returns>
    std::string SetVmDotNetSolutionPath(std::string const& solutionFileUtf8, int& updated);
}
