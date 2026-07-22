#ifndef CACHE_H
#define CACHE_H

#include <set>
#include <map>
#include <optional>
#include "z3++.h"
#include "llvm/IR/Function.h"

using param_list_ty = std::vector<int>;
// currently, assume both return values and arguments are integers
using function_cached_ty = std::map<param_list_ty, int>;
class Cache {
    public:
        Cache() = default;
        ~Cache() = default;
        Cache(const Cache&) = delete;
        Cache& operator=(const Cache&) = delete;
        Cache(Cache&&) = delete;
        Cache& operator=(Cache&&) = delete;
        std::optional<int> get_func_value(llvm::Function* func, const param_list_ty& args);

        void cache_func_value(llvm::Function* func, const param_list_ty& args, int value);

        bool is_visited(llvm::Function* func) const;

        void mark_visited(llvm::Function* func);

        void clear();

    private:
        std::map<llvm::Function*, function_cached_ty> cache;

        // store functions that have been summarized (no matter if the summarization is successful or not)
        std::set<llvm::Function*> visited_funcs;
};

#endif
