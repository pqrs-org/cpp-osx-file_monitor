#include <csignal>
#include <iostream>
#include <pqrs/environment_variable.hpp>
#include <pqrs/osx/file_monitor.hpp>

namespace {
auto global_wait = pqrs::make_thread_wait();
}

int main() {
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

    file_monitor->watched_file_availability_changed.connect([](auto&& watched_file, auto availability) {
      std::cout << "watched_file_availability_changed: " << watched_file << " ";
      switch (availability) {
        case pqrs::osx::file_monitor::availability::unavailable:
          std::cout << "unavailable";
          break;
        case pqrs::osx::file_monitor::availability::available:
          std::cout << "available";
          break;
      }
      std::cout << std::endl;
    });

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
