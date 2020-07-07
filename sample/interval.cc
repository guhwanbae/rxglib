#include <chrono>

#include "console.hpp"
#include "rxcpp/rx.hpp"
#include "rxglib.hpp"

int main() {
  rxglib::console::out("main\n");

  auto loop = g_main_loop_new(nullptr, false);
  auto context = g_main_context_default();

  rxglib::run_loop rl{loop, context};

  auto source = rxcpp::observable<>::interval(std::chrono::milliseconds(10))
                    .take(10)
                    .filter([](auto n) {
                      rxglib::console::out("filter: {} % 2 = {}\n", n, n % 2);
                      return n % 2 == 0;
                    })
                    .map([](auto n) {
                      rxglib::console::out("map: {} -> {}\n", n, n * 2);
                      return n * 2;
                    });

  source.subscribe([](auto n) { rxglib::console::out("on_next: {}\n", n); },
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