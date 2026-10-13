$src = @'
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.InteropServices;

public static class DbWin
{
    [StructLayout(LayoutKind.Sequential)]
    struct HANDLE_EX
    {
        public IntPtr Object;
        public IntPtr UniqueProcessId;
        public IntPtr HandleValue;
        public uint GrantedAccess;
        public ushort CreatorBackTraceIndex;
        public ushort ObjectTypeIndex;
        public uint HandleAttributes;
        public uint Reserved;
    }

    [DllImport("ntdll.dll")] static extern int NtQuerySystemInformation(int cls, IntPtr buf, int len, out int ret);
    [DllImport("ntdll.dll")] static extern int NtQueryObject(IntPtr h, int cls, IntPtr buf, int len, out int ret);
    [DllImport("kernel32.dll", SetLastError = true)] static extern IntPtr OpenProcess(uint acc, bool inh, int pid);
    [DllImport("kernel32.dll", SetLastError = true)] static extern bool DuplicateHandle(IntPtr sp, IntPtr sh, IntPtr tp, out IntPtr th, uint acc, bool inh, uint opt);
    [DllImport("kernel32.dll")] static extern IntPtr GetCurrentProcess();
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] static extern IntPtr OpenFileMapping(uint acc, bool inh, string name);

    static string NameOf(IntPtr h)
    {
        int len = 0x1000;
        IntPtr buf = Marshal.AllocHGlobal(len);
        try
        {
            int ret;
            if (NtQueryObject(h, 1, buf, len, out ret) != 0) return null;
            // UNICODE_STRING { ushort Length; ushort Max; IntPtr Buffer; }
            ushort l = (ushort)Marshal.ReadInt16(buf);
            if (l == 0) return "";
            IntPtr p = Marshal.ReadIntPtr(buf, IntPtr.Size);
            return Marshal.PtrToStringUni(p, l / 2);
        }
        finally { Marshal.FreeHGlobal(buf); }
    }

    public static List<string> Find()
    {
        var res = new List<string>();
        // Mở section DBWIN_BUFFER ở chính tiến trình này để biết kiểu "Section" mang chỉ số nào.
        IntPtr mine = OpenFileMapping(4, false, "DBWIN_BUFFER");
        if (mine == IntPtr.Zero) { res.Add("Không có DBWIN_BUFFER — không ai đang nghe debug output"); return res; }

        int size = 1 << 22;
        IntPtr buf = IntPtr.Zero;
        while (true)
        {
            buf = Marshal.AllocHGlobal(size);
            int ret;
            int st = NtQuerySystemInformation(64, buf, size, out ret);
            if (st == unchecked((int)0xC0000004)) { Marshal.FreeHGlobal(buf); size *= 2; continue; }
            if (st != 0) { res.Add("NtQuerySystemInformation lỗi " + st.ToString("X")); return res; }
            break;
        }
        long count = Marshal.ReadIntPtr(buf).ToInt64();
        int off = IntPtr.Size * 2;
        int esz = Marshal.SizeOf(typeof(HANDLE_EX));
        int self = Process.GetCurrentProcess().Id;

        ushort sectionType = 0;
        IntPtr dbwinObj = IntPtr.Zero;
        for (long i = 0; i < count; i++)
        {
            var e = (HANDLE_EX)Marshal.PtrToStructure(new IntPtr(buf.ToInt64() + off + i * esz), typeof(HANDLE_EX));
            if (e.UniqueProcessId.ToInt64() == self && e.HandleValue == mine) { sectionType = e.ObjectTypeIndex; dbwinObj = e.Object; break; }
        }
        for (long i = 0; i < count; i++)
        {
            var e = (HANDLE_EX)Marshal.PtrToStructure(new IntPtr(buf.ToInt64() + off + i * esz), typeof(HANDLE_EX));
            int pid = (int)e.UniqueProcessId.ToInt64();
            if (pid == self || e.ObjectTypeIndex != sectionType) continue;
            // Cùng một đối tượng kernel thì cùng địa chỉ Object (nếu hệ thống không giấu địa chỉ).
            bool match = dbwinObj != IntPtr.Zero && e.Object == dbwinObj;
            if (!match && dbwinObj != IntPtr.Zero) continue;
            string pname;
            try { pname = Process.GetProcessById(pid).ProcessName; } catch { pname = "?"; }
            res.Add(pname + " (PID " + pid + ")");
        }
        CloseHandle(mine);
        Marshal.FreeHGlobal(buf);
        if (res.Count == 0) res.Add("Không tìm thấy tiến trình nào khác giữ DBWIN_BUFFER (có thể thuộc tiến trình chạy quyền admin)");
        return res;
    }
}
'@
Add-Type -TypeDefinition $src
[DbWin]::Find()
