// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#ifndef CXXIME_COMPOSITION_PRESENTATION_H_
#define CXXIME_COMPOSITION_PRESENTATION_H_

#include <cstddef>
#include <string>

#include <cxxime/composition_state.h>

namespace cxxime {

struct CompositionPresentation {
    std::string logical_preedit;
    std::string preview_preedit;
    std::size_t cursor_bytes = 0;
    std::size_t converted_prefix_bytes = 0;

    bool fits_transport() const;
};

CompositionPresentation derive_composition_presentation(const CompositionState& state);

} // namespace cxxime

#endif // CXXIME_COMPOSITION_PRESENTATION_H_
