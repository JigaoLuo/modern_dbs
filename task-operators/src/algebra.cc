#include "moderndbs/algebra.h"
#include <cassert>
#include <functional>
#include <string>


namespace moderndbs::iterator_model {
Register Register::Construct_Int_Register(int64_t value) {
    /// Solution Idea.
    Register reg;
    reg.value.emplace<0>(value);
    return reg;
}

Register Register::from_int(int64_t value) { return Construct_Int_Register(value); }

Register Register::Construct_Char_Register(const std::string& value) {
    /// Solution Idea.
    Register reg;
    reg.value.emplace<1>(value);
    return reg;
}

Register Register::from_string(const std::string& value) { return Construct_Char_Register(value); }

Register::Type Register::get_type() const {
    /// Solution Idea.
    switch (value.index()) {
        case 0:
            return Type::INT64;
        case 1:
            return Type::CHAR16;
    }
    __builtin_unreachable();
}

int64_t Register::as_int() const {
    /// Solution Idea.
    return std::get<0>(value);
}

std::string Register::as_string() const {
    /// Solution Idea.
    return std::get<1>(value);
}

uint64_t Register::get_hash() const {
    /// Solution Idea.
    return std::hash<decltype(value)>{}(value);
}

bool operator==(const Register& r1, const Register& r2) { return r1.value == r2.value; }

bool operator!=(const Register& r1, const Register& r2) { return r1.value != r2.value; }

bool operator<(const Register& r1, const Register& r2) {
    assert(r1.get_type() == r2.get_type());
    if (r1.get_type() == Register::Type::INT64) {
        return r1.as_int() < r2.as_int();
    } else {
        assert(r1.get_type() == Register::Type::CHAR16);
        return r1.as_string() < r2.as_string();
    }
}

bool operator<=(const Register& r1, const Register& r2) {
    assert(r1.get_type() == r2.get_type());
    return r1 < r2 || r1 == r2;
}

bool operator>(const Register& r1, const Register& r2) {
    assert(r1.get_type() == r2.get_type());
    if (r1.get_type() == Register::Type::INT64) {
        return r1.as_int() > r2.as_int();
    } else {
        assert(r1.get_type() == Register::Type::CHAR16);
        return r1.as_string() > r2.as_string();
    }
}

bool operator>=(const Register& r1, const Register& r2) {
    assert(r1.get_type() == r2.get_type());
    return r1 > r2 || r1 == r2;
}

static std::vector<Register*> convert_to_output(std::vector<Register>& tuple) {
    std::vector<Register*> output;
    output.reserve(tuple.size());
    for (auto& reg : tuple) {
        output.push_back(&reg);
    }
    return output;
}

static std::vector<Register> convert_to_tuples(std::vector<Register*>& output) {
    std::vector<Register> tuple;
    tuple.reserve(output.size());
    for (auto& reg : output) {
        tuple.push_back(*reg);
    }
    return tuple;
}

Print::Print(Operator& input, std::ostream& stream) : UnaryOperator(input), stream(stream) {}

Print::~Print() = default;

void Print::open() {
    input->open();
    input_regs = input->get_output();
}

static void print_tuple(std::ostream& stream, const std::vector<Register*>& tuple) {
    for (size_t i = 0; i < tuple.size(); ++i) {
        auto &reg = *tuple[i];
        if (reg.get_type() == Register::Type::INT64) {
            stream << reg.as_int();
        } else {
            assert(reg.get_type() == Register::Type::CHAR16);
            stream << reg.as_string();
        }
        if (i + 1 < tuple.size()) {
            stream << ',';
        }
    }
}

bool Print::next() {
    if (input->next()) {
        print_tuple(stream, input_regs);
        stream << '\n';
        return true;
    } else {
        return false;
    }
}

void Print::close() {
    input->close();
    stream.clear();
}

std::vector<Register*> Print::get_output() {
    /// Print has no output.
    return {};
}

Projection::Projection(Operator& input, std::vector<size_t> attr_indexes) : UnaryOperator(input), attr_indexes(std::move(attr_indexes)) {}

Projection::~Projection() = default;

void Projection::open() { input->open(); }

bool Projection::next() { return input->next(); }

void Projection::close() {
    input->close();
    attr_indexes.clear();
}

std::vector<Register*> Projection::get_output() {
    const std::vector<Register*>& src_regs = input->get_output();
    std::vector<Register*> output;
    output.reserve(attr_indexes.size());
    for (size_t i = 0; i < attr_indexes.size(); i++) {
        output.push_back(src_regs[i] /* The attribute at the index to be projected */);
    }
    return output;
}

Select::Select(Operator& input, PredicateAttributeInt64 predicate)  // NOLINT
  : UnaryOperator(input),
  attr_index(predicate.attr_index),
  right_operand{std::in_place_index<0>, Register::from_int(predicate.constant)},
  predicate_type(predicate.predicate_type) {}

Select::Select(Operator& input, PredicateAttributeChar16 predicate)  // NOLINT
  : UnaryOperator(input),
  attr_index(predicate.attr_index),
  right_operand{std::in_place_index<0>, Register::from_string(predicate.constant)},
  predicate_type(predicate.predicate_type) {}

Select::Select(Operator& input, PredicateAttributeAttribute predicate)  // NOLINT
  : UnaryOperator(input),
  attr_index(predicate.attr_left_index),
  right_operand{std::in_place_index<1>, predicate.attr_right_index},
  predicate_type(predicate.predicate_type) {}

Select::~Select() = default;

void Select::open() {
    input->open();
    input_regs = input->get_output();
}

bool Select::next() {
    while (input->next()) {
        bool result = [&]() {
            auto* reg_left = input_regs[attr_index];
            Register* reg_right;
            switch (right_operand.index()) {
                case 0:
                    reg_right = &std::get<0>(right_operand);
                    break;
                case 1:
                    reg_right = input_regs[std::get<1>(right_operand)];
                    break;
                default:
                    __builtin_unreachable();
            }

            switch (predicate_type) {
                case PredicateType::EQ:
                    return (*reg_left == *reg_right);
                case PredicateType::NE:
                    return (*reg_left != *reg_right);
                case PredicateType::LT:
                    return (*reg_left < *reg_right);
                case PredicateType::LE:
                    return (*reg_left <= *reg_right);
                case PredicateType::GT:
                    return (*reg_left > *reg_right);
                case PredicateType::GE:
                    return (*reg_left >= *reg_right);
                default:
                    __builtin_unreachable();  /// You Dead :D.
        }
        }();

        if (result) return true;
    }
    return false;
}

void Select::close() { input->close(); }

std::vector<Register*> Select::get_output() { return input->get_output(); }

Sort::Sort(Operator& input, std::vector<Criterion> criteria) : UnaryOperator(input), criteria(std::move(criteria)) {}

Sort::~Sort() = default;

void Sort::open() {
    input->open();
    input_regs = input->get_output();
    output_regs.resize(input_regs.size());
}

bool Sort::next() {
    if (next_output_offset == 0) {
        /// Pipeline breaker: consume all child's input.
        while (input->next()) {
            /// Materialize all its attributes.
            /// Since the register is ALWAYS a same memory with changing data content. aka A same pointer pointing to a same space with different tuple data.
            /// For sorting, the operator must have all the data as REGISTER, not as pointers.
            std::vector<Register> reg;
            reg.reserve(input_regs.size());
            for (const auto& attr : input_regs) {
                reg.push_back(*attr);
            }
            sorted.push_back(reg);
        }

        /// Start to sort.
        /// Attention: The first criterion executed at last -- as the most important.
        for (auto it = criteria.rbegin(); it != criteria.rend(); it++) {
            const auto& criterion = *it;
            std::stable_sort(sorted.begin(), sorted.end(), [&criterion](const std::vector<Register>& a, const std::vector<Register>& b) {
                if (criterion.desc) {
                    return a[criterion.attr_index] > b[criterion.attr_index];
                } else {
                    return a[criterion.attr_index] < b[criterion.attr_index];
                }
            });
        }
    }
    if (next_output_offset == sorted.size()) {
        return false;
    } else {
        output_regs = sorted[next_output_offset++];
        return true;
    }
}

std::vector<Register*> Sort::get_output() { return convert_to_output(output_regs); }

void Sort::close() {
    input->close();
    sorted.clear();
}

HashJoin::HashJoin(Operator& input_left, Operator& input_right, size_t attr_index_left, size_t attr_index_right)
    : BinaryOperator(input_left, input_right), attr_index_left(attr_index_left), attr_index_right(attr_index_right) {}

HashJoin::~HashJoin() = default;

void HashJoin::open() {
    input_left->open();
    input_right->open();
    input_regs_left = input_left->get_output();
    input_regs_right = input_right->get_output();
    output_regs.resize(input_regs_left.size() + input_regs_right.size());
}

bool HashJoin::next() {
    if (!ht_build) {
        /// Pipeline breaker: consume all input_left's input: Hash Table Build.
        while (input_left->next()) {
            const auto &input_tuple = input_left->get_output();
            /// Materialize all its attributes.
            /// Since the register is ALWAYS a same memory with changing data content. aka A same pointer pointing to a same space with different tuple data.
            /// For sorting, the operator must have all the data as REGISTER, not as pointers.
            std::vector<Register> reg;
            reg.reserve(input_tuple.size());
            for (const auto &attr : input_tuple) {
                reg.push_back(*attr);
            }
            ht.emplace(*input_tuple[attr_index_left], reg);
        }
    }

    /// Start to use the input_right: Hash Table Probe.
    while (input_right->next()) {
        const auto it = ht.find(*input_regs_right[attr_index_right]);
        if (it != ht.end()) {
            for (size_t i = 0; i < it->second.size(); ++i) {
                output_regs[i] = it->second[i];
            }
            for (size_t i = 0; i < input_regs_right.size(); ++i) {
                output_regs[it->second.size() + i] = *input_regs_right[i];
            }
            return true;
        }
    }
    return false;
}

void HashJoin::close() {
    input_left->close();
    input_right->close();
    ht.clear();
}

std::vector<Register*> HashJoin::get_output() { return convert_to_output(output_regs); }

HashAggregation::HashAggregation(Operator& input, std::vector<size_t> group_by_attrs, std::vector<AggrFunc> aggr_funcs) :
    UnaryOperator(input), group_by_attrs(std::move(group_by_attrs)), aggr_funcs(std::move(aggr_funcs)) {}

HashAggregation::~HashAggregation() = default;

void HashAggregation::open() {
    input->open();
    input_regs = input->get_output();
    output_regs.resize(group_by_attrs.size() + aggr_funcs.size());
}

bool HashAggregation::next() {
    if (!seen_input) {
        while (input->next()) {
            std::vector<Register> group_by_regs;
            group_by_regs.reserve(group_by_attrs.size());
            for (const auto index : group_by_attrs) {
                group_by_regs.push_back(*input_regs[index]);
            }
            auto& agggregates = ht[group_by_regs];
            if (agggregates.empty()) {
                agggregates.reserve(aggr_funcs.size());
                for (const auto& aggr_func : aggr_funcs) {
                    switch (aggr_func.func) {
                    case AggrFunc::Func::MIN:
                    case AggrFunc::Func::MAX:
                        agggregates.push_back(*input_regs[aggr_func.attr_index]);
                        break;
                    case AggrFunc::Func::SUM:
                    case AggrFunc::Func::COUNT:
                        agggregates.push_back(Register::from_int(0));
                        break;
                    default:
                      __builtin_unreachable(); // You Dead :D
                    }
                }
            }
            for (size_t i = 0; i < aggr_funcs.size(); ++i) {
                auto& aggr_func = aggr_funcs[i];
                auto&  agggregate = agggregates[i];
                switch (aggr_func.func) {
                    case AggrFunc::Func::MIN:
                        agggregate = std::min(agggregate, *input_regs[aggr_func.attr_index]);
                        break;
                    case AggrFunc::Func::MAX:
                        agggregate = std::max(agggregate, *input_regs[aggr_func.attr_index]);
                        break;
                    case AggrFunc::Func::SUM:
                        assert(agggregate.get_type() == Register::Type::INT64 && "Can only sum up INT.");
                        agggregate = Register::from_int(agggregate.as_int() + input_regs[aggr_func.attr_index]->as_int());
                        break;
                    case AggrFunc::Func::COUNT:
                        assert(agggregate.get_type() == Register::Type::INT64 && "The counter must be INT.");
                        agggregate = Register::from_int(agggregate.as_int() + 1);
                        break;
                    default:
                      __builtin_unreachable(); // You Dead :D
                }
            }

        }
        output_iterator = ht.begin();
        seen_input = true;
    } else {
        ++output_iterator;
    }

    if (output_iterator != ht.end()) {
        for (size_t i = 0; i < group_by_attrs.size(); ++i) {
            output_regs[i] = output_iterator->first[i];
        }
        for (size_t i = 0; i < aggr_funcs.size(); ++i) {
            output_regs[group_by_attrs.size() + i] = output_iterator->second[i];
        }
        return true;
    } else {
        return false;
    }
}

void HashAggregation::close() {
    input->close();
    ht.clear();
}

std::vector<Register*> HashAggregation::get_output() { return convert_to_output(output_regs); }

Union::Union(Operator& input_left, Operator& input_right) : BinaryOperator(input_left, input_right) {}

Union::~Union() = default;

void Union::open() {
    input_left->open();
    input_right->open();
    input_regs_left = input_left->get_output();
    input_regs_right = input_right->get_output();
    output_regs.resize(input_regs_left.size());
}

bool Union::check_tuple(std::vector<Register*>& regs) {
    auto input_tuple = convert_to_tuples(regs);
    const bool was_inserted = ht.insert(input_tuple).second;
    if (was_inserted) {
        for (size_t i = 0; i < regs.size(); ++i) {
            output_regs[i] = *regs[i];
        }
        return true;
    } else {
        return false;
    }
}

bool Union::next() {
    /// This implementation: Not a pipeline breaker.
    if (!seen_input) {
        /// Iterate all left tuples.
        while (input_left->next()) {
            if (check_tuple(input_regs_left)) {
                return true;
            }
        }
        seen_input = true;
    }
    /// Iterate all left tuples.
    while (input_right->next()) {
        if (check_tuple(input_regs_right)) {
            return true;
        }
    }
    return false;
}

std::vector<Register*> Union::get_output() {
    return convert_to_output(output_regs);
}

void Union::close() {
    input_left->close();
    input_right->close();
    ht.clear();
}

UnionAll::UnionAll(Operator& input_left, Operator& input_right) : BinaryOperator(input_left, input_right) {}

UnionAll::~UnionAll() = default;

void UnionAll::open() {
    input_left->open();
    input_right->open();
    input_regs_left = input_left->get_output();
    input_regs_right = input_right->get_output();
    output_regs.resize(input_regs_left.size());
}

bool UnionAll::next() {
    /// UnionAll: just output all tuple from left and right. No need for hashing. :D
    /// Not a pipeline breaker.
    if (!seen_input) {
        /// Iterate all left tuples.
        if (input_left->next()) {
            for (size_t i = 0; i < output_regs.size(); ++i) {
                output_regs[i] = *input_regs_left[i];
            }
            return true;
        }
        seen_input = true;
    }
    /// Iterate all left tuples.
    if (input_right->next()) {
        for (size_t i = 0; i < output_regs.size(); ++i) {
            output_regs[i] = *input_regs_right[i];
        }
        return true;
    } else {
        return false;
    }
}

std::vector<Register*> UnionAll::get_output() { return convert_to_output(output_regs); }

void UnionAll::close() {
    input_left->close();
    input_right->close();
}

Intersect::Intersect(Operator& input_left, Operator& input_right) : BinaryOperator(input_left, input_right) {}

Intersect::~Intersect() = default;

void Intersect::open() {
    input_left->open();
    input_right->open();
    input_regs_left = input_left->get_output();
    input_regs_right = input_right->get_output();
    output_regs.resize(input_regs_left.size());
}

bool Intersect::next() {
    /// Pipeline breaker: consume all input_left's input: Build the hash set.
    if (!seen_input) {
        while (input_left->next()) {
            const auto input_tuple = convert_to_tuples(input_regs_left);
            auto& counter = ht[input_tuple];
            counter = 1;  /// The difference.
        }
        seen_input = true;
    }
    while (input_right->next()) {
        const auto input_tuple = convert_to_tuples(input_regs_right);
        auto it = ht.find(input_tuple);
        if (it != ht.end() && it->second > 0) {
            --it->second;
            for (size_t i = 0; i < output_regs.size(); ++i) {
                output_regs[i] = input_tuple[i];
            }
            return true;
        }
    }
    return false;
}

std::vector<Register*> Intersect::get_output() { return convert_to_output(output_regs); }

void Intersect::close() {
    input_left->close();
    input_right->close();
    ht.clear();
}

IntersectAll::IntersectAll(Operator& input_left, Operator& input_right) : BinaryOperator(input_left, input_right) {}

IntersectAll::~IntersectAll() = default;

void IntersectAll::open() {
    input_left->open();
    input_right->open();
    input_regs_left = input_left->get_output();
    input_regs_right = input_right->get_output();
    output_regs.resize(input_regs_left.size());
}

bool IntersectAll::next() {
    /// Pipeline breaker: consume all input_left's input: Build the hash set.
    if (!seen_input) {
        while (input_left->next()) {
            const auto input_tuple = convert_to_tuples(input_regs_left);
            auto& counter = ht[input_tuple];
            ++counter;  /// The difference.
        }
        seen_input = true;
    }
    while (input_right->next()) {
        const auto input_tuple = convert_to_tuples(input_regs_right);
        auto it = ht.find(input_tuple);
        if (it != ht.end() && it->second > 0) {
            --it->second;
            for (size_t i = 0; i < output_regs.size(); ++i) {
                output_regs[i] = input_tuple[i];
            }
            return true;
        }
    }
    return false;
}

std::vector<Register*> IntersectAll::get_output() { return convert_to_output(output_regs); }

void IntersectAll::close() {
    input_left->close();
    input_right->close();
    ht.clear();
}

Except::Except(Operator& input_left, Operator& input_right) : BinaryOperator(input_left, input_right) {}

Except::~Except() = default;

void Except::open() {
    input_left->open();
    input_right->open();
    input_regs_left = input_left->get_output();
    input_regs_right = input_right->get_output();
    output_regs.resize(input_regs_left.size());
}

bool Except::next() {
    /// Full Pipeline breaker: consume all input_left and input_right: Build the hash set.
    if (!seen_input) {
        while (input_left->next()) {
            const auto input_tuple = convert_to_tuples(input_regs_left);
            auto& counter = ht[input_tuple];
            counter = 1;  /// The difference.
        }
        while (input_right->next()) {
            const auto input_tuple = convert_to_tuples(input_regs_right);
            auto it = ht.find(input_tuple);
            if (it != ht.end() && it->second > 0) {
                --it->second;
            }
        }
        current_output_it = ht.begin();
        seen_input = true;
    }

    while (current_output_it != ht.end()) {
        auto& counter = current_output_it->second;
        if (counter > 0) {
            --counter;
            for (size_t i = 0; i < output_regs.size(); ++i) {
                output_regs[i] = current_output_it->first[i];
            }
            return true;
        }
        ++current_output_it;
    }
    return false;
}

std::vector<Register*> Except::get_output() { return convert_to_output(output_regs); }

void Except::close() {
    input_left->close();
    input_right->close();
    ht.clear();
}

ExceptAll::ExceptAll(Operator& input_left, Operator& input_right) : BinaryOperator(input_left, input_right) {}

ExceptAll::~ExceptAll() = default;

void ExceptAll::open() {
    input_left->open();
    input_right->open();
    input_regs_left = input_left->get_output();
    input_regs_right = input_right->get_output();
    output_regs.resize(input_regs_left.size());
}

bool ExceptAll::next() {
    /// Full Pipeline breaker: consume all input_left and input_right: Build the hash set.
    if (!seen_input) {
        while (input_left->next()) {
            const auto input_tuple = convert_to_tuples(input_regs_left);
            auto& counter = ht[input_tuple];
            ++counter;  /// The difference.
        }
        while (input_right->next()) {
            const auto input_tuple = convert_to_tuples(input_regs_right);
            auto it = ht.find(input_tuple);
            if (it != ht.end() && it->second > 0) {
                --it->second;
            }
        }
        current_output_it = ht.begin();
        seen_input = true;
    }

    while (current_output_it != ht.end()) {
        auto& counter = current_output_it->second;
        if (counter > 0) {
            --counter;
            for (size_t i = 0; i < output_regs.size(); ++i) {
                output_regs[i] = current_output_it->first[i];
            }
            return true;
        }
        ++current_output_it;
    }
    return false;
}

std::vector<Register*> ExceptAll::get_output() { return convert_to_output(output_regs); }

void ExceptAll::close() {
    input_left->close();
    input_right->close();
    ht.clear();
}

}  // namespace moderndbs::iterator_model
