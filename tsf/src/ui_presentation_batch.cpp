// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include "ui_presentation_batch.h"

#include "text_service.h"

namespace cxxime_tsf {

UiPresentationBatch::UiPresentationBatch(TextService& service)
    : service_(service) {
    ++service_._uiPresentationBatchDepth;
}

UiPresentationBatch::~UiPresentationBatch() {
    if (--service_._uiPresentationBatchDepth == 0 &&
        service_._uiPresentationPublishPending) {
        service_._uiPresentationPublishPending = false;
        service_._publish_ui_presentation();
    }
}

} // namespace cxxime_tsf
