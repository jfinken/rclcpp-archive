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

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <string>

#include "rclcpp/executors/events_executor.hpp"

#include "test_msgs/srv/empty.hpp"
#include "test_msgs/msg/empty.hpp"

#include "../../mocking_utils/patch.hpp"

using namespace std::chrono_literals;

using rclcpp::executors::EventsExecutor;
using rclcpp::executors::EventsExecutorNotifyWaitable;

class TestEventsExecutor : public ::testing::Test
{
public:
  void SetUp()
  {
    rclcpp::init(0, nullptr);
  }

  void TearDown()
  {
    rclcpp::shutdown();
  }
};

TEST_F(TestEventsExecutor, notify_waitable)
{
  auto notifier = std::make_shared<EventsExecutorNotifyWaitable>();

  // Waitset methods can't be used on EventsWaitable
  rcl_wait_set_t wait_set = rcl_get_zero_initialized_wait_set();
  EXPECT_THROW(notifier->add_to_wait_set(&wait_set), std::runtime_error);
  EXPECT_THROW(notifier->is_ready(&wait_set), std::runtime_error);

  {
    auto mock = mocking_utils::patch_and_return(
      "lib:rclcpp", rcl_guard_condition_set_listener_callback, RCL_RET_ERROR);
    EXPECT_THROW(std::make_shared<EventsExecutor>(), std::runtime_error);
  }
}

TEST_F(TestEventsExecutor, run_clients_servers)
{
  auto node = std::make_shared<rclcpp::Node>("node");

  bool request_received = false;
  bool response_received = false;
  auto service =
    node->create_service<test_msgs::srv::Empty>(
    "service",
    [&request_received](
      const test_msgs::srv::Empty::Request::SharedPtr,
      test_msgs::srv::Empty::Response::SharedPtr)
    {
      request_received = true;
    });
  auto client = node->create_client<test_msgs::srv::Empty>("service");

  EventsExecutor executor;
  executor.add_node(node);

  bool spin_exited = false;
  std::thread spinner([&spin_exited, &executor, this]() {
      executor.spin();
      spin_exited = true;
    });

  auto request = std::make_shared<test_msgs::srv::Empty::Request>();
  client->async_send_request(
    request,
    [&response_received](rclcpp::Client<test_msgs::srv::Empty>::SharedFuture result_future) {
      (void)result_future;
      response_received = true;
    });

  // Wait some time for the client-server to be invoked
  auto start = std::chrono::steady_clock::now();
  while (
    !response_received &&
    !spin_exited &&
    (std::chrono::steady_clock::now() - start < 1s))
  {
    std::this_thread::sleep_for(5ms);
  }

  executor.cancel();
  spinner.join();
  executor.remove_node(node);

  EXPECT_TRUE(request_received);
  EXPECT_TRUE(response_received);
  EXPECT_TRUE(spin_exited);
}

TEST_F(TestEventsExecutor, spin_once_max_duration)
{
  {
    auto node = std::make_shared<rclcpp::Node>("node");

    size_t t_runs = 0;
    auto t = node->create_wall_timer(
      10s,
      [&]() {
        t_runs++;
      });

    EventsExecutor executor;
    executor.add_node(node);

    auto start = std::chrono::steady_clock::now();
    executor.spin_once(10ms);

    EXPECT_EQ(0u, t_runs);
    EXPECT_TRUE(std::chrono::steady_clock::now() - start < 200ms);
  }

  {
    auto node = std::make_shared<rclcpp::Node>("node");

    size_t t_runs = 0;
    auto t = node->create_wall_timer(
      10ms,
      [&]() {
        t_runs++;
      });

    EventsExecutor executor;
    executor.add_node(node);

    auto start = std::chrono::steady_clock::now();
    executor.spin_once(10s);

    EXPECT_EQ(1u, t_runs);
    EXPECT_TRUE(std::chrono::steady_clock::now() - start < 200ms);
  }
}

TEST_F(TestEventsExecutor, spin_some_max_duration)
{
  {
    auto node = std::make_shared<rclcpp::Node>("node");

    size_t t_runs = 0;
    auto t = node->create_wall_timer(
      10s,
      [&]() {
        t_runs++;
      });

    EventsExecutor executor;
    executor.add_node(node);

    auto start = std::chrono::steady_clock::now();
    executor.spin_some(10ms);

    EXPECT_EQ(0u, t_runs);
    EXPECT_TRUE(std::chrono::steady_clock::now() - start < 200ms);
  }

  {
    auto node = std::make_shared<rclcpp::Node>("node");

    size_t t_runs = 0;
    auto t = node->create_wall_timer(
      10ms,
      [&]() {
        t_runs++;
      });

    // Sleep some time for the timer to be ready when spin
    std::this_thread::sleep_for(10ms);

    EventsExecutor executor;
    executor.add_node(node);

    auto start = std::chrono::steady_clock::now();
    executor.spin_some(10s);

    EXPECT_EQ(1u, t_runs);
    EXPECT_TRUE(std::chrono::steady_clock::now() - start < 200ms);
  }
}

TEST_F(TestEventsExecutor, spin_some_zero_duration)
{
  auto node = std::make_shared<rclcpp::Node>("node");

  size_t t_runs = 0;
  auto t = node->create_wall_timer(
    20ms,
    [&]() {
      t_runs++;
    });

  // Sleep some time for the timer to be ready when spin
  std::this_thread::sleep_for(20ms);

  EventsExecutor executor;
  executor.add_node(node);
  executor.spin_some(0ms);

  EXPECT_EQ(1u, t_runs);
}

TEST_F(TestEventsExecutor, spin_all_max_duration)
{
  {
    auto node = std::make_shared<rclcpp::Node>("node");

    size_t t_runs = 0;
    auto t = node->create_wall_timer(
      10s,
      [&]() {
        t_runs++;
      });

    EventsExecutor executor;
    executor.add_node(node);

    auto start = std::chrono::steady_clock::now();
    executor.spin_all(10ms);

    EXPECT_EQ(0u, t_runs);
    EXPECT_TRUE(std::chrono::steady_clock::now() - start < 200ms);
  }

  {
    auto node = std::make_shared<rclcpp::Node>("node");

    size_t t_runs = 0;
    auto t = node->create_wall_timer(
      10ms,
      [&]() {
        t_runs++;
      });

    // Sleep some time for the timer to be ready when spin
    std::this_thread::sleep_for(10ms);

    EventsExecutor executor;
    executor.add_node(node);

    auto start = std::chrono::steady_clock::now();
    executor.spin_all(10s);

    EXPECT_EQ(1u, t_runs);
    EXPECT_TRUE(std::chrono::steady_clock::now() - start < 200ms);
  }

  EventsExecutor executor;
  EXPECT_THROW(executor.spin_all(0ms), std::invalid_argument);
  EXPECT_THROW(executor.spin_all(-5ms), std::invalid_argument);
}

TEST_F(TestEventsExecutor, cancel_while_timers_running)
{
  auto node = std::make_shared<rclcpp::Node>("node");

  size_t t1_runs = 0;
  auto t1 = node->create_wall_timer(
    1ms,
    [&]() {
      t1_runs++;
      std::this_thread::sleep_for(25ms);
    });

  size_t t2_runs = 0;
  auto t2 = node->create_wall_timer(
    1ms,
    [&]() {
      t2_runs++;
      std::this_thread::sleep_for(25ms);
    });

  EventsExecutor executor;
  executor.add_node(node);

  std::thread spinner([&executor, this]() {executor.spin();});

  std::this_thread::sleep_for(10ms);
  // Call cancel while t1 callback is still being executed
  executor.cancel();
  spinner.join();

  // Depending on the latency on the system, t2 may start to execute before cancel is signaled
  EXPECT_GE(1u, t1_runs);
  EXPECT_GE(1u, t2_runs);
}

TEST_F(TestEventsExecutor, cancel_while_timers_waiting)
{
  auto node = std::make_shared<rclcpp::Node>("node");

  size_t t1_runs = 0;
  auto t1 = node->create_wall_timer(
    100s,
    [&]() {
      t1_runs++;
    });

  EventsExecutor executor;
  executor.add_node(node);

  auto start = std::chrono::steady_clock::now();
  std::thread spinner([&executor, this]() {executor.spin();});

  std::this_thread::sleep_for(10ms);
  executor.cancel();
  spinner.join();

  EXPECT_EQ(0u, t1_runs);
  EXPECT_TRUE(std::chrono::steady_clock::now() - start < 1s);
}

TEST_F(TestEventsExecutor, destroy_entities)
{
  // Create a publisher node and start publishing messages
  auto node_pub = std::make_shared<rclcpp::Node>("node_pub");
  auto publisher = node_pub->create_publisher<test_msgs::msg::Empty>("topic", rclcpp::QoS(10));
  auto timer = node_pub->create_wall_timer(
    2ms, [&]() {publisher->publish(std::make_unique<test_msgs::msg::Empty>());});
  EventsExecutor executor_pub;
  executor_pub.add_node(node_pub);
  std::thread spinner([&executor_pub, this]() {executor_pub.spin();});

  // Create a node with two different subscriptions to the topic
  auto node_sub = std::make_shared<rclcpp::Node>("node_sub");
  size_t callback_count_1 = 0;
  auto subscription_1 =
    node_sub->create_subscription<test_msgs::msg::Empty>(
    "topic", rclcpp::QoS(10), [&](test_msgs::msg::Empty::SharedPtr) {callback_count_1++;});
  size_t callback_count_2 = 0;
  auto subscription_2 =
    node_sub->create_subscription<test_msgs::msg::Empty>(
    "topic", rclcpp::QoS(10), [&](test_msgs::msg::Empty::SharedPtr) {callback_count_2++;});
  EventsExecutor executor_sub;
  executor_sub.add_node(node_sub);

  // Wait some time while messages are published
  std::this_thread::sleep_for(10ms);

  // Destroy one of the two subscriptions
  subscription_1.reset();

  // Let subscriptions executor spin
  executor_sub.spin_some(10ms);

  // The callback count of the destroyed subscription remained at 0
  EXPECT_EQ(0u, callback_count_1);
  EXPECT_LT(0u, callback_count_2);

  executor_pub.cancel();
  spinner.join();
}

/*
   Testing construction of a subscriptions with QoS event callback functions.
 */
std::string * g_pub_log_msg;
std::string * g_sub_log_msg;
std::promise<void> * g_log_msgs_promise;
TEST_F(TestEventsExecutor, test_default_incompatible_qos_callbacks)
{
  auto node = std::make_shared<rclcpp::Node>("node");
  rcutils_logging_output_handler_t original_output_handler = rcutils_logging_get_output_handler();

  std::string pub_log_msg;
  std::string sub_log_msg;
  std::promise<void> log_msgs_promise;
  g_pub_log_msg = &pub_log_msg;
  g_sub_log_msg = &sub_log_msg;
  g_log_msgs_promise = &log_msgs_promise;
  auto logger_callback = [](
    const rcutils_log_location_t * /*location*/,
    int /*level*/, const char * /*name*/, rcutils_time_point_value_t /*timestamp*/,
    const char * format, va_list * args) -> void {
      char buffer[1024];
      vsnprintf(buffer, sizeof(buffer), format, *args);
      const std::string msg = buffer;
      if (msg.rfind("New subscription discovered on topic '/test_topic'", 0) == 0) {
        *g_pub_log_msg = buffer;
      } else if (msg.rfind("New publisher discovered on topic '/test_topic'", 0) == 0) {
        *g_sub_log_msg = buffer;
      }

      if (!g_pub_log_msg->empty() && !g_sub_log_msg->empty()) {
        g_log_msgs_promise->set_value();
      }
    };
  rcutils_logging_set_output_handler(logger_callback);

  std::shared_future<void> log_msgs_future = log_msgs_promise.get_future();

  rclcpp::QoS qos_profile_publisher(10);
  qos_profile_publisher.durability(RMW_QOS_POLICY_DURABILITY_VOLATILE);
  auto publisher = node->create_publisher<test_msgs::msg::Empty>(
    "test_topic", qos_profile_publisher);

  rclcpp::QoS qos_profile_subscription(10);
  qos_profile_subscription.durability(RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL);
  auto subscription = node->create_subscription<test_msgs::msg::Empty>(
    "test_topic", qos_profile_subscription, [&](test_msgs::msg::Empty::SharedPtr) {});

  rclcpp::executors::EventsExecutor ex;
  ex.add_node(node->get_node_base_interface());

  // This future won't complete on fastrtps, so just timeout immediately
  bool is_fastrtps =
    std::string(rmw_get_implementation_identifier()).find("rmw_fastrtps") != std::string::npos;
  const auto timeout = (is_fastrtps) ? std::chrono::milliseconds(5) : std::chrono::seconds(10);
  ex.spin_until_future_complete(log_msgs_future, timeout);

  if (is_fastrtps) {
    EXPECT_EQ("", pub_log_msg);
    EXPECT_EQ("", sub_log_msg);
  } else {
    EXPECT_EQ(
      "New subscription discovered on topic '/test_topic', requesting incompatible QoS. "
      "No messages will be sent to it. Last incompatible policy: DURABILITY_QOS_POLICY",
      pub_log_msg);
    EXPECT_EQ(
      "New publisher discovered on topic '/test_topic', offering incompatible QoS. "
      "No messages will be sent to it. Last incompatible policy: DURABILITY_QOS_POLICY",
      sub_log_msg);
  }

  rcutils_logging_set_output_handler(original_output_handler);
}
