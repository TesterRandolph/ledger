// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/app/branch_tracker.h"

#include <vector>

#include "apps/ledger/src/app/page_manager.h"
#include "apps/ledger/src/callback/waiter.h"
#include "lib/ftl/functional/make_copyable.h"

namespace ledger {
class BranchTracker::PageWatcherContainer {
 public:
  PageWatcherContainer(PageWatcherPtr watcher,
                       PageManager* page_manager,
                       storage::PageStorage* storage,
                       std::unique_ptr<const storage::Commit> base_commit,
                       PageSnapshotPtr snapshot)
      : change_in_flight_(true),
        last_commit_(std::move(base_commit)),
        manager_(page_manager),
        storage_(storage),
        interface_(std::move(watcher)) {
    interface_->OnInitialState(std::move(snapshot), [this]() {
      change_in_flight_ = false;
      SendCommit();
    });
  }

  void set_on_empty(ftl::Closure on_empty_callback) {
    interface_.set_connection_error_handler(std::move(on_empty_callback));
  }

  void UpdateCommit(const storage::CommitId& commit_id) {
    storage::Status status = storage_->GetCommit(commit_id, &current_commit_);
    FTL_DCHECK(status == storage::Status::OK);
    SendCommit();
  }

 private:
  // Sends a commit to the watcher if needed.
  void SendCommit() {
    if (change_in_flight_) {
      return;
    }
    if (!current_commit_ || last_commit_->GetId() == current_commit_->GetId()) {
      return;
    }
    change_in_flight_ = true;
    // TODO(etiennej): See LE-74: clean object ownership
    std::unique_ptr<storage::CommitContents> last_contents =
        last_commit_->GetContents();
    std::unique_ptr<storage::CommitContents> current_contents =
        current_commit_->GetContents();
    std::function<void(
        storage::Status,
        std::unique_ptr<storage::Iterator<const storage::EntryChange>>)>
        diff_callback = ftl::MakeCopyable([
          this, new_commit = std::move(current_commit_)
        ](storage::Status status,
          std::unique_ptr<storage::Iterator<const storage::EntryChange>>
              it) mutable {
          if (status != storage::Status::OK) {
            // This change notification is abandonned. At the next commit, we
            // will try again (but not before). The next notification will cover
            // both this change and the next.
            FTL_LOG(ERROR) << "Unable to compute diff.";
            change_in_flight_ = false;
            return;
          }

          if (!it->Valid()) {
            change_in_flight_ = false;
            last_commit_.swap(new_commit);
            SendCommit();
            return;
          }

          auto waiter =
              callback::Waiter<storage::Status,
                               std::unique_ptr<const storage::Object>>::
                  Create(storage::Status::OK);

          PageChangePtr page_change = PageChange::New();
          page_change->timestamp = new_commit->GetTimestamp();
          page_change->changes = fidl::Array<EntryPtr>::New(0);
          page_change->deleted_keys = fidl::Array<fidl::Array<uint8_t>>::New(0);

          for (; it->Valid(); it->Next()) {
            if ((*it)->deleted) {
              page_change->deleted_keys.push_back(
                  convert::ToArray((*it)->entry.key));
              continue;
            }

            EntryPtr entry = Entry::New();
            entry->key = convert::ToArray((*it)->entry.key);
            entry->priority =
                (*it)->entry.priority == storage::KeyPriority::EAGER
                    ? Priority::EAGER
                    : Priority::LAZY;
            page_change->changes.push_back(std::move(entry));

            storage_->GetObject((*it)->entry.object_id, waiter->NewCallback());
          }

          std::function<void(
              storage::Status,
              std::vector<std::unique_ptr<const storage::Object>>)>
              result_callback = ftl::MakeCopyable([
                this, new_commit = std::move(new_commit),
                page_change = std::move(page_change)
              ](storage::Status status,
                std::vector<std::unique_ptr<const storage::Object>>
                    results) mutable {
                if (status != storage::Status::OK) {
                  FTL_LOG(ERROR)
                      << "Watcher: error while reading changed values.";
                  return;
                }
                FTL_DCHECK(results.size() == page_change->changes.size());
                for (size_t i = 0; i < results.size(); i++) {
                  ftl::StringView object_contents;

                  storage::Status read_status =
                      results[i]->GetData(&object_contents);
                  if (read_status != storage::Status::OK) {
                    FTL_LOG(ERROR)
                        << "Watcher: error while reading changed value.";
                    return;
                  }

                  // TODO(etiennej): LE-75 implement pagination on OnChange.
                  // TODO(etiennej): LE-120 Use VMOs for big values.
                  page_change->changes[i]->value = Value::New();
                  page_change->changes[i]->value->set_bytes(
                      convert::ToArray(object_contents));
                }

                interface_->OnChange(
                    std::move(page_change), ftl::MakeCopyable([
                      this, new_commit = std::move(new_commit)
                    ](fidl::InterfaceRequest<PageSnapshot>
                                                snapshot_request) mutable {
                      if (snapshot_request) {
                        manager_->BindPageSnapshot(new_commit->Clone(),
                                                   std::move(snapshot_request));
                      }
                      change_in_flight_ = false;
                      last_commit_.swap(new_commit);
                      SendCommit();
                    }));
              });
          waiter->Finalize(result_callback);
        });
    last_contents->diff(std::move(current_contents), diff_callback);
  }

  bool change_in_flight_;
  std::unique_ptr<const storage::Commit> last_commit_;
  std::unique_ptr<const storage::Commit> current_commit_;
  PageManager* manager_;
  storage::PageStorage* storage_;
  PageWatcherPtr interface_;
};

BranchTracker::BranchTracker(PageManager* manager,
                             storage::PageStorage* storage,
                             fidl::InterfaceRequest<Page> request)
    : manager_(manager),
      storage_(storage),
      interface_(std::move(request), storage, manager, this),
      transaction_in_progress_(false) {
  interface_.set_on_empty([this] {
    this->SetTransactionInProgress(false);
    CheckEmpty();
  });
  watchers_.set_on_empty([this] { CheckEmpty(); });
  std::vector<storage::CommitId> commit_ids;
  // TODO(etiennej): Fail more nicely.
  FTL_CHECK(storage_->GetHeadCommitIds(&commit_ids) == storage::Status::OK);
  FTL_DCHECK(commit_ids.size() > 0);
  current_commit_ = commit_ids[0];
  storage_->AddCommitWatcher(this);
}

BranchTracker::~BranchTracker() {
  storage_->RemoveCommitWatcher(this);
}

void BranchTracker::set_on_empty(ftl::Closure on_empty_callback) {
  on_empty_callback_ = on_empty_callback;
}

const storage::CommitId& BranchTracker::GetBranchHeadId() {
  return current_commit_;
}

void BranchTracker::SetBranchHead(const storage::CommitId& commit_id) {
  current_commit_ = commit_id;
  for (auto& watcher : watchers_) {
    watcher.UpdateCommit(current_commit_);
  }
}

void BranchTracker::OnNewCommits(
    const std::vector<std::unique_ptr<const storage::Commit>>& commits,
    storage::ChangeSource source) {
  bool changed = false;
  for (const auto& commit : commits) {
    if (commit->GetId() == current_commit_) {
      continue;
    }
    // This assumes commits are received in (partial) order. If the commit
    // doesn't have current_commit_ as a parent it is not part of this branch
    // and should be ignored.
    std::vector<storage::CommitId> parent_ids = commit->GetParentIds();
    if (std::find(parent_ids.begin(), parent_ids.end(), current_commit_) ==
        parent_ids.end()) {
      continue;
    }
    changed = true;
    current_commit_ = commit->GetId();
  }

  if (!changed || transaction_in_progress_) {
    return;
  }
  for (auto& watcher : watchers_) {
    watcher.UpdateCommit(current_commit_);
  }
}

void BranchTracker::SetTransactionInProgress(bool transaction_in_progress) {
  if (transaction_in_progress_ == transaction_in_progress) {
    return;
  }
  transaction_in_progress_ = transaction_in_progress;
  if (!transaction_in_progress) {
    for (auto it = watchers_.begin(); it != watchers_.end(); ++it) {
      it->UpdateCommit(current_commit_);
    }
  }
}

void BranchTracker::RegisterPageWatcher(PageWatcherPtr page_watcher_ptr,
                                        PageSnapshotPtr snapshot_ptr) {
  std::unique_ptr<const storage::Commit> base_commit;
  storage::Status status = storage_->GetCommit(current_commit_, &base_commit);
  FTL_DCHECK(status == storage::Status::OK);
  watchers_.emplace(std::move(page_watcher_ptr), manager_, storage_,
                    std::move(base_commit), std::move(snapshot_ptr));
}

void BranchTracker::CheckEmpty() {
  if (on_empty_callback_ && !interface_.is_bound() && watchers_.empty())
    on_empty_callback_();
}

}  // namespace ledger
