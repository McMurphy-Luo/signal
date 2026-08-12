// Smoke test for the *installed* package: both public headers must be present
// and usable through the imported target alone.

#include <signals/signals.h>
#include <signals/state.h>

#include <string>

int main() {
  signals2::signal2<void, int> signal;
  int received = 0;
  auto connection =
      signal.connect([&received](int value) { received = value; });
  signal(42);
  if (received != 42 || !connection.connected()) {
    return 1;
  }

  // state.h: State, Computed, and the read-only handle. This is also what
  // pins C++20 -- state.h uses concepts, so it fails to compile if the
  // imported target stops propagating cxx_std_20.
  signals2::State<int> count(2);
  signals2::Computed<std::string> label;
  label.Bind([&count] { return "n=" + std::to_string(count.Get()); });

  std::string seen;
  const signals2::Observable<std::string>& handle = label;
  signals2::ConnectionScope conns;
  conns.push_back(handle.Subscribe(
      [&seen](const std::string& value) { seen = value; },
      signals2::FireNow::Yes));
  if (seen != "n=2") {
    return 2;
  }

  count.Set(7);
  if (seen != "n=7" || label.Get() != "n=7") {
    return 3;
  }

  return 0;
}
