#include "MarkdownPipeline.h"
#include "ThemeConstants.h"
#include "md4c/md4c-html.h"
#include <windows.h>
#include <string>

static void md4c_cb(const MD_CHAR* text, MD_SIZE size, void* userdata)
{
    static_cast<std::string*>(userdata)->append(text, size);
}

std::wstring MarkdownPipeline::Convert(const std::string& markdown)
{
    std::string body;
    body.reserve(markdown.size() * 3); // markdown expands significantly to HTML

    unsigned flags =
        MD_FLAG_TABLES         |
        MD_FLAG_STRIKETHROUGH  |
        MD_FLAG_PERMISSIVEURLAUTOLINKS;

    int ret = md_html(
        markdown.c_str(),
        static_cast<MD_SIZE>(markdown.size()),
        md4c_cb,
        &body,
        flags,
        0
    );
    // md4c returns non-zero only on severe internal error; partial output is still usable.
    (void)ret;

    // Assemble the complete HTML document
    std::string html;
    html.reserve(sizeof(kCSS) + body.size() + 128);
    html  = "<!DOCTYPE html>"
            "<html><head>"
            "<meta charset=\"UTF-8\">"
            "<meta http-equiv=\"X-UA-Compatible\" content=\"IE=edge\">"
            "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
            "<style>";
    html += kCSS;
    html += "</style></head><body>";
    html += body;
    html += "</body></html>";

    // Convert UTF-8 → UTF-16 for BSTR / NavigateToString
    int wlen = MultiByteToWideChar(CP_UTF8, 0, html.c_str(), static_cast<int>(html.size()), nullptr, 0);
    if (wlen <= 0) return {};

    std::wstring result(static_cast<size_t>(wlen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, html.c_str(), static_cast<int>(html.size()), &result[0], wlen);
    return result;
}
