#pragma once
#include <string>

namespace wam {

// Turns the small HTML fragments CurseForge serves as changelogs into plain
// text: block tags and <br> become line breaks, <li> becomes a "- " bullet,
// every other tag is dropped, and the common entities are decoded. Not a
// general HTML parser; enough to read a changelog.
std::string htmlToPlainText(const std::string& html);

} // namespace wam
