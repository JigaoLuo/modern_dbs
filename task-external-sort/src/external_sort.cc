#include "moderndbs/external_sort.h"
#include "moderndbs/file.h"

#include <iostream>
#include <algorithm>
#include <utility>
#include <queue>
#include <memory>

namespace moderndbs {

namespace {
// Helpers in anonymous namespace
static constexpr size_t VALUE_SIZE     = sizeof(uint64_t); /// Assumption: Sort only 64 bit unsigned integers.
static_assert(VALUE_SIZE == 8);
static size_t NUM_IO_READS             = 0;                /// Benchmark metric.
static size_t NUM_IO_WRITES            = 0;                /// Benchmark metric.

// https://www.cplusplus.com/reference/utility/pair/operators/, https://en.cppreference.com/w/cpp/utility/pair/operator_cmp default std::pair < operator checks the **pair.first** value first
using Value = uint64_t;
using Input_Buffer_Index = uint64_t;
using PQ_Element = std::pair<Value, Input_Buffer_Index>;

/// Check if 64 bit unsigned integers sorted using std::is_sorted.
/// Attention: this function is using more than mem_size memory, and this function
///            is only called in assertion.
/// @param[in] tmp_file   Tmp_File that contains 64 bit unsigned integers which are
///                       stored as 8-byte little-endian values.
/// @param[in] num_values The number of integers that should be sorted from the
///                       input.
/// @param[in] run_size   The size of run
bool sort_phase_done(File *tmp_file, size_t num_values, size_t run_size) {
    const size_t INPUT_SORT_SIZE = num_values * VALUE_SIZE;  /// Size in memory, in bytes.
    assert(tmp_file->size() == INPUT_SORT_SIZE);
    const size_t NUM_RUNS = (INPUT_SORT_SIZE - 1) / run_size + 1;  /// (x - 1) / y + 1 := to do the ceil.
    const size_t NUM_VALUES_RUN = run_size / VALUE_SIZE;

    /// the size of last run. if last is full, then with mem_size.
    const size_t LAST_RUN_SIZE = (INPUT_SORT_SIZE == NUM_RUNS * run_size) ? run_size : INPUT_SORT_SIZE - (NUM_RUNS - 1) * run_size;
    assert(LAST_RUN_SIZE <= run_size);
    /// number of values in the last run
    assert(LAST_RUN_SIZE % VALUE_SIZE == 0);
    const size_t NUM_VALUES_LAST_RUN = LAST_RUN_SIZE / VALUE_SIZE;

    auto tmp_file_data = tmp_file->read_block(0, INPUT_SORT_SIZE);
    auto *tmp_file_data_ub8 = reinterpret_cast<Value *>(tmp_file_data.get());

    size_t offset = 0;
    bool result = true;
    for (size_t i = 0; i < NUM_RUNS - 1; i++, offset += run_size) {
        result &= std::is_sorted(tmp_file_data_ub8 + i * NUM_VALUES_RUN, tmp_file_data_ub8 + i * NUM_VALUES_RUN + NUM_VALUES_RUN);
        assert(result);
    }
    result &= std::is_sorted(tmp_file_data_ub8 + (NUM_RUNS - 1) * NUM_VALUES_RUN, tmp_file_data_ub8 + (NUM_RUNS - 1) * NUM_VALUES_RUN + NUM_VALUES_LAST_RUN);
    assert(result);
    return result;
}

/// Not Full Merge := with intermediate passes and terrible I/O number, since memory size too little.
/// This function use 2-way merge with code structure similar as k-way merge.
void not_full_way_merge(File *tmp_file, size_t num_values, size_t mem_size,
                        size_t &NUM_RUNS, size_t &NUM_VALUES_RUN,
                        size_t &NUM_VALUES_LAST_RUN, size_t &RUN_SIZE,
                        size_t &LAST_RUN_SIZE) {
    /// const fan-in := 2-way merge
    static constexpr size_t FAN_IN = 2;
    const size_t NUM_VALUE_INPUT_BUFFER = (mem_size / 3) / VALUE_SIZE;
    const size_t INPUT_BUFFER_SIZE = NUM_VALUE_INPUT_BUFFER * VALUE_SIZE;
    assert(NUM_VALUE_INPUT_BUFFER > 0);
    assert(INPUT_BUFFER_SIZE % 8 == 0);
    const size_t NUM_VALUE_OUTPUT_BUFFER_ = (mem_size - FAN_IN * INPUT_BUFFER_SIZE) / VALUE_SIZE;
    const size_t OUT_BUFFER_SIZE = NUM_VALUE_OUTPUT_BUFFER_ * VALUE_SIZE;
    assert(NUM_VALUE_OUTPUT_BUFFER_ > 0);
    assert(OUT_BUFFER_SIZE % 8 == 0);
    while (NUM_RUNS > FAN_IN) {
        const size_t num_iterations_in_pass = NUM_RUNS / FAN_IN;
        for (size_t it = 0; it < num_iterations_in_pass; it++) {
            size_t output_buffer_current_num_values = 0;
            std::array<size_t, FAN_IN> next_read_offset_run{0};
            std::array<size_t, FAN_IN> next_push_index_input_buffer{0};
            std::array<size_t, FAN_IN> remaining_num_values_run{0};
            for (size_t i = 0; i < FAN_IN; i++) {
                next_read_offset_run[i] = INPUT_BUFFER_SIZE;
                next_push_index_input_buffer[i] = 1;
                remaining_num_values_run[i] = NUM_VALUES_RUN;
            }
            size_t odd_run_num_adjustment = 0;
            if (it == num_iterations_in_pass - 1) {
                remaining_num_values_run[FAN_IN - 1] = NUM_VALUES_LAST_RUN;
                if (NUM_RUNS % FAN_IN != 0) {
                    /// if NUM_RUNS is not even, so can't be 2-way merged.
                    /// In this case we process the last 3 runs together := Full Run A, Full Run B, the Last Run merge Full Run B and the last Run into single Run first, then we have even NUM_RUNS
                    odd_run_num_adjustment = RUN_SIZE;
                }
            }
            auto memory = std::make_unique<char[]>(mem_size);
            char *memory_ptr = reinterpret_cast<char *>(memory.get());
            std::array<Value*, FAN_IN> input_buffer_base_ptrs{0};
            auto *output_buffer_base_ptr = reinterpret_cast<Value *>(memory_ptr + INPUT_BUFFER_SIZE * FAN_IN);

            /// intermediate_file only used in this iteration
            auto intermediate_file = File::make_temporary_file();
            if (it == num_iterations_in_pass - 1) {
                intermediate_file->resize((FAN_IN - 1) * RUN_SIZE + LAST_RUN_SIZE);
            } else {
                intermediate_file->resize(FAN_IN * RUN_SIZE);
            }
            NUM_IO_WRITES++;
            for (size_t i = 0, buffer_offset = 0, tmpfile_offset = RUN_SIZE * FAN_IN * it + odd_run_num_adjustment; i < FAN_IN; i++, buffer_offset += INPUT_BUFFER_SIZE, tmpfile_offset += RUN_SIZE) {
                input_buffer_base_ptrs[i] = reinterpret_cast<Value *>(memory_ptr + buffer_offset);
                tmp_file->read_block(tmpfile_offset, INPUT_BUFFER_SIZE, memory_ptr + buffer_offset);
                NUM_IO_READS++;
            }

            /// inputs_compare for 2-way merge, in this simple case priority queue is not necessary:
            /// [0]                    | [1]
            /// first input buffer     | second input buffer
            std::array<Value*, FAN_IN> inputs_compare{input_buffer_base_ptrs[0], input_buffer_base_ptrs[1]};
            size_t num_full_writes = 0;
            while (inputs_compare[0] != nullptr || inputs_compare[1] != nullptr) {
                /// the less value side index
                const Input_Buffer_Index ib_index = [&] {
                  if (inputs_compare[0] == nullptr) {
                      return 1;
                  } else if (inputs_compare[1] == nullptr) {
                      return 0;
                  } else {
                      return (*inputs_compare[0] < *inputs_compare[1]) ? 0 : 1;
                  }
                } ();
                *(output_buffer_base_ptr + output_buffer_current_num_values++) = *inputs_compare[ib_index];

                remaining_num_values_run[ib_index]--;
                if (output_buffer_current_num_values == NUM_VALUE_OUTPUT_BUFFER_) {
                    assert(std::is_sorted(output_buffer_base_ptr, output_buffer_base_ptr + NUM_VALUE_OUTPUT_BUFFER_));
                    intermediate_file->write_block(reinterpret_cast<char *>(output_buffer_base_ptr), (num_full_writes++) * OUT_BUFFER_SIZE, OUT_BUFFER_SIZE);
                    NUM_IO_WRITES++;
                    output_buffer_current_num_values = 0;
                }
                if (remaining_num_values_run[ib_index] == 0) {
                    inputs_compare[ib_index] = nullptr;
                    continue;
                }
                const bool if_last_run = it == num_iterations_in_pass - 1 && ib_index == (FAN_IN - 1);
                if (next_push_index_input_buffer[ib_index] == NUM_VALUE_INPUT_BUFFER) {
                    if (if_last_run ? next_read_offset_run[ib_index] + INPUT_BUFFER_SIZE >= LAST_RUN_SIZE : next_read_offset_run[ib_index] + INPUT_BUFFER_SIZE >= RUN_SIZE) {
                        /// last load from this run
                        const size_t last_load_size = (if_last_run ? LAST_RUN_SIZE - next_read_offset_run[ib_index] : RUN_SIZE - next_read_offset_run[ib_index]);
                        assert(last_load_size % 8 == 0);
                        tmp_file->read_block(next_read_offset_run[ib_index] + RUN_SIZE * ib_index + RUN_SIZE * FAN_IN * it + odd_run_num_adjustment, last_load_size, reinterpret_cast<char *>(input_buffer_base_ptrs[ib_index]));
                        assert(std::is_sorted(input_buffer_base_ptrs[ib_index], input_buffer_base_ptrs[ib_index] + (last_load_size / VALUE_SIZE)));
                        next_read_offset_run[ib_index] += last_load_size;
                    } else {
                        /// Load from run (not the last load)
                        tmp_file->read_block(next_read_offset_run[ib_index] + RUN_SIZE * ib_index + RUN_SIZE * FAN_IN * it + odd_run_num_adjustment, INPUT_BUFFER_SIZE, reinterpret_cast<char *>(input_buffer_base_ptrs[ib_index]));
                        assert(std::is_sorted(input_buffer_base_ptrs[ib_index], input_buffer_base_ptrs[ib_index] + NUM_VALUE_INPUT_BUFFER));
                        next_read_offset_run[ib_index] += INPUT_BUFFER_SIZE;
                    }
                    NUM_IO_READS++;
                    next_push_index_input_buffer[ib_index] = 0;
                }
                inputs_compare[ib_index] = input_buffer_base_ptrs[ib_index] + next_push_index_input_buffer[ib_index]++;
            }

            const size_t intermediate_file_size = intermediate_file->size();
            const size_t num_full_write = intermediate_file_size / OUT_BUFFER_SIZE;
            if (intermediate_file_size > OUT_BUFFER_SIZE * num_full_write) {
                assert(std::is_sorted(output_buffer_base_ptr, output_buffer_base_ptr + ((intermediate_file_size - OUT_BUFFER_SIZE * num_full_write) / VALUE_SIZE)));
                intermediate_file->write_block(reinterpret_cast<char *>(output_buffer_base_ptr), num_full_writes * OUT_BUFFER_SIZE, intermediate_file_size - OUT_BUFFER_SIZE * num_full_write);
                NUM_IO_WRITES++;
            }

            /// free this buffer
            memory.reset();
            /// buffer this intermediate, copy it's content into tem file
            /// Attention: This copy uses maybe more than mem_size memory XD
            auto intermediate_data = intermediate_file->read_block(0, intermediate_file_size);
            NUM_IO_READS++;
            assert(std::is_sorted(reinterpret_cast<Value *>(intermediate_data.get()), reinterpret_cast<Value *>(intermediate_data.get()) + (intermediate_file_size / VALUE_SIZE)));
            tmp_file->write_block(reinterpret_cast<char *>(intermediate_data.get()), RUN_SIZE * FAN_IN * it + odd_run_num_adjustment, intermediate_file_size);
            NUM_IO_WRITES++;
            /// Ff odd, merge two runs into one. Then we have even run number
            if (it == num_iterations_in_pass - 1 && NUM_RUNS % FAN_IN != 0) {
                assert(odd_run_num_adjustment == RUN_SIZE);
                NUM_RUNS--;
                it--;
                LAST_RUN_SIZE += RUN_SIZE;
                NUM_VALUES_LAST_RUN += NUM_VALUES_RUN;
                continue;
            }
        }
        NUM_RUNS /= FAN_IN;
        LAST_RUN_SIZE += (RUN_SIZE * (FAN_IN - 1));
        NUM_VALUES_LAST_RUN += (NUM_VALUES_RUN * (FAN_IN - 1));
        RUN_SIZE *= FAN_IN;
        NUM_VALUES_RUN *= FAN_IN;
        assert(sort_phase_done(tmp_file, num_values, RUN_SIZE));
    }
}

/// Sorts 64 bit unsigned integers using in-memory std::sort.
/// @param[in] input      File that contains 64 bit unsigned integers which are
///                       stored as 8-byte little-endian values. This file may
///                       be in `READ` mode and should not be written to.
/// @param[in] num_values The number of integers that should be sorted from the
///                       input.
/// @param[in] output     File that should contain the sorted values in the
///                       end. This file must be in `WRITE` mode.
void inmemory_sort(File &input, size_t num_values, File &output) {
    const size_t INPUT_SORT_SIZE = num_values * VALUE_SIZE;
    const auto input_file_buffer = input.read_block(0, INPUT_SORT_SIZE);
    NUM_IO_READS++;
    auto *const input_values = reinterpret_cast<Value *>(input_file_buffer.get());
    std::sort(input_values, input_values + num_values);
    output.write_block(input_file_buffer.get(), 0, INPUT_SORT_SIZE);
    NUM_IO_WRITES++;
    std::cout << "LOGGING OUTPUT: IN-MEMORY SORTING." << std::endl;
    std::cout << "NUM_IO_READS: " << NUM_IO_READS
              << ", NUM_IO_WRITES: " << NUM_IO_WRITES
              << ", NUM_IO: " << NUM_IO_READS + NUM_IO_WRITES << std::endl;
}

} // namespace

void external_sort(File& input, size_t num_values, File& output, size_t mem_size) {
    /// 0. Init
    /// 0.1. Assumption: the mem_size is multiple time of 8
    assert(mem_size % VALUE_SIZE == 0);
    /// 0.2. Check files mode, and if opened
    assert(input.get_mode() == File::Mode::READ);
    assert(output.get_mode() == File::Mode::WRITE);
    /// 0.3. Input file should have more or equal the size of sorting values
    const size_t INPUT_SORT_SIZE = num_values * VALUE_SIZE;
    assert(input.size() >= INPUT_SORT_SIZE);
    output.resize(INPUT_SORT_SIZE);

    /// 1. Edge cases
    /// 1.1. Edge Case: no values to sort, return directly
    if (num_values == 0) {
        std::cout << "LOGGING OUTPUT: NO VALUE FOR SORTING." << std::endl;
        return;
    }
    /// 1.2. Edges Case: fits actually into main memory
    if (INPUT_SORT_SIZE <= mem_size) {
        return inmemory_sort(input, num_values, output);
    }

    /// -------------------------------------------------------------------------------------------------------------------------------------------
    /// ------------------------------------------------------------External Merge-Sort------------------------------------------------------------
    /// -------------------------------------------------------------------------------------------------------------------------------------------

    /// -------------------------------------------------------------------------------
    /// ------------------------------------SORTING------------------------------------
    /// -------------------------------------------------------------------------------

    /// 2. Sort each run having the size of mem_size
    /**
      *  tmp file / input file := a set of runs
      *
      *  offset in file:
      *  (n stands for NUM_RUNS)
      *
      *  0                    mem_size                mem_size*2              mem_size*3              mem_size*(n-1)
      *  ^                       ^                       ^                       ^                       ^
      *  ---------------------------------------------------------------------------------------------------------
      *  |         Run 0         |         Run 1         |         Run 2         |          ...          |Run n-1|
      *  ---------------------------------------------------------------------------------------------------------
      *                                                                                                  ^
      *                                                                                                  the last run may be not full
      */

    /// 2.1. Init sorting phase
    /// number of runs, the last one maybe not full => so ceil
    size_t NUM_RUNS = (INPUT_SORT_SIZE - 1) / mem_size + 1;
    /// the size of run.
    size_t RUN_SIZE = mem_size;
    /// number of values in normal run (not the last run)
    size_t NUM_VALUES_RUN = mem_size / VALUE_SIZE;
    /// the size of last run. if last is full, then with mem_size.
    size_t LAST_RUN_SIZE = (INPUT_SORT_SIZE == NUM_RUNS * RUN_SIZE) ? RUN_SIZE : INPUT_SORT_SIZE - (NUM_RUNS - 1) * RUN_SIZE;
    assert(LAST_RUN_SIZE <= mem_size);
    /// number of values in the last run
    assert(LAST_RUN_SIZE % VALUE_SIZE == 0);
    size_t NUM_VALUES_LAST_RUN = LAST_RUN_SIZE / VALUE_SIZE;

    /// 2.2. New a tmp file having same size as INPUT_SORT_SIZE
    auto tmp_file = File::make_temporary_file();
    tmp_file->resize(INPUT_SORT_SIZE);
    NUM_IO_WRITES++;

    /// 2.3. Sort each runs having full utilized mem_size
    size_t offset = 0;
    for (size_t i = 0; i < NUM_RUNS - 1; i++, offset += RUN_SIZE) {
        /// 2.3.1. Read a run (but not the last one) from input file
        const auto input_file_buffer = input.read_block(offset, RUN_SIZE);
        NUM_IO_READS++;
        /// 2.3.2. Sort it
        auto *input_values = reinterpret_cast<Value*>(input_file_buffer.get());
        std::sort(input_values, input_values + NUM_VALUES_RUN);
        /// 2.3.3. Write this sorted run out to tmp file
        tmp_file->write_block(reinterpret_cast<char*>(input_values), offset, RUN_SIZE);
        NUM_IO_WRITES++;
    }

    /// 2.3.4. Read the last run from input file
    const auto input_file_buffer = input.read_block(offset, LAST_RUN_SIZE);
    NUM_IO_READS++;
    /// 2.3.5. Sort it
    auto *input_values = reinterpret_cast<Value*>(input_file_buffer.get());
    std::sort(input_values, input_values + NUM_VALUES_LAST_RUN);
    /// 2.3.6. Write this sorted last run out to tmp file
    tmp_file->write_block(input_file_buffer.get(), offset, LAST_RUN_SIZE);
    NUM_IO_WRITES++;

    /// 2.3.7. Check if sorted in tmp file => this function use more than mem_size memory for checking
    assert(sort_phase_done(tmp_file.get(), num_values, RUN_SIZE));



    /// -------------------------------------------------------------------------------
    /// ------------------------------------MERGING------------------------------------
    /// -------------------------------------------------------------------------------
    /// Min is 3 := 2 for input, 1 for output | Less than 3 not possible for merging phase
    assert((mem_size / VALUE_SIZE) >= 3);

    /// 3. Not Full Merge := with intermediate passes and terrible I/O number, since memory size too little
    ///    mem_size can not be devided into (NUM_RUNS + 1) parts ==> FAN_IN can't be NUM_RUNS
    ///    try to divide mem_size into FAN_IN parts
    if ((mem_size / (NUM_RUNS + 1)) / VALUE_SIZE == 0) {
        std::cout << "LOGGING OUTPUT: NOT FULL K WAY SORTING." << std::endl;
        not_full_way_merge(tmp_file.get(), num_values, mem_size, NUM_RUNS, NUM_VALUES_RUN, NUM_VALUES_LAST_RUN, RUN_SIZE, LAST_RUN_SIZE);
    } else {
        std::cout << "LOGGING OUTPUT: FULL K WAY SORTING." << std::endl;
    }

    /// 4. Full K-way Merge := partitioning mem_size into K input buffers and 1 output buffer without any intermediate passes
    /**
      *  mem_size := a set of K input buffers and 1 output buffer
      *
      *  IN HEAP ALLOCATED
      *  (n stands for NUM_RUNS)
      *
      *  0           buffer_size       buffer_size*2     buffer_size*3    buffer_size*(n-1)  buffer_size*n            mem_size
      *  ^                 ^                 ^                 ^                 ^                 ^                  ^
      *  --------------------------------------------------------------------------------------------------------------
      *  |   Run 0 Buffer  |   Run 1 Buffer  |   Run 2 Buffer  |   ............  |   Run n Buffer  |   Output Buffer  |
      *  --------------------------------------------------------------------------------------------------------------
      *  ^                                                                                         ^
      *  | <-----------------------------------Input Buffer--------------------------------------->|
      *
      */

    /// 4.1 Init Merge phase.
    const size_t FAN_IN = NUM_RUNS;
    /// 4.1.1. Get the buffer size and number values in buffer.
    /// Make sure Input Buffer Size is multiple time of 8.
    const size_t NUM_VALUE_INPUT_BUFFER = (mem_size / (FAN_IN + 1)) / VALUE_SIZE;
    const size_t INPUT_BUFFER_SIZE = NUM_VALUE_INPUT_BUFFER * VALUE_SIZE;
    assert(NUM_VALUE_INPUT_BUFFER > 0);
    assert(INPUT_BUFFER_SIZE > 0);
    assert(INPUT_BUFFER_SIZE % 8 == 0);
    /// Make sure Output Buffer Size is multiple time of 8.
    const size_t NUM_VALUE_OUTPUT_BUFFER = (mem_size - FAN_IN * INPUT_BUFFER_SIZE) / VALUE_SIZE;
    const size_t OUT_BUFFER_SIZE = NUM_VALUE_OUTPUT_BUFFER * VALUE_SIZE;
    assert(OUT_BUFFER_SIZE > 0);
    assert(OUT_BUFFER_SIZE % 8 == 0);
    /// 4.1.2. Make sure heap usage not to exceed the mem_size.
    assert(INPUT_BUFFER_SIZE * FAN_IN + OUT_BUFFER_SIZE <= mem_size);

    /// 4.1.3. Bookkeeping Values + std::vectors
    /// for output buffer size, for reading file, for input buffer index, for remaining number in runs.

    /// output_buffer_current_num_values := current size of output buffer, value range is [0, OUT_BUFFER_SIZE).
    size_t output_buffer_current_num_values = 0;
    /// next_read_offset_run             := next byte-offset to read of each run, assuming first read loaded into input buffer.
    /// Improvement after grading: Never use a variable-sized array! Use a std::vector.
    std::vector<size_t> next_read_offset_run(FAN_IN, 0);
    /// next_push_index_input_buffer     := next uint64-index to push into priority heap of each input buffer, assuming first read loaded into priority queue.

    /// Improvement after grading: Never use a variable-sized array! Use a std::vector allocated at Heap.
    /// value range is [0, NUM_VALUE_INPUT_BUFFER)
    std::vector<size_t> next_push_index_input_buffer(FAN_IN, 0);
    /// remaining_num_values_run         := remaining values to be output-ed in each run.
    /// Improvement after grading: Never use a variable-sized array! Use a std::vector.
    std::vector<size_t> remaining_num_values_run(FAN_IN, 0);
    for (size_t i = 0; i < FAN_IN - 1; i++) {
        next_read_offset_run[i] = INPUT_BUFFER_SIZE;
        next_push_index_input_buffer[i] = 1;
        remaining_num_values_run[i] = NUM_VALUES_RUN;
    }
    next_read_offset_run[FAN_IN - 1] = INPUT_BUFFER_SIZE;
    next_push_index_input_buffer[FAN_IN - 1] = 1;
    remaining_num_values_run[FAN_IN - 1] = NUM_VALUES_LAST_RUN;

    /// 4.1.4. Allocate mem_size memory, and pick out base point array to each input buffer
    auto memory = std::make_unique<char[]>(mem_size);
    char *memory_ptr = reinterpret_cast<char*>(memory.get());
    /// Improvement after grading: Never use a variable-sized array! Use a std::vector allocated at Heap.
    std::vector<Value*> input_buffer_base_ptrs(FAN_IN, nullptr);
    auto *output_buffer_base_ptr = reinterpret_cast<Value*>(memory_ptr + INPUT_BUFFER_SIZE * FAN_IN);

    /// 4.1.5. Load NUM_VALUE_INPUT_BUFFER values into read buffers
    for (size_t i = 0, buffer_offset = 0, tmpfile_offset = 0; i < FAN_IN; i++, buffer_offset += INPUT_BUFFER_SIZE, tmpfile_offset += RUN_SIZE) {
        input_buffer_base_ptrs[i] = reinterpret_cast<Value*>(memory_ptr + buffer_offset);
        tmp_file->read_block(tmpfile_offset, INPUT_BUFFER_SIZE, memory_ptr + buffer_offset);
        NUM_IO_READS++;
    }

    /// 4.1.6. Figure := After first time reading into input buffers
    /**
      *  tmp file := a set of runs
      *
      *  offset in file:
      *  (n stands for NUM_RUNS)
      *  (--- := loaded into input buffer)
      *
      *  0                    mem_size                mem_size*2              mem_size*3             mem_size*(n-1)
      *  ^                       ^                       ^                       ^                      ^
      *  -----------------------------------------------------------------------------------------------------------
      *  |---      Run 0         |---      Run 1         |---      Run 2         |---      ...          |---Run n-1|
      *  -----------------------------------------------------------------------------------------------------------
      *                                                                                                 ^
      *                                                                                                 the last run may be not full
      */

    /// 4.1.7. Init a priority queue, add the first value of each Input Buffer.
    std::priority_queue<PQ_Element, std::vector<PQ_Element>, std::greater<PQ_Element>> pq;  // NOLINT
    for (size_t i = 0; i < FAN_IN; i++) {
        pq.emplace(*(input_buffer_base_ptrs[i]), i);
    }

    /// 4.2. Merge using while looping a std::priority_queue.
    size_t num_full_writes = 0;
    while (!pq.empty()) {
        /// 4.2.1. Pop min value
        const PQ_Element top = pq.top();
        pq.pop();
        /// 4.2.1. Write it into output buffer
        *(output_buffer_base_ptr + output_buffer_current_num_values++) = top.first;
        /// 4.2.3. Locate which run the min comes from.
        const Input_Buffer_Index ib_index = top.second;
        /// 4.2.4. Decrement remaining values in this run.
        remaining_num_values_run[ib_index]--;

        /// 4.2.5. If full in output buffer, write out to output file.
        if (output_buffer_current_num_values == NUM_VALUE_OUTPUT_BUFFER) {
            assert(std::is_sorted(output_buffer_base_ptr, output_buffer_base_ptr + NUM_VALUE_OUTPUT_BUFFER));
            output.write_block(reinterpret_cast<char*>(output_buffer_base_ptr), (num_full_writes++) * OUT_BUFFER_SIZE, OUT_BUFFER_SIZE);
            NUM_IO_WRITES++;
            output_buffer_current_num_values = 0;
        }

        /// 4.2.6. This run is done / empty.
        if (remaining_num_values_run[ib_index] == 0) {
            continue;
        }

        /// 4.2.7. If done / all-used-up in input buffer, read next part of run into input run.
        if (next_push_index_input_buffer[ib_index] == NUM_VALUE_INPUT_BUFFER) {
            const bool if_last_run = ib_index == (FAN_IN - 1);
            if (if_last_run ? next_read_offset_run[ib_index] + INPUT_BUFFER_SIZE >= LAST_RUN_SIZE : next_read_offset_run[ib_index] + INPUT_BUFFER_SIZE >= RUN_SIZE) {
                /// last load from this run.
                const size_t last_load_size = if_last_run ? LAST_RUN_SIZE - next_read_offset_run[ib_index] : RUN_SIZE - next_read_offset_run[ib_index];
                assert(last_load_size % 8 == 0);
                tmp_file->read_block(next_read_offset_run[ib_index] + RUN_SIZE * ib_index, last_load_size, reinterpret_cast<char*>(input_buffer_base_ptrs[ib_index]));
                assert(std::is_sorted(input_buffer_base_ptrs[ib_index], input_buffer_base_ptrs[ib_index] + (last_load_size / VALUE_SIZE)));
                next_read_offset_run[ib_index] += last_load_size;
            } else {
                /// Load from run (not the last load).
                tmp_file->read_block(next_read_offset_run[ib_index] + RUN_SIZE * ib_index, INPUT_BUFFER_SIZE, reinterpret_cast<char*>(input_buffer_base_ptrs[ib_index]));
                assert(std::is_sorted(input_buffer_base_ptrs[ib_index], input_buffer_base_ptrs[ib_index] + NUM_VALUE_INPUT_BUFFER));
                next_read_offset_run[ib_index] += INPUT_BUFFER_SIZE;
            }
            NUM_IO_READS++;
            next_push_index_input_buffer[ib_index] = 0;
        }
        /// 4.2.8. Load next one from this input buffer to the std::priority_queue.
        pq.emplace(*(input_buffer_base_ptrs[ib_index] + next_push_index_input_buffer[ib_index]++), ib_index);
    }
    /// 4.2.9 The std::priority_queue is empty. But output buffer maybe still have to write out from output buffer.
    const size_t num_full_write = INPUT_SORT_SIZE / OUT_BUFFER_SIZE;
    if (INPUT_SORT_SIZE > OUT_BUFFER_SIZE * num_full_write) {
        assert(std::is_sorted(output_buffer_base_ptr, output_buffer_base_ptr + ((INPUT_SORT_SIZE - OUT_BUFFER_SIZE * num_full_write) / VALUE_SIZE)));
        output.write_block(reinterpret_cast<char*>(output_buffer_base_ptr), num_full_writes * OUT_BUFFER_SIZE, INPUT_SORT_SIZE - OUT_BUFFER_SIZE * num_full_write);
        NUM_IO_WRITES++;
    }
    std::cout << "NUM_IO_READS: " << NUM_IO_READS << ", NUM_IO_WRITES: " << NUM_IO_WRITES << ", NUM_IO: " << NUM_IO_READS + NUM_IO_WRITES << std::endl;
}

}  // namespace moderndbs
