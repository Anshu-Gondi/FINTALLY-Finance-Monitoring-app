#include "codebooks/codebook_registry.h"

#include "codebooks/codebook_en.h"
#include "codebooks/codebook_fr.h"
#include "codebooks/codebook_hi.h"
#include "codebooks/codebook_zh.h"

#include <math.h>

static char fin_voice_ascii_lower(char value)
{
    if (value >= 'A' && value <= 'Z') {
        return (char)(value + ('a' - 'A'));
    }
    return value;
}

static int fin_voice_ascii_alpha(char value)
{
    return (value >= 'A' && value <= 'Z') ||
           (value >= 'a' && value <= 'z');
}

static int fin_voice_ascii_digit(char value)
{
    return value >= '0' && value <= '9';
}

/* Validate a compact locale tag without locale APIs or heap allocation. */
static int fin_voice_language_tag_valid(const char *tag)
{
    if (tag == NULL || !fin_voice_ascii_alpha(tag[0]) ||
        !fin_voice_ascii_alpha(tag[1])) {
        return 0;
    }
    if (tag[2] == '\0') {
        return 1;
    }
    if (tag[2] != '-' && tag[2] != '_') {
        return 0;
    }

    int needs_alphanumeric = 1;
    for (size_t index = 3u; tag[index] != '\0'; ++index) {
        const char value = tag[index];
        if (value == '-' || value == '_') {
            if (needs_alphanumeric) {
                return 0;
            }
            needs_alphanumeric = 1;
        } else {
            if (!fin_voice_ascii_alpha(value) && !fin_voice_ascii_digit(value)) {
                return 0;
            }
            needs_alphanumeric = 0;
        }
    }
    return !needs_alphanumeric;
}

static int fin_voice_normalization_valid(void)
{
    for (size_t index = 0u; index < (size_t)FIN_VOICE_FEATURE_DIMENSION;
         ++index) {
        const float mean = fin_voice_feature_normalization_mean[index];
        const float inverse_std = fin_voice_feature_normalization_inv_std[index];
        if (!isfinite(mean) || !isfinite(inverse_std) || inverse_std <= 0.0f) {
            return 0;
        }
    }
    return 1;
}

static int fin_voice_codebook_metadata_valid(const FinVoiceCodebookView *view)
{
    return view != NULL && view->language != NULL && view->centroids != NULL &&
           view->dimensions == FIN_VOICE_FEATURE_DIMENSION &&
           view->centroid_count > 0u && view->training_samples > 0u;
}

static const FinVoiceCodebookView *fin_voice_codebook_for_language(
    char first,
    char second)
{
    switch (first) {
    case 'e':
        return second == 'n' ? fin_voice_codebook_en_get() : NULL;
    case 'f':
        return second == 'r' ? fin_voice_codebook_fr_get() : NULL;
    case 'h':
        return second == 'i' ? fin_voice_codebook_hi_get() : NULL;
    case 'z':
        return second == 'h' ? fin_voice_codebook_zh_get() : NULL;
    default:
        return NULL;
    }
}

FinVoiceCodebookRegistryStatus fin_voice_codebook_registry_lookup(
    const char *language_tag,
    FinVoiceCodebookRegistryEntry *out_entry)
{
    if (out_entry != NULL) {
        out_entry->codebook = NULL;
        out_entry->normalization_mean = NULL;
        out_entry->normalization_inv_std = NULL;
    }

    if (language_tag == NULL || out_entry == NULL) {
        return FIN_VOICE_CODEBOOK_REGISTRY_ERROR_INVALID_ARGUMENT;
    }
    if (!fin_voice_language_tag_valid(language_tag)) {
        return FIN_VOICE_CODEBOOK_REGISTRY_ERROR_INVALID_LANGUAGE_TAG;
    }

    const char first = fin_voice_ascii_lower(language_tag[0]);
    const char second = fin_voice_ascii_lower(language_tag[1]);
    const FinVoiceCodebookView *view =
        fin_voice_codebook_for_language(first, second);
    if (view == NULL) {
        return FIN_VOICE_CODEBOOK_REGISTRY_ERROR_UNSUPPORTED_LANGUAGE;
    }
    if (!fin_voice_codebook_metadata_valid(view)) {
        return FIN_VOICE_CODEBOOK_REGISTRY_ERROR_INVALID_CODEBOOK;
    }
    if (!fin_voice_normalization_valid()) {
        return FIN_VOICE_CODEBOOK_REGISTRY_ERROR_INVALID_NORMALIZATION;
    }

    out_entry->codebook = view;
    out_entry->normalization_mean = fin_voice_feature_normalization_mean;
    out_entry->normalization_inv_std = fin_voice_feature_normalization_inv_std;
    return FIN_VOICE_CODEBOOK_REGISTRY_OK;
}

size_t fin_voice_codebook_registry_supported_count(void)
{
    size_t count = 0u;
    count += fin_voice_codebook_en_get() != NULL ? 1u : 0u;
    count += fin_voice_codebook_fr_get() != NULL ? 1u : 0u;
    count += fin_voice_codebook_hi_get() != NULL ? 1u : 0u;
    count += fin_voice_codebook_zh_get() != NULL ? 1u : 0u;
    return count;
}

const char *fin_voice_codebook_registry_status_name(
    FinVoiceCodebookRegistryStatus status)
{
    switch (status) {
    case FIN_VOICE_CODEBOOK_REGISTRY_OK:
        return "ok";
    case FIN_VOICE_CODEBOOK_REGISTRY_ERROR_INVALID_ARGUMENT:
        return "invalid_argument";
    case FIN_VOICE_CODEBOOK_REGISTRY_ERROR_INVALID_LANGUAGE_TAG:
        return "invalid_language_tag";
    case FIN_VOICE_CODEBOOK_REGISTRY_ERROR_UNSUPPORTED_LANGUAGE:
        return "unsupported_language";
    case FIN_VOICE_CODEBOOK_REGISTRY_ERROR_INVALID_CODEBOOK:
        return "invalid_codebook";
    case FIN_VOICE_CODEBOOK_REGISTRY_ERROR_INVALID_NORMALIZATION:
        return "invalid_normalization";
    default:
        return "unknown_status";
    }
}
