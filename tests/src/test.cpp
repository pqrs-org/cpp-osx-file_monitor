#include <boost/ut.hpp>
#include <pqrs/osx/file_monitor.hpp>

namespace {
std::string file_path_1_1 = "target/sub1/file1_1";
std::string file_path_1_2 = "target/sub1/file1_2";
std::string file_path_2_1 = "../src/target//sub2/file2_1";

class test_file_monitor final {
public:
  test_file_monitor(void) : count_(0) {
    time_source_ = std::make_shared<pqrs::dispatcher::hardware_time_source>();
    dispatcher_ = std::make_shared<pqrs::dispatcher::dispatcher>(time_source_);

    std::vector<std::string> targets({
        file_path_1_1,
        file_path_1_2,
        file_path_2_1,
    });

    file_monitor_ = std::make_unique<pqrs::osx::file_monitor>(dispatcher_,
                                                              targets);

    file_monitor_->file_changed.connect([&](auto&& changed_file_path,
                                            auto&& changed_file_body) {
      if (!file_monitor_thread_id_) {
        file_monitor_thread_id_ = std::this_thread::get_id();
      }
      if (file_monitor_thread_id_ != std::this_thread::get_id()) {
        throw std::logic_error("thread id mismatch");
      }

      ++count_;
      last_file_path_ = changed_file_path;

      if (changed_file_path == file_path_1_1) {
        if (changed_file_body) {
          last_file_body1_1_ = std::string(std::begin(*changed_file_body),
                                           std::end(*changed_file_body));
        } else {
          last_file_body1_1_ = std::nullopt;
        }
      }
      if (changed_file_path == file_path_1_2) {
        if (changed_file_body) {
          last_file_body1_2_ = std::string(std::begin(*changed_file_body),
                                           std::end(*changed_file_body));
        } else {
          last_file_body1_2_ = std::nullopt;
        }
      }
      if (changed_file_path == file_path_2_1) {
        if (changed_file_body) {
          last_file_body2_1_ = std::string(std::begin(*changed_file_body),
                                           std::end(*changed_file_body));
        } else {
          last_file_body2_1_ = std::nullopt;
        }
      }
    });

    file_monitor_->async_start();

    wait();
  }

  ~test_file_monitor(void) {
    dispatcher_->terminate();
    dispatcher_ = nullptr;
  }

  size_t get_count(void) const {
    return count_;
  }

  const std::optional<std::string>& get_last_file_path(void) const {
    return last_file_path_;
  }

  const std::optional<std::string>& get_last_file_body1_1(void) const {
    return last_file_body1_1_;
  }

  const std::optional<std::string>& get_last_file_body1_2(void) const {
    return last_file_body1_2_;
  }

  const std::optional<std::string>& get_last_file_body2_1(void) const {
    return last_file_body2_1_;
  }

  void clear_results(void) {
    count_ = 0;
    last_file_path_ = std::nullopt;
    last_file_body1_1_ = std::nullopt;
    last_file_body1_2_ = std::nullopt;
    last_file_body2_1_ = std::nullopt;
  }

  void wait(void) {
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }

  void enqueue_file_changed(const std::string& file_path) {
    file_monitor_->enqueue_file_changed(file_path);
  }

private:
  std::shared_ptr<pqrs::dispatcher::hardware_time_source> time_source_;
  std::shared_ptr<pqrs::dispatcher::dispatcher> dispatcher_;
  std::unique_ptr<pqrs::osx::file_monitor> file_monitor_;
  std::optional<std::thread::id> file_monitor_thread_id_;
  size_t count_;
  std::optional<std::string> last_file_path_;
  std::optional<std::string> last_file_body1_1_;
  std::optional<std::string> last_file_body1_2_;
  std::optional<std::string> last_file_body2_1_;
};
} // namespace

int main(void) {
  using namespace boost::ut;
  using namespace boost::ut::literals;

  "file_monitor"_test = [] {
    using namespace std::string_literals;

    {
      std::cout << "." << std::flush;

      system("rm -rf target");
      system("mkdir -p target/sub1");
      system("mkdir -p target/sub2");
      system("/bin/echo -n 1_1_0 > target/sub1/file1_1");
      system("/bin/echo -n 1_2_0 > target/sub1/file1_2");

      test_file_monitor monitor;

      expect(monitor.get_count() >= 3);
      expect(monitor.get_last_file_path() == file_path_2_1);
      expect(monitor.get_last_file_body1_1() == "1_1_0"s);
      expect(monitor.get_last_file_body1_2() == "1_2_0"s);
      expect(monitor.get_last_file_body2_1() == std::nullopt);

      // ========================================
      // Generic file modification (update file1_1)
      // ========================================

      std::cout << "." << std::flush;

      monitor.clear_results();

      system("/bin/echo -n 1_1_1 > target/sub1/file1_1");

      monitor.wait();

      expect(monitor.get_count() >= 1);
      expect(monitor.get_last_file_path() == file_path_1_1);
      expect(monitor.get_last_file_body1_1() == "1_1_1"s);
      expect(monitor.get_last_file_body1_2() == std::nullopt);
      expect(monitor.get_last_file_body2_1() == std::nullopt);

      // ========================================
      // Generic file modification (update file1_1 again)
      // ========================================

      std::cout << "." << std::flush;

      monitor.clear_results();

      system("/bin/echo -n 1_1_2 > target/sub1/file1_1");

      monitor.wait();

      expect(monitor.get_count() >= 1);
      expect(monitor.get_last_file_path() == file_path_1_1);
      expect(monitor.get_last_file_body1_1() == "1_1_2"s);
      expect(monitor.get_last_file_body1_2() == std::nullopt);
      expect(monitor.get_last_file_body2_1() == std::nullopt);

      // ========================================
      // Generic file modification (update file1_2)
      // ========================================

      std::cout << "." << std::flush;

      monitor.clear_results();

      system("/bin/echo -n 1_2_1 > target/sub1/file1_2");

      monitor.wait();

      expect(monitor.get_count() >= 1);
      expect(monitor.get_last_file_path() == file_path_1_2);
      expect(monitor.get_last_file_body1_1() == std::nullopt);
      expect(monitor.get_last_file_body1_2() == "1_2_1"s);
      expect(monitor.get_last_file_body2_1() == std::nullopt);

      // ========================================
      // Generic file modification (update file1_2 again)
      // ========================================

      std::cout << "." << std::flush;

      monitor.clear_results();

      system("/bin/echo -n 1_2_2 > target/sub1/file1_2");

      monitor.wait();

      expect(monitor.get_count() >= 1);
      expect(monitor.get_last_file_path() == file_path_1_2);
      expect(monitor.get_last_file_body1_1() == std::nullopt);
      expect(monitor.get_last_file_body1_2() == "1_2_2"s);
      expect(monitor.get_last_file_body2_1() == std::nullopt);

      // ========================================
      // Generic file modification (update file1_1 again)
      // ========================================

      std::cout << "." << std::flush;

      monitor.clear_results();

      system("/bin/echo -n 1_1_3 > target/sub1/file1_1");

      monitor.wait();

      expect(monitor.get_count() >= 1);
      expect(monitor.get_last_file_path() == file_path_1_1);
      expect(monitor.get_last_file_body1_1() == "1_1_3"s);
      expect(monitor.get_last_file_body1_2() == std::nullopt);
      expect(monitor.get_last_file_body2_1() == std::nullopt);

      // ========================================
      // Generic file modification (update file2_1)
      // ========================================

      std::cout << "." << std::flush;

      monitor.clear_results();

      system("/bin/echo -n 2_1_1 > target/sub2/file2_1");

      monitor.wait();

      expect(monitor.get_count() >= 1);
      expect(monitor.get_last_file_path() == file_path_2_1);
      expect(monitor.get_last_file_body1_1() == std::nullopt);
      expect(monitor.get_last_file_body1_2() == std::nullopt);
      expect(monitor.get_last_file_body2_1() == "2_1_1"s);

      // ========================================
      // File removal
      // ========================================

      std::cout << "." << std::flush;

      monitor.clear_results();

      system("rm target/sub1/file1_2");

      monitor.wait();

      expect(monitor.get_count() >= 1);
      expect(monitor.get_last_file_path() == file_path_1_2);
      expect(monitor.get_last_file_body1_1() == std::nullopt);
      expect(monitor.get_last_file_body1_2() == std::nullopt);
      expect(monitor.get_last_file_body2_1() == std::nullopt);

      // ========================================
      // File removal
      // ========================================

      std::cout << "." << std::flush;

      monitor.clear_results();

      system("rm target/sub2/file2_1");

      monitor.wait();

      expect(monitor.get_count() >= 1);
      expect(monitor.get_last_file_path() == file_path_2_1);
      expect(monitor.get_last_file_body1_1() == std::nullopt);
      expect(monitor.get_last_file_body1_2() == std::nullopt);
      expect(monitor.get_last_file_body2_1() == std::nullopt);

      // ========================================
      // Directory removal
      // ========================================

      std::cout << "." << std::flush;

      monitor.clear_results();

      system("rm -rf target");

      monitor.wait();

      expect(monitor.get_count() >= 1);
      expect(monitor.get_last_file_path() == file_path_1_1);
      expect(monitor.get_last_file_body1_1() == std::nullopt);
      expect(monitor.get_last_file_body1_2() == std::nullopt);
      expect(monitor.get_last_file_body2_1() == std::nullopt);

      // ========================================
      // Generic file modification
      // ========================================

      std::cout << "." << std::flush;

      monitor.clear_results();

      system("mkdir -p target/sub1");

      monitor.wait();

      system("/bin/echo -n 1_1_4 > target/sub1/file1_1");

      monitor.wait();

      expect(monitor.get_count() >= 1);
      expect(monitor.get_last_file_path() == file_path_1_1);
      expect(monitor.get_last_file_body1_1() == "1_1_4"s);
      expect(monitor.get_last_file_body1_2() == std::nullopt);
      expect(monitor.get_last_file_body2_1() == std::nullopt);

      // ========================================
      // Move file
      // ========================================

      std::cout << "." << std::flush;

      monitor.clear_results();

      system("/bin/echo -n 1_1_5 > target/sub1/file1_1.new");
      system("mv target/sub1/file1_1.new target/sub1/file1_1");

      monitor.wait();

      expect(monitor.get_count() >= 1);
      expect(monitor.get_last_file_path() == file_path_1_1);
      expect(monitor.get_last_file_body1_1() == "1_1_5"s);
      expect(monitor.get_last_file_body1_2() == std::nullopt);
      expect(monitor.get_last_file_body2_1() == std::nullopt);

      // ========================================
      // Move directory
      // ========================================

      std::cout << "." << std::flush;

      monitor.clear_results();

      system("rm -rf target");

      monitor.wait();

      expect(monitor.get_count() >= 1);

      system("mkdir -p target.new/sub1");
      system("/bin/echo -n 1_1_6 > target.new/sub1/file1_1");
      system("mv target.new target");

      monitor.wait();

      expect(monitor.get_count() >= 2);
      expect(monitor.get_last_file_path() == file_path_1_1);
      expect(monitor.get_last_file_body1_1() == "1_1_6"s);
      expect(monitor.get_last_file_body1_2() == std::nullopt);
      expect(monitor.get_last_file_body2_1() == std::nullopt);

      // ========================================
      // Ignore own process
      // ========================================

      std::cout << "." << std::flush;

      monitor.clear_results();

      {
        std::ofstream(file_path_1_1) << "1_1_7";
      }

      monitor.wait();

      expect(monitor.get_count() == 0);
      expect(monitor.get_last_file_path() == std::nullopt);
      expect(monitor.get_last_file_body1_1() == std::nullopt);
      expect(monitor.get_last_file_body1_2() == std::nullopt);
      expect(monitor.get_last_file_body2_1() == std::nullopt);

      // ========================================
      // enqueue_file_changed
      // ========================================

      std::cout << "." << std::flush;

      monitor.clear_results();

      monitor.enqueue_file_changed(file_path_1_1);

      monitor.wait();

      expect(monitor.get_count() >= 1);
      expect(monitor.get_last_file_path() == file_path_1_1);
      expect(monitor.get_last_file_body1_1() == "1_1_7"s);
      expect(monitor.get_last_file_body1_2() == std::nullopt);
      expect(monitor.get_last_file_body2_1() == std::nullopt);
    }

    {
      // ========================================
      // Create test_file_monitor when any target files do not exist.
      // ========================================

      std::cout << "." << std::flush;

      system("rm -rf target");

      test_file_monitor monitor;

      expect(monitor.get_count() >= 3);
      expect(monitor.get_last_file_path() == file_path_2_1);
      expect(monitor.get_last_file_body1_1() == std::nullopt);
      expect(monitor.get_last_file_body1_2() == std::nullopt);
      expect(monitor.get_last_file_body2_1() == std::nullopt);

      // ========================================
      // Generic file modification
      // ========================================

      std::cout << "." << std::flush;

      monitor.clear_results();

      system("mkdir -p target/sub1");
      system("/bin/echo -n 1_1_0 > target/sub1/file1_1");

      monitor.wait();

      expect(monitor.get_count() >= 1);
      expect(monitor.get_last_file_path() == file_path_1_1);
      expect(monitor.get_last_file_body1_1() == "1_1_0"s);
      expect(monitor.get_last_file_body1_2() == std::nullopt);
      expect(monitor.get_last_file_body2_1() == std::nullopt);
    }

    {
      // ========================================
      // Update file after self update.
      // ========================================

      std::cout << "." << std::flush;

      system("rm -rf target");
      system("mkdir -p target/sub1");
      system("mkdir -p target/sub2");
      system("/bin/echo -n 1_1_0 > target/sub1/file1_1");
      system("/bin/echo -n 1_2_0 > target/sub1/file1_2");

      test_file_monitor monitor;

      monitor.clear_results();

      {
        std::ofstream(file_path_1_1) << "1_1_1";
      }

      monitor.wait();

      expect(monitor.get_count() == 0);

      system("/bin/echo -n 1_1_0 > target/sub1/file1_1");

      monitor.wait();

      expect(monitor.get_count() >= 1);
      expect(monitor.get_last_file_path() == file_path_1_1);
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

  return 0;
}
