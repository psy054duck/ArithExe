//------------------------------- SymbolTable.h -------------------------------
//
// This file declares the SymbolTable class, which is used to manage the
// symbolic variables and their values during the symbolic execution of a program.
//
//-------------------------------------------------------------------
#ifndef SYMBOLTABLE_H
#define SYMBOLTABLE_H

#include "llvm/IR/Instruction.h"
#include "llvm/IR/Constants.h"

#include "z3++.h"

#include <map>
#include <optional>

namespace ari_exe {
    template<typename value_t>
    class SymbolTable {
        public:
            SymbolTable() = default;
            SymbolTable(const SymbolTable&) = default;
            SymbolTable& operator=(const SymbolTable&) = default;

            // insert or assign a variable to the symbol table
            void insert_or_assign(llvm::Value* symbol, value_t value);

            // update the value of a variable
            void update_variable(llvm::Value* symbol, value_t value);

            // get the value of a variable
            std::optional<value_t> get_value(llvm::Value* symbol) const;

            void print() const;

        private:
            std::map<llvm::Value*, value_t> variables;
    };

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

    template<typename value_t>
    void SymbolTable<value_t>::print() const {
        for (const auto& [key, value] : variables) {
            llvm::errs() << key->getName() << " = " << value.to_string() << "\n";
        }
    }
}

#endif