#include <windows.h>
#include <commdlg.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cwctype>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;

constexpr wchar_t kProductName[] = L"Metaphor: ReFantazio Complete Audio Patch";
constexpr wchar_t kGameExecutable[] = L"METAPHOR.exe";
constexpr wchar_t kAsiFile[] = L"MetaphorCompleteAudioPatch.asi";
constexpr wchar_t kIniFile[] = L"MetaphorCompleteAudioPatch.ini";
constexpr wchar_t kLoaderFile[] = L"winmm.dll";
constexpr wchar_t kRuntimeFile[] = L"libwinpthread-1.dll";

constexpr std::array<const wchar_t*, 4> kPayloadFiles{
    kAsiFile,
    kIniFile,
    kLoaderFile,
    kRuntimeFile,
};

void ShowError(const std::wstring& message)
{
    MessageBoxW(nullptr, message.c_str(), kProductName, MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
}

void ShowInfo(const std::wstring& message)
{
    MessageBoxW(nullptr, message.c_str(), kProductName, MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND);
}

std::wstring WindowsError(DWORD error)
{
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<wchar_t*>(&buffer),
        0,
        nullptr
    );
    std::wstring message = length && buffer ? std::wstring(buffer, length) : L"Windows error " + std::to_wstring(error);
    if (buffer) {
        LocalFree(buffer);
    }
    while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' ')) {
        message.pop_back();
    }
    return message;
}

fs::path ModuleDirectory()
{
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        return {};
    }
    return fs::path(std::wstring(buffer.data(), length)).parent_path();
}

bool IsRegularFile(const fs::path& path)
{
    std::error_code error;
    return fs::is_regular_file(path, error);
}

std::optional<std::string> ReadTextFile(const fs::path& path)
{
    FILE* file = _wfopen(path.c_str(), L"rb");
    if (!file) {
        return std::nullopt;
    }
    if (_fseeki64(file, 0, SEEK_END) != 0) {
        std::fclose(file);
        return std::nullopt;
    }
    const auto size = _ftelli64(file);
    if (size < 0 || size > 16 * 1024 * 1024 || _fseeki64(file, 0, SEEK_SET) != 0) {
        std::fclose(file);
        return std::nullopt;
    }
    std::string text(static_cast<std::size_t>(size), '\0');
    if (!text.empty() && std::fread(text.data(), 1, text.size(), file) != text.size()) {
        std::fclose(file);
        return std::nullopt;
    }
    std::fclose(file);
    return text;
}

std::vector<std::string> QuotedTokens(const std::string& text)
{
    std::vector<std::string> tokens;
    for (std::size_t index = 0; index < text.size();) {
        if (text[index] != '"') {
            ++index;
            continue;
        }
        ++index;
        std::string token;
        while (index < text.size() && text[index] != '"') {
            if (text[index] == '\\' && index + 1 < text.size()) {
                const char escaped = text[index + 1];
                if (escaped == '\\' || escaped == '"') {
                    token.push_back(escaped);
                    index += 2;
                    continue;
                }
            }
            token.push_back(text[index++]);
        }
        if (index < text.size() && text[index] == '"') {
            ++index;
        }
        tokens.push_back(std::move(token));
    }
    return tokens;
}

bool EqualsAsciiInsensitive(const std::string& left, const char* right)
{
    const std::size_t right_length = std::char_traits<char>::length(right);
    if (left.size() != right_length) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (std::tolower(static_cast<unsigned char>(left[index])) !=
            std::tolower(static_cast<unsigned char>(right[index]))) {
            return false;
        }
    }
    return true;
}

std::vector<std::string> ValuesForKey(const std::string& text, const char* key)
{
    const auto tokens = QuotedTokens(text);
    std::vector<std::string> values;
    for (std::size_t index = 0; index + 1 < tokens.size(); ++index) {
        if (EqualsAsciiInsensitive(tokens[index], key)) {
            values.push_back(tokens[index + 1]);
        }
    }
    return values;
}

std::wstring Utf8ToWide(const std::string& value)
{
    if (value.empty()) {
        return {};
    }
    UINT code_page = CP_UTF8;
    int length = MultiByteToWideChar(code_page, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) {
        code_page = CP_ACP;
        length = MultiByteToWideChar(code_page, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    }
    if (length <= 0) {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(code_page, 0, value.data(), static_cast<int>(value.size()), result.data(), length);
    std::replace(result.begin(), result.end(), L'/', L'\\');
    return result;
}

void AddUniquePath(std::vector<fs::path>& paths, const fs::path& path)
{
    if (path.empty()) {
        return;
    }
    for (const auto& existing : paths) {
        if (_wcsicmp(existing.c_str(), path.c_str()) == 0) {
            return;
        }
    }
    paths.push_back(path);
}

std::optional<fs::path> ReadRegistryPath(HKEY root, const wchar_t* subkey, const wchar_t* value_name)
{
    DWORD type = 0;
    DWORD bytes = 0;
    if (RegGetValueW(root, subkey, value_name, RRF_RT_REG_SZ, &type, nullptr, &bytes) != ERROR_SUCCESS || bytes < sizeof(wchar_t)) {
        return std::nullopt;
    }
    std::vector<wchar_t> buffer(bytes / sizeof(wchar_t) + 1, L'\0');
    if (RegGetValueW(root, subkey, value_name, RRF_RT_REG_SZ, &type, buffer.data(), &bytes) != ERROR_SUCCESS) {
        return std::nullopt;
    }
    std::wstring value(buffer.data());
    std::replace(value.begin(), value.end(), L'/', L'\\');
    return fs::path(value);
}

std::vector<fs::path> SteamLibraryRoots()
{
    std::vector<fs::path> roots;
    if (const auto steam_path = ReadRegistryPath(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath")) {
        AddUniquePath(roots, *steam_path);
    }
    AddUniquePath(roots, fs::path(L"C:\\Program Files (x86)\\Steam"));
    AddUniquePath(roots, fs::path(L"C:\\Program Files\\Steam"));

    const auto initial_roots = roots;
    for (const auto& root : initial_roots) {
        const auto text = ReadTextFile(root / L"steamapps" / L"libraryfolders.vdf");
        if (!text) {
            continue;
        }
        for (const auto& value : ValuesForKey(*text, "path")) {
            AddUniquePath(roots, fs::path(Utf8ToWide(value)));
        }
    }
    return roots;
}

std::optional<fs::path> FindGameInLibrary(const fs::path& library_root)
{
    const fs::path steamapps = library_root / L"steamapps";
    WIN32_FIND_DATAW data{};
    const fs::path pattern = steamapps / L"appmanifest_*.acf";
    HANDLE find = FindFirstFileW(pattern.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }

    std::optional<fs::path> result;
    do {
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            continue;
        }
        const auto text = ReadTextFile(steamapps / data.cFileName);
        if (!text) {
            continue;
        }
        const auto install_dirs = ValuesForKey(*text, "installdir");
        for (const auto& install_dir : install_dirs) {
            const fs::path candidate = steamapps / L"common" / Utf8ToWide(install_dir) / kGameExecutable;
            if (IsRegularFile(candidate)) {
                result = candidate;
                break;
            }
        }
    } while (!result && FindNextFileW(find, &data));
    FindClose(find);
    return result;
}

std::optional<fs::path> DetectGameExecutable()
{
    for (const auto& root : SteamLibraryRoots()) {
        if (const auto game = FindGameInLibrary(root)) {
            return game;
        }
    }
    return std::nullopt;
}

std::optional<fs::path> BrowseForGameExecutable()
{
    std::vector<wchar_t> file_buffer(32768, L'\0');
    std::copy(std::begin(kGameExecutable), std::end(kGameExecutable), file_buffer.begin());
    const wchar_t filter[] = L"METAPHOR.exe\0METAPHOR.exe\0Windows executables\0*.exe\0All files\0*.*\0\0";

    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = file_buffer.data();
    dialog.nMaxFile = static_cast<DWORD>(file_buffer.size());
    dialog.lpstrTitle = L"Select METAPHOR.exe";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&dialog)) {
        return std::nullopt;
    }
    fs::path selected(file_buffer.data());
    if (_wcsicmp(selected.filename().c_str(), kGameExecutable) != 0) {
        ShowError(L"Please select METAPHOR.exe from the Metaphor: ReFantazio game folder.");
        return std::nullopt;
    }
    return selected;
}

std::optional<fs::path> ChooseGameExecutable()
{
    if (const auto detected = DetectGameExecutable()) {
        const std::wstring prompt = L"The game was detected here:\n\n" + detected->wstring() +
                                    L"\n\nUse this installation?";
        const int answer = MessageBoxW(nullptr, prompt.c_str(), kProductName,
                                       MB_YESNOCANCEL | MB_ICONQUESTION | MB_SETFOREGROUND);
        if (answer == IDYES) {
            return detected;
        }
        if (answer == IDCANCEL) {
            return std::nullopt;
        }
    }
    return BrowseForGameExecutable();
}

bool FilesEqual(const fs::path& left_path, const fs::path& right_path)
{
    FILE* left = _wfopen(left_path.c_str(), L"rb");
    FILE* right = _wfopen(right_path.c_str(), L"rb");
    if (!left || !right) {
        if (left) std::fclose(left);
        if (right) std::fclose(right);
        return false;
    }
    std::array<unsigned char, 64 * 1024> left_buffer{};
    std::array<unsigned char, 64 * 1024> right_buffer{};
    bool equal = true;
    while (true) {
        const std::size_t left_read = std::fread(left_buffer.data(), 1, left_buffer.size(), left);
        const std::size_t right_read = std::fread(right_buffer.data(), 1, right_buffer.size(), right);
        if (left_read != right_read || !std::equal(left_buffer.begin(), left_buffer.begin() + left_read, right_buffer.begin())) {
            equal = false;
            break;
        }
        if (left_read < left_buffer.size()) {
            if (std::ferror(left) || std::ferror(right)) {
                equal = false;
            }
            break;
        }
    }
    std::fclose(left);
    std::fclose(right);
    return equal;
}

bool ValidatePayload(const fs::path& installer_directory, bool show_error)
{
    std::wstring missing;
    for (const wchar_t* filename : kPayloadFiles) {
        if (!IsRegularFile(installer_directory / filename)) {
            missing += L"\n- ";
            missing += filename;
        }
    }
    if (!missing.empty() && show_error) {
        ShowError(L"The installer package is incomplete. Extract the full release ZIP and try again. Missing:" + missing);
    }
    return missing.empty();
}

bool SetWineOverride()
{
    HKEY key = nullptr;
    const LSTATUS create_result = RegCreateKeyExW(
        HKEY_CURRENT_USER,
        L"Software\\Wine\\DllOverrides",
        0,
        nullptr,
        REG_OPTION_NON_VOLATILE,
        KEY_SET_VALUE,
        nullptr,
        &key,
        nullptr
    );
    if (create_result != ERROR_SUCCESS) {
        ShowError(L"The files were copied, but the winmm Wine override could not be created:\n\n" +
                  WindowsError(static_cast<DWORD>(create_result)));
        return false;
    }
    constexpr wchar_t value[] = L"native,builtin";
    const LSTATUS set_result = RegSetValueExW(
        key,
        L"winmm",
        0,
        REG_SZ,
        reinterpret_cast<const BYTE*>(value),
        sizeof(value)
    );
    RegCloseKey(key);
    if (set_result != ERROR_SUCCESS) {
        ShowError(L"The files were copied, but the winmm Wine override could not be saved:\n\n" +
                  WindowsError(static_cast<DWORD>(set_result)));
        return false;
    }
    return true;
}

bool RemoveWineOverride()
{
    const LSTATUS result = RegDeleteKeyValueW(
        HKEY_CURRENT_USER,
        L"Software\\Wine\\DllOverrides",
        L"winmm"
    );
    return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND;
}

bool CopyPayloadFile(const fs::path& source, const fs::path& destination)
{
    if (CopyFileW(source.c_str(), destination.c_str(), FALSE)) {
        return true;
    }
    ShowError(L"Could not copy:\n" + source.wstring() + L"\n\nto:\n" + destination.wstring() +
              L"\n\n" + WindowsError(GetLastError()));
    return false;
}

int InstallPatch(const fs::path& installer_directory, const fs::path& game_directory)
{
    bool replace_loader = true;
    const fs::path source_loader = installer_directory / kLoaderFile;
    const fs::path destination_loader = game_directory / kLoaderFile;
    if (IsRegularFile(destination_loader) && !FilesEqual(source_loader, destination_loader)) {
        const int answer = MessageBoxW(
            nullptr,
            L"A different winmm.dll already exists in the game folder. It may belong to another mod.\n\n"
            L"Yes: replace it with the loader included in this patch.\n"
            L"No: keep the existing winmm.dll and install the other patch files.\n"
            L"Cancel: stop installation.",
            kProductName,
            MB_YESNOCANCEL | MB_ICONWARNING | MB_SETFOREGROUND
        );
        if (answer == IDCANCEL) {
            return 1;
        }
        replace_loader = answer == IDYES;
    }

    for (const wchar_t* filename : kPayloadFiles) {
        if (_wcsicmp(filename, kLoaderFile) == 0 && !replace_loader) {
            continue;
        }
        if (_wcsicmp(filename, kIniFile) == 0 && IsRegularFile(game_directory / filename)) {
            continue;
        }
        if (!CopyPayloadFile(installer_directory / filename, game_directory / filename)) {
            return 2;
        }
    }
    if (!SetWineOverride()) {
        return 3;
    }

    ShowInfo(L"Installation completed successfully.\n\n"
             L"Any existing patch settings were preserved.\n\n"
             L"winmm is set to Native, then Builtin for this CrossOver bottle.\n\n"
             L"Fully restart Steam inside the bottle before launching the game.");
    return 0;
}

void DeleteIfPresent(const fs::path& path, std::wstring& failures)
{
    if (!IsRegularFile(path)) {
        return;
    }
    if (!DeleteFileW(path.c_str())) {
        failures += L"\n- " + path.filename().wstring() + L": " + WindowsError(GetLastError());
    }
}

int UninstallPatch(const fs::path& game_directory)
{
    std::wstring failures;
    DeleteIfPresent(game_directory / kAsiFile, failures);
    DeleteIfPresent(game_directory / kIniFile, failures);

    if (IsRegularFile(game_directory / kRuntimeFile) &&
        MessageBoxW(nullptr,
                    L"Remove libwinpthread-1.dll?\n\nChoose No if another plugin may use this runtime file.",
                    kProductName,
                    MB_YESNO | MB_ICONQUESTION | MB_SETFOREGROUND) == IDYES) {
        DeleteIfPresent(game_directory / kRuntimeFile, failures);
    }
    if (IsRegularFile(game_directory / kLoaderFile) &&
        MessageBoxW(nullptr,
                    L"Remove winmm.dll?\n\nChoose No if another ASI mod uses this loader.",
                    kProductName,
                    MB_YESNO | MB_ICONQUESTION | MB_SETFOREGROUND) == IDYES) {
        DeleteIfPresent(game_directory / kLoaderFile, failures);
    }
    if (MessageBoxW(nullptr,
                    L"Remove the winmm Native, then Builtin override from this CrossOver bottle?\n\n"
                    L"Choose No if another ASI mod needs it.",
                    kProductName,
                    MB_YESNO | MB_ICONQUESTION | MB_SETFOREGROUND) == IDYES &&
        !RemoveWineOverride()) {
        failures += L"\n- winmm Wine library override could not be removed";
    }

    if (!failures.empty()) {
        ShowError(L"Uninstall finished with these problems:" + failures);
        return 4;
    }
    ShowInfo(L"Uninstall completed. Restart Steam inside the bottle before launching the game.");
    return 0;
}

bool HasVerifyArgument()
{
    std::wstring command_line = GetCommandLineW();
    std::transform(command_line.begin(), command_line.end(), command_line.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(std::towlower(character));
    });
    return command_line.find(L"--verify-package") != std::wstring::npos;
}
} // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    const fs::path installer_directory = ModuleDirectory();
    if (installer_directory.empty()) {
        ShowError(L"Could not determine the installer directory.");
        return 10;
    }
    if (!ValidatePayload(installer_directory, !HasVerifyArgument())) {
        return 11;
    }
    if (HasVerifyArgument()) {
        return 0;
    }

    const int action = MessageBoxW(
        nullptr,
        L"Run this program inside the CrossOver bottle that contains Steam and Metaphor: ReFantazio.\n\n"
        L"Yes: Install or repair the patch\n"
        L"No: Uninstall the patch\n"
        L"Cancel: Exit without changes",
        kProductName,
        MB_YESNOCANCEL | MB_ICONQUESTION | MB_SETFOREGROUND
    );
    if (action == IDCANCEL) {
        return 0;
    }

    const auto game_executable = ChooseGameExecutable();
    if (!game_executable) {
        return 1;
    }
    const fs::path game_directory = game_executable->parent_path();
    return action == IDYES ? InstallPatch(installer_directory, game_directory)
                           : UninstallPatch(game_directory);
}
