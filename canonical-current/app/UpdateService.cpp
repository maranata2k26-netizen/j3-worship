#include "UpdateService.h"

#include <regex>
#include <string>
#include <vector>

#if JUCE_WINDOWS
 #include <windows.h>
 #include <winhttp.h>
#endif

namespace
{
#if JUCE_WINDOWS
std::wstring utf8ToWide(const juce::String& text)
{
    const auto* utf8 = text.toRawUTF8();
    const int needed = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    if (needed <= 1)
        return {};
    std::wstring wide(static_cast<std::size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide.data(), needed);
    if (!wide.empty() && wide.back() == L'\0') wide.pop_back();
    return wide;
}

struct InternetHandle
{
    HINTERNET handle {};
    ~InternetHandle() { if (handle != nullptr) WinHttpCloseHandle(handle); }
    operator HINTERNET() const noexcept { return handle; }
};

bool isAllowedUpdateUrl(const juce::String& url)
{
    if (!url.startsWithIgnoreCase("https://"))
        return false;

    const auto lower = url.toLowerCase();
    return lower.startsWith("https://api.github.com/")
        || lower.startsWith("https://github.com/")
        || lower.startsWith("https://release-assets.githubusercontent.com/")
        || lower.startsWith("https://objects.githubusercontent.com/")
        || lower.startsWith("https://raw.githubusercontent.com/");
}

juce::String powershellQuote(const juce::String& value)
{
    return "'" + value.replace("'", "''") + "'";
}

juce::String lastWinHttpError(const juce::String& prefix)
{
    return prefix + " (WinHTTP " + juce::String(static_cast<int>(GetLastError())) + ")";
}

bool requestBytesWithPowerShell(const juce::String& url, juce::MemoryBlock& bytes, juce::String& error)
{
    if (!isAllowedUpdateUrl(url))
    {
        error = juce::String::fromUTF8("La URL de actualización no pertenece a un servidor permitido.");
        return false;
    }

    auto temp = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("j3-update-fallback", ".bin", false);

    const auto command =
        "$ErrorActionPreference='Stop'; "
        "[Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12; "
        "Invoke-WebRequest -UseBasicParsing -MaximumRedirection 10 -Uri "
        + powershellQuote(url)
        + " -OutFile "
        + powershellQuote(temp.getFullPathName())
        + "; exit 0";

    juce::StringArray args;
    args.add("powershell.exe");
    args.add("-NoProfile");
    args.add("-NonInteractive");
    args.add("-ExecutionPolicy");
    args.add("Bypass");
    args.add("-Command");
    args.add(command);

    juce::ChildProcess process;
    if (!process.start(args))
    {
        error = juce::String::fromUTF8("No se pudo iniciar el método alternativo de descarga de Windows.");
        return false;
    }

    if (!process.waitForProcessToFinish(180000))
    {
        process.kill();
        temp.deleteFile();
        error = juce::String::fromUTF8("La descarga alternativa superó el tiempo máximo.");
        return false;
    }

    if (process.getExitCode() != 0 || !temp.existsAsFile())
    {
        const auto output = process.readAllProcessOutput().trim();
        temp.deleteFile();
        error = juce::String::fromUTF8("Windows tampoco pudo descargar la actualización.")
            + (output.isNotEmpty() ? " " + output.substring(0, 240) : juce::String());
        return false;
    }

    bytes.reset();
    const bool loaded = temp.loadFileAsData(bytes);
    temp.deleteFile();
    if (!loaded || bytes.getSize() == 0)
    {
        error = juce::String::fromUTF8("La descarga alternativa terminó vacía.");
        return false;
    }

    return true;
}

bool requestBytesWithCurl(const juce::String& url, juce::MemoryBlock& bytes, juce::String& error)
{
    if (!isAllowedUpdateUrl(url))
    {
        error = juce::String::fromUTF8("La URL de actualización no pertenece a un servidor permitido.");
        return false;
    }

    auto temp = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("j3-update-curl", ".bin", false);

    juce::StringArray args;
    args.add("curl.exe");
    args.add("-L");
    args.add("--fail");
    args.add("--silent");
    args.add("--show-error");
    args.add("--retry");
    args.add("3");
    args.add("--retry-all-errors");
    args.add("--connect-timeout");
    args.add("15");
    args.add("--max-time");
    args.add("180");
    args.add("-A");
    args.add("J3Worship-Updater/3");
    args.add("-o");
    args.add(temp.getFullPathName());
    args.add(url);

    juce::ChildProcess process;
    if (!process.start(args))
    {
        error = juce::String::fromUTF8("No se pudo iniciar curl.exe como tercer método de descarga.");
        return false;
    }

    if (!process.waitForProcessToFinish(190000))
    {
        process.kill();
        temp.deleteFile();
        error = juce::String::fromUTF8("La descarga con curl superó el tiempo máximo.");
        return false;
    }

    const auto output = process.readAllProcessOutput().trim();
    if (process.getExitCode() != 0 || !temp.existsAsFile())
    {
        temp.deleteFile();
        error = juce::String::fromUTF8("curl tampoco pudo descargar la actualización.")
            + (output.isNotEmpty() ? " " + output.substring(0, 240) : juce::String());
        return false;
    }

    bytes.reset();
    const bool loaded = temp.loadFileAsData(bytes);
    temp.deleteFile();
    if (!loaded || bytes.getSize() == 0)
    {
        error = juce::String::fromUTF8("La descarga con curl terminó vacía.");
        return false;
    }

    return true;
}

bool requestBytesWithWinHttp(const juce::String& initialUrl,
                             juce::MemoryBlock& bytes,
                             juce::String& error)
{
    juce::String currentUrl = initialUrl;

    for (int redirect = 0; redirect < 10; ++redirect)
    {
        if (!isAllowedUpdateUrl(currentUrl))
        {
            error = juce::String::fromUTF8("La actualización intentó redirigir a un servidor no permitido.");
            return false;
        }

        const auto wideUrl = utf8ToWide(currentUrl);
        if (wideUrl.empty())
        {
            error = juce::String::fromUTF8("URL de actualización inválida.");
            return false;
        }

        URL_COMPONENTS parts {};
        parts.dwStructSize = sizeof(parts);
        parts.dwSchemeLength = static_cast<DWORD>(-1);
        parts.dwHostNameLength = static_cast<DWORD>(-1);
        parts.dwUrlPathLength = static_cast<DWORD>(-1);
        parts.dwExtraInfoLength = static_cast<DWORD>(-1);
        if (!WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &parts)
            || parts.nScheme != INTERNET_SCHEME_HTTPS)
        {
            error = juce::String::fromUTF8("No se pudo interpretar la URL segura de actualización.");
            return false;
        }

        const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
        std::wstring object(parts.lpszUrlPath, parts.dwUrlPathLength);
        if (parts.dwExtraInfoLength > 0)
            object.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
        if (object.empty())
            object = L"/";

        InternetHandle session { WinHttpOpen(L"J3Worship-Updater/2",
            WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS, 0) };
        if (session.handle == nullptr)
        {
            session.handle = WinHttpOpen(L"J3Worship-Updater/2",
                WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                WINHTTP_NO_PROXY_BYPASS, 0);
        }
        if (session.handle == nullptr)
        {
            error = lastWinHttpError(juce::String::fromUTF8("No se pudo iniciar la conexión para buscar actualizaciones."));
            return false;
        }

        WinHttpSetTimeouts(session, 5000, 5000, 12000, 30000);
        DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
        WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));

        InternetHandle connection { WinHttpConnect(session, host.c_str(), parts.nPort, 0) };
        if (connection.handle == nullptr)
        {
            error = lastWinHttpError(juce::String::fromUTF8("No se pudo conectar con el servidor de actualizaciones."));
            return false;
        }

        InternetHandle request { WinHttpOpenRequest(connection, L"GET", object.c_str(), nullptr,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) };
        if (request.handle == nullptr)
        {
            error = lastWinHttpError(juce::String::fromUTF8("No se pudo crear la solicitud de actualización."));
            return false;
        }

        DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));

        const bool apiRequest = currentUrl.startsWithIgnoreCase("https://api.github.com/");
        const wchar_t* headers = apiRequest
            ? L"Accept: application/vnd.github+json\r\nCache-Control: no-cache, no-store, max-age=0\r\nPragma: no-cache\r\n"
            : L"Accept: application/octet-stream\r\nCache-Control: no-cache\r\n";

        if (!WinHttpSendRequest(request, headers, static_cast<DWORD>(-1L),
                                WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
        {
            error = lastWinHttpError(juce::String::fromUTF8("No se pudo enviar la solicitud de actualización."));
            return false;
        }
        if (!WinHttpReceiveResponse(request, nullptr))
        {
            error = lastWinHttpError(juce::String::fromUTF8("No se pudo recibir la descarga de actualización."));
            return false;
        }

        DWORD status = 0;
        DWORD statusSize = sizeof(status);
        if (!WinHttpQueryHeaders(request,
                WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                WINHTTP_NO_HEADER_INDEX))
        {
            error = lastWinHttpError(juce::String::fromUTF8("No se pudo leer la respuesta del servidor de actualizaciones."));
            return false;
        }

        if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308)
        {
            DWORD locationSize = 0;
            WinHttpQueryHeaders(request, WINHTTP_QUERY_LOCATION,
                WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &locationSize, WINHTTP_NO_HEADER_INDEX);
            if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || locationSize == 0)
            {
                error = juce::String::fromUTF8("GitHub redirigió la descarga sin indicar un destino válido.");
                return false;
            }

            std::vector<wchar_t> location(static_cast<std::size_t>(locationSize / sizeof(wchar_t)) + 2, L'\0');
            if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_LOCATION,
                    WINHTTP_HEADER_NAME_BY_INDEX, location.data(), &locationSize,
                    WINHTTP_NO_HEADER_INDEX))
            {
                error = lastWinHttpError(juce::String::fromUTF8("No se pudo seguir la redirección de descarga."));
                return false;
            }

            juce::String next(location.data());
            if (next.startsWithChar('/'))
                next = "https://" + juce::String(host.c_str()) + next;
            currentUrl = next;
            continue;
        }

        if (status < 200 || status >= 300)
        {
            error = juce::String::fromUTF8("El servidor de actualizaciones respondió con HTTP ")
                + juce::String(static_cast<int>(status)) + ".";
            return false;
        }

        bytes.reset();
        constexpr std::size_t maxDownloadBytes = 256u * 1024u * 1024u;
        for (;;)
        {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(request, &available))
            {
                error = lastWinHttpError(juce::String::fromUTF8("Se interrumpió la descarga de actualización."));
                return false;
            }
            if (available == 0)
                break;
            if (bytes.getSize() + available > maxDownloadBytes)
            {
                error = juce::String::fromUTF8("La descarga de actualización excede el tamaño permitido.");
                return false;
            }

            std::vector<std::uint8_t> buffer(static_cast<std::size_t>(available));
            DWORD read = 0;
            if (!WinHttpReadData(request, buffer.data(), available, &read))
            {
                error = lastWinHttpError(juce::String::fromUTF8("No se pudo completar la descarga de actualización."));
                return false;
            }
            if (read > 0)
                bytes.append(buffer.data(), static_cast<std::size_t>(read));
        }

        if (bytes.getSize() == 0)
        {
            error = juce::String::fromUTF8("El servidor devolvió una descarga vacía.");
            return false;
        }
        return true;
    }

    error = juce::String::fromUTF8("La descarga superó el máximo de redirecciones permitido.");
    return false;
}
#endif

juce::String shaFromReleaseBody(const juce::String& body)
{
    const std::regex pattern(R"(SHA256:\s*([0-9a-fA-F]{64}))", std::regex::icase);
    std::smatch match;
    const auto text = body.toStdString();
    if (std::regex_search(text, match, pattern) && match.size() >= 2)
        return juce::String(match[1].str()).toLowerCase();
    return {};
}

juce::File updaterFolder()
{
    return juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("J3WorshipUpdater");
}

juce::File updateFailureMarker()
{
    return updaterFolder().getChildFile("last-update-failed.txt");
}

juce::File pendingUpdateVersionMarker()
{
    return updaterFolder().getChildFile("pending-update-version.txt");
}

juce::String quoteForCmd(const juce::String& value)
{
    const auto quote = juce::String::charToString('"');
    return quote + value.replace(quote, quote + quote) + quote;
}
}

namespace j3ui
{
bool UpdateService::requestBytes(const juce::String& url, juce::MemoryBlock& bytes, juce::String& error)
{
#if JUCE_WINDOWS
    if (!isAllowedUpdateUrl(url))
    {
        error = juce::String::fromUTF8("La actualización fue bloqueada porque la URL no es HTTPS o no pertenece a GitHub.");
        return false;
    }

    juce::String winHttpError;
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        bytes.reset();
        if (requestBytesWithWinHttp(url, bytes, winHttpError))
        {
            error.clear();
            return true;
        }

        if (attempt < 2)
            juce::Thread::sleep(350 + attempt * 650);
    }

    juce::String fallbackError;
    bytes.reset();
    if (requestBytesWithPowerShell(url, bytes, fallbackError))
    {
        error.clear();
        return true;
    }

    juce::String curlError;
    bytes.reset();
    if (requestBytesWithCurl(url, bytes, curlError))
    {
        error.clear();
        return true;
    }

    error = winHttpError;
    if (fallbackError.isNotEmpty())
        error << juce::String::fromUTF8(" PowerShell: ") << fallbackError;
    if (curlError.isNotEmpty())
        error << juce::String::fromUTF8(" curl: ") << curlError;
    return false;
#else
    juce::ignoreUnused(url, bytes);
    error = juce::String::fromUTF8("Las actualizaciones automáticas están disponibles en Windows.");
    return false;
#endif
}

std::optional<AvailableUpdate> UpdateService::checkLatest(j3::SemVer current, juce::String& error)
{
    juce::MemoryBlock response;
    if (!requestBytes("https://api.github.com/repos/maranata2k26-netizen/j3-worship/releases/latest",
                      response, error))
        return std::nullopt;

    const juce::String jsonText = juce::String::fromUTF8(
        static_cast<const char*>(response.getData()), static_cast<int>(response.getSize()));
    const auto parsed = juce::JSON::parse(jsonText);
    auto* release = parsed.getDynamicObject();
    if (release == nullptr)
    {
        error = juce::String::fromUTF8("La respuesta de actualizaciones no tiene un formato válido.");
        return std::nullopt;
    }

    const auto tag = release->getProperty("tag_name").toString().trim();
    const auto parsedVersion = j3::Updater::parseVersion(tag.toStdString());
    if (!parsedVersion.has_value())
    {
        error = juce::String::fromUTF8("La última versión publicada tiene un número inválido.");
        return std::nullopt;
    }

    if (*parsedVersion <= current)
    {
        error.clear();
        return std::nullopt;
    }

    juce::String setupUrl;
    juce::String digest;
    const auto assetsVar = release->getProperty("assets");
    if (auto* assets = assetsVar.getArray())
    {
        for (const auto& item : *assets)
        {
            auto* asset = item.getDynamicObject();
            if (asset == nullptr)
                continue;
            if (asset->getProperty("name").toString() != "J3Worship-Setup.exe")
                continue;
            setupUrl = asset->getProperty("browser_download_url").toString();
            digest = asset->getProperty("digest").toString().trim();
            break;
        }
    }

    if (!setupUrl.startsWithIgnoreCase(
            "https://github.com/maranata2k26-netizen/j3-worship/releases/download/"))
    {
        error = "La release no contiene el instalador oficial de J3 Worship.";
        return std::nullopt;
    }

    juce::String sha;
    if (digest.startsWithIgnoreCase("sha256:"))
        sha = digest.fromFirstOccurrenceOf(":", false, false).trim().toLowerCase();
    if (sha.isEmpty())
        sha = shaFromReleaseBody(release->getProperty("body").toString());

    j3::UpdateManifest manifest {
        *parsedVersion,
        setupUrl.toStdString(),
        sha.toStdString(),
        release->getProperty("body").toString().toStdString()
    };
    if (!j3::Updater::manifestLooksSafe(manifest))
    {
        error = "La release fue encontrada, pero no tiene un SHA-256 verificable.";
        return std::nullopt;
    }

    return AvailableUpdate {
        *parsedVersion,
        juce::String(parsedVersion->major) + "." + juce::String(parsedVersion->minor)
            + "." + juce::String(parsedVersion->patch),
        setupUrl,
        sha,
        release->getProperty("body").toString()
    };
}

bool UpdateService::downloadAndVerify(const AvailableUpdate& update, juce::File& installer, juce::String& error)
{
    juce::MemoryBlock bytes;
    if (!requestBytes(update.downloadUrl, bytes, error))
        return false;

    auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("J3WorshipUpdater");
    if (!folder.createDirectory())
    {
        error = juce::String::fromUTF8("No se pudo crear la carpeta temporal de actualización.");
        return false;
    }

    installer = folder.getChildFile("J3Worship-Setup-" + update.versionText + ".exe");
    installer.deleteFile();
    if (!installer.replaceWithData(bytes.getData(), bytes.getSize()))
    {
        error = "No se pudo guardar el instalador descargado.";
        return false;
    }

    const juce::SHA256 hash(installer);
    const auto actual = hash.toHexString().toLowerCase();
    if (actual != update.sha256.toLowerCase())
    {
        installer.deleteFile();
        error = juce::String::fromUTF8("La actualización descargada no pasó la verificación SHA-256.");
        return false;
    }
    return true;
}

bool UpdateService::launchInstallerAndRestart(const juce::File& installer,
                                                    const juce::String& expectedVersion,
                                                    juce::String& error)
{
#if JUCE_WINDOWS
    if (!installer.existsAsFile())
    {
        error = juce::String::fromUTF8("El instalador de actualización ya no está disponible.");
        return false;
    }

    const auto currentExe = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    if (!currentExe.existsAsFile())
    {
        error = juce::String::fromUTF8("J3 no pudo identificar el ejecutable que está usando ahora.");
        return false;
    }

    const auto installDir = currentExe.getParentDirectory();
    auto folder = updaterFolder();
    if (!folder.createDirectory())
    {
        error = juce::String::fromUTF8("No se pudo preparar la carpeta temporal del actualizador.");
        return false;
    }

    const auto logFile = folder.getChildFile("update-install.log");
    const auto failureMarker = updateFailureMarker();
    const auto pendingMarker = pendingUpdateVersionMarker();

    if (!pendingMarker.replaceWithText(expectedVersion))
    {
        error = juce::String::fromUTF8("No se pudo registrar la versión esperada antes de actualizar.");
        return false;
    }
    failureMarker.deleteFile();

    const auto installerPath = utf8ToWide(installer.getFullPathName());
    const auto installPath = utf8ToWide(installDir.getFullPathName());
    const auto logPath = utf8ToWide(logFile.getFullPathName());
    if (installerPath.empty() || installPath.empty() || logPath.empty())
    {
        pendingMarker.deleteFile();
        error = juce::String::fromUTF8("Windows no pudo preparar las rutas del actualizador.");
        return false;
    }

    std::wstring command;
    command.reserve(installerPath.size() + installPath.size() + logPath.size() + 160);
    command += L"\"" + installerPath + L"\"";
    command += L" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /CLOSEAPPLICATIONS";
    command += L" /DIR=\"" + installPath + L"\"";
    command += L" /LOG=\"" + logPath + L"\"";

    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');

    STARTUPINFOW si {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi {};
    if (!CreateProcessW(installerPath.c_str(), mutableCommand.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
    {
        pendingMarker.deleteFile();
        error = juce::String::fromUTF8("Windows no pudo iniciar el actualizador en segundo plano.")
            + " (Win32 " + juce::String(static_cast<int>(GetLastError())) + ")";
        return false;
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
#else
    juce::ignoreUnused(installer, expectedVersion);
    error = juce::String::fromUTF8("Las actualizaciones automáticas están disponibles en Windows.");
    return false;
#endif
}

PreviousUpdateResult UpdateService::verifyPreviousUpdate(const juce::String& currentVersion)
{
    const auto failureMarker = updateFailureMarker();
    const auto pendingMarker = pendingUpdateVersionMarker();

    if (failureMarker.existsAsFile())
    {
        auto message = failureMarker.loadFileAsString().trim();
        failureMarker.deleteFile();
        pendingMarker.deleteFile();
        if (message.isEmpty())
            message = juce::String::fromUTF8("La actualización anterior no pudo completarse.");
        return { PreviousUpdateState::Failed, message, {} };
    }

    if (!pendingMarker.existsAsFile())
        return {};

    const auto expectedText = pendingMarker.loadFileAsString().trim();
    pendingMarker.deleteFile();

    const auto expected = j3::Updater::parseVersion(expectedText.toStdString());
    const auto current = j3::Updater::parseVersion(currentVersion.toStdString());
    if (!expected.has_value() || !current.has_value())
    {
        return {
            PreviousUpdateState::Failed,
            juce::String::fromUTF8("J3 no pudo verificar qué versión quedó instalada. No volverá a ofrecer la misma actualización automáticamente hasta que la compruebes con REINTENTAR."),
            expectedText
        };
    }

    if (*current >= *expected)
    {
        return {
            PreviousUpdateState::Applied,
            juce::String::fromUTF8("Actualización instalada correctamente."),
            expectedText
        };
    }

    return {
        PreviousUpdateState::Failed,
        juce::String::fromUTF8("La actualización intentó instalar J3 Worship ")
            + expectedText
            + juce::String::fromUTF8(", pero Windows volvió a abrir la versión ")
            + currentVersion
            + juce::String::fromUTF8(". El ejecutable no fue reemplazado; por eso J3 detuvo el bucle automático. Tocá REINTENTAR para volver a instalarla."),
        expectedText
    };
}
}
