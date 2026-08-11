#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

#include "signals/state.h"

using namespace states;

struct Label {
  std::string text;
  bool visible = true;
  void SetText(const std::string& t) { text = t; }
  void SetTextRaw(const char* t) { text = t; }   // implicit conversion target
  void SetVisible(bool v) { visible = v; }
  void OnChangedNoArg() { ++hits; }
  int hits = 0;
};

// A type with no operator== -- must still compile.
struct NoEq {
  int a = 0;
};

int main() {
  int failures = 0;
  auto check = [&](bool ok, const char* what) {
    if (!ok) { std::printf("FAIL: %s\n", what); ++failures; }
  };

  // ---- 1. basic set / subscribe / equality gate ----
  {
    State<int> s(1);
    int calls = 0, last = 0;
    ConnectionScope conns;
    conns.push_back(s.Subscribe([&](int v) { ++calls; last = v; }));
    s.Set(2);
    s.Set(2);                                  // equal -> no notify
    check(calls == 1 && last == 2, "1. equality gate");

    s = 7;                                     // operator=
    check(calls == 2 && last == 7, "1. operator=");
  }

  // ---- 2. FireNow ----
  {
    State<std::string> s(std::string("hello"));
    std::string seen;
    ConnectionScope conns;
    conns.push_back(s.Subscribe([&](const std::string& v) { seen = v; }, FireNow::Yes));
    check(seen == "hello", "2. FireNow::Yes fires immediately");
  }

  // ---- 3. connection lifetime: scope dies -> no more callbacks ----
  {
    State<int> s(0);
    int calls = 0;
    {
      ConnectionScope conns;
      conns.push_back(s.Subscribe([&](int) { ++calls; }));
      s.Set(1);
    }
    s.Set(2);                                  // conns gone -> must not fire
    check(calls == 1, "3. connection RAII disconnects");
  }

  // ---- 4. member function overload, incl. implicit conversion ----
  {
    State<std::string> title(std::string("A"));
    State<bool> vis(false);
    Label label;
    ConnectionScope conns;
    conns.push_back(title.Subscribe(&label, &Label::SetText, FireNow::Yes));
    conns.push_back(vis.Subscribe(&label, &Label::SetVisible, FireNow::Yes));
    check(label.text == "A" && label.visible == false, "4. member fn + FireNow");
    title.Set(std::string("B"));
    check(label.text == "B", "4. member fn on change");

    // no-argument callable
    conns.push_back(title.Subscribe([&] { ++label.hits; }));
    title.Set(std::string("C"));
    check(label.hits == 1, "4. zero-arg callable");
  }

  // ---- 5. Computed: automatic dependency discovery ----
  {
    State<std::string> first(std::string("Ada"));
    State<std::string> last(std::string("Lovelace"));
    Computed<std::string> full;
    int notifies = 0;

    full.Bind([&] { return first.Get() + " " + last.Get(); });
    check(full.Get() == "Ada Lovelace", "5. computed initial value");

    ConnectionScope conns;
    conns.push_back(full.Subscribe([&](const std::string&) { ++notifies; }));

    first.Set(std::string("Grace"));
    check(full.Get() == "Grace Lovelace", "5. recompute on dep 1");
    last.Set(std::string("Hopper"));
    check(full.Get() == "Grace Hopper", "5. recompute on dep 2");
    check(notifies == 2, "5. one notify per real change");

    first.Set(std::string("Grace"));           // unchanged
    check(notifies == 2, "5. no notify when dep unchanged");
  }

  // ---- 6. chained computed (A -> B -> C) ----
  {
    State<int> n(2);
    Computed<int> doubled;
    Computed<std::string> text;
    doubled.Bind([&] { return n.Get() * 2; });
    text.Bind([&] { return std::string("v=") + std::to_string(doubled.Get()); });

    check(text.Get() == "v=4", "6. chained initial");
    n.Set(5);
    check(doubled.Get() == 10 && text.Get() == "v=10", "6. chained propagation");
  }

  // ---- 7. conditional dependency (branch switch) ----
  {
    State<bool> use_a(true);
    State<int> a(1), b(100);
    Computed<int> pick;
    pick.Bind([&] { return use_a.Get() ? a.Get() : b.Get(); });

    check(pick.Get() == 1, "7. branch a");
    use_a.Set(false);
    check(pick.Get() == 100, "7. switched to branch b");
    b.Set(200);
    check(pick.Get() == 200, "7. tracks new branch dep");
    a.Set(50);                                  // stale dep -> recompute, same result
    check(pick.Get() == 200, "7. stale dep harmless");
  }

  // ---- 8. Mutate on a container ----
  {
    State<std::vector<int>> list;
    int calls = 0;
    ConnectionScope conns;
    conns.push_back(list.Subscribe([&](const std::vector<int>&) { ++calls; }));
    list.Mutate([](std::vector<int>& v) { v.push_back(1); });
    list.Mutate([](std::vector<int>& v) { v.push_back(2); });
    check(list.Get().size() == 2 && calls == 2, "8. Mutate notifies");
  }

  // ---- 9. type without operator== still compiles ----
  {
    State<NoEq> s;
    int calls = 0;
    ConnectionScope conns;
    conns.push_back(s.Subscribe([&](const NoEq&) { ++calls; }));
    s.Set(NoEq{1});
    s.Set(NoEq{1});                            // no ==, so notifies every time
    check(calls == 2, "9. non-comparable T");
  }

  // ---- 10. cycle does not blow the stack ----
  {
    State<int> seed(1);
    Computed<int> x, y;
    x.Bind([&] { return seed.Get() + y.Get(); });
    y.Bind([&] { return x.Get(); });
    seed.Set(2);                               // must terminate
    check(true, "10. cycle terminates");
  }

  // ---- 11. Peek does not create a dependency ----
  {
    State<int> tracked(1), untracked(10);
    Computed<int> c;
    c.Bind([&] { return tracked.Get() + untracked.Peek(); });
    check(c.Get() == 11, "11. peek initial");
    untracked.Set(20);
    check(c.Get() == 11, "11. peek did not subscribe");
    tracked.Set(2);
    check(c.Get() == 22, "11. recompute reads fresh peek value");
  }

  // ---- 12. Observable<T>& as a read-only parameter type ----
  {
    State<int> s(3);
    Computed<int> c;
    c.Bind([&] { return s.Get() * 3; });
    auto read = [](const Observable<int>& o) { return o.Get(); };
    check(read(s) == 3 && read(c) == 9, "12. Observable as parameter");
  }

  // ---- 13. observed object outlives observer (reverse direction) ----
  {
    State<int> s(0);
    int calls = 0;
    {
      ConnectionScope conns;
      conns.push_back(s.Subscribe([&](int) { ++calls; }));
      s.Set(1);
    }
    check(calls == 1, "13. observer death is safe");
  }

  // ---- 14. State dies before Computed that depends on it ----
  {
    Computed<int> c;
    int last = -1;
    {
      State<int> tmp(5);
      c.Bind([&] { return tmp.Get() * 2; });
      last = c.Get();
      check(last == 10, "14. computed over temp state");
    }
    // tmp is gone; its signal is gone; c keeps its last value and must not crash.
    check(c.Get() == 10, "14. survives dependency destruction");
  }

  if (failures == 0) {
    std::printf("ALL PASS\n");
  }
  return failures;
}
