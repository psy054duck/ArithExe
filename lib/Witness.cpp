#include "Witness.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <map>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <utility>
#include <vector>

#include "llvm/ADT/StringRef.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Support/SHA256.h"

namespace ari_exe {
namespace {

struct SourceLocation {
    unsigned line;
    unsigned column;
};

struct GhostVariable {
    std::string name;
    std::string type;
};

struct GhostUpdate {
    SourceLocation location;
    std::vector<std::pair<std::string, std::string>> assignments;
};

struct GeneratedLoopInvariant {
    SourceLocation location;
    std::string value;
};

struct GeneratedFunctionContract {
    SourceLocation location;
    std::string ensures;
};

struct CorrectnessCertificate {
    std::vector<GeneratedLoopInvariant> invariants;
    std::vector<GeneratedFunctionContract> contracts;
    std::vector<GhostVariable> ghost_variables;
    std::vector<GhostUpdate> ghost_updates;
};

bool is_error_function(llvm::StringRef name) {
    return name.contains("reach_error") || name == "__VERIFIER_error";
}

std::string yaml_quote(const std::string& value) {
    std::ostringstream out;
    out << '"';
    for (unsigned char ch : value) {
        switch (ch) {
        case '\\': out << "\\\\"; break;
        case '"': out << "\\\""; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (ch < 0x20) {
                out << "\\x" << std::hex << std::setw(2) << std::setfill('0')
                    << static_cast<unsigned>(ch) << std::dec;
            } else {
                out << static_cast<char>(ch);
            }
        }
    }
    out << '"';
    return out.str();
}

std::optional<std::string> sha256_file(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return std::nullopt;

    llvm::SHA256 hash;
    std::array<char, 8192> buffer{};
    while (input) {
        input.read(buffer.data(), buffer.size());
        const auto count = input.gcount();
        if (count > 0) {
            hash.update(llvm::StringRef(buffer.data(),
                                        static_cast<size_t>(count)));
        }
    }
    if (input.bad()) return std::nullopt;

    std::ostringstream result;
    result << std::hex << std::setfill('0');
    for (uint8_t byte : hash.final()) result << std::setw(2) << unsigned(byte);
    return result.str();
}

std::string make_uuid_v4() {
    std::array<uint8_t, 16> bytes{};
    std::random_device device;
    for (auto& byte : bytes) byte = static_cast<uint8_t>(device());
    bytes[6] = static_cast<uint8_t>((bytes[6] & 0x0f) | 0x40);
    bytes[8] = static_cast<uint8_t>((bytes[8] & 0x3f) | 0x80);

    std::ostringstream result;
    result << std::hex << std::setfill('0');
    for (size_t i = 0; i < bytes.size(); ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) result << '-';
        result << std::setw(2) << unsigned(bytes[i]);
    }
    return result.str();
}

std::string creation_time() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t value = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &value);
#else
    gmtime_r(&value, &utc);
#endif
    std::ostringstream result;
    result << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return result.str();
}

bool debug_file_matches(const llvm::DILocation& location,
                        const std::string& input_file) {
    const std::filesystem::path expected(input_file);
    const std::filesystem::path actual(location.getFilename().str());
    if (actual.empty()) return true;
    return actual == expected || actual.filename() == expected.filename();
}

std::optional<SourceLocation>
source_location(const llvm::Instruction& instruction,
                const std::string& input_file) {
    const llvm::DebugLoc& debug_location = instruction.getDebugLoc();
    if (!debug_location || !debug_file_matches(*debug_location, input_file)) {
        return std::nullopt;
    }
    if (debug_location.getLine() == 0) return std::nullopt;
    return SourceLocation{debug_location.getLine(), debug_location.getCol()};
}

std::vector<std::string> read_source_lines(const std::string& input_file) {
    std::ifstream source(input_file);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(source, line)) lines.push_back(line);
    return lines;
}

std::set<std::string>
source_identifiers(const std::vector<std::string>& lines) {
    std::set<std::string> identifiers;
    for (const std::string& line : lines) {
        for (size_t position = 0; position < line.size();) {
            const unsigned char ch = line[position];
            if (!std::isalpha(ch) && ch != '_') {
                ++position;
                continue;
            }
            const size_t start = position++;
            while (position < line.size()) {
                const unsigned char next = line[position];
                if (!std::isalnum(next) && next != '_') break;
                ++position;
            }
            identifiers.insert(line.substr(start, position - start));
        }
    }
    return identifiers;
}

std::string unique_identifier(std::string base,
                              std::set<std::string>& used_identifiers) {
    std::string candidate = base;
    unsigned suffix = 1;
    while (!used_identifiers.insert(candidate).second) {
        candidate = base + "_" + std::to_string(suffix++);
    }
    return candidate;
}

unsigned first_non_space_column(const std::string& line) {
    const size_t position = line.find_first_not_of(" \t\r");
    return position == std::string::npos
               ? 1U
               : static_cast<unsigned>(position + 1);
}

bool identifier_boundary(const std::string& line, size_t position,
                         size_t length) {
    const auto is_identifier = [](unsigned char ch) {
        return std::isalnum(ch) || ch == '_';
    };
    return (position == 0 || !is_identifier(line[position - 1])) &&
           (position + length == line.size() ||
            !is_identifier(line[position + length]));
}

std::optional<SourceLocation>
loop_keyword_location(const llvm::Loop& loop,
                      const std::string& input_file,
                      const std::vector<std::string>& lines) {
    std::optional<SourceLocation> debug;
    const llvm::DebugLoc start = loop.getStartLoc();
    if (start && debug_file_matches(*start, input_file) &&
        start.getLine() != 0) {
        debug = SourceLocation{start.getLine(), start.getCol()};
    } else {
        debug = source_location(*loop.getHeader()->getTerminator(),
                                input_file);
    }
    if (!debug || debug->line > lines.size()) return std::nullopt;

    const std::string& line = lines[debug->line - 1];
    std::optional<size_t> best;
    for (const std::string keyword : {"for", "while", "do"}) {
        size_t position = line.find(keyword);
        while (position != std::string::npos) {
            if (identifier_boundary(line, position, keyword.size()) &&
                (!best || std::abs(static_cast<long>(position + 1) -
                                   static_cast<long>(debug->column)) <
                              std::abs(static_cast<long>(*best + 1) -
                                   static_cast<long>(debug->column)))) {
                best = position;
            }
            position = line.find(keyword, position + 1);
        }
    }
    if (!best) return std::nullopt;
    return SourceLocation{debug->line, static_cast<unsigned>(*best + 1)};
}

std::optional<SourceLocation>
function_definition_location(const llvm::Function& function,
                             const std::string& input_file,
                             const std::vector<std::string>& lines) {
    const llvm::DISubprogram* subprogram = function.getSubprogram();
    if (!subprogram || subprogram->getLine() == 0 ||
        subprogram->getLine() > lines.size()) {
        return std::nullopt;
    }
    const std::filesystem::path expected(input_file);
    const std::filesystem::path actual(subprogram->getFilename().str());
    if (!actual.empty() && actual != expected &&
        actual.filename() != expected.filename()) {
        return std::nullopt;
    }
    return SourceLocation{
        subprogram->getLine(),
        first_non_space_column(lines[subprogram->getLine() - 1])};
}

std::optional<SourceLocation>
loop_body_location(const SourceLocation& loop_location,
                   const std::vector<std::string>& lines) {
    unsigned depth = 0;
    bool found_parenthesis = false;
    for (size_t line_index = loop_location.line - 1;
         line_index < lines.size(); ++line_index) {
        const std::string& line = lines[line_index];
        size_t column = line_index == loop_location.line - 1
                            ? loop_location.column - 1
                            : 0;
        for (; column < line.size(); ++column) {
            const char ch = line[column];
            if (ch == '(') {
                found_parenthesis = true;
                ++depth;
            } else if (ch == ')' && found_parenthesis && depth > 0) {
                --depth;
            } else if (ch == '{' && (!found_parenthesis || depth == 0)) {
                return SourceLocation{
                    static_cast<unsigned>(line_index + 1),
                    static_cast<unsigned>(column + 1)};
            } else if (found_parenthesis && depth == 0 &&
                       !std::isspace(static_cast<unsigned char>(ch))) {
                return SourceLocation{
                    static_cast<unsigned>(line_index + 1),
                    static_cast<unsigned>(column + 1)};
            }
        }
    }
    return std::nullopt;
}

std::optional<SourceLocation>
pre_loop_statement_location(const SourceLocation& loop_location,
                            const std::vector<std::string>& lines) {
    if (loop_location.line <= 1) return std::nullopt;
    for (size_t line_number = loop_location.line - 1; line_number > 0;
         --line_number) {
        const std::string& raw_line = lines[line_number - 1];
        std::string line = raw_line;
        if (const size_t comment = line.find("//");
            comment != std::string::npos) {
            line.erase(comment);
        }
        if (line.find_first_not_of(" \t\r") == std::string::npos) continue;
        if (line.find(';') != std::string::npos) {
            return SourceLocation{static_cast<unsigned>(line_number),
                                  first_non_space_column(line)};
        }
        if (line.find('{') != std::string::npos ||
            line.find('}') != std::string::npos) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

std::string join_expression(const z3::expr& expression,
                            const std::map<std::string, std::string>& symbols,
                            bool& supported);

std::string join_arguments(const z3::expr& expression, const char* op,
                           const std::map<std::string, std::string>& symbols,
                           bool& supported) {
    std::ostringstream out;
    out << '(';
    for (unsigned index = 0; index < expression.num_args(); ++index) {
        if (index > 0) out << ' ' << op << ' ';
        out << join_expression(expression.arg(index), symbols, supported);
        if (!supported) return {};
    }
    out << ')';
    return out.str();
}

std::string join_expression(const z3::expr& expression,
                            const std::map<std::string, std::string>& symbols,
                            bool& supported) {
    if (expression.is_true()) return "1";
    if (expression.is_false()) return "0";
    if (expression.is_numeral()) return expression.to_string();
    if (expression.is_const()) {
        const std::string name = expression.decl().name().str();
        const auto found = symbols.find(name);
        if (found != symbols.end()) return found->second;
        supported = false;
        return {};
    }

    const auto kind = expression.decl().decl_kind();
    switch (kind) {
    case Z3_OP_ADD: return join_arguments(expression, "+", symbols, supported);
    case Z3_OP_SUB: return join_arguments(expression, "-", symbols, supported);
    case Z3_OP_MUL: return join_arguments(expression, "*", symbols, supported);
    case Z3_OP_IDIV: return join_arguments(expression, "/", symbols, supported);
    case Z3_OP_MOD:
    case Z3_OP_REM: return join_arguments(expression, "%", symbols, supported);
    case Z3_OP_LE: return join_arguments(expression, "<=", symbols, supported);
    case Z3_OP_LT: return join_arguments(expression, "<", symbols, supported);
    case Z3_OP_GE: return join_arguments(expression, ">=", symbols, supported);
    case Z3_OP_GT: return join_arguments(expression, ">", symbols, supported);
    case Z3_OP_EQ: return join_arguments(expression, "==", symbols, supported);
    case Z3_OP_AND: return join_arguments(expression, "&&", symbols, supported);
    case Z3_OP_OR: return join_arguments(expression, "||", symbols, supported);
    case Z3_OP_UMINUS:
        return "(-" + join_expression(expression.arg(0), symbols, supported) +
               ")";
    case Z3_OP_NOT:
        return "(!" + join_expression(expression.arg(0), symbols, supported) +
               ")";
    case Z3_OP_ITE: {
        const std::string condition =
            join_expression(expression.arg(0), symbols, supported);
        const std::string then_value =
            join_expression(expression.arg(1), symbols, supported);
        const std::string else_value =
            join_expression(expression.arg(2), symbols, supported);
        return supported ? "(" + condition + " ? " + then_value + " : " +
                               else_value + ")"
                         : std::string{};
    }
    case Z3_OP_TO_REAL:
    case Z3_OP_TO_INT:
        return join_expression(expression.arg(0), symbols, supported);
    default:
        supported = false;
        return {};
    }
}

std::string sanitize_identifier(std::string value) {
    for (char& ch : value) {
        if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_') {
            ch = '_';
        }
    }
    if (value.empty() || std::isdigit(static_cast<unsigned char>(value[0]))) {
        value.insert(value.begin(), '_');
    }
    return value;
}

z3::expr substitute(const z3::expr& expression, const z3::expr& source,
                    const z3::expr& replacement) {
    z3::expr_vector sources(expression.ctx());
    z3::expr_vector replacements(expression.ctx());
    sources.push_back(source);
    replacements.push_back(replacement);
    z3::expr copy = expression;
    return copy.substitute(sources, replacements);
}

CorrectnessCertificate build_correctness_certificate(
    const std::vector<LoopCertificate>& certificates,
    const std::vector<FunctionCertificate>& function_certificates,
    const std::string& input_file) {
    CorrectnessCertificate result;
    const std::vector<std::string> lines = read_source_lines(input_file);
    std::set<std::string> used_identifiers = source_identifiers(lines);

    for (const FunctionCertificate& certificate : function_certificates) {
        if (!certificate.function) continue;
        const auto location = function_definition_location(
            *certificate.function, input_file, lines);
        if (!location) continue;

        std::map<std::string, std::string> symbols;
        for (const FunctionParameterCertificate& parameter :
             certificate.parameters) {
            if (!parameter.symbol.is_const()) {
                symbols.clear();
                break;
            }
            symbols.insert_or_assign(
                parameter.symbol.decl().name().str(),
                "\\at(" + parameter.source_name + ", Old)");
        }
        if (symbols.size() != certificate.parameters.size()) continue;

        bool supported = true;
        const std::string expression =
            join_expression(certificate.result, symbols, supported);
        if (!supported || expression.empty()) continue;
        result.contracts.push_back(
            {*location, "\\result == " + expression});
    }

    unsigned loop_index = 0;
    for (const LoopCertificate& certificate : certificates) {
        if (!certificate.loop || certificate.variables.empty()) continue;
        const auto loop_location = loop_keyword_location(
            *certificate.loop, input_file, lines);
        if (!loop_location) continue;
        const auto body_location = loop_body_location(*loop_location, lines);
        const auto pre_location = pre_loop_statement_location(
            *loop_location, lines);
        if (!body_location || !pre_location) continue;

        const std::string prefix = "arithexe_loop_" +
                                   std::to_string(loop_index++);
        const size_t ghost_variable_start = result.ghost_variables.size();
        const std::string iteration_name = unique_identifier(
            prefix + "_iteration", used_identifiers);
        result.ghost_variables.push_back(
            {iteration_name, "unsigned long long"});

        GhostUpdate initialize{*pre_location, {{iteration_name, "0"}}};
        GhostUpdate increment{
            *body_location,
            {{iteration_name, iteration_name + " + 1"}}};
        std::vector<std::string> conjuncts;
        conjuncts.push_back(iteration_name + " >= 0");

        for (const ClosedFormVariable& variable : certificate.variables) {
            z3::expr normalized = variable.expression;
            z3::context& context = normalized.ctx();
            const z3::expr iteration = context.int_const("ari_loop_n");
            const z3::expr zero = context.int_val(0);
            z3::expr base = substitute(normalized, iteration, zero).simplify();

            std::map<std::string, std::string> symbols{
                {"ari_loop_n", iteration_name}};
            if (!base.is_numeral() && !base.is_true() && !base.is_false()) {
                const std::string initial_name = unique_identifier(
                    prefix + "_" +
                        sanitize_identifier(variable.source_name) + "_initial",
                    used_identifiers);
                const z3::expr ghost = context.int_const(initial_name.c_str());
                normalized = substitute(normalized, base, ghost).simplify();
                symbols.insert_or_assign(initial_name, initial_name);
                result.ghost_variables.push_back(
                    {initial_name, variable.source_type});
                initialize.assignments.emplace_back(
                    initial_name, variable.source_name);
            }

            bool supported = true;
            const std::string expression =
                join_expression(normalized, symbols, supported);
            if (supported && !expression.empty()) {
                conjuncts.push_back("(" + variable.source_name + " == " +
                                    expression + ")");
            }
        }

        if (conjuncts.size() == 1) {
            result.ghost_variables.resize(ghost_variable_start);
            continue;
        }
        std::ostringstream invariant;
        for (size_t index = 0; index < conjuncts.size(); ++index) {
            if (index > 0) invariant << " && ";
            invariant << conjuncts[index];
        }
        result.invariants.push_back({*loop_location, invariant.str()});
        result.ghost_updates.push_back(std::move(initialize));
        result.ghost_updates.push_back(std::move(increment));
    }
    return result;
}

std::optional<SourceLocation>
function_return_location(const llvm::Instruction& instruction,
                         const std::string& input_file) {
    const auto start = source_location(instruction, input_file);
    if (!start) return std::nullopt;

    std::ifstream source(input_file, std::ios::binary);
    if (!source) return std::nullopt;
    const std::string text((std::istreambuf_iterator<char>(source)),
                           std::istreambuf_iterator<char>());

    size_t offset = 0;
    unsigned line = 1;
    unsigned column = 1;
    while (offset < text.size() && line < start->line) {
        if (text[offset++] == '\n') {
            ++line;
            column = 1;
        }
    }
    while (offset < text.size() && column < std::max(1U, start->column)) {
        if (text[offset] == '\n') return std::nullopt;
        ++offset;
        ++column;
    }

    bool in_string = false;
    bool in_character = false;
    bool in_line_comment = false;
    bool in_block_comment = false;
    bool escaped = false;
    unsigned depth = 0;
    bool found_open = false;

    for (; offset < text.size(); ++offset) {
        const char ch = text[offset];
        const char next = offset + 1 < text.size() ? text[offset + 1] : '\0';

        if (in_line_comment) {
            if (ch == '\n') in_line_comment = false;
        } else if (in_block_comment) {
            if (ch == '*' && next == '/') {
                in_block_comment = false;
                ++offset;
                ++column;
            }
        } else if (in_string || in_character) {
            if (escaped) {
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else if ((in_string && ch == '"') ||
                       (in_character && ch == '\'')) {
                in_string = false;
                in_character = false;
            }
        } else if (ch == '/' && next == '/') {
            in_line_comment = true;
            ++offset;
            ++column;
        } else if (ch == '/' && next == '*') {
            in_block_comment = true;
            ++offset;
            ++column;
        } else if (ch == '"') {
            in_string = true;
        } else if (ch == '\'') {
            in_character = true;
        } else if (ch == '(') {
            found_open = true;
            ++depth;
        } else if (ch == ')' && found_open) {
            if (--depth == 0) return SourceLocation{line, column};
        }

        if (ch == '\n') {
            ++line;
            column = 1;
        } else {
            ++column;
        }
    }
    return std::nullopt;
}

const llvm::Function* direct_callee(const llvm::Instruction* instruction) {
    const auto* call = llvm::dyn_cast_or_null<llvm::CallBase>(instruction);
    return call ? call->getCalledFunction() : nullptr;
}

std::vector<const llvm::Instruction*>
find_error_calls(const llvm::Module& module) {
    std::vector<const llvm::Instruction*> calls;
    for (const llvm::Function& function : module) {
        for (const llvm::BasicBlock& block : function) {
            for (const llvm::Instruction& instruction : block) {
                const llvm::Function* callee = direct_callee(&instruction);
                if (callee && is_error_function(callee->getName())) {
                    calls.push_back(&instruction);
                }
            }
        }
    }
    return calls;
}

const llvm::Instruction*
find_error_call_in(const llvm::Function* function) {
    if (!function || function->isDeclaration()) return nullptr;
    for (const llvm::BasicBlock& block : *function) {
        for (const llvm::Instruction& instruction : block) {
            const llvm::Function* callee = direct_callee(&instruction);
            if (callee && is_error_function(callee->getName())) {
                return &instruction;
            }
        }
    }
    return nullptr;
}

void write_metadata(std::ostream& out, const WitnessOptions& options,
                    const std::string& input_hash) {
    out << "  metadata:\n"
        << "    format_version: \"2.1\"\n"
        << "    uuid: " << yaml_quote(make_uuid_v4()) << "\n"
        << "    creation_time: " << yaml_quote(creation_time()) << "\n"
        << "    producer:\n"
        << "      name: " << yaml_quote(options.producer_name) << "\n"
        << "      version: " << yaml_quote(options.producer_version) << "\n"
        << "      configuration: \"svcomp\"\n"
        << "    task:\n"
        << "      input_files:\n"
        << "        - " << yaml_quote(options.input_file) << "\n"
        << "      input_file_hashes:\n"
        << "        " << yaml_quote(options.input_file) << ": "
        << yaml_quote(input_hash) << "\n"
        << "      specification: " << yaml_quote(options.specification) << "\n"
        << "      data_model: " << yaml_quote(options.data_model) << "\n"
        << "      language: \"C\"\n";
}

void write_location(std::ostream& out, const WitnessOptions& options,
                    const SourceLocation& location, unsigned indentation) {
    const std::string spaces(indentation, ' ');
    out << spaces << "file_name: " << yaml_quote(options.input_file) << "\n"
        << spaces << "line: " << location.line << "\n";
    if (location.column > 0) {
        out << spaces << "column: " << location.column << "\n";
    }
}

} // namespace

WitnessWriter::WitnessWriter(WitnessOptions options)
    : options(std::move(options)) {}

bool WitnessWriter::write(VeriResult result, const llvm::Module& module,
                          const llvm::Instruction* violation_instruction,
                          const std::vector<CounterexampleInput>& inputs,
                          const std::vector<LoopCertificate>&
                              loop_certificates,
                          const std::vector<FunctionCertificate>&
                              function_certificates) {
    error_message.clear();
    if (result == VERIUNKNOWN) {
        error_message = "no witness is defined for an UNKNOWN result";
        return false;
    }
    if (options.data_model != "ILP32" && options.data_model != "LP64") {
        error_message = "data model must be ILP32 or LP64";
        return false;
    }
    const auto input_hash = sha256_file(options.input_file);
    if (!input_hash) {
        error_message = "cannot read input file for SHA-256: " + options.input_file;
        return false;
    }

    auto error_calls = find_error_calls(module);
    std::vector<std::pair<const llvm::Instruction*, SourceLocation>> targets;
    for (const llvm::Instruction* call : error_calls) {
        if (auto location = source_location(*call, options.input_file)) {
            targets.emplace_back(call, *location);
        }
    }

    if (targets.empty() && violation_instruction) {
        if (auto location = source_location(*violation_instruction,
                                            options.input_file)) {
            targets.emplace_back(violation_instruction, *location);
        }
    }
    if (targets.empty()) {
        error_message = "cannot find a source-level error location in " +
                        options.input_file;
        return false;
    }

    CorrectnessCertificate correctness_certificate;
    if (result == HOLD) {
        correctness_certificate = build_correctness_certificate(
            loop_certificates, function_certificates, options.input_file);
    }

    std::vector<std::pair<const CounterexampleInput*, SourceLocation>>
        located_inputs;
    if (result == FAIL) {
        std::map<const llvm::Instruction*, std::optional<SourceLocation>>
            location_cache;
        for (const CounterexampleInput& input : inputs) {
            auto [position, inserted] = location_cache.emplace(
                input.call, std::nullopt);
            if (inserted) {
                position->second = function_return_location(
                    *input.call, options.input_file);
            }
            if (!position->second) {
                error_message =
                    "cannot locate the closing parenthesis of a nondeterministic "
                    "call in " + options.input_file;
                return false;
            }
            located_inputs.emplace_back(&input, *position->second);
        }
    }

    std::ofstream output(options.output_path, std::ios::trunc);
    if (!output) {
        error_message = "cannot open witness output: " + options.output_path;
        return false;
    }

    if (result == HOLD) {
        output << "- entry_type: \"invariant_set\"\n";
        write_metadata(output, options, *input_hash);
        output << "  content:\n";
        for (const GeneratedFunctionContract& contract :
             correctness_certificate.contracts) {
            output << "    - contract:\n"
                   << "        type: \"function_contract\"\n"
                   << "        location:\n";
            write_location(output, options, contract.location, 10);
            output << "        ensures: " << yaml_quote(contract.ensures)
                   << "\n"
                   << "        format: \"ext_c_expression\"\n";
        }
        for (const GeneratedLoopInvariant& invariant :
             correctness_certificate.invariants) {
            output << "    - invariant:\n"
                   << "        type: \"loop_invariant\"\n"
                   << "        location:\n";
            write_location(output, options, invariant.location, 10);
            output << "        value: " << yaml_quote(invariant.value) << "\n"
                   << "        format: \"c_expression\"\n";
        }
        for (const auto& [_, location] : targets) {
            output << "    - invariant:\n"
                   << "        type: \"location_invariant\"\n"
                   << "        location:\n";
            write_location(output, options, location, 10);
            output << "        value: \"0\"\n"
                   << "        format: \"c_expression\"\n";
        }
        if (!correctness_certificate.ghost_variables.empty()) {
            output << "- entry_type: \"ghost_instrumentation\"\n";
            write_metadata(output, options, *input_hash);
            output << "  content:\n"
                   << "    ghost_variables:\n";
            for (const GhostVariable& variable :
                 correctness_certificate.ghost_variables) {
                output << "      - name: " << yaml_quote(variable.name) << "\n"
                       << "        type: " << yaml_quote(variable.type) << "\n"
                       << "        scope: \"global\"\n"
                       << "        initial:\n"
                       << "          value: \"0\"\n"
                       << "          format: \"c_expression\"\n";
            }
            output << "    ghost_updates:\n";
            for (const GhostUpdate& update :
                 correctness_certificate.ghost_updates) {
                output << "      - location:\n";
                write_location(output, options, update.location, 10);
                output << "        updates:\n";
                for (const auto& [variable, value] : update.assignments) {
                    output << "          - variable: "
                           << yaml_quote(variable) << "\n"
                           << "            value: " << yaml_quote(value)
                           << "\n"
                           << "            format: \"c_expression\"\n";
                }
            }
        }
    } else {
        const llvm::Instruction* target = violation_instruction;
        if (const llvm::Function* callee = direct_callee(violation_instruction)) {
            if (!is_error_function(callee->getName())) {
                if (const llvm::Instruction* nested = find_error_call_in(callee)) {
                    target = nested;
                }
            }
        }

        std::optional<SourceLocation> target_location;
        if (target) target_location = source_location(*target, options.input_file);
        if (!target_location) target_location = targets.front().second;

        output << "- entry_type: \"violation_sequence\"\n";
        write_metadata(output, options, *input_hash);
        output << "  content:\n";
        for (const auto& [input, location] : located_inputs) {
            output << "    - segment:\n"
                   << "        - waypoint:\n"
                   << "            type: \"function_return\"\n"
                   << "            action: \"follow\"\n"
                   << "            constraint:\n"
                   << "              value: "
                   << yaml_quote("\\result == " + input->value) << "\n"
                   << "              format: \"ext_c_expression\"\n"
                   << "            location:\n";
            write_location(output, options, location, 14);
        }
        output << "    - segment:\n"
               << "        - waypoint:\n"
               << "            type: \"target\"\n"
               << "            action: \"follow\"\n"
               << "            location:\n";
        write_location(output, options, *target_location, 14);
    }

    output.close();
    if (!output) {
        error_message = "failed while writing witness output: " +
                        options.output_path;
        return false;
    }
    return true;
}

} // namespace ari_exe
