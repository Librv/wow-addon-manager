#include "core/html_text.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace wam {

namespace {

void appendUtf8(std::string& out, unsigned long cp) {
    if (cp < 0x80) out += static_cast<char>(cp);
    else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp <= 0x10FFFF) {
        out += static_cast<char>(0xF0 | (cp >> 18)); out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// Decodes one entity starting at s[i] == '&'. Returns how many characters it
// consumed (0 if this is not an entity we know, so the '&' stays literal).
size_t decodeEntity(const std::string& s, size_t i, std::string& out) {
    const size_t semi = s.find(';', i);
    if (semi == std::string::npos || semi - i > 10) return 0;
    const std::string name = s.substr(i + 1, semi - i - 1);
    if (name == "amp") out += '&';
    else if (name == "lt") out += '<';
    else if (name == "gt") out += '>';
    else if (name == "quot") out += '"';
    else if (name == "apos" || name == "#39") out += '\'';
    else if (name == "nbsp") out += ' ';
    else if (name.size() > 1 && name[0] == '#') {
        const bool hex = name[1] == 'x' || name[1] == 'X';
        const std::string digits = name.substr(hex ? 2 : 1);
        if (digits.empty() || !std::all_of(digits.begin(), digits.end(),
                [&](unsigned char c) { return hex ? std::isxdigit(c) : std::isdigit(c); })) return 0;
        appendUtf8(out, std::strtoul(digits.c_str(), nullptr, hex ? 16 : 10));
    } else {
        return 0;
    }
    return semi - i + 1;
}

std::string tagName(const std::string& inner) {
    size_t i = 0;
    while (i < inner.size() && (inner[i] == '/' || std::isspace(static_cast<unsigned char>(inner[i])))) ++i;
    std::string name;
    while (i < inner.size() && std::isalnum(static_cast<unsigned char>(inner[i])))
        name += static_cast<char>(std::tolower(static_cast<unsigned char>(inner[i++])));
    return name;
}

} // namespace

std::string htmlToPlainText(const std::string& html) {
    std::string out;
    auto ensureNewline = [&] { if (!out.empty() && out.back() != '\n') out += '\n'; };

    for (size_t i = 0; i < html.size();) {
        const char c = html[i];
        if (c == '<') {
            if (html.compare(i, 4, "<!--") == 0) {
                const size_t end = html.find("-->", i + 4);
                i = end == std::string::npos ? html.size() : end + 3;
                continue;
            }
            const size_t close = html.find('>', i);
            if (close == std::string::npos) { out += c; ++i; continue; } // a stray '<'
            const std::string inner = html.substr(i + 1, close - i - 1);
            const std::string name = tagName(inner);
            const bool closing = !inner.empty() && inner[0] == '/';
            if (name == "br") out += '\n';
            else if (name == "li") { if (!closing) { ensureNewline(); out += "- "; } }
            else if (name == "p" || name == "div" || name == "ul" || name == "ol" || name == "tr" ||
                     (name.size() == 2 && name[0] == 'h' && std::isdigit(static_cast<unsigned char>(name[1]))))
                { if (closing && name != "ul" && name != "ol") out += "\n\n"; else ensureNewline(); }
            i = close + 1;
        } else if (c == '&') {
            const size_t used = decodeEntity(html, i, out);
            if (used == 0) { out += c; ++i; } else i += used;
        } else {
            out += c;
            ++i;
        }
    }

    // Tidy: no trailing spaces per line, at most one blank line in a row, trimmed ends.
    std::string tidy;
    size_t pos = 0;
    int blank = 0;
    while (pos <= out.size()) {
        size_t nl = out.find('\n', pos);
        std::string line = out.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) line.pop_back();
        if (line.empty()) { if (++blank <= 1) tidy += '\n'; }
        else { blank = 0; tidy += line; tidy += '\n'; }
        if (nl == std::string::npos) break;
        pos = nl + 1;
    }
    const size_t first = tidy.find_first_not_of("\n ");
    if (first == std::string::npos) return {};
    const size_t last = tidy.find_last_not_of("\n ");
    return tidy.substr(first, last - first + 1);
}

} // namespace wam
