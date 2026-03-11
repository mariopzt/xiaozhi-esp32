#include "memory_store.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <iomanip>
#include <sstream>
#include <vector>

#include "board.h"
#include "settings.h"
#include "system_info.h"

namespace {
constexpr size_t kMaxNotesChars = 1200;
constexpr size_t kMaxRecentTurnsChars = 2800;
constexpr const char* kMemorySyncBaseUrl = "https://xiaozhi-esp32-production.up.railway.app";
constexpr int kMemorySyncTimeoutSeconds = 3;
constexpr int kMemorySyncDebounceMs = 1200;
constexpr int kMemorySyncRetryMs = 2500;

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

cJSON* MemoryStore::BuildContextJson(const std::string& user_name, const std::string& notes, const std::string& recent_turns, const std::string& combined_context_override) const {
    cJSON* json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "user_name", user_name.c_str());
    cJSON_AddStringToObject(json, "notes", notes.c_str());
    cJSON_AddStringToObject(json, "recent_turns", recent_turns.c_str());

    std::string combined_context = combined_context_override;
    if (combined_context.empty()) {
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
    }
    if (!combined_context.empty()) {
        combined_context += "\n\nUse this context only when it is relevant to the user's current request.";
    }
    cJSON_AddStringToObject(json, "combined_context", combined_context.c_str());
    return json;
}

cJSON* MemoryStore::GetContextJson() {
    SyncToBackendAsync();
    std::string user_name = GetUserName();
    std::string notes = GetNotes();
    std::string recent_turns = GetRecentTurns();
    std::string combined_context;
    if (FetchContextFromBackend("", user_name, notes, recent_turns, combined_context)) {
        auto bootstrap = BuildBootstrapFactsText(user_name, notes);
        if (!bootstrap.empty()) {
            combined_context = bootstrap + (combined_context.empty() ? "" : "\n\n" + combined_context);
        }
        return BuildContextJson(user_name, notes, recent_turns, combined_context);
    }
    return BuildContextJson(user_name, notes, recent_turns, BuildBootstrapFactsText(user_name, notes));
}

cJSON* MemoryStore::GetUserProfileJson() {
    SyncToBackendAsync();
    std::string user_name = GetUserName();
    std::string notes = GetNotes();
    std::string recent_turns = GetRecentTurns();
    std::string combined_context;
    FetchContextFromBackend("", user_name, notes, recent_turns, combined_context);

    cJSON* json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "user_name", user_name.c_str());
    cJSON_AddStringToObject(json, "partner_name", ExtractLabeledFact(notes, "Partner").c_str());
    cJSON_AddStringToObject(json, "dog_name", ExtractLabeledFact(notes, "Dog").c_str());
    cJSON_AddStringToObject(json, "cat_name", ExtractLabeledFact(notes, "Cat").c_str());
    cJSON_AddStringToObject(json, "mother_name", ExtractLabeledFact(notes, "Mother").c_str());
    cJSON_AddStringToObject(json, "father_name", ExtractLabeledFact(notes, "Father").c_str());
    cJSON_AddStringToObject(json, "work", ExtractLabeledFact(notes, "Work").c_str());
    cJSON_AddStringToObject(json, "city", ExtractLabeledFact(notes, "City").c_str());
    cJSON_AddStringToObject(json, "notes", notes.c_str());
    cJSON_AddStringToObject(json, "profile_summary", BuildBootstrapFactsText(user_name, notes).c_str());
    return json;
}

cJSON* MemoryStore::GetKeyFactsJson() {
    SyncToBackendAsync();
    std::string user_name = GetUserName();
    std::string notes = GetNotes();
    std::string recent_turns = GetRecentTurns();
    std::string combined_context;
    FetchContextFromBackend("", user_name, notes, recent_turns, combined_context);

    cJSON* json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "user_name", user_name.c_str());
    cJSON_AddStringToObject(json, "partner_name", ExtractLabeledFact(notes, "Partner").c_str());
    cJSON_AddStringToObject(json, "dog_name", ExtractLabeledFact(notes, "Dog").c_str());
    cJSON_AddStringToObject(json, "cat_name", ExtractLabeledFact(notes, "Cat").c_str());
    cJSON_AddStringToObject(json, "mother_name", ExtractLabeledFact(notes, "Mother").c_str());
    cJSON_AddStringToObject(json, "father_name", ExtractLabeledFact(notes, "Father").c_str());
    cJSON_AddStringToObject(json, "work", ExtractLabeledFact(notes, "Work").c_str());
    cJSON_AddStringToObject(json, "city", ExtractLabeledFact(notes, "City").c_str());
    cJSON_AddStringToObject(json, "notes", notes.c_str());
    return json;
}

cJSON* MemoryStore::SearchContextJson(const std::string& query) {
    SyncToBackendAsync();
    std::string user_name = GetUserName();
    std::string notes = GetNotes();
    std::string recent_turns = GetRecentTurns();
    std::string combined_context;
    if (FetchContextFromBackend(query, user_name, notes, recent_turns, combined_context)) {
        auto lowered_query = ToLowerAscii(query);
        std::vector<std::string> direct_lines;

        auto add_direct = [&direct_lines](const std::string& line) {
            if (!line.empty() && std::find(direct_lines.begin(), direct_lines.end(), line) == direct_lines.end()) {
                direct_lines.push_back(line);
            }
        };

        if (lowered_query.find("como me llamo") != std::string::npos ||
            lowered_query.find("cómo me llamo") != std::string::npos ||
            lowered_query.find("mi nombre") != std::string::npos ||
            lowered_query.find("quien soy") != std::string::npos ||
            lowered_query.find("quién soy") != std::string::npos) {
            add_direct(!user_name.empty() ? "The user's name is " + user_name + "." : "");
        }

        if (lowered_query.find("que recuerdas de mi") != std::string::npos ||
            lowered_query.find("que sabes de mi") != std::string::npos) {
            auto bootstrap = BuildBootstrapFactsText(user_name, notes);
            for (const auto& line : SplitLines(bootstrap)) {
                if (line == "Bootstrap memory facts:") {
                    continue;
                }
                if (line.rfind("- ", 0) == 0) {
                    add_direct(line.substr(2));
                }
            }
        }

        if (lowered_query.find("mujer") != std::string::npos ||
            lowered_query.find("esposa") != std::string::npos ||
            lowered_query.find("esposo") != std::string::npos ||
            lowered_query.find("pareja") != std::string::npos ||
            lowered_query.find("novia") != std::string::npos ||
            lowered_query.find("novio") != std::string::npos) {
            auto value = ExtractLabeledFact(notes, "Partner");
            add_direct(!value.empty() ? "The user's partner is " + value + "." : "");
        }

        if (lowered_query.find("perro") != std::string::npos ||
            lowered_query.find("perra") != std::string::npos) {
            auto value = ExtractLabeledFact(notes, "Dog");
            add_direct(!value.empty() ? "The user's dog is " + value + "." : "");
        }

        if (lowered_query.find("gato") != std::string::npos ||
            lowered_query.find("gata") != std::string::npos) {
            auto value = ExtractLabeledFact(notes, "Cat");
            add_direct(!value.empty() ? "The user's cat is " + value + "." : "");
        }

        if (lowered_query.find("madre") != std::string::npos) {
            auto value = ExtractLabeledFact(notes, "Mother");
            add_direct(!value.empty() ? "The user's mother is " + value + "." : "");
        }

        if (lowered_query.find("padre") != std::string::npos) {
            auto value = ExtractLabeledFact(notes, "Father");
            add_direct(!value.empty() ? "The user's father is " + value + "." : "");
        }

        if (lowered_query.find("trabajo") != std::string::npos ||
            lowered_query.find("trabajas") != std::string::npos ||
            lowered_query.find("curro") != std::string::npos ||
            lowered_query.find("dedico") != std::string::npos) {
            auto value = ExtractLabeledFact(notes, "Work");
            add_direct(!value.empty() ? "The user's work is " + value + "." : "");
        }

        if (lowered_query.find("vives") != std::string::npos ||
            lowered_query.find("vivo") != std::string::npos ||
            lowered_query.find("ciudad") != std::string::npos ||
            lowered_query.find("donde") != std::string::npos ||
            lowered_query.find("dónde") != std::string::npos ||
            lowered_query.find("soy de") != std::string::npos) {
            auto value = ExtractLabeledFact(notes, "City");
            add_direct(!value.empty() ? "The user lives in or is from " + value + "." : "");
        }

        if (!direct_lines.empty()) {
            std::string direct_block = "Direct memory matches:\n";
            for (const auto& line : direct_lines) {
                direct_block += "- ";
                direct_block += line;
                direct_block += "\n";
            }
            if (!combined_context.empty()) {
                combined_context = direct_block + "\n" + combined_context;
            } else {
                combined_context = direct_block;
            }
        }
        return BuildContextJson(user_name, notes, recent_turns, combined_context);
    }
    return BuildContextJson(user_name, notes, recent_turns, BuildBootstrapFactsText(user_name, notes));
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
    SyncToBackendAsync();
    ESP_LOGI(TAG, "Stored memory note");
}

void MemoryStore::SyncToBackend() {
    if (SyncSnapshotToBackend()) {
        backend_snapshot_dirty_.store(false, std::memory_order_relaxed);
    } else {
        backend_snapshot_dirty_.store(true, std::memory_order_relaxed);
    }
}

void MemoryStore::SyncToBackendAsync() {
    backend_snapshot_dirty_.store(true, std::memory_order_relaxed);
    if (sync_task_running_.exchange(true, std::memory_order_relaxed)) {
        return;
    }

    auto task_entry = [](void* arg) {
        auto* store = static_cast<MemoryStore*>(arg);
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(kMemorySyncDebounceMs));

            if (!store->backend_snapshot_dirty_.load(std::memory_order_relaxed) && !store->HasPendingTurns()) {
                break;
            }

            bool had_snapshot = store->backend_snapshot_dirty_.exchange(false, std::memory_order_relaxed);
            if (had_snapshot && !store->SyncSnapshotToBackend()) {
                store->backend_snapshot_dirty_.store(true, std::memory_order_relaxed);
                vTaskDelay(pdMS_TO_TICKS(kMemorySyncRetryMs));
                continue;
            }

            while (store->HasPendingTurns()) {
                std::pair<std::string, std::string> next_turn;
                {
                    std::lock_guard<std::mutex> lock(store->pending_turns_mutex_);
                    if (store->pending_turns_.empty()) {
                        break;
                    }
                    next_turn = store->pending_turns_.front();
                }

                if (!store->SyncTurnToBackend(next_turn.first, next_turn.second)) {
                    store->backend_snapshot_dirty_.store(true, std::memory_order_relaxed);
                    vTaskDelay(pdMS_TO_TICKS(kMemorySyncRetryMs));
                    break;
                }

                {
                    std::lock_guard<std::mutex> lock(store->pending_turns_mutex_);
                    if (!store->pending_turns_.empty() &&
                        store->pending_turns_.front().first == next_turn.first &&
                        store->pending_turns_.front().second == next_turn.second) {
                        store->pending_turns_.erase(store->pending_turns_.begin());
                    }
                }
            }

            if (!store->backend_snapshot_dirty_.load(std::memory_order_relaxed) && !store->HasPendingTurns()) {
                break;
            }
        }

        store->sync_task_running_.store(false, std::memory_order_relaxed);
        if (store->backend_snapshot_dirty_.load(std::memory_order_relaxed) || store->HasPendingTurns()) {
            store->SyncToBackendAsync();
        }
        vTaskDelete(nullptr);
    };

    if (xTaskCreate(task_entry, "memory_sync", 4096, this, 1, nullptr) != pdPASS) {
        sync_task_running_.store(false, std::memory_order_relaxed);
        ESP_LOGE(TAG, "Failed to create memory sync task");
    }
}

void MemoryStore::RefreshFromBackend() {
    std::string user_name = GetUserName();
    std::string notes = GetNotes();
    std::string recent_turns = GetRecentTurns();
    std::string combined_context;
    FetchContextFromBackend("", user_name, notes, recent_turns, combined_context);
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
    SyncToBackendAsync();
    if (speaker[0] == 'U') {
        SyncTurnToBackendAsync("user", normalized);
    }
}

void MemoryStore::Clear() {
    Settings settings(kSettingsNamespace, true);
    settings.EraseAll();
    auto http = Board::GetInstance().GetNetwork()->CreateHttp(kMemorySyncTimeoutSeconds);
    if (http != nullptr) {
        std::string url = std::string(kMemorySyncBaseUrl) + "/memory-sync/clear/" + GetDeviceId();
        if (http->Open("POST", url)) {
            http->ReadAll();
            http->Close();
        }
    }
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
        {"mi mujer se llama ", "The user's partner is %s."},
        {"mi esposa se llama ", "The user's partner is %s."},
        {"mi esposo se llama ", "The user's partner is %s."},
        {"el nombre de mi mujer es ", "The user's partner is %s."},
        {"el nombre de mi esposa es ", "The user's partner is %s."},
        {"el nombre de mi esposo es ", "The user's partner is %s."},
        {"mi madre se llama ", "The user's mother is %s."},
        {"mi padre se llama ", "The user's father is %s."},
        {"mi hermana se llama ", "The user's sister is %s."},
        {"mi hermano se llama ", "The user's brother is %s."},
        {"mi hija se llama ", "The user's daughter is %s."},
        {"mi hijo se llama ", "The user's son is %s."},
        {"mi perro se llama ", "The user's dog is %s."},
        {"mi perra se llama ", "The user's dog is %s."},
        {"mi gato se llama ", "The user's cat is %s."},
        {"mi gata se llama ", "The user's cat is %s."},
        {"vivo en ", "The user lives in %s."},
        {"soy de ", "The user is from %s."},
        {"trabajo en ", "The user works at %s."},
        {"trabajo de ", "The user works as %s."},
        {"trabajo como ", "The user works as %s."},
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

std::string MemoryStore::BuildBootstrapFactsText(const std::string& user_name, const std::string& notes) const {
    std::vector<std::string> lines;
    auto add_line = [&lines](const std::string& line) {
        if (!line.empty() && std::find(lines.begin(), lines.end(), line) == lines.end()) {
            lines.push_back(line);
        }
    };

    if (!user_name.empty()) {
        add_line("The user's name is " + user_name + ".");
    }

    const struct {
        const char* label;
        const char* template_text;
    } fact_map[] = {
        {"Partner", "The user's partner is %s."},
        {"Dog", "The user's dog is %s."},
        {"Cat", "The user's cat is %s."},
        {"Mother", "The user's mother is %s."},
        {"Father", "The user's father is %s."},
        {"Work", "The user's work is %s."},
        {"City", "The user lives in or is from %s."},
    };

    for (const auto& fact : fact_map) {
        auto value = ExtractLabeledFact(notes, fact.label);
        if (value.empty()) {
            continue;
        }
        char buffer[192];
        snprintf(buffer, sizeof(buffer), fact.template_text, value.c_str());
        add_line(buffer);
    }

    if (lines.empty()) {
        return "";
    }

    std::string bootstrap = "Bootstrap memory facts:\n";
    for (const auto& line : lines) {
        bootstrap += "- ";
        bootstrap += line;
        bootstrap += "\n";
    }
    return bootstrap;
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
    backend_snapshot_dirty_.store(true, std::memory_order_relaxed);
}

void MemoryStore::SetRecentTurns(const std::string& turns) {
    Settings settings(kSettingsNamespace, true);
    settings.SetString(kRecentTurnsKey, TrimToLimit(turns, kMaxRecentTurnsChars));
    backend_snapshot_dirty_.store(true, std::memory_order_relaxed);
}

void MemoryStore::SetUserName(const std::string& user_name) {
    Settings settings(kSettingsNamespace, true);
    settings.SetString(kUserNameKey, user_name);
    backend_snapshot_dirty_.store(true, std::memory_order_relaxed);
}

std::string MemoryStore::ExtractLabeledFact(const std::string& notes, const char* label) const {
    if (label == nullptr || *label == '\0' || notes.empty()) {
        return "";
    }

    const std::string prefix = std::string(label) + ":";
    for (const auto& raw_line : SplitLines(notes)) {
        auto line = TrimSpaces(raw_line);
        if (line.rfind(prefix, 0) == 0) {
            return TrimSpaces(line.substr(prefix.size()));
        }
    }

    const auto lowered_notes = ToLowerAscii(notes);
    if (strcmp(label, "Partner") == 0) {
        auto pos = lowered_notes.find("the user's partner is ");
        if (pos != std::string::npos) {
            auto start = pos + strlen("the user's partner is ");
            auto end = notes.find('.', start);
            return TrimSpaces(notes.substr(start, end == std::string::npos ? std::string::npos : end - start));
        }
    } else if (strcmp(label, "Dog") == 0) {
        auto pos = lowered_notes.find("the user's dog is ");
        if (pos != std::string::npos) {
            auto start = pos + strlen("the user's dog is ");
            auto end = notes.find('.', start);
            return TrimSpaces(notes.substr(start, end == std::string::npos ? std::string::npos : end - start));
        }
    } else if (strcmp(label, "Cat") == 0) {
        auto pos = lowered_notes.find("the user's cat is ");
        if (pos != std::string::npos) {
            auto start = pos + strlen("the user's cat is ");
            auto end = notes.find('.', start);
            return TrimSpaces(notes.substr(start, end == std::string::npos ? std::string::npos : end - start));
        }
    } else if (strcmp(label, "Mother") == 0) {
        auto pos = lowered_notes.find("the user's mother is ");
        if (pos != std::string::npos) {
            auto start = pos + strlen("the user's mother is ");
            auto end = notes.find('.', start);
            return TrimSpaces(notes.substr(start, end == std::string::npos ? std::string::npos : end - start));
        }
    } else if (strcmp(label, "Father") == 0) {
        auto pos = lowered_notes.find("the user's father is ");
        if (pos != std::string::npos) {
            auto start = pos + strlen("the user's father is ");
            auto end = notes.find('.', start);
            return TrimSpaces(notes.substr(start, end == std::string::npos ? std::string::npos : end - start));
        }
    } else if (strcmp(label, "Work") == 0) {
        auto pos = lowered_notes.find("the user's work is ");
        if (pos != std::string::npos) {
            auto start = pos + strlen("the user's work is ");
            auto end = notes.find('.', start);
            return TrimSpaces(notes.substr(start, end == std::string::npos ? std::string::npos : end - start));
        }
    } else if (strcmp(label, "City") == 0) {
        auto pos = lowered_notes.find("the user lives in or is from ");
        if (pos != std::string::npos) {
            auto start = pos + strlen("the user lives in or is from ");
            auto end = notes.find('.', start);
            return TrimSpaces(notes.substr(start, end == std::string::npos ? std::string::npos : end - start));
        }
    }

    return "";
}

bool MemoryStore::SyncSnapshotToBackend() {
    auto http = Board::GetInstance().GetNetwork()->CreateHttp(kMemorySyncTimeoutSeconds);
    if (http == nullptr) {
        return false;
    }

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "user_name", GetUserName().c_str());
    cJSON_AddStringToObject(root, "notes", GetNotes().c_str());
    cJSON_AddStringToObject(root, "recent_turns", GetRecentTurns().c_str());
    char* payload = cJSON_PrintUnformatted(root);
    std::string body = payload ? payload : "{}";
    if (payload != nullptr) {
        cJSON_free(payload);
    }
    cJSON_Delete(root);

    std::string url = std::string(kMemorySyncBaseUrl) + "/memory-sync/snapshot/" + GetDeviceId();
    http->SetHeader("Content-Type", "application/json");
    http->SetContent(std::move(body));
    if (!http->Open("POST", url)) {
        return false;
    }
    int status = http->GetStatusCode();
    http->ReadAll();
    http->Close();
    return status >= 200 && status < 300;
}

void MemoryStore::SyncTurnToBackendAsync(const std::string& role, const std::string& text) {
    if (role.empty() || text.empty()) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(pending_turns_mutex_);
        pending_turns_.emplace_back(role, text);
    }
    SyncToBackendAsync();
}

bool MemoryStore::SyncTurnToBackend(const std::string& role, const std::string& text) {
    auto http = Board::GetInstance().GetNetwork()->CreateHttp(kMemorySyncTimeoutSeconds);
    if (http == nullptr) {
        return false;
    }

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "session_id", "device-live");
    cJSON_AddStringToObject(root, "role", role.c_str());
    cJSON_AddStringToObject(root, "text", text.c_str());
    cJSON_AddStringToObject(root, "device_id", GetDeviceId().c_str());
    char* payload = cJSON_PrintUnformatted(root);
    std::string body = payload ? payload : "{}";
    if (payload != nullptr) {
        cJSON_free(payload);
    }
    cJSON_Delete(root);

    std::string url = std::string(kMemorySyncBaseUrl) + "/memory-sync/turn/" + GetDeviceId();
    http->SetHeader("Content-Type", "application/json");
    http->SetContent(std::move(body));
    if (!http->Open("POST", url)) {
        return false;
    }
    int status = http->GetStatusCode();
    http->ReadAll();
    http->Close();
    return status >= 200 && status < 300;
}

bool MemoryStore::FetchDueReminder(std::string& reminder_id, std::string& message) {
    reminder_id.clear();
    message.clear();

    auto http = Board::GetInstance().GetNetwork()->CreateHttp(kMemorySyncTimeoutSeconds);
    if (http == nullptr) {
        return false;
    }

    std::string url = std::string(kMemorySyncBaseUrl) + "/memory-sync/reminders/due/" + GetDeviceId() + "?limit=1";
    if (!http->Open("GET", url)) {
        return false;
    }

    int status = http->GetStatusCode();
    std::string response = http->ReadAll();
    http->Close();
    if (status < 200 || status >= 300) {
        return false;
    }

    cJSON* root = cJSON_Parse(response.c_str());
    if (root == nullptr) {
        return false;
    }

    auto items = cJSON_GetObjectItem(root, "items");
    if (cJSON_IsArray(items) && cJSON_GetArraySize(items) > 0) {
        auto* first = cJSON_GetArrayItem(items, 0);
        if (cJSON_IsObject(first)) {
            auto* id_item = cJSON_GetObjectItem(first, "reminder_id");
            auto* message_item = cJSON_GetObjectItem(first, "message");
            auto* text_item = cJSON_GetObjectItem(first, "text");
            if (cJSON_IsString(id_item) && id_item->valuestring != nullptr) {
                reminder_id = id_item->valuestring;
            }
            if (cJSON_IsString(message_item) && message_item->valuestring != nullptr) {
                message = message_item->valuestring;
            } else if (cJSON_IsString(text_item) && text_item->valuestring != nullptr) {
                message = text_item->valuestring;
            }
        }
    }

    cJSON_Delete(root);
    return !reminder_id.empty() && !message.empty();
}

bool MemoryStore::AckDueReminder(const std::string& reminder_id) {
    if (reminder_id.empty()) {
        return false;
    }

    auto http = Board::GetInstance().GetNetwork()->CreateHttp(kMemorySyncTimeoutSeconds);
    if (http == nullptr) {
        return false;
    }

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "reminder_id", reminder_id.c_str());
    char* payload = cJSON_PrintUnformatted(root);
    std::string body = payload ? payload : "{}";
    if (payload != nullptr) {
        cJSON_free(payload);
    }
    cJSON_Delete(root);

    std::string url = std::string(kMemorySyncBaseUrl) + "/memory-sync/reminders/ack/" + GetDeviceId();
    http->SetHeader("Content-Type", "application/json");
    http->SetContent(std::move(body));
    if (!http->Open("POST", url)) {
        return false;
    }

    int status = http->GetStatusCode();
    http->ReadAll();
    http->Close();
    return status >= 200 && status < 300;
}

bool MemoryStore::FetchContextFromBackend(const std::string& query, std::string& user_name, std::string& notes, std::string& recent_turns, std::string& combined_context) {
    auto http = Board::GetInstance().GetNetwork()->CreateHttp(kMemorySyncTimeoutSeconds);
    if (http == nullptr) {
        return false;
    }

    std::string url = std::string(kMemorySyncBaseUrl) + "/memory-sync/context/" + GetDeviceId();
    if (!query.empty()) {
        url += "?query=" + UrlEncode(query);
    }

    if (!http->Open("GET", url)) {
        return false;
    }

    int status = http->GetStatusCode();
    std::string response = http->ReadAll();
    http->Close();
    if (status < 200 || status >= 300) {
        return false;
    }

    cJSON* root = cJSON_Parse(response.c_str());
    if (root == nullptr) {
        return false;
    }

    std::string merged_name = user_name;
    std::string merged_notes = notes;
    std::string merged_turns = recent_turns;

    auto user_name_item = cJSON_GetObjectItem(root, "user_name");
    if (merged_name.empty() && cJSON_IsString(user_name_item) && user_name_item->valuestring != nullptr) {
        merged_name = user_name_item->valuestring;
    }

    auto notes_item = cJSON_GetObjectItem(root, "notes");
    if (cJSON_IsString(notes_item) && notes_item->valuestring != nullptr) {
        merged_notes = MergeUniqueLines(merged_notes, notes_item->valuestring, kMaxNotesChars);
    }

    auto recent_turns_item = cJSON_GetObjectItem(root, "recent_turns");
    if (cJSON_IsString(recent_turns_item) && recent_turns_item->valuestring != nullptr) {
        merged_turns = MergeUniqueLines(merged_turns, recent_turns_item->valuestring, kMaxRecentTurnsChars);
    }

    auto combined_context_item = cJSON_GetObjectItem(root, "combined_context");
    if (cJSON_IsString(combined_context_item) && combined_context_item->valuestring != nullptr) {
        combined_context = combined_context_item->valuestring;
    }

    cJSON_Delete(root);

    if (merged_name != GetUserName()) {
        SetUserName(merged_name);
    }
    if (merged_notes != GetNotes()) {
        SetNotes(merged_notes);
    }
    if (merged_turns != GetRecentTurns()) {
        SetRecentTurns(merged_turns);
    }
    user_name = merged_name;
    notes = merged_notes;
    recent_turns = merged_turns;
    return true;
}

bool MemoryStore::MergeContextFromBackend(const std::string& query) {
    std::string user_name = GetUserName();
    std::string notes = GetNotes();
    std::string recent_turns = GetRecentTurns();
    std::string combined_context;
    return FetchContextFromBackend(query, user_name, notes, recent_turns, combined_context);
}

std::string MemoryStore::GetDeviceId() const {
    return SystemInfo::GetMacAddress();
}

std::string MemoryStore::MergeUniqueLines(const std::string& primary, const std::string& secondary, size_t max_chars) const {
    std::vector<std::string> merged;
    auto append_unique = [&merged](const std::string& text) {
        for (const auto& line : SplitLines(text)) {
            if (line.empty()) {
                continue;
            }
            if (std::find(merged.begin(), merged.end(), line) == merged.end()) {
                merged.push_back(line);
            }
        }
    };

    append_unique(primary);
    append_unique(secondary);

    std::string result;
    for (const auto& line : merged) {
        if (!result.empty()) {
            result += "\n";
        }
        result += line;
    }
    return TrimToLimit(result, max_chars);
}

std::string MemoryStore::UrlEncode(const std::string& value) const {
    std::ostringstream encoded;
    encoded.fill('0');
    encoded << std::hex << std::uppercase;

    for (unsigned char ch : value) {
        if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            encoded << static_cast<char>(ch);
        } else if (ch == ' ') {
            encoded << "%20";
        } else {
            encoded << '%' << std::setw(2) << static_cast<int>(ch);
        }
    }

    return encoded.str();
}

bool MemoryStore::HasPendingTurns() const {
    std::lock_guard<std::mutex> lock(pending_turns_mutex_);
    return !pending_turns_.empty();
}


