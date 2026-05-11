#include "onebase/storage/index/b_plus_tree_iterator.h"
#include <functional>
#include "onebase/common/exception.h"
#include "onebase/buffer/buffer_pool_manager.h"
#include "onebase/storage/page/b_plus_tree_leaf_page.h"

namespace onebase {

// Sequential cursor for leaf-level B+ tree traversal. It advances inside one
// leaf by index and follows next-page links when the current leaf is exhausted.

template <typename KeyType, typename ValueType, typename KeyComparator>
BPLUSTREE_ITERATOR_TYPE::BPlusTreeIterator(page_id_t page_id, int index, BufferPoolManager *bpm)
    : page_id_(page_id), index_(index), bpm_(bpm) {}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_ITERATOR_TYPE::IsEnd() const -> bool {
  return page_id_ == INVALID_PAGE_ID;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_ITERATOR_TYPE::operator*() -> const std::pair<KeyType, ValueType> & {
  auto *page = bpm_->FetchPage(page_id_);
  auto *leaf = reinterpret_cast<BPlusTreeLeafPage<KeyType, ValueType, KeyComparator> *>(page->GetData());
  current_ = {leaf->KeyAt(index_), leaf->ValueAt(index_)};
  bpm_->UnpinPage(page_id_, false);
  return current_;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_ITERATOR_TYPE::operator++() -> BPlusTreeIterator & {
  if (IsEnd()) {
    return *this;
  }

  auto *page = bpm_->FetchPage(page_id_);
  auto *leaf = reinterpret_cast<BPlusTreeLeafPage<KeyType, ValueType, KeyComparator> *>(page->GetData());
  index_++;
  if (index_ < leaf->GetSize()) {
    bpm_->UnpinPage(page_id_, false);
    return *this;
  }

  page_id_t next_page_id = leaf->GetNextPageId();
  bpm_->UnpinPage(page_id_, false);

  while (next_page_id != INVALID_PAGE_ID) {
    auto *next_page = bpm_->FetchPage(next_page_id);
    auto *next_leaf = reinterpret_cast<BPlusTreeLeafPage<KeyType, ValueType, KeyComparator> *>(next_page->GetData());
    if (next_leaf->GetSize() > 0) {
      page_id_ = next_page_id;
      index_ = 0;
      bpm_->UnpinPage(next_page_id, false);
      return *this;
    }
    const auto following_page_id = next_leaf->GetNextPageId();
    bpm_->UnpinPage(next_page_id, false);
    next_page_id = following_page_id;
  }

  page_id_ = INVALID_PAGE_ID;
  index_ = 0;
  return *this;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_ITERATOR_TYPE::operator==(const BPlusTreeIterator &other) const -> bool {
  return page_id_ == other.page_id_ && index_ == other.index_;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_ITERATOR_TYPE::operator!=(const BPlusTreeIterator &other) const -> bool {
  return !(*this == other);
}

template class BPlusTreeIterator<int, RID, std::less<int>>;

}  // namespace onebase
