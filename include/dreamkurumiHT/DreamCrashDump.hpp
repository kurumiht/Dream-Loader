#pragma once
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>
#include "DreamLog.hpp"

namespace dreamkurumi {

// ============================================================================
// DreamCrashDump - Auto Crash Report & Stack Trace Analyzer
// Captures unhandled exceptions, faulting module, RVA, registers, and callstack
// ============================================================================
struct CrashFrame {
    uint32_t frameIndex{0};
    uintptr_t address{0};
    std::string moduleName{"Unknown"};
    uintptr_t rva{0};
};

struct CrashInfo {
    uint32_t exceptionCode{0};
    std::string exceptionName;
    uintptr_t faultingAddress{0};
    std::string faultingModule{"Unknown"};
    uintptr_t faultingRva{0};
    uintptr_t targetMemoryAddress{0};
    int accessType{-1}; // 0 = Read, 1 = Write, 8 = Execute

    // Registers
    uint64_t rip{0};
    uint64_t rsp{0};
    uint64_t rbp{0};
    uint64_t rax{0};
    uint64_t rbx{0};
    uint64_t rcx{0};
    uint64_t rdx{0};
    uint64_t rsi{0};
    uint64_t rdi{0};
    uint64_t r8{0};
    uint64_t r9{0};
    uint64_t r10{0};
    uint64_t r11{0};
    uint64_t r12{0};
    uint64_t r13{0};
    uint64_t r14{0};
    uint64_t r15{0};
    uint32_t eflags{0};

    std::vector<CrashFrame> callstack;
    std::vector<uint8_t> codeBytes;
};

class DreamCrashDump {
public:
    static DreamCrashDump& Instance() {
        static DreamCrashDump s_instance;
        return s_instance;
    }

    // Capture crash from exception pointers and write crash report to disk
    std::string CaptureCrash(PEXCEPTION_POINTERS pExceptionPointers, const std::string& customReason = "") {
        if (!pExceptionPointers || !pExceptionPointers->ExceptionRecord || !pExceptionPointers->ContextRecord) {
            return "Invalid exception pointers";
        }

        CrashInfo info;
        PEXCEPTION_RECORD pRec = pExceptionPointers->ExceptionRecord;
        PCONTEXT pCtx = pExceptionPointers->ContextRecord;

        info.exceptionCode = pRec->ExceptionCode;
        info.exceptionName = GetExceptionDescription(pRec->ExceptionCode);
        info.faultingAddress = reinterpret_cast<uintptr_t>(pRec->ExceptionAddress);

        // Resolve faulting module and RVA
        ResolveModuleAndRva(info.faultingAddress, info.faultingModule, info.faultingRva);

        // Access violation target
        if (pRec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && pRec->NumberParameters >= 2) {
            info.accessType = static_cast<int>(pRec->ExceptionInformation[0]);
            info.targetMemoryAddress = static_cast<uintptr_t>(pRec->ExceptionInformation[1]);
        }

        // Copy CPU Registers
        info.rip = pCtx->Rip;
        info.rsp = pCtx->Rsp;
        info.rbp = pCtx->Rbp;
        info.rax = pCtx->Rax;
        info.rbx = pCtx->Rbx;
        info.rcx = pCtx->Rcx;
        info.rdx = pCtx->Rdx;
        info.rsi = pCtx->Rsi;
        info.rdi = pCtx->Rdi;
        info.r8  = pCtx->R8;
        info.r9  = pCtx->R9;
        info.r10 = pCtx->R10;
        info.r11 = pCtx->R11;
        info.r12 = pCtx->R12;
        info.r13 = pCtx->R13;
        info.r14 = pCtx->R14;
        info.r15 = pCtx->R15;
        info.eflags = pCtx->EFlags;

        // Capture surrounding code bytes around RIP
        info.codeBytes = ReadSafeMemory(info.rip - 8, 32);

        // Walk callstack backtrace
        CaptureCallstack(info.callstack);

        // Format and save report
        std::string report = FormatReport(info, customReason);
        m_lastReport = report;

        std::string filename = GenerateReportFileName();
        WriteReportFile(filename, report);

        DREAM_LOG_ERROR("DreamCrashDump: Fatal exception captured (%s at %s+0x%llX)! Crash report saved to '%s'",
                        info.exceptionName.c_str(), info.faultingModule.c_str(),
                        static_cast<unsigned long long>(info.faultingRva), filename.c_str());

        return report;
    }

    const std::string& GetLastReport() const { return m_lastReport; }

    // Install global top-level unhandled exception filter
    void InstallGlobalHandler() {
        if (m_handlerInstalled) return;
        m_previousFilter = SetUnhandledExceptionFilter(UnhandledExceptionFilterWrapper);
        m_handlerInstalled = true;
        DREAM_LOG_INFO("DreamCrashDump: Global unhandled exception filter installed");
    }

    void UninstallGlobalHandler() {
        if (!m_handlerInstalled) return;
        SetUnhandledExceptionFilter(m_previousFilter);
        m_handlerInstalled = false;
        DREAM_LOG_INFO("DreamCrashDump: Global unhandled exception filter uninstalled");
    }

    // Helper: resolve any address to ModuleName + RVA
    static void ResolveModuleAndRva(uintptr_t addr, std::string& outModuleName, uintptr_t& outRva) {
        HMODULE hMod = NULL;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCSTR>(addr), &hMod) && hMod) {
            char path[MAX_PATH] = {0};
            if (GetModuleFileNameA(hMod, path, MAX_PATH)) {
                std::string fullPath = path;
                size_t slash = fullPath.find_last_of("\\/");
                outModuleName = (slash != std::string::npos) ? fullPath.substr(slash + 1) : fullPath;
            } else {
                outModuleName = "UnknownModule";
            }
            outRva = addr - reinterpret_cast<uintptr_t>(hMod);
        } else {
            outModuleName = "Unknown/JIT";
            outRva = addr;
        }
    }

private:
    DreamCrashDump() = default;

    static LONG WINAPI UnhandledExceptionFilterWrapper(PEXCEPTION_POINTERS pEx) {
        DreamCrashDump::Instance().CaptureCrash(pEx, "Unhandled System Exception");
        return EXCEPTION_CONTINUE_SEARCH;
    }

    static std::string GetExceptionDescription(uint32_t code) {
        switch (code) {
            case EXCEPTION_ACCESS_VIOLATION:         return "0xC0000005 (Access Violation)";
            case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:    return "0xC000008C (Array Bounds Exceeded)";
            case EXCEPTION_BREAKPOINT:               return "0x80000003 (Breakpoint)";
            case EXCEPTION_DATATYPE_MISALIGNMENT:    return "0x80000002 (Datatype Misalignment)";
            case EXCEPTION_FLT_DIVIDE_BY_ZERO:       return "0xC000008E (Float Divide by Zero)";
            case EXCEPTION_ILLEGAL_INSTRUCTION:      return "0xC000001D (Illegal Instruction)";
            case EXCEPTION_IN_PAGE_ERROR:            return "0xC0000006 (In Page Error)";
            case EXCEPTION_INT_DIVIDE_BY_ZERO:       return "0xC0000094 (Integer Divide by Zero)";
            case EXCEPTION_INT_OVERFLOW:             return "0xC0000095 (Integer Overflow)";
            case EXCEPTION_PRIV_INSTRUCTION:         return "0xC0000096 (Privileged Instruction)";
            case EXCEPTION_STACK_OVERFLOW:           return "0xC00000FD (Stack Overflow)";
            default: {
                char buf[32];
                snprintf(buf, sizeof(buf), "0x%08X (Custom Exception)", code);
                return std::string(buf);
            }
        }
    }

    static std::vector<uint8_t> ReadSafeMemory(uintptr_t addr, size_t size) {
        std::vector<uint8_t> bytes(size, 0);
        SIZE_T bytesRead = 0;
        if (ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(addr), bytes.data(), size, &bytesRead)) {
            bytes.resize(bytesRead);
        } else {
            bytes.clear();
        }
        return bytes;
    }

    static void CaptureCallstack(std::vector<CrashFrame>& outFrames) {
        void* stackBackTrace[32];
        WORD count = CaptureStackBackTrace(0, 32, stackBackTrace, nullptr);
        for (WORD i = 0; i < count; ++i) {
            CrashFrame frame;
            frame.frameIndex = i;
            frame.address = reinterpret_cast<uintptr_t>(stackBackTrace[i]);
            ResolveModuleAndRva(frame.address, frame.moduleName, frame.rva);
            outFrames.push_back(frame);
        }
    }

    static std::string GenerateReportFileName() {
        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm tm;
        localtime_s(&tm, &t);
        char buf[64];
        snprintf(buf, sizeof(buf), "dream_crash_report_%04d%02d%02d_%02d%02d%02d.txt",
                 tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
        return std::string(buf);
    }

    static bool WriteReportFile(const std::string& filename, const std::string& content) {
        FILE* fp = fopen(filename.c_str(), "w");
        if (!fp) return false;
        fputs(content.c_str(), fp);
        fclose(fp);
        return true;
    }

    static std::string FormatReport(const CrashInfo& info, const std::string& reason) {
        std::stringstream ss;
        ss << "================================================================================\n";
        ss << "                     DREAMKURUMI HT CRASH DIAGNOSTICS REPORT                    \n";
        ss << "================================================================================\n\n";

        if (!reason.empty()) {
            ss << "Trigger Reason       : " << reason << "\n";
        }
        ss << "Exception Code       : " << info.exceptionName << "\n";
        ss << "Faulting Address     : 0x" << std::hex << std::uppercase << info.faultingAddress << "\n";
        ss << "Faulting Module      : " << info.faultingModule << " (RVA: +0x" << info.faultingRva << ")\n";

        if (info.accessType != -1) {
            const char* op = (info.accessType == 0) ? "Read" : ((info.accessType == 1) ? "Write" : "Execute");
            ss << "Violation Action     : " << op << " from 0x" << info.targetMemoryAddress << "\n";
        }

        ss << "\n--------------------------------------------------------------------------------\n";
        ss << " CPU REGISTERS (x64 State)\n";
        ss << "--------------------------------------------------------------------------------\n";
        ss << "  RIP: 0x" << std::setw(16) << std::setfill('0') << info.rip << "   RSP: 0x" << std::setw(16) << std::setfill('0') << info.rsp << "\n";
        ss << "  RBP: 0x" << std::setw(16) << std::setfill('0') << info.rbp << "   EFL: 0x" << std::setw(8) << std::setfill('0') << info.eflags << "\n";
        ss << "  RAX: 0x" << std::setw(16) << std::setfill('0') << info.rax << "   RBX: 0x" << std::setw(16) << std::setfill('0') << info.rbx << "\n";
        ss << "  RCX: 0x" << std::setw(16) << std::setfill('0') << info.rcx << "   RDX: 0x" << std::setw(16) << std::setfill('0') << info.rdx << "\n";
        ss << "  RSI: 0x" << std::setw(16) << std::setfill('0') << info.rsi << "   RDI: 0x" << std::setw(16) << std::setfill('0') << info.rdi << "\n";
        ss << "  R8 : 0x" << std::setw(16) << std::setfill('0') << info.r8  << "   R9 : 0x" << std::setw(16) << std::setfill('0') << info.r9  << "\n";
        ss << "  R10: 0x" << std::setw(16) << std::setfill('0') << info.r10 << "   R11: 0x" << std::setw(16) << std::setfill('0') << info.r11 << "\n";
        ss << "  R12: 0x" << std::setw(16) << std::setfill('0') << info.r12 << "   R13: 0x" << std::setw(16) << std::setfill('0') << info.r13 << "\n";
        ss << "  R14: 0x" << std::setw(16) << std::setfill('0') << info.r14 << "   R15: 0x" << std::setw(16) << std::setfill('0') << info.r15 << "\n";

        if (!info.codeBytes.empty()) {
            ss << "\n--------------------------------------------------------------------------------\n";
            ss << " INSTRUCTION BYTE STREAM (Surrounding RIP - 8)\n";
            ss << "--------------------------------------------------------------------------------\n  ";
            for (size_t i = 0; i < info.codeBytes.size(); ++i) {
                if (i == 8) ss << "[*"; // Mark faulting RIP
                ss << std::setw(2) << std::setfill('0') << static_cast<int>(info.codeBytes[i]) << " ";
                if (i == 8) ss << "*] ";
            }
            ss << "\n";
        }

        ss << "\n--------------------------------------------------------------------------------\n";
        ss << " STACK CALLTRACE (Backtrace Frames)\n";
        ss << "--------------------------------------------------------------------------------\n";
        for (const auto& frame : info.callstack) {
            ss << "  #" << std::dec << std::setw(2) << frame.frameIndex << "  0x"
               << std::hex << std::uppercase << std::setw(16) << std::setfill('0') << frame.address
               << "  (" << frame.moduleName << " + 0x" << frame.rva << ")\n";
        }

        ss << "\n================================================================================\n";
        ss << " End of crash report. dreamkurumiHT framework diagnostics.\n";
        ss << "================================================================================\n";

        return ss.str();
    }

    bool m_handlerInstalled{false};
    LPTOP_LEVEL_EXCEPTION_FILTER m_previousFilter{nullptr};
    std::string m_lastReport;
};

} // namespace dreamkurumi
