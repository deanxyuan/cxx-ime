// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_TSF_SEARCH_CANDIDATE_LIST_H_
#define CXXIME_TSF_SEARCH_CANDIDATE_LIST_H_

#include "pch.h"

#include <string>
#include <vector>

class SearchCandidateList final : public ITfCandidateList {
public:
    explicit SearchCandidateList(std::vector<std::wstring> candidates);

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // ITfCandidateList
    STDMETHODIMP EnumCandidates(IEnumTfCandidates** ppEnum) override;
    STDMETHODIMP GetCandidate(ULONG index, ITfCandidateString** ppCand) override;
    STDMETHODIMP GetCandidateNum(ULONG* count) override;
    STDMETHODIMP SetResult(ULONG index, TfCandidateResult result) override;

private:
    ~SearchCandidateList() = default;

    LONG ref_count_ = 1;
    std::vector<std::wstring> candidates_;
};

#endif // CXXIME_TSF_SEARCH_CANDIDATE_LIST_H_
