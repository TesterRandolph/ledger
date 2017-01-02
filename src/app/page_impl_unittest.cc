// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/app/page_impl.h"

#include <memory>

#include "apps/ledger/src/app/constants.h"
#include "apps/ledger/src/app/merging/merge_resolver.h"
#include "apps/ledger/src/app/page_manager.h"
#include "apps/ledger/src/convert/convert.h"
#include "apps/ledger/src/storage/fake/fake_journal.h"
#include "apps/ledger/src/storage/fake/fake_journal_delegate.h"
#include "apps/ledger/src/storage/fake/fake_page_storage.h"
#include "gtest/gtest.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/socket/strings.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/vmo/strings.h"

namespace ledger {
namespace {

class PageImplTest : public ::testing::Test {
 public:
  PageImplTest() {}
  ~PageImplTest() override {}

 protected:
  // ApplicationTestBase:
  void SetUp() override {
    ::testing::Test::SetUp();
    page_id1_ = storage::PageId(kPageIdSize, 'a');
    auto fake_storage =
        std::make_unique<storage::fake::FakePageStorage>(page_id1_);
    fake_storage_ = fake_storage.get();
    auto resolver = std::make_unique<MergeResolver>([] {}, fake_storage_);

    manager_ = std::make_unique<PageManager>(std::move(fake_storage), nullptr,
                                             std::move(resolver));
    manager_->BindPage(page_ptr_.NewRequest());
  }

  storage::PageId page_id1_;
  storage::fake::FakePageStorage* fake_storage_;
  std::unique_ptr<PageManager> manager_;

  PagePtr page_ptr_;

  mtl::MessageLoop message_loop_;

  storage::ObjectId AddObjectToStorage(const std::string& value_string);

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(PageImplTest);
};

storage::ObjectId PageImplTest::AddObjectToStorage(
    const std::string& value_string) {
  storage::ObjectId object_id;
  // FakeStorage is synchronous.
  fake_storage_->AddObjectFromLocal(
      mtl::WriteStringToSocket(value_string), value_string.size(),
      [&object_id](storage::Status status,
                   storage::ObjectId returned_object_id) {
        EXPECT_EQ(storage::Status::OK, status);
        object_id = std::move(returned_object_id);
      });
  return object_id;
}

TEST_F(PageImplTest, GetId) {
  page_ptr_->GetId([this](fidl::Array<uint8_t> page_id) {
    EXPECT_EQ(page_id1_, convert::ToString(page_id));
    message_loop_.PostQuitTask();
  });
  message_loop_.Run();
}

TEST_F(PageImplTest, PutNoTransaction) {
  std::string key("some_key");
  std::string value("a small value");
  auto callback = [this, &key, &value](Status status) {
    EXPECT_EQ(Status::OK, status);
    auto objects = fake_storage_->GetObjects();
    EXPECT_EQ(1u, objects.size());
    storage::ObjectId object_id = objects.begin()->first;
    std::string actual_value = objects.begin()->second;
    EXPECT_EQ(value, actual_value);

    const std::map<std::string,
                   std::unique_ptr<storage::fake::FakeJournalDelegate>>&
        journals = fake_storage_->GetJournals();
    EXPECT_EQ(1u, journals.size());
    auto it = journals.begin();
    EXPECT_TRUE(it->second->IsCommitted());
    EXPECT_EQ(1u, it->second->GetData().size());
    storage::fake::FakeJournalDelegate::Entry entry =
        it->second->GetData().at(key);
    EXPECT_EQ(object_id, entry.value);
    EXPECT_FALSE(entry.deleted);
    EXPECT_EQ(storage::KeyPriority::EAGER, entry.priority);
    message_loop_.PostQuitTask();
  };
  page_ptr_->Put(convert::ToArray(key), convert::ToArray(value), callback);
  message_loop_.Run();
}

TEST_F(PageImplTest, PutReferenceNoTransaction) {
  std::string key("some_key");
  storage::ObjectId object_id("some_id");
  ReferencePtr reference = Reference::New();
  reference->opaque_id = convert::ToArray(object_id);

  auto callback = [this, &key, &object_id](Status status) {
    EXPECT_EQ(Status::OK, status);
    auto objects = fake_storage_->GetObjects();
    // No object should have been added.
    EXPECT_EQ(0u, objects.size());

    const std::map<std::string,
                   std::unique_ptr<storage::fake::FakeJournalDelegate>>&
        journals = fake_storage_->GetJournals();
    EXPECT_EQ(1u, journals.size());
    auto it = journals.begin();
    EXPECT_TRUE(it->second->IsCommitted());
    EXPECT_EQ(1u, it->second->GetData().size());
    storage::fake::FakeJournalDelegate::Entry entry =
        it->second->GetData().at(key);
    EXPECT_EQ(object_id, entry.value);
    EXPECT_FALSE(entry.deleted);
    EXPECT_EQ(storage::KeyPriority::LAZY, entry.priority);
    message_loop_.PostQuitTask();
  };
  page_ptr_->PutReference(convert::ToArray(key), std::move(reference),
                          Priority::LAZY, callback);
  message_loop_.Run();
}

TEST_F(PageImplTest, DeleteNoTransaction) {
  std::string key("some_key");

  page_ptr_->Delete(convert::ToArray(key), [this, &key](Status status) {
    EXPECT_EQ(Status::OK, status);
    auto objects = fake_storage_->GetObjects();
    // No object should have been added.
    EXPECT_EQ(0u, objects.size());

    const std::map<std::string,
                   std::unique_ptr<storage::fake::FakeJournalDelegate>>&
        journals = fake_storage_->GetJournals();
    EXPECT_EQ(1u, journals.size());
    auto it = journals.begin();
    EXPECT_TRUE(it->second->IsCommitted());
    EXPECT_EQ(1u, it->second->GetData().size());
    storage::fake::FakeJournalDelegate::Entry entry =
        it->second->GetData().at(key);
    EXPECT_TRUE(entry.deleted);
    message_loop_.PostQuitTask();
  });
  message_loop_.Run();
}

TEST_F(PageImplTest, TransactionCommit) {
  std::string key1("some_key1");
  storage::ObjectId object_id1;
  std::string value("a small value");
  std::string key2("some_key2");
  storage::ObjectId object_id2("some_id2");
  ReferencePtr reference = Reference::New();
  reference->opaque_id = convert::ToArray(object_id2);

  // Sequence of operations:
  //  - StartTransaction
  //  - Put
  //  - PutReference
  //  - Delete
  //  - Commit
  page_ptr_->StartTransaction([this](Status status) {
    EXPECT_EQ(Status::OK, status);
    message_loop_.PostQuitTask();
  });
  message_loop_.Run();

  auto put_callback = [this, &key1, &value, &object_id1](Status status) {
    EXPECT_EQ(Status::OK, status);
    auto objects = fake_storage_->GetObjects();
    EXPECT_EQ(1u, objects.size());
    object_id1 = objects.begin()->first;
    std::string actual_value = objects.begin()->second;
    EXPECT_EQ(value, actual_value);

    // No finished commit yet.
    const std::map<std::string,
                   std::unique_ptr<storage::fake::FakeJournalDelegate>>&
        journals = fake_storage_->GetJournals();
    EXPECT_EQ(1u, journals.size());
    auto it = journals.begin();
    EXPECT_FALSE(it->second->IsCommitted());
    EXPECT_EQ(1u, it->second->GetData().size());
    storage::fake::FakeJournalDelegate::Entry entry =
        it->second->GetData().at(key1);
    EXPECT_EQ(object_id1, entry.value);
    EXPECT_FALSE(entry.deleted);
    EXPECT_EQ(storage::KeyPriority::EAGER, entry.priority);
    message_loop_.PostQuitTask();

  };
  page_ptr_->Put(convert::ToArray(key1), convert::ToArray(value), put_callback);
  message_loop_.Run();

  auto put_reference_callback = [this, &key2, &object_id2](Status status) {
    EXPECT_EQ(Status::OK, status);
    EXPECT_EQ(1u, fake_storage_->GetObjects().size());

    // No finished commit yet, with now two entries.
    const std::map<std::string,
                   std::unique_ptr<storage::fake::FakeJournalDelegate>>&
        journals = fake_storage_->GetJournals();
    EXPECT_EQ(1u, journals.size());
    auto it = journals.begin();
    EXPECT_FALSE(it->second->IsCommitted());
    EXPECT_EQ(2u, it->second->GetData().size());
    storage::fake::FakeJournalDelegate::Entry entry =
        it->second->GetData().at(key2);
    EXPECT_EQ(object_id2, entry.value);
    EXPECT_FALSE(entry.deleted);
    EXPECT_EQ(storage::KeyPriority::LAZY, entry.priority);
    message_loop_.PostQuitTask();

  };
  page_ptr_->PutReference(convert::ToArray(key2), std::move(reference),
                          Priority::LAZY, put_reference_callback);
  message_loop_.Run();

  auto delete_callback = [this, &key2](Status status) {
    EXPECT_EQ(Status::OK, status);
    EXPECT_EQ(1u, fake_storage_->GetObjects().size());

    // No finished commit yet, with the second entry deleted.
    const std::map<std::string,
                   std::unique_ptr<storage::fake::FakeJournalDelegate>>&
        journals = fake_storage_->GetJournals();
    EXPECT_EQ(1u, journals.size());
    auto it = journals.begin();
    EXPECT_FALSE(it->second->IsCommitted());
    EXPECT_EQ(2u, it->second->GetData().size());
    storage::fake::FakeJournalDelegate::Entry entry =
        it->second->GetData().at(key2);
    EXPECT_TRUE(entry.deleted);
    message_loop_.PostQuitTask();
  };

  page_ptr_->Delete(convert::ToArray(key2), delete_callback);
  message_loop_.Run();

  page_ptr_->Commit([this](Status status) {
    EXPECT_EQ(Status::OK, status);
    EXPECT_EQ(1u, fake_storage_->GetObjects().size());

    const std::map<std::string,
                   std::unique_ptr<storage::fake::FakeJournalDelegate>>&
        journals = fake_storage_->GetJournals();
    EXPECT_EQ(1u, journals.size());
    auto it = journals.begin();
    EXPECT_TRUE(it->second->IsCommitted());
    EXPECT_EQ(2u, it->second->GetData().size());
    message_loop_.PostQuitTask();
  });
  message_loop_.Run();
}

TEST_F(PageImplTest, TransactionRollback) {
  // Sequence of operations:
  //  - StartTransaction
  //  - Rollback
  page_ptr_->StartTransaction(
      [](Status status) { EXPECT_EQ(Status::OK, status); });
  page_ptr_->Rollback([this](Status status) {
    EXPECT_EQ(Status::OK, status);
    EXPECT_EQ(0u, fake_storage_->GetObjects().size());

    // Only one journal, rollbacked.
    const std::map<std::string,
                   std::unique_ptr<storage::fake::FakeJournalDelegate>>&
        journals = fake_storage_->GetJournals();
    EXPECT_EQ(1u, journals.size());
    auto it = journals.begin();
    EXPECT_TRUE(it->second->IsRolledBack());
    EXPECT_EQ(0u, it->second->GetData().size());
    message_loop_.PostQuitTask();
  });
  message_loop_.Run();
}

TEST_F(PageImplTest, NoTwoTransactions) {
  // Sequence of operations:
  //  - StartTransaction
  //  - StartTransaction
  page_ptr_->StartTransaction(
      [](Status status) { EXPECT_EQ(Status::OK, status); });
  page_ptr_->StartTransaction([this](Status status) {
    EXPECT_EQ(Status::TRANSACTION_ALREADY_IN_PROGRESS, status);
    message_loop_.PostQuitTask();
  });
  message_loop_.Run();
}

TEST_F(PageImplTest, NoTransactionCommit) {
  // Sequence of operations:
  //  - Commit
  page_ptr_->Commit([this](Status status) {
    EXPECT_EQ(Status::NO_TRANSACTION_IN_PROGRESS, status);
    message_loop_.PostQuitTask();
  });
  message_loop_.Run();
}

TEST_F(PageImplTest, NoTransactionRollback) {
  // Sequence of operations:
  //  - Rollback
  page_ptr_->Rollback([this](Status status) {
    EXPECT_EQ(Status::NO_TRANSACTION_IN_PROGRESS, status);
    message_loop_.PostQuitTask();
  });
  message_loop_.Run();
}

TEST_F(PageImplTest, CreateReference) {
  std::string value("a small value");
  Status status;
  ReferencePtr reference;
  page_ptr_->CreateReference(
      value.size(), mtl::WriteStringToSocket(value),
      [this, &status, &reference](Status received_status,
                                  ReferencePtr received_reference) {
        status = received_status;
        reference = std::move(received_reference);
        message_loop_.PostQuitTask();
      });
  message_loop_.Run();
  EXPECT_EQ(Status::OK, status);
  auto objects = fake_storage_->GetObjects();
  auto it = objects.find(reference->opaque_id);
  ASSERT_NE(objects.end(), it);
  ASSERT_EQ(value, it->second);
}

TEST_F(PageImplTest, PutGetSnapshotGetEntries) {
  std::string key("some_key");
  std::string value("a small value");
  PageSnapshotPtr snapshot;

  auto callback_put = [this](Status status) {
    EXPECT_EQ(Status::OK, status);
    message_loop_.PostQuitTask();
  };
  page_ptr_->Put(convert::ToArray(key), convert::ToArray(value), callback_put);
  message_loop_.Run();

  auto callback_getsnapshot = [this](Status status) {
    EXPECT_EQ(Status::OK, status);
    message_loop_.PostQuitTask();
  };
  page_ptr_->GetSnapshot(snapshot.NewRequest(), callback_getsnapshot);
  message_loop_.Run();

  fidl::Array<EntryPtr> actual_entries;
  auto callback_getentries = [this, &actual_entries](
      Status status, fidl::Array<EntryPtr> entries,
      fidl::Array<uint8_t> next_token) {
    EXPECT_EQ(Status::OK, status);
    EXPECT_TRUE(next_token.is_null());
    actual_entries = std::move(entries);
    message_loop_.PostQuitTask();
  };
  snapshot->GetEntries(nullptr, nullptr, callback_getentries);
  message_loop_.Run();

  EXPECT_EQ(1u, actual_entries.size());
  EXPECT_EQ(key, convert::ExtendedStringView(actual_entries[0]->key));
  EXPECT_EQ(value,
            convert::ExtendedStringView(actual_entries[0]->value->get_bytes()));
  EXPECT_EQ(Priority::EAGER, actual_entries[0]->priority);
}

TEST_F(PageImplTest, PutGetSnapshotGetKeys) {
  std::string key1("some_key");
  std::string value1("a small value");
  std::string key2("some_key2");
  std::string value2("another value");
  PageSnapshotPtr snapshot;

  auto callback_statusok = [this](Status status) {
    EXPECT_EQ(Status::OK, status);
    message_loop_.PostQuitTask();
  };
  page_ptr_->StartTransaction(callback_statusok);
  message_loop_.Run();
  page_ptr_->Put(convert::ToArray(key1), convert::ToArray(value1),
                 callback_statusok);
  message_loop_.Run();
  page_ptr_->Put(convert::ToArray(key2), convert::ToArray(value2),
                 callback_statusok);
  message_loop_.Run();
  page_ptr_->Commit(callback_statusok);
  message_loop_.Run();

  auto callback_getsnapshot = [this](Status status) {
    EXPECT_EQ(Status::OK, status);
    message_loop_.PostQuitTask();
  };
  page_ptr_->GetSnapshot(snapshot.NewRequest(), callback_getsnapshot);
  message_loop_.Run();

  fidl::Array<fidl::Array<uint8_t>> actual_keys;
  auto callback_getkeys = [this, &actual_keys](
      Status status, fidl::Array<fidl::Array<uint8_t>> keys,
      fidl::Array<uint8_t> next_token) {
    EXPECT_EQ(Status::OK, status);
    EXPECT_TRUE(next_token.is_null());
    actual_keys = std::move(keys);
    message_loop_.PostQuitTask();
  };
  snapshot->GetKeys(nullptr, nullptr, callback_getkeys);
  message_loop_.Run();

  EXPECT_EQ(2u, actual_keys.size());
  EXPECT_EQ(key1, convert::ExtendedStringView(actual_keys[0]));
  EXPECT_EQ(key2, convert::ExtendedStringView(actual_keys[1]));
}

TEST_F(PageImplTest, SnapshotGetReferenceSmall) {
  std::string key("some_key");
  std::string value("a small value");
  PageSnapshotPtr snapshot;

  auto callback_put = [this](Status status) {
    EXPECT_EQ(Status::OK, status);
    message_loop_.PostQuitTask();
  };
  page_ptr_->Put(convert::ToArray(key), convert::ToArray(value), callback_put);
  message_loop_.Run();

  auto callback_getsnapshot = [this](Status status) {
    EXPECT_EQ(Status::OK, status);
    message_loop_.PostQuitTask();
  };
  page_ptr_->GetSnapshot(snapshot.NewRequest(), callback_getsnapshot);
  message_loop_.Run();

  ValuePtr actual_value;
  auto callback_get = [this, &actual_value](Status status, ValuePtr value) {
    EXPECT_EQ(Status::OK, status);
    actual_value = std::move(value);
    message_loop_.PostQuitTask();
  };
  snapshot->Get(convert::ToArray(key), callback_get);
  message_loop_.Run();

  EXPECT_TRUE(actual_value->is_bytes());
  EXPECT_EQ(value, convert::ExtendedStringView(actual_value->get_bytes()));
}

TEST_F(PageImplTest, SnapshotGetReferenceLarge) {
  std::string value_string(kMaxInlineDataSize + 1, 'a');
  storage::ObjectId object_id = AddObjectToStorage(value_string);

  std::string key("some_key");
  PageSnapshotPtr snapshot;
  ReferencePtr reference = Reference::New();
  reference->opaque_id = convert::ToArray(object_id);

  auto callback_put = [this](Status status) {
    EXPECT_EQ(Status::OK, status);
    message_loop_.PostQuitTask();
  };
  page_ptr_->PutReference(convert::ToArray(key), std::move(reference),
                          Priority::EAGER, callback_put);
  message_loop_.Run();

  auto callback_getsnapshot = [this](Status status) {
    EXPECT_EQ(Status::OK, status);
    message_loop_.PostQuitTask();
  };
  page_ptr_->GetSnapshot(snapshot.NewRequest(), callback_getsnapshot);
  message_loop_.Run();

  ValuePtr actual_value;
  auto callback_get = [this, &actual_value](Status status, ValuePtr value) {
    EXPECT_EQ(Status::OK, status);
    actual_value = std::move(value);
    message_loop_.PostQuitTask();
  };
  snapshot->Get(convert::ExtendedStringView(key).ToArray(), callback_get);
  message_loop_.Run();

  EXPECT_FALSE(actual_value->is_bytes());
  EXPECT_TRUE(actual_value->is_buffer());
  std::string content;
  EXPECT_TRUE(mtl::StringFromVmo(actual_value->get_buffer(), &content));
  EXPECT_EQ(value_string, content);
}

TEST_F(PageImplTest, SnapshotGetPartial) {
  std::string key("some_key");
  std::string value("a small value");
  PageSnapshotPtr snapshot;

  auto callback_put = [this](Status status) {
    EXPECT_EQ(Status::OK, status);
    message_loop_.PostQuitTask();
  };
  page_ptr_->Put(convert::ToArray(key), convert::ToArray(value), callback_put);
  message_loop_.Run();

  auto callback_getsnapshot = [this](Status status) {
    EXPECT_EQ(Status::OK, status);
    message_loop_.PostQuitTask();
  };
  page_ptr_->GetSnapshot(snapshot.NewRequest(), callback_getsnapshot);
  message_loop_.Run();

  Status status;
  mx::vmo buffer;
  snapshot->GetPartial(convert::ToArray(key), 2, 5,
                       [this, &status, &buffer](Status received_status,
                                                mx::vmo received_buffer) {
                         status = received_status;
                         buffer = std::move(received_buffer);
                         message_loop_.PostQuitTask();
                       });
  message_loop_.Run();
  EXPECT_EQ(Status::OK, status);
  std::string content;
  EXPECT_TRUE(mtl::StringFromVmo(buffer, &content));
  EXPECT_EQ("small", content);
}

TEST_F(PageImplTest, ParallelPut) {
  PagePtr page_ptr2;
  manager_->BindPage(page_ptr2.NewRequest());

  std::string key("some_key");
  std::string value1("a small value");
  std::string value2("another value");

  PageSnapshotPtr snapshot1;
  PageSnapshotPtr snapshot2;

  auto callback_simple = [this](Status status) {
    EXPECT_EQ(Status::OK, status);
    message_loop_.PostQuitTask();
  };
  page_ptr_->StartTransaction(callback_simple);
  message_loop_.Run();

  page_ptr_->Put(convert::ToArray(key), convert::ToArray(value1),
                 callback_simple);
  message_loop_.Run();

  page_ptr2->StartTransaction(callback_simple);
  message_loop_.Run();

  page_ptr2->Put(convert::ToArray(key), convert::ToArray(value2),
                 callback_simple);
  message_loop_.Run();

  page_ptr_->Commit(callback_simple);
  message_loop_.Run();
  page_ptr2->Commit(callback_simple);
  message_loop_.Run();

  auto callback_getsnapshot = [this](Status status) {
    EXPECT_EQ(Status::OK, status);
    message_loop_.PostQuitTask();
  };
  page_ptr_->GetSnapshot(snapshot1.NewRequest(), callback_getsnapshot);
  message_loop_.Run();
  page_ptr2->GetSnapshot(snapshot2.NewRequest(), callback_getsnapshot);
  message_loop_.Run();

  std::string actual_value1;
  auto callback_getvalue1 = [this, &actual_value1](Status status,
                                                   ValuePtr returned_value) {
    EXPECT_EQ(Status::OK, status);
    actual_value1 = convert::ToString(returned_value->get_bytes());
    message_loop_.PostQuitTask();
  };
  snapshot1->Get(convert::ToArray(key), callback_getvalue1);
  message_loop_.Run();

  std::string actual_value2;
  auto callback_getvalue2 = [this, &actual_value2](Status status,
                                                   ValuePtr returned_value) {
    EXPECT_EQ(Status::OK, status);
    actual_value2 = convert::ToString(returned_value->get_bytes());
    message_loop_.PostQuitTask();
  };
  snapshot2->Get(convert::ToArray(key), callback_getvalue2);
  message_loop_.Run();

  // The two snapshots should have different contents.
  EXPECT_EQ(value1, actual_value1);
  EXPECT_EQ(value2, actual_value2);
}

}  // namespace
}  // namespace ledger
