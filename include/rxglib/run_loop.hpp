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
    using Args = std::pair<run_loop*, clock_type::time_point>;
    auto args = new Args{this, wakeup_point};
    g_main_context_invoke(
        context_,
        [](gpointer data) -> gboolean {
          auto args = static_cast<Args*>(data);
          auto this_ptr = args->first;
          const auto& wakeup_point = args->second;
          if (wakeup_point < this_ptr->timeout_point_ ||
              this_ptr->timeout_point_ < clock_type::now()) {
            this_ptr->reset_timeout_source(wakeup_point);
          }
          delete args;
          return false;
        },
        args);
  }

  void remove_glib_timeout_source() {
    if (timeout_source_ != nullptr) {
      g_source_destroy(timeout_source_);
      g_source_unref(timeout_source_);
      timeout_source_ = nullptr;
      timeout_point_ = clock_type::time_point::min();
    }
  }

  void reset_timeout_source(const clock_type::time_point& wakeup_point) {
    assert(g_main_context_is_owner(context_) == TRUE);

    // Discard last timeout source.
    remove_glib_timeout_source();

    auto millisec = static_cast<guint>(
        std::max(std::chrono::duration_cast<std::chrono::milliseconds>(
                     clock_type::now() - wakeup_point),
                 std::chrono::milliseconds::zero())
            .count());
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
    assert(g_main_context_is_owner(context_) == TRUE);

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

  GMainLoop* loop_;
  GMainContext* context_;

  GSource* timeout_source_;
  clock_type::time_point timeout_point_;
};

}  // namespace rxglib
