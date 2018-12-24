#include <csignal>
#include <iostream>
#include <pqrs/environment_variable.hpp>
#include <pqrs/osx/file_monitor.hpp>

namespace {
auto global_wait = pqrs::make_thread_wait();
}

int main(void) {
  std::signal(SIGINT, [](int) {
    global_wait->notify();
  });

  auto time_source = std::make_shared<pqrs::dispatcher::hardware_time_source>();
  auto dispatcher = std::make_shared<pqrs::dispatcher::dispatcher>(time_source);

  if (auto home = pqrs::environment_variable::find("HOME")) {
    std::vector<std::string> target_files = {
        *home + "/.config/karabiner/karabiner.json",
    };

    auto file_monitor = std::make_shared<pqrs::osx::file_monitor>(dispatcher, target_files);

    file_monitor->file_changed.connect([](auto&& changed_file_path, auto&& changed_file_body) {
      std::cout << "file_changed: " << changed_file_path << std::endl;
    });

    file_monitor->error_occurred.connect([](auto&& message) {
      std::cerr << "error_occurred: " << message << std::endl;
    });

    file_monitor->async_start();

    // ============================================================

    global_wait->wait_notice();

    // ============================================================

    file_monitor = nullptr;
  }

  dispatcher->terminate();
  dispatcher = nullptr;

  return 0;
}
