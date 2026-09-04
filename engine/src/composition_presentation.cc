// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/composition_presentation.h>

#include <cxxime/input_limits.h>

namespace cxxime {

bool CompositionPresentation::fits_transport() const {
    return logical_preedit.size() < kCandidateTextCapacity &&
           preview_preedit.size() < kCandidateTextCapacity &&
           cursor_bytes <= logical_preedit.size() &&
           converted_prefix_bytes <= logical_preedit.size();
}

CompositionPresentation derive_composition_presentation(const CompositionState& state) {
    CompositionPresentation presentation;
    for (const auto& segment : state.converted_segments()) {
        presentation.logical_preedit += segment.text;
    }
    presentation.converted_prefix_bytes = presentation.logical_preedit.size();
    presentation.logical_preedit += state.active().input;
    presentation.preview_preedit = presentation.logical_preedit;
    presentation.cursor_bytes = presentation.converted_prefix_bytes + state.active().cursor;
    return presentation;
}

} // namespace cxxime
