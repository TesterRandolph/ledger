// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_APP_MERGING_LAST_ONE_WINS_MERGER_H_
#define APPS_LEDGER_SRC_APP_MERGING_LAST_ONE_WINS_MERGER_H_

#include <memory>
#include "apps/ledger/src/app/merging/merge_strategy.h"
#include "apps/ledger/src/callback/auto_cleanable.h"
#include "apps/ledger/src/storage/public/commit.h"
#include "apps/ledger/src/storage/public/page_storage.h"
#include "lib/ftl/macros.h"

namespace ledger {
// Strategy for merging commits using a last-one-wins policy for conflicts.
// Commits are merged key-by-key. When a key has been modified on both sides,
// the value from the most recent commit is used.
class LastOneWinsMerger : public MergeStrategy {
 public:
  LastOneWinsMerger();
  ~LastOneWinsMerger() override;

  ftl::RefPtr<callback::Cancellable> Merge(
      storage::PageStorage* storage,
      std::unique_ptr<const storage::Commit> head_1,
      std::unique_ptr<const storage::Commit> head_2,
      std::unique_ptr<const storage::Commit> ancestor) override;

 private:
  storage::PageStorage* storage_;

  FTL_DISALLOW_COPY_AND_ASSIGN(LastOneWinsMerger);
};

}  // namespace ledger

#endif  // APPS_LEDGER_SRC_APP_MERGING_LAST_ONE_WINS_MERGER_H_
