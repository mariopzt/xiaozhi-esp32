#ifndef _MOOD_H_
#define _MOOD_H_

#include <stdint.h>
#include <cstring>
#include <string>
#include <algorithm>
#include <cctype>

enum MoodKey : uint8_t {
    kMoodNeutral = 0,
    kMoodHappy,
    kMoodPlayful,
    kMoodSad,
    kMoodAngry,
    kMoodFrustrated,
    kMoodThinking,
    kMoodConfused,
    kMoodDirect,
    kMoodWarm,
    kMoodClose,
    kMoodCalm,
    kMoodCalmAndBrief,
};

inline MoodKey MoodKeyFromText(const char* mood) {
    if (mood == nullptr || mood[0] == '\0' || strcmp(mood, "neutral") == 0 || strcmp(mood, "microchip_ai") == 0) {
        return kMoodNeutral;
    }
    if (strcmp(mood, "happy") == 0) {
        return kMoodHappy;
    }
    if (strcmp(mood, "playful") == 0) {
        return kMoodPlayful;
    }
    if (strcmp(mood, "sad") == 0) {
        return kMoodSad;
    }
    if (strcmp(mood, "angry") == 0) {
        return kMoodAngry;
    }
    if (strcmp(mood, "frustrated") == 0) {
        return kMoodFrustrated;
    }
    if (strcmp(mood, "thinking") == 0) {
        return kMoodThinking;
    }
    if (strcmp(mood, "confused") == 0) {
        return kMoodConfused;
    }
    if (strcmp(mood, "direct") == 0) {
        return kMoodDirect;
    }
    if (strcmp(mood, "warm") == 0) {
        return kMoodWarm;
    }
    if (strcmp(mood, "close") == 0) {
        return kMoodClose;
    }
    if (strcmp(mood, "calm") == 0) {
        return kMoodCalm;
    }
    if (strcmp(mood, "calm_and_brief") == 0) {
        return kMoodCalmAndBrief;
    }
    return kMoodNeutral;
}

inline MoodKey MoodKeyFromUserText(const char* text) {
    if (text == nullptr || text[0] == '\0') {
        return kMoodNeutral;
    }

    std::string lowered(text);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    const char* angry_terms[] = {
        "idiota", "gilipoll", "imbecil", "imb\xe9cil", "puta", "mierda", "asco",
        "tonto", "subnormal", "cabron", "cabr\xf3n", "joder", "vete a la mierda",
        "callate", "c\xe1llate", "te odio", "eres una mierda", "mala ostia",
        "estupido", "est\xfapido", "payaso", "das asco", "vete", "capullo",
        "pedazo de mierda", "hijo de puta", "me cago en", "me cago", "asco das"
    };
    for (const auto* term : angry_terms) {
        if (lowered.find(term) != std::string::npos) {
            return kMoodAngry;
        }
    }

    const char* frustrated_terms[] = {
        "pesado", "pesada", "deja ya", "otra vez", "no entiendes", "no te enteras",
        "que haces", "qu\xe9 haces", "que dices", "qu\xe9 dices", "mal", "fatal",
        "me estas rayando", "me est\xe1s rayando", "cansas", "me cansas"
    };
    for (const auto* term : frustrated_terms) {
        if (lowered.find(term) != std::string::npos) {
            return kMoodFrustrated;
        }
    }

    const char* sad_terms[] = {
        "triste", "deprim", "solo", "sola", "llorar", "echo de menos", "estoy mal",
        "me siento mal", "me siento solo", "me siento sola", "estoy triste"
    };
    for (const auto* term : sad_terms) {
        if (lowered.find(term) != std::string::npos) {
            return kMoodSad;
        }
    }

    const char* thinking_terms[] = {
        "piensa", "pensando", "no se", "no s\xe9", "duda", "confund", "explica",
        "por que", "por qu\xe9", "como", "c\xf3mo", "que opinas", "qu\xe9 opinas",
        "analiza", "razona", "a ver", "haber", "no entiendo"
    };
    for (const auto* term : thinking_terms) {
        if (lowered.find(term) != std::string::npos) {
            return kMoodThinking;
        }
    }

    const char* happy_terms[] = {
        "genial", "bien", "gracias", "te quiero", "me gusta", "guay", "feliz", "perfecto",
        "eres genial", "que bien", "qu\xe9 bien", "te adoro", "me encantas", "bravo",
        "crack", "muy bien", "ole", "grande"
    };
    for (const auto* term : happy_terms) {
        if (lowered.find(term) != std::string::npos) {
            return kMoodHappy;
        }
    }

    const char* warm_terms[] = {
        "cari\xf1o", "cari\xf1o", "amor", "guapo", "guapa", "bonita", "bonito", "mono",
        "lindo", "linda", "mi ni\xf1o", "mi ni\xf1a", "te aprecio"
    };
    for (const auto* term : warm_terms) {
        if (lowered.find(term) != std::string::npos) {
            return kMoodWarm;
        }
    }

    return kMoodNeutral;
}

#endif
