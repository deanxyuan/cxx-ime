// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_CUSTOM_PHRASE_TRANSLATION_H_
#define CXXIME_CUSTOM_PHRASE_TRANSLATION_H_

#include <cxxime/translation_result.h>

namespace cxxime {

class CustomPhrases;
class ITranslator;

TranslationResult translate_with_custom_phrases(ITranslator& translator,
                                                const TranslationRequest& request,
                                                const CustomPhrases* phrases,
                                                std::size_t converted_text_bytes);

} // namespace cxxime

#endif // CXXIME_CUSTOM_PHRASE_TRANSLATION_H_
