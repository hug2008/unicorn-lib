#pragma once

#include "unicorn/core.hpp"

namespace RS {

    namespace Unicorn {

        RS_ENUM(NormalizationForm, int, 1, NFC, NFD, NFKC, NFKD)

        U8string normalize(const U8string& src, NormalizationForm form);
        void normalize_in(U8string& src, NormalizationForm form);

    }

}
