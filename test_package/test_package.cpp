// Smoke test for the *installed* package: both public headers must be present
// and usable through the imported target alone.

#include <signals/signals.h>
#include <signals/state.h>

#include <string>
#include <vector>

struct NonComparable {
  int value = 0;
};

int main() {
  signals2::signal2<void, int> signal;
  int received = 0;
  auto connection =
      signal.connect([&received](int value) { received = value; });
  signal(42);
  if (received != 42 || !connection.connected()) {
    return 1;
  }

  // state.h: state, computed, and the read-only handle. This is also what
  // pins C++20 -- state.h uses concepts, so it fails to compile if the
  // imported target stops propagating cxx_std_20.
  signals2::state<int> count(2);
  signals2::computed<std::string> label;
  label.bind([&count] { return "n=" + std::to_string(count.get()); });

  std::string seen;
  const signals2::observable<std::string>& handle = label;
  std::vector<signals2::connection> conns;
  seen = handle.get();
  conns.push_back(handle.connect(
      [&seen](const std::string& value) { seen = value; }));
  if (seen != "n=2") {
    return 2;
  }

  count.set(7);
  if (seen != "n=7" || label.get() != "n=7") {
    return 3;
  }

  signals2::state<NonComparable, signals2::always_notify> always(
      NonComparable{1});
  int notifications = 0;
  conns.push_back(always.connect(
      [&notifications](const NonComparable&) { ++notifications; }));
  always.set(NonComparable{1});
  if (notifications != 1) {
    return 4;
  }

  return 0;
}
