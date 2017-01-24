// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/storage/fake/fake_commit.h"

#include <memory>
#include <string>

#include "apps/ledger/src/storage/fake/fake_journal_delegate.h"
#include "apps/ledger/src/storage/public/commit.h"
#include "apps/ledger/src/storage/public/iterator.h"

namespace storage {
namespace fake {

FakeCommit::FakeCommit(FakeJournalDelegate* journal) : journal_(journal) {}
FakeCommit::~FakeCommit() {}

std::unique_ptr<Commit> FakeCommit::Clone() const {
  return std::make_unique<FakeCommit>(journal_);
}

const CommitId& FakeCommit::GetId() const {
  return journal_->GetId();
}

std::vector<CommitIdView> FakeCommit::GetParentIds() const {
  return std::vector<CommitIdView>();
}

int64_t FakeCommit::GetTimestamp() const {
  return 0;
}

uint64_t FakeCommit::GetGeneration() const {
  return 0;
}

ObjectIdView FakeCommit::GetRootId() const {
  return journal_->GetId();
}

ftl::StringView FakeCommit::GetStorageBytes() const {
  return ftl::StringView();
}

}  // namespace fake
}  // namespace storage
