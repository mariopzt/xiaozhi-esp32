#ifndef MEMORY_STORE_H
#define MEMORY_STORE_H

#include <string>
#include <vector>
#include <atomic>
#include <mutex>

#include <cJSON.h>

class MemoryStore {
public:
    static MemoryStore& GetInstance();

    cJSON* GetContextJson();
    cJSON* GetUserProfileJson();
    cJSON* GetKeyFactsJson();
    cJSON* SearchContextJson(const std::string& query);
    void Remember(const std::string& note);
    void SyncToBackend();
    void SyncToBackendAsync();
    void SyncTurnToBackendAsync(const std::string& role, const std::string& text);
    void RefreshFromBackend();
    void LearnFromUserText(const std::string& text);
    void AppendConversationLine(const char* speaker, const std::string& text);
    void Clear();

private:
    MemoryStore() = default;

    std::string NormalizeText(const std::string& text) const;
    std::string TrimToLimit(const std::string& text, size_t max_chars) const;
    bool IsAffirmativeConfirmation(const std::string& text) const;
    bool AssistantAskedToRemember(const std::string& text) const;
    std::string GetPendingRememberCandidateFromRecentTurns() const;
    void RememberExtractedFacts(const std::string& text);
    std::string ExtractUserName(const std::string& text) const;
    std::string ExtractRememberNote(const std::string& text) const;
    std::string ExtractAgeFact(const std::string& text) const;
    std::vector<std::string> ExtractProfileFacts(const std::string& text) const;
    std::string BuildBootstrapFactsText(const std::string& user_name, const std::string& notes) const;
    cJSON* BuildContextJson(const std::string& user_name, const std::string& notes, const std::string& recent_turns, const std::string& combined_context = "") const;
    std::string GetNotes() const;
    std::string GetRecentTurns() const;
    std::string GetUserName() const;
    void SetNotes(const std::string& notes);
    void SetRecentTurns(const std::string& turns);
    void SetUserName(const std::string& user_name);
    std::string ExtractLabeledFact(const std::string& notes, const char* label) const;
    bool SyncSnapshotToBackend();
    bool SyncTurnToBackend(const std::string& role, const std::string& text);
    bool FetchContextFromBackend(const std::string& query, std::string& user_name, std::string& notes, std::string& recent_turns, std::string& combined_context);
    bool MergeContextFromBackend(const std::string& query);
    std::string GetDeviceId() const;
    std::string MergeUniqueLines(const std::string& primary, const std::string& secondary, size_t max_chars) const;
    std::string UrlEncode(const std::string& value) const;
    bool HasPendingTurns() const;

    std::atomic<bool> backend_snapshot_dirty_{false};
    std::atomic<bool> sync_task_running_{false};
    mutable std::mutex pending_turns_mutex_;
    std::vector<std::pair<std::string, std::string>> pending_turns_;
};

#endif
