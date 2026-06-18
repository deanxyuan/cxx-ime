// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_WUBI_TRANSLATOR_H_
#define CXXIME_WUBI_TRANSLATOR_H_

#include <cxxime/translator.h>

namespace cxxime {

class WubiTranslator : public ITranslator {
public:
    void set_dict(Dict* dict);

    CandidatePage translate(const std::string& code, int page_index = 0, int page_size = 9,
                            QueryTrace* trace = nullptr, const QueryBudget* budget = nullptr,
                            QueryScratch* scratch = nullptr) override;

private:
    Dict* dict_ = nullptr;
};

} // namespace cxxime

#endif // CXXIME_WUBI_TRANSLATOR_H_
