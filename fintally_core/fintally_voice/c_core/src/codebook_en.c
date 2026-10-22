#include <stddef.h>
#include "codebooks/codebook_en.h"

static char fin_voice_ascii_lower(char value)
{
    if (value >= 'A' && value <= 'Z') {
        return (char)(value + ('a' - 'A'));
    }
    return value;
}

static int fin_voice_tag_has_language(const char *tag, char first, char second)
{
    if (tag == NULL || fin_voice_ascii_lower(tag[0]) != first ||
        fin_voice_ascii_lower(tag[1]) != second) {
        return 0;
    }
    return tag[2] == '\0' || tag[2] == '-' || tag[2] == '_';
}

const FinVoiceCodebookView *fin_voice_codebook_en_get(void)
{
    for (size_t index = 0u; index < (size_t)FIN_VOICE_CODEBOOK_LANGUAGE_COUNT;
         ++index) {
        const FinVoiceCodebookView *view = &fin_voice_codebooks[index];
        if (fin_voice_tag_has_language(view->language, 'e', 'n')) {
            return view;
        }
    }
    return NULL;
}
