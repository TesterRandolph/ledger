// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_STORAGE_TEST_STORAGE_TEST_UTILS_H_
#define APPS_LEDGER_SRC_STORAGE_TEST_STORAGE_TEST_UTILS_H_

#include <string>

#include "apps/ledger/src/storage/impl/btree/tree_node.h"
#include "apps/ledger/src/storage/public/page_storage.h"
#include "apps/ledger/src/storage/public/types.h"
#include "apps/ledger/src/test/test_with_message_loop.h"
#include "lib/ftl/functional/closure.h"
#include "lib/mtl/socket/strings.h"

namespace storage {

// Creates a random id given its size.
std::string RandomId(size_t size);

// Creates the object id for testing from the given str, by resizing it as
// necessary.
ObjectId MakeObjectId(std::string str);

// Creates and returns a new EntryChange adding or updating the entry with the
// given information.
EntryChange NewEntryChange(std::string key,
                           std::string object_id,
                           KeyPriority priority);

// Creates and returns a new EntryChange removing the entry with the given key.
EntryChange NewRemoveEntryChange(std::string key);

// A TestWithMessageLoop providing some additional utility functions on
// PageStorage.
//
// All utility functions in this class return an |AssertionResult| meaning that
// they can be used in an EXPECT/ASSERT_TRUE: E.g.
//     ASSERT_TRUE(AddObject("value", &object));
// or an EXPECT/ASSERT_FALSE if the function is expected to fail.
//     ASSERT_FALSE(AddObject("value", &object));
class StorageTest : public ::test::TestWithMessageLoop {
 protected:
  StorageTest();

  ~StorageTest();

  virtual PageStorage* GetStorage() = 0;

  // Adds a new object with the given value in the page storage and updates
  // |object| with the new value.
  ::testing::AssertionResult AddObject(const std::string& value,
                                       std::unique_ptr<const Object>* object);

  // Creates a vector of entries, each of which has a key from "key00" to
  // "keyXX" where XX is |size-1|. A new value is created for each entry and the
  // corresponding object_id is set on the entry. |entries| vector will be
  // swapped with the result.
  ::testing::AssertionResult CreateEntries(size_t size,
                                           std::vector<Entry>* entries);

  // Creates a vector of entries, each of which has a key "keyXX", were "XX" is
  // taken from the |values| vector. A new value is created for each entry and
  // the corresponding object_id is set on the entry. |entries| vector will be
  // swapped with the result.
  ::testing::AssertionResult CreateEntries(std::vector<size_t> values,
                                           std::vector<Entry>* entries);

  // Creates a vector of entry changes adding or updating the given number of
  // entries. See |CreateEntries| for information on the created entries.
  // |changes| vector will be swapped with the result.
  ::testing::AssertionResult CreateEntryChanges(
      size_t size,
      std::vector<EntryChange>* changes);

  // Creates a vector of entry changes adding or updating the given number of
  // entries. See |CreateEntries| for information on the created entries.
  // |changes| vector will be swapped with the result. If |deletion| is true,
  // the changes will be deletions, otherwise the changes will be updates.
  ::testing::AssertionResult CreateEntryChanges(
      std::vector<size_t> values,
      std::vector<EntryChange>* changes,
      bool deletion = false);

  // Creates an empty tree node and updates |empty_node_id| with the result.
  ::testing::AssertionResult GetEmptyNodeId(ObjectId* empty_node_id);

  // Returns the tree node corresponding to the given id.
  ::testing::AssertionResult CreateNodeFromId(
      ObjectIdView id,
      std::unique_ptr<const TreeNode>* node);

  // Creates a new tree node from the given entries and children and updates
  // |node| with the result.
  ::testing::AssertionResult CreateNodeFromEntries(
      const std::vector<Entry>& entries,
      const std::vector<ObjectId>& children,
      std::unique_ptr<const TreeNode>* node);

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(StorageTest);
};

}  // namespace storage

#endif  // APPS_LEDGER_SRC_STORAGE_TEST_STORAGE_TEST_UTILS_H_
