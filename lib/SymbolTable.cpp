#include "SymbolTable.h"

using namespace ari_exe;

template<typename value_t>
void
SymbolTable<value_t>::insert_or_assign(llvm::Value* symbol, value_t value) {
    variables.insert_or_assign(symbol, value);
}

template<typename value_t>
void
SymbolTable<value_t>::update_variable(llvm::Value* symbol, value_t value) {
    auto it = variables.find(symbol);
    if (it != variables.end()) {
        it->second = value;
    } else {
        llvm::errs() << "Variable not found in symbol table: " << *symbol << "\n";
        assert(false);
    }
}

template<typename value_t>
std::optional<value_t>
SymbolTable<value_t>::get_value(llvm::Value* symbol) const {
    auto it = variables.find(symbol);
    if (it != variables.end()) {
        return it->second;
    }
    return std::nullopt;
}