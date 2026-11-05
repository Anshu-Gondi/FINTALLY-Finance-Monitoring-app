/* FinTally Voice codebook registry and generated-data integration tests. */

#include "codebooks/codebook_en.h"
#include "codebooks/codebook_fr.h"
#include "codebooks/codebook_hi.h"
#include "codebooks/codebook_zh.h"
#include "codebooks/codebook_registry.h"
#include "vq_quantize.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static unsigned g_checks = 0u;
static unsigned g_failures = 0u;

#define CHECK(condition, message)                                             \
    do {                                                                      \
        ++g_checks;                                                           \
        if (!(condition)) {                                                   \
            (void)fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,   \
                          (message));                                        \
            ++g_failures;                                                     \
        }                                                                     \
    } while (0)

static void check_codebook(const char *tag,
                           const char *expected_base,
                           const FinVoiceCodebookView *direct_view)
{
    FinVoiceCodebookRegistryEntry entry = {0};
    const FinVoiceCodebookRegistryStatus status =
        fin_voice_codebook_registry_lookup(tag, &entry);

    CHECK(status == FIN_VOICE_CODEBOOK_REGISTRY_OK,
          "supported language lookup must succeed");
    CHECK(entry.codebook != NULL, "lookup must return a codebook view");
    CHECK(entry.codebook == direct_view,
          "registry and language-specific accessor must return the same view");
    CHECK(entry.codebook->language != NULL,
          "codebook language metadata must exist");
    CHECK(entry.codebook->language[0] == expected_base[0] &&
              entry.codebook->language[1] == expected_base[1],
          "codebook metadata must match requested base language");
    CHECK(entry.codebook->centroids != NULL,
          "generated centroid storage must not be NULL");
    CHECK(entry.codebook->dimensions == FIN_VOICE_FEATURE_DIMENSION,
          "codebook dimensionality must match generated schema");
    CHECK(entry.codebook->centroid_count > 0u,
          "codebook must contain at least one centroid");
    CHECK(entry.codebook->training_samples > 0u,
          "codebook training sample count must be nonzero");
    CHECK(entry.normalization_mean == fin_voice_feature_normalization_mean,
          "mean normalization stats must be shared generated storage");
    CHECK(entry.normalization_inv_std == fin_voice_feature_normalization_inv_std,
          "inverse-standard-deviation stats must be shared generated storage");

    if (entry.codebook != NULL) {
        const FinVqCodebook vq_codebook = {
            .centroids = entry.codebook->centroids,
            .centroid_count = (size_t)entry.codebook->centroid_count,
            .dimension = (size_t)entry.codebook->dimensions,
        };
        CHECK(fin_vq_validate_codebook(&vq_codebook) == FIN_VQ_OK,
              "registry codebook must pass full VQ validation");
    }
}

static void check_normalization_metadata(void)
{
    for (size_t index = 0u; index < (size_t)FIN_VOICE_FEATURE_DIMENSION;
         ++index) {
        CHECK(isfinite(fin_voice_feature_normalization_mean[index]),
              "normalization mean must be finite");
        CHECK(isfinite(fin_voice_feature_normalization_inv_std[index]),
              "normalization inverse standard deviation must be finite");
        CHECK(fin_voice_feature_normalization_inv_std[index] > 0.0f,
              "normalization inverse standard deviation must be positive");
    }
}

static void check_invalid_tag(const char *tag)
{
    FinVoiceCodebookRegistryEntry entry = {
        .codebook = &fin_voice_codebooks[0],
        .normalization_mean = fin_voice_feature_normalization_mean,
        .normalization_inv_std = fin_voice_feature_normalization_inv_std,
    };
    const FinVoiceCodebookRegistryStatus status =
        fin_voice_codebook_registry_lookup(tag, &entry);
    CHECK(status == FIN_VOICE_CODEBOOK_REGISTRY_ERROR_INVALID_LANGUAGE_TAG,
          "malformed language tag must be rejected");
    CHECK(entry.codebook == NULL && entry.normalization_mean == NULL &&
              entry.normalization_inv_std == NULL,
          "failed lookup must clear output entry");
}

int main(void)
{
    (void)printf("FinTally Voice codebook registry test suite\n");

    check_codebook("en", "en", fin_voice_codebook_en_get());
    check_codebook("EN-us", "en", fin_voice_codebook_en_get());
    check_codebook("fr_FR", "fr", fin_voice_codebook_fr_get());
    check_codebook("FR", "fr", fin_voice_codebook_fr_get());
    check_codebook("hi-IN", "hi", fin_voice_codebook_hi_get());
    check_codebook("HI", "hi", fin_voice_codebook_hi_get());

    const FinVoiceCodebookView *zh_view = fin_voice_codebook_zh_get();
    FinVoiceCodebookRegistryEntry zh_entry = {0};
    const FinVoiceCodebookRegistryStatus zh_status =
        fin_voice_codebook_registry_lookup("zh-CN", &zh_entry);
    if (zh_view == NULL) {
        CHECK(zh_status == FIN_VOICE_CODEBOOK_REGISTRY_ERROR_UNSUPPORTED_LANGUAGE,
              "Chinese must be unsupported until generated data exists");
        CHECK(zh_entry.codebook == NULL,
              "unsupported Chinese lookup must not substitute another language");
    } else {
        CHECK(zh_status == FIN_VOICE_CODEBOOK_REGISTRY_OK,
              "Chinese lookup must succeed when real generated data exists");
        CHECK(zh_entry.codebook == zh_view,
              "Chinese registry must point to generated Chinese codebook");
        CHECK(zh_entry.codebook->dimensions == FIN_VOICE_FEATURE_DIMENSION,
              "Chinese dimensions must match generated schema");
    }

    CHECK(fin_voice_codebook_registry_supported_count() ==
              (size_t)(zh_view == NULL ? 3u : 4u),
          "supported count must reflect real language data only");
    check_normalization_metadata();

    FinVoiceCodebookRegistryEntry entry = {0};
    CHECK(fin_voice_codebook_registry_lookup(NULL, &entry) ==
              FIN_VOICE_CODEBOOK_REGISTRY_ERROR_INVALID_ARGUMENT,
          "NULL language tag must be rejected");
    CHECK(entry.codebook == NULL && entry.normalization_mean == NULL &&
              entry.normalization_inv_std == NULL,
          "NULL language tag must leave a cleared output entry");
    CHECK(fin_voice_codebook_registry_lookup("en", NULL) ==
              FIN_VOICE_CODEBOOK_REGISTRY_ERROR_INVALID_ARGUMENT,
          "NULL output pointer must be rejected");

    check_invalid_tag("");
    check_invalid_tag("e");
    check_invalid_tag("enx");
    check_invalid_tag("-en");
    check_invalid_tag("en-");
    check_invalid_tag("en--US");
    check_invalid_tag("en__US");
    check_invalid_tag("eñ");

    CHECK(fin_voice_codebook_registry_lookup("es", &entry) ==
              FIN_VOICE_CODEBOOK_REGISTRY_ERROR_UNSUPPORTED_LANGUAGE,
          "well-formed but unavailable Spanish codebook must be unsupported");
    CHECK(entry.codebook == NULL,
          "unsupported language must not return a fallback codebook");
    CHECK(fin_voice_codebook_registry_lookup("fi-FI", &entry) ==
              FIN_VOICE_CODEBOOK_REGISTRY_ERROR_UNSUPPORTED_LANGUAGE,
          "a language sharing the first letter with a supported one must not fall back");
    CHECK(entry.codebook == NULL,
          "unsupported Finnish lookup must not return the French codebook");
    CHECK(strcmp(fin_voice_codebook_registry_status_name(
                     FIN_VOICE_CODEBOOK_REGISTRY_OK), "ok") == 0,
          "success status must have stable name");
    CHECK(strcmp(fin_voice_codebook_registry_status_name(
                     FIN_VOICE_CODEBOOK_REGISTRY_ERROR_UNSUPPORTED_LANGUAGE),
                 "unsupported_language") == 0,
          "unsupported status must have stable name");
    CHECK(strcmp(fin_voice_codebook_registry_status_name(
                     (FinVoiceCodebookRegistryStatus)999),
                 "unknown_status") == 0,
          "unknown status must have a safe name");

    if (g_failures != 0u) {
        (void)fprintf(stderr, "FAIL: %u/%u checks failed\n", g_failures,
                      g_checks);
        return 1;
    }

    (void)printf("PASS: %u checks; generated centroid storage was not copied\n",
                 g_checks);
    return 0;
}
