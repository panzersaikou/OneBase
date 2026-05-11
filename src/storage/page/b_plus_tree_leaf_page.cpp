#include "onebase/storage/page/b_plus_tree_leaf_page.h"
#include <functional>
#include "onebase/common/exception.h"

namespace onebase {

namespace {

template <typename KeyType, typename KeyComparator>
auto KeyEqual(const KeyType &lhs, const KeyType &rhs, const KeyComparator &comparator) -> bool {
  return !comparator(lhs, rhs) && !comparator(rhs, lhs);
}

}  // namespace

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_LEAF_PAGE_TYPE::Init(int max_size) {
  SetPageType(IndexPageType::LEAF_PAGE);
  SetMaxSize(max_size);
  SetSize(0);
  next_page_id_ = INVALID_PAGE_ID;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto B_PLUS_TREE_LEAF_PAGE_TYPE::KeyAt(int index) const -> KeyType {
  return array_[index].first;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto B_PLUS_TREE_LEAF_PAGE_TYPE::ValueAt(int index) const -> ValueType {
  return array_[index].second;
}

// Return the first slot whose key is not less than the target key.
template <typename KeyType, typename ValueType, typename KeyComparator>
auto B_PLUS_TREE_LEAF_PAGE_TYPE::KeyIndex(const KeyType &key, const KeyComparator &comparator) const -> int {
  int left = 0;
  int right = GetSize();

  while (left < right) {
    const int mid = left + (right - left) / 2;
    if (comparator(array_[mid].first, key)) {
      left = mid + 1;
    } else {
      right = mid;
    }
  }
  return left;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto B_PLUS_TREE_LEAF_PAGE_TYPE::Lookup(const KeyType &key, ValueType *value,
                                         const KeyComparator &comparator) const -> bool {
  const int index = KeyIndex(key, comparator);
  if (index >= GetSize() || !KeyEqual(key, array_[index].first, comparator)) {
    return false;
  }
  *value = array_[index].second;
  return true;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto B_PLUS_TREE_LEAF_PAGE_TYPE::Insert(const KeyType &key, const ValueType &value,
                                         const KeyComparator &comparator) -> int {
  const int index = KeyIndex(key, comparator);
  if (index < GetSize() && KeyEqual(key, array_[index].first, comparator)) {
    return GetSize();
  }

  for (int i = GetSize(); i > index; --i) {
    array_[i] = array_[i - 1];
  }
  array_[index] = {key, value};
  IncreaseSize(1);
  return GetSize();
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto B_PLUS_TREE_LEAF_PAGE_TYPE::RemoveAndDeleteRecord(const KeyType &key,
                                                        const KeyComparator &comparator) -> int {
  const int index = KeyIndex(key, comparator);
  if (index >= GetSize() || !KeyEqual(key, array_[index].first, comparator)) {
    return GetSize();
  }

  for (int i = index; i + 1 < GetSize(); ++i) {
    array_[i] = array_[i + 1];
  }
  IncreaseSize(-1);
  return GetSize();
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_LEAF_PAGE_TYPE::MoveHalfTo(BPlusTreeLeafPage *recipient) {
  const int start = GetSize() / 2;
  const int move_count = GetSize() - start;
  for (int i = 0; i < move_count; ++i) {
    recipient->array_[recipient->GetSize() + i] = array_[start + i];
  }
  recipient->IncreaseSize(move_count);
  SetSize(start);
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_LEAF_PAGE_TYPE::MoveAllTo(BPlusTreeLeafPage *recipient) {
  const int recipient_size = recipient->GetSize();
  for (int i = 0; i < GetSize(); ++i) {
    recipient->array_[recipient_size + i] = array_[i];
  }
  recipient->IncreaseSize(GetSize());
  SetSize(0);
  recipient->SetNextPageId(GetNextPageId());
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_LEAF_PAGE_TYPE::MoveFirstToEndOf(BPlusTreeLeafPage *recipient) {
  if (GetSize() == 0) {
    return;
  }

  recipient->array_[recipient->GetSize()] = array_[0];
  recipient->IncreaseSize(1);
  for (int i = 0; i + 1 < GetSize(); ++i) {
    array_[i] = array_[i + 1];
  }
  IncreaseSize(-1);
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_LEAF_PAGE_TYPE::MoveLastToFrontOf(BPlusTreeLeafPage *recipient) {
  if (GetSize() == 0) {
    return;
  }

  for (int i = recipient->GetSize(); i > 0; --i) {
    recipient->array_[i] = recipient->array_[i - 1];
  }
  recipient->array_[0] = array_[GetSize() - 1];
  recipient->IncreaseSize(1);
  IncreaseSize(-1);
}

template class BPlusTreeLeafPage<int, RID, std::less<int>>;

}  // namespace onebase
