#ifndef MEMORY_STORE_H
#define MEMORY_STORE_H

#include <string>
#include <vector>
#include <atomic>
#include <mutex>

#include <cJSON.h>
#include "mood.h"

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
    bool FetchDueReminder(std::string& reminder_id, std::string& message);
    bool AckDueReminder(const std::string& reminder_id);
    void LearnFromUserText(const std::string& text);
    void AppendConversationLine(const char* speaker, const std::string& text);
    void Clear();
    std::string GetSessionMood() const;
    std::string GetRelationshipTone() const;
    std::string GetAssistantStyle() const;
    MoodKey GetSessionMoodKey() const { return static_cast<MoodKey>(session_mood_key_.load()); }
    MoodKey GetRelationshipToneKey() const { return static_cast<MoodKey>(relationship_tone_key_.load()); }

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
    void SetStyleState(const std::string& session_mood, const std::string& relationship_tone, const std::string& assistant_style);

    std::atomic<bool> backend_snapshot_dirty_{false};
    std::atomic<bool> sync_task_running_{false};
    mutable std::mutex pending_turns_mutex_;
    mutable std::mutex style_state_mutex_;
    std::vector<std::pair<std::string, std::string>> pending_turns_;
    std::atomic<uint8_t> session_mood_key_{kMoodNeutral};
    std::atomic<uint8_t> relationship_tone_key_{kMoodNeutral};
    std::string session_mood_;
    std::string relationship_tone_;
    std::string assistant_style_;
};

#endif


