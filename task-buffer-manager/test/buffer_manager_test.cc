#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <random>
#include <thread>
#include <vector>
#include <gtest/gtest.h>
#include "moderndbs/buffer_manager.h"
#define DEBUG 1

namespace {
#ifdef DEBUG
// NOLINTNEXTLINE
TEST(BufferManagerTest, FixSingle) {
    moderndbs::BufferManager buffer_manager{1024, 10};
    std::vector<uint64_t> expected_values(1024 / sizeof(uint64_t), 123);
    {
        auto& page = buffer_manager.fix_page(1, true);
        ASSERT_TRUE(page.get_data());
        std::memcpy(page.get_data(), expected_values.data(), 1024);
        buffer_manager.unfix_page(page, true);
        EXPECT_EQ(std::vector<uint64_t>{1}, buffer_manager.get_fifo_list());
        EXPECT_TRUE(buffer_manager.get_lru_list().empty());
    }
    {
        std::vector<uint64_t> values(1024 / sizeof(uint64_t));
        auto& page = buffer_manager.fix_page(1, false);
        std::memcpy(values.data(), page.get_data(), 1024);
        buffer_manager.unfix_page(page, true);
        EXPECT_TRUE(buffer_manager.get_fifo_list().empty());
        EXPECT_EQ(std::vector<uint64_t>{1}, buffer_manager.get_lru_list());
        ASSERT_EQ(expected_values, values);
    }
}

// NOLINTNEXTLINE
TEST(BufferManagerTest, PersistentRestart) {
    auto buffer_manager = std::make_unique<moderndbs::BufferManager>(1024, 10);
    for (uint16_t segment = 0; segment < 3; ++segment) {
        for (uint64_t segment_page = 0; segment_page < 10; ++segment_page) {
            uint64_t page_id = (static_cast<uint64_t>(segment) << 48) | segment_page;
            auto& page = buffer_manager->fix_page(page_id, true);
            ASSERT_TRUE(page.get_data());
            uint64_t& value = *reinterpret_cast<uint64_t*>(page.get_data());
            value = segment * 10 + segment_page;
            buffer_manager->unfix_page(page, true);
        }
    }
    // Destroy the buffer manager and create a new one.
    buffer_manager = std::make_unique<moderndbs::BufferManager>(1024, 10);
    for (uint16_t segment = 0; segment < 3; ++segment) {
        for (uint64_t segment_page = 0; segment_page < 10; ++segment_page) {
            uint64_t page_id = (static_cast<uint64_t>(segment) << 48) | segment_page;
            auto& page = buffer_manager->fix_page(page_id, false);
            ASSERT_TRUE(page.get_data());
            uint64_t value = *reinterpret_cast<uint64_t*>(page.get_data());
            buffer_manager->unfix_page(page, false);
            EXPECT_EQ(segment * 10 + segment_page, value);
        }
    }
}

// NOLINTNEXTLINE
TEST(BufferManagerTest, FIFOEvict) {
    moderndbs::BufferManager buffer_manager{1024, 10};
    for (uint64_t i = 1; i < 11; ++i) {
        auto& page = buffer_manager.fix_page(i, false);
        buffer_manager.unfix_page(page, false);
    }
    {
        std::vector<uint64_t> expected_fifo{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
        EXPECT_EQ(expected_fifo, buffer_manager.get_fifo_list());
        EXPECT_TRUE(buffer_manager.get_lru_list().empty());
    }
    {
        auto& page = buffer_manager.fix_page(11, false);
        buffer_manager.unfix_page(page, false);
    }
    {
        std::vector<uint64_t> expected_fifo{2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
        EXPECT_EQ(expected_fifo, buffer_manager.get_fifo_list());
        EXPECT_TRUE(buffer_manager.get_lru_list().empty());
    }
}

// NOLINTNEXTLINE
TEST(BufferManagerTest, BufferFull) {
    moderndbs::BufferManager buffer_manager{1024, 10};
    std::vector<moderndbs::BufferFrame*> pages;
    pages.reserve(10);
    for (uint64_t i = 1; i < 11; ++i) {
        auto& page = buffer_manager.fix_page(i, false);
        pages.push_back(&page);
    }
    EXPECT_THROW(buffer_manager.fix_page(11, false), moderndbs::buffer_full_error);
    for (auto* page : pages) {
        buffer_manager.unfix_page(*page, false);
    }
}

// NOLINTNEXTLINE
TEST(BufferManagerTest, MoveToLRU) {
    moderndbs::BufferManager buffer_manager{1024, 10};
    auto& fifo_page = buffer_manager.fix_page(1, false);
    auto* lru_page = &buffer_manager.fix_page(2, false);
    buffer_manager.unfix_page(fifo_page, false);
    buffer_manager.unfix_page(*lru_page, false);
    EXPECT_EQ((std::vector<uint64_t>{1, 2}), buffer_manager.get_fifo_list());
    EXPECT_TRUE(buffer_manager.get_lru_list().empty());
    lru_page = &buffer_manager.fix_page(2, false);
    buffer_manager.unfix_page(*lru_page, false);
    EXPECT_EQ(std::vector<uint64_t>{1}, buffer_manager.get_fifo_list());
    EXPECT_EQ(std::vector<uint64_t>{2}, buffer_manager.get_lru_list());
}

// NOLINTNEXTLINE
TEST(BufferManagerTest, LRURefresh) {
    moderndbs::BufferManager buffer_manager{1024, 10};
    auto* page1 = &buffer_manager.fix_page(1, false);
    buffer_manager.unfix_page(*page1, false);
    page1 = &buffer_manager.fix_page(1, false);
    buffer_manager.unfix_page(*page1, false);
    auto* page2 = &buffer_manager.fix_page(2, false);
    buffer_manager.unfix_page(*page2, false);
    page2 = &buffer_manager.fix_page(2, false);
    buffer_manager.unfix_page(*page2, false);
    EXPECT_TRUE(buffer_manager.get_fifo_list().empty());
    EXPECT_EQ((std::vector<uint64_t>{1, 2}), buffer_manager.get_lru_list());
    page1 = &buffer_manager.fix_page(1, false);
    buffer_manager.unfix_page(*page1, false);
    EXPECT_TRUE(buffer_manager.get_fifo_list().empty());
    EXPECT_EQ((std::vector<uint64_t>{2, 1}), buffer_manager.get_lru_list());
}

// NOLINTNEXTLINE
TEST(BufferManagerTest, MultithreadParallelFix) {
    moderndbs::BufferManager buffer_manager{1024, 10};
    std::vector<std::thread> threads;
    for (size_t i = 0; i < 4; ++i) {
        threads.emplace_back([i, &buffer_manager] {
            ASSERT_NO_THROW(
                auto& page1 = buffer_manager.fix_page(i, false);
                auto& page2 = buffer_manager.fix_page(i + 4, false);
                buffer_manager.unfix_page(page1, false);
                buffer_manager.unfix_page(page2, false);
            );
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    auto fifo_list = buffer_manager.get_fifo_list();
    std::sort(fifo_list.begin(), fifo_list.end());
    std::vector<uint64_t> expected_fifo{0, 1, 2, 3, 4, 5, 6, 7};
    EXPECT_EQ(expected_fifo, fifo_list);
    EXPECT_TRUE(buffer_manager.get_lru_list().empty());
}

// NOLINTNEXTLINE
TEST(BufferManagerTest, MultithreadExclusiveAccess) {
    moderndbs::BufferManager buffer_manager{1024, 10};
    {
        auto& page = buffer_manager.fix_page(0, true);
        ASSERT_TRUE(page.get_data());
        std::memset(page.get_data(), 0, 1024);
        buffer_manager.unfix_page(page, true);
    }
    std::vector<std::thread> threads;
    for (size_t i = 0; i < 4; ++i) {
        threads.emplace_back([&buffer_manager] {
            for (size_t j = 0; j < 1000; ++j) {
                auto& page = buffer_manager.fix_page(0, true);
                ASSERT_TRUE(page.get_data());
                uint64_t& value = *reinterpret_cast<uint64_t*>(page.get_data());
                ++value;
                buffer_manager.unfix_page(page, true);
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    EXPECT_TRUE(buffer_manager.get_fifo_list().empty());
    EXPECT_EQ(std::vector<uint64_t>{0}, buffer_manager.get_lru_list());
    auto& page = buffer_manager.fix_page(0, false);
    ASSERT_TRUE(page.get_data());
    uint64_t value = *reinterpret_cast<uint64_t*>(page.get_data());
    buffer_manager.unfix_page(page, false);
    EXPECT_EQ(4000, value);
}

// NOLINTNEXTLINE
TEST(BufferManagerTest, MultithreadBufferFull) {
    moderndbs::BufferManager buffer_manager{1024, 10};
    std::atomic<uint64_t> num_buffer_full = 0;
    std::atomic<uint64_t> finished_threads = 0;
    std::vector<std::thread> threads;
    for (size_t i = 0; i < 4; ++i) {
        threads.emplace_back([i, &buffer_manager, &num_buffer_full, &finished_threads] {
            std::vector<moderndbs::BufferFrame*> pages;
            pages.reserve(4);
            for (size_t j = 0; j < 4; ++j) {
                try {
                    pages.push_back(&buffer_manager.fix_page(i + j * 4, false));
                } catch (const moderndbs::buffer_full_error&) {
                    ++num_buffer_full;
                }
            }
            ++finished_threads;
            // Busy wait until all threads have finished.
            while (finished_threads.load() < 4) {}
            for (auto* page : pages) {
                buffer_manager.unfix_page(*page, false);
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    EXPECT_EQ(10, buffer_manager.get_fifo_list().size());
    EXPECT_TRUE(buffer_manager.get_lru_list().empty());
    EXPECT_EQ(6, num_buffer_full.load());
}

// NOLINTNEXTLINE
TEST(BufferManagerTest, MultithreadManyPages) {
    moderndbs::BufferManager buffer_manager{1024, 10};

        std::vector<std::thread> threads;
    for (size_t i = 0; i < 4; ++i) {
            threads.emplace_back([i, &buffer_manager] {
            std::mt19937_64 engine{i};  // pseudo-random number generators
            std::geometric_distribution<uint64_t> distr{0.1};
            for (size_t j = 0; j < 10000; ++j) {
//                std::stringstream stream2;
//                stream2 << "j: " << j << std::endl;
//                std::cout << stream2.str();
                ASSERT_NO_THROW(
                    auto& page = buffer_manager.fix_page(distr(engine), false);
                    buffer_manager.unfix_page(page, false);
//                            std::stringstream stream;
//                            for (std::vector<uint64_t>::const_iterator i = buffer_manager.get_fifo_list().begin(); i != buffer_manager.get_fifo_list().end(); ++i)
//                                stream << *i << ", ";
//                            std::cout << stream.str() << std::endl;
//                            std::stringstream stream1;
//                            for (std::vector<uint64_t>::const_iterator i = buffer_manager.get_lru_list().begin(); i != buffer_manager.get_lru_list().end(); ++i)
//                                stream1 << *i << ", ";
//                            std::cout << stream1.str() << std::endl;
                );
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
}
#endif

// NOLINTNEXTLINE
TEST(BufferManagerTest, MultithreadReaderWriter) {
    {
        // Zero out all pages first
        moderndbs::BufferManager buffer_manager{1024, 10};
        for (uint16_t segment = 0; segment <= 3; ++segment) {
            for (uint64_t segment_page = 0; segment_page <= 100; ++segment_page) {
                uint64_t page_id = (static_cast<uint64_t>(segment) << 48) | segment_page;
                auto& page = buffer_manager.fix_page(page_id, true);
                ASSERT_TRUE(page.get_data());
                std::memset(page.get_data(), 0, 1024);
                buffer_manager.unfix_page(page, true);
            }
        }
        // Let the buffer manager be destroyed here so that the caches are
        // empty before running the actual test.
    }

    moderndbs::BufferManager buffer_manager{1024, 10};
    std::atomic<size_t> aborts = 0;
    std::vector<std::thread> threads;
    for (size_t i = 0; i < 4; ++i) {
        threads.emplace_back([i, &buffer_manager, &aborts] {
            std::mt19937_64 engine{i};
            // 5% of queries are scans.
            std::bernoulli_distribution scan_distr{0.05};
            // Number of pages accessed by a point query is geometrically
            // distributed.
            std::geometric_distribution<size_t> num_pages_distr{0.5};
            // 60% of point queries are reads.
            std::bernoulli_distribution reads_distr{0.6};
            // Out of 20 accesses, 12 are from segment 0, 5 from segment 1,
            // 2 from segment 2, and 1 from segment 3.
            std::discrete_distribution<uint16_t> segment_distr{12.0, 5.0, 2.0, 1.0};
            // Page accesses for point queries are uniformly distributed in
            // [0, 100].
            std::uniform_int_distribution<uint64_t> page_distr{0, 100};
            std::vector<uint64_t> scan_sums(4);
            for (size_t j = 0; j < 100; ++j) {
                uint16_t segment = segment_distr(engine);
                uint64_t segment_shift = static_cast<uint64_t>(segment) << 48;
                if (scan_distr(engine)) {
                    // scan
                    uint64_t scan_sum = 0;
                    for (uint64_t segment_page = 0; segment_page <= 100; ++segment_page) {
                        uint64_t page_id = segment_shift | segment_page;
                        moderndbs::BufferFrame* page;
                        while (true) {
                            try {
                                page = &buffer_manager.fix_page(page_id, false);
                                break;
                            } catch (const moderndbs::buffer_full_error&) {
                                // Don't abort scan when the buffer is full, retry
                                // the current page.
                            }
                        }
                        ASSERT_TRUE(page->get_data());
                        uint64_t value = *reinterpret_cast<uint64_t*>(page->get_data());
                        scan_sum += value;
                        buffer_manager.unfix_page(*page, false);
                    }
                    EXPECT_GE(scan_sum, scan_sums[segment]);
                    scan_sums[segment] = scan_sum;
                } else {
                    // point query
                    auto num_pages = num_pages_distr(engine) + 1;
                    // For point queries all accesses but the last are always
                    // reads. Only the last is potentially a write. Also,
                    // all pages but the last are held for the entire duration
                    // of the query.
                    std::vector<moderndbs::BufferFrame*> pages;
                    auto unfix_pages = [&] {
                        for (auto it = pages.rbegin(); it != pages.rend(); ++it) {
                            auto& page = **it;
                            buffer_manager.unfix_page(page, false);
                        }
                        pages.clear();
                    };
                    for (size_t page_number = 0; page_number < num_pages - 1; ++page_number) {
                        uint64_t segment_page = page_distr(engine);
                        uint64_t page_id = segment_shift | segment_page;
                        moderndbs::BufferFrame* page;
                        try {
                            page = &buffer_manager.fix_page(page_id, false);
                        } catch (const moderndbs::buffer_full_error&) {
                            // Abort query when buffer is full.
                            ++aborts;
                            goto abort;
                        }
                        pages.push_back(page);
                    }
                    // Unfix all pages before accessing the last one
                    // (potentially exclusively) to avoid deadlocks.
                    unfix_pages();
                    {
                        uint64_t segment_page = page_distr(engine);
                        uint64_t page_id = segment_shift | segment_page;
                        if (reads_distr(engine)) {
                            // read
                            moderndbs::BufferFrame* page;
                            try {
                                page = &buffer_manager.fix_page(page_id, false);
                            } catch (const moderndbs::buffer_full_error&) {
                                ++aborts;
                                goto abort;
                            }
                            buffer_manager.unfix_page(*page, false);
                        } else {
                            // write
                            moderndbs::BufferFrame* page;
                            try {
                                page = &buffer_manager.fix_page(page_id, true);
                            } catch (const moderndbs::buffer_full_error&) {
                                ++aborts;
                                goto abort;
                            }
                            ASSERT_TRUE(page->get_data());
                            auto& value = *reinterpret_cast<uint64_t*>(page->get_data());
                            ++value;
                            buffer_manager.unfix_page(*page, true);
                        }
                    }
                    abort:
                    unfix_pages();
                }
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    EXPECT_LT(aborts.load(), 20);
}

}  // namespace
