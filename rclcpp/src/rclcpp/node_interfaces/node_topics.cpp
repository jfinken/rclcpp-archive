// Copyright 2016 Open Source Robotics Foundation, Inc.
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

#include "rclcpp/node_interfaces/node_topics.hpp"

#include <string>

#include "rclcpp/intra_process_manager.hpp"
#include "rclcpp/exceptions.hpp"

using rclcpp::exceptions::throw_from_rcl_error;

using rclcpp::node_interfaces::NodeTopics;

NodeTopics::NodeTopics(rclcpp::node_interfaces::NodeBaseInterface * node_base)
: node_base_(node_base)
{}

NodeTopics::~NodeTopics()
{}

rclcpp::PublisherBase::SharedPtr
NodeTopics::create_publisher(
  const std::string & topic_name,
  const rclcpp::PublisherFactory & publisher_factory,
  const rcl_publisher_options_t & publisher_options,
  bool use_intra_process)
{
  // Create the MessageT specific Publisher using the factory, but store it as PublisherBase.
  auto publisher = publisher_factory.create_typed_publisher(
    node_base_, topic_name, publisher_options);

  // Setup intra process publishing if requested.
  if (use_intra_process) {
    // Check if the QoS allows to use intra-process
    if (publisher_options.qos.history == RMW_QOS_POLICY_HISTORY_KEEP_ALL) {
      throw std::invalid_argument(
            "intraprocess communication is not allowed with keep all history qos policy");
    }
    if (publisher_options.qos.durability != RMW_QOS_POLICY_DURABILITY_VOLATILE) {
      throw std::invalid_argument(
            "intraprocess communication allowed only with volatile durability");
    }
    // Get the intra process manager for this context.
    auto context = node_base_->get_context();
    auto ipm = context->get_sub_context<rclcpp::intra_process_manager::IntraProcessManager>();
    // Register the publisher with the intra process manager.
    uint64_t intra_process_publisher_id = ipm->add_publisher(publisher);

    // Store references to the intra-process manager and the publisher intra-process id
    publisher->setup_intra_process(
      intra_process_publisher_id,
      ipm);
  }

  // Return the completed publisher.
  return publisher;
}

void
NodeTopics::add_publisher(
  rclcpp::PublisherBase::SharedPtr publisher,
  rclcpp::callback_group::CallbackGroup::SharedPtr callback_group)
{
  // Assign to a group.
  if (callback_group) {
    if (!node_base_->callback_group_in_node(callback_group)) {
      throw std::runtime_error("Cannot create publisher, callback group not in node.");
    }
  } else {
    callback_group = node_base_->get_default_callback_group();
  }

  for (auto & publisher_event : publisher->get_event_handlers()) {
    callback_group->add_waitable(publisher_event);
  }

  // Notify the executor that a new publisher was created using the parent Node.
  {
    auto notify_guard_condition_lock = node_base_->acquire_notify_guard_condition_lock();
    if (rcl_trigger_guard_condition(node_base_->get_notify_guard_condition()) != RCL_RET_OK) {
      throw std::runtime_error(
              std::string("Failed to notify wait set on publisher creation: ") +
              rmw_get_error_string().str);
    }
  }
}

rclcpp::SubscriptionBase::SharedPtr
NodeTopics::create_subscription(
  const std::string & topic_name,
  const rclcpp::SubscriptionFactory & subscription_factory,
  const rcl_subscription_options_t & subscription_options,
  bool use_intra_process,
  rclcpp::IntraProcessBufferType buffer_type)
{
  auto subscription = subscription_factory.create_typed_subscription(
    node_base_, topic_name, subscription_options);

  // Setup intra process communication if requested.
  if (use_intra_process) {
    // Check if the QoS allows to use intra-process
    if (subscription_options.qos.history == RMW_QOS_POLICY_HISTORY_KEEP_ALL) {
      throw std::invalid_argument(
              "intraprocess communication is not allowed with keep all history qos policy");
    }
    if (subscription_options.qos.history == RMW_QOS_POLICY_HISTORY_KEEP_LAST
        && subscription_options.qos.depth == 0)
    {
      throw std::invalid_argument(
              "intraprocess communication is not allowed with keep last history and 0 depth qos policy");
    }
    if (subscription_options.qos.durability != RMW_QOS_POLICY_DURABILITY_VOLATILE) {
      throw std::invalid_argument(
              "intraprocess communication allowed only with volatile durability");
    }
    // Get the intra process manager for this context.
    auto context = node_base_->get_context();
    auto ipm =
      context->get_sub_context<rclcpp::intra_process_manager::IntraProcessManager>();

    // Create a subscription intra-process
    // Note: topic_name does not include the namespace, use subscription->get_topic_name().
    auto subscription_intra_process = subscription_factory.create_typed_subscription_intra_process(
      buffer_type,
      context,
      subscription->get_topic_name(),
      subscription_options);

    // Register the subscription intra-process with the intra-process manager and get its id.
    uint64_t intra_process_subscription_id = ipm->add_subscription(subscription_intra_process);

    // Store references to the intra-process manager and the subscription intra-process id.
    subscription->setup_intra_process(
      intra_process_subscription_id,
      ipm);
  }

  // Return the completed subscription.
  return subscription;
}

void
NodeTopics::add_subscription(
  rclcpp::SubscriptionBase::SharedPtr subscription,
  rclcpp::callback_group::CallbackGroup::SharedPtr callback_group,
  bool use_intra_process)
{
  // Assign to a group.
  if (callback_group) {
    if (!node_base_->callback_group_in_node(callback_group)) {
      // TODO(jacquelinekay): use custom exception
      throw std::runtime_error("Cannot create subscription, callback group not in node.");
    }
  } else {
    callback_group = node_base_->get_default_callback_group();
  }

  callback_group->add_subscription(subscription);
  for (auto & subscription_event : subscription->get_event_handlers()) {
    callback_group->add_waitable(subscription_event);
  }

  if (use_intra_process) {
    // Get the intra process manager for this context.
    auto context = node_base_->get_context();
    auto ipm =
      context->get_sub_context<rclcpp::intra_process_manager::IntraProcessManager>();

    // Use the id to retrieve the subscription intra-process from the intra-process manager.
    auto subscription_intra_process =
      ipm->get_subscription_intra_process(subscription->get_intra_process_id());

    // Add to the callback group to be notified about intra-process msgs.
    callback_group->add_waitable(subscription_intra_process);
  }

  // Notify the executor that a new subscription was created using the parent Node.
  {
    auto notify_guard_condition_lock = node_base_->acquire_notify_guard_condition_lock();
    if (rcl_trigger_guard_condition(node_base_->get_notify_guard_condition()) != RCL_RET_OK) {
      throw std::runtime_error(
              std::string("Failed to notify wait set on subscription creation: ") +
              rmw_get_error_string().str
      );
    }
  }
}

rclcpp::node_interfaces::NodeBaseInterface *
NodeTopics::get_node_base_interface() const
{
  return node_base_;
}
