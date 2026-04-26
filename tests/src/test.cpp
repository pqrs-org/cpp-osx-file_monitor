#include <boost/ut.hpp>
#include <condition_variable>
#include <mutex>
#include <pqrs/osx/file_monitor.hpp>

namespace {
std::string file_path_1_1 = "target/sub1/file1_1";
std::string file_path_1_2 = "target/sub1/file1_2";
std::string file_path_2_1 = "../src/target//sub2/file2_1";

class test_file_monitor final {
public:
  struct snapshot final {
    size_t count;
    size_t signal_count;
    std::optional<std::string> last_file_path;
    std::optional<std::string> last_file_body1_1;
    std::optional<std::string> last_file_body1_2;
    std::optional<std::string> last_file_body2_1;
    std::optional<pqrs::osx::file_monitor::availability> last_availability1_1;
    std::optional<pqrs::osx::file_monitor::availability> last_availability1_2;
    std::optional<pqrs::osx::file_monitor::availability> last_availability2_1;
  };

  test_file_monitor() {
    time_source_ = std::make_shared<pqrs::dispatcher::hardware_time_source>();
    dispatcher_ = std::make_shared<pqrs::dispatcher::dispatcher>(time_source_);

    const auto targets = std::vector<std::string>{
        file_path_1_1,
        file_path_1_2,
        file_path_2_1,
    };

    file_monitor_ = std::make_unique<pqrs::osx::file_monitor>(dispatcher_,
                                                              targets);

    file_monitor_->file_changed.connect([&](auto&& changed_file_path,
                                            auto&& changed_file_body) {
      const auto to_optional_string = [](const auto& body) -> std::optional<std::string> {
        if (!body) {
          return std::nullopt;
        }
        return std::string(std::begin(*body), std::end(*body));
      };

      if (!file_monitor_thread_id_) {
        file_monitor_thread_id_ = std::this_thread::get_id();
      }
      if (file_monitor_thread_id_ != std::this_thread::get_id()) {
        throw std::logic_error("thread id mismatch");
      }

      {
        std::lock_guard<std::mutex> lock(mutex_);

        ++signal_count_;
        ++count_;
        last_file_path_ = changed_file_path;

        if (changed_file_path == file_path_1_1) {
          last_file_body1_1_ = to_optional_string(changed_file_body);
        }
        if (changed_file_path == file_path_1_2) {
          last_file_body1_2_ = to_optional_string(changed_file_body);
        }
        if (changed_file_path == file_path_2_1) {
          last_file_body2_1_ = to_optional_string(changed_file_body);
        }
      }

      condition_variable_.notify_all();
    });

    file_monitor_->watched_file_availability_changed.connect([&](const auto& watched_file,
                                                                 auto availability) {
      // std::cout << "watched_file_availability_changed: " << watched_file << " " << availability << std::endl;

      {
        std::lock_guard<std::mutex> lock(mutex_);

        ++signal_count_;
        if (watched_file == file_path_1_1) {
          last_availability1_1_ = availability;
        }
        if (watched_file == file_path_1_2) {
          last_availability1_2_ = availability;
        }
        if (watched_file == file_path_2_1) {
          last_availability2_1_ = availability;
        }
      }

      condition_variable_.notify_all();
    });

    file_monitor_->async_start();

    wait_until_ready();
    wait_until([](const auto& state) {
      return state.count >= 3;
    });
  }

  ~test_file_monitor() {
    dispatcher_->terminate();
    dispatcher_ = nullptr;
  }

  size_t get_count() const {
    return get_snapshot().count;
  }

  std::optional<std::string> get_last_file_body1_1() const {
    return get_snapshot().last_file_body1_1;
  }

  std::optional<std::string> get_last_file_body1_2() const {
    return get_snapshot().last_file_body1_2;
  }

  std::optional<std::string> get_last_file_body2_1() const {
    return get_snapshot().last_file_body2_1;
  }

  std::optional<pqrs::osx::file_monitor::availability> get_last_availability1_1() const {
    return get_snapshot().last_availability1_1;
  }

  std::optional<pqrs::osx::file_monitor::availability> get_last_availability1_2() const {
    return get_snapshot().last_availability1_2;
  }

  std::optional<pqrs::osx::file_monitor::availability> get_last_availability2_1() const {
    return get_snapshot().last_availability2_1;
  }

  snapshot get_snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);

    return {
        count_,
        signal_count_,
        last_file_path_,
        last_file_body1_1_,
        last_file_body1_2_,
        last_file_body2_1_,
        last_availability1_1_,
        last_availability1_2_,
        last_availability2_1_,
    };
  }

  void clear_results() {
    std::lock_guard<std::mutex> lock(mutex_);

    count_ = 0;
    last_file_path_ = std::nullopt;
    last_file_body1_1_ = std::nullopt;
    last_file_body1_2_ = std::nullopt;
    last_file_body2_1_ = std::nullopt;
    last_availability1_1_ = std::nullopt;
    last_availability1_2_ = std::nullopt;
    last_availability2_1_ = std::nullopt;
  }

  template <typename Predicate>
  bool wait_until(Predicate&& predicate,
                  std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) {
    std::unique_lock<std::mutex> lock(mutex_);
    return condition_variable_.wait_for(lock,
                                        timeout,
                                        [&] {
                                          return predicate(snapshot{
                                              count_,
                                              signal_count_,
                                              last_file_path_,
                                              last_file_body1_1_,
                                              last_file_body1_2_,
                                              last_file_body2_1_,
                                              last_availability1_1_,
                                              last_availability1_2_,
                                              last_availability2_1_,
                                          });
                                        });
  }

  void enqueue_file_changed(const std::string& file_path) {
    file_monitor_->enqueue_file_changed(file_path);
  }

  size_t get_signal_count() const {
    return get_snapshot().signal_count;
  }

  bool wait_until_enqueued_file_body1_1(const std::string& file_path,
                                        const std::optional<std::string>& expected_body,
                                        std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) {
    auto deadline = std::chrono::steady_clock::now() + timeout;

    while (std::chrono::steady_clock::now() < deadline) {
      clear_results();
      enqueue_file_changed(file_path);

      if (wait_until([&](const auto& state) {
            return state.count >= 1 &&
                   state.last_file_body1_1 == expected_body &&
                   state.last_file_body1_2 == std::nullopt &&
                   state.last_file_body2_1 == std::nullopt;
          },
                     std::chrono::milliseconds(100))) {
        return true;
      }
    }

    return false;
  }

  bool wait_until_ready(std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) const {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
      if (file_monitor_->ready()) {
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return file_monitor_->ready();
  }

  bool wait_for_no_signal(size_t previous_signal_count,
                          std::chrono::milliseconds timeout = std::chrono::milliseconds(500)) {
    std::unique_lock<std::mutex> lock(mutex_);
    return !condition_variable_.wait_for(lock,
                                         timeout,
                                         [&] {
                                           return signal_count_ != previous_signal_count;
                                         });
  }

private:
  std::shared_ptr<pqrs::dispatcher::hardware_time_source> time_source_;
  std::shared_ptr<pqrs::dispatcher::dispatcher> dispatcher_;
  std::unique_ptr<pqrs::osx::file_monitor> file_monitor_;
  std::optional<std::thread::id> file_monitor_thread_id_;
  mutable std::mutex mutex_;
  std::condition_variable condition_variable_;
  size_t count_ = 0;
  size_t signal_count_ = 0;
  std::optional<std::string> last_file_path_;
  std::optional<std::string> last_file_body1_1_;
  std::optional<std::string> last_file_body1_2_;
  std::optional<std::string> last_file_body2_1_;
  std::optional<pqrs::osx::file_monitor::availability> last_availability1_1_;
  std::optional<pqrs::osx::file_monitor::availability> last_availability1_2_;
  std::optional<pqrs::osx::file_monitor::availability> last_availability2_1_;
};
} // namespace

int main() {
  using namespace boost::ut;
  using namespace boost::ut::literals;

  const auto expect_state = [](const test_file_monitor::snapshot& state,
                               size_t minimum_count,
                               std::optional<std::string> file_body1_1,
                               std::optional<std::string> file_body1_2,
                               std::optional<std::string> file_body2_1) {
    return state.count >= minimum_count &&
           state.last_file_body1_1 == file_body1_1 &&
           state.last_file_body1_2 == file_body1_2 &&
           state.last_file_body2_1 == file_body2_1;
  };

  "file_monitor"_test = [expect_state] {
    using namespace std::string_literals;

    {
      std::cout << __LINE__ << " " << std::flush;

      system("rm -rf target");
      system("mkdir -p target/sub1");
      system("mkdir -p target/sub2");
      system("/bin/echo -n 1_1_0 > target/sub1/file1_1");
      system("/bin/echo -n 1_2_0 > target/sub1/file1_2");

      test_file_monitor monitor;

      expect(monitor.get_count() >= 3);
      expect(monitor.get_last_file_body1_1() == "1_1_0"s);
      expect(monitor.get_last_file_body1_2() == "1_2_0"s);
      expect(monitor.get_last_file_body2_1() == std::nullopt);

      //
      // File rename after startup
      //

      std::cout << __LINE__ << " " << std::flush;

      monitor.clear_results();

      system("mv target/sub1/file1_1 target/sub1/file1_1.bak");

      expect(monitor.wait_until([&](const auto& state) {
        return state.last_availability1_1 == pqrs::osx::file_monitor::availability::unavailable;
      }));

      expect(monitor.get_last_availability1_1() == pqrs::osx::file_monitor::availability::unavailable);
      expect(monitor.get_last_file_body1_1() == std::nullopt);

      monitor.clear_results();

      system("mv target/sub1/file1_1.bak target/sub1/file1_1");

      expect(monitor.wait_until([&](const auto& state) {
        return state.last_availability1_1 == pqrs::osx::file_monitor::availability::available &&
               state.last_file_body1_1 == "1_1_0"s;
      }));

      expect(monitor.get_last_availability1_1() == pqrs::osx::file_monitor::availability::available);
      expect(monitor.get_last_file_body1_1() == "1_1_0"s);

      //
      // Generic file modification (update file1_1)
      //

      std::cout << __LINE__ << " " << std::flush;

      monitor.clear_results();

      system("/bin/echo -n 1_1_1 > target/sub1/file1_1");

      expect(monitor.wait_until([&](const auto& state) {
        return expect_state(state, 1, "1_1_1"s, std::nullopt, std::nullopt);
      }));

      expect(monitor.get_count() >= 1);
      expect(monitor.get_last_file_body1_1() == "1_1_1"s);
      expect(monitor.get_last_file_body1_2() == std::nullopt);
      expect(monitor.get_last_file_body2_1() == std::nullopt);

      //
      // Generic file modification (update file1_1 again)
      //

      std::cout << __LINE__ << " " << std::flush;

      monitor.clear_results();

      system("/bin/echo -n 1_1_2 > target/sub1/file1_1");

      expect(monitor.wait_until([&](const auto& state) {
        return expect_state(state, 1, "1_1_2"s, std::nullopt, std::nullopt);
      }));

      expect(monitor.get_count() >= 1);
      expect(monitor.get_last_file_body1_1() == "1_1_2"s);
      expect(monitor.get_last_file_body1_2() == std::nullopt);
      expect(monitor.get_last_file_body2_1() == std::nullopt);

      //
      // Generic file modification (update file1_2)
      //

      std::cout << __LINE__ << " " << std::flush;

      monitor.clear_results();

      system("/bin/echo -n 1_2_1 > target/sub1/file1_2");

      expect(monitor.wait_until([&](const auto& state) {
        return expect_state(state, 1, std::nullopt, "1_2_1"s, std::nullopt);
      }));

      expect(monitor.get_count() >= 1);
      expect(monitor.get_last_file_body1_1() == std::nullopt);
      expect(monitor.get_last_file_body1_2() == "1_2_1"s);
      expect(monitor.get_last_file_body2_1() == std::nullopt);

      //
      // Generic file modification (update file1_2 again)
      //

      std::cout << __LINE__ << " " << std::flush;

      monitor.clear_results();

      system("/bin/echo -n 1_2_2 > target/sub1/file1_2");

      expect(monitor.wait_until([&](const auto& state) {
        return expect_state(state, 1, std::nullopt, "1_2_2"s, std::nullopt);
      }));

      expect(monitor.get_count() >= 1);
      expect(monitor.get_last_file_body1_1() == std::nullopt);
      expect(monitor.get_last_file_body1_2() == "1_2_2"s);
      expect(monitor.get_last_file_body2_1() == std::nullopt);

      //
      // Generic file modification (update file1_1 again)
      //

      std::cout << __LINE__ << " " << std::flush;

      monitor.clear_results();

      system("/bin/echo -n 1_1_3 > target/sub1/file1_1");

      expect(monitor.wait_until([&](const auto& state) {
        return expect_state(state, 1, "1_1_3"s, std::nullopt, std::nullopt);
      }));

      expect(monitor.get_count() >= 1);
      expect(monitor.get_last_file_body1_1() == "1_1_3"s);
      expect(monitor.get_last_file_body1_2() == std::nullopt);
      expect(monitor.get_last_file_body2_1() == std::nullopt);

      //
      // Generic file modification (update file2_1)
      //

      std::cout << __LINE__ << " " << std::flush;

      monitor.clear_results();

      system("/bin/echo -n 2_1_1 > target/sub2/file2_1");

      expect(monitor.wait_until([&](const auto& state) {
        return expect_state(state, 1, std::nullopt, std::nullopt, "2_1_1"s);
      }));

      expect(monitor.get_count() >= 1);
      expect(monitor.get_last_file_body1_1() == std::nullopt);
      expect(monitor.get_last_file_body1_2() == std::nullopt);
      expect(monitor.get_last_file_body2_1() == "2_1_1"s);

      //
      // File removal
      //

      std::cout << __LINE__ << " " << std::flush;

      monitor.clear_results();

      system("rm target/sub1/file1_2");

      expect(monitor.wait_until([&](const auto& state) {
        return expect_state(state, 1, std::nullopt, std::nullopt, std::nullopt) &&
               state.last_availability1_2 == pqrs::osx::file_monitor::availability::unavailable;
      }));

      expect(monitor.get_count() >= 1);
      expect(monitor.get_last_availability1_2() == pqrs::osx::file_monitor::availability::unavailable);
      expect(monitor.get_last_file_body1_1() == std::nullopt);
      expect(monitor.get_last_file_body1_2() == std::nullopt);
      expect(monitor.get_last_file_body2_1() == std::nullopt);

      //
      // File removal
      //

      std::cout << __LINE__ << " " << std::flush;

      monitor.clear_results();

      system("rm target/sub2/file2_1");

      expect(monitor.wait_until([&](const auto& state) {
        return expect_state(state, 1, std::nullopt, std::nullopt, std::nullopt) &&
               state.last_availability2_1 == pqrs::osx::file_monitor::availability::unavailable;
      }));

      expect(monitor.get_count() >= 1);
      expect(monitor.get_last_file_body1_1() == std::nullopt);
      expect(monitor.get_last_file_body1_2() == std::nullopt);
      expect(monitor.get_last_file_body2_1() == std::nullopt);

      //
      // Directory removal
      //

      std::cout << __LINE__ << " " << std::flush;

      monitor.clear_results();
      system("rm -rf target");

      expect(monitor.wait_until([&](const auto& state) {
        return state.last_availability1_1 == pqrs::osx::file_monitor::availability::unavailable;
      }));
      expect(monitor.wait_until_ready());

      expect(monitor.get_last_availability1_1() == pqrs::osx::file_monitor::availability::unavailable);
      expect(monitor.get_last_file_body1_1() == std::nullopt);
      expect(monitor.get_last_file_body1_2() == std::nullopt);
      expect(monitor.get_last_file_body2_1() == std::nullopt);

      //
      // Generic file modification
      //

      std::cout << __LINE__ << " " << std::flush;

      monitor.clear_results();

      system("mkdir -p target/sub1");
      system("/bin/echo -n 1_1_4 > target/sub1/file1_1");

      expect(monitor.wait_until([&](const auto& state) {
        return state.count >= 1 &&
               state.last_file_body1_1 == "1_1_4"s &&
               state.last_file_body1_2 == std::nullopt &&
               state.last_file_body2_1 == std::nullopt;
      }));

      expect(monitor.get_count() >= 1);
      expect(monitor.get_last_file_body1_1() == "1_1_4"s);
      expect(monitor.get_last_file_body1_2() == std::nullopt);
      expect(monitor.get_last_file_body2_1() == std::nullopt);

      //
      // Move file
      //

      std::cout << __LINE__ << " " << std::flush;

      monitor.clear_results();

      system("/bin/echo -n 1_1_5 > target/sub1/file1_1.new");
      system("mv target/sub1/file1_1.new target/sub1/file1_1");

      expect(monitor.wait_until([&](const auto& state) {
        return expect_state(state, 1, "1_1_5"s, std::nullopt, std::nullopt);
      }));

      expect(monitor.get_count() >= 1);
      expect(monitor.get_last_file_body1_1() == "1_1_5"s);
      expect(monitor.get_last_file_body1_2() == std::nullopt);
      expect(monitor.get_last_file_body2_1() == std::nullopt);

      //
      // Move directory
      //

      std::cout << __LINE__ << " " << std::flush;

      monitor.clear_results();
      system("rm -rf target");

      expect(monitor.wait_until([&](const auto& state) {
        return state.last_availability1_1 == pqrs::osx::file_monitor::availability::unavailable;
      }));
      expect(monitor.wait_until_ready());

      system("mkdir -p target.new/sub1");
      system("/bin/echo -n 1_1_6 > target.new/sub1/file1_1");
      system("mv target.new target");

      expect(monitor.wait_until([&](const auto& state) {
        return state.count >= 2 &&
               state.last_file_body1_1 == "1_1_6"s &&
               state.last_file_body1_2 == std::nullopt &&
               state.last_file_body2_1 == std::nullopt &&
               state.last_availability1_1 == pqrs::osx::file_monitor::availability::available;
      }));

      expect(monitor.get_count() >= 2);
      expect(monitor.get_last_file_body1_1() == "1_1_6"s);
      expect(monitor.get_last_file_body1_2() == std::nullopt);
      expect(monitor.get_last_file_body2_1() == std::nullopt);

      //
      // Ignore own process
      //

      std::cout << __LINE__ << " " << std::flush;

      monitor.clear_results();
      auto signal_count = monitor.get_signal_count();

      std::ofstream(file_path_1_1) << "1_1_7";

      expect(monitor.wait_for_no_signal(signal_count));
      expect(monitor.get_count() == 0);
      expect(monitor.get_last_file_body1_1() == std::nullopt);
      expect(monitor.get_last_file_body1_2() == std::nullopt);
      expect(monitor.get_last_file_body2_1() == std::nullopt);

      //
      // enqueue_file_changed
      //

      std::cout << __LINE__ << " " << std::flush;

      monitor.clear_results();

      monitor.enqueue_file_changed(file_path_1_1);

      expect(monitor.wait_until([&](const auto& state) {
        return expect_state(state, 1, "1_1_7"s, std::nullopt, std::nullopt);
      }));

      expect(monitor.get_count() >= 1);
      expect(monitor.get_last_file_body1_1() == "1_1_7"s);
      expect(monitor.get_last_file_body1_2() == std::nullopt);
      expect(monitor.get_last_file_body2_1() == std::nullopt);

      //
      // Remove target directory
      //

      std::cout << __LINE__ << " " << std::flush;

      system("rm -rf target");

      expect(monitor.wait_until([&](const auto& state) {
        return state.last_availability1_1 == pqrs::osx::file_monitor::availability::unavailable;
      }));
      expect(monitor.wait_until_ready());
    }

    {
      //
      // Create test_file_monitor when any target files do not exist.
      //

      test_file_monitor monitor;

      expect(monitor.wait_until([&](const auto& state) {
        return state.count >= 3 &&
               state.last_file_body1_1 == std::nullopt &&
               state.last_file_body1_2 == std::nullopt &&
               state.last_file_body2_1 == std::nullopt;
      }));

      //
      // Generic file modification
      //

      std::cout << __LINE__ << " " << std::flush;

      monitor.clear_results();

      system("mkdir -p target/sub1");
      system(": > target/sub1/file1_1");

      expect(monitor.wait_until([&](const auto& state) {
        return state.last_availability1_1 == pqrs::osx::file_monitor::availability::available;
      }));

      monitor.clear_results();

      system("/bin/echo -n 1_1_0 > target/sub1/file1_1");

      expect(monitor.wait_until([&](const auto& state) {
        return state.count >= 1 &&
               state.last_file_body1_1 == "1_1_0"s &&
               state.last_file_body1_2 == std::nullopt &&
               state.last_file_body2_1 == std::nullopt;
      }));

      expect(monitor.get_count() >= 1);
      expect(monitor.get_last_file_body1_1() == "1_1_0"s);
      expect(monitor.get_last_file_body1_2() == std::nullopt);
      expect(monitor.get_last_file_body2_1() == std::nullopt);
    }

    {
      //
      // Update file after self update.
      //

      std::cout << __LINE__ << " " << std::flush;

      system("rm -rf target");
      system("mkdir -p target/sub1");
      system("mkdir -p target/sub2");
      system("/bin/echo -n 1_1_0 > target/sub1/file1_1");
      system("/bin/echo -n 1_2_0 > target/sub1/file1_2");

      test_file_monitor monitor;

      monitor.clear_results();

      std::ofstream(file_path_1_1) << "1_1_1";

      expect(monitor.get_count() == 0);

      expect(monitor.wait_until_enqueued_file_body1_1(file_path_1_1, "1_1_1"s));

      monitor.clear_results();

      system("/bin/echo -n 1_1_0 > target/sub1/file1_1");

      expect(monitor.wait_until([&](const auto& state) {
        return expect_state(state, 1, "1_1_0"s, std::nullopt, std::nullopt);
      }));

      expect(monitor.get_count() >= 1);
      expect(monitor.get_last_file_body1_1() == "1_1_0"s);
      expect(monitor.get_last_file_body1_2() == std::nullopt);
      expect(monitor.get_last_file_body2_1() == std::nullopt);
    }

    std::cout << std::endl;
  };

  "read_file"_test = [] {
    {
      auto buffer = pqrs::osx::file_monitor::read_file("data/not_found");

      expect(buffer.get() == nullptr);
    }

    {
      system("mkdir -p target");
      system(": > target/empty");

      auto buffer = pqrs::osx::file_monitor::read_file("target/empty");

      expect(buffer.get() != nullptr);
      expect(buffer->empty());
    }

    {
      auto buffer = pqrs::osx::file_monitor::read_file("data/app.icns");

      expect(buffer.get() != nullptr);
      expect(buffer->size() == 225321);
      expect((*buffer)[0] == 0x69);
      expect((*buffer)[1] == 0x63);
      expect((*buffer)[2] == 0x6e);
      expect((*buffer)[3] == 0x73);
      expect((*buffer)[4] == 0x00);
      expect((*buffer)[5] == 0x03);
    }
  };

  "destruction_after_dispatcher_termination"_test = [] {
    auto time_source = std::make_shared<pqrs::dispatcher::hardware_time_source>();
    auto dispatcher = std::make_shared<pqrs::dispatcher::dispatcher>(time_source);

    auto monitor = std::make_unique<pqrs::osx::file_monitor>(
        dispatcher,
        std::vector<std::string>{"target/destruction_test"});

    monitor->async_start();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    dispatcher->terminate();
    dispatcher = nullptr;

    monitor = nullptr;

    expect(true);
  };

  return 0;
}
