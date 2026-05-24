// Copyright (c) 2026 CxxIME Contributors. Apache License 2.0.

#include <cxxime/engine.h>

namespace cxxime {

// Self-contained: owns all resources (tests/tools).
bool Engine::initialize(const std::string& dict_path, const std::string& config_path) {
    if (!owned_dict_.open(dict_path))
        return false;

    if (!config_path.empty()) {
        owned_config_.load(config_path);
    }

    std::string sp_path = derive_spellings_path(dict_path);
    if (!sp_path.empty() && owned_spellings_.load(sp_path) && owned_spellings_.has_spellings()) {
        owned_syllabifier_ = std::make_unique<Syllabifier>(owned_spellings_);
    }

    return initialize(owned_dict_, owned_spellings_,
                      owned_syllabifier_.get(), owned_config_);
}

// Shared-resource: references pre-loaded data (server sessions).
bool Engine::initialize(Dict& dict, SpellingsIndex& spellings,
                        Syllabifier* syllabifier, const Config& config) {
    dict_ = &dict;
    spellings_ = &spellings;
    syllabifier_ = syllabifier;
    config_ = &config;

    translator_.set_dict(dict_);
    if (syllabifier_) {
        translator_.set_syllabifier(syllabifier_);
    }

    init_per_session(config);
    return true;
}

void Engine::init_per_session(const Config& config) {
    ascii_composer_.load_config(config);
}

void Engine::finalize() {
    if (dict_ == &owned_dict_) {
        owned_dict_.close();
    }
    context_.reset();
}

ProcessResult Engine::process_key(const KeyEvent& event) {
    // Let AsciiComposer track modifier key state (may toggle ascii_mode)
    ascii_composer_.process_key(event.keycode, event.is_key_up, context_);

    // If in ASCII mode, handle letters/space directly
    if (ascii_composer_.is_ascii_mode() && !event.is_key_up) {
        uint32_t vk = event.keycode;

        // Letter keys (A-Z): commit as single ASCII char
        if (vk >= 'A' && vk <= 'Z') {
            context_.committed_text = std::string(1, static_cast<char>(vk - 'A' + 'a'));
            if (ascii_composer_.is_temporary_ascii()) {
                ascii_composer_.set_ascii_mode(false);
            }
            return ProcessResult::COMMITTED;
        }

        // Space: commit a space
        if (vk == 0x20) {  // VK_SPACE
            context_.committed_text = " ";
            return ProcessResult::COMMITTED;
        }

        // Enter: commit pending text and newline
        if (vk == 0x0D) {  // VK_RETURN
            context_.committed_text = "\r\n";
            if (ascii_composer_.is_temporary_ascii()) {
                ascii_composer_.set_ascii_mode(false);
            }
            return ProcessResult::COMMITTED;
        }

        // Other keys: reject (pass through to app)
        return ProcessResult::REJECTED;
    }

    auto result = processor_.process_key(event, context_);

    // Auto-restore from temporary inline_ascii when composition ends
    if (result == ProcessResult::COMMITTED && ascii_composer_.is_temporary_ascii()) {
        ascii_composer_.set_ascii_mode(false);
    }

    // After processing, update candidates if still composing
    if (result == ProcessResult::ACCEPTED && context_.is_composing()) {
        auto page = translator_.translate(context_.pinyin_buffer, context_.page_index, config_->page_size);
        context_.update_candidates(std::move(page));
    }

    // If committed, update user frequency
    if (result == ProcessResult::COMMITTED && !context_.committed_text.empty()) {
        std::string code = context_.pinyin_buffer;
        if (code.empty()) {
            code = dict_->reverse_lookup(context_.committed_text);
        }
        dict_->update_frequency(context_.committed_text, code);
    }

    return result;
}

const Context& Engine::context() const {
    return context_;
}

Context& Engine::context() {
    return context_;
}

bool Engine::select_candidate(int index) {
    if (index < 0 || index >= (int)context_.candidates.candidates.size())
        return false;

    context_.candidates.highlighted = index;
    context_.committed_text = context_.candidates.candidates[index].text;

    std::string code = context_.pinyin_buffer;
    if (code.empty())
        code = dict_->reverse_lookup(context_.committed_text);
    if (!code.empty())
        dict_->update_frequency(context_.committed_text, code);

    return true;
}

std::string Engine::get_commit_text() {
    std::string text = context_.committed_text;
    context_.pinyin_buffer.clear();
    context_.committed_text.clear();
    context_.candidates = {};
    context_.page_index = 0;
    return text;
}

void Engine::clear() {
    context_.reset();
}

std::string Engine::derive_spellings_path(const std::string& dict_path) {
    // pinyin.dict.bin → pinyin.spellings.bin
    // pinyin.dict.db  → pinyin.spellings.bin
    static const char kDictBinExt[] = ".dict.bin";
    static const char kDictDbExt[] = ".dict.db";
    static const char kSpellingsExt[] = ".spellings.bin";

    std::string path = dict_path;
    auto replace_ext = [&](const char* from, const char* to) {
        size_t pos = path.rfind(from);
        if (pos != std::string::npos && pos + strlen(from) == path.size()) {
            path.replace(pos, strlen(from), to);
            return true;
        }
        return false;
    };

    if (replace_ext(kDictBinExt, kSpellingsExt))
        return path;
    if (replace_ext(kDictDbExt, kSpellingsExt))
        return path;

    // Unknown extension — append .spellings.bin
    return path + kSpellingsExt;
}

} // namespace cxxime
