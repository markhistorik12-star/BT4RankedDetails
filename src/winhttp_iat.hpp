#pragma once
#include <windows.h>
#include <winhttp.h>
#include <string>
#include <unordered_map>
#include <mutex>
#include <functional>
#include <cstring>

namespace bt4rd {

struct HttpObservedEvent {
    std::string path;
    std::string request_body;
    std::string response_body;
};

using HttpEventCallback = std::function<void(const HttpObservedEvent&)>;

class WinHttpIatObserver {
public:
    static WinHttpIatObserver& instance() {
        static WinHttpIatObserver inst;
        return inst;
    }

    bool install(HttpEventCallback cb) {
        std::lock_guard<std::mutex> lk(mu_);
        if (installed_) return true;
        cb_ = std::move(cb);

        HMODULE exe = GetModuleHandleW(nullptr);
        if (!exe) return false;

        int patched = 0;
        patched += patch_iat(exe, "WINHTTP.dll", "WinHttpOpenRequest", (void*)&hook_OpenRequest, (void**)&orig_OpenRequest);
        patched += patch_iat(exe, "WINHTTP.dll", "WinHttpSendRequest", (void*)&hook_SendRequest, (void**)&orig_SendRequest);
        patched += patch_iat(exe, "WINHTTP.dll", "WinHttpReadData",    (void*)&hook_ReadData,    (void**)&orig_ReadData);
        patched += patch_iat(exe, "WINHTTP.dll", "WinHttpCloseHandle", (void*)&hook_CloseHandle, (void**)&orig_CloseHandle);

        installed_ = (patched >= 3);
        return installed_;
    }

    bool is_installed() const { return installed_; }

private:
    struct ReqState {
        std::string path;
        std::string req_body;
        std::string resp_body;
        bool interesting = false;
    };

    using FnOpenRequest = HINTERNET (WINAPI*)(HINTERNET, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR*, DWORD);
    using FnSendRequest = BOOL (WINAPI*)(HINTERNET, LPCWSTR, DWORD, LPVOID, DWORD, DWORD, DWORD_PTR);
    using FnReadData    = BOOL (WINAPI*)(HINTERNET, LPVOID, DWORD, LPDWORD);
    using FnCloseHandle = BOOL (WINAPI*)(HINTERNET);

    static inline FnOpenRequest orig_OpenRequest = nullptr;
    static inline FnSendRequest orig_SendRequest = nullptr;
    static inline FnReadData    orig_ReadData    = nullptr;
    static inline FnCloseHandle orig_CloseHandle = nullptr;

    std::mutex mu_;
    std::unordered_map<HINTERNET, ReqState> reqs_;
    HttpEventCallback cb_;
    bool installed_ = false;

    static std::string wide_to_utf8(LPCWSTR ws) {
        if (!ws || !*ws) return {};
        int n = WideCharToMultiByte(CP_UTF8, 0, ws, -1, nullptr, 0, nullptr, nullptr);
        if (n <= 1) return {};
        std::string out(n - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, ws, -1, out.data(), n, nullptr, nullptr);
        return out;
    }

    static bool is_interesting_path(const std::string& p) {
        return p.find("bt4_ranked_api") != std::string::npos ||
               p.find("ranked") != std::string::npos ||
               p.find("room") != std::string::npos ||
               p.find("match") != std::string::npos ||
               p.find("queue") != std::string::npos;
    }

    static HINTERNET WINAPI hook_OpenRequest(HINTERNET hConnect, LPCWSTR verb, LPCWSTR objName,
                                             LPCWSTR version, LPCWSTR referrer, LPCWSTR* accept, DWORD flags) {
        HINTERNET h = orig_OpenRequest(hConnect, verb, objName, version, referrer, accept, flags);
        if (h && objName) {
            std::string path = wide_to_utf8(objName);
            if (is_interesting_path(path)) {
                auto& self = instance();
                std::lock_guard<std::mutex> lk(self.mu_);
                ReqState st;
                st.path = std::move(path);
                st.interesting = true;
                self.reqs_[h] = std::move(st);
            }
        }
        return h;
    }

    static BOOL WINAPI hook_SendRequest(HINTERNET hRequest, LPCWSTR headers, DWORD headersLen,
                                        LPVOID opt, DWORD optLen, DWORD totalLen, DWORD_PTR ctx) {
        if (hRequest && opt && optLen > 0 && optLen < 65536) {
            auto& self = instance();
            std::lock_guard<std::mutex> lk(self.mu_);
            auto it = self.reqs_.find(hRequest);
            if (it != self.reqs_.end() && it->second.interesting) {
                it->second.req_body.assign(reinterpret_cast<const char*>(opt), optLen);
            }
        }
        return orig_SendRequest(hRequest, headers, headersLen, opt, optLen, totalLen, ctx);
    }

    static BOOL WINAPI hook_ReadData(HINTERNET hRequest, LPVOID buf, DWORD toRead, LPDWORD bytesRead) {
        BOOL ok = orig_ReadData(hRequest, buf, toRead, bytesRead);
        if (ok && hRequest && buf && bytesRead && *bytesRead > 0) {
            auto& self = instance();
            std::lock_guard<std::mutex> lk(self.mu_);
            auto it = self.reqs_.find(hRequest);
            if (it != self.reqs_.end() && it->second.interesting && it->second.resp_body.size() < 262144) {
                it->second.resp_body.append(reinterpret_cast<const char*>(buf), *bytesRead);
            }
        }
        return ok;
    }

    static BOOL WINAPI hook_CloseHandle(HINTERNET h) {
        HttpObservedEvent ev;
        bool fire = false;
        HttpEventCallback cb_copy;
        if (h) {
            auto& self = instance();
            std::lock_guard<std::mutex> lk(self.mu_);
            auto it = self.reqs_.find(h);
            if (it != self.reqs_.end()) {
                if (it->second.interesting && (!it->second.resp_body.empty() || !it->second.req_body.empty())) {
                    ev.path = std::move(it->second.path);
                    ev.request_body = std::move(it->second.req_body);
                    ev.response_body = std::move(it->second.resp_body);
                    cb_copy = self.cb_;
                    fire = (bool)cb_copy;
                }
                self.reqs_.erase(it);
            }
        }
        BOOL res = orig_CloseHandle(h);
        if (fire) {
            try { cb_copy(ev); } catch (...) {}
        }
        return res;
    }

    static int patch_iat(HMODULE mod, const char* dllName, const char* funcName, void* hookFn, void** origOut) {
        auto* base = reinterpret_cast<uint8_t*>(mod);
        auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
        auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
        auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (!dir.VirtualAddress) return 0;

        auto* imp = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress);
        for (; imp->Name; ++imp) {
            const char* name = reinterpret_cast<const char*>(base + imp->Name);
            if (_stricmp(name, dllName) != 0) continue;

            auto* ilt = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));
            auto* iat = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + imp->FirstThunk);
            for (; ilt->u1.AddressOfData; ++ilt, ++iat) {
                if (IMAGE_SNAP_BY_ORDINAL64(ilt->u1.Ordinal)) continue;
                auto* ibn = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + ilt->u1.AddressOfData);
                if (std::strcmp(reinterpret_cast<const char*>(ibn->Name), funcName) == 0) {
                    DWORD oldProt = 0;
                    if (VirtualProtect(&iat->u1.Function, sizeof(void*), PAGE_READWRITE, &oldProt)) {
                        if (origOut) *origOut = reinterpret_cast<void*>(iat->u1.Function);
                        iat->u1.Function = reinterpret_cast<ULONGLONG>(hookFn);
                        VirtualProtect(&iat->u1.Function, sizeof(void*), oldProt, &oldProt);
                        return 1;
                    }
                }
            }
        }
        return 0;
    }
};

} // namespace bt4rd
