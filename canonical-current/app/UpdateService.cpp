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
    std::wstring wide(static_cast<std::size_t>(needed - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide.data(), needed);
    return wide;
}

struct InternetHandle
{
    HINTERNET handle {};
    ~InternetHandle() { if (handle != nullptr) WinHttpCloseHandle(handle); }
    operator HINTERNET() const noexcept { return handle; }
};
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
}

namespace j3ui
{
bool UpdateService::requestBytes(const juce::String& url, juce::MemoryBlock& bytes, juce::String& error)
{
#if JUCE_WINDOWS
    if (!url.startsWithIgnoreCase("https://"))
    {
        error = "La actualización fue bloqueada porque la URL no usa HTTPS.";
        return false;
    }

    const auto wideUrl = utf8ToWide(url);
    if (wideUrl.empty())
    {
        error = "URL de actualización inválida.";
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
        error = "No se pudo interpretar la URL segura de actualización.";
        return false;
    }

    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring object(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength > 0)
        object.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    if (object.empty())
        object = L"/";

    InternetHandle session { WinHttpOpen(L"J3Worship/1.1",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0) };
    if (session.handle == nullptr)
    {
        error = "No se pudo iniciar la conexión para buscar actualizaciones.";
        return false;
    }
    WinHttpSetTimeouts(session, 5000, 5000, 10000, 15000);

    InternetHandle connection { WinHttpConnect(session, host.c_str(), parts.nPort, 0) };
    if (connection.handle == nullptr)
    {
        error = "No se pudo conectar con el servidor de actualizaciones.";
        return false;
    }

    InternetHandle request { WinHttpOpenRequest(connection, L"GET", object.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) };
    if (request.handle == nullptr)
    {
        error = "No se pudo crear la solicitud de actualización.";
        return false;
    }

    const wchar_t* headers = L"Accept: application/vnd.github+json\r\n";
    if (!WinHttpSendRequest(request, headers, static_cast<DWORD>(-1L),
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
        || !WinHttpReceiveResponse(request, nullptr))
    {
        error = "No se pudo descargar la información de actualización.";
        return false;
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (!WinHttpQueryHeaders(request,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
            WINHTTP_NO_HEADER_INDEX)
        || status < 200 || status >= 300)
    {
        error = "El servidor de actualizaciones respondió con HTTP " + juce::String(static_cast<int>(status)) + ".";
        return false;
    }

    bytes.reset();
    constexpr std::size_t maxDownloadBytes = 256u * 1024u * 1024u;
    for (;;)
    {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available))
        {
            error = "Se interrumpió la descarga de actualización.";
            return false;
        }
        if (available == 0)
            break;
        if (bytes.getSize() + available > maxDownloadBytes)
        {
            error = "La descarga de actualización excede el tamaño permitido.";
            return false;
        }

        std::vector<std::uint8_t> buffer(static_cast<std::size_t>(available));
        DWORD read = 0;
        if (!WinHttpReadData(request, buffer.data(), available, &read))
        {
            error = "No se pudo completar la descarga de actualización.";
            return false;
        }
        if (read > 0)
            bytes.append(buffer.data(), static_cast<std::size_t>(read));
    }
    return bytes.getSize() > 0;
#else
    juce::ignoreUnused(url, bytes);
    error = "Las actualizaciones automáticas están disponibles en Windows.";
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
        error = "La respuesta de actualizaciones no tiene un formato válido.";
        return std::nullopt;
    }

    const auto tag = release->getProperty("tag_name").toString().trim();
    const auto parsedVersion = j3::Updater::parseVersion(tag.toStdString());
    if (!parsedVersion.has_value())
    {
        error = "La última versión publicada tiene un número inválido.";
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
        error = "No se pudo crear la carpeta temporal de actualización.";
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
        error = "La actualización descargada no pasó la verificación SHA-256.";
        return false;
    }
    return true;
}

bool UpdateService::launchInstallerAndRestart(const juce::File& installer, juce::String& error)
{
#if JUCE_WINDOWS
    if (!installer.existsAsFile())
    {
        error = "El instalador de actualización ya no está disponible.";
        return false;
    }

    const auto currentExe = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    auto script = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("J3WorshipUpdater")
        .getChildFile("apply-update.cmd");

    auto quoteForCmd = [](juce::String s)
    {
        s = s.replace(""", """");
        return """ + s + """;
    };

    juce::String body;
    body << "@echo off\r\n";
    body << "timeout /t 2 /nobreak >nul\r\n";
    body << "start /wait \"\" " << quoteForCmd(installer.getFullPathName())
         << " /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /CLOSEAPPLICATIONS\r\n";
    body << "start \"\" " << quoteForCmd(currentExe.getFullPathName()) << "\r\n";
    body << "del \"%~f0\"\r\n";

    if (!script.replaceWithText(body))
    {
        error = "No se pudo preparar el instalador automático.";
        return false;
    }
    if (!script.startAsProcess())
    {
        error = "Windows no pudo iniciar el actualizador.";
        return false;
    }
    return true;
#else
    juce::ignoreUnused(installer);
    error = "Las actualizaciones automáticas están disponibles en Windows.";
    return false;
#endif
}
}
