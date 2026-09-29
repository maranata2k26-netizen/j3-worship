#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include <shellapi.h>
#include <wincrypt.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "BootstrapConfig.h"

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")

namespace
{
struct InternetHandle
{
    HINTERNET value {};
    ~InternetHandle() { if (value != nullptr) WinHttpCloseHandle(value); }
    operator HINTERNET() const noexcept { return value; }
};

std::filesystem::path updaterFolder()
{
    std::array<wchar_t, MAX_PATH + 2> buffer {};
    const auto count = GetTempPathW(static_cast<DWORD>(buffer.size()), buffer.data());
    std::filesystem::path path(count > 0 ? std::wstring(buffer.data(), count) : L".");
    path /= L"J3WorshipUpdater";
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    return path;
}

std::filesystem::path logPath()
{
    return updaterFolder() / L"bootstrap.log";
}

void logLine(const std::wstring& line)
{
    std::wofstream out(logPath(), std::ios::app);
    if (!out)
        return;

    SYSTEMTIME st {};
    GetLocalTime(&st);
    out << L"[" << st.wYear << L"-" << st.wMonth << L"-" << st.wDay
        << L" " << st.wHour << L":" << st.wMinute << L":" << st.wSecond << L"] "
        << line << L"\n";
}

std::wstring lastErrorText(DWORD code)
{
    wchar_t* message = nullptr;
    const auto flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
                     | FORMAT_MESSAGE_IGNORE_INSERTS;
    const auto size = FormatMessageW(flags, nullptr, code, 0,
                                     reinterpret_cast<wchar_t*>(&message), 0, nullptr);
    std::wstring result = size != 0 && message != nullptr
        ? std::wstring(message, size)
        : (L"Windows error " + std::to_wstring(code));
    if (message != nullptr)
        LocalFree(message);
    while (!result.empty() && (result.back() == L'\r' || result.back() == L'\n' || result.back() == L' '))
        result.pop_back();
    return result;
}

std::wstring lower(std::wstring text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    return text;
}

bool allowedHost(const std::wstring& host)
{
    const auto h = lower(host);
    return h == L"github.com"
        || h == L"release-assets.githubusercontent.com"
        || h == L"objects.githubusercontent.com";
}

std::wstring quoteArg(const std::wstring& arg)
{
    if (arg.empty())
        return L"\"\"";

    if (arg.find_first_of(L" \t\"") == std::wstring::npos)
        return arg;

    std::wstring out = L"\"";
    std::size_t slashes = 0;
    for (const auto ch : arg)
    {
        if (ch == L'\\')
        {
            ++slashes;
            continue;
        }

        if (ch == L'\"')
        {
            out.append(slashes * 2 + 1, L'\\');
            out.push_back(L'\"');
            slashes = 0;
            continue;
        }

        out.append(slashes, L'\\');
        slashes = 0;
        out.push_back(ch);
    }
    out.append(slashes * 2, L'\\');
    out.push_back(L'\"');
    return out;
}

bool crackUrl(const std::wstring& url,
              std::wstring& host,
              INTERNET_PORT& port,
              std::wstring& object,
              std::wstring& error)
{
    URL_COMPONENTS parts {};
    parts.dwStructSize = sizeof(parts);
    parts.dwSchemeLength = static_cast<DWORD>(-1);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);

    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts)
        || parts.nScheme != INTERNET_SCHEME_HTTPS)
    {
        error = L"URL HTTPS inválida.";
        return false;
    }

    host.assign(parts.lpszHostName, parts.dwHostNameLength);
    if (!allowedHost(host))
    {
        error = L"GitHub redirigió la actualización a un servidor no permitido: " + host;
        return false;
    }

    port = parts.nPort;
    object.assign(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength > 0)
        object.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    if (object.empty())
        object = L"/";
    return true;
}

bool downloadWithWinHttp(const std::wstring& initialUrl,
                         const std::filesystem::path& destination,
                         std::wstring& error)
{
    std::wstring currentUrl = initialUrl;

    for (int redirect = 0; redirect < 10; ++redirect)
    {
        std::wstring host;
        std::wstring object;
        INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT;
        if (!crackUrl(currentUrl, host, port, object, error))
            return false;

        InternetHandle session { WinHttpOpen(
            L"J3Worship-Bootstrap/1",
            WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            0) };

        if (session.value == nullptr)
        {
            session.value = WinHttpOpen(
                L"J3Worship-Bootstrap/1",
                WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                WINHTTP_NO_PROXY_NAME,
                WINHTTP_NO_PROXY_BYPASS,
                0);
        }

        if (session.value == nullptr)
        {
            error = L"No se pudo iniciar WinHTTP: " + lastErrorText(GetLastError());
            return false;
        }

        WinHttpSetTimeouts(session, 5000, 5000, 15000, 60000);
        DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
        WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));

        InternetHandle connection { WinHttpConnect(session, host.c_str(), port, 0) };
        if (connection.value == nullptr)
        {
            error = L"No se pudo conectar con GitHub: " + lastErrorText(GetLastError());
            return false;
        }

        InternetHandle request { WinHttpOpenRequest(
            connection,
            L"GET",
            object.c_str(),
            nullptr,
            WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            WINHTTP_FLAG_SECURE) };

        if (request.value == nullptr)
        {
            error = L"No se pudo crear la descarga: " + lastErrorText(GetLastError());
            return false;
        }

        DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY,
                         &redirectPolicy, sizeof(redirectPolicy));

        const wchar_t* headers =
            L"Accept: application/octet-stream\r\n"
            L"Cache-Control: no-cache\r\n";

        if (!WinHttpSendRequest(request, headers, static_cast<DWORD>(-1L),
                                WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
            || !WinHttpReceiveResponse(request, nullptr))
        {
            error = L"No se pudo recibir la actualización: " + lastErrorText(GetLastError());
            return false;
        }

        DWORD status = 0;
        DWORD statusSize = sizeof(status);
        if (!WinHttpQueryHeaders(request,
                                 WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                 WINHTTP_HEADER_NAME_BY_INDEX,
                                 &status,
                                 &statusSize,
                                 WINHTTP_NO_HEADER_INDEX))
        {
            error = L"No se pudo leer la respuesta de GitHub: " + lastErrorText(GetLastError());
            return false;
        }

        if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308)
        {
            DWORD locationSize = 0;
            WinHttpQueryHeaders(request, WINHTTP_QUERY_LOCATION,
                                WINHTTP_HEADER_NAME_BY_INDEX,
                                nullptr,
                                &locationSize,
                                WINHTTP_NO_HEADER_INDEX);
            if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || locationSize == 0)
            {
                error = L"GitHub devolvió una redirección sin destino.";
                return false;
            }

            std::vector<wchar_t> location(locationSize / sizeof(wchar_t) + 2, L'\0');
            if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_LOCATION,
                                     WINHTTP_HEADER_NAME_BY_INDEX,
                                     location.data(),
                                     &locationSize,
                                     WINHTTP_NO_HEADER_INDEX))
            {
                error = L"No se pudo seguir la redirección de GitHub: "
                      + lastErrorText(GetLastError());
                return false;
            }

            std::wstring next(location.data());
            if (!next.empty() && next.front() == L'/')
                next = L"https://" + host + next;
            currentUrl = std::move(next);
            continue;
        }

        if (status < 200 || status >= 300)
        {
            error = L"GitHub respondió HTTP " + std::to_wstring(status) + L".";
            return false;
        }

        HANDLE file = CreateFileW(destination.c_str(), GENERIC_WRITE, 0, nullptr,
                                  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            error = L"No se pudo guardar el instalador: " + lastErrorText(GetLastError());
            return false;
        }

        bool ok = true;
        std::uint64_t total = 0;
        constexpr std::uint64_t maxBytes = 256ull * 1024ull * 1024ull;

        for (;;)
        {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(request, &available))
            {
                error = L"Se interrumpió la descarga: " + lastErrorText(GetLastError());
                ok = false;
                break;
            }
            if (available == 0)
                break;

            total += available;
            if (total > maxBytes)
            {
                error = L"La actualización supera el tamaño máximo permitido.";
                ok = false;
                break;
            }

            std::vector<unsigned char> buffer(available);
            DWORD read = 0;
            if (!WinHttpReadData(request, buffer.data(), available, &read))
            {
                error = L"No se pudo completar la descarga: " + lastErrorText(GetLastError());
                ok = false;
                break;
            }

            if (read > 0)
            {
                DWORD written = 0;
                if (!WriteFile(file, buffer.data(), read, &written, nullptr) || written != read)
                {
                    error = L"No se pudo escribir la actualización en disco: "
                          + lastErrorText(GetLastError());
                    ok = false;
                    break;
                }
            }
        }

        CloseHandle(file);

        if (!ok || total == 0)
        {
            std::error_code ec;
            std::filesystem::remove(destination, ec);
            if (ok)
                error = L"GitHub devolvió una descarga vacía.";
            return false;
        }

        return true;
    }

    error = L"Se superó el máximo de redirecciones de descarga.";
    return false;
}

std::wstring psSingleQuote(const std::wstring& value)
{
    std::wstring out = L"'";
    for (const auto ch : value)
    {
        out.push_back(ch);
        if (ch == L'\'')
            out.push_back(L'\'');
    }
    out.push_back(L'\'');
    return out;
}

bool downloadWithPowerShell(const std::wstring& url,
                            const std::filesystem::path& destination,
                            std::wstring& error)
{
    const auto command =
        L"powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "
        L"\"$ErrorActionPreference='Stop'; "
        L"[Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12; "
        L"Invoke-WebRequest -UseBasicParsing -MaximumRedirection 10 -Uri "
        + psSingleQuote(url)
        + L" -OutFile "
        + psSingleQuote(destination.wstring())
        + L"; exit 0\"";

    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW si {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi {};

    if (!CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
    {
        error = L"No se pudo iniciar el método alternativo de Windows: "
              + lastErrorText(GetLastError());
        return false;
    }

    const auto wait = WaitForSingleObject(pi.hProcess, 180000);
    if (wait == WAIT_TIMEOUT)
    {
        TerminateProcess(pi.hProcess, 124);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        error = L"El método alternativo superó el tiempo máximo.";
        return false;
    }

    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (exitCode != 0 || !std::filesystem::exists(destination))
    {
        error = L"Windows tampoco pudo descargar la actualización. Código "
              + std::to_wstring(exitCode) + L".";
        return false;
    }

    return true;
}

bool downloadWithCurl(const std::wstring& url,
                      const std::filesystem::path& destination,
                      std::wstring& error)
{
    const auto command =
        L"curl.exe -L --fail --silent --show-error --retry 3 --retry-all-errors "
        L"--connect-timeout 15 --max-time 180 -A \"J3Worship-Bootstrap/2\" -o "
        + quoteArg(destination.wstring()) + L" " + quoteArg(url);

    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW si {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi {};

    if (!CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
    {
        error = L"No se pudo iniciar curl.exe: " + lastErrorText(GetLastError());
        return false;
    }

    const auto wait = WaitForSingleObject(pi.hProcess, 190000);
    if (wait == WAIT_TIMEOUT)
    {
        TerminateProcess(pi.hProcess, 124);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        error = L"curl superó el tiempo máximo.";
        return false;
    }

    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (exitCode != 0 || !std::filesystem::exists(destination))
    {
        std::error_code ec;
        std::filesystem::remove(destination, ec);
        error = L"curl tampoco pudo descargar la actualización. Código "
              + std::to_wstring(exitCode) + L".";
        return false;
    }

    if (std::filesystem::file_size(destination) == 0)
    {
        std::error_code ec;
        std::filesystem::remove(destination, ec);
        error = L"curl descargó un archivo vacío.";
        return false;
    }

    return true;
}

bool verifySha256(const std::filesystem::path& file,
                  const std::wstring& expected,
                  std::wstring& error)
{
    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;

    if (!CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
    {
        error = L"No se pudo iniciar la verificación SHA-256: "
              + lastErrorText(GetLastError());
        return false;
    }

    if (!CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash))
    {
        error = L"No se pudo crear la verificación SHA-256: "
              + lastErrorText(GetLastError());
        CryptReleaseContext(provider, 0);
        return false;
    }

    std::ifstream input(file, std::ios::binary);
    if (!input)
    {
        error = L"No se pudo abrir la actualización descargada.";
        CryptDestroyHash(hash);
        CryptReleaseContext(provider, 0);
        return false;
    }

    std::array<unsigned char, 64 * 1024> buffer {};
    while (input)
    {
        input.read(reinterpret_cast<char*>(buffer.data()),
                   static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        if (count > 0
            && !CryptHashData(hash, buffer.data(), static_cast<DWORD>(count), 0))
        {
            error = L"No se pudo calcular SHA-256: " + lastErrorText(GetLastError());
            CryptDestroyHash(hash);
            CryptReleaseContext(provider, 0);
            return false;
        }
    }

    std::array<unsigned char, 32> digest {};
    DWORD digestSize = static_cast<DWORD>(digest.size());
    if (!CryptGetHashParam(hash, HP_HASHVAL, digest.data(), &digestSize, 0))
    {
        error = L"No se pudo finalizar SHA-256: " + lastErrorText(GetLastError());
        CryptDestroyHash(hash);
        CryptReleaseContext(provider, 0);
        return false;
    }

    CryptDestroyHash(hash);
    CryptReleaseContext(provider, 0);

    static constexpr wchar_t hex[] = L"0123456789abcdef";
    std::wstring actual;
    actual.reserve(64);
    for (DWORD i = 0; i < digestSize; ++i)
    {
        actual.push_back(hex[(digest[i] >> 4) & 0x0f]);
        actual.push_back(hex[digest[i] & 0x0f]);
    }

    if (lower(actual) != lower(expected))
    {
        error = L"La actualización descargada no pasó la verificación SHA-256.";
        return false;
    }

    return true;
}

DWORD launchFullInstaller(const std::filesystem::path& setup, std::wstring& error)
{
    int argc = 0;
    auto** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr)
    {
        error = L"No se pudieron leer los parámetros del actualizador.";
        return ERROR_INVALID_PARAMETER;
    }

    std::wstring params;
    for (int i = 1; i < argc; ++i)
    {
        if (_wcsicmp(argv[i], L"--j3-worker") == 0)
            continue;
        if (!params.empty())
            params.push_back(L' ');
        params += quoteArg(argv[i]);
    }
    LocalFree(argv);

    auto waitForExit = [&](HANDLE process) -> DWORD
    {
        WaitForSingleObject(process, INFINITE);
        DWORD exitCode = 1;
        GetExitCodeProcess(process, &exitCode);
        CloseHandle(process);
        if (exitCode != 0)
            error = L"El instalador completo terminó con código " + std::to_wstring(exitCode) + L".";
        return exitCode;
    };

    std::wstring command = quoteArg(setup.wstring());
    if (!params.empty())
        command += L" " + params;
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW si {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi {};
    if (CreateProcessW(setup.c_str(), mutableCommand.data(), nullptr, nullptr, FALSE,
                       0, nullptr, nullptr, &si, &pi))
    {
        CloseHandle(pi.hThread);
        return waitForExit(pi.hProcess);
    }

    const auto createError = GetLastError();
    if (createError != ERROR_ELEVATION_REQUIRED && createError != ERROR_ACCESS_DENIED)
    {
        error = L"Windows no pudo iniciar el instalador completo: " + lastErrorText(createError);
        return createError;
    }

    SHELLEXECUTEINFOW info {};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    info.lpVerb = L"runas";
    info.lpFile = setup.c_str();
    info.lpParameters = params.c_str();
    info.nShow = SW_HIDE;

    if (!ShellExecuteExW(&info))
    {
        const auto code = GetLastError();
        if (code == ERROR_CANCELLED)
            error = L"La instalación necesita permiso de Windows y fue cancelada.";
        else
            error = L"Windows no pudo iniciar el instalador completo: " + lastErrorText(code);
        return code;
    }

    return waitForExit(info.hProcess);
}

void showFailure(const std::wstring& error)
{
    const auto message =
        L"J3 Worship no pudo completar la actualización.\n\n"
        + error
        + L"\n\nRegistro: "
        + logPath().wstring();

    MessageBoxW(nullptr, message.c_str(), L"J3 Worship - Actualización",
                MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
}

bool hasWorkerFlag()
{
    int argc = 0;
    auto** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr)
        return false;

    bool found = false;
    for (int i = 1; i < argc; ++i)
        if (_wcsicmp(argv[i], L"--j3-worker") == 0)
        {
            found = true;
            break;
        }

    LocalFree(argv);
    return found;
}

std::filesystem::path currentExecutablePath()
{
    std::vector<wchar_t> buffer(32768, L'\0');
    const auto len = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (len == 0 || len >= buffer.size())
        return {};
    return std::filesystem::path(std::wstring(buffer.data(), len));
}

bool launchDetachedWorker(std::wstring& error)
{
    const auto self = currentExecutablePath();
    if (self.empty())
    {
        error = L"No se pudo identificar el ejecutable del actualizador.";
        return false;
    }

    int argc = 0;
    auto** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr)
    {
        error = L"No se pudieron leer los parámetros del actualizador.";
        return false;
    }

    std::wstring command = quoteArg(self.wstring()) + L" --j3-worker";
    for (int i = 1; i < argc; ++i)
    {
        if (_wcsicmp(argv[i], L"--j3-worker") == 0)
            continue;
        command += L" " + quoteArg(argv[i]);
    }
    LocalFree(argv);

    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW si {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi {};
    if (!CreateProcessW(self.c_str(), mutableCommand.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
    {
        error = L"No se pudo iniciar el actualizador en segundo plano: "
              + lastErrorText(GetLastError());
        return false;
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

std::optional<std::filesystem::path> requestedInstallDirectory()
{
    int argc = 0;
    auto** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr)
        return std::nullopt;

    std::optional<std::filesystem::path> result;
    for (int i = 1; i < argc; ++i)
    {
        const std::wstring arg(argv[i]);
        if (arg.size() > 5 && _wcsnicmp(arg.c_str(), L"/DIR=", 5) == 0)
        {
            result = std::filesystem::path(arg.substr(5));
            break;
        }
    }
    LocalFree(argv);
    return result;
}

void relaunchInstalledApplication()
{
    const auto installDir = requestedInstallDirectory();
    if (!installDir.has_value())
    {
        logLine(L"No se recibió /DIR; se omite el relanzamiento automático.");
        return;
    }

    const auto exe = *installDir / L"J3Worship.exe";
    for (int attempt = 0; attempt < 40 && !std::filesystem::exists(exe); ++attempt)
        std::this_thread::sleep_for(std::chrono::milliseconds(250));

    if (!std::filesystem::exists(exe))
    {
        logLine(L"No se encontró J3Worship.exe para relanzar después de actualizar.");
        return;
    }

    const auto result = reinterpret_cast<INT_PTR>(
        ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr, installDir->c_str(), SW_SHOWNORMAL));
    if (result <= 32)
        logLine(L"No se pudo relanzar J3 Worship después de actualizar.");
    else
        logLine(L"J3 Worship relanzado correctamente.");
}
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    if (!hasWorkerFlag())
    {
        std::wstring detachError;
        if (!launchDetachedWorker(detachError))
        {
            showFailure(detachError);
            return 30;
        }

        // Important for legacy 1.11/1.12 launchers: return immediately so their
        // visible cmd.exe / start /wait chain can close instead of appearing frozen.
        return 0;
    }
    std::error_code ec;
    std::filesystem::remove(logPath(), ec);

    logLine(L"Bootstrap J3 Worship " + std::wstring(kVersion) + L" iniciado.");

    const auto destination =
        updaterFolder() / (L"J3Worship-Full-Setup-" + std::wstring(kVersion) + L".exe");
    std::filesystem::remove(destination, ec);

    std::wstring error;
    bool downloaded = false;

    for (int attempt = 1; attempt <= 3 && !downloaded; ++attempt)
    {
        error.clear();
        logLine(L"Intento WinHTTP " + std::to_wstring(attempt) + L".");
        downloaded = downloadWithWinHttp(kFullUrl, destination, error);
        if (!downloaded)
        {
            logLine(L"WinHTTP falló: " + error);
            std::this_thread::sleep_for(std::chrono::milliseconds(350 * attempt));
        }
    }

    if (!downloaded)
    {
        error.clear();
        logLine(L"Probando método alternativo PowerShell.");
        downloaded = downloadWithPowerShell(kFullUrl, destination, error);
        if (!downloaded)
            logLine(L"PowerShell falló: " + error);
    }

    if (!downloaded)
    {
        error.clear();
        logLine(L"Probando tercer método curl.exe.");
        downloaded = downloadWithCurl(kFullUrl, destination, error);
        if (!downloaded)
            logLine(L"curl falló: " + error);
    }

    if (!downloaded)
    {
        showFailure(error.empty() ? L"No se pudo descargar el instalador completo por ninguno de los métodos disponibles." : error);
        return 20;
    }

    error.clear();
    if (!verifySha256(destination, kFullSha256, error))
    {
        logLine(L"SHA-256 falló: " + error);
        std::filesystem::remove(destination, ec);
        showFailure(error);
        return 21;
    }

    logLine(L"SHA-256 correcto. Ejecutando instalador completo.");
    const auto exitCode = launchFullInstaller(destination, error);
    if (exitCode != 0)
    {
        logLine(L"Instalación falló: " + error);
        showFailure(error);
        return static_cast<int>(exitCode);
    }

    logLine(L"Actualización completada correctamente.");
    std::filesystem::remove(destination, ec);
    relaunchInstalledApplication();
    return 0;
}
