/**
 * @file fsexternalfloaterhost.h
 * @brief Native OS host windows for detached Firestorm floater experiments.
 */

#ifndef FS_EXTERNAL_FLOATER_HOST_H
#define FS_EXTERNAL_FLOATER_HOST_H

#include "llimview.h"

#include <string>

#if LL_WINDOWS
#include "boost/signals2/connection.hpp"

#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#include <windows.h>
#endif

class FSExternalFloaterHost : public LLIMSessionObserver
{
public:
    enum class Target
    {
        COMMUNICATIONS,
        CONVERSATIONS,
        PEOPLE,
        NEARBY_CHAT,
        WORLD_MAP,
        INVENTORY
    };

    static FSExternalFloaterHost& instance();

    void show(Target target);
    const char* getTitle(Target target) const;
    const char* getFloaterName(Target target) const;

#if LL_WINDOWS
    void traceLine(const std::string& message);
    void queueDebugLine(const std::string& message);
    void queueIMMgrMessage(const LLUUID& session_id,
                           const LLUUID& other_participant_id,
                           const std::string& session_name,
                           EInstantMessage dialog,
                           const std::string& from,
                           const LLUUID& from_id,
                           const std::string& message,
                           U32 timestamp);
    void queueNearbyChatMessage(const std::string& from,
                                const std::string& time,
                                const std::string& message,
                                S32 chat_type);
#endif

private:
    FSExternalFloaterHost() = default;
    FSExternalFloaterHost(const FSExternalFloaterHost&) = delete;
    FSExternalFloaterHost& operator=(const FSExternalFloaterHost&) = delete;

    void showHostWindow(Target target, const std::string& title, const std::string& body);

#if LL_WINDOWS
    void ensureIpcServer();
    void acceptPipeClients();
    void handlePipeClient(HANDLE pipe);
    void sendInitialConversationSnapshot(HANDLE pipe);
    void sendPeopleSnapshot(HANDLE pipe);
    void handlePipeCommand(const std::string& line);
    void onNewIM(const LLSD& message);
    void pollConversationModel();
    void pollPeopleModel();
    void sessionAdded(const LLUUID& session_id, const std::string& name, const LLUUID& other_participant_id, bool has_offline_msg) override;
    void sessionActivated(const LLUUID& session_id, const std::string& name, const LLUUID& other_participant_id) override;
    void sessionVoiceOrIMStarted(const LLUUID& session_id) override;
    void sessionRemoved(const LLUUID& session_id) override;
    void sessionIDUpdated(const LLUUID& old_session_id, const LLUUID& new_session_id) override;
    void queueBroadcastLine(const std::string& line);
    void broadcastQueuedLines();

    std::once_flag mStartPipeServer;
    std::once_flag mStartBroadcastWriter;
    std::once_flag mStartConversationPoller;
    std::once_flag mStartPeoplePoller;
    std::once_flag mConnectIMSignals;
    std::once_flag mConnectIMSessionObserver;
    std::mutex mClientsMutex;
    std::vector<HANDLE> mClients;
    std::mutex mSessionMessageCountsMutex;
    std::map<LLUUID, size_t> mSessionMessageCounts;
    std::map<LLUUID, S32> mSessionLastMessageIndex;
    std::set<LLUUID> mBroadcastHistorySessionIds;
    std::mutex mPendingLinesMutex;
    std::condition_variable mPendingLinesCV;
    std::deque<std::string> mPendingLines;
    boost::signals2::connection mNewIMConnection;
#endif
};

#endif // FS_EXTERNAL_FLOATER_HOST_H
