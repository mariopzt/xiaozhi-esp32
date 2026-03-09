#include "memory_store.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <esp_log.h>
#include <vector>

#include "settings.h"

namespace {
constexpr size_t kMaxNotesChars = 1200;
constexpr size_t kMaxRecentTurnsChars = 2800;

const char* kSettingsNamespace = "memory";
const char* kNotesKey = "notes";
const char* kRecentTurnsKey = "recent_turns";
const char* kUserNameKey = "user_name";
std::string ToLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string TrimSpaces(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.erase(value.begin());
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    return value;
}

std::vector<std::string> SplitLines(const std::string& text) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            end = text.size();
        }
        auto line = text.substr(start, end - start);
        if (!line.empty()) {
            lines.push_back(line);
        }
        start = end + 1;
    }
    return lines;
}
}

#define TAG "MemoryStore"

MemoryStore& MemoryStore::GetInstance() {
    static MemoryStore instance;
    return instance;
}

cJSON* MemoryStore::GetContextJson() const {
    auto notes = GetNotes();
    auto recent_turns = GetRecentTurns();
    auto user_name = GetUserName();

    cJSON* json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "user_name", user_name.c_str());
    cJSON_AddStringToObject(json, "notes", notes.c_str());
    cJSON_AddStringToObject(json, "recent_turns", recent_turns.c_str());

    std::string combined_context;
    if (!user_name.empty()) {
        combined_context += "Known user identity:\n";
        combined_context += "The user's name is ";
        combined_context += user_name;
        combined_context += ".";
    }
    if (!notes.empty()) {
        if (!combined_context.empty()) {
            combined_context += "\n\n";
        }
        combined_context += "Saved long-term memory:\n";
        combined_context += notes;
    }
    if (!recent_turns.empty()) {
        if (!combined_context.empty()) {
            combined_context += "\n\n";
        }
        combined_context += "Recent cross-session conversation:\n";
        combined_context += recent_turns;
    }
    if (!combined_context.empty()) {
        combined_context += "\n\nUse this context only when it is relevant to the user's current request.";
    }
    cJSON_AddStringToObject(json, "combined_context", combined_context.c_str());
    return json;
}

cJSON* MemoryStore::GetUserProfileJson() const {
    cJSON* json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "user_name", GetUserName().c_str());
    cJSON_AddStringToObject(json, "notes", GetNotes().c_str());
    return json;
}

void MemoryStore::Remember(const std::string& note) {
    auto normalized = NormalizeText(note);
    if (normalized.empty()) {
        return;
    }

    auto notes = GetNotes();
    auto lines = SplitLines(notes);
    if (std::find(lines.begin(), lines.end(), normalized) != lines.end()) {
        return;
    }

    if (!notes.empty()) {
        notes += "\n";
    }
    notes += normalized;
    SetNotes(TrimToLimit(notes, kMaxNotesChars));
    ESP_LOGI(TAG, "Stored memory note");
}

void MemoryStore::LearnFromUserText(const std::string& text) {
    auto normalized = NormalizeText(text);
    if (normalized.empty()) {
        return;
    }

    if (IsAffirmativeConfirmation(normalized)) {
        auto pending_candidate = GetPendingRememberCandidateFromRecentTurns();
        if (!pending_candidate.empty()) {
            ESP_LOGI(TAG, "Remembering prior user fact after confirmation: %s", pending_candidate.c_str());
            RememberExtractedFacts(pending_candidate);
        }
    }

    RememberExtractedFacts(normalized);
}

bool MemoryStore::IsAffirmativeConfirmation(const std::string& text) const {
    auto lowered = ToLowerAscii(TrimSpaces(text));
    return lowered == "si" ||
        lowered == "s\xc3\xad" ||
        lowered == "claro" ||
        lowered == "vale" ||
        lowered == "ok" ||
        lowered == "okay" ||
        lowered == "yes";
}

bool MemoryStore::AssistantAskedToRemember(const std::string& text) const {
    auto lowered = ToLowerAscii(TrimSpaces(text));
    return lowered.find("recuerde") != std::string::npos ||
        lowered.find("recordar") != std::string::npos ||
        lowered.find("guarde") != std::string::npos ||
        lowered.find("guardar") != std::string::npos ||
        lowered.find("tenga en cuenta") != std::string::npos ||
        lowered.find("remember") != std::string::npos;
}

std::string MemoryStore::GetPendingRememberCandidateFromRecentTurns() const {
    auto lines = SplitLines(GetRecentTurns());
    if (lines.size() < 2) {
        return "";
    }

    for (size_t idx = lines.size(); idx-- > 0;) {
        const auto& line = lines[idx];
        if (line.rfind("A: ", 0) != 0) {
            continue;
        }
        auto assistant_text = line.substr(3);
        if (!AssistantAskedToRemember(assistant_text)) {
            continue;
        }

        for (size_t prev = idx; prev-- > 0;) {
            const auto& previous_line = lines[prev];
            if (previous_line.rfind("U: ", 0) == 0) {
                return TrimSpaces(previous_line.substr(3));
            }
        }
    }

    return "";
}

void MemoryStore::RememberExtractedFacts(const std::string& text) {
    auto normalized = NormalizeText(text);
    if (normalized.empty()) {
        return;
    }

    auto user_name = ExtractUserName(normalized);
    auto remembered_note = ExtractRememberNote(normalized);
    auto age_fact = ExtractAgeFact(normalized);
    auto profile_facts = ExtractProfileFacts(normalized);

    if (!user_name.empty() && user_name != GetUserName()) {
        SetUserName(user_name);
        Remember("The user's name is " + user_name + ".");
        ESP_LOGI(TAG, "Learned user name: %s", user_name.c_str());
    }

    if (!remembered_note.empty()) {
        Remember(remembered_note);
    }

    if (!age_fact.empty()) {
        Remember(age_fact);
    }

    for (const auto& fact : profile_facts) {
        Remember(fact);
    }

    if (user_name.empty() &&
        remembered_note.empty() &&
        age_fact.empty() &&
        profile_facts.empty() &&
        normalized.size() <= 96) {
        Remember(normalized);
    }
}

void MemoryStore::AppendConversationLine(const char* speaker, const std::string& text) {
    if (speaker == nullptr || *speaker == '\0') {
        return;
    }

    auto normalized = NormalizeText(text);
    if (normalized.empty()) {
        return;
    }

    std::string line = std::string(speaker) + ": " + normalized;
    auto turns = GetRecentTurns();
    auto lines = SplitLines(turns);
    if (!lines.empty() && lines.back() == line) {
        return;
    }

    if (!turns.empty()) {
        turns += "\n";
    }
    turns += line;
    SetRecentTurns(TrimToLimit(turns, kMaxRecentTurnsChars));
}

void MemoryStore::Clear() {
    Settings settings(kSettingsNamespace, true);
    settings.EraseAll();
    ESP_LOGI(TAG, "Cleared persisted memory");
}

std::string MemoryStore::NormalizeText(const std::string& text) const {
    std::string normalized;
    normalized.reserve(text.size());

    bool previous_was_space = true;
    for (unsigned char ch : text) {
        if (std::isspace(ch)) {
            if (!previous_was_space) {
                normalized.push_back(' ');
                previous_was_space = true;
            }
            continue;
        }
        normalized.push_back(static_cast<char>(ch));
        previous_was_space = false;
    }

    while (!normalized.empty() && normalized.back() == ' ') {
        normalized.pop_back();
    }
    return normalized;
}

std::string MemoryStore::TrimToLimit(const std::string& text, size_t max_chars) const {
    if (text.size() <= max_chars) {
        return text;
    }

    auto lines = SplitLines(text);
    std::string trimmed;
    for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
        size_t extra = trimmed.empty() ? 0 : 1;
        if (it->size() + extra + trimmed.size() > max_chars) {
            if (trimmed.empty()) {
                return it->substr(it->size() - max_chars);
            }
            break;
        }
        if (trimmed.empty()) {
            trimmed = *it;
        } else {
            trimmed = *it + "\n" + trimmed;
        }
    }
    return trimmed;
}

std::string MemoryStore::ExtractUserName(const std::string& text) const {
    auto lowered = ToLowerAscii(text);
    const std::vector<std::string> patterns = {
        "me llamo ",
        "mi nombre es ",
        "llamame ",
        "soy "
    };

    for (const auto& pattern : patterns) {
        auto pos = lowered.find(pattern);
        if (pos == std::string::npos) {
            continue;
        }

        std::string candidate = text.substr(pos + pattern.size());
        size_t end = candidate.find_first_of(".,;:!?");
        if (end != std::string::npos) {
            candidate = candidate.substr(0, end);
        }
        candidate = TrimSpaces(candidate);
        if (candidate.empty()) {
            return "";
        }

        int word_count = 0;
        bool in_word = false;
        std::string limited;
        for (char ch : candidate) {
            if (std::isspace(static_cast<unsigned char>(ch))) {
                if (in_word) {
                    word_count++;
                    in_word = false;
                    if (word_count >= 4) {
                        break;
                    }
                }
                if (!limited.empty() && limited.back() != ' ') {
                    limited.push_back(' ');
                }
                continue;
            }
            limited.push_back(ch);
            in_word = true;
        }
        if (in_word) {
            word_count++;
        }

        limited = TrimSpaces(limited);
        if (word_count == 0 || word_count > 4) {
            return "";
        }
        return limited;
    }

    return "";
}

std::string MemoryStore::ExtractRememberNote(const std::string& text) const {
    auto lowered = ToLowerAscii(text);
    const std::vector<std::string> patterns = {
        "recuerda que ",
        "recuerda ",
        "acuerdate de que ",
        "acuérdate de que ",
        "acuerdate que ",
        "acuérdate que "
    };

    for (const auto& pattern : patterns) {
        auto pos = lowered.find(pattern);
        if (pos == std::string::npos) {
            continue;
        }

        auto note = TrimSpaces(text.substr(pos + pattern.size()));
        if (note.empty()) {
            return "";
        }
        return note;
    }

    return "";
}

std::string MemoryStore::ExtractAgeFact(const std::string& text) const {
    auto lowered = ToLowerAscii(text);
    auto pos = lowered.find("tengo ");
    if (pos == std::string::npos) {
        return "";
    }

    std::string rest = lowered.substr(pos + 6);
    size_t index = 0;
    while (index < rest.size() && std::isspace(static_cast<unsigned char>(rest[index]))) {
        index++;
    }

    std::string digits;
    while (index < rest.size() && std::isdigit(static_cast<unsigned char>(rest[index]))) {
        digits.push_back(rest[index]);
        index++;
    }
    if (digits.empty()) {
        return "";
    }

    while (index < rest.size() && std::isspace(static_cast<unsigned char>(rest[index]))) {
        index++;
    }

    auto suffix = rest.substr(index);
    bool looks_like_years =
        suffix.rfind("ano", 0) == 0 ||
        suffix.rfind("anos", 0) == 0 ||
        suffix.rfind(std::string("a\xc3\xb1o"), 0) == 0 ||
        suffix.rfind(std::string("a\xc3\xb1os"), 0) == 0;
    if (!looks_like_years) {
        return "";
    }

    return "The user is " + digits + " years old.";
}

std::vector<std::string> MemoryStore::ExtractProfileFacts(const std::string& text) const {
    std::vector<std::string> facts;
    auto lowered = ToLowerAscii(text);

    struct Pattern {
        const char* prefix;
        const char* template_text;
    };

    const std::vector<Pattern> patterns = {
        {"mi pareja se llama ", "The user's partner is %s."},
        {"mi novia se llama ", "The user's partner is %s."},
        {"mi novio se llama ", "The user's partner is %s."},
        {"mi madre se llama ", "The user's mother is %s."},
        {"mi padre se llama ", "The user's father is %s."},
        {"mi hermana se llama ", "The user's sister is %s."},
        {"mi hermano se llama ", "The user's brother is %s."},
        {"mi perro se llama ", "The user's dog is %s."},
        {"mi perra se llama ", "The user's dog is %s."},
        {"mi gato se llama ", "The user's cat is %s."},
        {"mi gata se llama ", "The user's cat is %s."},
        {"vivo en ", "The user lives in %s."},
        {"soy de ", "The user is from %s."},
        {"trabajo en ", "The user works at %s."},
        {"trabajo de ", "The user works as %s."},
        {"me dedico a ", "The user works as %s."},
        {"me gusta ", "The user likes %s."},
        {"no me gusta ", "The user does not like %s."},
        {"mi color favorito es ", "The user's favorite color is %s."},
        {"mi comida favorita es ", "The user's favorite food is %s."},
        {"mi juego favorito es ", "The user's favorite game is %s."},
    };

    for (const auto& pattern : patterns) {
        auto pos = lowered.find(pattern.prefix);
        if (pos == std::string::npos) {
            continue;
        }

        std::string value = text.substr(pos + strlen(pattern.prefix));
        size_t end = value.find_first_of(".;:!?");
        if (end != std::string::npos) {
            value = value.substr(0, end);
        }
        value = TrimSpaces(value);
        if (value.empty() || value.size() > 64) {
            continue;
        }

        char buffer[160];
        snprintf(buffer, sizeof(buffer), pattern.template_text, value.c_str());
        facts.emplace_back(buffer);
    }

    if ((lowered.rfind("tengo un ", 0) == 0 || lowered.rfind("tengo una ", 0) == 0) && text.size() <= 80) {
        facts.emplace_back("The user said: " + text + ".");
    }

    return facts;
}

std::string MemoryStore::GetNotes() const {
    Settings settings(kSettingsNamespace);
    return settings.GetString(kNotesKey);
}

std::string MemoryStore::GetRecentTurns() const {
    Settings settings(kSettingsNamespace);
    return settings.GetString(kRecentTurnsKey);
}

std::string MemoryStore::GetUserName() const {
    Settings settings(kSettingsNamespace);
    return settings.GetString(kUserNameKey);
}

void MemoryStore::SetNotes(const std::string& notes) {
    Settings settings(kSettingsNamespace, true);
    settings.SetString(kNotesKey, TrimToLimit(notes, kMaxNotesChars));
}

void MemoryStore::SetRecentTurns(const std::string& turns) {
    Settings settings(kSettingsNamespace, true);
    settings.SetString(kRecentTurnsKey, TrimToLimit(turns, kMaxRecentTurnsChars));
}

void MemoryStore::SetUserName(const std::string& user_name) {
    Settings settings(kSettingsNamespace, true);
    settings.SetString(kUserNameKey, user_name);
}
