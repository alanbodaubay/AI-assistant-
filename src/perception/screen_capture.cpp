#include "perception/screen_capture.h"
#include <windows.h>
#include <gdiplus.h>
#include <algorithm>
#include <cctype>
#include <bit>
#include <chrono>
#include <cstring>
#include <vector>



namespace screenmate::perception {

namespace {

int GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
    UINT num = 0;
    UINT size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;

    std::vector<uint8_t> buffer(size);
    auto* pImageCodecInfo = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buffer.data());
    Gdiplus::GetImageEncoders(num, size, pImageCodecInfo);

    for (UINT j = 0; j < num; ++j) {
        if (wcscmp(pImageCodecInfo[j].MimeType, format) == 0) {
            *pClsid = pImageCodecInfo[j].Clsid;
            return static_cast<int>(j);
        }
    }
    return -1;
}

std::string Utf16ToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
    std::string str(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), str.data(), size_needed, nullptr, nullptr);
    return str;
}

std::string ToLower(const std::string& str) {
    std::string lower = str;
    for (char& c : lower) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return lower;
}

} // namespace

ScreenCapture::ScreenCapture(uint32_t max_dimension, int jpeg_quality)
    : max_dimension_(max_dimension), jpeg_quality_(jpeg_quality) {
    init_gdiplus();
}

ScreenCapture::~ScreenCapture() {
    shutdown_gdiplus();
}

bool ScreenCapture::init_gdiplus() {
    if (gdiplus_initialized_) return true;
    Gdiplus::GdiplusStartupInput input;
    Gdiplus::Status status = Gdiplus::GdiplusStartup(&gdiplus_token_, &input, nullptr);
    gdiplus_initialized_ = (status == Gdiplus::Ok);
    return gdiplus_initialized_;
}

void ScreenCapture::shutdown_gdiplus() {
    if (gdiplus_initialized_) {
        Gdiplus::GdiplusShutdown(gdiplus_token_);
        gdiplus_initialized_ = false;
    }
}

WindowInfo ScreenCapture::GetForegroundWindowInfo() const {
    WindowInfo info;
    HWND hwnd = GetForegroundWindow();
    if (!hwnd || !IsWindow(hwnd)) return info;

    info.hwnd = hwnd;
    info.is_valid = true;

    // Window title
    wchar_t title_buf[512] = {};
    int title_len = GetWindowTextW(hwnd, title_buf, 512);
    if (title_len > 0) {
        info.title = Utf16ToUtf8(std::wstring(title_buf, title_len));
    }

    // Window rect
    GetWindowRect(hwnd, &info.rect);

    // Process image name
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != 0) {
        HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (hProcess) {
            wchar_t proc_buf[MAX_PATH] = {};
            DWORD proc_len = MAX_PATH;
            if (QueryFullProcessImageNameW(hProcess, 0, proc_buf, &proc_len)) {
                info.process_name = Utf16ToUtf8(std::wstring(proc_buf, proc_len));
            }
            CloseHandle(hProcess);
        }
    }

    return info;
}

bool ScreenCapture::IsPrivacyBlocked(const WindowInfo& info, std::string* out_reason) const {
    static const std::vector<std::string> blacklist_keywords = {
        "password", "bitwarden", "1password", "keepass", "lastpass", "dashlane",
        "bank", "banking", "incognito", "private browsing", "inprivate",
        "authenticator", "2fa", "secret", "vault", "credentials", "login",
        "sign in"
    };

    std::string lower_title = ToLower(info.title);
    std::string lower_proc = ToLower(info.process_name);

    for (const auto& kw : blacklist_keywords) {
        if (lower_title.find(kw) != std::string::npos) {
            if (out_reason) *out_reason = "Window title contains privacy keyword: " + kw;
            return true;
        }
        if (lower_proc.find(kw) != std::string::npos) {
            if (out_reason) *out_reason = "Process name contains privacy keyword: " + kw;
            return true;
        }
    }
    return false;
}

CaptureResult ScreenCapture::CaptureForegroundWindow() {
    WindowInfo win = GetForegroundWindowInfo();
    if (!win.is_valid) {
        CaptureResult res;
        res.success = false;
        return res;
    }
    return CaptureWindow(win.hwnd);
}

CaptureResult ScreenCapture::CaptureWindow(HWND hwnd) {
    CaptureResult res;
    auto now = std::chrono::system_clock::now();
    res.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

    if (!hwnd || !IsWindow(hwnd)) {
        res.success = false;
        return res;
    }

    // Populate window metadata
    res.window.hwnd = hwnd;
    res.window.is_valid = true;
    wchar_t title_buf[512] = {};
    int title_len = GetWindowTextW(hwnd, title_buf, 512);
    if (title_len > 0) {
        res.window.title = Utf16ToUtf8(std::wstring(title_buf, title_len));
    }
    GetWindowRect(hwnd, &res.window.rect);

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != 0) {
        HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (hProcess) {
            wchar_t proc_buf[MAX_PATH] = {};
            DWORD proc_len = MAX_PATH;
            if (QueryFullProcessImageNameW(hProcess, 0, proc_buf, &proc_len)) {
                res.window.process_name = Utf16ToUtf8(std::wstring(proc_buf, proc_len));
            }
            CloseHandle(hProcess);
        }
    }

    // Privacy filter gate
    std::string block_reason;
    if (IsPrivacyBlocked(res.window, &block_reason)) {
        res.is_privacy_blocked = true;
        res.privacy_reason = block_reason;
        res.success = false;
        return res;
    }

    int width = res.window.rect.right - res.window.rect.left;
    int height = res.window.rect.bottom - res.window.rect.top;
    if (width <= 16 || height <= 16) {
        res.success = false;
        return res;
    }

    // Capture window pixels via Win32 GDI BitBlt
    HDC hdc_screen = GetDC(nullptr);
    HDC hdc_mem = CreateCompatibleDC(hdc_screen);
    HBITMAP hbm_capture = CreateCompatibleBitmap(hdc_screen, width, height);
    HGDIOBJ hbm_old = SelectObject(hdc_mem, hbm_capture);

    // Prefer BitBlt with CAPTUREBLT
    BitBlt(hdc_mem, 0, 0, width, height, hdc_screen, res.window.rect.left, res.window.rect.top, SRCCOPY | CAPTUREBLT);

    SelectObject(hdc_mem, hbm_old);
    DeleteDC(hdc_mem);
    ReleaseDC(nullptr, hdc_screen);

    if (!init_gdiplus()) {
        DeleteObject(hbm_capture);
        res.success = false;
        return res;
    }

    // Wrap in GDI+ Bitmap
    std::unique_ptr<Gdiplus::Bitmap> src_bitmap(new Gdiplus::Bitmap(hbm_capture, nullptr));
    DeleteObject(hbm_capture);

    if (!src_bitmap || src_bitmap->GetLastStatus() != Gdiplus::Ok) {
        res.success = false;
        return res;
    }

    // Calculate aspect-preserving downscaled dimensions
    int src_w = src_bitmap->GetWidth();
    int src_h = src_bitmap->GetHeight();
    int target_w = src_w;
    int target_h = src_h;

    if (max_dimension_ > 0 && (static_cast<uint32_t>(src_w) > max_dimension_ || static_cast<uint32_t>(src_h) > max_dimension_)) {
        float scale = std::min(static_cast<float>(max_dimension_) / static_cast<float>(src_w),
                               static_cast<float>(max_dimension_) / static_cast<float>(src_h));
        target_w = std::max(1, static_cast<int>(src_w * scale));
        target_h = std::max(1, static_cast<int>(src_h * scale));
    }

    Gdiplus::Bitmap resized_bitmap(target_w, target_h, PixelFormat32bppARGB);
    {
        Gdiplus::Graphics g(&resized_bitmap);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.DrawImage(src_bitmap.get(), 0, 0, target_w, target_h);
    }

    res.width = target_w;
    res.height = target_h;

    // 1. Calculate 64-bit dHash from 9x8 thumbnail
    {
        Gdiplus::Bitmap dhash_thumb(9, 8, PixelFormat32bppARGB);
        Gdiplus::Graphics g_dhash(&dhash_thumb);
        g_dhash.SetInterpolationMode(Gdiplus::InterpolationModeBilinear);
        g_dhash.DrawImage(&resized_bitmap, 0, 0, 9, 8);

        Gdiplus::BitmapData bdata;
        Gdiplus::Rect rc(0, 0, 9, 8);
        if (dhash_thumb.LockBits(&rc, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bdata) == Gdiplus::Ok) {
            res.dhash = ComputeDHash(reinterpret_cast<const uint8_t*>(bdata.Scan0), 9, 8, bdata.Stride);
            dhash_thumb.UnlockBits(&bdata);
        }
    }

    // 2. Encode downscaled image to JPEG memory stream
    CLSID jpeg_clsid;
    if (GetEncoderClsid(L"image/jpeg", &jpeg_clsid) >= 0) {
        IStream* stream = nullptr;
        if (CreateStreamOnHGlobal(nullptr, TRUE, &stream) == S_OK) {
            Gdiplus::EncoderParameters encoder_params;
            encoder_params.Count = 1;
            encoder_params.Parameter[0].Guid = Gdiplus::EncoderQuality;
            encoder_params.Parameter[0].Type = Gdiplus::EncoderParameterValueTypeLong;
            encoder_params.Parameter[0].NumberOfValues = 1;
            ULONG quality = static_cast<ULONG>(jpeg_quality_);
            encoder_params.Parameter[0].Value = &quality;

            if (resized_bitmap.Save(stream, &jpeg_clsid, &encoder_params) == Gdiplus::Ok) {
                STATSTG stat;
                if (stream->Stat(&stat, STATFLAG_NONAME) == S_OK) {
                    ULONG bytes_size = static_cast<ULONG>(stat.cbSize.QuadPart);
                    res.jpeg_bytes.resize(bytes_size);

                    LARGE_INTEGER li_zero;
                    li_zero.QuadPart = 0;
                    stream->Seek(li_zero, STREAM_SEEK_SET, nullptr);

                    ULONG bytes_read = 0;
                    stream->Read(res.jpeg_bytes.data(), bytes_size, &bytes_read);
                    res.base64_jpeg = Base64Encode(res.jpeg_bytes.data(), res.jpeg_bytes.size());
                    res.success = true;
                }
            }
            stream->Release();
        }
    }

    return res;
}

uint64_t ScreenCapture::ComputeDHash(const uint8_t* bgra_pixels, int width, int height, int stride) {
    if (!bgra_pixels || width < 9 || height < 8) return 0;

    // Build 9x8 luminance matrix
    float lum[8][9];
    for (int y = 0; y < 8; ++y) {
        const uint8_t* row = bgra_pixels + (y * stride);
        for (int x = 0; x < 9; ++x) {
            uint8_t b = row[x * 4 + 0];
            uint8_t g = row[x * 4 + 1];
            uint8_t r = row[x * 4 + 2];
            lum[y][x] = 0.299f * r + 0.587f * g + 0.114f * b;
        }
    }

    // 64-bit comparison: lum[y][x] > lum[y][x + 1]
    uint64_t hash = 0;
    int bit_index = 0;
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            if (lum[y][x] > lum[y][x + 1]) {
                hash |= (1ULL << bit_index);
            }
            bit_index++;
        }
    }
    return hash;
}

float ScreenCapture::HashDifference(uint64_t hash1, uint64_t hash2) {
    uint64_t xor_diff = hash1 ^ hash2;
    int bits = std::popcount(xor_diff);
    return static_cast<float>(bits) / 64.0f;
}

std::string ScreenCapture::Base64Encode(const uint8_t* data, size_t length) {
    static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    if (length == 0 || !data) return "";

    std::string out;
    out.reserve(((length + 2) / 3) * 4);

    for (size_t i = 0; i < length; i += 3) {
        uint32_t octet_a = data[i];
        uint32_t octet_b = (i + 1 < length) ? data[i + 1] : 0;
        uint32_t octet_c = (i + 2 < length) ? data[i + 2] : 0;

        uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;

        out.push_back(b64_table[(triple >> 18) & 0x3F]);
        out.push_back(b64_table[(triple >> 12) & 0x3F]);

        if (i + 1 < length) {
            out.push_back(b64_table[(triple >> 6) & 0x3F]);
        } else {
            out.push_back('=');
        }

        if (i + 2 < length) {
            out.push_back(b64_table[triple & 0x3F]);
        } else {
            out.push_back('=');
        }
    }

    return out;
}

} // namespace screenmate::perception

