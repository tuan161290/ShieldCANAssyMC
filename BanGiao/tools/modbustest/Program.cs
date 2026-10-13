using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Text;

// Chạy riêng SelfTest.TestServoModbusWrites (private static) bằng reflection — không dựng App,
// không McControl, chỉ ServoService + FakeA6sfPort. In báo cáo của bài đó ra stdout.
static class Program
{
    // WPF_BIN: chạy trên bản sao thư mục bin (bản đã thay ServoControl.dll gài lỗi). LoadFrom lấy
    // phụ thuộc cạnh exe trước cả AssemblyResolve, nên muốn đổi DLL thì phải đổi ngay trong thư mục.
    static readonly string Bin = Environment.GetEnvironmentVariable("WPF_BIN") ??
        @"C:\Users\Tuan\OneDrive\Tuan\2026\Actro\SO1C86-SHIELD CAN ASSY MC\ShieldCANAssyMC\ShieldCanAssy.Wpf\bin\Debug\net48";

    static int Main(string[] args)
    {
        Console.OutputEncoding = Encoding.UTF8;
        AppDomain.CurrentDomain.AssemblyResolve += (s, e) =>
        {
            string p = Path.Combine(Bin, new AssemblyName(e.Name).Name + ".dll");
            return File.Exists(p) ? Assembly.LoadFrom(p) : null;
        };
        var wpf = Assembly.LoadFrom(Path.Combine(Bin, "ShieldCanAssy.Wpf.exe"));
        var t = wpf.GetType("ShieldCanAssy.Wpf.SelfTest", true);
        string name = args.Length > 0 ? args[0] : "TestServoModbusWrites";
        var m = t.GetMethod(name, BindingFlags.NonPublic | BindingFlags.Static);
        m.Invoke(null, null);
        var report = (List<string>)t.GetField("_report", BindingFlags.NonPublic | BindingFlags.Static).GetValue(null);
        foreach (var line in report) Console.WriteLine(line);
        int pass = (int)t.GetField("_pass", BindingFlags.NonPublic | BindingFlags.Static).GetValue(null);
        int fail = (int)t.GetField("_fail", BindingFlags.NonPublic | BindingFlags.Static).GetValue(null);
        Console.WriteLine($"=== {pass} đạt, {fail} không đạt");
        return fail == 0 ? 0 : 1;
    }
}
