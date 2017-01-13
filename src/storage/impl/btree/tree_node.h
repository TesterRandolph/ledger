// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_STORAGE_IMPL_BTREE_TREE_NODE_H_
#define APPS_LEDGER_SRC_STORAGE_IMPL_BTREE_TREE_NODE_H_

#include <memory>
#include <unordered_set>
#include <vector>

#include "apps/ledger/src/convert/convert.h"
#include "apps/ledger/src/storage/public/object.h"
#include "apps/ledger/src/storage/public/page_storage.h"
#include "apps/ledger/src/storage/public/types.h"

namespace storage {

// A node of the B-Tree holding the commit contents.
class TreeNode {
 public:
  ~TreeNode();

  // A TreeNode builder, based on an initial node and allowing to apply a set of
  // changes to it. Mutation calls should be sorted in a strictly increasing
  // order based on the corresponding key of the change with one exception: to
  // update the child id before a key K, and update the value of the same key,
  // two calls using the same key should be made. In this case, the
  // UpdateChildId call should precede the UpdateEntry one:
  //
  // node.StartMutation()
  //     .UpdateChildId(K, child_id)
  //     .UpdateEntry(Entry{K, V', priority})
  //     .Finish();
  class Mutation {
   public:
    using Updater = std::function<void(Mutation*)>;

    explicit Mutation(const TreeNode& node);
    Mutation(Mutation&&);
    ~Mutation();

    // Adds a new entry with the given ids as its left and right children. The
    // corresponding child nodes are expected to be the result of spliting the
    // the previous child node in that entry's position.
    Mutation& AddEntry(const Entry& entry,
                       ObjectIdView left_id,
                       ObjectIdView right_id);

    // Updates the value and/or priority of an existing key.
    Mutation& UpdateEntry(const Entry& entry);

    // Removes the entry with the given |key| from this node and updates the id
    // of the child in that position. The new |child_id| is expected to be the
    // result of the merge of the left and right children of the deleted entry.
    Mutation& RemoveEntry(const std::string& key, ObjectIdView child_id);

    // Updates the id of a child on the left of the entry with the given key.
    Mutation& UpdateChildId(const std::string& key_after,
                            ObjectIdView child_id);

    // Creates the new TreeNode as a result of the given updates. |on_done| will
    // be called with the status and, if successfull, the new node's id. After
    // calling this method, this Mutation object is no longer valid and calling
    // any methods on it will fail.
    void Finish(std::function<void(Status, ObjectId)> on_done);

    // Creates as many tree nodes as necessary given the |max_size| of entries a
    // node can have. If this mutation corresponds to a root node, |on_done|
    // will be called with the new root id and a null Updater. Otherwise,
    // |on_done| will be called with an empty object id and if there some
    // changes to be done on the parent node, the Updater will have a non-null
    // value. After calling this method, this Mutation object is no longer valid
    // and calling any methods on it will fail.
    // TODO(nellyv): This method should not be necessary after updating the
    // B-Tree node implementation.
    void Finish(size_t max_size,
                bool is_root,
                const std::string& max_key,
                std::unordered_set<ObjectId>* new_nodes,
                std::function<void(Status, ObjectId, std::unique_ptr<Updater>)>
                    on_done);

   private:
    // Copies the entries from the |node_| starting at |node_index_| and until
    // that entry's key is equal to or greater than the given |key|. If |key| is
    // empty, all entries until the end of the vector are copied.
    void CopyUntil(std::string key);

    void FinalizeEntriesChildren();

    const TreeNode& node_;
    // The index of the next entry of the node to be added in the entries of
    // this mutation.
    int node_index_ = 0;

    std::vector<Entry> entries_;
    std::vector<ObjectId> children_;
    bool finished = false;

    FTL_DISALLOW_COPY_AND_ASSIGN(Mutation);
  };

  // Creates a |TreeNode| object for an existing node and calls the given
  // |callback| with the returned status and node.
  static void FromId(
      PageStorage* page_storage,
      ObjectIdView id,
      std::function<void(Status, std::unique_ptr<const TreeNode>)> callback);

  // Creates a |TreeNode| object for an existing |object| and stores it in the
  // given |node|.
  static Status FromObject(PageStorage* page_storage,
                           std::unique_ptr<const Object> object,
                           std::unique_ptr<const TreeNode>* node);

  // Creates a |TreeNode| object with the given entries. Contents of |children|
  // are optional and if a child is not present, an empty id should be given in
  // the corresponding index. The id of the new node is stored in |node_id|. It
  // is expected that |children| = |entries| + 1.
  static Status FromEntries(PageStorage* page_storage,
                            const std::vector<Entry>& entries,
                            const std::vector<ObjectId>& children,
                            ObjectId* node_id);

  // Creates a new tree node by merging |left| and |right|. |merged_child_id|
  // should contain the id of the new child node stored between the last entry
  // of |left| and the first entry of |right| in the merged node. |on_done| will
  // be called with the status and the new merged node's id.
  // Typical usage of this method would be to merge nodes bottom-up, each time
  // using the id of the newly merged node as the |merged_child_id| of the next
  // merge call.
  static void Merge(PageStorage* page_storage,
                    std::unique_ptr<const TreeNode> left,
                    std::unique_ptr<const TreeNode> right,
                    ObjectIdView merged_child_id,
                    std::function<void(Status, ObjectId)> on_done);

  // Starts a new mutation based on this node. See also |TreeNode::Mutation|.
  Mutation StartMutation() const;

  // Creates two new tree nodes by splitting this one. The left one will store
  // entries in [0, index) and the right one those in [index, GetKeyCount()).
  // The rightmost child of |left| will be set to |left_rightmost_child| and the
  // leftmost child of |right| will be set to |right_leftmost_child|. Both
  // |left_rightmost_child| and |right_leftmost_child| can be empty, if there is
  // no child in the given position. |on_done| will be called with the status
  // and if successfull with the ids of new left and right nodes.
  void Split(int index,
             ObjectIdView left_rightmost_child,
             ObjectIdView right_leftmost_child,
             std::function<void(Status, ObjectId, ObjectId)> on_done) const;

  // Returns the number of entries stored in this tree node.
  int GetKeyCount() const;

  // Finds the entry at position |index| and stores it in |entry|. |index| has
  // to be in [0, GetKeyCount() - 1].
  Status GetEntry(int index, Entry* entry) const;

  // Finds the child node at position |index| and calls the |callback| with the
  // result. |index| has to be in [0, GetKeyCount()]. If the child at the given
  // index is empty |NO_SUCH_CHILD| is returned and the value of |child| is not
  // updated.
  void GetChild(int index,
                std::function<void(Status, std::unique_ptr<const TreeNode>)>
                    callback) const;

  // Returns the id of the child node at position |index|. If the child at the
  // given index is empty, an empty string is returned. |index| has to be in [0,
  // GetKeyCount()].
  ObjectId GetChildId(int index) const;

  // Searches for the given |key| in this node. If it is found, |OK| is
  // returned and index contains the index of the entry. If not, |NOT_FOUND|
  // is returned and index stores the index of the child node where the key
  // might be found.
  Status FindKeyOrChild(convert::ExtendedStringView key, int* index) const;

  ObjectId GetId() const;

 private:
  TreeNode(PageStorage* page_storage,
           std::string id,
           std::vector<Entry> entries,
           std::vector<ObjectId> children);

  PageStorage* page_storage_;
  ObjectId id_;
  const std::vector<Entry> entries_;
  const std::vector<ObjectId> children_;
};

}  // namespace storage

#endif  // APPS_LEDGER_SRC_STORAGE_IMPL_BTREE_TREE_NODE_H_
