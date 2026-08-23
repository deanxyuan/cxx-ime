// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_TSF_UI_PRESENTATION_BATCH_H_
#define CXXIME_TSF_UI_PRESENTATION_BATCH_H_

class TextService;

namespace cxxime_tsf {

class UiPresentationBatch final {
public:
    explicit UiPresentationBatch(TextService& service);
    ~UiPresentationBatch();

    UiPresentationBatch(const UiPresentationBatch&) = delete;
    UiPresentationBatch& operator=(const UiPresentationBatch&) = delete;

private:
    TextService& service_;
};

} // namespace cxxime_tsf

#endif // CXXIME_TSF_UI_PRESENTATION_BATCH_H_
