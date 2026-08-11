#include <signals/signals.h>

int main() {
  signals2::signal2<void, int> signal;
  int received = 0;
  auto connection =
      signal.connect([&received](int value) { received = value; });
  signal(42);
  return received == 42 && connection.connected() ? 0 : 1;
}
