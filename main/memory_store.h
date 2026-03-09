#ifndef MEMORY_STORE_H
#define MEMORY_STORE_H

#include <string>
#include <vector>

#include <cJSON.h>

class MemoryStore {
public:
    static MemoryStore& GetInstance();

    cJSON* GetContextJson() const;
    cJSON* GetUserProfileJson() const;
    void Remember(const std::string& note);
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
    std::string GetNotes() const;
    std::string GetRecentTurns() const;
    std::string GetUserName() const;
    void SetNotes(const std::string& notes);
    void SetRecentTurns(const std::string& turns);
    void SetUserName(const std::string& user_name);
};

#endif
