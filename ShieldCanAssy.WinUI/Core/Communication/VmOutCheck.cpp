#include "Communication/VmOutCheck.h"

#include "Base/Text.h"
#include "Lang/Loc.h"

#include <algorithm>
#include <climits>
#include <map>

namespace sca
{
    namespace
    {
        const char* const NL = "\r\n";   // StringBuilder.AppendLine trên Windows

        std::string TrimEnd(std::string s)
        {
            while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.pop_back();
            return s;
        }

        bool Has(const std::vector<std::string>& list, const std::string& name)
        {
            return std::find(list.begin(), list.end(), name) != list.end();
        }

        /// Số ký tự (code point) của chuỗi UTF-8 — thay cho string.Length khi canh cột.
        size_t CharCount(const std::string& s)
        {
            size_t n = 0;
            for (unsigned char c : s) if ((c & 0xC0) != 0x80) ++n;
            return n;
        }

        std::string Pad(const std::string& s, size_t width)
        {
            size_t len = CharCount(s);
            return len >= width ? s + " " : s + std::string(width - len, ' ');
        }

        std::vector<std::string> DistinctFlows(const std::vector<const VmOutItem*>& items)
        {
            std::vector<std::string> flows;
            for (auto* i : items) if (!Has(flows, i->Flow)) flows.push_back(i->Flow);
            return flows;
        }

        VmScriptExpect Make(const std::string& flow, const char* script, const std::vector<std::string>& ins, const std::vector<std::string>& outs)
        {
            VmScriptExpect e;
            e.Flow = flow;
            e.ScriptName = script;
            e.Inputs = ins;
            e.Outputs = outs;
            return e;
        }

        /// Một tab tham số: thiếu dòng nào, dòng nào gõ sai tên, dòng nào bỏ trống Subscribe.
        void CompareIoTab(const std::string& scope, const std::vector<std::string>& expected, const std::vector<VmIoInfo>& declared,
                          bool checkBind, VmOutCheckReport& report)
        {
            std::map<std::string, const VmIoInfo*> byName;
            for (auto& d : declared)
                if (!d.Name.empty()) byName[d.Name] = &d;

            std::vector<std::string> missing;
            for (auto& name : expected)
            {
                auto it = byName.find(name);
                if (it == byName.end())
                {
                    missing.push_back(name);
                    report.Items.push_back(VmOutItem{ scope, name, EVmOutState::Missing, {}, {} });
                    continue;
                }
                const VmIoInfo& d = *it->second;
                report.Items.push_back(VmOutItem{ scope, name, checkBind && !d.Bound() ? EVmOutState::Unbound : EVmOutState::Ok, d.TypeText, {} });
            }

            for (auto& d : declared)
            {
                if (d.Name.empty()) continue;
                if (Has(expected, d.Name)) continue;

                auto suggest = VmOutCheck::SuggestFor(d.Name, missing);
                report.Items.push_back(VmOutItem{ scope, d.Name, suggest ? EVmOutState::Typo : EVmOutState::Extra, d.TypeText, suggest.value_or("") });
            }
        }
    }

    VmFlowOutputs VmFlowOutputs::Fail(const std::string& flow, const std::string& why)
    {
        VmFlowOutputs r;
        r.Flow = flow;
        r.Error = why;
        return r;
    }

    void VmFlowOutputs::AddDeclared(const std::string& name, int typeCode)
    {
        Declared.push_back(VmDeclaredOut{ name, typeCode, VmOutCheck::TypeName(typeCode), VmOutCheck::IsNumeric(typeCode) });
    }

    const std::vector<std::string>& VmScriptContract::TopAbsIn()
    {
        static const std::vector<std::string> v{ "TransPointX_mm", "TransPointY_mm", "TransPointA_deg",
                                                 "EchoX_mm", "EchoY2_mm", "EchoRunId", "MatchNum" };
        return v;
    }

    const std::vector<std::string>& VmScriptContract::TopAbsOut()
    {
        static const std::vector<std::string> v{ "TopFound", "TopX_mm", "TopY_mm", "TopA_deg", "TopRunId" };
        return v;
    }

    const std::vector<std::string>& VmScriptContract::CmdIn()
    {
        static const std::vector<std::string> v{ "HasTarget", "TarX_mm", "TarY_mm", "AlignSign",
                                                 "DeltaX_mm", "DeltaY_mm", "DeltaTheta_deg", "SpaStatus",
                                                 "ObjX_mm", "ObjY_mm", "EchoA_deg", "EchoRunId" };
        return v;
    }

    const std::vector<std::string>& VmScriptContract::CmdOut()
    {
        static const std::vector<std::string> v{ "Status", "CmdX_mm", "CmdY2_mm", "CmdA_deg",
                                                 "DiagDA_deg", "DiagEcc_mm", "DiagTarX_mm", "DiagTarY_mm", "ObjRunId" };
        return v;
    }

    std::vector<VmScriptExpect> VmScriptContract::Build(const VisionOutputNames& n)
    {
        std::vector<VmScriptExpect> list;
        list.push_back(Make(n.FlowLocateTop, "TopAbs", TopAbsIn(), TopAbsOut()));
        list.push_back(Make(n.FlowAlignBottomTool1, "Cmd", CmdIn(), CmdOut()));
        list.push_back(Make(n.FlowAlignBottomTool2, "Cmd", CmdIn(), CmdOut()));
        return list;
    }

    int VmOutCheckReport::Count(EVmOutState state) const
    {
        return static_cast<int>(std::count_if(Items.begin(), Items.end(), [state](const VmOutItem& i) { return i.State == state; }));
    }

    bool VmOutCheckReport::Clean() const
    {
        return Error.empty() && FlowErrors.empty() &&
               Count(EVmOutState::Missing) == 0 && Count(EVmOutState::WrongType) == 0 &&
               Count(EVmOutState::Typo) == 0 && Count(EVmOutState::Unbound) == 0;
    }

    VmOutCheckReport VmOutCheckReport::Fail(const std::string& error)
    {
        VmOutCheckReport r;
        r.Error = error;
        return r;
    }

    std::string VmOutCheckReport::ToText() const
    {
        if (!Error.empty()) return Loc::F("Vm.ReportError", Error);

        std::string sb;
        sb += Loc::F("Vm.OutOkCount", Count(EVmOutState::Ok));
        sb += Loc::F("Vm.CountMissing", Count(EVmOutState::Missing));
        if (Count(EVmOutState::Typo) > 0) sb += Loc::F("Vm.OutTypoCount", Count(EVmOutState::Typo));
        if (Count(EVmOutState::WrongType) > 0) sb += Loc::F("Vm.CountWrongType", Count(EVmOutState::WrongType));
        if (Count(EVmOutState::Unbound) > 0) sb += Loc::F("Vm.OutUnboundCount", Count(EVmOutState::Unbound));
        if (Count(EVmOutState::Extra) > 0) sb += Loc::F("Vm.OutExtraCount", Count(EVmOutState::Extra));
        sb += NL;

        for (auto& e : FlowErrors) sb += "✗ " + e + NL;

        // Gom theo Flow, giữ đúng thứ tự hợp đồng để người đọc lần theo được từng tab một.
        std::vector<const VmOutItem*> all;
        for (auto& i : Items) all.push_back(&i);
        for (auto& flow : DistinctFlows(all))
        {
            std::vector<const VmOutItem*> bad;
            for (auto& i : Items)
                if (i.Flow == flow && i.State != EVmOutState::Ok && i.State != EVmOutState::Extra) bad.push_back(&i);
            if (bad.empty()) continue;

            sb += NL;
            sb += "Flow " + flow + ":" + NL;
            for (auto* i : bad)
            {
                switch (i->State)
                {
                case EVmOutState::Missing:
                    sb += Loc::F("Vm.OutLineMissing", i->Name) + NL;
                    break;
                case EVmOutState::Typo:
                    sb += Loc::F("Vm.OutLineTypo", i->Name, i->Suggest) + NL;
                    break;
                case EVmOutState::WrongType:
                    sb += Loc::F("Vm.OutLineWrongType", i->Name, i->TypeText) + NL;
                    break;
                case EVmOutState::Unbound:
                    sb += Loc::F("Vm.OutLineUnbound", i->Name) + NL;
                    break;
                default:
                    break;
                }
            }
        }

        std::vector<const VmOutItem*> extras;
        for (auto& i : Items) if (i.State == EVmOutState::Extra) extras.push_back(&i);
        if (!extras.empty())
        {
            sb += NL;
            sb += Loc::T("Vm.OutExtraHeader") + NL;
            for (auto& flow : DistinctFlows(extras))
            {
                std::vector<std::string> names;
                for (auto* i : extras) if (i->Flow == flow) names.push_back(i->Name);
                sb += "  · " + flow + ": " + base::Join(names, ", ") + NL;
            }
        }

        for (auto& b : ScriptBlocks)
        {
            sb += NL;
            sb += TrimEnd(b) + NL;
        }

        if (!Notes.empty())
        {
            sb += NL;
            for (auto& n : Notes) sb += "· " + n + NL;
        }

        if (Clean() && !Items.empty()) sb += std::string(NL) + Loc::T("Vm.OutAllClean");
        return TrimEnd(sb);
    }

    std::string VmOutCheck::TypeName(int code)
    {
        switch (code)
        {
        case TypeInt: return "int";
        case TypeFloat: return "float";
        case 1: return Loc::T("Vm.TypeImage");
        case 15: return Loc::T("Vm.TypeString");
        case 25: return "double";
        default: return Loc::F("Vm.TypeCode", code);
        }
    }

    std::string VmOutCheck::CleanIoName(const std::string& raw)
    {
        if (raw.size() > 2 && raw.front() == '%' && raw.back() == '%') return raw.substr(1, raw.size() - 2);
        return raw;
    }

    std::string VmOutCheck::Normalize(const std::string& s)
    {
        if (s.empty()) return "";
        std::string t = base::ToLower(base::Trim(s));
        if (base::EndsWith(t, "_mm")) t = t.substr(0, t.size() - 3);
        else if (base::EndsWith(t, "_deg")) t = t.substr(0, t.size() - 4);
        return base::Replace(t, "_", "");
    }

    int VmOutCheck::EditDistance(const std::string& a, const std::string& b)
    {
        if (a.empty()) return static_cast<int>(b.size());
        if (b.empty()) return static_cast<int>(a.size());

        std::vector<int> prev(b.size() + 1), cur(b.size() + 1);
        for (size_t j = 0; j <= b.size(); j++) prev[j] = static_cast<int>(j);

        for (size_t i = 1; i <= a.size(); i++)
        {
            cur[0] = static_cast<int>(i);
            for (size_t j = 1; j <= b.size(); j++)
            {
                int cost = a[i - 1] == b[j - 1] ? 0 : 1;
                cur[j] = std::min(std::min(cur[j - 1] + 1, prev[j] + 1), prev[j - 1] + cost);
            }
            prev = cur;
        }
        return prev[b.size()];
    }

    std::optional<std::string> VmOutCheck::SuggestFor(const std::string& declared, const std::vector<std::string>& missing)
    {
        if (declared.empty()) return std::nullopt;
        std::string nd = Normalize(declared);

        std::optional<std::string> best;
        int bestDist = INT_MAX;
        for (auto& m : missing)
        {
            std::string nm = Normalize(m);
            int d = nd == nm ? 0 : EditDistance(nd, nm);
            // Tên ngắn thì một chữ khác nhau đã là tên khác hẳn; tên dài mới cho sai hai chữ.
            int limit = nm.size() >= 5 ? 2 : 1;
            if (d > limit || d >= bestDist) continue;
            best = m;
            bestDist = d;
        }
        return best;
    }

    void VmOutCheck::CompareFlow(const VmFlowExpect& expect, const VmFlowOutputs* actual, VmOutCheckReport& report)
    {
        const std::string& flow = expect.Flow;

        if (actual == nullptr || !actual->Error.empty())
        {
            report.FlowErrors.push_back(flow + ": " + (actual == nullptr ? Loc::T("Vm.FlowUnreadable") : actual->Error));
            return;
        }
        if (actual->Declared.empty())
        {
            report.FlowErrors.push_back(Loc::F("Vm.FlowNoOutputs", flow));
            return;
        }

        std::map<std::string, const VmDeclaredOut*> byName;
        for (auto& d : actual->Declared)
            if (!d.Name.empty()) byName[d.Name] = &d;

        std::vector<std::string> missing;
        for (auto& name : expect.Names)
        {
            auto it = byName.find(name);
            if (it == byName.end())
            {
                missing.push_back(name);
                report.Items.push_back(VmOutItem{ flow, name, EVmOutState::Missing, {}, {} });
                continue;
            }
            const VmDeclaredOut& d = *it->second;
            report.Items.push_back(VmOutItem{ flow, name, d.Numeric ? EVmOutState::Ok : EVmOutState::WrongType, d.TypeText, {} });
        }

        for (auto& d : actual->Declared)
        {
            if (d.Name.empty()) continue;
            if (Has(expect.Names, d.Name)) continue;
            if (!expect.ImageName.empty() && d.Name == expect.ImageName) continue;

            auto suggest = SuggestFor(d.Name, missing);
            report.Items.push_back(VmOutItem{ flow, d.Name, suggest ? EVmOutState::Typo : EVmOutState::Extra, d.TypeText, suggest.value_or("") });
        }

        // Hợp đồng bỏ Input Params từ 2026-09-14 — mọi số App gửi sang đều đi qua biến toàn cục.
        // Còn sót thì Flow vẫn chạy bình thường, chỉ là rác của bản cũ.
        if (actual->InputParamCount > 0)
            report.Notes.push_back(Loc::F("Vm.FlowInputParamsLeft", flow, actual->InputParamCount));

        if (!expect.Note.empty() && !missing.empty())
            report.Notes.push_back(flow + ": " + expect.Note);
    }

    const VmModuleIo* VmOutCheck::FindScript(const VmFlowOutputs* flow, const VmScriptExpect& expect)
    {
        if (flow == nullptr) return nullptr;

        const VmModuleIo* best = nullptr;
        int bestHits = 0;
        for (auto& m : flow->Modules)
        {
            int hits = 0;
            for (auto& o : m.Outputs) if (Has(expect.Outputs, o.Name)) ++hits;
            if (hits > bestHits) { best = &m; bestHits = hits; }
        }
        return bestHits > 0 ? best : nullptr;
    }

    void VmOutCheck::CompareScript(const VmScriptExpect& expect, const VmFlowOutputs* flow, VmOutCheckReport& report)
    {
        if (flow == nullptr || !flow->Error.empty()) return;   // lỗi Flow đã báo ở CompareFlow

        if (flow->Modules.empty())
        {
            report.Notes.push_back(Loc::F("Vm.ScriptNoModules", expect.Flow));
            return;
        }

        const VmModuleIo* script = FindScript(flow, expect);
        if (script == nullptr)
        {
            // Kèm luôn tên output mà các module Script ĐANG khai — lần sau lệch cách gọi tên nào
            // nữa thì đọc báo cáo là thấy ngay, không phải đoán.
            std::vector<std::string> scriptLike, moduleNames;
            for (auto& m : flow->Modules)
            {
                moduleNames.push_back(m.Name);
                if (!base::Contains(base::ToLower(m.Name), "script")) continue;
                std::vector<std::string> outs;
                for (auto& o : m.Outputs) outs.push_back(o.Name);
                scriptLike.push_back(Loc::F("Vm.ScriptDeclares", m.Name, outs.empty() ? Loc::T("Vm.ScriptNoOutputs") : base::Join(outs, ", ")));
            }
            report.FlowErrors.push_back(Loc::F("Vm.ScriptNotFound", expect.Flow, expect.ScriptName, base::Join(moduleNames, ", ")) +
                                        (scriptLike.empty() ? std::string() : " — " + base::Join(scriptLike, "; ")));
            return;
        }

        std::string scope = expect.Flow + " · Script " + script->Name;
        CompareIoTab(scope + " (Output Params)", expect.Outputs, script->Outputs, false, report);
        CompareIoTab(scope + " (Input Params)", expect.Inputs, script->Inputs, true, report);

        std::string sb = Loc::F("Vm.ScriptBlockHead", script->Name, expect.Flow, static_cast<int>(script->Inputs.size()),
                                static_cast<int>(script->Outputs.size())) + NL;
        for (auto& io : script->Inputs)
            sb += "    " + Pad(io.Name, 16) + Pad(io.TypeText, 7) + (io.Bound() ? "← " + io.Source : Loc::T("Vm.ScriptNotBound")) + NL;
        report.ScriptBlocks.push_back(sb);
    }
}
