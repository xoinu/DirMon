#include <Windows.h>
#include <iostream>
#include <thread>
#include <mutex>
#include <string>
#include <vector>

using namespace std;

//-----------------------------------------------------------------------------
static void appendWord(vector<wstring>& lines, const wstring&& w, size_t width)
{
    if (lines.back().length() + w.length() + 1 > width)
    {
        lines.push_back(w);
    }
    else
    {
        if (!lines.back().empty())
            lines.back() += L' ';
        lines.back() += w;
    }
}

//-----------------------------------------------------------------------------
static vector<wstring> split(const wstring& s, size_t width)
{
    if (s.length() < width)
        return vector<wstring>(1, s);

    vector<wstring> res(1);
    size_t prev = 0;
    size_t next = s.find_first_of(L' ', 0);

    while (next != s.npos)
    {
        appendWord(res, s.substr(prev, next - prev), width);
        prev = s.find_first_not_of(L' ', next);
        next = s.find_first_of(L' ', prev);
    }

    if (prev != next)
        appendWord(res, s.substr(prev, next - prev), width);

    return res;
}

//-----------------------------------------------------------------------------
static void printUsage()
{
    const wstring rawString =
        L"DirMon is a utility to watch changes in a directory using a change notification handle created with FindFirstChangeNotificationW(). "
        L"It executes a batch file specified with the second argument when it receives a change notification. "
        L"Multiple notifications received within less than 5 seconds intervals are handled as single notification so that it will not execute the batch file too often.";
    CONSOLE_SCREEN_BUFFER_INFO info = {};
    size_t width = 80;

    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info))
        width = info.dwSize.X;

    auto lines = split(rawString, width - 1);

    wcout << LR"__RAW_STRING__(
File Monitor : Copyright (c) 2021 Junnosuke Yamazaki : 2021-05-29

Usage: DirMon <path_of_directory_to_watch> <action_bat>

)__RAW_STRING__";

    for (auto& l : lines)
        wcout << l << L'\n';

    wcout << endl;
}

//-----------------------------------------------------------------------------
static void fatalError(const wchar_t* mess, int exitCode = 1)
{
    wcerr << mess << endl;
    exit(exitCode);
}

//-----------------------------------------------------------------------------
static void fatalError(int exitCode = 1)
{
    do
    {
        DWORD err = GetLastError();

        if (!err)
            break;

        const wchar_t* errBuf = nullptr;
        constexpr DWORD dwFlags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
        FormatMessageW(dwFlags, nullptr, err, 0, (LPWSTR)&errBuf, 0, nullptr);

        if (!errBuf)
            break;

        wcerr << errBuf << endl;
        LocalFree((HLOCAL)errBuf);
        errBuf = nullptr;
    }
    while (0);

    exit(exitCode);
}

//-----------------------------------------------------------------------------
static void monitor(HANDLE h, const wstring& batPath)
{
    bool done = false;
    mutex muxSig;
    time_t timeSig = 0;
    time_t timeSigLast = 0;

    auto thd = thread([&]()
        {
            while (!done)
            {
                ::Sleep(5000);

                if (!timeSig)
                    continue;

                const time_t now = time(nullptr);

                if (now - timeSig < 60)
                {
                    if (now - timeSigLast < 5)
                    {
                        // Wait 5 more seconds if signal keeps coming.
                        continue;
                    }
                }
                else
                {
                    // Ignite when the original signal was more than 1 minute old while new signal keeps coming.
                }

                lock_guard<mutex> lock(muxSig);
                wchar_t dateBuf[64] = {};
                _wctime64_s(dateBuf, &timeSig);

                for (wchar_t& ch : dateBuf)
                {
                    if (ch == L'\r' || ch == L'\n')
                    {
                        ch = L'\0';
                        break;
                    }
                }

                wclog << L'[' << dateBuf << L"] Detected a modification." << endl;
                
                if (_wsystem(batPath.c_str()))
                    wcerr << L'[' << dateBuf << L"] Non-zero error code was returned by " << batPath << endl;

                timeSig = 0;
                timeSigLast = 0;
            }
        });

    do
    {
        WaitForSingleObject(h, INFINITE);
        lock_guard<mutex> lock(muxSig);
        time_t now = time(nullptr);

        wchar_t dateBuf[64] = {};
        _wctime64_s(dateBuf, &now);

        for (wchar_t& ch : dateBuf)
        {
            if (ch == L'\r' || ch == L'\n')
            {
                ch = L'\0';
                break;
            }
        }

        wclog << L'[' << dateBuf << L"] Detected a modification. (raw)" << endl;
        if (!timeSig)
            timeSig = now;
        timeSigLast = now;
    }
    while (FindNextChangeNotification(h));

    done = true;
    thd.join();
}

//-----------------------------------------------------------------------------
static void verifyPath(const wstring& path)
{
    WIN32_FIND_DATAW data = {};
    auto h = FindFirstFileW(path.c_str(), &data);
    if (h == INVALID_HANDLE_VALUE)
        fatalError(1);
    FindClose(h);
}

//-----------------------------------------------------------------------------
int wmain(int argc, wchar_t* argv[])
{
    if (argc == 1)
    {
        printUsage();
        return 0;
    }

    if (argc < 3)
        fatalError(L"too few arguments");

    wstring path = argv[1];

    // Clean up the trailing back slash
    while (!path.empty() && path.at(path.length() - 1) == L'\\')
        path = path.substr(0, path.length() - 1);

    verifyPath(path);

    const wstring batPath = argv[2];
    const size_t batPathLen = batPath.length();

    // The second parameter must be a ".bat" file
    if (batPathLen < 4 || batPath[batPathLen - 4] != L'.' ||
        towlower(batPath[batPathLen - 3]) != L'b' ||
        towlower(batPath[batPathLen - 2]) != L'a' ||
        towlower(batPath[batPathLen - 1]) != L't')
        fatalError(L"the second parameter must be a \".bat\" file");

    verifyPath(batPath);
    constexpr DWORD dwNotifyFilter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE;
    auto h = FindFirstChangeNotificationW(path.c_str(), TRUE, dwNotifyFilter);
    if (h == INVALID_HANDLE_VALUE)
        fatalError(1);

    wclog << L"Start monitoring " << path << L" ..." << endl;
    monitor(h, batPath);
    FindCloseChangeNotification(h);
    h = nullptr;

    return 0;
}
