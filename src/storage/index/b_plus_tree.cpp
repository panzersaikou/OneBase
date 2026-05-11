#include "onebase/storage/index/b_plus_tree.h"
#include "onebase/storage/index/b_plus_tree_iterator.h"
#include <functional>
#include <vector>
#include "onebase/common/exception.h"

namespace onebase {

// Top-level B+ tree operations. The tree descends through internal pages to a
// leaf, performs lookup/insert/delete there, and creates a new root when splits
// propagate past the old root.

template <typename KeyType, typename ValueType, typename KeyComparator>
BPLUSTREE_TYPE::BPlusTree(std::string name, BufferPoolManager *bpm, const KeyComparator &comparator,
                           int leaf_max_size, int internal_max_size)
    : Index(std::move(name)), bpm_(bpm), comparator_(comparator),
      leaf_max_size_(leaf_max_size), internal_max_size_(internal_max_size) {
  if (leaf_max_size_ == 0) {
    leaf_max_size_ = static_cast<int>(
        (ONEBASE_PAGE_SIZE - sizeof(BPlusTreePage) - sizeof(page_id_t)) /
        (sizeof(KeyType) + sizeof(ValueType)));
  }
  if (internal_max_size_ == 0) {
    internal_max_size_ = static_cast<int>(
        (ONEBASE_PAGE_SIZE - sizeof(BPlusTreePage)) /
        (sizeof(KeyType) + sizeof(page_id_t)));
  }
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_TYPE::IsEmpty() const -> bool {
  return root_page_id_ == INVALID_PAGE_ID;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_TYPE::Insert(const KeyType &key, const ValueType &value) -> bool {
  if (IsEmpty()) {
    page_id_t root_page_id;
    auto *root_page = bpm_->NewPage(&root_page_id);
    auto *root = reinterpret_cast<LeafPage *>(root_page->GetData());
    root->Init(leaf_max_size_);
    root->SetParentPageId(INVALID_PAGE_ID);
    root->Insert(key, value, comparator_);
    root_page_id_ = root_page_id;
    bpm_->UnpinPage(root_page_id, true);
    return true;
  }

  std::vector<page_id_t> parents;
  page_id_t leaf_page_id = root_page_id_;
  while (true) {
    auto *page = bpm_->FetchPage(leaf_page_id);
    auto *tree_page = reinterpret_cast<BPlusTreePage *>(page->GetData());
    if (tree_page->IsLeafPage()) {
      bpm_->UnpinPage(leaf_page_id, false);
      break;
    }
    auto *internal = reinterpret_cast<InternalPage *>(page->GetData());
    parents.push_back(leaf_page_id);
    leaf_page_id = internal->Lookup(key, comparator_);
    bpm_->UnpinPage(page->GetPageId(), false);
  }

  auto *leaf_page = bpm_->FetchPage(leaf_page_id);
  auto *leaf = reinterpret_cast<LeafPage *>(leaf_page->GetData());
  ValueType existing;
  if (leaf->Lookup(key, &existing, comparator_)) {
    bpm_->UnpinPage(leaf_page_id, false);
    return false;
  }

  leaf->Insert(key, value, comparator_);
  if (leaf->GetSize() <= leaf->GetMaxSize()) {
    bpm_->UnpinPage(leaf_page_id, true);
    return true;
  }

  page_id_t new_leaf_page_id;
  auto *new_leaf_page = bpm_->NewPage(&new_leaf_page_id);
  auto *new_leaf = reinterpret_cast<LeafPage *>(new_leaf_page->GetData());
  new_leaf->Init(leaf_max_size_);
  new_leaf->SetParentPageId(leaf->GetParentPageId());
  new_leaf->SetNextPageId(leaf->GetNextPageId());
  leaf->SetNextPageId(new_leaf_page_id);
  leaf->MoveHalfTo(new_leaf);

  KeyType separator = new_leaf->KeyAt(0);
  page_id_t left_page_id = leaf_page_id;
  page_id_t right_page_id = new_leaf_page_id;
  bpm_->UnpinPage(leaf_page_id, true);
  bpm_->UnpinPage(new_leaf_page_id, true);

  while (!parents.empty()) {
    const page_id_t parent_page_id = parents.back();
    parents.pop_back();

    auto *parent_page = bpm_->FetchPage(parent_page_id);
    auto *parent = reinterpret_cast<InternalPage *>(parent_page->GetData());
    parent->InsertNodeAfter(left_page_id, separator, right_page_id);

    auto *right_child_page = bpm_->FetchPage(right_page_id);
    auto *right_child = reinterpret_cast<BPlusTreePage *>(right_child_page->GetData());
    right_child->SetParentPageId(parent_page_id);
    bpm_->UnpinPage(right_page_id, true);

    if (parent->GetSize() <= parent->GetMaxSize()) {
      bpm_->UnpinPage(parent_page_id, true);
      return true;
    }

    page_id_t new_internal_page_id;
    auto *new_internal_page = bpm_->NewPage(&new_internal_page_id);
    auto *new_internal = reinterpret_cast<InternalPage *>(new_internal_page->GetData());
    new_internal->Init(internal_max_size_);
    new_internal->SetParentPageId(parent->GetParentPageId());

    const int middle_index = parent->GetSize() / 2;
    separator = parent->KeyAt(middle_index);
    parent->MoveHalfTo(new_internal, separator);

    for (int i = 0; i < new_internal->GetSize(); ++i) {
      auto child_page_id = new_internal->ValueAt(i);
      auto *child_page = bpm_->FetchPage(child_page_id);
      auto *child = reinterpret_cast<BPlusTreePage *>(child_page->GetData());
      child->SetParentPageId(new_internal_page_id);
      bpm_->UnpinPage(child_page_id, true);
    }

    left_page_id = parent_page_id;
    right_page_id = new_internal_page_id;
    bpm_->UnpinPage(parent_page_id, true);
    bpm_->UnpinPage(new_internal_page_id, true);
  }

  page_id_t new_root_page_id;
  auto *new_root_page = bpm_->NewPage(&new_root_page_id);
  auto *new_root = reinterpret_cast<InternalPage *>(new_root_page->GetData());
  new_root->Init(internal_max_size_);
  new_root->SetParentPageId(INVALID_PAGE_ID);
  new_root->PopulateNewRoot(left_page_id, separator, right_page_id);

  for (auto child_page_id : {left_page_id, right_page_id}) {
    auto *child_page = bpm_->FetchPage(child_page_id);
    auto *child = reinterpret_cast<BPlusTreePage *>(child_page->GetData());
    child->SetParentPageId(new_root_page_id);
    bpm_->UnpinPage(child_page_id, true);
  }

  root_page_id_ = new_root_page_id;
  bpm_->UnpinPage(new_root_page_id, true);
  return true;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void BPLUSTREE_TYPE::Remove(const KeyType &key) {
  if (IsEmpty()) {
    return;
  }

  page_id_t leaf_page_id = root_page_id_;
  while (true) {
    auto *page = bpm_->FetchPage(leaf_page_id);
    auto *tree_page = reinterpret_cast<BPlusTreePage *>(page->GetData());
    if (tree_page->IsLeafPage()) {
      bpm_->UnpinPage(leaf_page_id, false);
      break;
    }
    auto *internal = reinterpret_cast<InternalPage *>(page->GetData());
    leaf_page_id = internal->Lookup(key, comparator_);
    bpm_->UnpinPage(page->GetPageId(), false);
  }

  auto *leaf_page = bpm_->FetchPage(leaf_page_id);
  auto *leaf = reinterpret_cast<LeafPage *>(leaf_page->GetData());
  const int old_size = leaf->GetSize();
  leaf->RemoveAndDeleteRecord(key, comparator_);
  const bool removed = leaf->GetSize() != old_size;
  const bool root_leaf_empty = leaf_page_id == root_page_id_ && leaf->GetSize() == 0;
  bpm_->UnpinPage(leaf_page_id, removed);

  if (!removed) {
    return;
  }

  if (root_leaf_empty) {
    root_page_id_ = INVALID_PAGE_ID;
    bpm_->DeletePage(leaf_page_id);
    return;
  }

  auto it = Begin();
  if (it == End()) {
    root_page_id_ = INVALID_PAGE_ID;
  }
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_TYPE::GetValue(const KeyType &key, std::vector<ValueType> *result) -> bool {
  if (IsEmpty()) {
    return false;
  }

  page_id_t page_id = root_page_id_;
  while (true) {
    auto *page = bpm_->FetchPage(page_id);
    auto *tree_page = reinterpret_cast<BPlusTreePage *>(page->GetData());
    if (tree_page->IsLeafPage()) {
      auto *leaf = reinterpret_cast<LeafPage *>(page->GetData());
      ValueType value;
      const bool found = leaf->Lookup(key, &value, comparator_);
      if (found) {
        result->push_back(value);
      }
      bpm_->UnpinPage(page_id, false);
      return found;
    }

    auto *internal = reinterpret_cast<InternalPage *>(page->GetData());
    const page_id_t next_page_id = internal->Lookup(key, comparator_);
    bpm_->UnpinPage(page_id, false);
    page_id = next_page_id;
  }
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_TYPE::Begin() -> Iterator {
  if (IsEmpty()) {
    return End();
  }

  page_id_t page_id = root_page_id_;
  while (true) {
    auto *page = bpm_->FetchPage(page_id);
    auto *tree_page = reinterpret_cast<BPlusTreePage *>(page->GetData());
    if (tree_page->IsLeafPage()) {
      auto *leaf = reinterpret_cast<LeafPage *>(page->GetData());
      while (leaf->GetSize() == 0 && leaf->GetNextPageId() != INVALID_PAGE_ID) {
        const page_id_t next_page_id = leaf->GetNextPageId();
        bpm_->UnpinPage(page_id, false);
        page_id = next_page_id;
        page = bpm_->FetchPage(page_id);
        leaf = reinterpret_cast<LeafPage *>(page->GetData());
      }
      const bool is_empty_leaf = leaf->GetSize() == 0;
      bpm_->UnpinPage(page_id, false);
      return is_empty_leaf ? End() : Iterator(page_id, 0, bpm_);
    }

    auto *internal = reinterpret_cast<InternalPage *>(page->GetData());
    const page_id_t next_page_id = internal->ValueAt(0);
    bpm_->UnpinPage(page_id, false);
    page_id = next_page_id;
  }
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_TYPE::Begin(const KeyType &key) -> Iterator {
  if (IsEmpty()) {
    return End();
  }

  page_id_t page_id = root_page_id_;
  while (true) {
    auto *page = bpm_->FetchPage(page_id);
    auto *tree_page = reinterpret_cast<BPlusTreePage *>(page->GetData());
    if (tree_page->IsLeafPage()) {
      auto *leaf = reinterpret_cast<LeafPage *>(page->GetData());
      int index = leaf->KeyIndex(key, comparator_);
      while (index >= leaf->GetSize() && leaf->GetNextPageId() != INVALID_PAGE_ID) {
        const page_id_t next_page_id = leaf->GetNextPageId();
        bpm_->UnpinPage(page_id, false);
        page_id = next_page_id;
        page = bpm_->FetchPage(page_id);
        leaf = reinterpret_cast<LeafPage *>(page->GetData());
        index = 0;
      }

      const bool is_end = index >= leaf->GetSize();
      bpm_->UnpinPage(page_id, false);
      return is_end ? End() : Iterator(page_id, index, bpm_);
    }

    auto *internal = reinterpret_cast<InternalPage *>(page->GetData());
    const page_id_t next_page_id = internal->Lookup(key, comparator_);
    bpm_->UnpinPage(page_id, false);
    page_id = next_page_id;
  }
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_TYPE::End() -> Iterator {
  return Iterator(INVALID_PAGE_ID, 0, bpm_);
}

template class BPlusTree<int, RID, std::less<int>>;

}  // namespace onebase
