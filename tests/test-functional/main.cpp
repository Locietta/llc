#include <llc/utils/functional.h>

#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace {

void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct TrivialCallable final {
    int value;

    int operator()() const { return value; }
};

static_assert(std::is_trivially_copyable_v<TrivialCallable>);
static_assert(std::is_nothrow_move_constructible_v<TrivialCallable>);
static_assert(sizeof(TrivialCallable) <= llc::Function<int()>::k_sbo_size);

struct TrackedCallable final {
    std::unique_ptr<int> value;
    int *move_count;
    int *destruction_count;

    TrackedCallable(int value, int &move_count, int &destruction_count)
        : value(std::make_unique<int>(value)), move_count(&move_count), destruction_count(&destruction_count) {}

    TrackedCallable(const TrackedCallable &) = delete;
    TrackedCallable &operator=(const TrackedCallable &) = delete;

    TrackedCallable(TrackedCallable &&other) noexcept
        : value(std::move(other.value)), move_count(other.move_count), destruction_count(other.destruction_count) {
        ++*move_count;
    }

    ~TrackedCallable() { ++*destruction_count; }

    int operator()() const { return *value; }
    int invoke() { return *value; }
    int invoke_const() const { return *value; }
};

static_assert(!std::is_trivially_copyable_v<TrackedCallable>);
static_assert(std::is_nothrow_move_constructible_v<TrackedCallable>);
static_assert(sizeof(TrackedCallable) <= llc::Function<int()>::k_sbo_size);
static_assert(alignof(TrackedCallable) <= llc::Function<int()>::k_sbo_align);

struct ThrowingMoveCallable final {
    int value;
    int *move_count;

    ThrowingMoveCallable(int value, int &move_count) : value(value), move_count(&move_count) {}

    ThrowingMoveCallable(ThrowingMoveCallable &&other) noexcept(false) : value(other.value), move_count(other.move_count) {
        ++*move_count;
    }

    int operator()() const { return value; }
};

static_assert(!std::is_nothrow_move_constructible_v<ThrowingMoveCallable>);
static_assert(sizeof(ThrowingMoveCallable) <= llc::Function<int()>::k_sbo_size);

void test_trivially_copyable_sbo_move() {
    llc::Function<int()> source{TrivialCallable{42}};
    llc::Function<int()> destination{std::move(source)};
    require(destination() == 42, "trivially copyable target was corrupted by move construction");

    llc::Function<int()> assigned{TrivialCallable{7}};
    assigned = std::move(destination);
    require(assigned() == 42, "trivially copyable target was corrupted by move assignment");
}

void test_nontrivial_target_uses_stable_storage() {
    int moves = 0;
    int destructions = 0;

    {
        llc::Function<int()> source{TrackedCallable{42, moves, destructions}};
        const int moves_before_construction = moves;
        const int destructions_before_construction = destructions;

        llc::Function<int()> destination{std::move(source)};
        require(moves == moves_before_construction, "moving Function attempted to move a non-trivial target");
        require(destructions == destructions_before_construction,
                "moving Function unexpectedly destroyed a non-trivial target");
        require(destination() == 42, "non-trivial target was corrupted by move construction");

        llc::Function<int()> replacement{TrackedCallable{7, moves, destructions}};
        const int moves_before_assignment = moves;
        const int destructions_before_assignment = destructions;

        destination = std::move(replacement);
        require(moves == moves_before_assignment, "move assignment attempted to move a non-trivial target");
        require(destructions == destructions_before_assignment + 1,
                "move assignment did not destroy the replaced target exactly once");
        require(destination() == 7, "non-trivial target was corrupted by move assignment");
    }
}

void test_throwing_move_target_uses_stable_storage() {
    int moves = 0;
    llc::Function<int()> source{ThrowingMoveCallable{42, moves}};
    const int moves_before_wrapper_move = moves;

    llc::Function<int()> destination{std::move(source)};
    require(moves == moves_before_wrapper_move, "moving Function attempted to move a potentially-throwing target");
    require(destination() == 42, "heap-stored target was corrupted by wrapper move");
}

void test_const_and_bound_function_moves() {
    int moves = 0;
    int destructions = 0;

    llc::Function<int() const> const_source{TrackedCallable{13, moves, destructions}};
    const int moves_before_const_move = moves;
    llc::Function<int() const> const_destination{std::move(const_source)};
    require(moves == moves_before_const_move, "moving const Function attempted to move a non-trivial target");
    require(const_destination() == 13, "const Function target was corrupted by move");

    auto bound_source = llc::bind<&TrackedCallable::invoke>(TrackedCallable{17, moves, destructions});
    const int moves_before_bound_move = moves;
    auto bound_destination = std::move(bound_source);
    require(moves == moves_before_bound_move, "moving bound Function attempted to move a non-trivial target");
    require(bound_destination() == 17, "bound Function target was corrupted by move");

    auto const_bound_source = llc::bind<&TrackedCallable::invoke_const>(TrackedCallable{23, moves, destructions});
    const int moves_before_const_bound_move = moves;
    auto const_bound_destination = std::move(const_bound_source);
    require(moves == moves_before_const_bound_move, "moving const bound Function attempted to move a non-trivial target");
    require(const_bound_destination() == 23, "const bound Function target was corrupted by move");
}

} // namespace

int main() {
    test_trivially_copyable_sbo_move();
    test_nontrivial_target_uses_stable_storage();
    test_throwing_move_target_uses_stable_storage();
    test_const_and_bound_function_moves();
    return 0;
}
