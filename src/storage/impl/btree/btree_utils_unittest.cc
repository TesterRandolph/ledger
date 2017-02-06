// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/storage/impl/btree/btree_utils.h"

#include <stdio.h>

#include "apps/ledger/src/storage/fake/fake_page_storage.h"
#include "apps/ledger/src/storage/impl/btree/entry_change_iterator.h"
#include "apps/ledger/src/storage/impl/btree/tree_node.h"
#include "apps/ledger/src/storage/public/constants.h"
#include "apps/ledger/src/storage/public/types.h"
#include "apps/ledger/src/storage/test/storage_test_utils.h"
#include "apps/ledger/src/test/capture.h"
#include "apps/ledger/src/test/test_with_message_loop.h"
#include "gtest/gtest.h"
#include "lib/ftl/arraysize.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/strings/string_printf.h"
#include "lib/mtl/socket/strings.h"
#include "lib/mtl/tasks/message_loop.h"

namespace storage {
namespace {

const int kTestNodeSize = 4;

class TrackGetObjectFakePageStorage : public fake::FakePageStorage {
 public:
  TrackGetObjectFakePageStorage(PageId id) : fake::FakePageStorage(id) {}
  ~TrackGetObjectFakePageStorage() {}

  void GetObject(
      ObjectIdView object_id,
      const std::function<void(Status, std::unique_ptr<const Object>)>&
          callback) override {
    object_requests.insert(object_id.ToString());
    fake::FakePageStorage::GetObject(object_id, callback);
  }

  std::set<ObjectId> object_requests;
};

class BTreeUtilsTest : public StorageTest {
 public:
  BTreeUtilsTest() : fake_storage_("page_id") {}

  ~BTreeUtilsTest() override {}

  // Test:
  void SetUp() override {
    ::testing::Test::SetUp();
    std::srand(0);
  }

 protected:
  PageStorage* GetStorage() override { return &fake_storage_; }

  ObjectId CreateTree(std::vector<EntryChange>& entries) {
    ObjectId root_id;
    EXPECT_TRUE(GetEmptyNodeId(&root_id));

    Status status;
    ObjectId new_root_id;
    std::unordered_set<ObjectId> new_nodes;
    btree::ApplyChanges(
        &fake_storage_, root_id, kTestNodeSize,
        std::make_unique<EntryChangeIterator>(entries.begin(), entries.end()),
        ::test::Capture([this] { message_loop_.PostQuitTask(); }, &status,
                        &new_root_id, &new_nodes));
    EXPECT_FALSE(RunLoopWithTimeout());
    EXPECT_EQ(Status::OK, status);
    return new_root_id;
  }

  std::vector<Entry> GetEntriesList(ObjectId root_id) {
    std::vector<Entry> entries;
    auto on_next = [&entries](btree::EntryAndNodeId entry) {
      entries.push_back(entry.entry);
      return true;
    };
    auto on_done = [this](Status status) {
      EXPECT_EQ(Status::OK, status);
      message_loop_.PostQuitTask();
    };
    btree::ForEachEntry(&fake_storage_, root_id, "", std::move(on_next),
                        std::move(on_done));
    EXPECT_FALSE(RunLoopWithTimeout());
    return entries;
  }

  TrackGetObjectFakePageStorage fake_storage_;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(BTreeUtilsTest);
};

TEST_F(BTreeUtilsTest, GetNodeLevel) {
  size_t level_distribution[4];
  memset(level_distribution, 0, sizeof(level_distribution));

  for (size_t i = 0; i < 1000; ++i) {
    ftl::StringView key(reinterpret_cast<char*>(&i), sizeof(i));
    level_distribution[btree::GetNodeLevel(key, arraysize(level_distribution))]++;
  }

  EXPECT_TRUE(std::is_sorted(level_distribution, level_distribution + arraysize(level_distribution),
                             [](int v1, int v2) { return v2 < v1; }));
  EXPECT_NE(0u, level_distribution[1]);
}

TEST_F(BTreeUtilsTest, ApplyChangesFromEmpty) {
  ObjectId root_id;
  ASSERT_TRUE(GetEmptyNodeId(&root_id));
  std::vector<EntryChange> changes;
  ASSERT_TRUE(CreateEntryChanges(4, &changes));

  Status status;
  ObjectId new_root_id;
  std::unordered_set<ObjectId> new_nodes;
  // Expected layout (X is key "keyX"):
  // [1, 2, 3, 4]
  btree::ApplyChanges(
      &fake_storage_, root_id, 4,
      std::make_unique<EntryChangeIterator>(changes.begin(), changes.end()),
      ::test::Capture([this] { message_loop_.PostQuitTask(); }, &status,
                      &new_root_id, &new_nodes));
  ASSERT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(Status::OK, status);
  EXPECT_EQ(1u, new_nodes.size());
  EXPECT_TRUE(new_nodes.find(new_root_id) != new_nodes.end());

  std::vector<Entry> entries = GetEntriesList(new_root_id);
  ASSERT_EQ(changes.size(), entries.size());
  for (size_t i = 0; i < entries.size(); ++i) {
    EXPECT_EQ(changes[i].entry, entries[i]);
  }
}

TEST_F(BTreeUtilsTest, ApplyChangesManyEntries) {
  ObjectId root_id;
  ASSERT_TRUE(GetEmptyNodeId(&root_id));
  std::vector<EntryChange> golden_entries;
  ASSERT_TRUE(CreateEntryChanges(11, &golden_entries));

  Status status;
  ObjectId new_root_id;
  std::unordered_set<ObjectId> new_nodes;
  // Expected layout (XX is key "keyXX"):
  //                 [03, 07]
  //            /       |            \
  // [00, 01, 02]  [04, 05, 06] [08, 09, 10]
  btree::ApplyChanges(&fake_storage_, root_id, 4,
                      std::make_unique<EntryChangeIterator>(
                          golden_entries.begin(), golden_entries.end()),
                      ::test::Capture([this] { message_loop_.PostQuitTask(); },
                                      &status, &new_root_id, &new_nodes));
  ASSERT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(Status::OK, status);
  EXPECT_EQ(4u, new_nodes.size());
  EXPECT_TRUE(new_nodes.find(new_root_id) != new_nodes.end());

  std::vector<Entry> entries = GetEntriesList(new_root_id);
  ASSERT_EQ(golden_entries.size(), entries.size());
  for (size_t i = 0; i < golden_entries.size(); ++i) {
    EXPECT_EQ(golden_entries[i].entry, entries[i]);
  }

  Entry new_entry = {"key071", MakeObjectId("objectid071"), KeyPriority::EAGER};
  std::vector<EntryChange> new_change{EntryChange{new_entry, false}};
  // Insert key "071" between keys "07" and "08".
  golden_entries.insert(golden_entries.begin() + 8, new_change[0]);

  new_nodes.clear();
  ObjectId new_root_id2;
  // Expected layout (XX is key "keyXX"):
  //                 [03, 07]
  //            /       |            \
  // [00, 01, 02]  [04, 05, 06] [071, 08, 09, 10]
  btree::ApplyChanges(&fake_storage_, new_root_id, 4,
                      std::make_unique<EntryChangeIterator>(new_change.begin(),
                                                            new_change.end()),
                      ::test::Capture([this] { message_loop_.PostQuitTask(); },
                                      &status, &new_root_id2, &new_nodes));
  ASSERT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(Status::OK, status);
  EXPECT_NE(new_root_id, new_root_id2);
  // The root and the 3rd child have changed.
  EXPECT_EQ(2u, new_nodes.size());
  EXPECT_TRUE(new_nodes.find(new_root_id2) != new_nodes.end());

  entries = GetEntriesList(new_root_id2);
  ASSERT_EQ(golden_entries.size(), entries.size());
  for (size_t i = 0; i < golden_entries.size(); ++i) {
    EXPECT_EQ(golden_entries[i].entry, entries[i]);
  }
}

TEST_F(BTreeUtilsTest, DeleteChanges) {
  // Expected layout (XX is key "keyXX"):
  //                 [03, 07]
  //            /       |            \
  // [00, 01, 02]  [04, 05, 06] [08, 09, 10, 11]
  std::vector<EntryChange> golden_entries;
  ASSERT_TRUE(CreateEntryChanges(11, &golden_entries));
  ObjectId root_id = CreateTree(golden_entries);

  // Delete entries.
  std::vector<Entry> entries_to_delete{golden_entries[2].entry,
                                       golden_entries[4].entry};
  std::vector<EntryChange> delete_changes;
  for (size_t i = 0; i < entries_to_delete.size(); ++i) {
    delete_changes.push_back(EntryChange{entries_to_delete[i], true});
  }

  // Expected layout (XX is key "keyXX"):
  //            [03, 07]
  //         /     |        \
  // [00, 01]  [05, 06]    [08, 09, 10, 11]
  Status status;
  ObjectId new_root_id;
  std::unordered_set<ObjectId> new_nodes;
  btree::ApplyChanges(&fake_storage_, root_id, 4,
                      std::make_unique<EntryChangeIterator>(
                          delete_changes.begin(), delete_changes.end()),
                      ::test::Capture([this] { message_loop_.PostQuitTask(); },
                                      &status, &new_root_id, &new_nodes));
  ASSERT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(Status::OK, status);
  EXPECT_NE(root_id, new_root_id);
  // The root and the first 2 children have changed.
  EXPECT_EQ(3u, new_nodes.size());
  EXPECT_TRUE(new_nodes.find(new_root_id) != new_nodes.end());

  std::vector<Entry> entries = GetEntriesList(new_root_id);
  ASSERT_EQ(golden_entries.size() - entries_to_delete.size(), entries.size());
  size_t deleted_index = 0;
  for (size_t i = 0; i < golden_entries.size(); ++i) {
    if (golden_entries[i].entry == entries_to_delete[deleted_index]) {
      // Skip the deleted entries.
      deleted_index++;
      continue;
    }
    EXPECT_EQ(golden_entries[i].entry, entries[i - deleted_index]);
  }
}

TEST_F(BTreeUtilsTest, GetObjectIdsFromEmpty) {
  ObjectId root_id;
  ASSERT_TRUE(GetEmptyNodeId(&root_id));
  Status status;
  std::set<ObjectId> object_ids;
  btree::GetObjectIds(&fake_storage_, root_id,
                      ::test::Capture([this] { message_loop_.PostQuitTask(); },
                                      &status, &object_ids));
  ASSERT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(Status::OK, status);
  EXPECT_EQ(1u, object_ids.size());
  EXPECT_TRUE(object_ids.find(root_id) != object_ids.end());
}

TEST_F(BTreeUtilsTest, GetObjectOneNodeTree) {
  std::vector<EntryChange> entries;
  ASSERT_TRUE(CreateEntryChanges(kTestNodeSize, &entries));
  ObjectId root_id = CreateTree(entries);

  Status status;
  std::set<ObjectId> object_ids;
  btree::GetObjectIds(&fake_storage_, root_id,
                      ::test::Capture([this] { message_loop_.PostQuitTask(); },
                                      &status, &object_ids));
  ASSERT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(Status::OK, status);
  EXPECT_EQ(5u, object_ids.size());
  EXPECT_TRUE(object_ids.find(root_id) != object_ids.end());
  for (EntryChange& e : entries) {
    EXPECT_TRUE(object_ids.find(e.entry.object_id) != object_ids.end());
  }
}

TEST_F(BTreeUtilsTest, GetObjectIdsBigTree) {
  std::vector<EntryChange> entries;
  ASSERT_TRUE(CreateEntryChanges(99, &entries));
  ObjectId root_id = CreateTree(entries);

  Status status;
  std::set<ObjectId> object_ids;
  btree::GetObjectIds(&fake_storage_, root_id,
                      ::test::Capture([this] { message_loop_.PostQuitTask(); },
                                      &status, &object_ids));
  ASSERT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(Status::OK, status);
  EXPECT_EQ(99u + 25u, object_ids.size());
  EXPECT_TRUE(object_ids.find(root_id) != object_ids.end());
  for (EntryChange& e : entries) {
    EXPECT_TRUE(object_ids.find(e.entry.object_id) != object_ids.end());
  }
}

TEST_F(BTreeUtilsTest, GetObjectsFromSync) {
  std::vector<EntryChange> entries;
  ASSERT_TRUE(CreateEntryChanges(5, &entries));
  entries[3].entry.priority = KeyPriority::LAZY;
  ObjectId root_id = CreateTree(entries);

  fake_storage_.object_requests.clear();
  Status status;
  // Expected layout (XX is key "keyXX"):
  //        [02]
  //      /      \
  // [00, 01]  [03, 04]
  btree::GetObjectsFromSync(
      root_id, &fake_storage_,
      ::test::Capture([this] { message_loop_.PostQuitTask(); }, &status));
  ASSERT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(Status::OK, status);

  std::vector<ObjectId> object_requests;
  std::copy(fake_storage_.object_requests.begin(),
            fake_storage_.object_requests.end(),
            std::back_inserter(object_requests));
  // There are 8 objects: 3 nodes and 4 eager values and 1 lazy. Except from the
  // lazy object, all others should have been requested.
  EXPECT_EQ(3 + 4u, object_requests.size());

  std::set<ObjectId> object_ids;
  btree::GetObjectIds(&fake_storage_, root_id,
                      ::test::Capture([this] { message_loop_.PostQuitTask(); },
                                      &status, &object_ids));
  ASSERT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(Status::OK, status);
  ASSERT_EQ(3 + 5u, object_ids.size());
  for (ObjectId& id : object_requests) {
    // entries[3] contains the lazy value.
    if (id != entries[3].entry.object_id) {
      EXPECT_TRUE(object_ids.find(id) != object_ids.end());
    }
  }
}

TEST_F(BTreeUtilsTest, ForEachEmptyTree) {
  std::vector<EntryChange> entries = {};
  ObjectId root_id = CreateTree(entries);
  auto on_next = [](btree::EntryAndNodeId e) {
    // Fail: There are no elements in the tree.
    EXPECT_TRUE(false);
    return false;
  };
  auto on_done = [this](Status status) {
    EXPECT_EQ(Status::OK, status);
    message_loop_.PostQuitTask();
  };
  btree::ForEachEntry(&fake_storage_, root_id, "", std::move(on_next),
                      std::move(on_done));
  ASSERT_FALSE(RunLoopWithTimeout());
}

TEST_F(BTreeUtilsTest, ForEachAllEntries) {
  // Create a tree from entries with keys from 00-99.
  std::vector<EntryChange> entries;
  ASSERT_TRUE(CreateEntryChanges(100, &entries));
  ObjectId root_id = CreateTree(entries);

  int current_key = 0;
  auto on_next = [&current_key](btree::EntryAndNodeId e) {
    EXPECT_EQ(ftl::StringPrintf("key%02d", current_key), e.entry.key);
    current_key++;
    return true;
  };
  auto on_done = [this](Status status) {
    EXPECT_EQ(Status::OK, status);
    message_loop_.PostQuitTask();
  };
  btree::ForEachEntry(&fake_storage_, root_id, "", on_next, on_done);
  ASSERT_FALSE(RunLoopWithTimeout());
}

TEST_F(BTreeUtilsTest, ForEachEntryPrefix) {
  // Create a tree from entries with keys from 00-99.
  std::vector<EntryChange> entries;
  ASSERT_TRUE(CreateEntryChanges(100, &entries));
  ObjectId root_id = CreateTree(entries);

  // Find all entries with "key3" prefix in the key.
  std::string prefix = "key3";
  int current_key = 30;
  auto on_next = [&current_key, &prefix](btree::EntryAndNodeId e) {
    if (e.entry.key.substr(0, prefix.length()) != prefix) {
      return false;
    }
    EXPECT_EQ(ftl::StringPrintf("key%02d", current_key++), e.entry.key);
    return true;
  };
  auto on_done = [this, &current_key](Status status) {
    EXPECT_EQ(Status::OK, status);
    EXPECT_EQ(40, current_key);
    message_loop_.PostQuitTask();
  };
  btree::ForEachEntry(&fake_storage_, root_id, prefix, on_next, on_done);
  ASSERT_FALSE(RunLoopWithTimeout());
}

TEST_F(BTreeUtilsTest, ForEachDiff) {
  std::unique_ptr<const Object> object;
  ASSERT_TRUE(AddObject("change1", &object));
  ObjectId object_id = object->GetId();

  std::vector<EntryChange> changes;
  ASSERT_TRUE(CreateEntryChanges(50, &changes));
  ObjectId base_root_id = CreateTree(changes);
  changes.clear();
  // Update value for key10.
  changes.push_back(
      EntryChange{Entry{"key1", object_id, KeyPriority::LAZY}, false});
  // Add entry key255.
  changes.push_back(
      EntryChange{Entry{"key255", object_id, KeyPriority::LAZY}, false});
  // Remove entry key40.
  changes.push_back(EntryChange{Entry{"key40", "", KeyPriority::LAZY}, true});

  Status status;
  ObjectId other_root_id;
  std::unordered_set<ObjectId> new_nodes;
  btree::ApplyChanges(
      &fake_storage_, base_root_id, kTestNodeSize,
      std::make_unique<EntryChangeIterator>(changes.begin(), changes.end()),
      ::test::Capture([this] { message_loop_.PostQuitTask(); }, &status,
                      &other_root_id, &new_nodes));
  ASSERT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(Status::OK, status);

  // ForEachDiff should return all changes just applied.
  size_t current_change = 0;
  btree::ForEachDiff(
      &fake_storage_, base_root_id, other_root_id,
      [&changes, &current_change](EntryChange e) {
        EXPECT_EQ(changes[current_change].deleted, e.deleted);
        if (e.deleted) {
          EXPECT_EQ(changes[current_change].entry.key, e.entry.key);
        } else {
          EXPECT_EQ(changes[current_change].entry, e.entry);
        }
        ++current_change;
        return true;
      },
      ::test::Capture([this] { message_loop_.PostQuitTask(); }, &status));
  ASSERT_FALSE(RunLoopWithTimeout());
  ASSERT_EQ(Status::OK, status);
  EXPECT_EQ(changes.size(), current_change);
}

}  // namespace
}  // namespace storage
