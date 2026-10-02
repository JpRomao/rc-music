#include "m0/adapters/SingleWorkerDownloadCheckpointIo.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <chrono>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace music_player::m0::test {
namespace {
using namespace std::chrono_literals;

// Both a bounded watchdog and RAII release prevent a failed REQUIRE from
// leaving the worker joined behind a test-owned gate.
class Gate final {
  public:
    void block() {
        const std::scoped_lock lock(mutex_);
        blocked_ = true;
    }
    void enter() {
        std::unique_lock lock(mutex_);
        entered_ = true;
        condition_.notify_all();
        if (blocked_ && !condition_.wait_for(lock, 5s, [&] { return released_; }))
            expired_ = true;
    }
    bool awaitEntry() {
        std::unique_lock lock(mutex_);
        return condition_.wait_for(lock, 2s, [&] { return entered_; });
    }
    void release() {
        const std::scoped_lock lock(mutex_);
        released_ = true;
        condition_.notify_all();
    }
    bool expired() const {
        const std::scoped_lock lock(mutex_);
        return expired_;
    }

  private:
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    bool blocked_{false};
    bool entered_{false};
    bool released_{false};
    bool expired_{false};
};

DownloadCheckpoint checkpoint() {
    return {.receipts = {{.request_id = "abcdefab-cdef-4abc-8def-000000000001",
                          .fingerprint = std::string(64, 'a'),
                          .job_ids = {"synthetic-1"},
                          .retained_request_bytes = 100}},
            .jobs = {{.job_id = "synthetic-1", .title = "Synthetic audio"}}};
}

struct Observations final {
    unsigned int factory_calls{0};
    unsigned int load_calls{0};
    unsigned int save_calls{0};
    std::thread::id factory_thread{};
    std::thread::id load_thread{};
    std::thread::id save_thread{};
    std::thread::id store_destruction_thread{};
    std::thread::id factory_destruction_thread{};
    std::optional<DownloadCheckpoint> saved{};
};

struct State final {
    Gate factory_gate{};
    Gate load_gate{};
    Gate save_gate{};
    Gate store_destruction_gate{};
    Gate factory_destruction_gate{};
    DownloadCheckpointLoadResult load_result{.checkpoint = checkpoint(),
                                             .status = DownloadCheckpointLoadStatus::Loaded};
    DownloadCheckpointSaveStatus save_result{DownloadCheckpointSaveStatus::Saved};
    bool factory_throws{false};
    bool factory_null{false};
    bool load_throws{false};
    bool save_throws{false};
    mutable std::mutex mutex{};
    Observations observations{};

    template <class F> void record(F&& operation) {
        const std::scoped_lock lock(mutex);
        operation(observations);
    }
    Observations observed() const {
        const std::scoped_lock lock(mutex);
        return observations;
    }
    void releaseAll() {
        factory_gate.release();
        load_gate.release();
        save_gate.release();
        store_destruction_gate.release();
        factory_destruction_gate.release();
    }
};

struct ReleaseGates final {
    std::shared_ptr<State> state;
    ~ReleaseGates() { state->releaseAll(); }
};

class Store final : public DownloadCheckpointStore {
  public:
    explicit Store(std::shared_ptr<State> state) : state_(std::move(state)) {}
    ~Store() override {
        state_->record([](auto& value) { value.store_destruction_thread = std::this_thread::get_id(); });
        state_->store_destruction_gate.enter();
    }
    DownloadCheckpointLoadResult load() override {
        state_->record([](auto& value) {
            ++value.load_calls;
            value.load_thread = std::this_thread::get_id();
        });
        state_->load_gate.enter();
        if (state_->load_throws)
            throw std::runtime_error{"synthetic store error"};
        return state_->load_result;
    }
    DownloadCheckpointSaveStatus save(const DownloadCheckpoint& value) override {
        state_->record([&](auto& observed) {
            ++observed.save_calls;
            observed.save_thread = std::this_thread::get_id();
            observed.saved = value;
        });
        state_->save_gate.enter();
        if (state_->save_throws)
            throw std::runtime_error{"synthetic store error"};
        return state_->save_result;
    }

  private:
    std::shared_ptr<State> state_;
};

struct FactoryLifetime final {
    std::shared_ptr<State> state;
    ~FactoryLifetime() {
        state->record([](auto& value) { value.factory_destruction_thread = std::this_thread::get_id(); });
        state->factory_destruction_gate.enter();
    }
};

SingleWorkerDownloadCheckpointIo::Factory factory(const std::shared_ptr<State>& state) {
    auto lifetime = std::make_shared<FactoryLifetime>();
    lifetime->state = state;
    return [state, lifetime = std::move(lifetime)]() -> std::unique_ptr<DownloadCheckpointStore> {
        state->record([](auto& value) {
            ++value.factory_calls;
            value.factory_thread = std::this_thread::get_id();
        });
        state->factory_gate.enter();
        if (state->factory_throws)
            throw std::runtime_error{"synthetic factory error"};
        if (state->factory_null)
            return nullptr;
        return std::make_unique<Store>(state);
    };
}

template <class F> bool await(F&& predicate) {
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    do {
        if (predicate())
            return true;
        std::this_thread::yield();
    } while (std::chrono::steady_clock::now() < deadline);
    return predicate();
}

std::optional<DownloadCheckpointIoResult> completion(DownloadCheckpointIo& io, std::uint64_t id) {
    std::optional<DownloadCheckpointIoResult> result;
    (void)await([&] {
        result = io.poll(id);
        return result.has_value();
    });
    return result;
}

void closeAndCheck(SingleWorkerDownloadCheckpointIo& io, const std::shared_ptr<State>& state) {
    io.shutdown();
    REQUIRE(await([&] { return io.stopped(); }));
    CHECK_FALSE(state->factory_gate.expired());
    CHECK_FALSE(state->load_gate.expired());
    CHECK_FALSE(state->save_gate.expired());
    CHECK_FALSE(state->store_destruction_gate.expired());
    CHECK_FALSE(state->factory_destruction_gate.expired());
}
} // namespace

TEST_CASE("Checkpoint worker performs factory load save and destruction off the caller", "[download][checkpoint-io]") {
    const auto state = std::make_shared<State>();
    const auto caller = std::this_thread::get_id();
    SingleWorkerDownloadCheckpointIo io(factory(state));
    ReleaseGates release{state};
    REQUIRE(io.startLoad(1) == DownloadCheckpointIoCommand::Accepted);
    const auto loaded = completion(io, 1);
    REQUIRE(loaded.has_value());
    CHECK(loaded->operation_id == 1);
    CHECK(loaded->operation == DownloadCheckpointOperation::Load);
    CHECK(loaded->load.status == DownloadCheckpointLoadStatus::Loaded);
    CHECK(loaded->load.checkpoint == checkpoint());
    REQUIRE(io.startSave(2, checkpoint()) == DownloadCheckpointIoCommand::Accepted);
    const auto saved = completion(io, 2);
    REQUIRE(saved.has_value());
    CHECK(saved->operation_id == 2);
    CHECK(saved->operation == DownloadCheckpointOperation::Save);
    CHECK(saved->save_status == DownloadCheckpointSaveStatus::Saved);
    closeAndCheck(io, state);
    const auto observed = state->observed();
    CHECK(observed.factory_calls == 1);
    CHECK(observed.load_calls == 1);
    CHECK(observed.save_calls == 1);
    CHECK(observed.saved == checkpoint());
    CHECK(observed.factory_thread != caller);
    CHECK(observed.load_thread == observed.factory_thread);
    CHECK(observed.save_thread == observed.factory_thread);
    CHECK(observed.store_destruction_thread == observed.factory_thread);
    CHECK(observed.factory_destruction_thread == observed.factory_thread);
}

TEST_CASE("Checkpoint worker retains one unobserved completion and stale polling cannot consume it",
          "[download][checkpoint-io]") {
    const auto state = std::make_shared<State>();
    state->save_gate.block();
    state->store_destruction_gate.block();
    SingleWorkerDownloadCheckpointIo io(factory(state));
    ReleaseGates release{state};
    REQUIRE(io.startSave(10, checkpoint()) == DownloadCheckpointIoCommand::Accepted);
    REQUIRE(state->save_gate.awaitEntry());
    CHECK(io.startLoad(11) == DownloadCheckpointIoCommand::Busy);
    CHECK(io.startSave(12, checkpoint()) == DownloadCheckpointIoCommand::Busy);
    CHECK_FALSE(io.poll(10).has_value());
    CHECK_FALSE(io.poll(9).has_value());
    state->save_gate.release();
    io.shutdown();
    REQUIRE(state->store_destruction_gate.awaitEntry());
    // Reaching destruction proves native save has returned and published its
    // terminal completion without consuming that completion in the test.
    CHECK_FALSE(io.poll(9).has_value());
    CHECK_FALSE(io.poll(11).has_value());
    const auto result = io.poll(10);
    REQUIRE(result.has_value());
    CHECK(result->save_status == DownloadCheckpointSaveStatus::Saved);
    CHECK_FALSE(io.poll(10).has_value());
    CHECK(io.startLoad(11) == DownloadCheckpointIoCommand::Closed);
    state->store_destruction_gate.release();
    closeAndCheck(io, state);
}

TEST_CASE("Checkpoint worker leaves admission busy after native completion until matching poll",
          "[download][checkpoint-io]") {
    const auto state = std::make_shared<State>();
    state->save_gate.block();
    SingleWorkerDownloadCheckpointIo io(factory(state));
    ReleaseGates release{state};
    REQUIRE(io.startSave(1, checkpoint()) == DownloadCheckpointIoCommand::Accepted);
    REQUIRE(state->save_gate.awaitEntry());
    state->save_gate.release();
    // Busy is required both while active and after completion, so the check
    // cannot accidentally admit a later operation by racing the worker.
    CHECK(io.startLoad(2) == DownloadCheckpointIoCommand::Busy);
    REQUIRE(completion(io, 1).has_value());
    REQUIRE(io.startLoad(2) == DownloadCheckpointIoCommand::Accepted);
    REQUIRE(completion(io, 2).has_value());
    closeAndCheck(io, state);
    CHECK(state->observed().save_calls == 1);
    CHECK(state->observed().load_calls == 1);
}

TEST_CASE("Checkpoint worker accepts only nonzero increasing IDs without burning rejected IDs",
          "[download][checkpoint-io]") {
    const auto state = std::make_shared<State>();
    SingleWorkerDownloadCheckpointIo io(factory(state));
    ReleaseGates release{state};
    auto invalid = checkpoint();
    invalid.jobs[0].title.clear();
    CHECK(io.startLoad(0) == DownloadCheckpointIoCommand::InvalidRequest);
    CHECK(io.startSave(100, invalid) == DownloadCheckpointIoCommand::InvalidRequest);
    REQUIRE(io.startLoad(1) == DownloadCheckpointIoCommand::Accepted);
    CHECK(io.startLoad(100) == DownloadCheckpointIoCommand::Busy);
    REQUIRE(completion(io, 1).has_value());
    CHECK(io.startLoad(1) == DownloadCheckpointIoCommand::InvalidRequest);
    CHECK(io.startSave(0, checkpoint()) == DownloadCheckpointIoCommand::InvalidRequest);
    REQUIRE(io.startSave(2, checkpoint()) == DownloadCheckpointIoCommand::Accepted);
    REQUIRE(completion(io, 2).has_value());
    REQUIRE(io.startLoad(std::numeric_limits<std::uint64_t>::max()) == DownloadCheckpointIoCommand::Accepted);
    REQUIRE(completion(io, std::numeric_limits<std::uint64_t>::max()).has_value());
    CHECK(io.startLoad(3) == DownloadCheckpointIoCommand::InvalidRequest);
    closeAndCheck(io, state);
}

TEST_CASE("Checkpoint worker rejects invalid bounded snapshots before any store call", "[download][checkpoint-io]") {
    const auto invalid_kind = GENERATE(0, 1, 2, 3);
    auto invalid = checkpoint();
    if (invalid_kind == 0)
        invalid.jobs.resize(download_checkpoint_max_jobs + 1);
    else if (invalid_kind == 1)
        invalid.receipts.resize(download_checkpoint_max_receipts + 1);
    else if (invalid_kind == 2)
        invalid.jobs[0].title = std::string(download_checkpoint_max_bytes + 1, 'x');
    else
        invalid.receipts[0].fingerprint = "not-a-digest";
    REQUIRE_FALSE(validDownloadCheckpoint(invalid));
    const auto state = std::make_shared<State>();
    SingleWorkerDownloadCheckpointIo io(factory(state));
    ReleaseGates release{state};
    CHECK(io.startSave(50, invalid) == DownloadCheckpointIoCommand::InvalidRequest);
    REQUIRE(io.startSave(1, checkpoint()) == DownloadCheckpointIoCommand::Accepted);
    REQUIRE(completion(io, 1).has_value());
    closeAndCheck(io, state);
    CHECK(state->observed().save_calls == 1);
    CHECK(state->observed().load_calls == 0);
}

TEST_CASE("Checkpoint worker copies accepted snapshots before caller mutation", "[download][checkpoint-io]") {
    const auto state = std::make_shared<State>();
    state->factory_gate.block();
    SingleWorkerDownloadCheckpointIo io(factory(state));
    ReleaseGates release{state};
    auto input = checkpoint();
    REQUIRE(io.startSave(1, input) == DownloadCheckpointIoCommand::Accepted);
    REQUIRE(state->factory_gate.awaitEntry());
    input.jobs[0].title = "Changed only in caller";
    input.receipts.clear();
    state->factory_gate.release();
    REQUIRE(completion(io, 1).has_value());
    closeAndCheck(io, state);
    CHECK(state->observed().saved == checkpoint());
}

TEST_CASE("Checkpoint worker propagates coherent load states without fabricating a checkpoint",
          "[download][checkpoint-io]") {
    const auto status = GENERATE(DownloadCheckpointLoadStatus::Missing, DownloadCheckpointLoadStatus::Invalid,
                                 DownloadCheckpointLoadStatus::Unreadable, DownloadCheckpointLoadStatus::FutureVersion);
    const auto state = std::make_shared<State>();
    state->load_result = {.status = status};
    SingleWorkerDownloadCheckpointIo io(factory(state));
    ReleaseGates release{state};
    REQUIRE(io.startLoad(1) == DownloadCheckpointIoCommand::Accepted);
    const auto result = completion(io, 1);
    REQUIRE(result.has_value());
    CHECK(result->load.status == status);
    CHECK_FALSE(result->load.checkpoint.has_value());
    closeAndCheck(io, state);
}

TEST_CASE("Checkpoint worker fails closed on unknown or incoherent loaded data", "[download][checkpoint-io]") {
    const auto malformed_kind = GENERATE(0, 1, 2, 3, 4, 5, 6);
    const auto state = std::make_shared<State>();
    if (malformed_kind == 0)
        state->load_result = {.status = static_cast<DownloadCheckpointLoadStatus>(99)};
    else if (malformed_kind == 1)
        state->load_result = {.status = DownloadCheckpointLoadStatus::Loaded};
    else if (malformed_kind == 2)
        state->load_result.checkpoint->jobs[0].title.clear();
    else {
        constexpr DownloadCheckpointLoadStatus statuses[]{
            DownloadCheckpointLoadStatus::Missing, DownloadCheckpointLoadStatus::Invalid,
            DownloadCheckpointLoadStatus::Unreadable, DownloadCheckpointLoadStatus::FutureVersion};
        state->load_result.status = statuses[malformed_kind - 3];
    }
    // Kinds 3..6 carry data for Missing/Invalid/Unreadable/FutureVersion.
    SingleWorkerDownloadCheckpointIo io(factory(state));
    ReleaseGates release{state};
    REQUIRE(io.startLoad(1) == DownloadCheckpointIoCommand::Accepted);
    const auto result = completion(io, 1);
    REQUIRE(result.has_value());
    CHECK(result->load.status == DownloadCheckpointLoadStatus::Unreadable);
    CHECK_FALSE(result->load.checkpoint.has_value());
    closeAndCheck(io, state);
}

TEST_CASE("Checkpoint worker propagates save statuses and never reconciles a Failed write by readback",
          "[download][checkpoint-io]") {
    const auto status = GENERATE(DownloadCheckpointSaveStatus::Saved, DownloadCheckpointSaveStatus::Failed,
                                 DownloadCheckpointSaveStatus::BlockedByExistingData);
    const auto state = std::make_shared<State>();
    state->save_result = status;
    // load() would return the same supposedly committed bytes; it must not be
    // called to promote an ambiguous Failed status to Saved.
    SingleWorkerDownloadCheckpointIo io(factory(state));
    ReleaseGates release{state};
    REQUIRE(io.startSave(1, checkpoint()) == DownloadCheckpointIoCommand::Accepted);
    const auto result = completion(io, 1);
    REQUIRE(result.has_value());
    CHECK(result->save_status == status);
    CHECK(state->observed().load_calls == 0);
    closeAndCheck(io, state);
}

TEST_CASE("Checkpoint worker catches store exceptions and unknown save statuses", "[download][checkpoint-io]") {
    const auto operation = GENERATE(DownloadCheckpointOperation::Load, DownloadCheckpointOperation::Save);
    const auto state = std::make_shared<State>();
    state->load_throws = true;
    state->save_throws = true;
    SingleWorkerDownloadCheckpointIo io(factory(state));
    ReleaseGates release{state};
    REQUIRE((operation == DownloadCheckpointOperation::Load ? io.startLoad(1) : io.startSave(1, checkpoint())) ==
            DownloadCheckpointIoCommand::Accepted);
    const auto result = completion(io, 1);
    REQUIRE(result.has_value());
    CHECK(result->operation == operation);
    if (operation == DownloadCheckpointOperation::Load) {
        CHECK(result->load.status == DownloadCheckpointLoadStatus::Unreadable);
        CHECK_FALSE(result->load.checkpoint.has_value());
    } else
        CHECK(result->save_status == DownloadCheckpointSaveStatus::Failed);
    closeAndCheck(io, state);
}

TEST_CASE("Checkpoint worker fails closed on an unknown save status", "[download][checkpoint-io]") {
    const auto state = std::make_shared<State>();
    state->save_result = static_cast<DownloadCheckpointSaveStatus>(99);
    SingleWorkerDownloadCheckpointIo io(factory(state));
    ReleaseGates release{state};
    REQUIRE(io.startSave(1, checkpoint()) == DownloadCheckpointIoCommand::Accepted);
    const auto result = completion(io, 1);
    REQUIRE(result.has_value());
    CHECK(result->save_status == DownloadCheckpointSaveStatus::Failed);
    CHECK(state->observed().load_calls == 0);
    closeAndCheck(io, state);
}

TEST_CASE("Checkpoint worker maps null and throwing factories to typed terminal failures",
          "[download][checkpoint-io]") {
    const auto throws = GENERATE(false, true);
    const auto operation = GENERATE(DownloadCheckpointOperation::Load, DownloadCheckpointOperation::Save);
    const auto state = std::make_shared<State>();
    state->factory_throws = throws;
    state->factory_null = !throws;
    SingleWorkerDownloadCheckpointIo io(factory(state));
    ReleaseGates release{state};
    REQUIRE((operation == DownloadCheckpointOperation::Load ? io.startLoad(1) : io.startSave(1, checkpoint())) ==
            DownloadCheckpointIoCommand::Accepted);
    const auto result = completion(io, 1);
    REQUIRE(result.has_value());
    CHECK(result->operation_id == 1);
    CHECK(result->operation == operation);
    if (operation == DownloadCheckpointOperation::Load) {
        CHECK(result->load.status == DownloadCheckpointLoadStatus::Unreadable);
        CHECK_FALSE(result->load.checkpoint.has_value());
    } else
        CHECK(result->save_status == DownloadCheckpointSaveStatus::Failed);
    closeAndCheck(io, state);
    CHECK(state->observed().load_calls == 0);
    CHECK(state->observed().save_calls == 0);
    CHECK(state->observed().factory_thread != std::this_thread::get_id());
    CHECK(state->observed().factory_destruction_thread == state->observed().factory_thread);
}

TEST_CASE("Checkpoint worker accepts an empty factory but fails load safely", "[download][checkpoint-io]") {
    SingleWorkerDownloadCheckpointIo io({});
    REQUIRE(io.startLoad(1) == DownloadCheckpointIoCommand::Accepted);
    const auto result = completion(io, 1);
    REQUIRE(result.has_value());
    CHECK(result->load.status == DownloadCheckpointLoadStatus::Unreadable);
    io.shutdown();
    REQUIRE(await([&] { return io.stopped(); }));
}

TEST_CASE("Checkpoint worker shutdown is prompt while native save is gated and drains without preemption",
          "[download][checkpoint-io]") {
    const auto state = std::make_shared<State>();
    state->save_gate.block();
    SingleWorkerDownloadCheckpointIo io(factory(state));
    ReleaseGates release{state};
    REQUIRE(io.startSave(1, checkpoint()) == DownloadCheckpointIoCommand::Accepted);
    REQUIRE(state->save_gate.awaitEntry());
    io.shutdown();
    io.shutdown();
    CHECK_FALSE(state->save_gate.expired());
    CHECK_FALSE(io.stopped());
    CHECK(io.startLoad(2) == DownloadCheckpointIoCommand::Closed);
    auto invalid = checkpoint();
    invalid.jobs[0].title.clear();
    CHECK(io.startSave(2, invalid) == DownloadCheckpointIoCommand::Closed);
    CHECK_FALSE(io.poll(1).has_value());
    state->save_gate.release();
    REQUIRE(await([&] { return io.stopped(); }));
    const auto result = io.poll(1);
    REQUIRE(result.has_value());
    CHECK(result->save_status == DownloadCheckpointSaveStatus::Saved);
    CHECK_FALSE(io.poll(1).has_value());
    closeAndCheck(io, state);
    CHECK(state->observed().save_calls == 1);
    CHECK(state->observed().load_calls == 0);
}

TEST_CASE("Checkpoint worker cancels pre-I-O shutdown without executing the store", "[download][checkpoint-io]") {
    const auto operation = GENERATE(DownloadCheckpointOperation::Load, DownloadCheckpointOperation::Save);
    const auto state = std::make_shared<State>();
    state->factory_gate.block();
    SingleWorkerDownloadCheckpointIo io(factory(state));
    ReleaseGates release{state};
    REQUIRE((operation == DownloadCheckpointOperation::Load ? io.startLoad(1) : io.startSave(1, checkpoint())) ==
            DownloadCheckpointIoCommand::Accepted);
    REQUIRE(state->factory_gate.awaitEntry());
    io.shutdown();
    CHECK_FALSE(state->factory_gate.expired());
    CHECK_FALSE(io.stopped());
    state->factory_gate.release();
    REQUIRE(await([&] { return io.stopped(); }));
    const auto result = io.poll(1);
    REQUIRE(result.has_value());
    CHECK(result->operation_id == 1);
    CHECK(result->operation == operation);
    if (operation == DownloadCheckpointOperation::Load)
        CHECK(result->load.status == DownloadCheckpointLoadStatus::Unreadable);
    else
        CHECK(result->save_status == DownloadCheckpointSaveStatus::Failed);
    CHECK(state->observed().save_calls == 0);
    CHECK(state->observed().load_calls == 0);
    closeAndCheck(io, state);
}

TEST_CASE("Checkpoint worker stopped requires both store and factory resource destruction",
          "[download][checkpoint-io]") {
    const auto state = std::make_shared<State>();
    state->store_destruction_gate.block();
    state->factory_destruction_gate.block();
    SingleWorkerDownloadCheckpointIo io(factory(state));
    ReleaseGates release{state};
    REQUIRE(io.startLoad(1) == DownloadCheckpointIoCommand::Accepted);
    REQUIRE(completion(io, 1).has_value());
    io.shutdown();
    REQUIRE(state->store_destruction_gate.awaitEntry());
    CHECK_FALSE(io.stopped());
    state->store_destruction_gate.release();
    REQUIRE(state->factory_destruction_gate.awaitEntry());
    CHECK_FALSE(io.stopped());
    state->factory_destruction_gate.release();
    closeAndCheck(io, state);
}

TEST_CASE("Checkpoint worker destroys unused factory resources off caller before reporting stopped",
          "[download][checkpoint-io]") {
    const auto state = std::make_shared<State>();
    state->factory_destruction_gate.block();
    SingleWorkerDownloadCheckpointIo io(factory(state));
    ReleaseGates release{state};
    io.shutdown();
    CHECK(io.startLoad(1) == DownloadCheckpointIoCommand::Closed);
    REQUIRE(state->factory_destruction_gate.awaitEntry());
    CHECK_FALSE(io.stopped());
    CHECK(state->observed().factory_destruction_thread != std::this_thread::get_id());
    state->factory_destruction_gate.release();
    closeAndCheck(io, state);
    CHECK(state->observed().load_calls == 0);
    CHECK(state->observed().save_calls == 0);
}

} // namespace music_player::m0::test
