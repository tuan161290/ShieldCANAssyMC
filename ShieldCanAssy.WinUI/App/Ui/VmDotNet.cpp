#include "pch.h"
#include "Ui/VmDotNet.h"

#include <format>

#include <metahost.h>
#include <mscoree.h>

#include "Base/Text.h"

// Giao diện COM của mscorlib (_AppDomain, _Assembly, _Type) — type library đăng ký sẵn cùng .NET Framework 4.
// #import không chạy được với /MP: file này tắt MultiProcessorCompilation trong ShieldCanAssy.vcxproj.
#import "libid:BED7F4EA-1A96-11D2-8F08-00A0C9A6186D" version("2.4") lcid("0") raw_interfaces_only no_smart_pointers \
    rename("ReportEvent", "ReportEventManaged") rename("or", "InteropServices_or")

namespace
{
    // metahost.h / mscoree.h chỉ khai báo (EXTERN_GUID) — định nghĩa tại chỗ thay vì link mscoree.lib.
    constexpr GUID kClsidClrMetaHost = { 0x9280188d, 0x0e8e, 0x4867, { 0xb3, 0x0c, 0x7f, 0xa8, 0x38, 0x84, 0xe8, 0xde } };
    constexpr GUID kClsidCorRuntimeHost = { 0xcb2f6723, 0xab3a, 0x11d2, { 0x9c, 0x40, 0x00, 0xc0, 0x4f, 0xa3, 0x0a, 0x3e } };

    struct Failed
    {
        std::string What;
    };

    void Check(HRESULT hr, char const* what)
    {
        if (FAILED(hr)) throw Failed{ std::format("{} (hr=0x{:08X})", what, (unsigned)hr) };
    }

    /// <summary>AppDomain mặc định của CLR v4 ĐÃ chạy trong tiến trình — nơi COM kích hoạt các control .NET.</summary>
    ATL::CComPtr<mscorlib::_AppDomain> DefaultDomain()
    {
        // Chỉ dùng CLR có sẵn: chưa control VM nào được tạo thì mscoree.dll chưa nạp, không khởi động CLR chỉ để ghi một chuỗi.
        HMODULE shim = GetModuleHandleW(L"mscoree.dll");
        if (shim == nullptr) throw Failed{ ".NET runtime not loaded yet (no VisionMaster control created)" };
        auto create = reinterpret_cast<CLRCreateInstanceFnPtr>(GetProcAddress(shim, "CLRCreateInstance"));
        if (create == nullptr) throw Failed{ "CLRCreateInstance not found in mscoree.dll" };

        ATL::CComPtr<ICLRMetaHost> meta;
        Check(create(kClsidClrMetaHost, __uuidof(ICLRMetaHost), reinterpret_cast<void**>(&meta)), "CLRCreateInstance");
        ATL::CComPtr<IEnumUnknown> runtimes;
        Check(meta->EnumerateLoadedRuntimes(GetCurrentProcess(), &runtimes), "EnumerateLoadedRuntimes");

        ATL::CComPtr<ICLRRuntimeInfo> v4;
        ATL::CComPtr<IUnknown> item;
        while (!v4 && runtimes->Next(1, &item, nullptr) == S_OK)
        {
            ATL::CComPtr<ICLRRuntimeInfo> info;
            if (SUCCEEDED(item.QueryInterface(&info)))
            {
                wchar_t version[64]{};
                DWORD length = 64;
                if (SUCCEEDED(info->GetVersionString(version, &length)) && wcsncmp(version, L"v4.", 3) == 0) v4 = info;
            }
            item.Release();
        }
        if (!v4) throw Failed{ "no .NET 4 runtime loaded in process" };

        ATL::CComPtr<ICorRuntimeHost> host;
        Check(v4->GetInterface(kClsidCorRuntimeHost, __uuidof(ICorRuntimeHost), reinterpret_cast<void**>(&host)),
              "GetInterface(CorRuntimeHost)");
        ATL::CComPtr<IUnknown> domainUnk;
        Check(host->GetDefaultDomain(&domainUnk), "GetDefaultDomain");
        ATL::CComPtr<mscorlib::_AppDomain> domain;
        Check(domainUnk.QueryInterface(&domain), "QueryInterface(_AppDomain)");
        return domain;
    }

    constexpr auto Flags(int f) { return static_cast<mscorlib::BindingFlags>(f); }

    /// <summary>Ghi SolutionPath của VmSolution.Instance trong MỘT bản VM.Core; true khi đọc lại khớp.</summary>
    bool SetOn(mscorlib::_Type* solutionType, std::wstring const& path)
    {
        using namespace mscorlib;
        ATL::CComVariant none;
        ATL::CComVariant instance;
        Check(solutionType->InvokeMember_3(ATL::CComBSTR(L"Instance"),
                                           Flags(BindingFlags_GetProperty | BindingFlags_Static | BindingFlags_Public),
                                           nullptr, none, nullptr, &instance),
              "VmSolution.Instance");
        if (instance.vt != VT_DISPATCH && instance.vt != VT_UNKNOWN) throw Failed{ "VmSolution.Instance is null" };

        // Đối số: một VARIANT chứa chuỗi. SAFEARRAY sở hữu bản sao (SafeArrayPutElement copy), huỷ cùng mảng.
        SAFEARRAY* args = SafeArrayCreateVector(VT_VARIANT, 0, 1);
        if (args == nullptr) throw Failed{ "SafeArrayCreateVector" };
        struct ArrayGuard { SAFEARRAY* a; ~ArrayGuard() { SafeArrayDestroy(a); } } guard{ args };
        ATL::CComVariant value(path.c_str());
        LONG index = 0;
        Check(SafeArrayPutElement(args, &index, &value), "SafeArrayPutElement");

        // Setter của SolutionPath là private: SetProperty kèm NonPublic; SDK đổi cách khai báo thì gọi thẳng set_SolutionPath.
        ATL::CComVariant ignored;
        HRESULT hr = solutionType->InvokeMember_3(ATL::CComBSTR(L"SolutionPath"),
                                                  Flags(BindingFlags_SetProperty | BindingFlags_Instance | BindingFlags_Public |
                                                        BindingFlags_NonPublic),
                                                  nullptr, instance, args, &ignored);
        if (FAILED(hr))
            Check(solutionType->InvokeMember_3(ATL::CComBSTR(L"set_SolutionPath"),
                                               Flags(BindingFlags_InvokeMethod | BindingFlags_Instance | BindingFlags_Public |
                                                     BindingFlags_NonPublic),
                                               nullptr, instance, args, &ignored),
                  "set VmSolution.SolutionPath");

        ATL::CComVariant readBack;
        Check(solutionType->InvokeMember_3(ATL::CComBSTR(L"SolutionPath"),
                                           Flags(BindingFlags_GetProperty | BindingFlags_Instance | BindingFlags_Public),
                                           nullptr, instance, nullptr, &readBack),
              "read VmSolution.SolutionPath");
        return readBack.vt == VT_BSTR && readBack.bstrVal != nullptr && _wcsicmp(readBack.bstrVal, path.c_str()) == 0;
    }
}

namespace ui
{
    std::string SetVmDotNetSolutionPath(std::string const& solutionFileUtf8, int& updated)
    {
        updated = 0;
        if (solutionFileUtf8.empty()) return "no solution file";
        try
        {
            const std::wstring path = ::base::Widen(solutionFileUtf8);
            ATL::CComPtr<mscorlib::_AppDomain> domain = DefaultDomain();

            SAFEARRAY* assemblies = nullptr;
            Check(domain->GetAssemblies(&assemblies), "AppDomain.GetAssemblies");
            struct ArrayGuard { SAFEARRAY* a; ~ArrayGuard() { if (a) SafeArrayDestroy(a); } } guard{ assemblies };
            LONG lo = 0, hi = -1;
            SafeArrayGetLBound(assemblies, 1, &lo);
            SafeArrayGetUBound(assemblies, 1, &hi);

            int found = 0;
            std::string lastError;
            for (LONG i = lo; i <= hi; ++i)
            {
                ATL::CComPtr<IUnknown> unk;
                if (FAILED(SafeArrayGetElement(assemblies, &i, &unk)) || !unk) continue;
                ATL::CComPtr<mscorlib::_Assembly> assembly;
                if (FAILED(unk.QueryInterface(&assembly))) continue;
                ATL::CComBSTR name;
                if (FAILED(assembly->get_FullName(&name)) || !name || wcsncmp(name, L"VM.Core,", 8) != 0) continue;

                ATL::CComPtr<mscorlib::_Type> type;
                if (FAILED(assembly->GetType_2(ATL::CComBSTR(L"VM.Core.VmSolution"), &type)) || !type) continue;
                ++found;
                try
                {
                    if (SetOn(type, path)) ++updated;
                    else lastError = "SolutionPath read back differs";
                }
                catch (Failed const& f) { lastError = f.What; }
            }
            if (updated > 0) return {};
            if (found == 0) return "VM.Core not loaded in default AppDomain";
            return lastError;
        }
        catch (Failed const& f) { return f.What; }
        catch (std::exception const& e) { return e.what(); }
        catch (...) { return "unknown error"; }
    }
}
