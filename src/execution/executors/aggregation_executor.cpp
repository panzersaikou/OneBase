#include "onebase/execution/executors/aggregation_executor.h"
#include <unordered_map>
#include "onebase/common/exception.h"

namespace onebase {

namespace {

auto MakeGroupKey(const std::vector<Value> &values) -> std::string {
  std::string key;
  for (const auto &value : values) {
    key += value.ToString();
    key.push_back('\x1f');
  }
  return key;
}

}  // namespace

AggregationExecutor::AggregationExecutor(ExecutorContext *exec_ctx, const AggregationPlanNode *plan,
                                          std::unique_ptr<AbstractExecutor> child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {}

void AggregationExecutor::Init() {
  result_tuples_.clear();
  cursor_ = 0;

  struct GroupState {
    std::vector<Value> group_values;
    std::vector<Value> aggregate_values;
    std::vector<bool> initialized;
  };

  std::unordered_map<std::string, GroupState> groups;
  child_executor_->Init();

  Tuple child_tuple;
  RID child_rid;
  while (child_executor_->Next(&child_tuple, &child_rid)) {
    std::vector<Value> group_values;
    group_values.reserve(plan_->GetGroupBys().size());
    for (const auto &expr : plan_->GetGroupBys()) {
      group_values.push_back(expr->Evaluate(&child_tuple, &child_executor_->GetOutputSchema()));
    }

    auto group_key = MakeGroupKey(group_values);
    auto [it, inserted] = groups.emplace(group_key, GroupState{group_values, {}, {}});
    auto &state = it->second;
    if (inserted) {
      state.aggregate_values.resize(plan_->GetAggregateTypes().size());
      state.initialized.assign(plan_->GetAggregateTypes().size(), false);
      for (size_t i = 0; i < plan_->GetAggregateTypes().size(); ++i) {
        if (plan_->GetAggregateTypes()[i] == AggregationType::CountStarAggregate ||
            plan_->GetAggregateTypes()[i] == AggregationType::CountAggregate) {
          state.aggregate_values[i] = Value(TypeId::INTEGER, 0);
          state.initialized[i] = true;
        }
      }
    }

    for (size_t i = 0; i < plan_->GetAggregateTypes().size(); ++i) {
      const auto agg_type = plan_->GetAggregateTypes()[i];
      Value input;
      if (agg_type != AggregationType::CountStarAggregate && i < plan_->GetAggregates().size()) {
        input = plan_->GetAggregates()[i]->Evaluate(&child_tuple, &child_executor_->GetOutputSchema());
      }

      switch (agg_type) {
        case AggregationType::CountStarAggregate:
        case AggregationType::CountAggregate:
          state.aggregate_values[i] = state.aggregate_values[i].Add(Value(TypeId::INTEGER, 1));
          break;
        case AggregationType::SumAggregate:
          state.aggregate_values[i] = state.initialized[i] ? state.aggregate_values[i].Add(input) : input;
          state.initialized[i] = true;
          break;
        case AggregationType::MinAggregate:
          if (!state.initialized[i] || input.CompareLessThan(state.aggregate_values[i]).GetAsBoolean()) {
            state.aggregate_values[i] = input;
            state.initialized[i] = true;
          }
          break;
        case AggregationType::MaxAggregate:
          if (!state.initialized[i] || input.CompareGreaterThan(state.aggregate_values[i]).GetAsBoolean()) {
            state.aggregate_values[i] = input;
            state.initialized[i] = true;
          }
          break;
      }
    }
  }

  if (groups.empty() && plan_->GetGroupBys().empty()) {
    std::vector<Value> values;
    values.reserve(plan_->GetAggregateTypes().size());
    for (auto agg_type : plan_->GetAggregateTypes()) {
      if (agg_type == AggregationType::CountStarAggregate || agg_type == AggregationType::CountAggregate) {
        values.emplace_back(TypeId::INTEGER, 0);
      } else {
        values.emplace_back(TypeId::INTEGER);
      }
    }
    if (!values.empty()) {
      result_tuples_.emplace_back(std::move(values));
    }
    return;
  }

  for (auto &[key, state] : groups) {
    std::vector<Value> values = state.group_values;
    values.insert(values.end(), state.aggregate_values.begin(), state.aggregate_values.end());
    result_tuples_.emplace_back(std::move(values));
  }
}

auto AggregationExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  if (cursor_ >= result_tuples_.size()) {
    return false;
  }
  *tuple = result_tuples_[cursor_++];
  if (rid != nullptr) {
    *rid = RID{};
  }
  return true;
}

}  // namespace onebase
