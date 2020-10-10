#include <gtest/gtest.h>
#include <algorithm>
#include <cstddef>
#include <numeric>
#include <random>
#include <sstream>
#include <vector>
#include <map>
#include <limits>
#include "moderndbs/defer.h"
#include "moderndbs/btree.h"

using BufferFrame = moderndbs::BufferFrame;
using BufferManager = moderndbs::BufferManager;
using Defer = moderndbs::Defer;
using BTree = moderndbs::BTree<uint64_t, uint64_t, std::less<uint64_t>, 1024>; // NOLINT

namespace {

// NOLINTNEXTLINE
TEST(BTreeLeafNodeTest, LeafNodeInsert) {
    std::vector<std::byte> buffer;
    buffer.resize(1024);
    auto node = new (buffer.data()) BTree::LeafNode();
    ASSERT_EQ(node->count, 0);

    std::stringstream expected_keys;
    std::stringstream expected_values;
    std::stringstream seen_keys;
    std::stringstream seen_values;

    auto n = BTree::LeafNode::kCapacity;

    // Insert children into the leaf nodes
    for (uint32_t i = 0, j = 0; i < n; ++i, j = i * 2) {
        node->insert(i, j);

        ASSERT_EQ(node->count, i + 1)
            << "LeafNode::insert did not increase the the child count";

        // Append current node to expected nodes
        expected_keys << ((i != 0) ? ", " : "") << i;
        expected_values << ((i != 0) ? ", " : "") << j;
    }

    // Check the number of keys & values
    auto keys = node->get_key_vector();
    auto children = node->get_value_vector();

    ASSERT_EQ(keys.size(), n)
        << "leaf node must contain " << n << " keys for " << n << " values";
    ASSERT_EQ(children.size(), n)
        << "leaf node must contain " << n << " values";

    // Check the keys
    for (uint32_t i = 0; i < n; ++i) {
        auto k = keys[i];
        seen_keys << ((i != 0) ? ", " : "") << k;
        ASSERT_EQ(k, i)
            << "leaf node does not store key=" << i << "\n"
            << "EXPECTED:\n"
            << expected_keys.str()
            << "\nSEEN:\n"
            << seen_keys.str();
    }

    // Check the values
    for (uint32_t i = 0, j = 0; i < n; ++i, j = i * 2) {
        auto c = children[i];
        seen_values << ((i != 0) ? ", " : "") << c;
        ASSERT_EQ(c, j)
            << "leaf node does not store value=" << j << "\n"
            << "EXPECTED:\n"
            << expected_values.str()
            << "\nSEEN:\n"
            << seen_values.str();
    }
}

// NOLINTNEXTLINE
TEST(BTreeLeafNodeTest, LeafNodeSplit) {
    std::vector<std::byte> buffer_left;
    std::vector<std::byte> buffer_right;
    buffer_left.resize(1024);
    buffer_right.resize(1024);

    auto left_node = new (buffer_left.data()) BTree::LeafNode();
    auto right_node = reinterpret_cast<BTree::LeafNode*>(buffer_right.data());
    ASSERT_EQ(left_node->count, 0);
    ASSERT_EQ(right_node->count, 0);

    auto n = BTree::LeafNode::kCapacity;

    // Fill the left node
    for (uint32_t i = 0, j = 0; i < n; ++i, j = i * 2) {
        left_node->insert(i, j);
    }

    // Check the number of keys & values
    auto left_keys = left_node->get_key_vector();
    auto left_values = left_node->get_value_vector();
    ASSERT_EQ(left_keys.size(), n);
    ASSERT_EQ(left_values.size(), n);

    // Now split the left node
    auto separator = left_node->split(buffer_right.data());
    ASSERT_EQ(left_node->count, n / 2 + 1);
    ASSERT_EQ(right_node->count, n - (n / 2) - 1);
    ASSERT_EQ(separator, n / 2);

    // Check keys & values of the left node
    left_keys = left_node->get_key_vector();
    left_values = left_node->get_value_vector();
    ASSERT_EQ(left_keys.size(), left_node->count);
    ASSERT_EQ(left_values.size(), left_node->count);
    for (auto i = 0; i < left_node->count; ++i) {
        ASSERT_EQ(left_keys[i], i);
    }
    for (auto i = 0; i < left_node->count; ++i) {
        ASSERT_EQ(left_values[i], i * 2);
    }

    // Check keys & values of the right node
    auto right_keys = right_node->get_key_vector();
    auto right_values = right_node->get_value_vector();
    ASSERT_EQ(right_keys.size(), right_node->count);
    ASSERT_EQ(right_values.size(), right_node->count);
    for (auto i = 0; i < right_node->count; ++i) {
        ASSERT_EQ(right_keys[i], left_node->count + i);
    }
    for (auto i = 0; i < right_node->count; ++i) {
        ASSERT_EQ(right_values[i], (left_node->count + i) * 2);
    }
}

// Added by Jigao
// NOLINTNEXTLINE
TEST(BTreeInnerNodeTest, InnerNodeInsert_SplitLeftChild) {
    std::vector<std::byte> buffer;
    buffer.resize(1024);
    auto node = new (buffer.data()) BTree::InnerNode();
    ASSERT_EQ(node->count, 0);

    auto n = BTree::InnerNode::kCapacity;
    std::map<uint64_t, uint64_t> kv_map; // Key, the page having larger value as key -- the right pointer

    // Insert children into the inner nodes
    for (uint32_t i = 0, j = 0; i < n - 1; ++i, j = i * 2) {
        if (i == 0 && j == 0) {
          node->init_insert(100, 0, 1000 /* next page or new page */);
          ASSERT_EQ(node->count, 2)
                      << "InnerNode::insert did not increase the the child count";
          // Append current node to expected nodes
          kv_map[100] = 1000;
        } else {
            node->insert(100 - i, 0, (i + 1) * 2 /* next page or new page */);
            ASSERT_EQ(node->count, 2 + i)
                        << "InnerNode::insert did not increase the the child count";
            // Append current node to expected nodes
            kv_map[100 - i] = (i + 1) * 2;
        }
    }
    kv_map[0 /*this is a dummy key, not occurs int the B+ Tree*/] = 0;

    // Check the number of keys & children
    auto keys = node->get_key_vector();
    auto children = node->get_child_vector();

    ASSERT_EQ(keys.size(), n - 1)
                << "inner node must contain " << n - 1 << " keys for " << n << " values";
    ASSERT_EQ(children.size(), n)
                << "inner node must contain " << n << " values";
    ASSERT_EQ(kv_map.size(), n);

    // Check the keys
    uint32_t i = 0;
    for (auto const& x : kv_map) {
        if (x.first != 0 /* not the dummy key */) {
            ASSERT_EQ(x.first, keys[i++]);
        }
    }

    // Check the children
    i = 0;
    for (auto const& x : kv_map) {
        ASSERT_EQ(x.second, children[i++]);
    }
}

// NOLINTNEXTLINE
TEST(BTreeLeafNodeTest, LeafNodeLookUp) {
    std::vector<std::byte> buffer;
    buffer.resize(1024);
    auto node = new (buffer.data()) BTree::LeafNode();
    ASSERT_EQ(node->count, 0);

    auto n = BTree::LeafNode::kCapacity;

    // Insert children into the leaf nodes
    for (uint32_t i = 0, j = 0; i < n; ++i, j = i * 2) {
      node->insert(i, j);

      ASSERT_EQ(node->count, i + 1)
                  << "LeafNode::insert did not increase the the child count";
    }

    // Check the number of keys & values
    auto keys = node->get_key_vector();
    auto children = node->get_value_vector();

    ASSERT_EQ(keys.size(), n)
                << "leaf node must contain " << n << " keys for " << n << " values";
    ASSERT_EQ(children.size(), n)
                << "leaf node must contain " << n << " values";

    // Iteratively erase all values
    for (auto i = 0ul; i < n; ++i) {
        ASSERT_TRUE(node->lookup(i))
            << "k=" << i << " was not in the tree";
    }
}

// NOLINTNEXTLINE
TEST(BTreeLeafNodeTest, LeafNodeErase) {
    std::vector<std::byte> buffer;
    buffer.resize(1024);
    auto node = new (buffer.data()) BTree::LeafNode();
    ASSERT_EQ(node->count, 0);

    auto n = BTree::LeafNode::kCapacity;

    // Insert children into the leaf nodes
    for (uint32_t i = 0, j = 0; i < n; ++i, j = i * 2) {
        node->insert(i, j);

        ASSERT_EQ(node->count, i + 1)
                    << "LeafNode::insert did not increase the the child count";
    }

    // Check the number of keys & values
    auto keys = node->get_key_vector();
    auto children = node->get_value_vector();

    ASSERT_EQ(keys.size(), n)
                << "leaf node must contain " << n << " keys for " << n << " values";
    ASSERT_EQ(children.size(), n)
                << "leaf node must contain " << n << " values";

    // Iteratively erase all values
    for (auto i = 0ul; i < n; ++i) {
        ASSERT_TRUE(node->lookup(i))
                    << "k=" << i << " was not in the tree";
        node->erase(i);
        ASSERT_FALSE(node->lookup(i))
                    << "k=" << i << " was still in the tree";
    }
}

// Added by Jigao
// NOLINTNEXTLINE
TEST(BTreeInnerNodeTest, InnerNodeInsert_SplitRightChild) {
    std::vector<std::byte> buffer;
    buffer.resize(1024);
    auto node = new (buffer.data()) BTree::InnerNode();
    ASSERT_EQ(node->count, 0);

    std::stringstream expected_keys;
    std::stringstream expected_values;
    std::stringstream seen_keys;
    std::stringstream seen_values;

    auto n = BTree::InnerNode::kCapacity;

    // Insert children into the inner nodes
    for (uint32_t i = 0, j = 0; i < n - 1; ++i, j = i * 2) {
        if (i == 0 && j == 0) {
            node->init_insert(i, j, (i + 1) * 2 /* next page or new page */);
            ASSERT_EQ(node->count, 2)
                       << "InnerNode::insert did not increase the the child count";
            // Append current node to expected nodes
            expected_keys << i;
            expected_values << j;
            expected_values << "," << (i + 1) * 2;
        } else {
            node->insert(i, j, (i + 1) * 2 /* next page or new page */);
            ASSERT_EQ(node->count, 2 + i)
                       << "InnerNode::insert did not increase the the child count";
            // Append current node to expected nodes
            expected_keys << ", " << i;
            expected_values << ", " << (i + 1) * 2;
        }
    }

    // Check the number of keys & children
    auto keys = node->get_key_vector();
    auto children = node->get_child_vector();

    ASSERT_EQ(keys.size(), n - 1)
        << "inner node must contain " << n - 1 << " keys for " << n << " values";
    ASSERT_EQ(children.size(), n)
        << "inner node must contain " << n << " values";

    // Check the keys
    for (uint32_t i = 0; i < n - 1 /* Without the last INVALID key */; ++i) {
        auto k = keys[i];
        seen_keys << ((i != 0) ? ", " : "") << k;
        ASSERT_EQ(k, i)
            << "inner node does not store key=" << i << "\n"
            << "EXPECTED:\n"
            << expected_keys.str()
            << "\nSEEN:\n"
            << seen_keys.str();
    }

    // Check the children
    for (uint32_t i = 0, j = 0; i < n; ++i, j = i * 2) {
        auto c = children[i];
        seen_values << ((i != 0) ? ", " : "") << c;
        ASSERT_EQ(c, j)
            << "inner node does not store value=" << j << "\n"
            << "EXPECTED:\n"
            << expected_values.str()
            << "\nSEEN:\n"
            << seen_values.str();
    }
}

// Added by Jigao
// NOLINTNEXTLINE
TEST(BTreeInnerNodeTest, InnerNodeInsert_SplitMixedChildren) {
    std::vector<std::byte> buffer;
    buffer.resize(1024);
    auto node = new (buffer.data()) BTree::InnerNode();
    ASSERT_EQ(node->count, 0);

    auto n = BTree::InnerNode::kCapacity;
    std::map<uint64_t, uint64_t> kv_map; // Key, the page having larger value as key -- the right pointer

    uint64_t last_left_pageid_odd = 0;
    uint64_t last_right_pageid_even = 4;
    uint64_t last_key = 50;

    // Insert children into the inner nodes
    for (uint32_t i = 0, j = 0; i < n - 1; ++i, j = i * 2) {
        if (i == 0 && j == 0) {
            node->init_insert(100, 0, 1000 /* next page or new page */);
            ASSERT_EQ(node->count, 2)
                        << "InnerNode::insert did not increase the the child count";
            // Append current node to expected nodes
            kv_map[100] = 1000;
        } else {
            if ((i & 1) == 0) {
                // Even: larger than last key
                node->insert(last_key + i, last_right_pageid_even, (i + 1) * 2 /* next page or new page */);
                ASSERT_EQ(node->count, 2 + i)
                            << "InnerNode::insert did not increase the the child count";
                // Append current node to expected nodes
                kv_map[last_key + i] = (i + 1) * 2;
                last_right_pageid_even = (i + 1) * 2;
                last_key = last_key + i;
            } else {
                // Odd: less than last key
                node->insert(last_key - i, last_left_pageid_odd, (i + 1) * 2 /* next page or new page */);
                ASSERT_EQ(node->count, 2 + i)
                            << "InnerNode::insert did not increase the the child count";
                // Append current node to expected nodes
                kv_map[last_key - i] = (i + 1) * 2;
                last_key = last_key - i;
            }
        }
    }
    kv_map[0 /*this is a dummy key, not occurs int the B+ Tree*/] = 0;

    // Check the number of keys & children
    auto keys = node->get_key_vector();
    auto children = node->get_child_vector();

    ASSERT_EQ(keys.size(), n - 1)
                << "inner node must contain " << n - 1 << " keys for " << n << " values";
    ASSERT_EQ(children.size(), n)
                << "inner node must contain " << n << " values";
    ASSERT_EQ(kv_map.size(), n);

    // Check the keys
    uint32_t i = 0;
    for (auto const& x : kv_map) {
        if (x.first != 0 /* not the dummy key */) {
            ASSERT_EQ(x.first, keys[i++]);
        }
    }

    // Check the children
    i = 0;
    for (auto const& x : kv_map) {
        ASSERT_EQ(x.second, children[i++]);
    }
}

// Added by Jigao
// NOLINTNEXTLINE
TEST(BTreeInnerNodeTest, InnerNodeSplit) {
    std::vector<std::byte> buffer_left;
    std::vector<std::byte> buffer_right;
    buffer_left.resize(1024);
    buffer_right.resize(1024);

    auto left_node = new (buffer_left.data()) BTree::InnerNode();
    auto right_node = reinterpret_cast<BTree::InnerNode*>(buffer_right.data());
    ASSERT_EQ(left_node->count, 0);
    ASSERT_EQ(right_node->count, 0);

    auto n = BTree::InnerNode::kCapacity;

    // Fill the left node
    for (uint32_t i = 0, j = 0; i < n - 1; ++i, j = i * 2) {
      if (i == 0 && j == 0) {
        left_node->init_insert(i, j, (i + 1) * 2 /* next page or new page */);
        ASSERT_EQ(left_node->count, 2)
                    << "InnerNode::insert did not increase the the child count";
      } else {
        left_node->insert(i, j, (i + 1) * 2 /* next page or new page */);
        ASSERT_EQ(left_node->count, 2 + i)
                    << "InnerNode::insert did not increase the the child count";
      }
    }

    // Check the number of keys & children
    auto left_keys = left_node->get_key_vector();
    auto left_children = left_node->get_child_vector();
    ASSERT_EQ(left_keys.size(), n - 1);
    ASSERT_EQ(left_children.size(), n);

    // Now split the left node
    auto separator = left_node->split(buffer_right.data());
    ASSERT_EQ(left_node->count, n / 2 + 1);
    ASSERT_EQ(right_node->count, n - (n / 2) - 1);
    ASSERT_EQ(separator, n / 2);

    // Check keys & values of the left node
    left_keys = left_node->get_key_vector();
    left_children = left_node->get_child_vector();
    ASSERT_EQ(left_keys.size(), left_node->count - 1);
    ASSERT_EQ(left_children.size(), left_node->count);
    for (auto i = 0; i < left_node->count - 1; ++i) {
        ASSERT_EQ(left_keys[i], i);
    }
    for (auto i = 0; i < left_node->count; ++i) {
        ASSERT_EQ(left_children[i], i * 2);
    }

    // Check keys & values of the right node
    auto right_keys = right_node->get_key_vector();
    auto right_children = right_node->get_child_vector();
    ASSERT_EQ(right_keys.size(), right_node->count - 1);
    ASSERT_EQ(right_children.size(), right_node->count);
    for (auto i = 0; i < right_node->count - 1; ++i) {
        ASSERT_EQ(right_keys[i], left_node->count + i);
    }
    for (auto i = 0; i < right_node->count; ++i) {
        ASSERT_EQ(right_children[i], (left_node->count + i) * 2);
    }
}

// NOLINTNEXTLINE
TEST(BTreeTest, InsertEmptyTree) {
    BufferManager buffer_manager(1024, 100);
    BTree tree(0, buffer_manager);
    ASSERT_FALSE(tree.root);

    tree.insert(42, 21);

    auto test = "inserting an element into an empty B-Tree";
    ASSERT_TRUE(tree.root)
        << test << " does not create a node.";

    auto& root_page = buffer_manager.fix_page(*tree.root, false);
    auto root_node = reinterpret_cast<BTree::Node*>(root_page.get_data());
    Defer root_page_unfix([&]() { buffer_manager.unfix_page(root_page, false); });

    ASSERT_TRUE(root_node->is_leaf())
        << test << " does not create a leaf node.";
    ASSERT_TRUE(root_node->count == 1)
        << test << " does not create a leaf node with count = 1.";
}

// NOLINTNEXTLINE
TEST(BTreeTest, InsertLeafNode) {
    uint32_t page_size = 1024;
    BufferManager buffer_manager(page_size, 100);
    BTree tree(0, buffer_manager);
    ASSERT_FALSE(tree.root);

    for (auto i = 0ul; i < BTree::LeafNode::kCapacity; ++i) {
        tree.insert(i, 2 * i);
    }

    auto test = "inserting BTree::LeafNode::kCapacity elements into an empty B-Tree";
    ASSERT_TRUE(tree.root);

    auto& root_page = buffer_manager.fix_page(*tree.root, false);
    auto root_node = reinterpret_cast<BTree::Node*>(root_page.get_data());
    auto root_inner_node = static_cast<BTree::LeafNode*>(root_node);
    Defer root_page_unfix([&]() { buffer_manager.unfix_page(root_page, false); });

    ASSERT_TRUE(root_node->is_leaf())
        << test << " creates an inner node as root.";
    ASSERT_EQ(root_inner_node->count, BTree::LeafNode::kCapacity)
        << test << " does not store all elements.";

    // Add by Jigao
    // Check keys & values of the root inner node
    auto keys = root_inner_node->get_key_vector();
    auto children = root_inner_node->get_value_vector();
    ASSERT_EQ(keys.size(), root_inner_node->count);
    ASSERT_EQ(children.size(), root_inner_node->count);
    for (auto i = 0; i < root_inner_node->count; ++i) {
        ASSERT_EQ(keys[i], i);
    }
    for (auto i = 0; i < root_inner_node->count; ++i) {
        ASSERT_EQ(children[i], i * 2);
    }
}

// NOLINTNEXTLINE
TEST(BTreeTest, InsertLeafNodeSplit) {
    BufferManager buffer_manager(1024, 100);
    BTree tree(0, buffer_manager);

    ASSERT_FALSE(tree.root);
    ASSERT_EQ(tree.next_page_id, 0);
    for (auto i = 0ul; i < BTree::LeafNode::kCapacity; ++i) {
        tree.insert(i, 2 * i);
        ASSERT_EQ(tree.next_page_id, 1);
    }

    ASSERT_TRUE(tree.root);
    ASSERT_EQ(tree.next_page_id, 1);
    auto* root_page = &buffer_manager.fix_page(*tree.root, false);
    auto root_node = reinterpret_cast<BTree::Node*>(root_page->get_data());
    auto root_inner_node = static_cast<BTree::InnerNode*>(root_node);
    Defer root_page_unfix([&]() { buffer_manager.unfix_page(*root_page, false); });
    ASSERT_TRUE(root_inner_node->is_leaf());
    ASSERT_EQ(root_inner_node->count, BTree::LeafNode::kCapacity);
    root_page_unfix.run();

    // Let there be a split...
    ASSERT_EQ(tree.next_page_id, 1);
    tree.insert(424242, 42);
    ASSERT_EQ(tree.next_page_id, 3);

    auto test = "inserting BTree::LeafNode::kCapacity + 1 elements into an empty B-Tree";

    ASSERT_TRUE(tree.root)
        << test << " removes the root :-O";

    root_page = &buffer_manager.fix_page(*tree.root, false);
    root_node = reinterpret_cast<BTree::Node*>(root_page->get_data());
    root_inner_node = static_cast<BTree::InnerNode*>(root_node);
    root_page_unfix = Defer([&]() { buffer_manager.unfix_page(*root_page, false); });

    ASSERT_FALSE(root_inner_node->is_leaf())
        << test << " does not create a root inner node";
    ASSERT_EQ(root_inner_node->count, 2)
        << test << " creates a new root with count != 2";

    // Add by Jigao
    // Checking the details of two leaf pages
    auto root_children = root_inner_node->get_child_vector();
    ASSERT_EQ(root_children.size(), 2);

    auto left_leaf_page = &buffer_manager.fix_page(root_children[0], false);
    auto left_leaf_node = reinterpret_cast<BTree::LeafNode*>(left_leaf_page->get_data());
    auto left_leaf_page_unfix = Defer([&]() { buffer_manager.unfix_page(*left_leaf_page, false); });

    auto right_leaf_page = &buffer_manager.fix_page(root_children[1], false);
    auto right_leaf_node = reinterpret_cast<BTree::LeafNode*>(right_leaf_page->get_data());
    auto right_leaf_page_unfix = Defer([&]() { buffer_manager.unfix_page(*right_leaf_page, false); });

    ASSERT_EQ(left_leaf_node->count, (BTree::LeafNode::kCapacity + 1) / 2 + 1);
    ASSERT_EQ(right_leaf_node->count, (BTree::LeafNode::kCapacity + 1) - ((BTree::LeafNode::kCapacity + 1) / 2) - 1);

    auto keys = left_leaf_node->get_key_vector();
    auto values = left_leaf_node->get_value_vector();
    ASSERT_EQ(keys.size(), left_leaf_node->count);
    ASSERT_EQ(values.size(), left_leaf_node->count);
    for (auto i = 0; i < left_leaf_node->count; ++i) {
      ASSERT_EQ(keys[i], i);
    }
    for (auto i = 0; i < left_leaf_node->count; ++i) {
      ASSERT_EQ(values[i], i * 2);
    }

    keys = right_leaf_node->get_key_vector();
    values = right_leaf_node->get_value_vector();
    ASSERT_EQ(keys.size(), right_leaf_node->count);
    ASSERT_EQ(values.size(), right_leaf_node->count);
    for (auto i = 0; i < right_leaf_node->count - 1; ++i) {
      ASSERT_EQ(keys[i], i + left_leaf_node->count);
    }
    ASSERT_EQ(keys[right_leaf_node->count - 1], 424242);
    for (auto i = 0; i < right_leaf_node->count - 1; ++i) {
      ASSERT_EQ(values[i], (i + left_leaf_node->count) * 2);
    }
    ASSERT_EQ(values[right_leaf_node->count - 1], 42);
}

// NOLINTNEXTLINE
TEST(BTreeTest, LookupEmptyTree) {
    BufferManager buffer_manager(1024, 100);
    BTree tree(0, buffer_manager);

    auto test = "searching for a non-existing element in an empty B-Tree";

    ASSERT_FALSE(tree.lookup(42))
        << test << " seems to return something :-O";
}

// NOLINTNEXTLINE
TEST(BTreeTest, LookupSingleLeaf) {
    BufferManager buffer_manager(1024, 100);
    BTree tree(0, buffer_manager);

    // Fill one page
    for (auto i = 0ul; i < BTree::LeafNode::kCapacity; ++i) {
        tree.insert(i, 2 * i);
        ASSERT_TRUE(tree.lookup(i))
            << "searching for the just inserted key k=" << i << " yields nothing";
    }

    // Lookup all values
    for (auto i = 0ul; i < BTree::LeafNode::kCapacity; ++i) {
        auto v = tree.lookup(i);
        ASSERT_TRUE(v)
            << "key=" << i << " is missing";
        ASSERT_EQ(*v, 2 * i)
            << "key=" << i << " should have the value v=" << 2*i;
    }
}

// NOLINTNEXTLINE
TEST(BTreeTest, LookupSingleSplit) {
    BufferManager buffer_manager(1024, 100);
    BTree tree(0, buffer_manager);

    // Insert values
    for (auto i = 0ul; i < BTree::LeafNode::kCapacity; ++i) {
        tree.insert(i, 2 * i);
    }

    // Lookup all values
    for (auto i = 0ul; i < BTree::LeafNode::kCapacity; ++i) {
        auto v = tree.lookup(i);
        ASSERT_TRUE(v)
                    << "key=" << i << " is missing";
        ASSERT_EQ(*v, 2* i)
                    << "key=" << i << " should have the value v=" << 2*i;
    }

    tree.insert(BTree::LeafNode::kCapacity, 2 * BTree::LeafNode::kCapacity);
    ASSERT_TRUE(tree.lookup(BTree::LeafNode::kCapacity))
        << "searching for the just inserted key k=" << (BTree::LeafNode::kCapacity + 1) << " yields nothing";

    // Lookup all values
    for (auto i = 0ul; i < BTree::LeafNode::kCapacity + 1; ++i) {
        auto v = tree.lookup(i);
        ASSERT_TRUE(v)
            << "key=" << i << " is missing";
        ASSERT_EQ(*v, 2* i)
            << "key=" << i << " should have the value v=" << 2*i;
    }
}

// NOLINTNEXTLINE
TEST(BTreeTest, LookupMultipleSplitsIncreasing) {
    BufferManager buffer_manager(1024, 100);
    BTree tree(0, buffer_manager);
    auto n = 100 * BTree::LeafNode::kCapacity;

    // Insert values
    for (auto i = 0ul; i < n; ++i) {
        ASSERT_FALSE(tree.lookup(i));
        tree.insert(i, 2 * i);
        ASSERT_TRUE(tree.lookup(i))
            << "searching for the just inserted key k=" << i << " yields nothing";
    }

    // Lookup all values
    for (auto i = 0ul; i < n; ++i) {
        auto v = tree.lookup(i);
        ASSERT_TRUE(v)
            << "key=" << i << " is missing";
        ASSERT_EQ(*v, 2* i)
            << "key=" << i << " should have the value v=" << 2*i;
    }
}

// NOLINTNEXTLINE
TEST(BTreeTest, LookupMultipleSplitsDecreasing) {
    BufferManager buffer_manager(1024, 100);
    BTree tree(0, buffer_manager);
    auto n = 10 * BTree::LeafNode::kCapacity;

    // Insert values
    for (auto i = n; i > 0; --i) {
        ASSERT_FALSE(tree.lookup(i));
        tree.insert(i, 2 * i);
        ASSERT_TRUE(tree.lookup(i))
            << "searching for the just inserted key k=" << i << " yields nothing";
    }

    // Lookup all values
    for (auto i = n; i > 0; --i) {
        auto v = tree.lookup(i);
        ASSERT_TRUE(v)
            << "key=" << i << " is missing";
        ASSERT_EQ(*v, 2* i)
            << "key=" << i << " should have the value v=" << 2*i;
    }
}

// NOLINTNEXTLINE
TEST(BTreeTest, LookupRandomNonRepeating) {
    BufferManager buffer_manager(1024, 100);
    BTree tree(0, buffer_manager);
    auto n = 10 * BTree::LeafNode::kCapacity;

    // Generate random non-repeating key sequence
    std::vector<uint64_t> keys(n);
    std::iota(keys.begin(), keys.end(), n);
    std::mt19937_64 engine(0);
    std::shuffle(keys.begin(), keys.end(), engine);

    // Insert values
    for (auto i = 0ul; i < n; ++i) {
        tree.insert(keys[i], 2 * keys[i]);
        ASSERT_TRUE(tree.lookup(keys[i]))
            << "searching for the just inserted key k=" << keys[i] << " after i=" << i << " inserts yields nothing";
    }

    // Lookup all values
    for (auto i = 0ul; i < n; ++i) {
        auto v = tree.lookup(keys[i]);
        ASSERT_TRUE(v)
            << "key=" << keys[i] << " is missing";
        ASSERT_EQ(*v, 2 * keys[i])
            << "key=" << keys[i] << " should have the value v=" << keys[i];
    }
}

// NOLINTNEXTLINE
TEST(BTreeTest, LookupRandomRepeating) {
    BufferManager buffer_manager(1024, 100);
    BTree tree(0, buffer_manager);
    auto n = 10 * BTree::LeafNode::kCapacity;

    // Insert & updated 100 keys at random
    std::mt19937_64 engine{0};
    std::uniform_int_distribution<uint64_t> key_distr(0, 99);
    std::vector<uint64_t> values(100);

    for (auto i = 1ul; i < n; ++i) {
        uint64_t rand_key = key_distr(engine);
        values[rand_key] = i;
        tree.insert(rand_key, i);

        auto v = tree.lookup(rand_key);
        ASSERT_TRUE(v)
            << "searching for the just inserted key k=" << rand_key << " after i=" << (i - 1) << " inserts yields nothing";
        ASSERT_EQ(*v, i)
            << "overwriting k=" << rand_key << " with value v=" << i << " failed";
    }

    // Lookup all values
    for (auto i = 0ul; i < 100; ++i) {
        if (values[i] == 0) {
            continue;
        }
        auto v = tree.lookup(i);
        ASSERT_TRUE(v)
            << "key=" << i << " is missing";
        ASSERT_EQ(*v, values[i])
            << "key=" << i << " should have the value v=" << values[i];
    }
}

// NOLINTNEXTLINE
TEST(BTreeTest, Erase) {
    BufferManager buffer_manager(1024, 100);
    BTree tree(0, buffer_manager);

    // Insert values
    for (auto i = 0ul; i < 2 * BTree::LeafNode::kCapacity; ++i) {
        tree.insert(i, 2 * i);
    }

    // Iteratively erase all values
    for (auto i = 0ul; i < 2 * BTree::LeafNode::kCapacity; ++i) {
        ASSERT_TRUE(tree.lookup(i))
            << "k=" << i << " was not in the tree";
        tree.erase(i);
        ASSERT_FALSE(tree.lookup(i))
            << "k=" << i << " was not removed from the tree";
    }
}

}  // namespace
