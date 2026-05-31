#pragma once
#include <string>

namespace MarkdownPipeline {
    // Convert UTF-8 markdown to a complete HTML document (UTF-16 wstring).
    // The returned string is ready to pass directly to IWebBrowser2::NavigateToString.
    // Returns empty wstring on allocation failure.
    std::wstring Convert(const std::string& markdown);
}
