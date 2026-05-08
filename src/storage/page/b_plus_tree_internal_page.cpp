#include "onebase/storage/page/b_plus_tree_internal_page.h"
#include <functional>
#include "onebase/common/exception.h"

namespace onebase {

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::Init(int max_size) {
  SetPageType(IndexPageType::INTERNAL_PAGE);
  SetMaxSize(max_size);
  SetSize(0);
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::KeyAt(int index) const -> KeyType {
  return array_[index].first;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::SetKeyAt(int index, const KeyType &key) {
  array_[index].first = key;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::ValueAt(int index) const -> ValueType {
  return array_[index].second;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::SetValueAt(int index, const ValueType &value) {
  array_[index].second = value;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::ValueIndex(const ValueType &value) const -> int {
  for (int i = 0; i < GetSize(); ++i) {
    if (array_[i].second == value) {
      return i;
    }
  }
  return -1;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::Lookup(const KeyType &key, const KeyComparator &comparator) const -> ValueType {
  int left = 1;
  int right = GetSize() - 1;
  int result = 0;

  while (left <= right) {
    const int mid = left + (right - left) / 2;
    if (!comparator(key, array_[mid].first)) {
      result = mid;
      left = mid + 1;
    } else {
      right = mid - 1;
    }
  }
  return array_[result].second;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::PopulateNewRoot(const ValueType &old_value, const KeyType &key,
                                                      const ValueType &new_value) {
  array_[0].second = old_value;
  array_[1] = {key, new_value};
  SetSize(2);
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::InsertNodeAfter(const ValueType &old_value, const KeyType &key,
                                                      const ValueType &new_value) -> int {
  const int old_index = ValueIndex(old_value);
  if (old_index == -1) {
    return GetSize();
  }

  const int insert_index = old_index + 1;
  for (int i = GetSize(); i > insert_index; --i) {
    array_[i] = array_[i - 1];
  }
  array_[insert_index] = {key, new_value};
  IncreaseSize(1);
  return GetSize();
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::Remove(int index) {
  for (int i = index; i + 1 < GetSize(); ++i) {
    array_[i] = array_[i + 1];
  }
  IncreaseSize(-1);
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::RemoveAndReturnOnlyChild() -> ValueType {
  const auto only_child = array_[0].second;
  SetSize(0);
  return only_child;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::MoveAllTo(BPlusTreeInternalPage *recipient, const KeyType &middle_key) {
  const int recipient_size = recipient->GetSize();
  recipient->array_[recipient_size] = {middle_key, array_[0].second};
  for (int i = 1; i < GetSize(); ++i) {
    recipient->array_[recipient_size + i] = array_[i];
  }
  recipient->IncreaseSize(GetSize());
  SetSize(0);
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::MoveHalfTo(BPlusTreeInternalPage *recipient, const KeyType &middle_key) {
  const int start = GetSize() / 2 + 1;
  const int move_count = GetSize() - start;

  recipient->array_[0].second = array_[start - 1].second;
  for (int i = 0; i < move_count; ++i) {
    recipient->array_[i + 1] = array_[start + i];
  }
  recipient->SetSize(move_count + 1);
  SetSize(start - 1);
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::MoveFirstToEndOf(BPlusTreeInternalPage *recipient, const KeyType &middle_key) {
  if (GetSize() == 0) {
    return;
  }

  recipient->array_[recipient->GetSize()] = {middle_key, array_[0].second};
  recipient->IncreaseSize(1);
  array_[0].second = array_[1].second;
  Remove(1);
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::MoveLastToFrontOf(BPlusTreeInternalPage *recipient, const KeyType &middle_key) {
  if (GetSize() == 0) {
    return;
  }

  for (int i = recipient->GetSize(); i > 0; --i) {
    recipient->array_[i] = recipient->array_[i - 1];
  }
  recipient->array_[0].second = array_[GetSize() - 1].second;
  recipient->array_[1].first = middle_key;
  recipient->IncreaseSize(1);
  IncreaseSize(-1);
}

template class BPlusTreeInternalPage<int, page_id_t, std::less<int>>;

}  // namespace onebase
