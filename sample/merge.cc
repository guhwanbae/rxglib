#include <chrono>

#include "console.hpp"
#include "rxcpp/rx.hpp"
#include "rxglib.hpp"

int main() {
  rxglib::console::out("main\n");

  auto loop = g_main_loop_new(nullptr, false);
  auto context = g_main_context_default();
  rxglib::run_loop rl{loop, context};

  auto main_thread = rxcpp::observe_on_run_loop(rl.get_rx_run_loop());
  auto worker_threads = rxcpp::synchronize_new_thread();

  auto source1 = rxcpp::observable<>::interval(std::chrono::milliseconds(10))
                     .subscribe_on(worker_threads)
                     .map([](auto n) {
                       rxglib::console::out("1-map: {} -> {}\n", n, n * 2);
                       return n * 2;
                     });

  auto source2 = rxcpp::observable<>::interval(std::chrono::milliseconds(20))
                     .subscribe_on(worker_threads)
                     .map([](auto n) {
                       rxglib::console::out("2-map: {} -> {}\n", n, -n);
                       return -n;
                     });

  source1.merge(source2)
      .take(10)
      .observe_on(main_thread)
      .subscribe([](auto n) { rxglib::console::out("on_next: {}\n", n); },
                 [loop]() {
                   rxglib::console::out("on_complete\n");
                   rxglib::console::out("quit glib event loop\n");
                   g_main_loop_quit(loop);
                 });

  rxglib::console::out("run glib event loop\n");
  g_main_loop_run(loop);
  g_main_loop_unref(loop);

  return 0;
}