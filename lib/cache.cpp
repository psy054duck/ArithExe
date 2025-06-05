#include "cache.h"

Cache* Cache::instance = new Cache();

std::optional<int> 
Cache::get_func_value(llvm::Function* func, const param_list_ty& args) {
    auto it = cache.find(func);
    if (it != cache.end()) {
        auto& func_cache = it->second;
        auto arg_it = func_cache.find(args);
        if (arg_it != func_cache.end()) {
            return arg_it->second;
        }
    }
    return std::nullopt;
}

void
Cache::cache_func_value(llvm::Function* func, const param_list_ty& args, int value) {
    auto& func_cache = cache[func];
    func_cache.insert_or_assign(args, value);
}

bool
Cache::is_visited(llvm::Function* func) const {
    return visited_funcs.find(func) != visited_funcs.end();
}

void
Cache::mark_visited(llvm::Function* func) {
    visited_funcs.insert(func);
}