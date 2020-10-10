#ifndef INCLUDE_MODERNDBS_BTREE_H
#define INCLUDE_MODERNDBS_BTREE_H

#include <cstddef>
#include <cassert>
#include <cstring>
#include <functional>
#include <optional>
#include <array>
#include "moderndbs/buffer_manager.h"
#include "moderndbs/defer.h"
#include "moderndbs/segment.h"

namespace moderndbs {
template<typename KeyT, typename ValueT, typename ComparatorT, size_t PageSize>
struct BTree : public Segment {
    /**
     * Both inner node and leaf node are inherited from this node.
     *
     * "Node" and "Page" are two interchangeable words in our context.
     *
     * It actually serves as a header part for each B+ tree page and
     * contains information shared by both leaf page and internal page.
     *
     * Header format (size in byte, 4 bytes in total):
     * ----------------------------------------------------------------------------
     * | level (2) | count (2) |
     * ----------------------------------------------------------------------------
     *
     *
     */
     //TODO(future): Add LSN, B link tree features ... into the header.
    struct Node {
        /// The level in the tree, which indicates the type of a node.
        uint16_t level{0};
        /// The number of children.
        uint16_t count{0};

        /// Constructor
        Node(uint16_t level, uint16_t count) : level(level), count(count) {}

        /// Is the node a leaf node?
        bool is_leaf() const { return level == 0; }

        /// Is the node a inner node?
        bool is_inner() const { return level != 0; }

        /// Get the data of a node.
        std::byte* get_data() const { return reinterpret_cast<std::byte*>(this); }
    };


    /**
      * n := InnerNode::kCapacity
      *
      * Store n-1 indexed keys and n child pointers (page_id) within inner node.
      * Pointer PAGE_ID(i) points to a subtree in which all keys K satisfy:
      * K(i) <= K < K(i+1).
      *
      * NOTE: since the number of keys does not equal to number of child pointers,
      * the last key always remains invalid. That is to say, any search/lookup
      * should ignore the last key.
      *
      * Inner node format (keys are stored in increasing order):
      *
      *  ---------------------------------------------------------------------------------------
      * | HEADER | PAGE_ID(1) | PAGE_ID(2) | ... | PAGE_ID(n) | KEY(1) | KEY(2) | ... | KEY(n) |
      *  ---------------------------------------------------------------------------------------
      */
    struct InnerNode: public Node {
        /// The size of a InnerNode: eliminate padding and be platform-independent.
        static constexpr uint32_t SIZE = (sizeof(Node) + 7) / 8 * 8;
        /// The capacity of a InnerNode: (kCapacity - 1) keys, kCapacity children.
        static constexpr uint32_t kCapacity = (PageSize - SIZE) / (sizeof(KeyT) + sizeof(uint64_t));

        /// The children's PAGE_IDs.
        std::array<uint64_t, kCapacity> children;
        /// The keys.
        std::array<KeyT, kCapacity> keys;

        /// Constructor.
        InnerNode() : Node(0, 0) {}

        /// Returns whether the inner node is full.
        bool is_full() const { return this->count == kCapacity; }

        /// Get the index of the first key that is not less than than a provided key.
        /// @param[in] key          The key to find.
        std::pair<uint32_t, bool> lower_bound (const KeyT& to_find_key) const {
            assert(this->count >= 0 && this->count <= kCapacity);
            if (this->count == 0) { return {0, false}; }

            /// TODO(future): Plug-in whatever fancy advanced binary search implementation!
            /// https://en.cppreference.com/w/cpp/algorithm/lower_bound
            uint32_t first = 0;
            uint32_t cnt = this->count - 1; // Don't consider the last INVALID key!!!
            while (cnt > 0) {
                uint32_t idx = first;
                uint32_t step = cnt / 2;
                idx += step;
                const ComparatorT comparator = ComparatorT();
                if (comparator(keys[idx], to_find_key) /* key[idx] < to_find_key */) {
                    first = ++idx;
                    cnt -= step + 1;
                } else {
                    cnt = step;
                }
            }
            const bool found = (first >= 0 && first < this->count && keys[first] == to_find_key);
            return {first, found};
        }

        /// Lookup a key. The key is possible relevant with the return child page id. No checking if the page exactly on the child page, since the child page can also be a inner page.
        /// @param[in] key             The key that should be looked up.
        /// @return                    The child page id maybe with key of this inner node.
        uint64_t lookup(const KeyT& key) const {
            assert(this->count >= 2 && this->count <= kCapacity);
            const auto index = this->lower_bound(key).first;
            /// Refer to the structure of the Inner node.
            return (index == this->count /* if is larger than the last (aka n-1 th key) */) ? this->children[index - 1] /* the child page id having keys larger than the largest in keys */ : this->children[index];
        }

        /// Insert a key as an initialization (first insertion) on a inner node.
        /// Before this function call: count == 0.
        /// After this function call: count == 2.
        /// @param[in] key             The key that should be inserted.
        /// @param[in] left_page_id    The left child page id having the value <= key.
        /// @param[in] right_page_id   The right child page id having the value > key.
        void init_insert(const KeyT& key, uint64_t left_page_id, uint64_t right_page_id) {
            assert(this->count == 0);
            children[0] = left_page_id;
            keys[0] = key;
            children[1] = right_page_id;
            keys[1] = key;  // This is the last key, is INVALID anyway
            this->count = 2;
        }

        /// Insert a key. Not a initialization.
        /// Before this function call: count == n.
        /// After this function call: count == n + 1.
        /// @param[in] key             The key that should be inserted. The split key from the children.
        /// @param[in] left_page_id    The left child page id having the value <= key. This value muss occur in this inner node! So only checking this value, no insertion of this value.
        /// @param[in] right_page_id   The right child page id having the value > key. Always the new page after splitting.
        void insert(const KeyT& key, uint64_t left_page_id, uint64_t right_page_id) {
            assert(!is_full());
            assert(this->count >= 2  && this->count <= kCapacity - 1);
            const auto& [index, found] = lower_bound(key);
            assert(!found && "This case can't happen! The split key must be unique!");
            assert(index >= 0 && index <= static_cast<uint32_t>(this->count - 1));
            assert(children[index] == left_page_id);
            if (index == static_cast<uint32_t>(this->count - 1)) {
                /// Only fix the last index.
                keys[index] = key;
                keys[index + 1] = key;  // This is the last key, is INVALID anyway
                children[index + 1] = right_page_id;
                this->count++;
                return;
            } else {
                /// Copy the affected data first.
                /// Key, children are changing on different index. Following are hard-coded. Details referring to the structure of a inner node.
                const uint32_t num_to_copy = this->count - 1 - index;
                std::memmove(&keys[index + 1], &keys[index], (num_to_copy + 1) * sizeof(KeyT));
                std::memmove(&children[index + 2], &children[index + 1], num_to_copy * sizeof(ValueT));
                /// insert at the popper place.
                children[index + 1] = right_page_id;
                keys[index] = key;
                this->count++;
            }
        }

        /// Split the node.
        /// @param[in] buffer       The buffer for the new page.
        /// @return                 The separator key.
        KeyT split(std::byte* buffer) {
            assert(this->count >= kCapacity - 1);   /// Precondition: Only split when no free space or almost no free space (non-safe) on the node.
            /// # keys := this->count - 1
            const KeyT separator = keys[(this->count - 1) / 2];
            /// 1. Spilt to another LeafNode.
            auto other = reinterpret_cast<BTree::InnerNode*>(buffer);
            /// 2. Update the count.
            ///    No need to memset this LeafNode, since count is updated and the elements not within count are logically invalid.
            other->count = this->count - ((this->count - 1) / 2 + 1);
            this->count = (this->count - 1) / 2 + 1;
            /// 3. Copy to other page.
            std::memcpy(other->keys.data(), &keys[this->count], other->count * sizeof(KeyT));  /// the invalided key copied to other.
            std::memcpy(other->children.data(), &children[this->count], other->count * sizeof(ValueT));
            assert(this->keys[this->count - 1] == separator);  /// This separator is the last INVALID key. It should be pushed into a page above these two splitting pages.
            return separator;
        }

        /// Returns the keys in a vector copying all keys.
        const std::vector<KeyT> get_key_vector() const { assert(this->count >= 1 && this->count <= kCapacity); return {keys.begin(), keys.begin() + (this->count - 1)}; }

        /// Returns the child page ids in a vector copying all child page ids.
        const std::vector<uint64_t> get_child_vector() const { assert(this->count >= 0 && this->count <= kCapacity); return {children.begin(), children.begin() + this->count}; }
    };


    /**
     * n := LeafNode::kCapacity
     *
     * Store indexed key and value together within leaf node. Only support unique key.
     *
     * Leaf node format (keys are stored in order):
     *
     *  ---------------------------------------------------------------------------------
     * | HEADER | KEY(1) | KEY(2) | ... | KEY(n) | VALUE(1) | VALUE(2) | ... | VALUE(n) |
     *  ----------------------------------------------------------------------------------
     *
     */
    struct LeafNode: public Node {
        /// The capacity of a LeafNode.
        static constexpr uint32_t kCapacity = (PageSize - sizeof(Node) - sizeof(std::optional<uint64_t>)) / (sizeof(KeyT) + sizeof(ValueT));

        /// The pointer to next node := B+ link tree.
//        std::optional<uint64_t> next_node;
        /// The keys.
        std::array<KeyT, kCapacity> keys;
        /// The values.
        std::array<ValueT, kCapacity> values;

        /// Constructor.
        LeafNode() : Node(0, 0) {}

        /// Returns whether the leaf node is full.
        bool is_full() const { return this->count == kCapacity; }

        /// Get the index of the first key that is not less than than a provided key.
        /// @param[in] to_find_key          The key to find.
        std::pair<uint32_t, bool> lower_bound(const KeyT& to_find_key) {
            if (this->count == 0) { return {0, false}; }

            /// TODO(future): Plug-in whatever fancy advanced binary search implementation!
            /// https://en.cppreference.com/w/cpp/algorithm/lower_bound
            uint32_t first = 0;
            uint32_t cnt = this->count;
            while (cnt > 0) {
                uint32_t idx = first;
                uint32_t step = cnt / 2;
                idx += step;
                const ComparatorT comparator = ComparatorT();
                if (comparator(keys[idx], to_find_key) /* key[idx] < to_find_key */) {
                    first = ++idx;
                    cnt -= step + 1;
                } else {
                    cnt = step;
                }
            }
            const bool found = (first >= 0 && first < this->count && keys[first] == to_find_key);
            return {first, found};
        }

        /// Lookup a key.
        /// @param[in] key          The key that should be looked up.
        std::optional<ValueT> lookup(const KeyT& key) {
            const auto& [index, found] = this->lower_bound(key);
            return (found && this->keys[index] == key) ? std::make_optional<ValueT>(this->values[index]) : std::nullopt;
        }

        /// Insert a key.
        /// @param[in] key          The key that should be inserted.
        /// @param[in] value        The value that should be inserted.
        void insert(const KeyT& key, const ValueT& value) {
            assert(!is_full());
            if (this->count == 0) {
                keys[0] = key;
                values[0] = value;
            } else {
                const auto& [index, found] = lower_bound(key);
                if (found) {
                    /// If same key found: update the value.
                    values[index] = value;
                    return;
                }
                assert(this->count >= index);
                /// If the key not found: copy the affected keys and values && insert at the popper place.
                const uint32_t num_to_copy = this->count - index;
                std::memmove(&values[index + 1], &values[index], num_to_copy * sizeof(ValueT));
                std::memmove(&keys[index + 1], &keys[index], num_to_copy * sizeof(KeyT));
                keys[index] = key;
                values[index] = value;
            }
            this->count++;
        }

        /// Erase a key.
        void erase(const KeyT& key) {
            if (this->count == 0) { return; }
            const auto& [index, found] = lower_bound(key);
            if (!found) { return; }
            assert(static_cast<uint32_t>(this->count - 1) >= index);
            const uint32_t num_to_copy = (this->count - 1) - index;
            std::memmove(&values[index], &values[index + 1], num_to_copy * sizeof(ValueT));
            std::memmove(&keys[index], &keys[index + 1], num_to_copy * sizeof(KeyT));
            this->count--;
        }

        /// Split the node.
        /// @param[in] buffer       The buffer for the new page.
        /// @return                 The separator key.
        KeyT split(std::byte* buffer) {
            assert(this->count >= kCapacity - 1);   /// Precondition: Only split when no free space or almost no free space (non-safe) on the node.
            const KeyT separator = keys[this->count / 2];
            /// 1. Spilt to another LeafNode.
            auto other = reinterpret_cast<BTree::LeafNode*>(buffer);
            /// 2. Update te count.
            ///    No need to memset this LeafNode, since count is updated and the elements not within count are logically invalid.
            other->count = this->count - ((this->count / 2) + 1);
            this->count = (this->count / 2) + 1;
            // 3. Copy to other page
            std::memcpy(other->keys.data(), &keys[this->count], other->count * sizeof(KeyT));
            std::memcpy(other->values.data(), &values[this->count], other->count * sizeof(ValueT));
            return separator;
        }

        /// Returns the keys in a vector copying all keys.
        const std::vector<KeyT> get_key_vector() const { return {keys.begin(), keys.begin() + this->count}; }

        /// Returns the values in a vector copying all values.
        const std::vector<ValueT> get_value_vector() const { return {values.begin(), values.begin() + this->count}; }
    };

    private:
    /// The head of leaf node list
//    std::optional<uint64_t> leaf_list_head;

    enum class TraversalType {
        Insert = 0,
        Erase,
        Lookup
    };

    /// Go down the B+ Tree until a leaf node found. A super-important function for B+ Tree insert, lookup, erase.
    ///
    /// B+ Tree Synchronization := Lock Coupling.
    ///                            Maximal hold 2 locks for 2 individual nodes (a pair of parent and child).
    ///                            Spilt the inner node, when the inner node is not safe.
    ///                            Safe, aka "not full and not almost full", where the node has more than 1 free space.
    ///                            Different cases: insert, lookup, erase behavior differently. More detail in code and comments.
    ///
    /// @param[in] key      The key that should be lookup.
    /// @return the parent and the leaf page, where they are **both fixed** by buffer manager.
    std::pair<BufferFrame*, BufferFrame*> get_leaf_page(const KeyT key, TraversalType traversal_type) {
        /// If lookup, then just traversal the tree with read-only-mode, non-exclusive fix.
        /// If erase, then traversal all inner nodes with read-only-mode, non-exclusive fix. Only exclusively fix the final leaf node for erasing.
        ///           PARENT_FIX_MODE == true only for the tree has only one level / only leaf node as root.
        /// If insert, all the way with exclusive fix. Split when the inner node not safe.
        const bool PARENT_FIX_MODE = traversal_type == TraversalType::Insert ? true : false;
        /// 1. Look up a non-empty B+ Tree := go down from root to leaf.
        assert(root.has_value());
        uint64_t parent_id = *root;
        BufferFrame* parent_page = &buffer_manager.fix_page(*root, PARENT_FIX_MODE);
        Node* parent_node = reinterpret_cast<BTree::Node*>(parent_page->get_data());
        bool PARENT_PAGE_DIRTY = false;

        /// 2. To check if the root is a leaf node or inner node.
        if (parent_node->is_leaf()) {
            /// We treat this leaf page as parent with PARENT_FIX_MODE. That is want we want for insert, erase, lookup.
            /// But have to exclusively fix this page for erase := unfix with non-dirty, then fix exclusively.
            if (traversal_type == TraversalType::Erase) {
                buffer_manager.unfix_page(*parent_page, false);
                parent_page = &buffer_manager.fix_page(*root, true);
            }
            return {parent_page, parent_page};
        } else {
            /// 2.2. The root is a inner node.
            InnerNode* parent_inner_node = reinterpret_cast<BTree::InnerNode*>(parent_page->get_data());  // NOLINT
            assert(*root == parent_id);

            if (traversal_type == TraversalType::Insert) {
                /// 2.3 ONLY WHEN INSERT := Check if the root is safe (having more than 1 free space).
                if (parent_inner_node->count >= InnerNode::kCapacity - 1) {
                    /// If not safe, spilt this root into two inner node + a new root node.
                    /// 2.3.1. New a page to be split buddy, which is also a inner node.
                    const uint64_t new_inner_page_id = next_page_id++;
                    BufferFrame* new_inner_page = &buffer_manager.fix_page(new_inner_page_id, true);
                    InnerNode* new_inner_node = reinterpret_cast<InnerNode*>(new_inner_page->get_data());  // NOLINT
                    new_inner_node->level = parent_inner_node->level;

                    /// 2.3.2. Split on the OLD root and get the separator_key to-be-inserted into the new root.
                    KeyT separator_key = parent_inner_node->split(reinterpret_cast<std::byte*>(new_inner_page->get_data()));  /// this `root_node` is not a root node any more!

                    /// 2.3.3. After splitting, check the inner pages' safety.
                    assert(parent_inner_node->count < InnerNode::kCapacity - 1 && "Still no safe!");  /// OLD root node, now is a normal inner node.
                    assert(new_inner_node->count < InnerNode::kCapacity - 1 && "Still no safe!");

                    /// 2.3.4. New a root and init_insert the separator_key.
                    BufferFrame& new_root_page = buffer_manager.fix_page(next_page_id, true);
                    assert(*root == parent_id);
                    root = next_page_id++;
                    assert(*root != parent_id);
                    auto new_root_node = reinterpret_cast<InnerNode*>(new_root_page.get_data());
                    new_root_node->level = parent_inner_node->level + 1;
                    new_root_node->init_insert(separator_key, parent_id, new_inner_page_id);
                    assert(new_root_node->count == 2);  /// Actually one key, two page ids.
                    buffer_manager.unfix_page(new_root_page, true);

                    /// 2.3.5. Adjust the parent id, which can be the parent_id or new_inner_page_id (so the left child of the root or the right child of the root, for now the root has only two children).
                    const ComparatorT comparator = ComparatorT();
                    if (comparator(separator_key /* the largest key on parent_inner_node */, key) /* separator_key < key */) {
                        /// 2.3.5.1. Key at the right child. Unfix the left child. Update the parent page information.
                        buffer_manager.unfix_page(*parent_page, true);
                        parent_id = new_inner_page_id;
                        parent_page = new_inner_page;
                        parent_inner_node = new_inner_node;
                    } else {
                        /// 2.3.5.1. Key at the left child. Unfix the right child. Update the parent page id.
                        buffer_manager.unfix_page(*new_inner_page, true);
                    }
                    PARENT_PAGE_DIRTY = true;
                }

                /// 2.4. The parent has to be safe (having only 1 free space)
                assert(parent_inner_node->count < InnerNode::kCapacity - 1);
            }


            /// Traversal until leaf node using lock coupling to ensure the safety.

            /// 2.5. Get the child repeatedly until reaching leaf node.
            uint64_t child_page_id = parent_inner_node->lookup(key);
            /// Hint: we can know if the child is a leaf BEFORE fix it. We just simply check if the parent if has the level 1.
            /// Depending on if a child is a leaf or not. The fix mode of ERASE differs. ERASE have to have exclusive fix ONLY at the leaf node.
            bool CHILD_FIX_MODE = [&]{
                if (traversal_type == TraversalType::Insert) {
                    return true;
                } else if (traversal_type == TraversalType::Lookup) {
                    return false;
                } else if (traversal_type == TraversalType::Erase) {
                    if (parent_inner_node->level == 1) {
                        return true;
                    } else {
                        return false;
                    }
                }
                __builtin_unreachable();
            } ();

            BufferFrame* child_page = &buffer_manager.fix_page(child_page_id, CHILD_FIX_MODE);
            Node* child_node = reinterpret_cast<BTree::Node*>(child_page->get_data());
            assert(child_node->level == parent_inner_node->level - 1);
            bool CHILD_PAGE_DIRTY = false;

            /// Hint: this while-loop can be very easily re-written into a for-loop := for (parent_level; parent_level >= 1;) + other little changes inside the loop.
            while (!child_node->is_leaf()) {
                InnerNode* child_inner_node = reinterpret_cast<BTree::InnerNode*>(child_page->get_data());  // NOLINT

                if (traversal_type == TraversalType::Insert) {
                    /// 2.5.1. ONLY WHEN INSERT := Check if the child inner node is safe (having more than 1 free space).
                    if (child_inner_node->count >= InnerNode::kCapacity - 1) {
                        /// If not safe, spilt this child into two inner node
                        /// 2.5.2. New a page to be split buddy, which is also a inner node
                        const uint64_t new_inner_page_id = next_page_id++;
                        BufferFrame* new_inner_page = &buffer_manager.fix_page(new_inner_page_id, true);
                        InnerNode* new_inner_node = reinterpret_cast<InnerNode*>(new_inner_page->get_data());  // NOLINT
                        new_inner_node->level = child_inner_node->level;

                        /// 2.5.3. Split on the OLD child inner node and get the separator_key to-be-inserted into the parent inner node.
                        KeyT separator_key = child_inner_node->split(reinterpret_cast<std::byte*>(new_inner_page->get_data())); // this `root_node` is not a root node any more!

                        /// 2.5.4. After splitting, check the inner pages' safety.
                        assert(child_inner_node->count < InnerNode::kCapacity - 1 && "Still no safe!");
                        assert(new_inner_node->count < InnerNode::kCapacity - 1 && "Still no safe!");

                        /// 2.5.6. Insert the separator_key into the parent.
                        assert(parent_inner_node->count < InnerNode::kCapacity);
                        assert(parent_inner_node->count >= 2 && "Non empty inner node!");
                        parent_inner_node->insert(separator_key, child_page_id, new_inner_page_id);

                        /// 2.5.7. Adjust the child id, which can be the child_page_id or new_inner_page_id (so the left child of the parent_inner_node or the right child of the parent_inner_node).
                        const ComparatorT comparator = ComparatorT();
                        if (comparator(separator_key /* the largest key on child_page_id */, key) /* separator_key < key */) {
                            /// 2.5.7.1. Key at the right child. Unfix the left child. Update the child page information.
                            buffer_manager.unfix_page(*child_page, true);
                            child_page_id = new_inner_page_id;
                            child_page = new_inner_page;
                            child_inner_node = new_inner_node;
                        } else {
                            /// 2.5.7.2. Key at the left child. Unfix the right child. Update the child page id.
                            buffer_manager.unfix_page(*new_inner_page, true);
                        }
                        PARENT_PAGE_DIRTY = true;
                        CHILD_PAGE_DIRTY = true;
                    }
                    /// 2.6. The child has to be safe (having only 1 free space).
                    assert(child_inner_node->count < InnerNode::kCapacity - 1);
                }


                /// 2.7. Unfix the parent, since it is not relevant anymore.
                buffer_manager.unfix_page(*parent_page, PARENT_PAGE_DIRTY);

                /// 2.8. The child is now the parent. Update the parent metadata.
                parent_id = child_page_id;
                parent_page = child_page;
                parent_node = child_node;
                parent_inner_node = child_inner_node;
                PARENT_PAGE_DIRTY = CHILD_PAGE_DIRTY;

                /// 2.9. Get next child at the next level.
                child_page_id = parent_inner_node->lookup(key);
                CHILD_FIX_MODE |= (traversal_type == TraversalType::Erase && parent_inner_node->level == 1);  /// Change this mode from false to true, only when ERASE and at leaf node. Branch-free :D.
                child_page = &buffer_manager.fix_page(child_page_id, CHILD_FIX_MODE);
                child_node = reinterpret_cast<BTree::Node*>(child_page->get_data());
                assert(child_node->level == parent_inner_node->level - 1);
                CHILD_PAGE_DIRTY = false;
            }
            assert(parent_inner_node->level == 1);
            assert(child_node->level == 0);
            buffer_manager.unfix_page(*parent_page, PARENT_PAGE_DIRTY);
            assert(child_node->is_leaf());
            return {parent_page, child_page};
        }
    }


    public:
    /// The root.
    std::optional<uint64_t> root;  /// std::optional will be harder to synchronisze, uint64_t will be okay with synchronization.
    /// Next page id.
    uint64_t next_page_id;

    /// Constructor.
    BTree(uint16_t segment_id, BufferManager& buffer_manager) : Segment(segment_id, buffer_manager) {
      root = std::nullopt;
      next_page_id = (static_cast<uint64_t>(segment_id) << 48);  /// this is the first page.
    }

    /// Destructor.
    ~BTree() = default;

    /// Lookup an entry in the tree.
    /// @param[in] key      The key that should be searched.
    /// @return             Whether the key was in the tree.
    std::optional<ValueT> lookup(const KeyT& key) {
        /// 0. Look up in an empty B+ Tree.
        if (!root) {
            return std::nullopt;
        }
        /// 1. Traversal the tree.
        auto [parent_page, leaf_page] = get_leaf_page(key, TraversalType::Lookup);
        buffer_manager.unfix_page(*parent_page, false);
        assert(reinterpret_cast<BTree::Node*>(leaf_page->get_data())->is_leaf());
        auto leaf_node = reinterpret_cast<LeafNode*>(leaf_page->get_data());
        auto res = leaf_node->lookup(key);
        buffer_manager.unfix_page(*leaf_page, false);
        return res;
    }

    /// Erase an entry in the tree.
    /// @param[in] key      The key that should be searched.
    void erase(const KeyT& key) {
        /// 0. Look up in an empty B+ Tree.
        if (!root) {
            return;
        }
        /// 1. Traversal the tree.
        auto [parent_page, leaf_page] = get_leaf_page(key, TraversalType::Erase);
        buffer_manager.unfix_page(*parent_page, false);
        assert(reinterpret_cast<BTree::Node*>(leaf_page->get_data())->is_leaf());
        auto leaf_node = reinterpret_cast<LeafNode*>(leaf_page->get_data());
        leaf_node->erase(key);
        buffer_manager.unfix_page(*leaf_page, true);
    }

    /// Inserts a new entry into the tree.
    /// @param[in] key      The key that should be inserted.
    /// @param[in] value    The value that should be inserted.
    void insert(const KeyT& key, const ValueT& value) {
        /// 1. Insert into an empty B+ Tree := to generate page representing a root node, is a leaf node with level = 0 and the head of the linked leaf nodes, is also an inner node.
        if (!root) {
            root = next_page_id++;
            BufferFrame& root_page = buffer_manager.fix_page(*root, true);
            Defer page_unfix([&]() { buffer_manager.unfix_page(root_page, true); });
            auto root_node = reinterpret_cast<LeafNode*>(root_page.get_data());
            root_node->level = 0;
            root_node->insert(key, value);
            return;
        }

        /// 2. Traversal the tree. Get parent_page and leaf_page fixed.
        auto [parent_page, leaf_page] = get_leaf_page(key, TraversalType::Insert);
        Node* node = reinterpret_cast<BTree::Node*>(leaf_page->get_data());
        assert(node->is_leaf());
        LeafNode* leaf_node = reinterpret_cast<LeafNode*>(leaf_page->get_data());  // NOLINT
        assert(leaf_node->level == 0);

        /// 2.1. Check if the leaf is full.
        if (leaf_node->count == LeafNode::kCapacity) {
          /// 2.1.2. New a page to be split buddy (also a leaf node).
          const auto new_leaf_page_id = next_page_id++;
          BufferFrame* new_leaf_page = &buffer_manager.fix_page(new_leaf_page_id, true);
          LeafNode* new_leaf_node = reinterpret_cast<LeafNode*>(new_leaf_page->get_data());  // NOLINT
          new_leaf_node->level = 0;

          /// 2.1.3. Split on the old leaf node.
          KeyT separator_key = leaf_node->split(reinterpret_cast<std::byte*>(new_leaf_page->get_data())); // this `parent_leaf_node` is not a root node any more!

          /// 2.1.4. After splitting, check the count.
          assert(leaf_node->count < LeafNode::kCapacity);
          assert(new_leaf_node->count < LeafNode::kCapacity);

          /// 2.1.5. Check if necessary to add an new root as inner page.
          if (parent_page == leaf_page) {
              /// Case: add an new root as inner page, since the B+ Tree for now has only 1 page as leaf page.
              assert(*root == leaf_page->get_page_id());
              assert(*root == parent_page->get_page_id());
              BufferFrame& new_root_page = buffer_manager.fix_page(next_page_id, true);
              root = next_page_id++;
              InnerNode* new_root_node = reinterpret_cast<InnerNode*>(new_root_page.get_data());  // NOLINT
              new_root_node->level = 1;
              new_root_node->init_insert(separator_key, leaf_page->get_page_id(), new_leaf_page_id);
              assert(new_root_node->count == 2);  // Actually one key, two page ids
              buffer_manager.unfix_page(new_root_page, true);
          } else {
              /// Case: no need to add an new root page.
              InnerNode* parent_inner_node = reinterpret_cast<InnerNode*>(parent_page->get_data());  // NOLINT
              assert(parent_inner_node->level == 1);
              assert(parent_inner_node->count >= 2);
              parent_inner_node->insert(separator_key, leaf_page->get_page_id(), new_leaf_page_id);
              /// Parent page is not relevant anymore, unfix it.
              buffer_manager.unfix_page(*parent_page, true);
          }

          /// 2.2.1. After splitting, re-try to insert the key-value into this tree
          const ComparatorT comparator = ComparatorT();
          if (comparator(separator_key /* the largest key on parent_inner_node */, key) /* separator_key < key */) {
            /// 2.2.1.1. Key at the right child. Unfix the left child. Update the parent page information.
            new_leaf_node->insert(key, value);
          } else {
            /// 2.2.1.2. Key at the left child. Unfix the right child. Update the parent page id.
            leaf_node->insert(key, value);
          }
          buffer_manager.unfix_page(*new_leaf_page, true);
          buffer_manager.unfix_page(*leaf_page, true);
          return;
        } else {
            /// 2.2. The Leaf node is not full, so just simple insert.
            if (parent_page != leaf_page) {
                buffer_manager.unfix_page(*parent_page, true);
            }
            leaf_node->insert(key, value);
            buffer_manager.unfix_page(*leaf_page, true);
        }
        return;
    }
};
}  // namespace moderndbs

#endif
