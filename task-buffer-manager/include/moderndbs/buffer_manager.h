#ifndef INCLUDE_MODERNDBS_BUFFER_MANAGER_H
#define INCLUDE_MODERNDBS_BUFFER_MANAGER_H

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <vector>
#include <memory>
#include <list>
#include <shared_mutex>
#include <mutex>
#include <unordered_map>
#include "file.h"

namespace moderndbs {

// -------------------------------------------------------------------------------------
using std::unordered_map;
using std::unique_ptr;
using std::make_unique;
using std::array;
using std::string;
using std::to_string;
using std::strerror;
using std::runtime_error;
using std::unique_lock;
using std::mutex;
using std::find;
using std::vector;
using std::list;
using std::shared_mutex;
// -------------------------------------------------------------------------------------
class BufferFrame {
private:
    friend class BufferManager;

    using list_position = std::list<BufferFrame*>::iterator;

    enum BufferFrameState {
        /// -----------------------------------------------------
        /// MORE INFORMATION + GRAPH(State Machine) IN MY ONENOTE
        /// -----------------------------------------------------

        NEW,        /// data not loaded => so empty page
        LOADING,    /// data loaded and unmodified => not to flush to dish
        LOADED,	    /// data loaded and modified => flush to disk
        EVICTING,   /// data being evicting
        RELOADED    /// data is newly needed while being evicting => so reload this needed one
    };

    /// Page Id
    uint64_t pid;

    /// Data of this page in bytes (char*)
    char* data;

    std::shared_mutex shared_mutex;

    /// State of this BufferFrame
    BufferFrameState state = NEW;

    /// number of current users(thread) = How many times this page has been fixed
    size_t num_users = 0;

    /// Is this page exclusively locked
    bool exclusively_locked = false;

    /// Is this page dirty
    bool is_dirty = false;

    /// Position of this BufferFrame in the FIFO List
    list_position  fifo_position;

    /// Position of this BufferFrame in the LRU List
    list_position lru_position;

    /**
     * Lock the shared_mutex
     * @param exclusive: if it should be exclusively locked
     */
    void lock(const bool exclusive_lock);

    /**
     * Unlock the shared_mutex
     */
    void unlock();

    /**
     * Set this BufferFrame state as Dirty
     */
    void set_dirty();

    /**
     * Increase the number of user of this Buffer Frame
     */
    void inc_num_users();

    /**
     * Decrease the number of user of this Buffer Frame
     */
    void dec_num_users();

    /**
     * Get the number of user of this Buffer Frame
     * @return the number of user of this Buffer Frame
     */
    size_t get_num_users();

 public:
    /**
     * Returns a pointer to this page's data.
     * @return data in char*
     */
    char* get_data();

    /**
     * Open the file associated with the BufferFrame, create if necessary.
     * Allocate enough memory and filesize
     * (and read the data from the file into an internal buffer) <- not here
     * @param pageId the page ID of the frame
     * @param size how much space is needed
     * @throws runtime_error, if the file can't be opened, created or stated
     */
     BufferFrame(const uint64_t pageId, char* data, list_position fifo_position, list_position lru_position);
    };


class buffer_full_error : public std::exception {
 public:
    const char* what() const noexcept override { return "buffer is full"; }
};

class BufferManager {
private:

    struct SegmentFile {
        std::mutex file_latch;

        std::unique_ptr<File> file;

        explicit SegmentFile(std::unique_ptr<File> file) : file(std::move(file)) {}
    };

    const size_t page_size;

    const size_t page_count;

    /// FIFO List
    std::list<BufferFrame*> fifo_list;

    /// LRU List
    std::list<BufferFrame*> lru_list;

    /// global lock that makes sure we don't modify the Hash Table when doing look ups
    /// Directory_Latch in the Tipp Slide
    std::mutex global_mutex;

    /// Memory Storage for all loaded pages
    std::unique_ptr<char[]> loaded_pages;

    /// Maps segment ids to their files
    std::unordered_map<uint16_t, SegmentFile> segment_files;

    /// Hashtable of all pages/frames, which loaded in RAM
    std::unordered_map<uint64_t, BufferFrame> bufferframes;

    /**
     * Load the Page from Disk
     * @param page load this page
     * @param latch the locked global latch / directory latch => should be unlocked while doing I/O
     */
    void load_page(BufferFrame& page, unique_lock<mutex>& latch);

    /**
     * Write out the Page to Disk
     * @param page write out this page
     * @param latch the locked global latch / directory latch => should be unlocked while doing I/O
     */
    void write_out_page(BufferFrame& page, unique_lock<mutex>& latch);

    /**
     * Caller must hold the global latch / directory latch
     * @return the next page that can be evicted. When no page can be evicted, return nullptr
     */
    BufferFrame* find_page_to_evict();

    /**
     * Evicts a page from the buffer
     * @param latch must be the locked directory latch
     * @return the data pointer to the evicted page. When no page can be evicted, return nullptr
     */
    char* evict_page(std::unique_lock<mutex>& latch);

public:
    /// Constructor.
    /// @param[in] page_size  Size in bytes that all pages will have.
    /// @param[in] page_count Maximum number of pages that should reside in
    ///                       memory at the same time.
    BufferManager(size_t page_size, size_t page_count);

    /// Destructor. Writes all dirty pages to disk.
    ~BufferManager();

    /// Returns a reference to a `BufferFrame` object for a given page id. When
    /// the page is not loaded into memory, it is read from disk. Otherwise the
    /// loaded page is used.
    /// When the page cannot be loaded because the buffer is full, throws the
    /// exception `buffer_full_error`.
    /// Is thread-safe w.r.t. other concurrent calls to `fix_page()` and
    /// `unfix_page()`.
    /// @param[in] page_id   Page id of the page that should be loaded.
    /// @param[in] exclusive If `exclusive` is true, the page is locked
    ///                      exclusively. Otherwise it is locked
    ///                      non-exclusively (shared).
    BufferFrame& fix_page(uint64_t page_id, bool exclusive);

    /// Takes a `BufferFrame` reference that was returned by an earlier call to
    /// `fix_page()` and unfixes it. When `is_dirty` is / true, the page is
    /// written back to disk eventually.
    void unfix_page(BufferFrame& page, bool is_dirty);

    /// Returns the page ids of all pages (fixed and unfixed) that are in the
    /// FIFO list in FIFO order.
    /// Is not thread-safe.
    std::vector<uint64_t> get_fifo_list() const;

    /// Returns the page ids of all pages (fixed and unfixed) that are in the
    /// LRU list in LRU order.
    /// Is not thread-safe.
    std::vector<uint64_t> get_lru_list() const;

    /// Returns the segment id for a given page id which is contained in the 16
    /// most significant bits of the page id.
    static constexpr uint16_t get_segment_id(uint64_t page_id) { return page_id >> 48; }

    /// Returns the page id within its segment for a given page id. This
    /// corresponds to the 48 least significant bits of the page id.
    static constexpr uint64_t get_segment_page_id(uint64_t page_id) { return page_id & ((1ull << 48) - 1); }
};
}  // namespace moderndbs
#endif
