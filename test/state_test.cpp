/**
 * @author McMurphy Luo
 * @description Test cases for state / computed (state.h)
 *
 * The numbering matches the test matrix in docs/STATE_DESIGN_CONTEXT.md §7.
 * Cases marked (invariant) guard a rule from §4 -- they must keep passing
 * across refactors.
 */

#include "catch_amalgamated.hpp"

#include <signals/state.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

using namespace signals2;

namespace {

class Label {
public:
  void SetText(const std::string& t) { text = t; }
  // Exercises the implicit-conversion path of the member-function overload:
  // the signal carries const std::string&, the method takes string_view.
  void SetTextView(std::string_view t) { text = std::string(t); }
  void SetVisible(bool v) { visible = v; }

  std::string text;
  bool visible = true;
  int hits = 0;
};

// A type with no operator==. Scheme A requires an explicit predicate.
struct NoEq {
  int a = 0;
};

struct NoEqEqual {
  bool operator()(const NoEq& lhs, const NoEq& rhs) const noexcept {
    return lhs.a == rhs.a;
  }
};

struct ModuloEqual {
  explicit ModuloEqual(int value) : divisor(value) {}

  bool operator()(int lhs, int rhs) const noexcept {
    return lhs % divisor == rhs % divisor;
  }

  int divisor;
};

template <typename T>
concept HasDefaultState = requires { typename state<T>; };

static_assert(HasDefaultState<int>);
static_assert(!HasDefaultState<NoEq>);
static_assert(!std::default_initializable<ModuloEqual>);

}  // namespace

// ---- 1. basic set / subscribe / equality gate ----
TEST_CASE("state notifies only on a real change") {
  state<int> s(1);
  int calls = 0;
  int last = 0;
  std::vector<signals2::connection> conns;
  conns.push_back(s.connect([&](int v) { ++calls; last = v; }));

  s.set(2);
  s.set(2);  // equal -> no notify
  CHECK(calls == 1);
  CHECK(last == 2);

  s = 7;  // operator=
  CHECK(calls == 2);
  CHECK(last == 7);
}

// ---- 2. connect observes future notifications only ----
TEST_CASE("connect does not invoke the callback immediately") {
  state<std::string> s(std::string("hello"));
  std::string seen;
  std::vector<signals2::connection> conns;
  conns.push_back(s.connect([&](const std::string& v) { seen = v; }));

  CHECK(seen.empty());
  s.set(std::string("world"));
  CHECK(seen == "world");
}

// ---- 3. connection lifetime: scope dies -> no more callbacks ----
TEST_CASE("A connection vector disconnects on destruction") {
  state<int> s(0);
  int calls = 0;
  {
    std::vector<signals2::connection> conns;
    conns.push_back(s.connect([&](int) { ++calls; }));
    s.set(1);
  }
  s.set(2);  // conns gone -> must not fire
  CHECK(calls == 1);
}

// ---- 4. member function overload, incl. implicit conversion ----
TEST_CASE("connect accepts member functions and zero-arg callables") {
  state<std::string> title(std::string("A"));
  state<bool> vis(false);
  Label label;
  std::vector<signals2::connection> conns;

  label.SetText(title.get());
  label.SetVisible(vis.get());
  conns.push_back(title.connect(&label, &Label::SetText));
  conns.push_back(vis.connect(&label, &Label::SetVisible));
  CHECK(label.text == "A");
  CHECK(label.visible == false);

  title.set(std::string("B"));
  CHECK(label.text == "B");

  // zero-argument callable
  conns.push_back(title.connect([&] { ++label.hits; }));
  title.set(std::string("C"));
  CHECK(label.hits == 1);

  // method whose parameter type the value only converts to
  Label converted;
  std::vector<signals2::connection> conv_conns;
  converted.SetTextView(title.get());
  conv_conns.push_back(title.connect(&converted, &Label::SetTextView));
  CHECK(converted.text == "C");
  title.set(std::string("D"));
  CHECK(converted.text == "D");
}

// ---- 5. computed: automatic dependency discovery ----
TEST_CASE("computed discovers its dependencies automatically") {
  state<std::string> first(std::string("Ada"));
  state<std::string> last(std::string("Lovelace"));
  computed<std::string> full;
  int notifies = 0;

  full.bind([&] { return first.get() + " " + last.get(); });
  CHECK(full.get() == "Ada Lovelace");

  std::vector<signals2::connection> conns;
  conns.push_back(full.connect([&](const std::string&) { ++notifies; }));

  first.set(std::string("Grace"));
  CHECK(full.get() == "Grace Lovelace");
  last.set(std::string("Hopper"));
  CHECK(full.get() == "Grace Hopper");
  CHECK(notifies == 2);

  first.set(std::string("Grace"));  // unchanged
  CHECK(notifies == 2);
}

// ---- 6. chained computed (A -> B -> C) ----
TEST_CASE("computed chains propagate through nested tracking") {
  state<int> n(2);
  computed<int> doubled;
  computed<std::string> text;
  doubled.bind([&] { return n.get() * 2; });
  text.bind([&] { return std::string("v=") + std::to_string(doubled.get()); });

  CHECK(text.get() == "v=4");
  n.set(5);
  CHECK(doubled.get() == 10);
  CHECK(text.get() == "v=10");
}

// ---- 7. conditional dependency (branch switch) -- invariant §2.3 ----
TEST_CASE("computed tracks a new branch and tolerates stale dependencies") {
  state<bool> use_a(true);
  state<int> a(1);
  state<int> b(100);
  computed<int> pick;
  pick.bind([&] { return use_a.get() ? a.get() : b.get(); });

  CHECK(pick.get() == 1);
  use_a.set(false);
  CHECK(pick.get() == 100);
  b.set(200);
  CHECK(pick.get() == 200);

  a.set(50);  // stale dep -> recompute, same result
  CHECK(pick.get() == 200);
}

// ---- 8. mutate on a container ----
TEST_CASE("mutate edits in place and always notifies") {
  state<std::vector<int>> list;
  int calls = 0;
  std::vector<signals2::connection> conns;
  conns.push_back(list.connect([&](const std::vector<int>&) { ++calls; }));

  list.mutate([](std::vector<int>& v) { v.push_back(1); });
  list.mutate([](std::vector<int>& v) { v.push_back(2); });
  CHECK(list.get().size() == 2u);
  CHECK(calls == 2);
}

// ---- 9. a type without operator== supplies an explicit BinaryPred ----
TEST_CASE("state accepts an explicit equality predicate") {
  state<NoEq, NoEqEqual> s(NoEq{1});
  int calls = 0;
  std::vector<signals2::connection> conns;
  conns.push_back(s.connect([&](const NoEq&) { ++calls; }));

  s.set(NoEq{1});  // predicate says equal -> retain the old value, no notify
  CHECK(calls == 0);

  s.set(NoEq{2});
  CHECK(s.get().a == 2);
  CHECK(calls == 1);
}

// ---- 9b. computed stores and uses a stateful equality predicate ----
TEST_CASE("computed accepts a stateful equality predicate") {
  state<int> source(1);
  computed<int, ModuloEqual> computed(ModuloEqual{2});
  computed.bind([&] { return source.get(); });
  CHECK(computed.get() == 1);

  int calls = 0;
  std::vector<signals2::connection> conns;
  conns.push_back(computed.connect([&](int) { ++calls; }));

  source.set(3);  // equivalent modulo 2: keep the previously stored value
  CHECK(computed.get() == 1);
  CHECK(calls == 0);

  source.set(4);  // a different equivalence class: commit and notify
  CHECK(computed.get() == 4);
  CHECK(calls == 1);
}

// ---- 10. converging feedback terminates on the equality check -- §2.7 ----
TEST_CASE("Feedback that settles converges to its fixed point") {
  // x and y feed each other, but x saturates, so the loop has a fixed point
  // and the equality check stops it. Divergent feedback is NOT covered here
  // and is not covered anywhere: it exhausts the stack by design. See §2.7.
  state<int> seed(1);
  computed<int> x;
  computed<int> y;
  x.bind([&] { return std::min(seed.get() + y.get(), 10); });
  y.bind([&] { return x.get(); });

  CHECK(x.get() == 10);
  CHECK(y.get() == 10);

  seed.set(2);
  CHECK(x.get() == 10);
  CHECK(y.get() == 10);
}

// ---- 11. peek does not create a dependency ----
TEST_CASE("peek reads without subscribing") {
  state<int> tracked(1);
  state<int> untracked(10);
  computed<int> c;
  c.bind([&] { return tracked.get() + untracked.peek(); });

  CHECK(c.get() == 11);
  untracked.set(20);
  CHECK(c.get() == 11);  // peek did not subscribe
  tracked.set(2);
  CHECK(c.get() == 22);  // recompute reads the fresh peeked value
}

// ---- 12. observable<T> as a read-only parameter type ----
TEST_CASE("const observable<T>& accepts both state and computed") {
  state<int> s(3);
  computed<int> c;
  c.bind([&] { return s.get() * 3; });

  auto read = [](const observable<int>& o) { return o.get(); };
  CHECK(read(s) == 3);
  CHECK(read(c) == 9);
}

// ---- 12b. the read-only handle must also be able to observe ----
TEST_CASE("const observable<T>& can subscribe, not just read") {
  state<int> s(1);
  computed<int> c;
  c.bind([&] { return s.get() * 3; });

  int from_state = 0;
  int from_computed = 0;
  std::vector<signals2::connection> conns;

  // Takes the read-only handle by const reference, exactly as the docs
  // recommend for a function that only observes.
  auto observe = [&conns](const observable<int>& o, int& sink) {
    sink = o.get();
    conns.push_back(o.connect([&sink](int v) { sink = v; }));
  };
  observe(s, from_state);
  observe(c, from_computed);
  CHECK(from_state == 1);
  CHECK(from_computed == 3);

  s.set(4);
  CHECK(from_state == 4);
  CHECK(from_computed == 12);
}

// ---- 12c. the member-function overload is const too ----
TEST_CASE("const observable<T>& can subscribe a member function") {
  state<std::string> s(std::string("A"));
  Label label;
  std::vector<signals2::connection> conns;

  // Guards the second of the two const overloads: test 4 exercises the same
  // call through a non-const state, so dropping the const there would still
  // compile without this case.
  const observable<std::string>& handle = s;
  label.SetText(handle.get());
  conns.push_back(handle.connect(&label, &Label::SetText));
  CHECK(label.text == "A");

  s.set(std::string("B"));
  CHECK(label.text == "B");
}

// ---- 13. observer dies before the observed state ----
TEST_CASE("An observer that dies with its connections is safe") {
  state<std::string> s(std::string("a"));
  {
    Label label;
    std::vector<signals2::connection> conns;
    conns.push_back(s.connect(&label, &Label::SetText));
    s.set(std::string("b"));
    CHECK(label.text == "b");
  }
  // label and its connections are gone; the slot must not be invoked.
  s.set(std::string("c"));
  CHECK(s.get() == "c");
}

// ---- 14. state dies before the computed that depends on it -- invariant ----
TEST_CASE("computed keeps its last value after a dependency is destroyed") {
  computed<int> c;
  {
    state<int> tmp(5);
    c.bind([&] { return tmp.get() * 2; });
    CHECK(c.get() == 10);
  }
  // tmp is gone; its signal is gone; c keeps its last value and must not crash.
  //
  // NOTE: this only covers *reading* the cached value. Re-running the compute
  // function after tmp died would read a dangling reference -- a computed must
  // not outlive any state its compute function can reach.
  CHECK(c.get() == 10);
}

// ---- 15. a subscriber writes a dependency -- invariant §2.7 ----
TEST_CASE("A subscriber that corrects a dependency is honoured, not dropped") {
  state<int> src(1);
  computed<int> doubled;
  doubled.bind([&] { return src.get() * 2; });

  std::vector<signals2::connection> conns;
  conns.push_back(doubled.connect([&](int v) {
    if (v < 10) {
      src.set(v + 100);  // one-shot correction from inside the notification
    }
  }));

  src.set(2);
  // The nested recompute triggered by the correction must land. Before the
  // epoch rule this stopped at 4 -- src had moved to 104 and the derived value
  // never caught up.
  CHECK(src.get() == 104);
  CHECK(doubled.get() == 208);
}

// ---- 16. calc writes a dependency it already read -- invariant §2.7 ----
TEST_CASE("A superseded compute pass does not commit its stale result") {
  state<int> a(1);
  state<int> b(1);
  computed<int> c;

  c.bind([&] {
    int va = a.get();
    int vb = b.get();  // read, so the write below reenters recompute
    if (vb < 5) {
      b.set(vb + 10);
    }
    return va + vb;  // computed from the value of b *before* the write
  });

  // The nested pass sees b == 11 and stores 12. This outer pass must then
  // discard its own result rather than overwrite 12 with 1 + 1.
  CHECK(b.get() == 11);
  CHECK(c.get() == 12);
}

// ---- 17. reentrant state writes use latest-value notification semantics ----
TEST_CASE("state reentrant writes are serialized to the latest value") {
  state<int> state(0);
  std::vector<int> first_seen;
  std::vector<int> second_seen;
  std::vector<signals2::connection> conns;

  conns.push_back(state.connect([&](int value) {
    first_seen.push_back(value);
    if (value == 1) {
      state.set(2);
      state.set(3);  // supersedes 2 before another notification round starts
    }
  }));
  conns.push_back(
      state.connect([&](int value) { second_seen.push_back(value); }));

  state.set(1);

  CHECK(state.get() == 3);
  CHECK(first_seen == std::vector<int>{1, 3});
  CHECK(second_seen == std::vector<int>{3});
}

// ---- 18. a computed's final revision reaches every subscriber ----
TEST_CASE("computed skips an obsolete revision and delivers its final value") {
  state<int> left(1);
  state<int> right(0);
  computed<int> sum;
  sum.bind([&] { return left.get() + right.get(); });

  std::vector<int> first_seen;
  std::vector<int> second_seen;
  std::vector<signals2::connection> conns;
  conns.push_back(sum.connect([&](int value) {
    first_seen.push_back(value);
    if (value == 2) {
      right.set(10);  // reenters sum while sum is notifying value 2
    }
  }));
  conns.push_back(sum.connect([&](int value) { second_seen.push_back(value); }));

  left.set(2);

  CHECK(sum.get() == 12);
  CHECK(first_seen == std::vector<int>{2, 12});
  CHECK(second_seen == std::vector<int>{12});
}

// ---- 19. notify follows the same revision rules as set ----
TEST_CASE("Reentrant notify serializes another notification round") {
  state<int> state(7);
  int first_calls = 0;
  int second_calls = 0;
  bool notify_again = true;
  std::vector<signals2::connection> conns;

  conns.push_back(state.connect([&](int value) {
    CHECK(value == state.get());
    ++first_calls;
    if (notify_again) {
      notify_again = false;
      state.notify();
    }
  }));
  conns.push_back(state.connect([&](int value) {
    CHECK(value == state.get());
    ++second_calls;
  }));

  state.notify();

  CHECK(first_calls == 2);
  CHECK(second_calls == 1);
}

// ---- 20. mutate also coalesces an obsolete in-flight value ----
TEST_CASE("Reentrant mutate delivers the final container value") {
  state<std::vector<int>> state;
  std::vector<std::size_t> first_sizes;
  std::vector<std::size_t> second_sizes;
  std::vector<signals2::connection> conns;

  conns.push_back(state.connect([&](const std::vector<int>& value) {
    first_sizes.push_back(value.size());
    if (value.size() == 1) {
      state.mutate([](auto& current) { current.push_back(2); });
    }
  }));
  conns.push_back(state.connect(
      [&](const std::vector<int>& value) { second_sizes.push_back(value.size()); }));

  state.mutate([](auto& current) { current.push_back(1); });

  CHECK(state.get() == std::vector<int>{1, 2});
  CHECK(first_sizes == std::vector<std::size_t>{1, 2});
  CHECK(second_sizes == std::vector<std::size_t>{2});
}

// ---- 21. always_notify supports a type without equality ----
TEST_CASE("always_notify lets state store a type without equality") {
  state<NoEq, always_notify> state(NoEq{1});
  int calls = 0;
  std::vector<signals2::connection> conns;
  conns.push_back(state.connect([&](const NoEq&) { ++calls; }));

  state.set(NoEq{1});
  CHECK(calls == 1);

  state.set(NoEq{2});
  CHECK(state.get().a == 2);
  CHECK(calls == 2);
}

// ---- 21b. always_notify also applies to computed ----
TEST_CASE("always_notify makes computed notify for an equivalent result") {
  state<int> source(0);
  computed<int, always_notify> computed;
  computed.bind([&] {
    source.get();
    return 7;
  });

  int calls = 0;
  std::vector<signals2::connection> conns;
  conns.push_back(computed.connect([&](int value) {
    CHECK(value == 7);
    ++calls;
  }));

  source.set(1);
  source.set(2);

  CHECK(computed.get() == 7);
  CHECK(calls == 2);
}

// ---- 22. callback arguments never describe an obsolete value ----
TEST_CASE("A callback argument equals get at invocation time") {
  state<int> state(0);
  std::vector<signals2::connection> conns;
  int calls = 0;

  conns.push_back(state.connect([&](int value) {
    CHECK(value == state.get());
    ++calls;
    if (value < 5) {
      state.set(value + 1);
    }
  }));

  state.set(1);

  CHECK(state.get() == 5);
  CHECK(calls == 5);
}
