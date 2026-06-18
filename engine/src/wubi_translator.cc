// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/wubi_translator.h>
#include <algorithm>

namespace cxxime {

void WubiTranslator::set_dict(Dict* dict) {
    dict_ = dict;
}

CandidatePage WubiTranslator::translate(const std::string& code, int page_index,
                                         int page_size, QueryTrace* trace,
                                         const QueryBudget* budget,
                                         QueryScratch* scratch) {
    if (!dict_ || code.empty()) {
        return {};
    }

    // Dict::lookup() 已经返回按 composite frequency 排序（精确匹配优先、
    // 短码优先、频率降序）且按 text 去重的结果。
    int limit = (page_index + 1) * page_size + 1;
    std::vector<Candidate> results;
    if (budget) {
        results = dict_->lookup(code, limit, *budget, trace);
    } else {
        results = dict_->lookup(code, limit);
    }

    if (results.empty()) {
        return {};
    }

    // 分页
    CandidatePage page;
    page.page_size = page_size;
    page.total_count = (int)results.size();

    int start = page_index * page_size;
    if (start >= (int)results.size()) {
        return page;
    }

    int end = std::min(start + page_size, (int)results.size());
    page.candidates.assign(results.begin() + start, results.begin() + end);
    page.highlighted = 0;

    return page;
}

} // namespace cxxime
