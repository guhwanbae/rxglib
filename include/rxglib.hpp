#pragma once

#include <algorithm>
#include <cassert>
#include <chrono>
#include <thread>

#include "glib.h"
#include "rxcpp/rx.hpp"

namespace rxglib {

class run_loop {
 public:
  using clock_type = rxcpp::schedulers::run_loop::clock_type;

  run_loop(GMainLoop* loop, GMainContext* context)
      : rx_run_loop_{},
        owner_tid_{std::this_thread::get_id()},
        loop_{loop},
        context_{context},
        timeout_source_{nullptr},
        timeout_point_{} {
    // rxglib::run_loop should be constructed on the thread which runs
    // assocaited gmain-context.
    assert(loop_ != nullptr && context_ != nullptr);

    // Let rxcpp run_loop reschedule rx events on glib event loop.
    rx_run_loop_.set_notify_earlier_wakeup(
        [this](const clock_type::time_point& wakeup_point) {
          this->on_earlier_wakeup(wakeup_point);
        });
  }

  ~run_loop() {
    rx_run_loop_.set_notify_earlier_wakeup({});

    remove_glib_timeout_source();
  }

  run_loop(const run_loop&) = delete;
  run_loop& operator=(const run_loop&) = delete;

  run_loop(run_loop&&) = delete;
  run_loop& operator=(run_loop&&) = delete;

  inline const rxcpp::schedulers::run_loop& get_rx_run_loop() const noexcept {
    return rx_run_loop_;
  }

 private:
  void on_earlier_wakeup(const clock_type::time_point& wakeup_point) {
    if (owner_tid_ == std::this_thread::get_id()) {
      if (wakeup_point < timeout_point_ || timeout_point_ < clock_type::now()) {
        reset_timeout_source(wakeup_point);
      }
    } else {
      auto queued_task = g_idle_source_new();
      using Args = std::pair<run_loop*, clock_type::time_point>;
      auto args = new Args{this, wakeup_point};
      g_source_set_callback(
          queued_task,
          [](gpointer data) -> gboolean {
            auto args = static_cast<Args*>(data);
            auto this_ptr = args->first;
            const auto& wakeup_point = args->second;
            this_ptr->on_earlier_wakeup(wakeup_point);
            return false;
          },
          args,
          [](gpointer data) {
            auto args = static_cast<Args*>(data);
            delete args;
          });
      g_source_attach(queued_task, context_);
      g_source_unref(queued_task);
    }
  }

  void remove_glib_timeout_source() {
    assert(owner_tid_ == std::this_thread::get_id());

    if (timeout_source_ != nullptr) {
      g_source_destroy(timeout_source_);
      g_source_unref(timeout_source_);
      timeout_source_ = nullptr;
      timeout_point_ = clock_type::time_point::min();
    }
  }

  void reset_timeout_source(const clock_type::time_point& wakeup_point) {
    assert(owner_tid_ == std::this_thread::get_id());

    // Discard last timeout source.
    remove_glib_timeout_source();

    auto millisec = static_cast<guint>(
        std::max(std::chrono::duration_cast<std::chrono::milliseconds>(
                     clock_type::now() - wakeup_point)
                     .count(),
                 0LL));
    auto new_timeout_source = g_timeout_source_new(millisec);
    g_source_set_callback(
        new_timeout_source,
        [](gpointer data) -> gboolean {
          // Dispatch scheduled rx events.
          auto this_ptr = static_cast<run_loop*>(data);
          this_ptr->on_rx_event_scheduled();
          return false;
        },
        this, nullptr);
    g_source_attach(new_timeout_source, context_);
    timeout_source_ = new_timeout_source;
    timeout_point_ = wakeup_point;
  }

  void on_rx_event_scheduled() {
    assert(owner_tid_ == std::this_thread::get_id());

    while (!rx_run_loop_.empty() &&
           rx_run_loop_.peek().when < clock_type::now()) {
      rx_run_loop_.dispatch();
    }

    if (!rx_run_loop_.empty()) {
      const auto next_wakeup_point = rx_run_loop_.peek().when;
      reset_timeout_source(next_wakeup_point);
    }
  }

  rxcpp::schedulers::run_loop rx_run_loop_;
  std::thread::id owner_tid_;

  GMainLoop* loop_;
  GMainContext* context_;

  GSource* timeout_source_;
  clock_type::time_point timeout_point_;
};

}  // namespace rxglib