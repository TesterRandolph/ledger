// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/app/merging/merge_resolver.h"

#include <algorithm>
#include <memory>
#include <queue>
#include <set>

#include "apps/ledger/src/app/merging/ledger_merge_manager.h"
#include "apps/ledger/src/app/merging/merge_strategy.h"
#include "apps/ledger/src/app/page_manager.h"
#include "lib/ftl/memory/weak_ptr.h"
#include "lib/mtl/tasks/message_loop.h"

namespace ledger {
namespace {
// Comparator for commits that order commits based on their generation, then on
// their id.
struct GenerationComparator {
  bool operator()(const std::unique_ptr<const storage::Commit>& lhs,
                  const std::unique_ptr<const storage::Commit>& rhs) const {
    uint64_t lhs_generation = lhs->GetGeneration();
    uint64_t rhs_generation = rhs->GetGeneration();
    return lhs_generation == rhs_generation ? lhs->GetId() < rhs->GetId()
                                            : lhs_generation < rhs_generation;
  }
};

}  // namespace

MergeResolver::MergeResolver(ftl::Closure on_destroyed,
                             storage::PageStorage* storage)
    : storage_(storage), on_destroyed_(on_destroyed), weak_ptr_factory_(this) {
  storage_->AddCommitWatcher(this);
  PostCheckConflicts();
}

MergeResolver::~MergeResolver() {
  storage_->RemoveCommitWatcher(this);
  on_destroyed_();
}

void MergeResolver::set_on_empty(ftl::Closure on_empty_callback) {
  on_empty_callback_ = std::move(on_empty_callback);
}

bool MergeResolver::IsEmpty() {
  return !merge_in_progress_;
}

void MergeResolver::SetMergeStrategy(std::unique_ptr<MergeStrategy> strategy) {
  if (merge_in_progress_) {
    FTL_DCHECK(strategy_);
    next_strategy_ = std::move(strategy);
    switch_strategy_ = true;
    strategy_->Cancel();
    return;
  }
  strategy_.swap(strategy);
  if (strategy_) {
    PostCheckConflicts();
  }
}

void MergeResolver::SetPageManager(PageManager* page_manager) {
  FTL_DCHECK(page_manager_ == nullptr);
  page_manager_ = page_manager;
}

void MergeResolver::OnNewCommits(
    const std::vector<std::unique_ptr<const storage::Commit>>& commits,
    storage::ChangeSource source) {
  PostCheckConflicts();
}

void MergeResolver::PostCheckConflicts() {
  mtl::MessageLoop::GetCurrent()
      ->task_runner()
      ->PostTask([weak_this_ptr = weak_ptr_factory_.GetWeakPtr()]() {
        if (weak_this_ptr) {
          weak_this_ptr->CheckConflicts();
        }
      });
}
void MergeResolver::CheckConflicts() {
  if (!strategy_ || merge_in_progress_) {
    // No strategy, or a merge already in progress. Let's bail out early.
    return;
  }

  std::vector<storage::CommitId> heads;
  storage::Status s = storage_->GetHeadCommitIds(&heads);
  FTL_DCHECK(s == storage::Status::OK);
  if (heads.size() == 1) {
    // No conflict.
    return;
  }
  ResolveConflicts(std::move(heads));
}

void MergeResolver::ResolveConflicts(std::vector<storage::CommitId> heads) {
  FTL_DCHECK(heads.size() >= 2);
  std::vector<std::unique_ptr<const storage::Commit>> commits;
  for (const storage::CommitId& id : heads) {
    std::unique_ptr<const storage::Commit> commit;
    storage::Status s = storage_->GetCommitSynchronous(id, &commit);
    FTL_DCHECK(s == storage::Status::OK);
    commits.push_back(std::move(commit));
  }
  std::sort(commits.begin(), commits.end(),
            [](const std::unique_ptr<const storage::Commit>& lhs,
               const std::unique_ptr<const storage::Commit>& rhs) {
              return lhs->GetTimestamp() < rhs->GetTimestamp();
            });

  // Use the most recent commit as the base.
  auto head_1 = std::move(commits[0]);
  auto head_2 = std::move(commits[1]);
  std::unique_ptr<const storage::Commit> common_ancestor(
      FindCommonAncestor(head_1, head_2));
  merge_in_progress_ = true;
  strategy_->Merge(storage_, page_manager_, std::move(head_1),
                   std::move(head_2), std::move(common_ancestor), [this]() {
                     merge_in_progress_ = false;
                     if (switch_strategy_) {
                       strategy_ = std::move(next_strategy_);
                       next_strategy_.reset();
                       switch_strategy_ = false;
                     }
                     PostCheckConflicts();
                     // Call on_empty_callback_ at the very end as this might
                     // delete this.
                     if (on_empty_callback_) {
                       on_empty_callback_();
                     }
                   });
}

std::unique_ptr<const storage::Commit> MergeResolver::FindCommonAncestor(
    const std::unique_ptr<const storage::Commit>& head1,
    const std::unique_ptr<const storage::Commit>& head2) {
  std::set<std::unique_ptr<const storage::Commit>, GenerationComparator>
      commits;
  commits.emplace(head1->Clone());
  commits.emplace(head2->Clone());
  // The algorithm goes as follows: we keep a set of "active" commits, ordered
  // by generation order. Until this set has only one element, we take the
  // commit with the greater generation (the one deepest in the commit graph)
  // and replace it by its parent. If we seed the initial set with two commits,
  // we get their unique closest common ancestor.
  while (commits.size() != 1) {
    const std::unique_ptr<const storage::Commit> commit = std::move(
        const_cast<std::unique_ptr<const storage::Commit>&>(*commits.rbegin()));
    auto it = commits.end();
    --it;
    commits.erase(it);
    std::vector<storage::CommitId> parent_ids = commit->GetParentIds();
    for (const storage::CommitId& parent_id : parent_ids) {
      std::unique_ptr<const storage::Commit> parent_commit;
      storage::Status s =
          storage_->GetCommitSynchronous(parent_id, &parent_commit);
      FTL_DCHECK(s == storage::Status::OK);
      commits.emplace(std::move(parent_commit));
    }
  }
  return std::move(
      const_cast<std::unique_ptr<const storage::Commit>&>(*commits.rbegin()));
}

}  // namespace ledger
