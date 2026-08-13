/**
 * @author McMurphy Luo
 * @description Test cases for State / Computed (state.h)
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
concept HasDefaultState = requires { typename State<T>; };

static_assert(HasDefaultState<int>);
static_assert(!HasDefaultState<NoEq>);
static_assert(!std::default_initializable<ModuloEqual>);

}  // namespace

// ---- 1. basic set / subscribe / equality gate ----
TEST_CASE("State notifies only on a real change") {
  State<int> s(1);
  int calls = 0;
  int last = 0;
  std::vector<signals2::connection> conns;
  conns.push_back(s.Subscribe([&](int v) { ++calls; last = v; }));

  s.Set(2);
  s.Set(2);  // equal -> no notify
  CHECK(calls == 1);
  CHECK(last == 2);

  s = 7;  // operator=
  CHECK(calls == 2);
  CHECK(last == 7);
}

// ---- 2. FireNow ----
TEST_CASE("FireNow::Yes invokes the callback immediately") {
  State<std::string> s(std::string("hello"));
  std::string seen;
  std::vector<signals2::connection> conns;
  conns.push_back(
      s.Subscribe([&](const std::string& v) { seen = v; }, FireNow::Yes));
  CHECK(seen == "hello");
}

// ---- 3. connection lifetime: scope dies -> no more callbacks ----
TEST_CASE("A connection vector disconnects on destruction") {
  State<int> s(0);
  int calls = 0;
  {
    std::vector<signals2::connection> conns;
    conns.push_back(s.Subscribe([&](int) { ++calls; }));
    s.Set(1);
  }
  s.Set(2);  // conns gone -> must not fire
  CHECK(calls == 1);
}

// ---- 4. member function overload, incl. implicit conversion ----
TEST_CASE("Subscribe accepts member functions and zero-arg callables") {
  State<std::string> title(std::string("A"));
  State<bool> vis(false);
  Label label;
  std::vector<signals2::connection> conns;

  conns.push_back(title.Subscribe(&label, &Label::SetText, FireNow::Yes));
  conns.push_back(vis.Subscribe(&label, &Label::SetVisible, FireNow::Yes));
  CHECK(label.text == "A");
  CHECK(label.visible == false);

  title.Set(std::string("B"));
  CHECK(label.text == "B");

  // zero-argument callable
  conns.push_back(title.Subscribe([&] { ++label.hits; }));
  title.Set(std::string("C"));
  CHECK(label.hits == 1);

  // method whose parameter type the value only converts to
  Label converted;
  std::vector<signals2::connection> conv_conns;
  conv_conns.push_back(
      title.Subscribe(&converted, &Label::SetTextView, FireNow::Yes));
  CHECK(converted.text == "C");
  title.Set(std::string("D"));
  CHECK(converted.text == "D");
}

// ---- 5. Computed: automatic dependency discovery ----
TEST_CASE("Computed discovers its dependencies automatically") {
  State<std::string> first(std::string("Ada"));
  State<std::string> last(std::string("Lovelace"));
  Computed<std::string> full;
  int notifies = 0;

  full.Bind([&] { return first.Get() + " " + last.Get(); });
  CHECK(full.Get() == "Ada Lovelace");

  std::vector<signals2::connection> conns;
  conns.push_back(full.Subscribe([&](const std::string&) { ++notifies; }));

  first.Set(std::string("Grace"));
  CHECK(full.Get() == "Grace Lovelace");
  last.Set(std::string("Hopper"));
  CHECK(full.Get() == "Grace Hopper");
  CHECK(notifies == 2);

  first.Set(std::string("Grace"));  // unchanged
  CHECK(notifies == 2);
}

// ---- 6. chained computed (A -> B -> C) ----
TEST_CASE("Computed chains propagate through nested tracking") {
  State<int> n(2);
  Computed<int> doubled;
  Computed<std::string> text;
  doubled.Bind([&] { return n.Get() * 2; });
  text.Bind([&] { return std::string("v=") + std::to_string(doubled.Get()); });

  CHECK(text.Get() == "v=4");
  n.Set(5);
  CHECK(doubled.Get() == 10);
  CHECK(text.Get() == "v=10");
}

// ---- 7. conditional dependency (branch switch) -- invariant §2.3 ----
TEST_CASE("Computed tracks a new branch and tolerates stale dependencies") {
  State<bool> use_a(true);
  State<int> a(1);
  State<int> b(100);
  Computed<int> pick;
  pick.Bind([&] { return use_a.Get() ? a.Get() : b.Get(); });

  CHECK(pick.Get() == 1);
  use_a.Set(false);
  CHECK(pick.Get() == 100);
  b.Set(200);
  CHECK(pick.Get() == 200);

  a.Set(50);  // stale dep -> recompute, same result
  CHECK(pick.Get() == 200);
}

// ---- 8. Mutate on a container ----
TEST_CASE("Mutate edits in place and always notifies") {
  State<std::vector<int>> list;
  int calls = 0;
  std::vector<signals2::connection> conns;
  conns.push_back(list.Subscribe([&](const std::vector<int>&) { ++calls; }));

  list.Mutate([](std::vector<int>& v) { v.push_back(1); });
  list.Mutate([](std::vector<int>& v) { v.push_back(2); });
  CHECK(list.Get().size() == 2u);
  CHECK(calls == 2);
}

// ---- 9. a type without operator== supplies an explicit BinaryPred ----
TEST_CASE("State accepts an explicit equality predicate") {
  State<NoEq, NoEqEqual> s(NoEq{1});
  int calls = 0;
  std::vector<signals2::connection> conns;
  conns.push_back(s.Subscribe([&](const NoEq&) { ++calls; }));

  s.Set(NoEq{1});  // predicate says equal -> retain the old value, no notify
  CHECK(calls == 0);

  s.Set(NoEq{2});
  CHECK(s.Get().a == 2);
  CHECK(calls == 1);
}

// ---- 9b. Computed stores and uses a stateful equality predicate ----
TEST_CASE("Computed accepts a stateful equality predicate") {
  State<int> source(1);
  Computed<int, ModuloEqual> computed(ModuloEqual{2});
  computed.Bind([&] { return source.Get(); });
  CHECK(computed.Get() == 1);

  int calls = 0;
  std::vector<signals2::connection> conns;
  conns.push_back(computed.Subscribe([&](int) { ++calls; }));

  source.Set(3);  // equivalent modulo 2: keep the previously stored value
  CHECK(computed.Get() == 1);
  CHECK(calls == 0);

  source.Set(4);  // a different equivalence class: commit and notify
  CHECK(computed.Get() == 4);
  CHECK(calls == 1);
}

// ---- 10. converging feedback terminates on the equality check -- §2.7 ----
TEST_CASE("Feedback that settles converges to its fixed point") {
  // x and y feed each other, but x saturates, so the loop has a fixed point
  // and the equality check stops it. Divergent feedback is NOT covered here
  // and is not covered anywhere: it exhausts the stack by design. See §2.7.
  State<int> seed(1);
  Computed<int> x;
  Computed<int> y;
  x.Bind([&] { return std::min(seed.Get() + y.Get(), 10); });
  y.Bind([&] { return x.Get(); });

  CHECK(x.Get() == 10);
  CHECK(y.Get() == 10);

  seed.Set(2);
  CHECK(x.Get() == 10);
  CHECK(y.Get() == 10);
}

// ---- 11. Peek does not create a dependency ----
TEST_CASE("Peek reads without subscribing") {
  State<int> tracked(1);
  State<int> untracked(10);
  Computed<int> c;
  c.Bind([&] { return tracked.Get() + untracked.Peek(); });

  CHECK(c.Get() == 11);
  untracked.Set(20);
  CHECK(c.Get() == 11);  // peek did not subscribe
  tracked.Set(2);
  CHECK(c.Get() == 22);  // recompute reads the fresh peeked value
}

// ---- 12. Observable<T> as a read-only parameter type ----
TEST_CASE("const Observable<T>& accepts both State and Computed") {
  State<int> s(3);
  Computed<int> c;
  c.Bind([&] { return s.Get() * 3; });

  auto read = [](const Observable<int>& o) { return o.Get(); };
  CHECK(read(s) == 3);
  CHECK(read(c) == 9);
}

// ---- 12b. the read-only handle must also be able to observe ----
TEST_CASE("const Observable<T>& can subscribe, not just read") {
  State<int> s(1);
  Computed<int> c;
  c.Bind([&] { return s.Get() * 3; });

  int from_state = 0;
  int from_computed = 0;
  std::vector<signals2::connection> conns;

  // Takes the read-only handle by const reference, exactly as the docs
  // recommend for a function that only observes.
  auto observe = [&conns](const Observable<int>& o, int& sink) {
    conns.push_back(o.Subscribe([&sink](int v) { sink = v; }, FireNow::Yes));
  };
  observe(s, from_state);
  observe(c, from_computed);
  CHECK(from_state == 1);
  CHECK(from_computed == 3);

  s.Set(4);
  CHECK(from_state == 4);
  CHECK(from_computed == 12);
}

// ---- 12c. the member-function overload is const too ----
TEST_CASE("const Observable<T>& can subscribe a member function") {
  State<std::string> s(std::string("A"));
  Label label;
  std::vector<signals2::connection> conns;

  // Guards the second of the two const overloads: test 4 exercises the same
  // call through a non-const State, so dropping the const there would still
  // compile without this case.
  const Observable<std::string>& handle = s;
  conns.push_back(handle.Subscribe(&label, &Label::SetText, FireNow::Yes));
  CHECK(label.text == "A");

  s.Set(std::string("B"));
  CHECK(label.text == "B");
}

// ---- 13. observer dies before the observed state ----
TEST_CASE("An observer that dies with its connections is safe") {
  State<std::string> s(std::string("a"));
  {
    Label label;
    std::vector<signals2::connection> conns;
    conns.push_back(s.Subscribe(&label, &Label::SetText, FireNow::Yes));
    s.Set(std::string("b"));
    CHECK(label.text == "b");
  }
  // label and its connections are gone; the slot must not be invoked.
  s.Set(std::string("c"));
  CHECK(s.Get() == "c");
}

// ---- 14. State dies before the Computed that depends on it -- invariant ----
TEST_CASE("Computed keeps its last value after a dependency is destroyed") {
  Computed<int> c;
  {
    State<int> tmp(5);
    c.Bind([&] { return tmp.Get() * 2; });
    CHECK(c.Get() == 10);
  }
  // tmp is gone; its signal is gone; c keeps its last value and must not crash.
  //
  // NOTE: this only covers *reading* the cached value. Re-running the compute
  // function after tmp died would read a dangling reference -- a Computed must
  // not outlive any state its compute function can reach.
  CHECK(c.Get() == 10);
}

// ---- 15. a subscriber writes a dependency -- invariant §2.7 ----
TEST_CASE("A subscriber that corrects a dependency is honoured, not dropped") {
  State<int> src(1);
  Computed<int> doubled;
  doubled.Bind([&] { return src.Get() * 2; });

  std::vector<signals2::connection> conns;
  conns.push_back(doubled.Subscribe([&](int v) {
    if (v < 10) {
      src.Set(v + 100);  // one-shot correction from inside the notification
    }
  }));

  src.Set(2);
  // The nested recompute triggered by the correction must land. Before the
  // epoch rule this stopped at 4 -- src had moved to 104 and the derived value
  // never caught up.
  CHECK(src.Get() == 104);
  CHECK(doubled.Get() == 208);
}

// ---- 16. calc writes a dependency it already read -- invariant §2.7 ----
TEST_CASE("A superseded compute pass does not commit its stale result") {
  State<int> a(1);
  State<int> b(1);
  Computed<int> c;

  c.Bind([&] {
    int va = a.Get();
    int vb = b.Get();  // read, so the write below reenters Recompute
    if (vb < 5) {
      b.Set(vb + 10);
    }
    return va + vb;  // computed from the value of b *before* the write
  });

  // The nested pass sees b == 11 and stores 12. This outer pass must then
  // discard its own result rather than overwrite 12 with 1 + 1.
  CHECK(b.Get() == 11);
  CHECK(c.Get() == 12);
}
