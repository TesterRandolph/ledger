// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/storage/impl/btree/btree_builder.h"

#include "apps/ledger/src/storage/fake/fake_page_storage.h"
#include "apps/ledger/src/storage/impl/btree/commit_contents_impl.h"
#include "apps/ledger/src/storage/impl/btree/entry_change_iterator.h"
#include "apps/ledger/src/storage/impl/btree/tree_node.h"
#include "apps/ledger/src/storage/public/types.h"
#include "gtest/gtest.h"
#include "lib/ftl/logging.h"

namespace storage {
namespace {

class BTreeBuilderTest : public ::testing::Test {
 public:
  BTreeBuilderTest() : fake_storage_("page_id") {}

  ~BTreeBuilderTest() override {}

  // Test:
  void SetUp() override {
    ::testing::Test::SetUp();
    std::srand(0);
  }

  ObjectId CreateEmptyContents() {
    ObjectId id;
    EXPECT_EQ(Status::OK,
              TreeNode::FromEntries(&fake_storage_, std::vector<Entry>(),
                                    std::vector<ObjectId>(1), &id));
    return id;
  }

 protected:
  fake::FakePageStorage fake_storage_;

  FTL_DISALLOW_COPY_AND_ASSIGN(BTreeBuilderTest);
};

TEST_F(BTreeBuilderTest, ApplyChangesFromEmpty) {
  ObjectId root_id = CreateEmptyContents();

  Entry entry1 = Entry{"key1", "objectid1", KeyPriority::EAGER};
  Entry entry2 = Entry{"key2", "objectid2", KeyPriority::LAZY};
  Entry entry3 = Entry{"key3", "objectid3", KeyPriority::EAGER};
  Entry entry4 = Entry{"key4", "objectid4", KeyPriority::EAGER};
  std::vector<EntryChange> changes{
      EntryChange{entry1, false}, EntryChange{entry2, false},
      EntryChange{entry3, false}, EntryChange{entry4, false}};

  // Expected layout (X is key "keyX"):
  // [1, 2, 3, 4]
  ObjectId new_root_id;
  BTreeBuilder::ApplyChanges(
      &fake_storage_, root_id, 4,
      std::unique_ptr<Iterator<const EntryChange>>(
          new EntryChangeIterator(changes.begin(), changes.end())),
      [&new_root_id](Status status, ObjectId obj_id,
                     std::unordered_set<ObjectId>&& new_nodes) {
        EXPECT_EQ(Status::OK, status);
        EXPECT_EQ(1u, new_nodes.size());
        EXPECT_TRUE(new_nodes.find(obj_id) != new_nodes.end());
        new_root_id = obj_id;
      });

  CommitContentsImpl reader(new_root_id, &fake_storage_);
  std::unique_ptr<Iterator<const Entry>> entries = reader.begin();
  EXPECT_TRUE(entries->Valid());
  EXPECT_EQ(entry1, **entries);
  entries->Next();
  EXPECT_TRUE(entries->Valid());
  EXPECT_EQ(entry2, **entries);
  entries->Next();
  EXPECT_TRUE(entries->Valid());
  EXPECT_EQ(entry3, **entries);
  entries->Next();
  EXPECT_TRUE(entries->Valid());
  EXPECT_EQ(entry4, **entries);
  entries->Next();
  EXPECT_FALSE(entries->Valid());
}

TEST_F(BTreeBuilderTest, ApplyChangesManyEntries) {
  ObjectId root_id = CreateEmptyContents();

  std::vector<Entry> golden_entries{
      {"key00", "objectid00", KeyPriority::EAGER},
      {"key01", "objectid01", KeyPriority::EAGER},
      {"key02", "objectid02", KeyPriority::LAZY},
      {"key03", "objectid03", KeyPriority::EAGER},
      {"key04", "objectid04", KeyPriority::EAGER},
      {"key05", "objectid05", KeyPriority::EAGER},
      {"key06", "objectid06", KeyPriority::EAGER},
      {"key07", "objectid07", KeyPriority::EAGER},
      {"key08", "objectid08", KeyPriority::EAGER},
      {"key09", "objectid09", KeyPriority::EAGER},
      {"key10", "objectid10", KeyPriority::EAGER}};
  std::vector<EntryChange> changes{
      EntryChange{golden_entries[0], false},
      EntryChange{golden_entries[1], false},
      EntryChange{golden_entries[2], false},
      EntryChange{golden_entries[3], false},
      EntryChange{golden_entries[4], false},
      EntryChange{golden_entries[5], false},
      EntryChange{golden_entries[6], false},
      EntryChange{golden_entries[7], false},
      EntryChange{golden_entries[8], false},
      EntryChange{golden_entries[9], false},
      EntryChange{golden_entries[10], false},
  };
  ObjectId new_root_id;
  // Expected layout (XX is key "keyXX"):
  //                 [03, 07]
  //            /       |            \
  // [00, 01, 02]  [04, 05, 06] [08, 09, 10]
  BTreeBuilder::ApplyChanges(
      &fake_storage_, root_id, 4,
      std::unique_ptr<Iterator<const EntryChange>>(
          new EntryChangeIterator(changes.begin(), changes.end())),
      [&new_root_id](Status status, ObjectId obj_id,
                     std::unordered_set<ObjectId>&& new_nodes) {
        EXPECT_EQ(Status::OK, status);
        EXPECT_EQ(4u, new_nodes.size());
        EXPECT_TRUE(new_nodes.find(obj_id) != new_nodes.end());
        new_root_id = obj_id;
      });

  CommitContentsImpl reader(new_root_id, &fake_storage_);
  std::unique_ptr<Iterator<const Entry>> entries = reader.begin();
  for (size_t i = 0; i < golden_entries.size(); ++i) {
    EXPECT_TRUE(entries->Valid());
    EXPECT_EQ(golden_entries[i], **entries)
        << "Expected " << golden_entries[i].key << " but found "
        << (*entries)->key;
    entries->Next();
  }
  EXPECT_FALSE(entries->Valid());

  Entry new_entry = {"key071", "objectid071", KeyPriority::EAGER};
  std::vector<EntryChange> new_change{EntryChange{new_entry, false}};
  golden_entries.insert(golden_entries.begin() + 8, new_entry);

  // Expected layout (XX is key "keyXX"):
  //                 [03, 07]
  //            /       |            \
  // [00, 01, 02]  [04, 05, 06] [071, 08, 09, 10]
  ObjectId new_root_id2;
  BTreeBuilder::ApplyChanges(
      &fake_storage_, new_root_id, 4,
      std::unique_ptr<Iterator<const EntryChange>>(
          new EntryChangeIterator(new_change.begin(), new_change.end())),
      [&new_root_id2](Status status, ObjectId obj_id,
                      std::unordered_set<ObjectId>&& new_nodes) {
        EXPECT_EQ(Status::OK, status);
        // The root and the 3rd child have changed.
        EXPECT_EQ(2u, new_nodes.size());
        EXPECT_TRUE(new_nodes.find(obj_id) != new_nodes.end());
        new_root_id2 = obj_id;
      });

  CommitContentsImpl reader2(new_root_id2, &fake_storage_);
  entries = reader2.begin();
  for (size_t i = 0; i < golden_entries.size(); ++i) {
    EXPECT_TRUE(entries->Valid());
    EXPECT_EQ(golden_entries[i], **entries)
        << "Expected " << golden_entries[i].key << " but found "
        << (*entries)->key;
    entries->Next();
  }
  EXPECT_FALSE(entries->Valid());
}

TEST_F(BTreeBuilderTest, DeleteChanges) {
  ObjectId root_id = CreateEmptyContents();

  std::vector<Entry> golden_entries{
      {"key00", "objectid00", KeyPriority::EAGER},
      {"key01", "objectid01", KeyPriority::EAGER},
      {"key02", "objectid02", KeyPriority::LAZY},
      {"key03", "objectid03", KeyPriority::EAGER},
      {"key04", "objectid04", KeyPriority::EAGER},
      {"key05", "objectid05", KeyPriority::EAGER},
      {"key06", "objectid06", KeyPriority::EAGER},
      {"key07", "objectid07", KeyPriority::EAGER},
      {"key08", "objectid08", KeyPriority::EAGER},
      {"key09", "objectid09", KeyPriority::EAGER},
      {"key10", "objectid10", KeyPriority::EAGER}};
  std::vector<EntryChange> changes{
      EntryChange{golden_entries[0], false},
      EntryChange{golden_entries[1], false},
      EntryChange{golden_entries[2], false},
      EntryChange{golden_entries[3], false},
      EntryChange{golden_entries[4], false},
      EntryChange{golden_entries[5], false},
      EntryChange{golden_entries[6], false},
      EntryChange{golden_entries[7], false},
      EntryChange{golden_entries[8], false},
      EntryChange{golden_entries[9], false},
      EntryChange{golden_entries[10], false},
  };

  std::vector<Entry> entries_to_delete{golden_entries[2], golden_entries[4]};

  // Expected layout (XX is key "keyXX"):
  //                 [03, 07]
  //            /       |            \
  // [00, 01, 02]  [04, 05, 06] [071, 08, 09, 10]
  ObjectId tmp_root_id;
  BTreeBuilder::ApplyChanges(
      &fake_storage_, root_id, 4,
      std::unique_ptr<Iterator<const EntryChange>>(
          new EntryChangeIterator(changes.begin(), changes.end())),
      [&tmp_root_id](Status status, ObjectId obj_id,
                     std::unordered_set<ObjectId>&& new_nodes) {
        EXPECT_EQ(Status::OK, status);
        EXPECT_EQ(4u, new_nodes.size());
        EXPECT_TRUE(new_nodes.find(obj_id) != new_nodes.end());
        tmp_root_id = obj_id;
      });

  // Delete entries.
  std::vector<EntryChange> delete_changes;
  for (size_t i = 0; i < entries_to_delete.size(); ++i) {
    delete_changes.push_back(EntryChange{entries_to_delete[i], true});
  }

  // Expected layout (XX is key "keyXX"):
  //            [03, 07]
  //         /     |        \
  // [00, 01]  [05, 06]    [071, 08, 09, 10]
  ObjectId new_root_id;
  BTreeBuilder::ApplyChanges(
      &fake_storage_, tmp_root_id, 4,
      std::unique_ptr<Iterator<const EntryChange>>(new EntryChangeIterator(
          delete_changes.begin(), delete_changes.end())),
      [&new_root_id](Status status, ObjectId obj_id,
                     std::unordered_set<ObjectId>&& new_nodes) {
        EXPECT_EQ(Status::OK, status);
        // The root and the first 2 children have changed.
        EXPECT_EQ(3u, new_nodes.size());
        EXPECT_TRUE(new_nodes.find(obj_id) != new_nodes.end());
        new_root_id = obj_id;
      });

  CommitContentsImpl reader(new_root_id, &fake_storage_);
  std::unique_ptr<Iterator<const Entry>> entries = reader.begin();
  size_t deleted_index = 0;
  for (size_t i = 0; i < golden_entries.size(); ++i) {
    if (golden_entries[i] == entries_to_delete[deleted_index]) {
      // Skip deleted entries
      deleted_index++;
      continue;
    }
    EXPECT_TRUE(entries->Valid());
    EXPECT_EQ(golden_entries[i], **entries)
        << "Expected " << golden_entries[i].key << " but found "
        << (*entries)->key;
    entries->Next();
  }
  EXPECT_FALSE(entries->Valid());
}

}  // namespace
}  // namespace storage
