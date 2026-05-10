#include "onebase/execution/executors/update_executor.h"
#include "onebase/common/exception.h"

namespace onebase {

UpdateExecutor::UpdateExecutor(ExecutorContext *exec_ctx, const UpdatePlanNode *plan,
                               std::unique_ptr<AbstractExecutor> child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {}

void UpdateExecutor::Init() {
  child_executor_->Init();
  has_updated_ = false;
}

auto UpdateExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  if (has_updated_) {
    return false;
  }
  has_updated_ = true;

  auto *catalog = GetExecutorContext()->GetCatalog();
  auto *table_info = catalog->GetTable(plan_->GetTableOid());
  auto indexes = catalog->GetTableIndexes(table_info->name_);
  const auto &schema = table_info->schema_;

  int32_t count = 0;
  Tuple old_tuple;
  RID old_rid;
  while (child_executor_->Next(&old_tuple, &old_rid)) {
    std::vector<Value> values;
    values.reserve(plan_->GetUpdateExpressions().size());
    for (const auto &expr : plan_->GetUpdateExpressions()) {
      values.push_back(expr->Evaluate(&old_tuple, &schema));
    }
    Tuple new_tuple(std::move(values));

    for (auto *index_info : indexes) {
      if (index_info->SupportsPointLookup()) {
        int32_t old_key = old_tuple.GetValue(&schema, index_info->GetLookupAttr()).GetAsInteger();
        index_info->RemoveEntry(old_key, old_rid);
      }
    }

    if (table_info->table_->UpdateTuple(old_rid, new_tuple)) {
      count++;
      for (auto *index_info : indexes) {
        if (index_info->SupportsPointLookup()) {
          int32_t new_key = new_tuple.GetValue(&schema, index_info->GetLookupAttr()).GetAsInteger();
          index_info->InsertEntry(new_key, old_rid);
        }
      }
    }
  }

  *tuple = Tuple({Value(TypeId::INTEGER, count)});
  if (rid != nullptr) {
    *rid = RID{};
  }
  return true;
}

}  // namespace onebase
