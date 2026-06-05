/**
 * @file fsexternalfloaterhost.cpp
 * @brief Native OS host windows for detached Firestorm floater experiments.
 */

#include "llviewerprecompiledheaders.h"

#include "fsexternalfloaterhost.h"

#include "llagent.h"
#include "llavataractions.h"
#include "llavatarname.h"
#include "llavatarnamecache.h"
#include "lldate.h"
#include "lldir.h"
#include "fsradar.h"
#include "fsnearbychathub.h"
#include "llcallingcard.h"
#include "llimview.h"
#include "lllogchat.h"
#include "llmainthreadtask.h"
#include "llmutelist.h"
#include "llrecentpeople.h"
#include "llsd.h"
#include "llstring.h"
#include "llurlaction.h"
#include "llviewercontrol.h"
#include "llworld.h"

#if LL_WINDOWS
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <list>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>
#endif

FSExternalFloaterHost& FSExternalFloaterHost::instance()
{
    static FSExternalFloaterHost host;
    return host;
}

void FSExternalFloaterHost::show(Target target)
{
    const std::string title = target == Target::COMMUNICATIONS ?
        "External Communications" :
        llformat("Firestorm External %s", getTitle(target));
    const std::string body = llformat(
        "External %s host prototype\r\n\r\n"
        "Firestorm action surface: %s\r\n\r\n"
        "This native OS window is the reusable shell for companion Firestorm tools.\r\n"
        "Next steps:\r\n"
        "  1. Build native controls for this tool.\r\n"
        "  2. Read Firestorm data models directly.\r\n"
        "  3. Call Firestorm actions directly for send/search/open behavior.\r\n",
        getTitle(target),
        getFloaterName(target));

    showHostWindow(target, title, body);
}

const char* FSExternalFloaterHost::getTitle(Target target) const
{
    switch (target)
    {
        case Target::COMMUNICATIONS:
            return "Communications";
        case Target::CONVERSATIONS:
            return "Conversations";
        case Target::PEOPLE:
            return "People";
        case Target::NEARBY_CHAT:
            return "Nearby Chat";
        case Target::WORLD_MAP:
            return "World Map";
        case Target::INVENTORY:
            return "Inventory";
    }

    return "Unknown";
}

const char* FSExternalFloaterHost::getFloaterName(Target target) const
{
    switch (target)
    {
        case Target::COMMUNICATIONS:
            return "communications";
        case Target::CONVERSATIONS:
            return "fs_im_container";
        case Target::PEOPLE:
            return "people";
        case Target::NEARBY_CHAT:
            return "nearby_chat";
        case Target::WORLD_MAP:
            return "world_map";
        case Target::INVENTORY:
            return "inventory";
    }

    return "";
}

#if LL_WINDOWS
namespace
{
    constexpr const char* PIPE_PREFIX = "\\\\.\\pipe\\FirestormExternal-";

    std::wstring utf8_to_wstring(const std::string& value)
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

    std::string get_pipe_name()
    {
        return std::string(PIPE_PREFIX) + std::to_string(GetCurrentProcessId());
    }

    std::string sanitize_pipe_field(std::string value)
    {
        for (char& ch : value)
        {
            if (ch == '\r' || ch == '\n')
            {
                ch = ' ';
            }
            else if (ch == '|')
            {
                ch = '/';
            }
        }
        return value;
    }

    std::string base64_encode(const std::string& value)
    {
        static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string result;
        int buffer = 0;
        int bits = -6;
        for (unsigned char ch : value)
        {
            buffer = (buffer << 8) | ch;
            bits += 8;
            while (bits >= 0)
            {
                result.push_back(alphabet[(buffer >> bits) & 0x3f]);
                bits -= 6;
            }
        }
        if (bits > -6)
        {
            result.push_back(alphabet[((buffer << 8) >> (bits + 8)) & 0x3f]);
        }
        while (result.size() % 4)
        {
            result.push_back('=');
        }
        return result;
    }

    std::string build_log_root_line()
    {
        std::string path = gDirUtilp ? gDirUtilp->getPerAccountChatLogsDir() : std::string();
        return "LOG_ROOT|" + base64_encode(path);
    }

    bool write_pipe_line(HANDLE pipe, const std::string& line)
    {
        std::string payload = line;
        payload += "\n";

        DWORD written = 0;
        return WriteFile(pipe, payload.data(), static_cast<DWORD>(payload.size()), &written, nullptr)
            && written == payload.size();
    }

    std::vector<std::string> split_pipe_line(const std::string& line)
    {
        std::vector<std::string> parts;
        std::stringstream stream(line);
        std::string part;
        while (std::getline(stream, part, '|'))
        {
            parts.push_back(part);
        }
        return parts;
    }

    std::string build_session_line(const LLIMModel::LLIMSession* session)
    {
        if (!session)
        {
            return std::string();
        }

        std::string line = "SESSION|";
        line += session->mSessionID.asString();
        line += "|";
        line += sanitize_pipe_field(session->mName);
        line += "|";
        line += session->mOtherParticipantID.asString();
        line += "|";
        line += std::to_string(static_cast<int>(session->mType));
        line += "|";
        line += std::to_string(session->mNumUnread);
        return line;
    }

    std::string build_message_line(const LLUUID& session_id, const LLSD& message)
    {
        std::string line = "MSG|";
        line += session_id.asString();
        line += "|";
        line += sanitize_pipe_field(message["from"].asString());
        line += "|";
        line += sanitize_pipe_field(message["time"].asString());
        line += "|";
        line += sanitize_pipe_field(message["message"].asString());
        if (message.has("index"))
        {
            line += "|";
            line += std::to_string(message["index"].asInteger());
        }
        return line;
    }

    std::string build_history_message_line(const LLUUID& session_id, const LLSD& message)
    {
        std::string line = "HISTORY_MSG|";
        line += session_id.asString();
        line += "|";
        line += sanitize_pipe_field(message[LL_IM_FROM].asString());
        line += "|";
        line += sanitize_pipe_field(message[LL_IM_TIME].asString());
        line += "|";
        line += sanitize_pipe_field(message[LL_IM_TEXT].asString());
        return line;
    }

    std::vector<std::string> build_history_lines(const LLIMModel::LLIMSession* session, size_t max_messages = 30)
    {
        std::vector<std::string> lines;
        if (!session || session->mHistoryFileName.empty())
        {
            return lines;
        }

        std::list<LLSD> history;
        LLLogChat::loadChatHistory(session->mHistoryFileName, history, LLSD(), session->isGroupSessionType());
        if (history.empty())
        {
            return lines;
        }

        lines.push_back("HISTORY_BEGIN|" + session->mSessionID.asString());
        const size_t skip_count = history.size() > max_messages ? history.size() - max_messages : 0;
        size_t index = 0;
        S32 sent_count = 0;
        for (const LLSD& message : history)
        {
            if (index++ < skip_count)
            {
                continue;
            }

            lines.push_back(build_history_message_line(session->mSessionID, message));
            ++sent_count;
        }
        lines.push_back(llformat("HISTORY_DONE|%s|%d", session->mSessionID.asString().c_str(), sent_count));
        return lines;
    }

    std::string get_avatar_display_name(const LLUUID& avatar_id)
    {
        LLAvatarName avatar_name;
        if (LLAvatarNameCache::get(avatar_id, &avatar_name))
        {
            return avatar_name.getCompleteName();
        }

        return avatar_id.asString();
    }

    std::string format_seconds_duration(F64 seconds)
    {
        const S32 total_seconds = llmax(0, static_cast<S32>(seconds));
        const S32 hours = total_seconds / 3600;
        const S32 minutes = (total_seconds % 3600) / 60;
        const S32 secs = total_seconds % 60;
        return llformat("%d:%02d:%02d", hours, minutes, secs);
    }

    struct PeopleRow
    {
        LLUUID id;
        std::string name;
        F64 distance = 0.0;
        std::string time;
        std::string age;
    };

    std::string build_people_row_line(const std::string& tab, const PeopleRow& row)
    {
        std::string line = "PEOPLE|";
        line += sanitize_pipe_field(tab);
        line += "|";
        line += row.id.asString();
        line += "|";
        line += sanitize_pipe_field(row.name);
        line += "|";
        line += sanitize_pipe_field(llformat("%.2f", row.distance));
        line += "|";
        line += sanitize_pipe_field(row.time);
        line += "|";
        line += sanitize_pipe_field(row.age);
        return line;
    }

    std::string yes_no(bool value)
    {
        return value ? "yes" : "no";
    }

    std::string build_friend_line(const LLUUID& id, const std::string& name, const LLRelationship* relation)
    {
        const bool online = relation && relation->isOnline();
        std::string line = "FRIEND|";
        line += id.asString();
        line += "|";
        line += sanitize_pipe_field(name);
        line += "|";
        line += online ? "online" : "offline";
        line += "|";
        line += relation ? yes_no(relation->isRightGrantedTo(LLRelationship::GRANT_ONLINE_STATUS)) : "no";
        line += "|";
        line += relation ? yes_no(relation->isRightGrantedTo(LLRelationship::GRANT_MAP_LOCATION)) : "no";
        line += "|";
        line += relation ? yes_no(relation->isRightGrantedTo(LLRelationship::GRANT_MODIFY_OBJECTS)) : "no";
        return line;
    }

    std::vector<PeopleRow> get_radar_people_rows()
    {
        std::vector<PeopleRow> rows;
        std::vector<LLSD> radar_entries;
        LLSD radar_stats;
        FSRadar::getInstance()->getCurrentData(radar_entries, radar_stats);

        for (const LLSD& radar_entry_data : radar_entries)
        {
            const LLSD& entry = radar_entry_data["entry"];
            PeopleRow row;
            row.id = entry["id"].asUUID();
            row.name = entry["name"].asString();
            const std::string range = entry["range"].asString();
            row.distance = std::atof(range.c_str());
            row.time = entry["seen"].asString();
            row.age = entry["age"].asString();
            rows.push_back(row);
        }

        std::sort(rows.begin(), rows.end(), [](const PeopleRow& lhs, const PeopleRow& rhs)
        {
            return lhs.distance < rhs.distance;
        });

        return rows;
    }

    std::string build_message_line(const LLUUID& session_id,
                                   const std::string& from,
                                   const std::string& time,
                                   const std::string& message)
    {
        std::string line = "MSG|";
        line += session_id.asString();
        line += "|";
        line += sanitize_pipe_field(from);
        line += "|";
        line += sanitize_pipe_field(time);
        line += "|";
        line += sanitize_pipe_field(message);
        return line;
    }

    std::string build_session_line(const LLUUID& session_id,
                                   const LLUUID& other_participant_id,
                                   const std::string& session_name,
                                   EInstantMessage dialog,
                                   S32 unread)
    {
        std::string line = "SESSION|";
        line += session_id.asString();
        line += "|";
        line += sanitize_pipe_field(session_name);
        line += "|";
        line += other_participant_id.asString();
        line += "|";
        line += std::to_string(static_cast<int>(dialog));
        line += "|";
        line += std::to_string(unread);
        return line;
    }

    std::wstring quote_command_argument(const std::wstring& value)
    {
        std::wstring result = L"\"";
        size_t backslashes = 0;

        for (wchar_t ch : value)
        {
            if (ch == L'\\')
            {
                ++backslashes;
                continue;
            }

            if (ch == L'"')
            {
                result.append(backslashes * 2 + 1, L'\\');
            }
            else
            {
                result.append(backslashes, L'\\');
            }

            result.push_back(ch);
            backslashes = 0;
        }

        result.append(backslashes * 2, L'\\');
        result.push_back(L'"');
        return result;
    }

    std::wstring get_companion_executable_path()
    {
        wchar_t module_path[MAX_PATH] = {};
        DWORD path_length = GetModuleFileNameW(nullptr, module_path, MAX_PATH);
        if (path_length == 0 || path_length >= MAX_PATH)
        {
            return L"firestorm-external.exe";
        }

        std::filesystem::path executable_path(module_path);
        executable_path.replace_filename(L"firestorm-external.exe");
        return executable_path.wstring();
    }
}
#endif

void FSExternalFloaterHost::showHostWindow(Target target, const std::string& title, const std::string& body)
{
#if LL_WINDOWS
    traceLine(llformat("showHostWindow target=%s title=%s", getFloaterName(target), title.c_str()));
    ensureIpcServer();

    const std::wstring companion_path = get_companion_executable_path();
    std::wstring command_line = quote_command_argument(companion_path);
    command_line += L" --target ";
    command_line += quote_command_argument(utf8_to_wstring(getFloaterName(target)));
    command_line += L" --pipe ";
    command_line += quote_command_argument(utf8_to_wstring(get_pipe_name()));
    command_line += L" --title ";
    command_line += quote_command_argument(utf8_to_wstring(title));
    command_line += L" --body ";
    command_line += quote_command_argument(utf8_to_wstring(body));

    STARTUPINFOW startup_info = {};
    startup_info.cb = sizeof(startup_info);

    PROCESS_INFORMATION process_info = {};
    if (CreateProcessW(
            companion_path.c_str(),
            command_line.data(),
            nullptr,
            nullptr,
            FALSE,
            0,
            nullptr,
            nullptr,
            &startup_info,
            &process_info))
    {
        traceLine("CreateProcess succeeded for firestorm-external.exe");
        CloseHandle(process_info.hThread);
        CloseHandle(process_info.hProcess);
        return;
    }

    traceLine(llformat("CreateProcess failed for firestorm-external.exe error=%lu", GetLastError()));
    LL_WARNS() << "Unable to launch Firestorm external companion: "
               << title << LL_ENDL;
#else
    LL_WARNS() << "External floater host windows are currently implemented for Windows only: "
               << title << LL_ENDL;
#endif
}

#if LL_WINDOWS
void FSExternalFloaterHost::traceLine(const std::string& message)
{
    static std::mutex sTraceMutex;
    std::lock_guard<std::mutex> lock(sTraceMutex);

    const std::string path = gDirUtilp
        ? gDirUtilp->getExpandedFilename(LL_PATH_LOGS, "external_im_trace.log")
        : "external_im_trace.log";

    std::ofstream out(path, std::ios::app);
    if (!out.is_open())
    {
        return;
    }

    SYSTEMTIME st = {};
    GetLocalTime(&st);
    out << llformat(
               "%04d-%02d-%02d %02d:%02d:%02d.%03d pid=%lu ",
               st.wYear,
               st.wMonth,
               st.wDay,
               st.wHour,
               st.wMinute,
               st.wSecond,
               st.wMilliseconds,
               GetCurrentProcessId())
        << message << "\n";
}

void FSExternalFloaterHost::ensureIpcServer()
{
    traceLine("ensureIpcServer");
    std::call_once(mStartPipeServer, [this]()
    {
        traceLine("start pipe accept thread");
        LL_INFOS("FSExternal") << "Starting external conversation pipe server" << LL_ENDL;
        std::thread(&FSExternalFloaterHost::acceptPipeClients, this).detach();
    });

    std::call_once(mStartBroadcastWriter, [this]()
    {
        traceLine("start queued broadcast writer");
        LL_INFOS("FSExternal") << "Starting external conversation pipe writer" << LL_ENDL;
        std::thread(&FSExternalFloaterHost::broadcastQueuedLines, this).detach();
    });

    std::call_once(mStartConversationPoller, [this]()
    {
        traceLine("start conversation poller");
        LL_INFOS("FSExternal") << "Starting external conversation model poller" << LL_ENDL;
        std::thread(&FSExternalFloaterHost::pollConversationModel, this).detach();
    });

    std::call_once(mStartPeoplePoller, [this]()
    {
        traceLine("start people poller");
        LL_INFOS("FSExternal") << "Starting external people model poller" << LL_ENDL;
        std::thread(&FSExternalFloaterHost::pollPeopleModel, this).detach();
    });
}

void FSExternalFloaterHost::acceptPipeClients()
{
    const std::string pipe_name = get_pipe_name();
    traceLine("acceptPipeClients pipe=" + pipe_name);

    while (true)
    {
        HANDLE pipe = CreateNamedPipeA(
            pipe_name.c_str(),
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            8192,
            8192,
            0,
            nullptr);

        if (pipe == INVALID_HANDLE_VALUE)
        {
            traceLine(llformat("CreateNamedPipe failed error=%lu pipe=%s", GetLastError(), pipe_name.c_str()));
            LL_WARNS("FSExternal") << "Unable to create external conversation pipe: " << pipe_name << LL_ENDL;
            return;
        }

        const BOOL connected = ConnectNamedPipe(pipe, nullptr) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);
        if (connected)
        {
            traceLine("pipe client connected");
            LL_INFOS("FSExternal") << "External conversation companion connected" << LL_ENDL;
            DWORD mode = PIPE_READMODE_BYTE | PIPE_NOWAIT;
            if (!SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr))
            {
                traceLine(llformat("SetNamedPipeHandleState nonblocking failed error=%lu", GetLastError()));
            }
            {
                std::lock_guard<std::mutex> lock(mClientsMutex);
                mClients.push_back(pipe);
            }
            queueBroadcastLine("DEBUG|Queued writer connected to companion");

            std::thread(&FSExternalFloaterHost::handlePipeClient, this, pipe).detach();
        }
        else
        {
            CloseHandle(pipe);
        }
    }
}

void FSExternalFloaterHost::handlePipeClient(HANDLE pipe)
{
    sendInitialConversationSnapshot(pipe);
    sendPeopleSnapshot(pipe);

    std::string pending;
    char buffer[1024] = {};
    DWORD bytes_read = 0;

    while (true)
    {
        if (!ReadFile(pipe, buffer, sizeof(buffer), &bytes_read, nullptr) || bytes_read == 0)
        {
            const DWORD error = GetLastError();
            if (error == ERROR_NO_DATA)
            {
                Sleep(50);
                continue;
            }

            traceLine(llformat("handlePipeClient read ended error=%lu", error));
            break;
        }

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
            handlePipeCommand(line);
        }
    }

    {
        std::lock_guard<std::mutex> lock(mClientsMutex);
        mClients.erase(std::remove(mClients.begin(), mClients.end(), pipe), mClients.end());
    }

    DisconnectNamedPipe(pipe);
    CloseHandle(pipe);
}

void FSExternalFloaterHost::sendInitialConversationSnapshot(HANDLE pipe)
{
    LLMainThreadTask::dispatch([this, pipe]()
    {
        traceLine("sendInitialConversationSnapshot begin");
        write_pipe_line(pipe, "READY|Firestorm conversation bridge");
        write_pipe_line(pipe, build_log_root_line());
        write_pipe_line(pipe, "DEBUG|Snapshot requested from Firestorm conversation model");

        S32 session_count = 0;
        S32 message_count = 0;
        for (const auto& entry : LLIMModel::instance().mId2SessionMap)
        {
            const LLIMModel::LLIMSession* session = entry.second;
            if (!session)
            {
                continue;
            }

            std::string line = build_session_line(session);
            write_pipe_line(pipe, line);
            for (const std::string& history_line : build_history_lines(session))
            {
                write_pipe_line(pipe, history_line);
            }
            ++session_count;

            S32 max_message_index = -1;
            for (const LLSD& message : session->mMsgs)
            {
                if (message.has("index"))
                {
                    max_message_index = std::max(max_message_index, message["index"].asInteger());
                }
                const bool is_history = message.has("is_history") && message["is_history"].asInteger() != 0;
                if (!is_history)
                {
                    write_pipe_line(pipe, build_message_line(session->mSessionID, message));
                    ++message_count;
                }
            }

            {
                std::lock_guard<std::mutex> lock(mSessionMessageCountsMutex);
                mSessionMessageCounts[session->mSessionID] = session->mMsgs.size();
                mSessionLastMessageIndex[session->mSessionID] = max_message_index;
                mBroadcastHistorySessionIds.insert(session->mSessionID);
            }
        }

        write_pipe_line(pipe, llformat("SNAPSHOT_DONE|%d|%d", session_count, message_count));
        traceLine(llformat("sendInitialConversationSnapshot done sessions=%d messages=%d", session_count, message_count));
        LL_INFOS("FSExternal") << "Sent external conversation snapshot: sessions="
                               << session_count << ", messages="
                               << message_count << LL_ENDL;
    });
}

void FSExternalFloaterHost::sendPeopleSnapshot(HANDLE pipe)
{
    LLMainThreadTask::dispatch([this, pipe]()
    {
        traceLine("sendPeopleSnapshot begin");

        std::vector<PeopleRow> nearby_rows = get_radar_people_rows();

        write_pipe_line(pipe, "PEOPLE_CLEAR|nearby");
        for (const PeopleRow& row : nearby_rows)
        {
            write_pipe_line(pipe, build_people_row_line("nearby", row));
        }
        write_pipe_line(pipe, llformat("PEOPLE_DONE|nearby|%d", static_cast<S32>(nearby_rows.size())));

        uuid_vec_t recent_ids;
        LLRecentPeople::instance().get(recent_ids);
        write_pipe_line(pipe, "PEOPLE_CLEAR|recent");
        S32 recent_count = 0;
        for (const LLUUID& id : recent_ids)
        {
            PeopleRow row;
            row.id = id;
            row.name = get_avatar_display_name(id);
            row.distance = 0.0;
            const LLDate recent_date = LLRecentPeople::instance().getDate(id);
            row.time = format_seconds_duration(LLDate::now().secondsSinceEpoch() - recent_date.secondsSinceEpoch());
            row.age = "-";
            write_pipe_line(pipe, build_people_row_line("recent", row));
            ++recent_count;
        }
        write_pipe_line(pipe, llformat("PEOPLE_DONE|recent|%d", recent_count));

        const std::vector<LLMute> mutes = LLMuteList::getInstance()->getMutes();
        write_pipe_line(pipe, "PEOPLE_CLEAR|blocked");
        S32 blocked_count = 0;
        for (const LLMute& mute : mutes)
        {
            if (mute.mType != LLMute::AGENT && mute.mType != LLMute::BY_NAME)
            {
                continue;
            }

            PeopleRow row;
            row.id = mute.mID;
            row.name = mute.mName.empty() && mute.mID.notNull() ? get_avatar_display_name(mute.mID) : mute.mName;
            row.distance = 0.0;
            row.time = "-";
            row.age = mute.getDisplayType();
            write_pipe_line(pipe, build_people_row_line("blocked", row));
            ++blocked_count;
        }
        write_pipe_line(pipe, llformat("PEOPLE_DONE|blocked|%d", blocked_count));

        LLAvatarTracker::buddy_map_t buddies;
        LLAvatarTracker::instance().copyBuddyList(buddies);
        write_pipe_line(pipe, "FRIENDS_CLEAR");
        S32 friend_count = 0;
        S32 online_friend_count = 0;
        for (const auto& buddy : buddies)
        {
            const LLUUID& id = buddy.first;
            const LLRelationship* relation = LLAvatarTracker::instance().getBuddyInfo(id);
            if (relation && relation->isOnline())
            {
                ++online_friend_count;
            }
            write_pipe_line(pipe, build_friend_line(id, get_avatar_display_name(id), relation));
            ++friend_count;
        }
        write_pipe_line(pipe, llformat("FRIENDS_DONE|%d|%d", friend_count, online_friend_count));

        traceLine(llformat("sendPeopleSnapshot done nearby=%d recent=%d blocked=%d",
            static_cast<S32>(nearby_rows.size()),
            recent_count,
            blocked_count));
    });
}

void FSExternalFloaterHost::handlePipeCommand(const std::string& line)
{
    traceLine("handlePipeCommand line=" + sanitize_pipe_field(line));
    const std::vector<std::string> parts = split_pipe_line(line);
    if (parts.size() >= 3 && parts[0] == "SEND_NEARBY")
    {
        const EChatType chat_type = static_cast<EChatType>(std::atoi(parts[1].c_str()));
        const std::string text = parts[2];

        LLMainThreadTask::dispatch([chat_type, text]()
        {
            FSNearbyChat::instance().sendChat(utf8str_to_wstring(text), chat_type);
        });
        return;
    }

    if (parts.size() >= 2 && parts[0] == "OPEN_IM")
    {
        const LLUUID avatar_id(parts[1]);
        if (avatar_id.notNull())
        {
            LLMainThreadTask::dispatch([this, avatar_id]()
            {
                LLAvatarNameCache::get(avatar_id, [this](const LLUUID& agent_id, const LLAvatarName& avatar_name)
                {
                    const std::string name = avatar_name.getDisplayName();
                    const LLUUID session_id = gIMMgr->addSession(name, IM_NOTHING_SPECIAL, agent_id);
                    if (session_id.notNull())
                    {
                        queueBroadcastLine(build_session_line(session_id, agent_id, name, IM_NOTHING_SPECIAL, 0));
                    }
                });
            });
        }
        return;
    }

    if (parts.size() >= 2 && parts[0] == "ZOOM_AVATAR")
    {
        const LLUUID avatar_id(parts[1]);
        if (avatar_id.notNull() && avatar_id != gAgent.getID())
        {
            LLMainThreadTask::dispatch([avatar_id]()
            {
                LLAvatarActions::zoomIn(avatar_id);
            });
        }
        return;
    }

    if (parts.size() >= 2 && parts[0] == "OPEN_SLURL")
    {
        const std::string url = parts[1];
        if (!url.empty())
        {
            LLMainThreadTask::dispatch([url]()
            {
                const std::string lowered = utf8str_tolower(url);
                if (lowered.rfind("http://maps.secondlife.com/", 0) == 0 ||
                    lowered.rfind("https://maps.secondlife.com/", 0) == 0)
                {
                    LLUrlAction::showLocationOnMap(url);
                }
                else
                {
                    LLUrlAction::executeSLURL(url, true);
                }
            });
        }
        return;
    }

    if (parts.size() < 5 || parts[0] != "SEND")
    {
        return;
    }

    const LLUUID session_id(parts[1]);
    const LLUUID other_participant_id(parts[2]);
    const EInstantMessage dialog = static_cast<EInstantMessage>(std::atoi(parts[3].c_str()));
    const std::string text = parts[4];

    LLMainThreadTask::dispatch([session_id, other_participant_id, dialog, text]()
    {
        LLIMModel::sendMessage(text, session_id, other_participant_id, dialog);
    });
}

void FSExternalFloaterHost::queueDebugLine(const std::string& message)
{
    traceLine("queueDebugLine " + message);
    queueBroadcastLine("DEBUG|" + sanitize_pipe_field(message));
}

void FSExternalFloaterHost::queueNearbyChatMessage(const std::string& from,
                                                   const std::string& time,
                                                   const std::string& message,
                                                   S32 chat_type)
{
    ensureIpcServer();

    std::string line = "CHAT|";
    line += sanitize_pipe_field(from);
    line += "|";
    line += sanitize_pipe_field(time);
    line += "|";
    line += sanitize_pipe_field(message);
    line += "|";
    line += std::to_string(chat_type);
    queueBroadcastLine(line);
}

void FSExternalFloaterHost::queueTypingState(const LLUUID& session_id, const LLUUID& from_id, bool typing)
{
    if (session_id.isNull() || from_id.isNull())
    {
        return;
    }

    ensureIpcServer();

    std::string line = "TYPING|";
    line += session_id.asString();
    line += "|";
    line += from_id.asString();
    line += "|";
    line += typing ? "1" : "0";
    queueBroadcastLine(line);
}

void FSExternalFloaterHost::queueIMMgrMessage(const LLUUID& session_id,
                                              const LLUUID& other_participant_id,
                                              const std::string& session_name,
                                              EInstantMessage dialog,
                                              const std::string& from,
                                              const LLUUID& from_id,
                                              const std::string& message,
                                              U32 timestamp)
{
    traceLine(llformat(
        "queueIMMgrMessage enter session=%s other=%s dialog=%d from=%s from_id=%s text=%s",
        session_id.asString().c_str(),
        other_participant_id.asString().c_str(),
        static_cast<int>(dialog),
        from.c_str(),
        from_id.asString().c_str(),
        message.substr(0, 120).c_str()));

    if (session_id.isNull() || message.empty())
    {
        traceLine("queueIMMgrMessage skipped null session or empty message");
        return;
    }

    ensureIpcServer();

    LLIMModel::LLIMSession* session = LLIMModel::instance().findIMSession(session_id);
    if (session)
    {
        queueBroadcastLine(build_session_line(session));
    }
    else
    {
        queueBroadcastLine(build_session_line(session_id, other_participant_id, session_name, dialog, 0));
    }

    queueBroadcastLine(build_message_line(
        session_id,
        from,
        LLLogChat::timestamp2LogString(timestamp, true),
        message));
    queueBroadcastLine("DEBUG|Mirrored IM message from viewer model");

    {
        std::lock_guard<std::mutex> lock(mSessionMessageCountsMutex);
        ++mSessionMessageCounts[session_id];
    }

    LL_INFOS("FSExternal") << "Queued direct LLIMMgr message for external conversation session "
                           << session_id << LL_ENDL;
    traceLine("queueIMMgrMessage queued session=" + session_id.asString());
}

void FSExternalFloaterHost::onNewIM(const LLSD& message)
{
    traceLine("onNewIM signal");
    if (!message.has("session_id"))
    {
        traceLine("onNewIM skipped missing session_id");
        return;
    }

    const LLUUID session_id = message["session_id"].asUUID();
    if (session_id.isNull())
    {
        traceLine("onNewIM skipped null session_id");
        return;
    }

    LLIMModel::LLIMSession* session = LLIMModel::instance().findIMSession(session_id);
    if (session)
    {
        queueBroadcastLine(build_session_line(session));
    }

    queueBroadcastLine(build_message_line(session_id, message));
    LL_INFOS("FSExternal") << "Queued external conversation message for session "
                           << session_id << LL_ENDL;
    traceLine("onNewIM queued session=" + session_id.asString());
}

void FSExternalFloaterHost::pollConversationModel()
{
    while (true)
    {
        Sleep(1000);

        LLMainThreadTask::dispatch([this]()
        {
            S32 session_count = 0;
            S32 new_message_count = 0;

            for (const auto& entry : LLIMModel::instance().mId2SessionMap)
            {
                const LLIMModel::LLIMSession* session = entry.second;
                if (!session)
                {
                    continue;
                }

                ++session_count;
                queueBroadcastLine(build_session_line(session));

                bool send_history = false;
                {
                    std::lock_guard<std::mutex> lock(mSessionMessageCountsMutex);
                    send_history = mBroadcastHistorySessionIds.insert(session->mSessionID).second;
                }
                if (send_history)
                {
                    for (const std::string& history_line : build_history_lines(session))
                    {
                        queueBroadcastLine(history_line);
                    }
                }

                S32 previous_last_index = -1;
                {
                    std::lock_guard<std::mutex> lock(mSessionMessageCountsMutex);
                    const auto index_iter = mSessionLastMessageIndex.find(session->mSessionID);
                    if (index_iter != mSessionLastMessageIndex.end())
                    {
                        previous_last_index = index_iter->second;
                    }
                }

                S32 max_seen_index = previous_last_index;
                for (const LLSD& message : session->mMsgs)
                {
                    const S32 message_index = message.has("index") ? message["index"].asInteger() : -1;
                    max_seen_index = std::max(max_seen_index, message_index);

                    const bool is_history = message.has("is_history") && message["is_history"].asInteger() != 0;
                    if (!is_history && message_index > previous_last_index)
                    {
                        queueBroadcastLine(build_message_line(session->mSessionID, message));
                        ++new_message_count;
                    }
                }

                {
                    std::lock_guard<std::mutex> lock(mSessionMessageCountsMutex);
                    mSessionMessageCounts[session->mSessionID] = session->mMsgs.size();
                    mSessionLastMessageIndex[session->mSessionID] = max_seen_index;
                }
            }

            if (session_count > 0 || new_message_count > 0)
            {
                traceLine(llformat("pollConversationModel sessions=%d new_messages=%d", session_count, new_message_count));
                LL_INFOS("FSExternal") << "Polled external conversations: sessions="
                                       << session_count << ", new_messages="
                                       << new_message_count << LL_ENDL;
            }
        });
    }
}

void FSExternalFloaterHost::pollPeopleModel()
{
    while (true)
    {
        Sleep(2000);

        LLMainThreadTask::dispatch([this]()
        {
            std::vector<std::string> lines;

            std::vector<PeopleRow> nearby_rows = get_radar_people_rows();

            lines.push_back("PEOPLE_CLEAR|nearby");
            for (const PeopleRow& row : nearby_rows)
            {
                lines.push_back(build_people_row_line("nearby", row));
            }
            lines.push_back(llformat("PEOPLE_DONE|nearby|%d", static_cast<S32>(nearby_rows.size())));

            uuid_vec_t recent_ids;
            LLRecentPeople::instance().get(recent_ids);
            lines.push_back("PEOPLE_CLEAR|recent");
            S32 recent_count = 0;
            for (const LLUUID& id : recent_ids)
            {
                PeopleRow row;
                row.id = id;
                row.name = get_avatar_display_name(id);
                const LLDate recent_date = LLRecentPeople::instance().getDate(id);
                row.time = format_seconds_duration(LLDate::now().secondsSinceEpoch() - recent_date.secondsSinceEpoch());
                row.age = "-";
                lines.push_back(build_people_row_line("recent", row));
                ++recent_count;
            }
            lines.push_back(llformat("PEOPLE_DONE|recent|%d", recent_count));

            const std::vector<LLMute> mutes = LLMuteList::getInstance()->getMutes();
            lines.push_back("PEOPLE_CLEAR|blocked");
            S32 blocked_count = 0;
            for (const LLMute& mute : mutes)
            {
                if (mute.mType != LLMute::AGENT && mute.mType != LLMute::BY_NAME)
                {
                    continue;
                }

                PeopleRow row;
                row.id = mute.mID;
                row.name = mute.mName.empty() && mute.mID.notNull() ? get_avatar_display_name(mute.mID) : mute.mName;
                row.time = "-";
                row.age = mute.getDisplayType();
                lines.push_back(build_people_row_line("blocked", row));
                ++blocked_count;
            }
            lines.push_back(llformat("PEOPLE_DONE|blocked|%d", blocked_count));

            LLAvatarTracker::buddy_map_t buddies;
            LLAvatarTracker::instance().copyBuddyList(buddies);
            lines.push_back("FRIENDS_CLEAR");
            S32 friend_count = 0;
            S32 online_friend_count = 0;
            for (const auto& buddy : buddies)
            {
                const LLUUID& id = buddy.first;
                const LLRelationship* relation = LLAvatarTracker::instance().getBuddyInfo(id);
                const std::string name = get_avatar_display_name(id);
                if (relation && relation->isOnline())
                {
                    ++online_friend_count;
                }
                lines.push_back(build_friend_line(id, name, relation));
                ++friend_count;
            }
            lines.push_back(llformat("FRIENDS_DONE|%d|%d", friend_count, online_friend_count));

            for (const std::string& line : lines)
            {
                queueBroadcastLine(line);
            }

            traceLine(llformat("pollPeopleModel nearby=%d recent=%d blocked=%d",
                static_cast<S32>(nearby_rows.size()),
                recent_count,
                blocked_count));
        });
    }
}

void FSExternalFloaterHost::sessionAdded(const LLUUID& session_id, const std::string& name, const LLUUID& other_participant_id, bool has_offline_msg)
{
    traceLine(llformat("sessionAdded session=%s name=%s other=%s offline=%d",
        session_id.asString().c_str(),
        name.c_str(),
        other_participant_id.asString().c_str(),
        static_cast<int>(has_offline_msg)));
}

void FSExternalFloaterHost::sessionActivated(const LLUUID& session_id, const std::string& name, const LLUUID& other_participant_id)
{
}

void FSExternalFloaterHost::sessionVoiceOrIMStarted(const LLUUID& session_id)
{
}

void FSExternalFloaterHost::sessionRemoved(const LLUUID& session_id)
{
    traceLine("sessionRemoved session=" + session_id.asString());
    std::lock_guard<std::mutex> lock(mSessionMessageCountsMutex);
    mSessionMessageCounts.erase(session_id);
    mSessionLastMessageIndex.erase(session_id);
    mBroadcastHistorySessionIds.erase(session_id);
}

void FSExternalFloaterHost::sessionIDUpdated(const LLUUID& old_session_id, const LLUUID& new_session_id)
{
    traceLine("sessionIDUpdated old=" + old_session_id.asString() + " new=" + new_session_id.asString());
    std::lock_guard<std::mutex> lock(mSessionMessageCountsMutex);

    const auto count_iter = mSessionMessageCounts.find(old_session_id);
    if (count_iter != mSessionMessageCounts.end())
    {
        mSessionMessageCounts[new_session_id] = count_iter->second;
        mSessionMessageCounts.erase(count_iter);
    }

    const auto index_iter = mSessionLastMessageIndex.find(old_session_id);
    if (index_iter != mSessionLastMessageIndex.end())
    {
        mSessionLastMessageIndex[new_session_id] = index_iter->second;
        mSessionLastMessageIndex.erase(index_iter);
    }

    const auto history_iter = mBroadcastHistorySessionIds.find(old_session_id);
    if (history_iter != mBroadcastHistorySessionIds.end())
    {
        mBroadcastHistorySessionIds.insert(new_session_id);
        mBroadcastHistorySessionIds.erase(history_iter);
    }
}

void FSExternalFloaterHost::queueBroadcastLine(const std::string& line)
{
    if (line.rfind("FRIEND|", 0) != 0 && line.rfind("PEOPLE|", 0) != 0)
    {
        traceLine("queueBroadcastLine " + line.substr(0, 180));
    }
    {
        std::lock_guard<std::mutex> lock(mPendingLinesMutex);
        constexpr size_t MAX_PENDING_LINES = 4096;
        while (mPendingLines.size() >= MAX_PENDING_LINES)
        {
            mPendingLines.pop_front();
        }
        mPendingLines.push_back(line);
    }
    mPendingLinesCV.notify_one();
}

void FSExternalFloaterHost::broadcastQueuedLines()
{
    while (true)
    {
        std::deque<std::string> lines;
        {
            std::unique_lock<std::mutex> lock(mPendingLinesMutex);
            mPendingLinesCV.wait(lock, [this]()
            {
                return !mPendingLines.empty();
            });
            lines.swap(mPendingLines);
        }

        std::vector<HANDLE> clients;
        {
            std::lock_guard<std::mutex> lock(mClientsMutex);
            clients = mClients;
        }
        traceLine(llformat("broadcastQueuedLines woke lines=%d clients=%d",
            static_cast<S32>(lines.size()),
            static_cast<S32>(clients.size())));

        std::vector<HANDLE> failed_clients;
        for (const std::string& line : lines)
        {
            for (HANDLE pipe : clients)
            {
                traceLine("broadcastQueuedLines writing " + line.substr(0, 180));
                if (!write_pipe_line(pipe, line))
                {
                    failed_clients.push_back(pipe);
                    traceLine(llformat("broadcastQueuedLines write failed error=%lu line=%s",
                        GetLastError(),
                        line.substr(0, 180).c_str()));
                }
                else
                {
                    traceLine("broadcastQueuedLines wrote " + line.substr(0, 180));
                }
            }
        }

        if (!failed_clients.empty())
        {
            std::lock_guard<std::mutex> lock(mClientsMutex);
            for (HANDLE pipe : failed_clients)
            {
                mClients.erase(std::remove(mClients.begin(), mClients.end(), pipe), mClients.end());
            }
        }
    }
}
#endif
