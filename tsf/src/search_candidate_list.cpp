// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "search_candidate_list.h"

#include <algorithm>
#include <new>
#include <utility>

namespace {

class SearchCandidateString final : public ITfCandidateString {
public:
    SearchCandidateString(std::wstring text, ULONG index)
        : text_(std::move(text))
        , index_(index) {}

    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override {
        if (!ppvObj) {
            return E_INVALIDARG;
        }
        *ppvObj = nullptr;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfCandidateString)) {
            *ppvObj = static_cast<ITfCandidateString*>(this);
        }
        if (!*ppvObj) {
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }

    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&ref_count_); }

    STDMETHODIMP_(ULONG) Release() override {
        const LONG value = InterlockedDecrement(&ref_count_);
        if (value == 0) {
            delete this;
        }
        return value;
    }

    STDMETHODIMP GetString(BSTR* text) override {
        if (!text) {
            return E_INVALIDARG;
        }
        *text = SysAllocStringLen(text_.data(), static_cast<UINT>(text_.size()));
        return *text ? S_OK : E_OUTOFMEMORY;
    }

    STDMETHODIMP GetIndex(ULONG* index) override {
        if (!index) {
            return E_INVALIDARG;
        }
        *index = index_;
        return S_OK;
    }

private:
    ~SearchCandidateString() = default;

    LONG ref_count_ = 1;
    std::wstring text_;
    ULONG index_ = 0;
};

class SearchCandidateEnumerator final : public IEnumTfCandidates {
public:
    explicit SearchCandidateEnumerator(const std::vector<std::wstring>& candidates)
        : candidates_(candidates) {}

    SearchCandidateEnumerator(const SearchCandidateEnumerator& other)
        : candidates_(other.candidates_)
        , position_(other.position_) {}

    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObj) override {
        if (!ppvObj) {
            return E_INVALIDARG;
        }
        *ppvObj = nullptr;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IEnumTfCandidates)) {
            *ppvObj = static_cast<IEnumTfCandidates*>(this);
        }
        if (!*ppvObj) {
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }

    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&ref_count_); }

    STDMETHODIMP_(ULONG) Release() override {
        const LONG value = InterlockedDecrement(&ref_count_);
        if (value == 0) {
            delete this;
        }
        return value;
    }

    STDMETHODIMP Clone(IEnumTfCandidates** result) override {
        if (!result) {
            return E_INVALIDARG;
        }
        *result = new (std::nothrow) SearchCandidateEnumerator(*this);
        if (!*result) {
            return E_OUTOFMEMORY;
        }
        return S_OK;
    }

    STDMETHODIMP Next(ULONG count, ITfCandidateString** items, ULONG* fetched) override {
        if (!items || !fetched) {
            return E_INVALIDARG;
        }
        *fetched = 0;
        while (*fetched < count && position_ < candidates_.size()) {
            auto* item = new (std::nothrow)
                SearchCandidateString(candidates_[position_], static_cast<ULONG>(position_));
            if (!item) {
                return E_OUTOFMEMORY;
            }
            items[*fetched] = item;
            ++position_;
            ++*fetched;
        }
        return *fetched == count ? S_OK : S_FALSE;
    }

    STDMETHODIMP Reset() override {
        position_ = 0;
        return S_OK;
    }

    STDMETHODIMP Skip(ULONG count) override {
        position_ = (std::min)(candidates_.size(), position_ + static_cast<size_t>(count));
        return position_ < candidates_.size() ? S_OK : S_FALSE;
    }

private:
    ~SearchCandidateEnumerator() = default;

    LONG ref_count_ = 1;
    std::vector<std::wstring> candidates_;
    size_t position_ = 0;
};

} // namespace

SearchCandidateList::SearchCandidateList(std::vector<std::wstring> candidates)
    : candidates_(std::move(candidates)) {}

STDMETHODIMP SearchCandidateList::QueryInterface(REFIID riid, void** ppvObj) {
    if (!ppvObj) {
        return E_INVALIDARG;
    }
    *ppvObj = nullptr;
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_ITfCandidateList)) {
        *ppvObj = static_cast<ITfCandidateList*>(this);
    }
    if (!*ppvObj) {
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) SearchCandidateList::AddRef() { return InterlockedIncrement(&ref_count_); }

STDMETHODIMP_(ULONG) SearchCandidateList::Release() {
    const LONG value = InterlockedDecrement(&ref_count_);
    if (value == 0) {
        delete this;
    }
    return value;
}

STDMETHODIMP SearchCandidateList::EnumCandidates(IEnumTfCandidates** result) {
    if (!result) {
        return E_INVALIDARG;
    }
    *result = new (std::nothrow) SearchCandidateEnumerator(candidates_);
    if (!*result) {
        return E_OUTOFMEMORY;
    }
    return S_OK;
}

STDMETHODIMP SearchCandidateList::GetCandidate(ULONG index, ITfCandidateString** result) {
    if (!result) {
        return E_INVALIDARG;
    }
    *result = nullptr;
    if (index >= candidates_.size()) {
        return E_INVALIDARG;
    }
    *result = new (std::nothrow) SearchCandidateString(candidates_[index], index);
    return *result ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP SearchCandidateList::GetCandidateNum(ULONG* count) {
    if (!count) {
        return E_INVALIDARG;
    }
    *count = static_cast<ULONG>(candidates_.size());
    return S_OK;
}

STDMETHODIMP SearchCandidateList::SetResult(ULONG index, TfCandidateResult result) {
    UNREFERENCED_PARAMETER(result);
    return index < candidates_.size() ? E_NOTIMPL : E_INVALIDARG;
}
