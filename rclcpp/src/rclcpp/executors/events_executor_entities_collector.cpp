// Copyright 2020 Open Source Robotics Foundation, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "rclcpp/executors/events_executor.hpp"
#include "rclcpp/executors/events_executor_entities_collector.hpp"

using rclcpp::executors::EventsExecutorEntitiesCollector;

EventsExecutorEntitiesCollector::EventsExecutorEntitiesCollector(
  EventsExecutor * executor_context,
  TimersManager::SharedPtr timers_manager)
{
  associated_executor_ = executor_context;
  timers_manager_ = timers_manager;
}

EventsExecutorEntitiesCollector::~EventsExecutorEntitiesCollector()
{
  // Disassociate all nodes
  for (const auto & weak_node : weak_nodes_) {
    auto node = weak_node.lock();
    if (node) {
      std::atomic_bool & has_executor = node->get_associated_with_executor_atomic();
      has_executor.store(false);
    }
  }
  weak_nodes_.clear();
}

void
EventsExecutorEntitiesCollector::execute()
{
  // This function is called when the associated executor is notified that something changed.
  // We do not know if an entity has been added or remode so we have to rebuild everything.

  timers_manager_->clear_all();

  for (auto & weak_node : weak_nodes_) {
    auto node = weak_node.lock();
    if (!node) {
      continue;
    }
    set_entities_callbacks(node);
  }
}

void
EventsExecutorEntitiesCollector::add_node(
  rclcpp::node_interfaces::NodeBaseInterface::SharedPtr node_ptr)
{
  // If the node already has an executor
  std::atomic_bool & has_executor = node_ptr->get_associated_with_executor_atomic();

  if (has_executor.exchange(true)) {
    throw std::runtime_error("Node has already been added to an executor.");
  }

  weak_nodes_.push_back(node_ptr);

  set_entities_callbacks(node_ptr);

  // Set node's guard condition callback, so if new entities are added while
  // spinning we can set their callback.
  rcl_ret_t ret = rcl_guard_condition_set_events_executor_callback(
    associated_executor_,
    &EventsExecutor::push_event,
    this,
    node_ptr->get_notify_guard_condition(),
    false /* Discard previous events */);

  if (ret != RCL_RET_OK) {
    throw std::runtime_error("Couldn't set node guard condition callback");
  }
}

void
EventsExecutorEntitiesCollector::remove_node(
  rclcpp::node_interfaces::NodeBaseInterface::SharedPtr node_ptr)
{
  auto node_it = weak_nodes_.begin();

  // Here we unset the node entities callback.
  while (node_it != weak_nodes_.end()) {
    bool matched = (node_it->lock() == node_ptr);
    if (matched) {
      // Node found: unset its entities callbacks
      rcl_ret_t ret = rcl_guard_condition_set_events_executor_callback(
        nullptr, nullptr, nullptr,
        node_ptr->get_notify_guard_condition(),
        false);

      if (ret != RCL_RET_OK) {
        throw std::runtime_error(std::string("Couldn't set guard condition callback"));
      }

      // Unset entities callbacks
      for (auto & weak_group : node_ptr->get_callback_groups()) {
        auto group = weak_group.lock();
        if (!group || !group->can_be_taken_from().load()) {
          continue;
        }
        group->find_timer_ptrs_if(
          [this](const rclcpp::TimerBase::SharedPtr & timer) {
            if (timer) {
              timers_manager_->remove_timer(timer);
            }
            return false;
          });
        group->find_subscription_ptrs_if(
          [this](const rclcpp::SubscriptionBase::SharedPtr & subscription) {
            if (subscription) {
              subscription->set_events_executor_callback(nullptr, nullptr);
            }
            return false;
          });
        group->find_service_ptrs_if(
          [this](const rclcpp::ServiceBase::SharedPtr & service) {
            if (service) {
              service->set_events_executor_callback(nullptr, nullptr);
            }
            return false;
          });
        group->find_client_ptrs_if(
          [this](const rclcpp::ClientBase::SharedPtr & client) {
            if (client) {
              client->set_events_executor_callback(nullptr, nullptr);
            }
            return false;
          });
        group->find_waitable_ptrs_if(
          [this](const rclcpp::Waitable::SharedPtr & waitable) {
            if (waitable) {
              waitable->set_events_executor_callback(nullptr, nullptr);
            }
            return false;
          });
      }

      weak_nodes_.erase(node_it);
      return;
    } else {
      ++node_it;
    }
  }
}

void
EventsExecutorEntitiesCollector::set_entities_callbacks(
  rclcpp::node_interfaces::NodeBaseInterface::SharedPtr node)
{
  // Check in all the callback groups
  for (auto & weak_group : node->get_callback_groups()) {
    auto group = weak_group.lock();
    if (!group || !group->can_be_taken_from().load()) {
      continue;
    }
    group->find_timer_ptrs_if(
      [this](const rclcpp::TimerBase::SharedPtr & timer) {
        if (timer) {
          timers_manager_->add_timer(timer);
        }
        return false;
      });
    group->find_subscription_ptrs_if(
      [this](const rclcpp::SubscriptionBase::SharedPtr & subscription) {
        if (subscription) {
          subscription->set_events_executor_callback(associated_executor_, &EventsExecutor::push_event);
        }
        return false;
      });
    group->find_service_ptrs_if(
      [this](const rclcpp::ServiceBase::SharedPtr & service) {
        if (service) {
          service->set_events_executor_callback(associated_executor_, &EventsExecutor::push_event);
        }
        return false;
      });
    group->find_client_ptrs_if(
      [this](const rclcpp::ClientBase::SharedPtr & client) {
        if (client) {
          client->set_events_executor_callback(associated_executor_, &EventsExecutor::push_event);
        }
        return false;
      });
    group->find_waitable_ptrs_if(
      [this](const rclcpp::Waitable::SharedPtr & waitable) {
        if (waitable) {
          waitable->set_events_executor_callback(associated_executor_, &EventsExecutor::push_event);
        }
        return false;
      });
  }
}