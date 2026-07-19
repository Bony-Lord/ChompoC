#include "symbol.h"

#include <functional>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace {
    struct TransparentStringHash {
        using is_transparent = void;

        std::size_t operator()(std::string_view value) const noexcept {
            return std::hash<std::string_view>{}(value);
        }

        std::size_t operator()(const std::string &value) const noexcept {
            return (*this)(std::string_view(value));
        }
    };

    using SymbolMap =
        std::unordered_map<std::string, SymbolId, TransparentStringHash, std::equal_to<>>;
}

SymbolId intern_symbol(std::string_view name) {
    static SymbolMap symbols;

    if (const auto iterator = symbols.find(name); iterator != symbols.end())
        return iterator->second;

    if (symbols.size() >= static_cast<std::size_t>(std::numeric_limits<SymbolId>::max() - 1))
        throw std::overflow_error("symbol table limit reached");

    const SymbolId symbol = static_cast<SymbolId>(symbols.size() + 1);
    symbols.emplace(std::string(name), symbol);
    return symbol;
}
