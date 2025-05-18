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

        private:
            std::map<llvm::Value*, value_t> variables;
    };
}

#endif