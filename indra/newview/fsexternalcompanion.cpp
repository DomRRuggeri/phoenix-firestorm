/**
 * @file fsexternalcompanion.cpp
 * @brief Standalone native host process for Firestorm companion windows.
 */

#include <string>
#include <vector>
#include <fstream>
#include <array>
#include <algorithm>
#include <map>
#include <set>
#include <cstdio>
#include <cwctype>
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <commdlg.h>
#include <richedit.h>

namespace
{
    constexpr const wchar_t* WINDOW_CLASS_NAME = L"FirestormExternalCompanion";
    constexpr int IDI_EXTERNAL_COMPANION = 1;
    constexpr int DEFAULT_WIDTH = 998;
    constexpr int DEFAULT_HEIGHT = 799;

    struct AppTheme
    {
        COLORREF mBackground = RGB(40, 42, 54);
        COLORREF mSurface = RGB(68, 71, 90);
        COLORREF mField = RGB(30, 31, 41);
        COLORREF mBorder = RGB(98, 114, 164);
        COLORREF mText = RGB(248, 248, 242);
        COLORREF mMutedText = RGB(189, 147, 249);
        COLORREF mAccent = RGB(255, 121, 198);
        COLORREF mAccentDark = RGB(255, 85, 170);
        COLORREF mButtonPressed = RGB(80, 83, 104);
    };

    struct DisplayMessage
    {
        std::wstring mText;
        std::wstring mKey;
        bool mHistory = false;
    };

    struct WindowState
    {
        std::wstring mTarget;
        HWND mStatus = nullptr;
        HWND mSessions = nullptr;
        HWND mMessages = nullptr;
        HWND mInput = nullptr;
        HWND mSend = nullptr;
        HWND mCommTabs[5] = {};
        int mActiveCommPanel = 4;
        int mAllSplitX = 62;
        int mAllSplitY = 70;
        bool mDraggingAllVerticalSplitter = false;
        bool mDraggingAllHorizontalSplitter = false;
        RECT mAllPanelRect = {};
        RECT mAllVerticalSplitterRect = {};
        RECT mAllHorizontalSplitterRect = {};
        HWND mNearbyMessages = nullptr;
        HWND mNearbyInput = nullptr;
        HWND mNearbySend = nullptr;
        HWND mFriendsSearch = nullptr;
        HWND mFriendsList = nullptr;
        HWND mPeopleTabs[3] = {};
        HWND mPeopleList = nullptr;
        int mPeopleActiveTab = 0;
        int mPeopleSortColumn[3] = { 1, 0, 0 };
        bool mPeopleSortAscending[3] = { true, true, true };
        std::array<std::vector<std::wstring>, 3> mPeopleRows;
        std::array<std::vector<std::wstring>, 3> mPeopleIds;
        std::array<std::vector<std::wstring>, 3> mRenderedPeopleIds;
        std::map<std::wstring, std::wstring> mNearbyPeople;
        std::map<std::wstring, std::wstring> mPendingNearbyPeople;
        bool mNearbyPeopleInitialized = false;
        bool mPeopleRefreshDeferred = false;
        std::vector<std::wstring> mFriendRows;
        std::vector<std::wstring> mFriendIds;
        std::vector<std::wstring> mRenderedFriendRows;
        std::vector<std::wstring> mRenderedFriendIds;
        int mRenderedFriendsSortColumn = -1;
        bool mRenderedFriendsSortAscending = true;
        std::wstring mRenderedFriendsFilter;
        int mFriendsSortColumn = 0;
        bool mFriendsSortAscending = true;
        std::wstring mPendingOpenParticipantId;
        std::set<std::wstring> mLocallyClosedSessionIds;
        bool mFriendsRefreshDeferred = false;
        std::map<std::wstring, std::vector<DisplayMessage>> mSessionMessages;
        std::map<std::wstring, std::set<std::wstring>> mSessionMessageKeys;
        HANDLE mPipe = INVALID_HANDLE_VALUE;
        std::wstring mPipeName;
        bool mClosing = false;
        bool mReconnectInProgress = false;
        DWORD mLastPipeReadTick = 0;
        HFONT mFont = nullptr;
        HFONT mHeaderFont = nullptr;
        HBRUSH mBackgroundBrush = nullptr;
        HBRUSH mSurfaceBrush = nullptr;
        HBRUSH mFieldBrush = nullptr;
        WNDPROC mMessagesProc = nullptr;
        WNDPROC mNearbyMessagesProc = nullptr;
        AppTheme mTheme;
    };

    struct SessionInfo
    {
        std::wstring mSessionId;
        std::wstring mName;
        std::wstring mOtherParticipantId;
        std::wstring mDialog;
        bool mHasUnread = false;
        bool mIsTyping = false;
    };

    constexpr int IDC_SESSIONS = 1001;
    constexpr int IDC_MESSAGES = 1002;
    constexpr int IDC_INPUT = 1003;
    constexpr int IDC_SEND = 1004;
    constexpr int IDC_PEOPLE_NEARBY = 1010;
    constexpr int IDC_PEOPLE_RECENT = 1011;
    constexpr int IDC_PEOPLE_BLOCKED = 1012;
    constexpr int IDC_PEOPLE_LIST = 1013;
    constexpr int IDC_COMM_IMS = 1020;
    constexpr int IDC_COMM_NEARBY = 1021;
    constexpr int IDC_COMM_FRIENDS = 1022;
    constexpr int IDC_COMM_PEOPLE = 1023;
    constexpr int IDC_COMM_ALL = 1024;
    constexpr int IDC_NEARBY_MESSAGES = 1030;
    constexpr int IDC_NEARBY_INPUT = 1031;
    constexpr int IDC_NEARBY_SEND = 1032;
    constexpr int IDC_FRIENDS_LIST = 1040;
    constexpr int IDC_FRIENDS_SEARCH = 1041;
    constexpr int IDM_THEME_DRACULA = 2001;
    constexpr int IDM_THEME_SAVE = 2002;
    constexpr int IDM_THEME_LOAD = 2003;
    constexpr int IDM_SESSION_CLOSE = 2101;
    constexpr UINT WM_PIPE_LINE = WM_APP + 1;
    constexpr UINT WM_PIPE_DISCONNECTED = WM_APP + 2;
    constexpr UINT WM_PIPE_RECONNECTED = WM_APP + 3;
    constexpr UINT_PTR IDT_PIPE_HEARTBEAT = 3001;
    constexpr int CHAT_TYPE_RADAR = 12;
    constexpr COLORREF NOTICE_TEXT_COLOR = RGB(139, 233, 253);
    constexpr DWORD LISTBOX_STYLE = WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS;
    constexpr DWORD LISTBOX_NOTIFY_STYLE = LISTBOX_STYLE | LBS_NOTIFY;
    constexpr DWORD LISTBOX_TABS_STYLE = LISTBOX_STYLE | LBS_USETABSTOPS;
    constexpr DWORD LISTBOX_TABS_NOTIFY_STYLE = LISTBOX_TABS_STYLE | LBS_NOTIFY;
    constexpr DWORD MESSAGE_VIEW_STYLE = WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_NOHIDESEL;

    bool is_rich_edit(HWND hwnd);
    int compare_table_rows(const std::wstring& left, const std::wstring& right, int column, bool people);
    std::wstring table_header_text(const wchar_t* const* labels, int count, int sort_column, bool ascending);
    int table_column_from_client_x(HWND listbox, int control_id, int x);
    void apply_dark_title_bar(HWND hwnd, const AppTheme& theme);

    void trace_line(const std::string& message)
    {
        char appdata[MAX_PATH] = {};
        DWORD length = GetEnvironmentVariableA("APPDATA", appdata, MAX_PATH);
        std::string path = (length > 0 && length < MAX_PATH) ? std::string(appdata) : ".";
        path += "\\Firestorm_x64\\logs\\external_companion_trace.log";

        std::ofstream out(path, std::ios::app);
        if (!out.is_open())
        {
            return;
        }

        SYSTEMTIME st = {};
        GetLocalTime(&st);
        out << st.wHour << ":" << st.wMinute << ":" << st.wSecond << "." << st.wMilliseconds
            << " pid=" << GetCurrentProcessId() << " " << message << "\n";
    }

    bool color_from_hex(const std::string& value, COLORREF* color)
    {
        if (!color || value.size() != 7 || value[0] != '#')
        {
            return false;
        }

        unsigned int rgb = 0;
        if (sscanf_s(value.c_str() + 1, "%x", &rgb) != 1)
        {
            return false;
        }

        *color = RGB((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff);
        return true;
    }

    bool color_from_hex(const std::wstring& value, COLORREF* color)
    {
        if (!color || value.size() != 7 || value[0] != L'#')
        {
            return false;
        }

        unsigned int rgb = 0;
        if (swscanf_s(value.c_str() + 1, L"%x", &rgb) != 1)
        {
            return false;
        }

        *color = RGB((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff);
        return true;
    }

    std::string color_to_hex(COLORREF color)
    {
        char buffer[16] = {};
        sprintf_s(buffer, "#%02X%02X%02X", GetRValue(color), GetGValue(color), GetBValue(color));
        return buffer;
    }

    void reset_theme_brushes(WindowState* state)
    {
        if (!state)
        {
            return;
        }

        if (state->mBackgroundBrush)
        {
            DeleteObject(state->mBackgroundBrush);
        }
        if (state->mSurfaceBrush)
        {
            DeleteObject(state->mSurfaceBrush);
        }
        if (state->mFieldBrush)
        {
            DeleteObject(state->mFieldBrush);
        }

        state->mBackgroundBrush = CreateSolidBrush(state->mTheme.mBackground);
        state->mSurfaceBrush = CreateSolidBrush(state->mTheme.mSurface);
        state->mFieldBrush = CreateSolidBrush(state->mTheme.mField);
    }

    void invalidate_theme(HWND hwnd, WindowState* state)
    {
        if (!hwnd || !state)
        {
            return;
        }

        for (HWND button : state->mCommTabs)
        {
            if (button)
            {
                InvalidateRect(button, nullptr, TRUE);
            }
        }
        for (HWND button : state->mPeopleTabs)
        {
            if (button)
            {
                InvalidateRect(button, nullptr, TRUE);
            }
        }
        if (state->mSend)
        {
            InvalidateRect(state->mSend, nullptr, TRUE);
        }
        if (state->mNearbySend)
        {
            InvalidateRect(state->mNearbySend, nullptr, TRUE);
        }
        for (HWND view : { state->mMessages, state->mNearbyMessages })
        {
            if (view && is_rich_edit(view))
            {
                SendMessageW(view, EM_SETBKGNDCOLOR, 0, state->mTheme.mField);
            }
        }
        apply_dark_title_bar(hwnd, state->mTheme);
        RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    }

    void save_theme_file(const AppTheme& theme, const wchar_t* path)
    {
        FILE* file = nullptr;
        if (_wfopen_s(&file, path, L"w, ccs=UTF-8") != 0 || !file)
        {
            return;
        }

        fwprintf(file, L"background=%S\n", color_to_hex(theme.mBackground).c_str());
        fwprintf(file, L"surface=%S\n", color_to_hex(theme.mSurface).c_str());
        fwprintf(file, L"field=%S\n", color_to_hex(theme.mField).c_str());
        fwprintf(file, L"border=%S\n", color_to_hex(theme.mBorder).c_str());
        fwprintf(file, L"text=%S\n", color_to_hex(theme.mText).c_str());
        fwprintf(file, L"muted=%S\n", color_to_hex(theme.mMutedText).c_str());
        fwprintf(file, L"accent=%S\n", color_to_hex(theme.mAccent).c_str());
        fwprintf(file, L"accentDark=%S\n", color_to_hex(theme.mAccentDark).c_str());
        fwprintf(file, L"buttonPressed=%S\n", color_to_hex(theme.mButtonPressed).c_str());
        fclose(file);
    }

    bool load_theme_file(AppTheme* theme, const wchar_t* path)
    {
        if (!theme)
        {
            return false;
        }

        FILE* file = nullptr;
        if (_wfopen_s(&file, path, L"r, ccs=UTF-8") != 0 || !file)
        {
            return false;
        }

        wchar_t line[256] = {};
        while (fgetws(line, 256, file))
        {
            std::wstring wide_line(line);
            while (!wide_line.empty() && (wide_line.back() == L'\n' || wide_line.back() == L'\r'))
            {
                wide_line.pop_back();
            }

            const size_t equals = wide_line.find(L'=');
            if (equals == std::wstring::npos)
            {
                continue;
            }

            std::wstring key = wide_line.substr(0, equals);
            std::wstring value = wide_line.substr(equals + 1);
            COLORREF color = 0;
            if (!color_from_hex(value, &color))
            {
                continue;
            }

            if (key == L"background") theme->mBackground = color;
            else if (key == L"surface") theme->mSurface = color;
            else if (key == L"field") theme->mField = color;
            else if (key == L"border") theme->mBorder = color;
            else if (key == L"text") theme->mText = color;
            else if (key == L"muted") theme->mMutedText = color;
            else if (key == L"accent") theme->mAccent = color;
            else if (key == L"accentDark") theme->mAccentDark = color;
            else if (key == L"buttonPressed") theme->mButtonPressed = color;
        }

        fclose(file);
        return true;
    }

    std::wstring get_argument(int argc, wchar_t** argv, const wchar_t* name, const wchar_t* fallback)
    {
        for (int index = 1; index + 1 < argc; ++index)
        {
            if (wcscmp(argv[index], name) == 0)
            {
                return argv[index + 1];
            }
        }

        return fallback;
    }

    std::string wide_to_utf8(const std::wstring& value)
    {
        if (value.empty())
        {
            return std::string();
        }

        const int required = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (required <= 0)
        {
            return std::string();
        }

        std::string result(static_cast<size_t>(required), '\0');
        WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, &result[0], required, nullptr, nullptr);
        if (!result.empty() && result.back() == '\0')
        {
            result.pop_back();
        }
        return result;
    }

    std::wstring utf8_to_wide(const std::string& value)
    {
        if (value.empty())
        {
            return std::wstring();
        }

        const int required = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
        if (required <= 0)
        {
            return std::wstring();
        }

        std::wstring result(static_cast<size_t>(required), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, &result[0], required);
        if (!result.empty() && result.back() == L'\0')
        {
            result.pop_back();
        }
        return result;
    }

    std::vector<std::string> split_pipe_line(const std::string& line)
    {
        std::vector<std::string> parts;
        size_t start = 0;
        while (start <= line.size())
        {
            const size_t end = line.find('|', start);
            if (end == std::string::npos)
            {
                parts.push_back(line.substr(start));
                break;
            }
            parts.push_back(line.substr(start, end - start));
            start = end + 1;
        }
        return parts;
    }

    std::wstring make_message_key(const std::vector<std::string>& parts)
    {
        if (parts.size() >= 6 && !parts[5].empty())
        {
            std::wstring key = L"index:";
            key += utf8_to_wide(parts[5]);
            return key;
        }

        std::wstring key;
        if (parts.size() >= 2)
        {
            key += utf8_to_wide(parts[1]);
        }
        key += L"|";
        if (parts.size() >= 3)
        {
            key += utf8_to_wide(parts[2]);
        }
        key += L"|";
        if (parts.size() >= 4)
        {
            key += utf8_to_wide(parts[3]);
        }
        key += L"|";
        if (parts.size() >= 5)
        {
            key += utf8_to_wide(parts[4]);
        }
        return key;
    }

    bool remember_message_key(WindowState* state, const std::wstring& session_id, const std::wstring& key)
    {
        if (!state || session_id.empty() || key.empty())
        {
            return true;
        }

        return state->mSessionMessageKeys[session_id].insert(key).second;
    }

    bool write_pipe_line(HANDLE pipe, const std::string& line)
    {
        std::string payload = line;
        payload += "\n";

        DWORD written = 0;
        const bool ok = WriteFile(pipe, payload.data(), static_cast<DWORD>(payload.size()), &written, nullptr)
            && written == payload.size();
        trace_line(std::string("write_pipe_line ") + (ok ? "ok " : "failed ") + line.substr(0, 180));
        return ok;
    }

    struct PipeWriteRequest
    {
        HANDLE mPipe = INVALID_HANDLE_VALUE;
        std::string mLine;
    };

    void async_write_pipe_line(HANDLE pipe, const std::string& line)
    {
        if (pipe == INVALID_HANDLE_VALUE)
        {
            return;
        }

        HANDLE duplicate = INVALID_HANDLE_VALUE;
        if (!DuplicateHandle(
                GetCurrentProcess(),
                pipe,
                GetCurrentProcess(),
                &duplicate,
                0,
                FALSE,
                DUPLICATE_SAME_ACCESS))
        {
            trace_line("async_write_pipe_line duplicate failed " + std::to_string(GetLastError()));
            return;
        }

        auto request = new PipeWriteRequest();
        request->mPipe = duplicate;
        request->mLine = line;
        CreateThread(nullptr, 0, [](LPVOID param) -> DWORD
        {
            auto request = reinterpret_cast<PipeWriteRequest*>(param);
            write_pipe_line(request->mPipe, request->mLine);
            CloseHandle(request->mPipe);
            delete request;
            return 0;
        }, request, 0, nullptr);
    }

    HANDLE open_pipe_with_retry(const std::wstring& pipe_name)
    {
        constexpr DWORD WAIT_SLICE_MS = 250;
        constexpr DWORD TOTAL_WAIT_MS = 8000;

        DWORD waited = 0;
        while (waited <= TOTAL_WAIT_MS)
        {
            HANDLE pipe = CreateFileW(
                pipe_name.c_str(),
                GENERIC_READ | GENERIC_WRITE,
                0,
                nullptr,
                OPEN_EXISTING,
                0,
                nullptr);

            if (pipe != INVALID_HANDLE_VALUE)
            {
                DWORD mode = PIPE_READMODE_BYTE;
                SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr);
                trace_line("open_pipe_with_retry connected");
                return pipe;
            }

            const DWORD error = GetLastError();
            if (error != ERROR_PIPE_BUSY && error != ERROR_FILE_NOT_FOUND)
            {
                break;
            }

            WaitNamedPipeW(pipe_name.c_str(), WAIT_SLICE_MS);
            waited += WAIT_SLICE_MS;
        }

        return INVALID_HANDLE_VALUE;
    }

    void append_listbox_line(HWND listbox, const std::wstring& line, bool scroll_to_bottom = true)
    {
        if (!listbox)
        {
            return;
        }
        SendMessageW(listbox, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line.c_str()));
        if (scroll_to_bottom)
        {
            const LRESULT count = SendMessageW(listbox, LB_GETCOUNT, 0, 0);
            if (count > 0)
            {
                SendMessageW(listbox, LB_SETTOPINDEX, count - 1, 0);
            }
        }
    }

    bool is_rich_edit(HWND hwnd)
    {
        wchar_t class_name[64] = {};
        GetClassNameW(hwnd, class_name, 64);
        return wcscmp(class_name, L"RICHEDIT50W") == 0 || wcscmp(class_name, L"RichEdit20W") == 0;
    }

    std::wstring action_body_from_message(const std::wstring& message)
    {
        if (message == L"/me")
        {
            return std::wstring();
        }
        if (message.rfind(L"/me ", 0) == 0)
        {
            return message.substr(4);
        }
        return std::wstring();
    }

    void append_rich_segment(HWND edit, const std::wstring& text, COLORREF color, bool italic)
    {
        if (text.empty())
        {
            return;
        }

        CHARFORMAT2W format = {};
        format.cbSize = sizeof(format);
        format.dwMask = CFM_COLOR | CFM_ITALIC;
        format.dwEffects = italic ? CFE_ITALIC : 0;
        format.crTextColor = color;
        SendMessageW(edit, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&format));
        SendMessageW(edit, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(text.c_str()));
    }

    void append_text_line(HWND edit, const std::wstring& line, bool history = false)
    {
        if (!edit)
        {
            return;
        }

        const int length = GetWindowTextLengthW(edit);
        SendMessageW(edit, EM_SETSEL, length, length);
        SendMessageW(edit, EM_SETREADONLY, FALSE, 0);
        if (is_rich_edit(edit))
        {
            auto state = reinterpret_cast<WindowState*>(GetWindowLongPtr(GetAncestor(edit, GA_ROOT), GWLP_USERDATA));
            const AppTheme theme = state ? state->mTheme : AppTheme();
            const COLORREF name_color = history ? RGB(150, 150, 158) : theme.mAccent;
            const COLORREF body_color = history ? RGB(176, 176, 184) : theme.mText;
            const size_t separator = line.find(L": ");
            if (separator != std::wstring::npos)
            {
                const std::wstring speaker = line.substr(0, separator);
                const std::wstring message = line.substr(separator + 2);
                const std::wstring action = action_body_from_message(message);
                if (!action.empty() || message == L"/me")
                {
                    append_rich_segment(edit, speaker, name_color, true);
                    if (!action.empty())
                    {
                        append_rich_segment(edit, L" ", body_color, true);
                        append_rich_segment(edit, action, body_color, true);
                    }
                }
                else
                {
                    append_rich_segment(edit, speaker + L": ", name_color, false);
                    append_rich_segment(edit, message, body_color, false);
                }
            }
            else
            {
                append_rich_segment(edit, line, body_color, false);
            }
            append_rich_segment(edit, L"\r\n", body_color, false);
        }
        else
        {
            std::wstring text = line;
            text += L"\r\n";
            SendMessageW(edit, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(text.c_str()));
        }
        SendMessageW(edit, EM_SETREADONLY, TRUE, 0);
        SendMessageW(edit, EM_SCROLLCARET, 0, 0);
        HideCaret(edit);
    }

    void append_notice_line(HWND edit, const std::wstring& line)
    {
        if (!edit)
        {
            return;
        }

        const int length = GetWindowTextLengthW(edit);
        SendMessageW(edit, EM_SETSEL, length, length);
        SendMessageW(edit, EM_SETREADONLY, FALSE, 0);
        if (is_rich_edit(edit))
        {
            append_rich_segment(edit, line, NOTICE_TEXT_COLOR, true);
            append_rich_segment(edit, L"\r\n", NOTICE_TEXT_COLOR, false);
        }
        else
        {
            std::wstring text = line;
            text += L"\r\n";
            SendMessageW(edit, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(text.c_str()));
        }
        SendMessageW(edit, EM_SETREADONLY, TRUE, 0);
        SendMessageW(edit, EM_SCROLLCARET, 0, 0);
        HideCaret(edit);
    }

    LRESULT CALLBACK message_view_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
    {
        auto state = reinterpret_cast<WindowState*>(GetWindowLongPtr(GetAncestor(hwnd, GA_ROOT), GWLP_USERDATA));
        WNDPROC original_proc = nullptr;
        if (state)
        {
            original_proc = (hwnd == state->mNearbyMessages) ? state->mNearbyMessagesProc : state->mMessagesProc;
        }

        switch (message)
        {
            case WM_SETFOCUS:
            case WM_LBUTTONDOWN:
            case WM_LBUTTONUP:
            case WM_MOUSEMOVE:
            case WM_KEYDOWN:
            {
                const LRESULT result = original_proc ? CallWindowProcW(original_proc, hwnd, message, wparam, lparam) : DefWindowProcW(hwnd, message, wparam, lparam);
                HideCaret(hwnd);
                return result;
            }
            case WM_SETCURSOR:
                SetCursor(LoadCursor(nullptr, IDC_ARROW));
                return TRUE;
            default:
                break;
        }

        return original_proc ? CallWindowProcW(original_proc, hwnd, message, wparam, lparam) : DefWindowProcW(hwnd, message, wparam, lparam);
    }

    void subclass_message_view(WindowState* state, HWND hwnd, WNDPROC WindowState::* slot)
    {
        if (!state || !hwnd || (state->*slot))
        {
            return;
        }

        (state->*slot) = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(message_view_proc)));
    }

    bool is_people_visible(const WindowState* state)
    {
        return state && (state->mTarget == L"people" ||
            (state->mTarget == L"communications" && (state->mActiveCommPanel == 3 || state->mActiveCommPanel == 4)));
    }

    bool should_repaint_people_list(const WindowState* state)
    {
        return is_people_visible(state) && GetFocus() != state->mPeopleList;
    }

    bool is_friends_visible(const WindowState* state)
    {
        return state && state->mTarget == L"communications" &&
            (state->mActiveCommPanel == 2 || state->mActiveCommPanel == 4);
    }

    bool should_repaint_friends_list(const WindowState* state)
    {
        return is_friends_visible(state) && GetFocus() != state->mFriendsList;
    }

    std::wstring lowercase_copy(std::wstring value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch)
        {
            return static_cast<wchar_t>(std::towlower(ch));
        });
        return value;
    }

    std::wstring get_friends_filter(WindowState* state)
    {
        if (!state || !state->mFriendsSearch)
        {
            return {};
        }

        wchar_t text[256] = {};
        GetWindowTextW(state->mFriendsSearch, text, 256);
        return lowercase_copy(text);
    }

    void restore_list_selection(HWND listbox, const std::wstring& selected_text, LRESULT top_index)
    {
        if (!listbox)
        {
            return;
        }

        if (!selected_text.empty())
        {
            const LRESULT count = SendMessageW(listbox, LB_GETCOUNT, 0, 0);
            for (LRESULT index = 0; index < count; ++index)
            {
                const LRESULT text_length = SendMessageW(listbox, LB_GETTEXTLEN, index, 0);
                if (text_length <= 0)
                {
                    continue;
                }

                std::wstring row;
                row.resize(static_cast<size_t>(text_length));
                SendMessageW(listbox, LB_GETTEXT, index, reinterpret_cast<LPARAM>(row.data()));
                if (row == selected_text)
                {
                    SendMessageW(listbox, LB_SETCURSEL, index, 0);
                    break;
                }
            }
        }
        if (top_index != LB_ERR)
        {
            SendMessageW(listbox, LB_SETTOPINDEX, top_index, 0);
        }
    }

    std::wstring get_selected_list_text(HWND listbox)
    {
        if (!listbox)
        {
            return std::wstring();
        }

        const LRESULT selected = SendMessageW(listbox, LB_GETCURSEL, 0, 0);
        if (selected == LB_ERR)
        {
            return std::wstring();
        }

        const LRESULT text_length = SendMessageW(listbox, LB_GETTEXTLEN, selected, 0);
        if (text_length <= 0)
        {
            return std::wstring();
        }

        std::wstring selected_text;
        selected_text.resize(static_cast<size_t>(text_length));
        SendMessageW(listbox, LB_GETTEXT, selected, reinterpret_cast<LPARAM>(selected_text.data()));
        return selected_text;
    }

    int people_tab_index(const std::string& tab)
    {
        if (tab == "recent")
        {
            return 1;
        }
        if (tab == "blocked")
        {
            return 2;
        }
        return 0;
    }

    std::wstring people_tab_name(int tab)
    {
        switch (tab)
        {
            case 1:
                return L"Recent";
            case 2:
                return L"Blocked";
            default:
                return L"Nearby";
        }
    }

    void refresh_people_list(WindowState* state)
    {
        if (!state || !state->mPeopleList)
        {
            return;
        }

        const std::wstring selected_text = get_selected_list_text(state->mPeopleList);
        const LRESULT top_index = SendMessageW(state->mPeopleList, LB_GETTOPINDEX, 0, 0);

        SendMessageW(state->mPeopleList, WM_SETREDRAW, FALSE, 0);
        SendMessageW(state->mPeopleList, LB_RESETCONTENT, 0, 0);
        std::vector<std::pair<std::wstring, std::wstring>> people;
        auto& rows = state->mPeopleRows[state->mPeopleActiveTab];
        auto& ids = state->mPeopleIds[state->mPeopleActiveTab];
        people.reserve(rows.size());
        for (size_t index = 0; index < rows.size(); ++index)
        {
            const std::wstring id = index < ids.size() ? ids[index] : L"";
            people.emplace_back(rows[index], id);
        }

        const int sort_column = state->mPeopleSortColumn[state->mPeopleActiveTab];
        const bool ascending = state->mPeopleSortAscending[state->mPeopleActiveTab];
        std::stable_sort(people.begin(), people.end(), [sort_column, ascending](const auto& left, const auto& right)
        {
            const int result = compare_table_rows(left.first, right.first, sort_column, true);
            return ascending ? result < 0 : result > 0;
        });

        state->mRenderedPeopleIds[state->mPeopleActiveTab].clear();
        const wchar_t* people_headers[] = { L"Name", L"Distance", L"Time", L"Age" };
        append_listbox_line(state->mPeopleList, table_header_text(people_headers, 4, sort_column, ascending), false);
        for (const auto& row : people)
        {
            append_listbox_line(state->mPeopleList, row.first, false);
            state->mRenderedPeopleIds[state->mPeopleActiveTab].push_back(row.second);
        }

        restore_list_selection(state->mPeopleList, selected_text, top_index);
        SendMessageW(state->mPeopleList, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(state->mPeopleList, nullptr, TRUE);

        std::wstring status = L"People - ";
        status += people_tab_name(state->mPeopleActiveTab);
        status += L": ";
        status += std::to_wstring(state->mPeopleRows[state->mPeopleActiveTab].size());
        if (state->mTarget == L"people" || (state->mTarget == L"communications" && state->mActiveCommPanel == 3))
        {
            SetWindowTextW(state->mStatus, status.c_str());
        }
        state->mPeopleRefreshDeferred = false;
    }

    void refresh_friends_list(WindowState* state)
    {
        if (!state || !state->mFriendsList)
        {
            return;
        }

        const std::wstring selected_text = get_selected_list_text(state->mFriendsList);
        const LRESULT top_index = SendMessageW(state->mFriendsList, LB_GETTOPINDEX, 0, 0);
        const std::wstring filter = get_friends_filter(state);

        std::vector<std::pair<std::wstring, std::wstring>> friends;
        friends.reserve(state->mFriendRows.size());
        for (size_t index = 0; index < state->mFriendRows.size(); ++index)
        {
            const std::wstring id = index < state->mFriendIds.size() ? state->mFriendIds[index] : L"";
            friends.emplace_back(state->mFriendRows[index], id);
        }

        if (!filter.empty())
        {
            friends.erase(
                std::remove_if(
                    friends.begin(),
                    friends.end(),
                    [&filter](const auto& row)
                    {
                        return lowercase_copy(row.first).find(filter) == std::wstring::npos;
                    }),
                friends.end());
        }

        const int sort_column = state->mFriendsSortColumn;
        const bool ascending = state->mFriendsSortAscending;
        std::stable_sort(friends.begin(), friends.end(), [sort_column, ascending](const auto& left, const auto& right)
        {
            const int result = compare_table_rows(left.first, right.first, sort_column, false);
            return ascending ? result < 0 : result > 0;
        });
        std::vector<std::wstring> rendered_rows;
        std::vector<std::wstring> rendered_ids;
        for (const auto& friend_row : friends)
        {
            rendered_rows.push_back(friend_row.first);
            rendered_ids.push_back(friend_row.second);
        }

        if (state->mRenderedFriendRows == rendered_rows &&
            state->mRenderedFriendIds == rendered_ids &&
            state->mRenderedFriendsSortColumn == sort_column &&
            state->mRenderedFriendsSortAscending == ascending &&
            state->mRenderedFriendsFilter == filter)
        {
            state->mFriendsRefreshDeferred = false;
            return;
        }

        SendMessageW(state->mFriendsList, WM_SETREDRAW, FALSE, 0);
        SendMessageW(state->mFriendsList, LB_RESETCONTENT, 0, 0);
        const wchar_t* friend_headers[] = { L"Name", L"Status", L"Online", L"Map", L"Modify" };
        append_listbox_line(state->mFriendsList, table_header_text(friend_headers, 5, sort_column, ascending), false);
        for (const std::wstring& row : rendered_rows)
        {
            append_listbox_line(state->mFriendsList, row, false);
        }

        restore_list_selection(state->mFriendsList, selected_text, top_index);
        SendMessageW(state->mFriendsList, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(state->mFriendsList, nullptr, TRUE);
        state->mRenderedFriendRows = rendered_rows;
        state->mRenderedFriendIds = rendered_ids;
        state->mRenderedFriendsSortColumn = sort_column;
        state->mRenderedFriendsSortAscending = ascending;
        state->mRenderedFriendsFilter = filter;
        state->mFriendsRefreshDeferred = false;
    }

    void set_people_tab(WindowState* state, int tab)
    {
        if (!state)
        {
            return;
        }
        state->mPeopleActiveTab = tab;
        for (HWND button : state->mPeopleTabs)
        {
            if (button)
            {
                InvalidateRect(button, nullptr, TRUE);
            }
        }
        refresh_people_list(state);
    }

    void sort_people_from_header_click(WindowState* state)
    {
        if (!state || !state->mPeopleList)
        {
            return;
        }

        POINT cursor = {};
        GetCursorPos(&cursor);
        ScreenToClient(state->mPeopleList, &cursor);
        const int column = table_column_from_client_x(state->mPeopleList, IDC_PEOPLE_LIST, cursor.x);
        int& sort_column = state->mPeopleSortColumn[state->mPeopleActiveTab];
        bool& ascending = state->mPeopleSortAscending[state->mPeopleActiveTab];
        if (sort_column == column)
        {
            ascending = !ascending;
        }
        else
        {
            sort_column = column;
            ascending = true;
        }
        refresh_people_list(state);
        SendMessageW(state->mPeopleList, LB_SETCURSEL, static_cast<WPARAM>(-1), 0);
    }

    void sort_friends_from_header_click(WindowState* state)
    {
        if (!state || !state->mFriendsList)
        {
            return;
        }

        POINT cursor = {};
        GetCursorPos(&cursor);
        ScreenToClient(state->mFriendsList, &cursor);
        const int column = table_column_from_client_x(state->mFriendsList, IDC_FRIENDS_LIST, cursor.x);
        if (state->mFriendsSortColumn == column)
        {
            state->mFriendsSortAscending = !state->mFriendsSortAscending;
        }
        else
        {
            state->mFriendsSortColumn = column;
            state->mFriendsSortAscending = true;
        }
        refresh_friends_list(state);
        SendMessageW(state->mFriendsList, LB_SETCURSEL, static_cast<WPARAM>(-1), 0);
    }

    bool is_conversations_target(const WindowState* state)
    {
        return state && (state->mTarget == L"fs_im_container" || state->mTarget == L"communications");
    }

    bool is_nearby_target(const WindowState* state)
    {
        return state && (state->mTarget == L"nearby_chat" || state->mTarget == L"communications");
    }

    bool is_people_target(const WindowState* state)
    {
        return state && (state->mTarget == L"people" || state->mTarget == L"communications");
    }

    bool is_communications_target(const WindowState* state)
    {
        return state && state->mTarget == L"communications";
    }

    void append_nearby_presence_notice(WindowState* state, const std::wstring& name, bool entered)
    {
        if (!state || !is_nearby_target(state))
        {
            return;
        }

        std::wstring line = L"* ";
        line += name.empty() ? L"(unknown)" : name;
        line += entered ? L" entered nearby range" : L" left nearby range";
        append_notice_line(is_communications_target(state) ? state->mNearbyMessages : state->mMessages, line);
    }

    void update_nearby_presence_notices(WindowState* state)
    {
        if (!state)
        {
            return;
        }

        if (state->mNearbyPeopleInitialized)
        {
            for (const auto& entry : state->mPendingNearbyPeople)
            {
                if (state->mNearbyPeople.find(entry.first) == state->mNearbyPeople.end())
                {
                    append_nearby_presence_notice(state, entry.second, true);
                }
            }

            for (const auto& entry : state->mNearbyPeople)
            {
                if (state->mPendingNearbyPeople.find(entry.first) == state->mPendingNearbyPeople.end())
                {
                    append_nearby_presence_notice(state, entry.second, false);
                }
            }
        }

        state->mNearbyPeople = state->mPendingNearbyPeople;
        state->mPendingNearbyPeople.clear();
        state->mNearbyPeopleInitialized = true;
    }

    SessionInfo* get_selected_session(WindowState* state)
    {
        if (!state || !state->mSessions)
        {
            return nullptr;
        }

        const LRESULT selected = SendMessageW(state->mSessions, LB_GETCURSEL, 0, 0);
        if (selected == LB_ERR)
        {
            return nullptr;
        }

        return reinterpret_cast<SessionInfo*>(SendMessageW(state->mSessions, LB_GETITEMDATA, selected, 0));
    }

    void set_session_unread(WindowState* state, const std::wstring& session_id, bool unread)
    {
        if (!state || !state->mSessions)
        {
            return;
        }

        const LRESULT count = SendMessageW(state->mSessions, LB_GETCOUNT, 0, 0);
        for (LRESULT index = 0; index < count; ++index)
        {
            auto session = reinterpret_cast<SessionInfo*>(SendMessageW(state->mSessions, LB_GETITEMDATA, index, 0));
            if (session && session->mSessionId == session_id)
            {
                if (session->mHasUnread != unread)
                {
                    session->mHasUnread = unread;
                    InvalidateRect(state->mSessions, nullptr, TRUE);
                }
                return;
            }
        }
    }

    void set_session_typing(WindowState* state, const std::wstring& session_id, bool typing)
    {
        if (!state || !state->mSessions)
        {
            return;
        }

        const LRESULT count = SendMessageW(state->mSessions, LB_GETCOUNT, 0, 0);
        for (LRESULT index = 0; index < count; ++index)
        {
            auto session = reinterpret_cast<SessionInfo*>(SendMessageW(state->mSessions, LB_GETITEMDATA, index, 0));
            if (session && session->mSessionId == session_id)
            {
                if (session->mIsTyping != typing)
                {
                    session->mIsTyping = typing;
                    InvalidateRect(state->mSessions, nullptr, TRUE);
                }
                return;
            }
        }
    }

    void refresh_conversation_messages(WindowState* state)
    {
        if (!state || !state->mMessages)
        {
            return;
        }

        SetWindowTextW(state->mMessages, L"");

        SessionInfo* session = get_selected_session(state);
        if (!session)
        {
            SetWindowTextW(state->mStatus, L"Ready. No conversation selected.");
            return;
        }
        set_session_unread(state, session->mSessionId, false);

        const auto messages = state->mSessionMessages.find(session->mSessionId);
        if (messages != state->mSessionMessages.end())
        {
            for (const DisplayMessage& message : messages->second)
            {
                append_text_line(state->mMessages, message.mText, message.mHistory);
            }
        }

        std::wstring status = L"Conversation - ";
        status += session->mName;
        SetWindowTextW(state->mStatus, status.c_str());
    }

    void show_control(HWND hwnd, bool show)
    {
        if (hwnd)
        {
            ShowWindow(hwnd, show ? SW_SHOW : SW_HIDE);
        }
    }

    int clamp_int(int value, int min_value, int max_value)
    {
        if (value < min_value)
        {
            return min_value;
        }
        if (value > max_value)
        {
            return max_value;
        }
        return value;
    }

    bool point_in_rect(const RECT& rect, int x, int y)
    {
        return x >= rect.left && x <= rect.right && y >= rect.top && y <= rect.bottom;
    }

    HFONT create_ui_font(int height, int weight)
    {
        return CreateFontW(
            -height,
            0,
            0,
            0,
            weight,
            FALSE,
            FALSE,
            FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE,
            L"Segoe UI");
    }

    void set_control_font(HWND control, HFONT font)
    {
        if (control && font)
        {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        }
    }

    void apply_dark_control_theme(HWND control)
    {
        if (!control)
        {
            return;
        }

        using SetWindowThemeFn = HRESULT(WINAPI*)(HWND, LPCWSTR, LPCWSTR);
        HMODULE uxtheme = LoadLibraryW(L"uxtheme.dll");
        if (!uxtheme)
        {
            return;
        }

        auto set_window_theme = reinterpret_cast<SetWindowThemeFn>(GetProcAddress(uxtheme, "SetWindowTheme"));
        if (set_window_theme)
        {
            set_window_theme(control, L"DarkMode_Explorer", nullptr);
        }
        FreeLibrary(uxtheme);
    }

    void apply_dark_title_bar(HWND hwnd, const AppTheme& theme)
    {
        if (!hwnd)
        {
            return;
        }

        using DwmSetWindowAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
        HMODULE dwmapi = LoadLibraryW(L"dwmapi.dll");
        if (!dwmapi)
        {
            return;
        }

        auto set_window_attribute = reinterpret_cast<DwmSetWindowAttributeFn>(GetProcAddress(dwmapi, "DwmSetWindowAttribute"));
        if (set_window_attribute)
        {
            BOOL enabled = TRUE;
            constexpr DWORD DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20H1 = 19;
            constexpr DWORD DWMWA_USE_IMMERSIVE_DARK_MODE = 20;
            constexpr DWORD DWMWA_BORDER_COLOR = 34;
            constexpr DWORD DWMWA_CAPTION_COLOR = 35;
            constexpr DWORD DWMWA_TEXT_COLOR = 36;

            set_window_attribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &enabled, sizeof(enabled));
            set_window_attribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20H1, &enabled, sizeof(enabled));
            COLORREF caption = theme.mBackground;
            COLORREF text = theme.mText;
            COLORREF border = theme.mBorder;
            set_window_attribute(hwnd, DWMWA_CAPTION_COLOR, &caption, sizeof(caption));
            set_window_attribute(hwnd, DWMWA_TEXT_COLOR, &text, sizeof(text));
            set_window_attribute(hwnd, DWMWA_BORDER_COLOR, &border, sizeof(border));
        }
        FreeLibrary(dwmapi);
    }

    void apply_common_fonts(WindowState* state)
    {
        if (!state)
        {
            return;
        }

        set_control_font(state->mStatus, state->mHeaderFont);
        set_control_font(state->mSessions, state->mFont);
        set_control_font(state->mMessages, state->mFont);
        set_control_font(state->mInput, state->mFont);
        set_control_font(state->mSend, state->mFont);
        set_control_font(state->mNearbyMessages, state->mFont);
        set_control_font(state->mNearbyInput, state->mFont);
        set_control_font(state->mNearbySend, state->mFont);
        set_control_font(state->mFriendsSearch, state->mFont);
        set_control_font(state->mFriendsList, state->mFont);
        set_control_font(state->mPeopleList, state->mFont);
        for (HWND tab : state->mCommTabs)
        {
            set_control_font(tab, state->mFont);
        }
        for (HWND tab : state->mPeopleTabs)
        {
            set_control_font(tab, state->mFont);
        }
    }

    void apply_control_details(WindowState* state)
    {
        if (!state)
        {
            return;
        }

        const LPARAM edit_margins = MAKELPARAM(8, 8);
        for (HWND edit : { state->mMessages, state->mInput, state->mNearbyMessages, state->mNearbyInput, state->mFriendsSearch })
        {
            if (edit)
            {
                SendMessageW(edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, edit_margins);
                apply_dark_control_theme(edit);
            }
        }

        for (HWND view : { state->mMessages, state->mNearbyMessages })
        {
            if (view && is_rich_edit(view))
            {
                SendMessageW(view, EM_SETBKGNDCOLOR, 0, state->mTheme.mField);
                HideCaret(view);
            }
        }

        int people_tabs[] = { 140, 205, 270 };
        if (state->mPeopleList)
        {
            SendMessageW(state->mPeopleList, LB_SETTABSTOPS, 3, reinterpret_cast<LPARAM>(people_tabs));
            apply_dark_control_theme(state->mPeopleList);
        }

        int friend_tabs[] = { 180, 260, 330, 400 };
        if (state->mFriendsList)
        {
            SendMessageW(state->mFriendsList, LB_SETTABSTOPS, 4, reinterpret_cast<LPARAM>(friend_tabs));
            apply_dark_control_theme(state->mFriendsList);
        }

        if (state->mSessions)
        {
            apply_dark_control_theme(state->mSessions);
        }
    }

    bool is_primary_button_id(UINT id)
    {
        return id == IDC_SEND || id == IDC_NEARBY_SEND;
    }

    bool is_active_tab(const WindowState* state, UINT id)
    {
        if (!state)
        {
            return false;
        }

        if (id == IDC_COMM_IMS) return state->mActiveCommPanel == 0;
        if (id == IDC_COMM_NEARBY) return state->mActiveCommPanel == 1;
        if (id == IDC_COMM_FRIENDS) return state->mActiveCommPanel == 2;
        if (id == IDC_COMM_PEOPLE) return state->mActiveCommPanel == 3;
        if (id == IDC_COMM_ALL) return state->mActiveCommPanel == 4;
        if (id == IDC_PEOPLE_NEARBY) return state->mPeopleActiveTab == 0;
        if (id == IDC_PEOPLE_RECENT) return state->mPeopleActiveTab == 1;
        if (id == IDC_PEOPLE_BLOCKED) return state->mPeopleActiveTab == 2;
        return false;
    }

    void draw_section_label(HDC dc, const wchar_t* label, const RECT& rect, COLORREF color)
    {
        RECT text_rect = { rect.left + 2, rect.top, rect.right, rect.bottom };
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, color);
        DrawTextW(dc, label, -1, &text_rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    std::vector<std::wstring> split_tab_columns(const std::wstring& row)
    {
        std::vector<std::wstring> columns;
        size_t start = 0;
        while (start <= row.size())
        {
            const size_t end = row.find(L'\t', start);
            if (end == std::wstring::npos)
            {
                columns.push_back(row.substr(start));
                break;
            }
            columns.push_back(row.substr(start, end - start));
            start = end + 1;
        }
        return columns;
    }

    std::wstring lower_text(std::wstring value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch)
        {
            return static_cast<wchar_t>(std::towlower(ch));
        });
        return value;
    }

    double parse_sort_number(const std::wstring& value)
    {
        if (value.empty() || value == L"-")
        {
            return 0.0;
        }
        return _wtof(value.c_str());
    }

    int parse_sort_time(const std::wstring& value)
    {
        int first = 0;
        int second = 0;
        int third = 0;
        if (swscanf_s(value.c_str(), L"%d:%d:%d", &first, &second, &third) == 3)
        {
            return first * 3600 + second * 60 + third;
        }
        if (swscanf_s(value.c_str(), L"%d:%d", &first, &second) == 2)
        {
            return first * 60 + second;
        }
        return 0;
    }

    int compare_table_rows(const std::wstring& left, const std::wstring& right, int column, bool people)
    {
        const std::vector<std::wstring> left_columns = split_tab_columns(left);
        const std::vector<std::wstring> right_columns = split_tab_columns(right);
        const std::wstring left_value = column >= 0 && column < static_cast<int>(left_columns.size()) ? left_columns[column] : L"";
        const std::wstring right_value = column >= 0 && column < static_cast<int>(right_columns.size()) ? right_columns[column] : L"";

        if (people && (column == 1 || column == 3))
        {
            const double left_number = parse_sort_number(left_value);
            const double right_number = parse_sort_number(right_value);
            return left_number < right_number ? -1 : left_number > right_number ? 1 : 0;
        }
        if (people && column == 2)
        {
            const int left_time = parse_sort_time(left_value);
            const int right_time = parse_sort_time(right_value);
            return left_time < right_time ? -1 : left_time > right_time ? 1 : 0;
        }

        const std::wstring left_text = lower_text(left_value);
        const std::wstring right_text = lower_text(right_value);
        const int text_compare = left_text.compare(right_text);
        if (text_compare < 0)
        {
            return -1;
        }
        if (text_compare > 0)
        {
            return 1;
        }
        return 0;
    }

    std::wstring table_header_text(const wchar_t* const* labels, int count, int sort_column, bool ascending)
    {
        std::wstring header;
        for (int index = 0; index < count; ++index)
        {
            if (index > 0)
            {
                header += L"\t";
            }
            header += labels[index];
            if (index == sort_column)
            {
                header += ascending ? L" ^" : L" v";
            }
        }
        return header;
    }

    int table_column_from_client_x(HWND listbox, int control_id, int x)
    {
        RECT rect = {};
        if (!listbox || !GetClientRect(listbox, &rect))
        {
            return 0;
        }

        const int right = rect.right - 4;
        if (control_id == IDC_PEOPLE_LIST)
        {
            const int age_width = 56;
            const int time_width = 76;
            const int distance_width = 86;
            const int name_right = right - age_width - time_width - distance_width;
            if (x < name_right) return 0;
            if (x < name_right + distance_width) return 1;
            if (x < name_right + distance_width + time_width) return 2;
            return 3;
        }

        const int modify_width = 58;
        const int map_width = 50;
        const int online_width = 58;
        const int status_width = 72;
        const int name_right = right - modify_width - map_width - online_width - status_width;
        if (x < name_right) return 0;
        if (x < name_right + status_width) return 1;
        if (x < name_right + status_width + online_width) return 2;
        if (x < name_right + status_width + online_width + map_width) return 3;
        return 4;
    }

    void draw_column_text(HDC dc, const std::wstring& text, RECT rect, UINT format)
    {
        rect.left += 6;
        rect.right -= 6;
        DrawTextW(dc, text.c_str(), -1, &rect, format | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    bool draw_table_row(DRAWITEMSTRUCT* draw, const std::wstring& row)
    {
        if (!draw || (draw->CtlID != IDC_PEOPLE_LIST && draw->CtlID != IDC_FRIENDS_LIST))
        {
            return false;
        }

        const std::vector<std::wstring> columns = split_tab_columns(row);
        const int width = draw->rcItem.right - draw->rcItem.left;
        const int left = draw->rcItem.left + 4;
        const int right = draw->rcItem.right - 4;

        if (draw->CtlID == IDC_PEOPLE_LIST)
        {
            const int age_width = 56;
            const int time_width = 76;
            const int distance_width = 86;
            RECT name_rect = { left, draw->rcItem.top, right - age_width - time_width - distance_width, draw->rcItem.bottom };
            RECT distance_rect = { name_rect.right, draw->rcItem.top, name_rect.right + distance_width, draw->rcItem.bottom };
            RECT time_rect = { distance_rect.right, draw->rcItem.top, distance_rect.right + time_width, draw->rcItem.bottom };
            RECT age_rect = { time_rect.right, draw->rcItem.top, right, draw->rcItem.bottom };
            draw_column_text(draw->hDC, columns.size() > 0 ? columns[0] : L"", name_rect, DT_LEFT);
            draw_column_text(draw->hDC, columns.size() > 1 ? columns[1] : L"", distance_rect, DT_RIGHT);
            draw_column_text(draw->hDC, columns.size() > 2 ? columns[2] : L"", time_rect, DT_RIGHT);
            draw_column_text(draw->hDC, columns.size() > 3 ? columns[3] : L"", age_rect, DT_RIGHT);
            return true;
        }

        const int modify_width = 58;
        const int map_width = 50;
        const int online_width = 58;
        const int status_width = 72;
        RECT name_rect = { left, draw->rcItem.top, right - modify_width - map_width - online_width - status_width, draw->rcItem.bottom };
        RECT status_rect = { name_rect.right, draw->rcItem.top, name_rect.right + status_width, draw->rcItem.bottom };
        RECT online_rect = { status_rect.right, draw->rcItem.top, status_rect.right + online_width, draw->rcItem.bottom };
        RECT map_rect = { online_rect.right, draw->rcItem.top, online_rect.right + map_width, draw->rcItem.bottom };
        RECT modify_rect = { map_rect.right, draw->rcItem.top, right, draw->rcItem.bottom };
        draw_column_text(draw->hDC, columns.size() > 0 ? columns[0] : L"", name_rect, DT_LEFT);
        draw_column_text(draw->hDC, columns.size() > 1 ? columns[1] : L"", status_rect, DT_LEFT);
        draw_column_text(draw->hDC, columns.size() > 2 ? columns[2] : L"", online_rect, DT_CENTER);
        draw_column_text(draw->hDC, columns.size() > 3 ? columns[3] : L"", map_rect, DT_CENTER);
        draw_column_text(draw->hDC, columns.size() > 4 ? columns[4] : L"", modify_rect, DT_CENTER);
        return true;
    }

    void set_communications_panel(WindowState* state, int panel)
    {
        if (!state)
        {
            return;
        }

        state->mActiveCommPanel = panel;
        for (HWND button : state->mCommTabs)
        {
            if (button)
            {
                InvalidateRect(button, nullptr, TRUE);
            }
        }
        RECT rect = {};
        HWND parent = nullptr;
        if (state->mStatus)
        {
            parent = GetParent(state->mStatus);
        }
        if (parent && GetClientRect(parent, &rect))
        {
            PostMessageW(parent, WM_SIZE, 0, MAKELPARAM(rect.right - rect.left, rect.bottom - rect.top));
        }

        switch (panel)
        {
            case 1:
                break;
            case 2:
                break;
            case 3:
                refresh_people_list(state);
                break;
            case 4:
                break;
            default:
                refresh_conversation_messages(state);
                break;
        }
    }

    void handle_pipe_line(HWND hwnd, const std::string& line)
    {
        if (line.rfind("FRIEND|", 0) != 0 && line.rfind("PEOPLE|", 0) != 0)
        {
            trace_line("handle_pipe_line " + line.substr(0, 180));
        }
        auto state = reinterpret_cast<WindowState*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        if (!state)
        {
            return;
        }

        const std::vector<std::string> parts = split_pipe_line(line);
        if (parts.empty())
        {
            return;
        }

        if (parts[0] == "READY" && parts.size() > 1)
        {
            SetWindowTextW(state->mStatus, utf8_to_wide(parts[1]).c_str());
        }
        else if (parts[0] == "PEOPLE_CLEAR" && parts.size() >= 2)
        {
            if (!is_people_target(state))
            {
                return;
            }
            const int tab = people_tab_index(parts[1]);
            state->mPeopleRows[tab].clear();
            state->mPeopleIds[tab].clear();
            state->mRenderedPeopleIds[tab].clear();
            if (tab == 0)
            {
                state->mPendingNearbyPeople.clear();
            }
        }
        else if (parts[0] == "PEOPLE" && parts.size() >= 7)
        {
            if (!is_people_target(state))
            {
                return;
            }
            const int tab = people_tab_index(parts[1]);
            std::wstring row = utf8_to_wide(parts[3]);
            row += L"\t";
            row += utf8_to_wide(parts[4]);
            row += L"\t";
            row += utf8_to_wide(parts[5]);
            row += L"\t";
            row += utf8_to_wide(parts[6]);
            state->mPeopleRows[tab].push_back(row);
            state->mPeopleIds[tab].push_back(utf8_to_wide(parts[2]));
            if (tab == 0)
            {
                state->mPendingNearbyPeople[utf8_to_wide(parts[2])] = utf8_to_wide(parts[3]);
            }
        }
        else if (parts[0] == "PEOPLE_DONE" && parts.size() >= 3)
        {
            if (!is_people_target(state))
            {
                return;
            }
            const int tab = people_tab_index(parts[1]);
            if (state->mPeopleActiveTab == tab && should_repaint_people_list(state))
            {
                refresh_people_list(state);
            }
            else if (state->mPeopleActiveTab == tab && is_people_visible(state))
            {
                state->mPeopleRefreshDeferred = true;
            }
        }
        else if (parts[0] == "CHAT" && parts.size() >= 4)
        {
            if (!is_nearby_target(state))
            {
                return;
            }
            std::wstring display;
            if (!parts[1].empty())
            {
                display += utf8_to_wide(parts[1]);
                display += L": ";
            }
            display += utf8_to_wide(parts[3]);
            const int chat_type = parts.size() >= 5 ? std::atoi(parts[4].c_str()) : 0;
            if (chat_type == CHAT_TYPE_RADAR)
            {
                append_notice_line(is_communications_target(state) ? state->mNearbyMessages : state->mMessages, display);
            }
            else
            {
                append_text_line(is_communications_target(state) ? state->mNearbyMessages : state->mMessages, display);
            }
            if (!is_communications_target(state) || state->mActiveCommPanel == 1)
            {
                SetWindowTextW(state->mStatus, L"Nearby Chat");
            }
        }
        else if (parts[0] == "SESSION" && parts.size() >= 6)
        {
            if (!is_conversations_target(state))
            {
                return;
            }
            const std::wstring incoming_session_id = utf8_to_wide(parts[1]);
            const std::wstring incoming_participant_id = utf8_to_wide(parts[3]);
            const bool reopening_pending_session = !state->mPendingOpenParticipantId.empty() &&
                state->mPendingOpenParticipantId == incoming_participant_id;
            if (state->mLocallyClosedSessionIds.find(incoming_session_id) != state->mLocallyClosedSessionIds.end())
            {
                if (!reopening_pending_session)
                {
                    return;
                }
                state->mLocallyClosedSessionIds.erase(incoming_session_id);
            }
            const LRESULT selected = SendMessageW(state->mSessions, LB_GETCURSEL, 0, 0);
            std::wstring selected_session_id;
            if (selected != LB_ERR)
            {
                auto selected_session = reinterpret_cast<SessionInfo*>(SendMessageW(state->mSessions, LB_GETITEMDATA, selected, 0));
                if (selected_session)
                {
                    selected_session_id = selected_session->mSessionId;
                }
            }
            const LRESULT count = SendMessageW(state->mSessions, LB_GETCOUNT, 0, 0);
            for (LRESULT index = 0; index < count; ++index)
            {
                auto existing = reinterpret_cast<SessionInfo*>(SendMessageW(state->mSessions, LB_GETITEMDATA, index, 0));
                if (existing && existing->mSessionId == incoming_session_id)
                {
                    const std::wstring incoming_name = utf8_to_wide(parts[2]);
                    const std::wstring incoming_other_participant_id = utf8_to_wide(parts[3]);
                    const std::wstring incoming_dialog = utf8_to_wide(parts[4]);
                    const bool session_changed = existing->mName != incoming_name ||
                        existing->mOtherParticipantId != incoming_other_participant_id ||
                        existing->mDialog != incoming_dialog;

                    existing->mName = incoming_name;
                    existing->mOtherParticipantId = incoming_other_participant_id;
                    existing->mDialog = incoming_dialog;

                    LRESULT new_index = index;
                    if (session_changed)
                    {
                        SendMessageW(state->mSessions, LB_DELETESTRING, index, 0);
                        new_index = SendMessageW(
                            state->mSessions,
                            LB_INSERTSTRING,
                            index,
                            reinterpret_cast<LPARAM>(existing->mName.c_str()));
                        SendMessageW(state->mSessions, LB_SETITEMDATA, new_index, reinterpret_cast<LPARAM>(existing));
                    }
                    if (reopening_pending_session)
                    {
                        SendMessageW(state->mSessions, LB_SETCURSEL, new_index, 0);
                        state->mPendingOpenParticipantId.clear();
                        refresh_conversation_messages(state);
                    }
                    else if (selected_session_id == incoming_session_id && session_changed)
                    {
                        SendMessageW(state->mSessions, LB_SETCURSEL, new_index, 0);
                    }
                    return;
                }
            }

            auto session = new SessionInfo();
            session->mSessionId = incoming_session_id;
            session->mName = utf8_to_wide(parts[2]);
            session->mOtherParticipantId = utf8_to_wide(parts[3]);
            session->mDialog = utf8_to_wide(parts[4]);

            const LRESULT index = SendMessageW(
                state->mSessions,
                LB_ADDSTRING,
                0,
                reinterpret_cast<LPARAM>(session->mName.c_str()));
            SendMessageW(state->mSessions, LB_SETITEMDATA, index, reinterpret_cast<LPARAM>(session));

            if (SendMessageW(state->mSessions, LB_GETCURSEL, 0, 0) == LB_ERR)
            {
                SendMessageW(state->mSessions, LB_SETCURSEL, index, 0);
                refresh_conversation_messages(state);
            }
            else if (!state->mPendingOpenParticipantId.empty() && state->mPendingOpenParticipantId == session->mOtherParticipantId)
            {
                SendMessageW(state->mSessions, LB_SETCURSEL, index, 0);
                state->mPendingOpenParticipantId.clear();
                refresh_conversation_messages(state);
            }
        }
        else if (parts[0] == "HISTORY_BEGIN" && parts.size() >= 2)
        {
            if (!is_conversations_target(state))
            {
                return;
            }

            const std::wstring session_id = utf8_to_wide(parts[1]);
            auto& messages = state->mSessionMessages[session_id];
            auto& keys = state->mSessionMessageKeys[session_id];
            for (const DisplayMessage& message : messages)
            {
                if (message.mHistory && !message.mKey.empty())
                {
                    keys.erase(message.mKey);
                }
            }
            messages.erase(
                std::remove_if(
                    messages.begin(),
                    messages.end(),
                    [](const DisplayMessage& message) { return message.mHistory; }),
                messages.end());
        }
        else if (parts[0] == "HISTORY_MSG" && parts.size() >= 5)
        {
            if (!is_conversations_target(state))
            {
                return;
            }

            const std::wstring session_id = utf8_to_wide(parts[1]);
            std::wstring display = utf8_to_wide(parts[2]);
            display += L": ";
            display += utf8_to_wide(parts[4]);

            const std::wstring key = make_message_key(parts);
            if (!remember_message_key(state, session_id, key))
            {
                return;
            }

            auto& messages = state->mSessionMessages[session_id];
            const auto first_live = std::find_if(
                messages.begin(),
                messages.end(),
                [](const DisplayMessage& message) { return !message.mHistory; });
            messages.insert(first_live, { display, key, true });
        }
        else if (parts[0] == "HISTORY_DONE" && parts.size() >= 2)
        {
            if (!is_conversations_target(state))
            {
                return;
            }

            const std::wstring session_id = utf8_to_wide(parts[1]);
            SessionInfo* selected_session = get_selected_session(state);
            if (selected_session && selected_session->mSessionId == session_id)
            {
                refresh_conversation_messages(state);
            }
        }
        else if (parts[0] == "MSG" && parts.size() >= 5)
        {
            if (!is_conversations_target(state))
            {
                return;
            }
            const std::wstring session_id = utf8_to_wide(parts[1]);
            if (state->mLocallyClosedSessionIds.find(session_id) != state->mLocallyClosedSessionIds.end())
            {
                state->mLocallyClosedSessionIds.erase(session_id);
            }
            std::wstring display = utf8_to_wide(parts[2]);
            display += L": ";
            display += utf8_to_wide(parts[4]);
            const std::wstring key = make_message_key(parts);
            if (!remember_message_key(state, session_id, key))
            {
                trace_line("ignored duplicate MSG for session " + parts[1]);
                return;
            }

            state->mSessionMessages[session_id].push_back({ display, key, false });

            SessionInfo* selected_session = get_selected_session(state);
            if (selected_session && selected_session->mSessionId == session_id)
            {
                append_text_line(state->mMessages, display);
            }
            else
            {
                set_session_unread(state, session_id, true);
            }
            set_session_typing(state, session_id, false);
        }
        else if (parts[0] == "TYPING" && parts.size() >= 4)
        {
            if (!is_conversations_target(state))
            {
                return;
            }

            set_session_typing(state, utf8_to_wide(parts[1]), parts[3] == "1");
        }
        else if (parts[0] == "DEBUG" && parts.size() >= 2)
        {
            if (state->mTarget == L"people" || is_communications_target(state))
            {
                return;
            }
            std::wstring display = L"[debug] ";
            display += utf8_to_wide(parts[1]);
            append_text_line(state->mMessages, display);
            SetWindowTextW(state->mStatus, display.c_str());
        }
        else if (parts[0] == "SNAPSHOT_DONE" && parts.size() >= 3)
        {
            if (!is_conversations_target(state))
            {
                return;
            }
            std::wstring status = L"Ready. Sessions: ";
            status += utf8_to_wide(parts[1]);
            status += L" Messages: ";
            status += utf8_to_wide(parts[2]);
            SetWindowTextW(state->mStatus, status.c_str());
        }
        else if (parts[0] == "SESSION_REMOVED" && parts.size() >= 2)
        {
            if (!is_conversations_target(state))
            {
                return;
            }
            const std::wstring removed_session_id = utf8_to_wide(parts[1]);
            state->mLocallyClosedSessionIds.erase(removed_session_id);
            const LRESULT count = SendMessageW(state->mSessions, LB_GETCOUNT, 0, 0);
            for (LRESULT index = 0; index < count; ++index)
            {
                auto existing = reinterpret_cast<SessionInfo*>(SendMessageW(state->mSessions, LB_GETITEMDATA, index, 0));
                if (existing && existing->mSessionId == removed_session_id)
                {
                    delete existing;
                    SendMessageW(state->mSessions, LB_DELETESTRING, index, 0);
                    break;
                }
            }
        }
        else if (parts[0] == "FRIENDS_CLEAR")
        {
            if (!is_communications_target(state))
            {
                return;
            }
            state->mFriendRows.clear();
            state->mFriendIds.clear();
        }
        else if (parts[0] == "FRIEND" && parts.size() >= 7)
        {
            if (!is_communications_target(state))
            {
                return;
            }
            std::wstring row = utf8_to_wide(parts[2]);
            row += L"\t";
            row += utf8_to_wide(parts[3]);
            row += L"\t";
            row += utf8_to_wide(parts[4]);
            row += L"\t";
            row += utf8_to_wide(parts[5]);
            row += L"\t";
            row += utf8_to_wide(parts[6]);
            const std::wstring friend_id = utf8_to_wide(parts[1]);
            auto existing_id = std::find(state->mFriendIds.begin(), state->mFriendIds.end(), friend_id);
            if (existing_id != state->mFriendIds.end())
            {
                const size_t existing_index = static_cast<size_t>(std::distance(state->mFriendIds.begin(), existing_id));
                if (existing_index < state->mFriendRows.size())
                {
                    state->mFriendRows[existing_index] = row;
                }
            }
            else
            {
                state->mFriendRows.push_back(row);
                state->mFriendIds.push_back(friend_id);
            }
        }
        else if (parts[0] == "FRIENDS_DONE" && parts.size() >= 3)
        {
            if (!is_communications_target(state))
            {
                return;
            }

            if (should_repaint_friends_list(state))
            {
                refresh_friends_list(state);
            }
            else if (is_friends_visible(state))
            {
                state->mFriendsRefreshDeferred = true;
            }

            if (state->mActiveCommPanel == 2)
            {
                std::wstring status = L"Friends - Online: ";
                status += utf8_to_wide(parts[2]);
                status += L" / ";
                status += utf8_to_wide(parts[1]);
                SetWindowTextW(state->mStatus, status.c_str());
            }
        }
    }

    void read_pipe_lines(HWND hwnd, HANDLE pipe)
    {
        trace_line("read_pipe_lines start");
        std::string pending;
        char buffer[1024] = {};
        DWORD bytes_read = 0;

        while (ReadFile(pipe, buffer, sizeof(buffer), &bytes_read, nullptr) && bytes_read > 0)
        {
            pending.append(buffer, bytes_read);

            size_t newline = std::string::npos;
            while ((newline = pending.find('\n')) != std::string::npos)
            {
                std::string line = pending.substr(0, newline);
                pending.erase(0, newline + 1);
                if (!line.empty() && line.back() == '\r')
                {
                    line.pop_back();
                }

                PostMessageW(hwnd, WM_PIPE_LINE, 0, reinterpret_cast<LPARAM>(new std::string(line)));
            }
        }

        trace_line("read_pipe_lines ended error=" + std::to_string(GetLastError()));
        PostMessageW(hwnd, WM_PIPE_LINE, 0, reinterpret_cast<LPARAM>(new std::string("READY|Disconnected from Firestorm")));
        PostMessageW(hwnd, WM_PIPE_DISCONNECTED, reinterpret_cast<WPARAM>(pipe), 0);
    }

    void start_pipe_reader(HWND hwnd, HANDLE pipe)
    {
        CreateThread(nullptr, 0, [](LPVOID param) -> DWORD
        {
            auto pair = reinterpret_cast<std::pair<HWND, HANDLE>*>(param);
            read_pipe_lines(pair->first, pair->second);
            delete pair;
            return 0;
        }, new std::pair<HWND, HANDLE>(hwnd, pipe), 0, nullptr);
    }

    void start_pipe_reconnect(HWND hwnd, const std::wstring& pipe_name)
    {
        CreateThread(nullptr, 0, [](LPVOID param) -> DWORD
        {
            auto pair = reinterpret_cast<std::pair<HWND, std::wstring>*>(param);
            while (IsWindow(pair->first))
            {
                HANDLE pipe = open_pipe_with_retry(pair->second);
                if (pipe != INVALID_HANDLE_VALUE)
                {
                    PostMessageW(pair->first, WM_PIPE_RECONNECTED, reinterpret_cast<WPARAM>(pipe), 0);
                    break;
                }
                Sleep(2000);
            }
            delete pair;
            return 0;
        }, new std::pair<HWND, std::wstring>(hwnd, pipe_name), 0, nullptr);
    }

    void begin_pipe_reconnect(HWND hwnd, WindowState* state, const wchar_t* status)
    {
        if (!hwnd || !state || state->mClosing || state->mPipeName.empty() || state->mReconnectInProgress)
        {
            return;
        }

        state->mReconnectInProgress = true;
        if (status)
        {
            SetWindowTextW(state->mStatus, status);
        }
        start_pipe_reconnect(hwnd, state->mPipeName);
    }

    void layout_controls(WindowState* state, int width, int height)
    {
        if (!state)
        {
            return;
        }

        const int margin = 14;
        const int status_height = 28;
        const int input_height = 30;
        const int send_width = 68;
        const int sessions_width = 190;
        const int content_top = margin + status_height + 6;
        const int content_height = height - content_top - input_height - (margin * 3);

        if (state->mTarget == L"communications")
        {
            const int tab_height = 28;
            const int tab_width = 82;
            const int panel_top = margin + tab_height + 12;
            const int panel_height = height - panel_top - input_height - (margin * 3);

            for (int tab = 0; tab < 5; ++tab)
            {
                MoveWindow(state->mCommTabs[tab], margin + (tab * (tab_width + 4)), margin, tab_width, tab_height, TRUE);
            }
            show_control(state->mStatus, false);

            const bool show_ims = state->mActiveCommPanel == 0;
            const bool show_nearby = state->mActiveCommPanel == 1;
            const bool show_friends = state->mActiveCommPanel == 2;
            const bool show_people = state->mActiveCommPanel == 3;
            const bool show_all = state->mActiveCommPanel == 4;

            show_control(state->mSessions, show_ims || show_all);
            show_control(state->mMessages, show_ims || show_all);
            show_control(state->mInput, show_ims || show_all);
            show_control(state->mSend, show_ims || show_all);

            show_control(state->mNearbyMessages, show_nearby || show_all);
            show_control(state->mNearbyInput, show_nearby || show_all);
            show_control(state->mNearbySend, show_nearby || show_all);

            show_control(state->mFriendsSearch, show_friends || show_all);
            show_control(state->mFriendsList, show_friends || show_all);
            for (int tab = 0; tab < 3; ++tab)
            {
                show_control(state->mPeopleTabs[tab], show_people || show_all);
            }
            show_control(state->mPeopleList, show_people || show_all);

            if (show_all)
            {
                const int splitter_size = 10;
                const int section_label_height = 26;
                const int all_left = margin;
                const int all_top = panel_top;
                const int all_width = width - margin * 2;
                const int all_height = height - panel_top - margin;
                const int split_x = all_left + clamp_int((all_width * state->mAllSplitX) / 100, 220, all_width - 220);
                const int split_y = all_top + clamp_int((all_height * state->mAllSplitY) / 100, 170, all_height - 170);
                const int left_width = split_x - all_left - splitter_size / 2;
                const int right_left = split_x + splitter_size / 2;
                const int right_width = all_left + all_width - right_left;
                const int top_content_top = all_top + section_label_height;
                const int top_height = split_y - top_content_top - splitter_size / 2 - input_height - 10;
                const int top_input_y = top_content_top + top_height + 10;
                const int bottom_top = split_y + splitter_size / 2;
                const int bottom_content_top = bottom_top + section_label_height;
                const int bottom_height = all_top + all_height - bottom_content_top;
                const int mini_sessions_width = 170;

                state->mAllPanelRect = { all_left, all_top, all_left + all_width, all_top + all_height };
                state->mAllVerticalSplitterRect = { split_x - splitter_size / 2, all_top, split_x + splitter_size / 2, all_top + all_height };
                state->mAllHorizontalSplitterRect = { all_left, split_y - splitter_size / 2, all_left + all_width, split_y + splitter_size / 2 };

                MoveWindow(state->mSessions, all_left, top_content_top, mini_sessions_width, top_height, TRUE);
                MoveWindow(state->mMessages, all_left + mini_sessions_width + 10, top_content_top, left_width - mini_sessions_width - 10, top_height, TRUE);
                MoveWindow(state->mInput, all_left, top_input_y, left_width - send_width - 8, input_height, TRUE);
                MoveWindow(state->mSend, all_left + left_width - send_width, top_input_y, send_width, input_height, TRUE);

                MoveWindow(state->mNearbyMessages, right_left, top_content_top, right_width, top_height, TRUE);
                MoveWindow(state->mNearbyInput, right_left, top_input_y, right_width - send_width - 8, input_height, TRUE);
                MoveWindow(state->mNearbySend, right_left + right_width - send_width, top_input_y, send_width, input_height, TRUE);

                const int people_tab_height = 24;
                const int people_tab_width = 72;
                MoveWindow(state->mFriendsSearch, all_left, bottom_content_top, left_width, people_tab_height, TRUE);
                MoveWindow(state->mFriendsList, all_left, bottom_content_top + people_tab_height + 8, left_width, bottom_height - people_tab_height - 8, TRUE);

                const int people_left = right_left;
                for (int tab = 0; tab < 3; ++tab)
                {
                    MoveWindow(state->mPeopleTabs[tab], people_left + (tab * (people_tab_width + 6)), bottom_content_top, people_tab_width, people_tab_height, TRUE);
                }
                MoveWindow(state->mPeopleList, people_left, bottom_content_top + people_tab_height + 8, right_width, bottom_height - people_tab_height - 8, TRUE);
                InvalidateRect(GetParent(state->mStatus), nullptr, TRUE);
                return;
            }

            state->mAllPanelRect = {};
            state->mAllVerticalSplitterRect = {};
            state->mAllHorizontalSplitterRect = {};

            MoveWindow(state->mSessions, margin, panel_top, sessions_width, panel_height, TRUE);
            MoveWindow(state->mMessages, margin + sessions_width + 10, panel_top, width - sessions_width - margin * 2 - 10, panel_height, TRUE);
            MoveWindow(state->mInput, margin, height - margin - input_height, width - send_width - margin * 3, input_height, TRUE);
            MoveWindow(state->mSend, width - send_width - margin, height - margin - input_height, send_width, input_height, TRUE);

            MoveWindow(state->mNearbyMessages, margin, panel_top, width - margin * 2, panel_height, TRUE);
            MoveWindow(state->mNearbyInput, margin, height - margin - input_height, width - send_width - margin * 3, input_height, TRUE);
            MoveWindow(state->mNearbySend, width - send_width - margin, height - margin - input_height, send_width, input_height, TRUE);

            const int people_tab_height = 24;
            const int people_tab_width = 76;
            MoveWindow(state->mFriendsSearch, margin, panel_top, width - margin * 2, people_tab_height, TRUE);
            MoveWindow(state->mFriendsList, margin, panel_top + people_tab_height + 6, width - margin * 2, height - panel_top - people_tab_height - margin - 6, TRUE);

            for (int tab = 0; tab < 3; ++tab)
            {
                MoveWindow(state->mPeopleTabs[tab], margin + (tab * (people_tab_width + 4)), panel_top, people_tab_width, people_tab_height, TRUE);
            }
            MoveWindow(state->mPeopleList, margin, panel_top + people_tab_height + 6, width - margin * 2, height - panel_top - people_tab_height - margin - 6, TRUE);
            return;
        }

        if (state->mTarget == L"people")
        {
            const int tab_height = 24;
            const int tab_width = 76;
            MoveWindow(state->mStatus, margin, margin, width - margin * 2, status_height, TRUE);
            for (int tab = 0; tab < 3; ++tab)
            {
                MoveWindow(state->mPeopleTabs[tab], margin + (tab * (tab_width + 4)), content_top, tab_width, tab_height, TRUE);
            }
            MoveWindow(state->mPeopleList, margin, content_top + tab_height + 6, width - margin * 2, height - content_top - tab_height - margin * 2 - 6, TRUE);
            return;
        }

        if (state->mTarget == L"nearby_chat")
        {
            MoveWindow(state->mStatus, margin, margin, width - margin * 2, status_height, TRUE);
            MoveWindow(state->mMessages, margin, content_top, width - margin * 2, content_height, TRUE);
            MoveWindow(state->mInput, margin, height - margin - input_height, width - send_width - margin * 3, input_height, TRUE);
            MoveWindow(state->mSend, width - send_width - margin, height - margin - input_height, send_width, input_height, TRUE);
            return;
        }

        MoveWindow(state->mStatus, margin, margin, width - margin * 2, status_height, TRUE);
        MoveWindow(state->mSessions, margin, content_top, sessions_width, content_height, TRUE);
        MoveWindow(state->mMessages, margin + sessions_width + 8, content_top, width - sessions_width - margin * 2 - 8, content_height, TRUE);
        MoveWindow(state->mInput, margin, height - margin - input_height, width - send_width - margin * 3, input_height, TRUE);
        MoveWindow(state->mSend, width - send_width - margin, height - margin - input_height, send_width, input_height, TRUE);
    }

    void send_nearby_message(WindowState* state)
    {
        if (!state || state->mPipe == INVALID_HANDLE_VALUE)
        {
            return;
        }

        HWND input = is_communications_target(state) ? state->mNearbyInput : state->mInput;
        wchar_t text[2048] = {};
        GetWindowTextW(input, text, 2048);
        std::wstring message(text);
        if (message.empty())
        {
            return;
        }

        std::string line = "SEND_NEARBY|1|";
        line += wide_to_utf8(message);
        async_write_pipe_line(state->mPipe, line);
        SetWindowTextW(input, L"");
    }

    void send_current_message(WindowState* state)
    {
        if (!state || state->mPipe == INVALID_HANDLE_VALUE)
        {
            return;
        }

        if (state->mTarget == L"nearby_chat" || (is_communications_target(state) && state->mActiveCommPanel == 1))
        {
            send_nearby_message(state);
            return;
        }

        wchar_t text[2048] = {};
        GetWindowTextW(state->mInput, text, 2048);
        std::wstring message(text);
        if (message.empty())
        {
            return;
        }

        const LRESULT selected = SendMessageW(state->mSessions, LB_GETCURSEL, 0, 0);
        if (selected == LB_ERR)
        {
            return;
        }

        auto session = reinterpret_cast<SessionInfo*>(SendMessageW(state->mSessions, LB_GETITEMDATA, selected, 0));
        if (!session)
        {
            return;
        }

        std::string line = "SEND|";
        line += wide_to_utf8(session->mSessionId);
        line += "|";
        line += wide_to_utf8(session->mOtherParticipantId);
        line += "|";
        line += wide_to_utf8(session->mDialog);
        line += "|";
        line += wide_to_utf8(message);
        async_write_pipe_line(state->mPipe, line);

        SetWindowTextW(state->mInput, L"");
    }

    void open_selected_friend_im(WindowState* state)
    {
        if (!state || !state->mFriendsList || state->mPipe == INVALID_HANDLE_VALUE)
        {
            return;
        }

        const LRESULT selected = SendMessageW(state->mFriendsList, LB_GETCURSEL, 0, 0);
        if (selected == LB_ERR || selected <= 0)
        {
            return;
        }

        const size_t friend_index = static_cast<size_t>(selected - 1);
        if (friend_index >= state->mRenderedFriendIds.size())
        {
            return;
        }

        std::string line = "OPEN_IM|";
        line += wide_to_utf8(state->mRenderedFriendIds[friend_index]);
        state->mPendingOpenParticipantId = state->mRenderedFriendIds[friend_index];
        async_write_pipe_line(state->mPipe, line);
        if (is_communications_target(state) && state->mActiveCommPanel != 4)
        {
            set_communications_panel(state, 4);
        }
    }

    void zoom_selected_person(WindowState* state)
    {
        if (!state || !state->mPeopleList || state->mPipe == INVALID_HANDLE_VALUE)
        {
            return;
        }

        const LRESULT selected = SendMessageW(state->mPeopleList, LB_GETCURSEL, 0, 0);
        if (selected == LB_ERR || selected <= 0)
        {
            return;
        }

        const size_t person_index = static_cast<size_t>(selected - 1);
        const auto& ids = state->mRenderedPeopleIds[state->mPeopleActiveTab];
        if (person_index >= ids.size() || ids[person_index].empty())
        {
            return;
        }

        std::string line = "ZOOM_AVATAR|";
        line += wide_to_utf8(ids[person_index]);
        async_write_pipe_line(state->mPipe, line);
    }

    void close_selected_session(WindowState* state)
    {
        if (!state || !state->mSessions)
        {
            return;
        }

        const LRESULT selected = SendMessageW(state->mSessions, LB_GETCURSEL, 0, 0);
        if (selected == LB_ERR)
        {
            return;
        }

        auto session = reinterpret_cast<SessionInfo*>(SendMessageW(state->mSessions, LB_GETITEMDATA, selected, 0));
        if (!session)
        {
            return;
        }

        const std::wstring session_id = session->mSessionId;
        state->mLocallyClosedSessionIds.insert(session_id);
        state->mSessionMessages.erase(session_id);
        state->mSessionMessageKeys.erase(session_id);
        delete session;
        SendMessageW(state->mSessions, LB_DELETESTRING, selected, 0);

        const LRESULT count = SendMessageW(state->mSessions, LB_GETCOUNT, 0, 0);
        if (count > 0)
        {
            const LRESULT next_index = selected < count ? selected : count - 1;
            SendMessageW(state->mSessions, LB_SETCURSEL, next_index, 0);
        }
        SetWindowTextW(state->mMessages, L"");
        refresh_conversation_messages(state);
    }

    void show_session_context_menu(HWND hwnd, WindowState* state, int x, int y)
    {
        if (!state || !state->mSessions || !is_conversations_target(state))
        {
            return;
        }

        POINT screen_point = { x, y };
        POINT client_point = screen_point;
        ScreenToClient(state->mSessions, &client_point);
        const LRESULT hit = SendMessageW(state->mSessions, LB_ITEMFROMPOINT, 0, MAKELPARAM(client_point.x, client_point.y));
        if (HIWORD(hit))
        {
            return;
        }

        const int index = LOWORD(hit);
        SendMessageW(state->mSessions, LB_SETCURSEL, index, 0);
        refresh_conversation_messages(state);

        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, IDM_SESSION_CLOSE, L"Close IM");
        TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_LEFTALIGN, screen_point.x, screen_point.y, 0, hwnd, nullptr);
        DestroyMenu(menu);
    }

    HMENU control_id(int id)
    {
        return reinterpret_cast<HMENU>(static_cast<INT_PTR>(id));
    }

    void send_from_focused_input(HWND hwnd)
    {
        auto state = reinterpret_cast<WindowState*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        if (!state)
        {
            return;
        }

        HWND focus = GetFocus();
        if (focus == state->mNearbyInput)
        {
            send_nearby_message(state);
        }
        else if (focus == state->mInput)
        {
            send_current_message(state);
        }
    }

    HMENU create_theme_menu()
    {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, IDM_THEME_DRACULA, L"Dracula");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, IDM_THEME_SAVE, L"Save Theme...");
        AppendMenuW(menu, MF_STRING, IDM_THEME_LOAD, L"Load Theme...");
        return menu;
    }

    void show_theme_context_menu(HWND hwnd, int x, int y)
    {
        HMENU menu = create_theme_menu();
        TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_LEFTALIGN, x, y, 0, hwnd, nullptr);
        DestroyMenu(menu);
    }

    bool pick_theme_file(HWND hwnd, wchar_t* path, DWORD path_size, bool save)
    {
        if (!path || path_size == 0)
        {
            return false;
        }

        wcscpy_s(path, path_size, L"firestorm-companion.theme");
        OPENFILENAMEW ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwnd;
        ofn.lpstrFilter = L"Companion Theme (*.theme)\0*.theme\0All Files (*.*)\0*.*\0";
        ofn.lpstrFile = path;
        ofn.nMaxFile = path_size;
        ofn.lpstrDefExt = L"theme";
        ofn.Flags = OFN_HIDEREADONLY | OFN_PATHMUSTEXIST | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
        return save ? GetSaveFileNameW(&ofn) != FALSE : GetOpenFileNameW(&ofn) != FALSE;
    }

    void apply_theme(HWND hwnd, WindowState* state, const AppTheme& theme)
    {
        if (!state)
        {
            return;
        }

        state->mTheme = theme;
        reset_theme_brushes(state);
        invalidate_theme(hwnd, state);
    }

    void handle_theme_command(HWND hwnd, WindowState* state, UINT command)
    {
        if (!state)
        {
            return;
        }

        if (command == IDM_THEME_DRACULA)
        {
            apply_theme(hwnd, state, AppTheme());
            return;
        }

        wchar_t path[MAX_PATH] = {};
        if (command == IDM_THEME_SAVE && pick_theme_file(hwnd, path, MAX_PATH, true))
        {
            save_theme_file(state->mTheme, path);
            SetWindowTextW(state->mStatus, L"Theme saved");
            return;
        }

        if (command == IDM_THEME_LOAD && pick_theme_file(hwnd, path, MAX_PATH, false))
        {
            AppTheme theme = state->mTheme;
            if (load_theme_file(&theme, path))
            {
                apply_theme(hwnd, state, theme);
                SetWindowTextW(state->mStatus, L"Theme loaded");
            }
        }
    }

    LRESULT CALLBACK companion_window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
    {
        switch (message)
        {
            case WM_NCCREATE:
            {
                auto create_struct = reinterpret_cast<CREATESTRUCT*>(lparam);
                SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create_struct->lpCreateParams));
                return TRUE;
            }
            case WM_SIZE:
            {
                auto state = reinterpret_cast<WindowState*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
                layout_controls(state, LOWORD(lparam), HIWORD(lparam));
                return 0;
            }
            case WM_ERASEBKGND:
            {
                auto state = reinterpret_cast<WindowState*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
                RECT rect = {};
                GetClientRect(hwnd, &rect);
                FillRect(reinterpret_cast<HDC>(wparam), &rect, state && state->mBackgroundBrush ? state->mBackgroundBrush : reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
                return 1;
            }
            case WM_PAINT:
            {
                PAINTSTRUCT ps = {};
                HDC dc = BeginPaint(hwnd, &ps);
                auto state = reinterpret_cast<WindowState*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
                if (state && is_communications_target(state) && state->mActiveCommPanel == 4)
                {
                    RECT splitter_line = state->mAllVerticalSplitterRect;
                    const int splitter_center_x = (splitter_line.left + splitter_line.right) / 2;
                    splitter_line.left = splitter_center_x - 1;
                    splitter_line.right = splitter_center_x + 1;

                    RECT horizontal_line = state->mAllHorizontalSplitterRect;
                    const int splitter_center_y = (horizontal_line.top + horizontal_line.bottom) / 2;
                    horizontal_line.top = splitter_center_y - 1;
                    horizontal_line.bottom = splitter_center_y + 1;

                    HBRUSH splitter_brush = CreateSolidBrush(state->mTheme.mSurface);
                    FillRect(dc, &splitter_line, splitter_brush);
                    FillRect(dc, &horizontal_line, splitter_brush);
                    DeleteObject(splitter_brush);

                    HFONT previous_font = reinterpret_cast<HFONT>(SelectObject(dc, state->mHeaderFont ? state->mHeaderFont : GetStockObject(DEFAULT_GUI_FONT)));
                    RECT im_label = { state->mAllPanelRect.left, state->mAllPanelRect.top, state->mAllVerticalSplitterRect.left - 8, state->mAllPanelRect.top + 22 };
                    RECT nearby_label = { state->mAllVerticalSplitterRect.right + 8, state->mAllPanelRect.top, state->mAllPanelRect.right, state->mAllPanelRect.top + 22 };
                    RECT friends_label = { state->mAllPanelRect.left, state->mAllHorizontalSplitterRect.bottom + 4, state->mAllVerticalSplitterRect.left - 8, state->mAllHorizontalSplitterRect.bottom + 26 };
                    RECT people_label = { state->mAllVerticalSplitterRect.right + 8, state->mAllHorizontalSplitterRect.bottom + 4, state->mAllPanelRect.right, state->mAllHorizontalSplitterRect.bottom + 26 };
                    draw_section_label(dc, L"Instant Messages", im_label, state->mTheme.mMutedText);
                    draw_section_label(dc, L"Nearby Chat", nearby_label, state->mTheme.mMutedText);
                    draw_section_label(dc, L"Friends", friends_label, state->mTheme.mMutedText);
                    draw_section_label(dc, L"People", people_label, state->mTheme.mMutedText);
                    SelectObject(dc, previous_font);
                }
                EndPaint(hwnd, &ps);
                return 0;
            }
            case WM_CTLCOLORSTATIC:
            {
                auto state = reinterpret_cast<WindowState*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
                HDC dc = reinterpret_cast<HDC>(wparam);
                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, state ? state->mTheme.mText : RGB(0, 0, 0));
                return reinterpret_cast<LRESULT>(state && state->mBackgroundBrush ? state->mBackgroundBrush : GetStockObject(NULL_BRUSH));
            }
            case WM_CTLCOLOREDIT:
            {
                auto state = reinterpret_cast<WindowState*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
                HDC dc = reinterpret_cast<HDC>(wparam);
                SetBkColor(dc, state ? state->mTheme.mField : RGB(255, 255, 255));
                SetTextColor(dc, state ? state->mTheme.mText : RGB(0, 0, 0));
                return reinterpret_cast<LRESULT>(state && state->mFieldBrush ? state->mFieldBrush : reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
            }
            case WM_CTLCOLORLISTBOX:
            {
                auto state = reinterpret_cast<WindowState*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
                HDC dc = reinterpret_cast<HDC>(wparam);
                SetBkColor(dc, state ? state->mTheme.mField : RGB(255, 255, 255));
                SetTextColor(dc, state ? state->mTheme.mText : RGB(0, 0, 0));
                return reinterpret_cast<LRESULT>(state && state->mFieldBrush ? state->mFieldBrush : reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
            }
            case WM_MEASUREITEM:
            {
                auto measure = reinterpret_cast<MEASUREITEMSTRUCT*>(lparam);
                if (measure && measure->CtlType == ODT_LISTBOX)
                {
                    measure->itemHeight = 20;
                    return TRUE;
                }
                break;
            }
            case WM_DRAWITEM:
            {
                auto state = reinterpret_cast<WindowState*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
                auto draw = reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
                if (!draw)
                {
                    break;
                }

                const AppTheme theme = state ? state->mTheme : AppTheme();
                if (draw->CtlType == ODT_LISTBOX)
                {
                    if (draw->itemID == static_cast<UINT>(-1))
                    {
                        return TRUE;
                    }

                    wchar_t text_buffer[2048] = {};
                    SendMessageW(draw->hwndItem, LB_GETTEXT, draw->itemID, reinterpret_cast<LPARAM>(text_buffer));
                    const bool selected = (draw->itemState & ODS_SELECTED) != 0;
                    const bool header = draw->itemID == 0 &&
                        (draw->CtlID == IDC_PEOPLE_LIST || draw->CtlID == IDC_FRIENDS_LIST);
                    bool unread_session = false;
                    bool typing_session = false;
                    if (draw->CtlID == IDC_SESSIONS)
                    {
                        auto session = reinterpret_cast<SessionInfo*>(SendMessageW(draw->hwndItem, LB_GETITEMDATA, draw->itemID, 0));
                        unread_session = session && session->mHasUnread;
                        typing_session = session && session->mIsTyping;
                    }
                    const COLORREF fill = selected ? theme.mSurface : theme.mField;
                    const COLORREF text = header ? theme.mMutedText : unread_session ? theme.mAccent : typing_session ? NOTICE_TEXT_COLOR : theme.mText;
                    HBRUSH fill_brush = CreateSolidBrush(fill);
                    FillRect(draw->hDC, &draw->rcItem, fill_brush);
                    DeleteObject(fill_brush);

                    if (selected)
                    {
                        RECT accent = draw->rcItem;
                        accent.right = accent.left + 3;
                        HBRUSH accent_brush = CreateSolidBrush(theme.mAccent);
                        FillRect(draw->hDC, &accent, accent_brush);
                        DeleteObject(accent_brush);
                    }

                    SetBkMode(draw->hDC, TRANSPARENT);
                    SetTextColor(draw->hDC, text);
                    HFONT previous_font = reinterpret_cast<HFONT>(SelectObject(draw->hDC, state && state->mFont ? state->mFont : GetStockObject(DEFAULT_GUI_FONT)));
                    if (!draw_table_row(draw, text_buffer))
                    {
                        RECT text_rect = { draw->rcItem.left + 7, draw->rcItem.top + 1, draw->rcItem.right - 4, draw->rcItem.bottom };
                        DrawTextW(draw->hDC, text_buffer, -1, &text_rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                    }
                    SelectObject(draw->hDC, previous_font);
                    return TRUE;
                }

                if (draw->CtlType != ODT_BUTTON)
                {
                    break;
                }

                const UINT id = static_cast<UINT>(draw->CtlID);
                const bool active = is_active_tab(state, id);
                const bool primary = is_primary_button_id(id);
                const bool pressed = (draw->itemState & ODS_SELECTED) != 0;
                const COLORREF fill = primary ? (pressed ? theme.mAccentDark : theme.mAccent) :
                    active ? theme.mAccent :
                    pressed ? theme.mButtonPressed : theme.mSurface;
                const COLORREF border = primary || active ? theme.mAccentDark : theme.mBorder;
                const COLORREF text = primary || active ? RGB(40, 42, 54) : theme.mText;

                HBRUSH background_brush = CreateSolidBrush(theme.mBackground);
                FillRect(draw->hDC, &draw->rcItem, background_brush);
                DeleteObject(background_brush);

                HBRUSH fill_brush = CreateSolidBrush(fill);
                HPEN border_pen = CreatePen(PS_SOLID, 1, border);
                HGDIOBJ old_brush = SelectObject(draw->hDC, fill_brush);
                HGDIOBJ old_pen = SelectObject(draw->hDC, border_pen);
                RoundRect(draw->hDC, draw->rcItem.left, draw->rcItem.top, draw->rcItem.right, draw->rcItem.bottom, 7, 7);
                SelectObject(draw->hDC, old_pen);
                SelectObject(draw->hDC, old_brush);
                DeleteObject(border_pen);
                DeleteObject(fill_brush);

                wchar_t label[128] = {};
                GetWindowTextW(draw->hwndItem, label, 128);
                SetBkMode(draw->hDC, TRANSPARENT);
                SetTextColor(draw->hDC, text);
                HFONT previous_font = reinterpret_cast<HFONT>(SelectObject(draw->hDC, state && state->mFont ? state->mFont : GetStockObject(DEFAULT_GUI_FONT)));
                RECT text_rect = draw->rcItem;
                DrawTextW(draw->hDC, label, -1, &text_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                SelectObject(draw->hDC, previous_font);
                return TRUE;
            }
            case WM_LBUTTONDOWN:
            {
                auto state = reinterpret_cast<WindowState*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
                const int x = GET_X_LPARAM(lparam);
                const int y = GET_Y_LPARAM(lparam);
                if (state && is_communications_target(state) && state->mActiveCommPanel == 4)
                {
                    state->mDraggingAllVerticalSplitter = point_in_rect(state->mAllVerticalSplitterRect, x, y);
                    state->mDraggingAllHorizontalSplitter = point_in_rect(state->mAllHorizontalSplitterRect, x, y);
                    if (state->mDraggingAllVerticalSplitter || state->mDraggingAllHorizontalSplitter)
                    {
                        SetCapture(hwnd);
                        return 0;
                    }
                }
                break;
            }
            case WM_MOUSEMOVE:
            {
                auto state = reinterpret_cast<WindowState*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
                const int x = GET_X_LPARAM(lparam);
                const int y = GET_Y_LPARAM(lparam);
                if (state && is_communications_target(state) && state->mActiveCommPanel == 4)
                {
                    if (state->mDraggingAllVerticalSplitter || state->mDraggingAllHorizontalSplitter)
                    {
                        const int panel_width = state->mAllPanelRect.right - state->mAllPanelRect.left;
                        const int panel_height = state->mAllPanelRect.bottom - state->mAllPanelRect.top;
                        if (state->mDraggingAllVerticalSplitter && panel_width > 0)
                        {
                            state->mAllSplitX = clamp_int(((x - state->mAllPanelRect.left) * 100) / panel_width, 25, 75);
                        }
                        if (state->mDraggingAllHorizontalSplitter && panel_height > 0)
                        {
                            state->mAllSplitY = clamp_int(((y - state->mAllPanelRect.top) * 100) / panel_height, 25, 75);
                        }

                        RECT rect = {};
                        if (GetClientRect(hwnd, &rect))
                        {
                            layout_controls(state, rect.right - rect.left, rect.bottom - rect.top);
                        }
                        return 0;
                    }

                    const bool over_vertical = point_in_rect(state->mAllVerticalSplitterRect, x, y);
                    const bool over_horizontal = point_in_rect(state->mAllHorizontalSplitterRect, x, y);
                    if (over_vertical || over_horizontal)
                    {
                        SetCursor(LoadCursor(nullptr, over_vertical ? IDC_SIZEWE : IDC_SIZENS));
                        return 0;
                    }
                }
                break;
            }
            case WM_LBUTTONUP:
            {
                auto state = reinterpret_cast<WindowState*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
                if (state && (state->mDraggingAllVerticalSplitter || state->mDraggingAllHorizontalSplitter))
                {
                    state->mDraggingAllVerticalSplitter = false;
                    state->mDraggingAllHorizontalSplitter = false;
                    ReleaseCapture();
                    return 0;
                }
                break;
            }
            case WM_COMMAND:
            {
                auto state = reinterpret_cast<WindowState*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
                if (LOWORD(wparam) == IDM_THEME_DRACULA || LOWORD(wparam) == IDM_THEME_SAVE || LOWORD(wparam) == IDM_THEME_LOAD)
                {
                    handle_theme_command(hwnd, state, LOWORD(wparam));
                    return 0;
                }
                if (LOWORD(wparam) == IDM_SESSION_CLOSE)
                {
                    close_selected_session(state);
                    return 0;
                }
                if ((LOWORD(wparam) == IDC_INPUT || LOWORD(wparam) == IDC_NEARBY_INPUT) && HIWORD(wparam) == EN_UPDATE)
                {
                    HWND input = reinterpret_cast<HWND>(lparam);
                    wchar_t text[2048] = {};
                    GetWindowTextW(input, text, 2048);
                    std::wstring value(text);
                    if (!value.empty() && value.back() == L'\n')
                    {
                        while (!value.empty() && (value.back() == L'\r' || value.back() == L'\n'))
                        {
                            value.pop_back();
                        }
                        SetWindowTextW(input, value.c_str());
                        SendMessageW(input, EM_SETSEL, value.size(), value.size());
                        send_from_focused_input(hwnd);
                    }
                    return 0;
                }
                if (LOWORD(wparam) == IDC_FRIENDS_SEARCH && HIWORD(wparam) == EN_CHANGE)
                {
                    refresh_friends_list(state);
                    return 0;
                }
                if (LOWORD(wparam) == IDC_SEND && HIWORD(wparam) == BN_CLICKED)
                {
                    send_current_message(state);
                    return 0;
                }
                if (LOWORD(wparam) == IDC_NEARBY_SEND && HIWORD(wparam) == BN_CLICKED)
                {
                    send_nearby_message(state);
                    return 0;
                }
                if (LOWORD(wparam) == IDC_COMM_IMS && HIWORD(wparam) == BN_CLICKED)
                {
                    set_communications_panel(state, 0);
                    return 0;
                }
                if (LOWORD(wparam) == IDC_COMM_NEARBY && HIWORD(wparam) == BN_CLICKED)
                {
                    set_communications_panel(state, 1);
                    return 0;
                }
                if (LOWORD(wparam) == IDC_COMM_FRIENDS && HIWORD(wparam) == BN_CLICKED)
                {
                    set_communications_panel(state, 2);
                    return 0;
                }
                if (LOWORD(wparam) == IDC_COMM_PEOPLE && HIWORD(wparam) == BN_CLICKED)
                {
                    set_communications_panel(state, 3);
                    return 0;
                }
                if (LOWORD(wparam) == IDC_COMM_ALL && HIWORD(wparam) == BN_CLICKED)
                {
                    set_communications_panel(state, 4);
                    return 0;
                }
                if (LOWORD(wparam) == IDC_SESSIONS && HIWORD(wparam) == LBN_SELCHANGE)
                {
                    refresh_conversation_messages(state);
                    return 0;
                }
                if (LOWORD(wparam) == IDC_SESSIONS && HIWORD(wparam) == LBN_DBLCLK)
                {
                    if (state && is_communications_target(state))
                    {
                        set_communications_panel(state, 0);
                    }
                    return 0;
                }
                if (LOWORD(wparam) == IDC_PEOPLE_LIST && HIWORD(wparam) == LBN_SELCHANGE)
                {
                    if (state && SendMessageW(state->mPeopleList, LB_GETCURSEL, 0, 0) == 0)
                    {
                        sort_people_from_header_click(state);
                    }
                    return 0;
                }
                if (LOWORD(wparam) == IDC_FRIENDS_LIST && HIWORD(wparam) == LBN_SELCHANGE)
                {
                    if (state && SendMessageW(state->mFriendsList, LB_GETCURSEL, 0, 0) == 0)
                    {
                        sort_friends_from_header_click(state);
                    }
                    return 0;
                }
                if (LOWORD(wparam) == IDC_PEOPLE_LIST && HIWORD(wparam) == LBN_KILLFOCUS)
                {
                    if (state && state->mPeopleRefreshDeferred)
                    {
                        refresh_people_list(state);
                    }
                    return 0;
                }
                if (LOWORD(wparam) == IDC_FRIENDS_LIST && HIWORD(wparam) == LBN_KILLFOCUS)
                {
                    if (state && state->mFriendsRefreshDeferred)
                    {
                        refresh_friends_list(state);
                    }
                    return 0;
                }
                if (LOWORD(wparam) == IDC_PEOPLE_LIST && HIWORD(wparam) == LBN_DBLCLK)
                {
                    zoom_selected_person(state);
                    return 0;
                }
                if (LOWORD(wparam) == IDC_FRIENDS_LIST && HIWORD(wparam) == LBN_DBLCLK)
                {
                    open_selected_friend_im(state);
                    return 0;
                }
                if (LOWORD(wparam) == IDC_PEOPLE_NEARBY && HIWORD(wparam) == BN_CLICKED)
                {
                    set_people_tab(state, 0);
                    return 0;
                }
                if (LOWORD(wparam) == IDC_PEOPLE_RECENT && HIWORD(wparam) == BN_CLICKED)
                {
                    set_people_tab(state, 1);
                    return 0;
                }
                if (LOWORD(wparam) == IDC_PEOPLE_BLOCKED && HIWORD(wparam) == BN_CLICKED)
                {
                    set_people_tab(state, 2);
                    return 0;
                }
                return 0;
            }
            case WM_CONTEXTMENU:
            {
                auto state = reinterpret_cast<WindowState*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
                HWND source = reinterpret_cast<HWND>(wparam);
                if (state && source == state->mSessions)
                {
                    show_session_context_menu(hwnd, state, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
                    return 0;
                }
                if (source == hwnd)
                {
                    int x = GET_X_LPARAM(lparam);
                    int y = GET_Y_LPARAM(lparam);
                    if (x == -1 && y == -1)
                    {
                        RECT rect = {};
                        GetClientRect(hwnd, &rect);
                        POINT point = { rect.left + 20, rect.top + 20 };
                        ClientToScreen(hwnd, &point);
                        x = point.x;
                        y = point.y;
                    }
                    show_theme_context_menu(hwnd, x, y);
                    return 0;
                }
                break;
            }
            case WM_PIPE_LINE:
            {
                auto line = reinterpret_cast<std::string*>(lparam);
                if (line)
                {
                    auto state = reinterpret_cast<WindowState*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
                    if (state)
                    {
                        state->mLastPipeReadTick = GetTickCount();
                    }
                    handle_pipe_line(hwnd, *line);
                    delete line;
                }
                return 0;
            }
            case WM_TIMER:
            {
                auto state = reinterpret_cast<WindowState*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
                if (state && wparam == IDT_PIPE_HEARTBEAT && !state->mClosing && !state->mPipeName.empty())
                {
                    const DWORD now = GetTickCount();
                    const DWORD elapsed = now - state->mLastPipeReadTick;
                    if (state->mPipe != INVALID_HANDLE_VALUE && elapsed > 5000)
                    {
                        trace_line("pipe heartbeat stale elapsed=" + std::to_string(elapsed));
                        SetWindowTextW(state->mStatus, L"Reconnecting to Firestorm conversation bridge...");
                        CancelIoEx(state->mPipe, nullptr);
                    }
                    else if (state->mPipe == INVALID_HANDLE_VALUE)
                    {
                        begin_pipe_reconnect(hwnd, state, L"Reconnecting to Firestorm conversation bridge...");
                    }
                }
                return 0;
            }
            case WM_PIPE_DISCONNECTED:
            {
                auto state = reinterpret_cast<WindowState*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
                HANDLE pipe = reinterpret_cast<HANDLE>(wparam);
                if (state && !state->mClosing && state->mPipe == pipe)
                {
                    CloseHandle(state->mPipe);
                    state->mPipe = INVALID_HANDLE_VALUE;
                    begin_pipe_reconnect(hwnd, state, L"Reconnecting to Firestorm conversation bridge...");
                }
                else if (pipe != INVALID_HANDLE_VALUE)
                {
                    CloseHandle(pipe);
                }
                return 0;
            }
            case WM_PIPE_RECONNECTED:
            {
                auto state = reinterpret_cast<WindowState*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
                HANDLE pipe = reinterpret_cast<HANDLE>(wparam);
                if (state && !state->mClosing && state->mPipe == INVALID_HANDLE_VALUE)
                {
                    state->mPipe = pipe;
                    state->mReconnectInProgress = false;
                    state->mLastPipeReadTick = GetTickCount();
                    SetWindowTextW(state->mStatus, L"Connected to Firestorm conversation bridge");
                    start_pipe_reader(hwnd, state->mPipe);
                }
                else if (pipe != INVALID_HANDLE_VALUE)
                {
                    CloseHandle(pipe);
                }
                return 0;
            }
            case WM_CLOSE:
                DestroyWindow(hwnd);
                return 0;
            case WM_NCDESTROY:
            {
                auto state = reinterpret_cast<WindowState*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
                SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
                if (state)
                {
                    state->mClosing = true;
                    KillTimer(hwnd, IDT_PIPE_HEARTBEAT);
                    if (state->mSessions)
                    {
                        const LRESULT count = SendMessageW(state->mSessions, LB_GETCOUNT, 0, 0);
                        for (LRESULT index = 0; index < count; ++index)
                        {
                            auto session = reinterpret_cast<SessionInfo*>(SendMessageW(state->mSessions, LB_GETITEMDATA, index, 0));
                            delete session;
                        }
                    }
                    if (state->mPipe != INVALID_HANDLE_VALUE)
                    {
                        CloseHandle(state->mPipe);
                    }
                    if (state->mFont)
                    {
                        DeleteObject(state->mFont);
                    }
                    if (state->mHeaderFont)
                    {
                        DeleteObject(state->mHeaderFont);
                    }
                    if (state->mBackgroundBrush)
                    {
                        DeleteObject(state->mBackgroundBrush);
                    }
                    if (state->mSurfaceBrush)
                    {
                        DeleteObject(state->mSurfaceBrush);
                    }
                    if (state->mFieldBrush)
                    {
                        DeleteObject(state->mFieldBrush);
                    }
                }
                delete state;
                PostQuitMessage(0);
                return 0;
            }
            default:
                return DefWindowProc(hwnd, message, wparam, lparam);
        }

        return DefWindowProc(hwnd, message, wparam, lparam);
    }

    bool register_window_class(HINSTANCE instance)
    {
        WNDCLASS wc = {};
        wc.lpfnWndProc = companion_window_proc;
        wc.hInstance = instance;
        wc.lpszClassName = WINDOW_CLASS_NAME;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_EXTERNAL_COMPANION));
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

        return RegisterClass(&wc) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command)
{
    if (!register_window_class(instance))
    {
        return 1;
    }

    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring title = get_argument(argc, argv, L"--title", L"External Communications");
    const std::wstring body = get_argument(argc, argv, L"--body", L"Firestorm companion window");
    const std::wstring pipe_name = get_argument(argc, argv, L"--pipe", L"");
    const std::wstring target = get_argument(argc, argv, L"--target", L"");
    if (argv)
    {
        LocalFree(argv);
    }
    if (target == L"communications")
    {
        title = L"External Communications";
    }

    LoadLibraryW(L"Msftedit.dll");

    auto state = new WindowState();
    state->mTarget = target;
    state->mFont = create_ui_font(14, FW_NORMAL);
    state->mHeaderFont = create_ui_font(15, FW_SEMIBOLD);
    reset_theme_brushes(state);
    HWND hwnd = CreateWindowEx(
        0,
        WINDOW_CLASS_NAME,
        title.c_str(),
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        DEFAULT_WIDTH,
        DEFAULT_HEIGHT,
        nullptr,
        nullptr,
        instance,
        state);

    if (!hwnd)
    {
        delete state;
        return 1;
    }
    SetWindowTextW(hwnd, title.empty() ? L"External Communications" : title.c_str());
    apply_dark_control_theme(hwnd);
    apply_dark_title_bar(hwnd, state->mTheme);
    SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(LoadIconW(instance, MAKEINTRESOURCEW(IDI_EXTERNAL_COMPANION))));
    SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(LoadIconW(instance, MAKEINTRESOURCEW(IDI_EXTERNAL_COMPANION))));

    state->mStatus = CreateWindowEx(
        0,
        L"STATIC",
        body.c_str(),
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        12,
        12,
        DEFAULT_WIDTH - 24,
        DEFAULT_HEIGHT - 64,
        hwnd,
        nullptr,
        instance,
        nullptr);
    if (target == L"communications")
    {
        SetWindowTextW(state->mStatus, L"Communications");
        state->mCommTabs[0] = CreateWindowEx(0, L"BUTTON", L"All", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, control_id(IDC_COMM_ALL), instance, nullptr);
        state->mCommTabs[1] = CreateWindowEx(0, L"BUTTON", L"IMs", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, control_id(IDC_COMM_IMS), instance, nullptr);
        state->mCommTabs[2] = CreateWindowEx(0, L"BUTTON", L"Nearby", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, control_id(IDC_COMM_NEARBY), instance, nullptr);
        state->mCommTabs[3] = CreateWindowEx(0, L"BUTTON", L"Friends", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, control_id(IDC_COMM_FRIENDS), instance, nullptr);
        state->mCommTabs[4] = CreateWindowEx(0, L"BUTTON", L"People", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, control_id(IDC_COMM_PEOPLE), instance, nullptr);

        state->mSessions = CreateWindowEx(WS_EX_CLIENTEDGE, L"LISTBOX", L"", LISTBOX_NOTIFY_STYLE, 0, 0, 0, 0, hwnd, control_id(IDC_SESSIONS), instance, nullptr);
        state->mMessages = CreateWindowEx(WS_EX_CLIENTEDGE, L"RICHEDIT50W", L"", MESSAGE_VIEW_STYLE, 0, 0, 0, 0, hwnd, control_id(IDC_MESSAGES), instance, nullptr);
        state->mInput = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL, 0, 0, 0, 0, hwnd, control_id(IDC_INPUT), instance, nullptr);
        state->mSend = CreateWindowEx(0, L"BUTTON", L"Send", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, control_id(IDC_SEND), instance, nullptr);

        state->mNearbyMessages = CreateWindowEx(WS_EX_CLIENTEDGE, L"RICHEDIT50W", L"", MESSAGE_VIEW_STYLE, 0, 0, 0, 0, hwnd, control_id(IDC_NEARBY_MESSAGES), instance, nullptr);
        state->mNearbyInput = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL, 0, 0, 0, 0, hwnd, control_id(IDC_NEARBY_INPUT), instance, nullptr);
        state->mNearbySend = CreateWindowEx(0, L"BUTTON", L"Say", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, control_id(IDC_NEARBY_SEND), instance, nullptr);

        state->mFriendsSearch = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd, control_id(IDC_FRIENDS_SEARCH), instance, nullptr);
        state->mFriendsList = CreateWindowEx(WS_EX_CLIENTEDGE, L"LISTBOX", L"", LISTBOX_TABS_NOTIFY_STYLE, 0, 0, 0, 0, hwnd, control_id(IDC_FRIENDS_LIST), instance, nullptr);

        state->mPeopleTabs[0] = CreateWindowEx(0, L"BUTTON", L"Nearby", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, control_id(IDC_PEOPLE_NEARBY), instance, nullptr);
        state->mPeopleTabs[1] = CreateWindowEx(0, L"BUTTON", L"Recent", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, control_id(IDC_PEOPLE_RECENT), instance, nullptr);
        state->mPeopleTabs[2] = CreateWindowEx(0, L"BUTTON", L"Blocked", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, control_id(IDC_PEOPLE_BLOCKED), instance, nullptr);
        state->mPeopleList = CreateWindowEx(WS_EX_CLIENTEDGE, L"LISTBOX", L"", LISTBOX_TABS_NOTIFY_STYLE, 0, 0, 0, 0, hwnd, control_id(IDC_PEOPLE_LIST), instance, nullptr);
        refresh_people_list(state);
    }
    else if (target == L"people")
    {
        state->mPeopleTabs[0] = CreateWindowEx(0, L"BUTTON", L"Nearby", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, control_id(IDC_PEOPLE_NEARBY), instance, nullptr);
        state->mPeopleTabs[1] = CreateWindowEx(0, L"BUTTON", L"Recent", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, control_id(IDC_PEOPLE_RECENT), instance, nullptr);
        state->mPeopleTabs[2] = CreateWindowEx(0, L"BUTTON", L"Blocked", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, control_id(IDC_PEOPLE_BLOCKED), instance, nullptr);
        state->mPeopleList = CreateWindowEx(WS_EX_CLIENTEDGE, L"LISTBOX", L"", LISTBOX_TABS_NOTIFY_STYLE, 0, 0, 0, 0, hwnd, control_id(IDC_PEOPLE_LIST), instance, nullptr);
        refresh_people_list(state);
    }
    else if (target == L"nearby_chat")
    {
        SetWindowTextW(state->mStatus, L"Nearby Chat");
        state->mMessages = CreateWindowEx(WS_EX_CLIENTEDGE, L"RICHEDIT50W", L"", MESSAGE_VIEW_STYLE, 0, 0, 0, 0, hwnd, control_id(IDC_MESSAGES), instance, nullptr);
        state->mInput = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL, 0, 0, 0, 0, hwnd, control_id(IDC_INPUT), instance, nullptr);
        state->mSend = CreateWindowEx(0, L"BUTTON", L"Say", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, control_id(IDC_SEND), instance, nullptr);
    }
    else
    {
        state->mSessions = CreateWindowEx(WS_EX_CLIENTEDGE, L"LISTBOX", L"", LISTBOX_NOTIFY_STYLE, 0, 0, 0, 0, hwnd, control_id(IDC_SESSIONS), instance, nullptr);
        state->mMessages = CreateWindowEx(WS_EX_CLIENTEDGE, L"RICHEDIT50W", L"", MESSAGE_VIEW_STYLE, 0, 0, 0, 0, hwnd, control_id(IDC_MESSAGES), instance, nullptr);
        state->mInput = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL, 0, 0, 0, 0, hwnd, control_id(IDC_INPUT), instance, nullptr);
        state->mSend = CreateWindowEx(0, L"BUTTON", L"Send", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, hwnd, control_id(IDC_SEND), instance, nullptr);
    }

    apply_common_fonts(state);
    apply_control_details(state);
    subclass_message_view(state, state->mMessages, &WindowState::mMessagesProc);
    subclass_message_view(state, state->mNearbyMessages, &WindowState::mNearbyMessagesProc);

    if (!pipe_name.empty())
    {
        state->mPipeName = pipe_name;
        state->mLastPipeReadTick = GetTickCount();
        SetTimer(hwnd, IDT_PIPE_HEARTBEAT, 5000, nullptr);
        SetWindowTextW(state->mStatus, L"Connecting to Firestorm conversation bridge...");
        state->mPipe = open_pipe_with_retry(pipe_name);
        if (state->mPipe != INVALID_HANDLE_VALUE)
        {
            state->mReconnectInProgress = false;
            state->mLastPipeReadTick = GetTickCount();
            SetWindowTextW(state->mStatus, L"Connected to Firestorm conversation bridge");
            start_pipe_reader(hwnd, state->mPipe);
        }
        else
        {
            SetWindowTextW(state->mStatus, L"Unable to connect to Firestorm conversation bridge");
            begin_pipe_reconnect(hwnd, state, L"Reconnecting to Firestorm conversation bridge...");
        }
    }

    ShowWindow(hwnd, show_command);
    RECT client_rect = {};
    GetClientRect(hwnd, &client_rect);
    layout_controls(state, client_rect.right - client_rect.left, client_rect.bottom - client_rect.top);
    UpdateWindow(hwnd);

    MSG message;
    while (GetMessage(&message, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessage(&message);
    }

    return 0;
}
