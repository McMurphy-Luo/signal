# `signals2::state` / `computed` — 设计上下文与交接说明

> 这份文档记录的是**代码里看不出来的东西**：为什么是这个形状、哪些"更自然"的写法是陷阱、
> 哪些方案被明确否决过。代码本身能回答"是什么"，这里回答"为什么"和"别改成什么"。
>
> 迁出自 `client-5.x-release`，原始参考实现路径见 §1（新库里已无法回溯，故在此记录）。

---

## 0. 交付物

| 文件 | 说明 |
|---|---|
| `include/signals/state.h` | 实现，header-only，依赖 `signals.h`（signals2） |
| `test/state_test.cpp` | 27 组 Catch 行为测试，见 §7 |
| 本文档 | 设计上下文 |

命名空间为 `signals2`，与 `signals.h` 同一个（早期迁移中曾用 `states`，已废弃）。

验证状态：MSVC C++20，`state.h` / `state_test.cpp` 零警告，`signals2_tests` 全过
（含 signals 侧共 42 组）。

注意"零警告"只覆盖本文档的两个交付物。整个测试树目前还有 2 条 C4834，都在
`signals_test.cpp`（[55](../test/signals_test.cpp:55)、[385](../test/signals_test.cpp:385) 行）——
那两处是**故意丢弃 connection 以验证"析构即断开"**，不是疏漏。

---

## 1. 为什么存在：三个前身

设计是从三份既有实现里提炼的。原始路径（`client-5.x-release` 仓库内）：

| 来源 | 路径 |
|---|---|
| signals2（底层信号库，保留使用） | `common/include/signals/signals.h` |
| `phone::State`（直接前身，被替换） | `win-client/src/zPhoneUI/common/State.h` + `State.inl` |
| `zui::State` / `Bind` / `calc`（思路来源） | `common/include/zUI/model/model.h`、`model_observer.h`、`expression.h` |
| zUI 的运行时注册表（**未采用**） | `prism-ui/zUI/src/core/model/model_store.h`、`model_observer.cpp` |

### `phone::State` 的问题（本实现直接针对的）

1. **连接存在被观察者身上。** `SetUpdate` / `SetViewUpdate` 把 `connection` 存进 State 自己的
   `connections_`，于是订阅的存活期绑在**subject**而非**observer**。observer 先死就是野指针调用。
   而 API 命名（`SetUpdate(this, &Foo::Bar)`）在主动诱导这种写法。
2. **空 `std::function` 直接调用。** `SetViewUpdateWithConnection(const UpdateCallback&)` 里
   `func(Get())` 没有空检查，相邻的所有重载都有。
3. **"立即触发"语义不一致且未文档化。** view 通道 connect 时同步跑一次，data 通道不跑，头文件没写。
4. **相等性判断硬写 `value_ != val`。** 没有 `operator!=` 的类型直接编不过；`State<float>` 是浮点精确比较。
5. **`SetComputed(fn, &dep1, &dep2)` 手写依赖列表。** 在 lambda 里加一个 `.Get()` 却忘了加对应 dep，
   computed 就永久读旧值，**编译器不报错**，UI 只是偶尔不刷新。这是最容易在线上出问题的一条。
6. **重载爆炸。** 4 个订阅函数族 × 6 种回调形态 ≈ 20 个公开接口，其中 `void(Class::*)(LPCTSTR)`
   一支写死了 `CString`、且头文件未 include `<tchar.h>`（靠调用方已引入 `LPCTSTR`）——
   这条是"搬进公共 include 目录"的硬阻塞。
7. **`SetWithoutUpdateView` 和 `UpdateView()` 零调用**，是投机 API。

### `zui::State` 为什么没直接复用

它的便利性（自动依赖收集、`Bind` 的安全降级、rebind）**全部建立在一个进程级单例上**：

- 每个 State 的构造 / `Set` / 析构都要穿过 `ModelStore::Instance()->Dispatch(...)`
  → 强制初始化全局单例 + 必须链接 zUI 库。
- 观察者关系不在对象里，而在全局 `std::map<int, std::set<IStateObserver*>>` 里，按 `_bindId` 索引。
- **没有 connection 概念**，通知是按 bindId 广播给整组 observer，无法取消单个订阅 ——
  这就是它的 `SetUpdate` 只能用 lambda 手工链式包装的原因，不是实现偷懒，是架构上做不到。
- 静态析构顺序问题已在代码里显形：`ModelStore::_isDestroy` 这个静态 flag 就是那个补丁。
- `Set` 要构造一个含 `zui::any` 成员的 `Action` 再 dispatch + map 查找 + 虚调用，比直接遍历
  vector 调 function 贵一个数量级。

**结论：`zui::State` 是框架组件，不是库。** 但它的三个核心想法（自动依赖收集、只读句柄、原地
mutate）都不需要那个注册表，可以在 signals2 之上本地化 —— 这就是本实现做的事。

---

## 2. 设计决策（及各自避开的陷阱）

### 2.1 单通道订阅，没有 data / view 之分

`phone::State` 和 `zui::State` 都有 data / view 双通道。**否决理由是决定性的：两份同源实现对通知
顺序的规定是相反的。**

| 实现 | 顺序 |
|---|---|
| `phone::State::SetImpl` | data → view |
| `zui::State::OnUpdate` | view → data |

同源代码对"为什么要分两个通道"给出了互相矛盾的答案 → 这个划分没有承载真实语义。

它实际上把三件正交的事塞进了一个维度：通知优先级、connect 时是否立即触发、set 时选择性跳过部分
订阅者。当前 `connect()` 只建立连接，不立即调用；初始同步由调用方显式 `get()`。第三个
（`SetWithoutUpdateView`）最糟 —— 让**写入方**决定哪些订阅者被通知，直接破坏 observer 契约。

**证据：** `phone::State` 的 data 通道全仓只有 2 个外部调用点，其中一个
（`phone_foreground_dashpad_panel.cpp:1108`）用 data 通道去刷一个 label —— 恰好是 view 通道的职责，
紧邻的下一行又用了 `SetViewUpdate`。一个只有 2 个用户、其中一个还用错了的抽象，说明它没传达清楚语义。

### 2.2 依赖自动收集，用 thread_local 栈而非全局注册表

`observable::get()` 里埋钩子；`computed::recompute()` 试跑 `calc_()` 时压栈，被读到的 state 自己上报。

三个必须保持的细节：

- **是栈不是单槽位。** 嵌套 computed（A 的计算里读 B，B 也是 computed）要能正确归属。
  zUI 的 `RegGuard` 用单个全局 `g_reg_fun`，析构时直接置 `nullptr` 而不是恢复前值 →
  **嵌套 `calc()` 静默丢失依赖追踪**。不要抄那个形状。
- **`thread_local`。** zUI 的 `g_reg_fun` 是普通全局变量。
- **依赖归 `computed` 自己所有**（`deps_` 成员），不是全局 map、也不是被观察者身上。
  这同时修掉了 §1 的第 1 条。

### 2.3 依赖集合只增不减 ← 最反直觉的一条，不要"优化"掉

最初的写法是每次 `recompute` 清空并重建 `deps_`，这样条件分支切换后旧依赖能丢掉。**这是错的。**

`OnDependencyChanged()` 是从依赖 signal 的发射循环里调进来的。此时 `deps_.clear()` 会走到
`signals.h` 里 `signal_slot_connection::disconnect()` 的第一行：

```cpp
void disconnect() {
  *the_slot = nullptr;          // ← 给正在执行的 std::function 赋空值
  ...
}
```

即**销毁正在执行的 lambda 自己的捕获存储**。signals2 用 `signal_lock` / `compact()` 处理了内存
回收（locked 时 `the_slot.release()` 交给 signal 延后删除），但没处理"函数对象在自身调用期间被
重新赋值"这个 UB。

改为只增不减后：分支切换时旧订阅保留，代价是偶尔一次多余重算（被 §2.5 的相等性判断吸收），
换来的是**绝不在发射过程中销毁 slot**。集合规模由 compute 函数能触达的 state 总数封顶，不会无界增长。
`tracked_` 负责去重（同一 state 被读多次只订阅一次）。

行为正确性由测试 7 守着。

### 2.4 `tracking_scope` 必须在赋值和通知之前退出

```cpp
T next = [this] {
  detail::tracking_scope scope(this);
  return static_cast<T>(calc_());
}();                                  // ← 追踪域到此结束
// 之后才比较、赋值、emit
```

否则**订阅者回调里的 `get()` 会被误记成本 computed 的依赖**。用 IIFE 而不是普通块作用域，是为了
同时避免要求计算结果 `next` 默认构造（不使用 `T next{}; { ... next = calc_(); }` 那种双初始化）。`computed` 自身若使用默认构造，`T` 仍须默认可构造；不可默认构造的 `T` 应通过 `computed(std::in_place, args...)` 提供绑定前的合法初值。`state` 同样提供 `state(std::in_place, args...)`，可直接原地构造值。

值类型约束按实际操作表达：`computed<T>` 的核心重算会执行 `value_ = std::move(next)`，因此类级要求 `std::assignable_from<T&, T>`；`state<T>` 不作类级赋值要求，`set(const T&)`、`set(T&&)` 和对应的赋值运算符分别只在该赋值表达式有效时参与重载。这样不可整体赋值的值仍可通过 `mutate()` 原地修改。两个类型的零参数构造只在 `T` 和需要默认生成的 `Equal` 都可默认构造时可用，`in_place` 构造不受此限制。

zUI 的 `calc()` 没有这层隔离 —— 试跑完直接赋值。

### 2.5 相等性是对象级 BinaryPred 策略

`state<T, Equal>` 和 `computed<T, Equal>` 把等价判断作为模板策略保存，默认是
`std::equal_to<>`，并要求它满足 `std::predicate<Equal&, const T&, const T&>`。比较策略在对象整个
生命周期内保持一致，这一点更像 `std::map` 的 `Compare`，而不是每次调用都传 predicate 的
`std::unique`。

默认策略要求 `T` 支持 `operator==`。没有自然相等语义或不能修改的第三方类型必须显式提供比较器
（测试 9）；有状态比较器同样受支持（测试 9b）。空比较器用 `[[no_unique_address]]` 保存，通常不增加
对象大小。`observable<T>` 不携带 Equal，因此使用不同比较策略的 state / computed 仍能统一转换成
`const observable<T>&`。

没有相等性或希望每次都通知时，可以显式使用 `always_notify`。它对任意两个值都返回 `false`，因此
`state<T, always_notify>` 的每次 `set()`、以及 `computed<T, always_notify>` 的每次依赖重算都会提交并
通知。对 computed 使用时要格外谨慎，因为相等性不再能吸收反馈或重复重算（测试 21、21b）。

Equal 返回 `true` 表示两个值属于同一个等价类：新值会被丢弃，既不保存也不通知。这不是
"保存但静默"；后者会让依赖它的 computed 与 state 当前值失去同步。比较器应当稳定、无副作用，并尽量
满足等价关系。

### 2.6 `computed::bind()` 延迟绑定，不在构造函数里计算

如果构造即计算，**成员声明顺序会变成隐式契约** —— 派生成员必须声明在所有依赖之后，否则读到
尚未构造的成员。model struct 里几十个成员，这个约束迟早被违反且难查。

延迟绑定（在 `DoInit()` 里 `bind(...)`）让顺序无关，也让从 `SetComputed(fn, deps...)` 的迁移变成
机械替换（删掉依赖列表即可）。

`bind()` 是一次性操作。第二次绑定会让旧函数发现的依赖继续保留，破坏 §2.3 中“由一个 compute 函数可触达的 state 数量封顶”的约束；Debug 构建会通过 `assert` 直接报告误用。Release 构建不为这一使用错误增加运行时分支，调用方必须遵守只绑定一次的前置条件。

zUI 的 `State(const std::function<T()>&)` 是构造即计算，它靠 `calc()` 那层外部封装绕开。我们没那个包袱。

### 2.7 重入：允许嵌套重算，但作废的结果不许提交（epoch）

> 这一节推翻了早前的 `recomputing_` 方案。旧方案的描述（"环形依赖用标志显式断开"、
> "断环时保留上一个一致的结果"）**是错的**，下面记录为什么，以免有人把它改回去。

**旧方案做了什么。**`computed` 有个 `bool recomputing_`，覆盖 `recompute()` 从进入到 `emit()` 结束
的整段。期间任何依赖变动触发的重算请求，撞上门禁直接 `return` —— **丢弃，不是延后**。

**实测它在每一种重入情形下都给错值**（探针数据，非推理）：

| 情形 | 旧方案结果 | 一致值 |
|---|---|---|
| 订阅者回写依赖 | `4` | 208 |
| 间接反馈（订阅者写另一个 state） | `3` | 32 |
| calc 内先读后写同一依赖 | `2` | 12 |
| 真环 `x = seed + y, y = x` | `x=3, y=3` | 无解 |

真环那条尤其说明问题：**`bind` 一结束就已经不一致**（`x=2, y=1`），根本不是"更新之后才偏"。
`x == seed + y` 和 `y == x` 哪个成立，纯看谁是传播链的最后一环。所谓"断环"是虚构的 ——
标志只是把恰好落在发射期间的那次重算丢掉，剩下什么值全凭顺序。

**根因不是"重入"，是"无条件提交"。**看栈：

```
外层 recompute
  ├ calc_() 读 a=1, b=1                     ← 输入快照
  │    └ b.set(11) → 内层 recompute
  │         └ 读 a=1, b=11 → value_ = 12    ← 更新的结果，已提交
  ├ calc_() 返回 1+1 = 2                    ← 用变更前的快照算出来的
  └ value_ = 2                              ← 无条件赋值，把 12 覆盖回去
```

外层坚持提交一份**输入已经作废**的结果。这是 lost update，跟重入本身无关。

**现在的做法。**每趟 `recompute()` 领一个 epoch，提交前检查自己有没有被取代：

```cpp
const unsigned long long started = ++epoch_;
T next = [this]{ detail::tracking_scope scope(this); return static_cast<T>(calc_()); }();
if (epoch_ != started) return;   // 嵌套重算跑完了，我这份作废
```

嵌套重算**照常执行**（它读到的输入更新，结果更可信），外层安静作废。三行，上表四种情形里
前三种全部变正确，普通传播和相等性门禁不受影响。

两个细节：

- **检查必须在赋值和 `emit()` 之前**。放后面就等于没放。
- **嵌套层级任意深时 epoch 规则仍成立**：每层跟自己的 `started` 比，只有读到最新输入、未被后续
  重算取代的那趟能提交。这些已经作废的计算帧不会发通知；这不等于整个依赖图无中间态，见 §5.3。

收敛的反馈靠相等性门禁自然终止（钳位、规范化这类都能收敛，测试 15 守着）；不收敛的反馈仍是
使用错误。当前不限制递归深度或通知轮数，也不规定恢复策略，见 §5.5。

这是**刻意选的**：静默错值比崩溃难查一个数量级，而且症状是间歇性的 —— 下一次任何无关变动都可能
把它顺手修好（实测），于是表现为"UI 偶尔不刷新"。文档 §1 第 5 条描述旧 `phone::State` 手写依赖
列表的 bug 时，原话正是这句。自动依赖收集干掉了那个成因，`recomputing_` 又从另一条路把它复现了。

见 §5.5。epoch 只负责结果正确性；如果以后增加发散诊断，也必须作为独立机制，不能靠丢更新断环。

### 2.7.1 通知重入：per-observable revision + pending

`state` / `computed` 表示的是**当前值**，不是事件流。因此通知采用 latest-value 语义：减少已经被覆盖的
中间值，但保证收敛后最后一次变化能送到订阅者。若业务要求每个瞬时事件都不能丢，应直接使用
`signal2`。

每个 `observable` 保存 `revision_`、`emitting_` 和 `pending_emit_`。每次 `set` / `mutate` / `notify`
导致发射时先递增 revision；若同一个对象正在通知，只标记 pending，不递归调用自己的 signal。`emit()`
通过 signal 的 iterator 逐个调用原始 callback，并在调用下一个 callback 前检查当前轮的 revision 是否仍然
有效。revision 一旦改变就结束旧轮，随后只为最新 revision 再跑一轮。

例子（A 先连接，B 后连接）：

```text
state.set(1)
  A(1)
    state.set(2)   -> value 立即变为 2，只标记 pending
    state.set(3)   -> 2 被 3 覆盖
  B(旧 revision)  -> 跳过
  A(3)
  B(3)
state.set(1) 返回
```

在 callback、计算函数和 mutator 都正常返回、传播最终收敛的前提下，契约是：

- callback 真正执行时，参数与当时的 `get()` 一致；
- 同一对象已经过期的中间 revision 不继续发送给后续订阅者；
- 参与本次传播且仍连接的订阅者一定收到最终稳定 revision；
- 最外层更新返回前，同步触发的 pending 通知已经清空；
- 可达的 bound `computed` 最终值与其当前依赖重新计算结果处于同一个 Equal 等价类。

不保证每个订阅者看到相同的中间序列；连接顺序仍决定谁在值被覆盖前已经看过它。跨多个 observable 的
菱形传播也仍可能产生 glitch（§5.3）。若任一用户 callback 抛异常，通知立即中止并向外重抛，上述
“最终通知送达”保证不适用。

`connect()` 只观察连接后的通知，不隐式调用新 callback。需要初始同步时，调用方显式读取当前值，
例如先执行 `label.SetText(state.get())`，再保存 `state.connect(...)` 返回的 connection。库是单线程的，
两步之间没有并发更新窗口。

### 2.8 `state::get()` 返 `const T&`，不做 `operator T()`

`phone::State` 和 `zui::State` 都有非 explicit 的 `operator T()`。配合 `operator=(const T&)`
容易写出意外的重载解析结果，而且每次转换都拷贝。只留 `get()`。

`zui::State::Get()` 返回 `T const`（按值，每读必拷贝）—— 那是它 `weak_ptr` 设计逼出来的必然结果
（`lock()` 拿到的临时 shared_ptr 出函数就释放，返回引用不安全）。我们不做 weak 视图，所以可以返引用。
见 §3.1。

### 2.9 `state` / `computed` 不可拷贝不可移动

`observable` 删掉拷贝构造（连带删掉隐式移动）。**必须保持** —— `tracked_` 存的是 `const void*`
地址，slot 捕获的是 tracker 裸指针，对象地址是身份的一部分。

`zui::State` 的拷贝构造是**共享 `_val`**（注释承认是为 `Loop` 的单项刷新特意做的）。
"拷贝之后两个对象指向同一个值"是极度反直觉的语义，不要抄。

### 2.10 `notify()` 的存在理由

`set` 有相等性门禁，所以当 `T` 是指针 / 句柄、**指向的对象内部变了但指针没变**时，`set` 会被判定为
无变化而静默跳过。`notify()` 是这种场景的逃生口。原代码里 `model_.channel`、
`model_.transfer_summary` 就是这类指针型 state。

（`phone::State::UpdateView()` 是零调用的投机 API，`notify()` 不同 —— 它有具体场景。）

---

## 3. 明确否决的方案（不要重新提出）

### 3.1 `Ref<T>` / `Bind<T>`（weak_ptr 只读句柄）—— 不做

zUI 的 `Bind` 解决三个问题，逐条核对后**三个在这里都不成立或已被覆盖**：

| Bind 的用途 | 在这里 |
|---|---|
| 生命周期安全（源头死了不崩） | `connection` 已覆盖回调悬空；**读**已死 state 的路径在现有所有权模型下不存在 |
| 常量/绑定统一参数（`Text("hi")` 与 `Text(state)` 都能编） | 这是**声明式组件构造**的需求。DuiLib 控件从 XML 创建后命令式绑定，没有这个构造参数 |
| rebind（列表项复用时重指向另一个 State） | zUI view tree diff 的需求，当前无此模式 |

**所有权分析**（否决的依据）：

- model（一堆 state）和 DuiLib 控件**同属一个 panel**，同生共死。
- 跨对象的例子（`btn_transfer_summary_->GetModel().visible` 被 panel 观察）方向是
  **state 先死、observer 后死** → signals2 天然安全（connection 持 `weak_ptr<signal_detail>`）。
- 跨模块的典型场景是"service 持 state，多个 panel 观察"，仍然是 state 活得更久。
- 反方向（长命对象直接持有短命 state 并读它）本身是设计问题，该修那个设计。

**成本**（如果做）：`state` 内部必须改成 `shared_ptr<T>` → 每个 state 多一次堆分配 + 一次间接寻址
（一个 16 成员的 model 就是 16 次额外 `make_shared`）；`Ref::Get()` 必须按值返回 →
`state<CString>` 每次读都拷贝；每个 `Ref` 额外存一份 `T fallback_`；最贵的是**概念负担**
（使用者每次要判断该用哪个句柄 —— data/view 双通道已经因为多余的选择被用错过一次了）。

**现在不做没有沉没成本**：`T value_` → `shared_ptr<T> value_` 是纯内部布局改动，
`get` / `set` / `connect` / `mutate` 签名一个都不变（header-only 模板，重编即可）。

**三个触发条件，出现任一个再回来加**：
1. 需要列表项复用 + rebind；
2. 出现确有必要的"长命对象读短命 state"且不是设计错误；
3. 要做声明式组件 API（那基本等于在往 zUI 方向走，那时更该讨论直接用 zUI）。

`observable<T>` 顺手顶掉了 Bind 唯一成立的那个角色（只读句柄）：`const observable<T>&` 作参数类型，
`state` 和 `computed` 都能传，零分配、返引用。

两个 `connect` 重载都是 **const**（`sig_` 是 `mutable`，见 §4.5）—— 这是上面那句成立的前提：
只读句柄必须能"观察"，而不只是"读一次"。const 掉之后 `const observable<T>&` 才是完整的观察者入口，
否则拿到它的函数只能 `get()`，还得把非 const 引用传下去，这个抽象就白给了。测试 12b 守着这条。

### 3.2 `ScopedConnections` RAII 容器 —— 不做（连别名也不留）

比 `std::vector<signals2::connection>` 多给的东西趋近于零：

- RAII 自动断开 —— vector 本来就有；
- 禁止拷贝 —— `connection` 是 move-only（`final`、无用户声明的拷贝/析构、单个 `unique_ptr` 成员），
  `vector<connection>` **自动**不可拷贝；
- `operator+=` 比 `emplace_back` 短 —— 化妆。

原本的论证是"让正确写法比错误写法省事"，但**删掉 `SetUpdate` 那族之后就没有错误写法了**，
`[[nodiscard]]` 会逼你接住返回值。论证自己塌了。

早前的折中是保留一个 `using ConnectionScope = std::vector<signals2::connection>` 别名，
**现在连这个也删掉了**：同一个论证对别名一样成立 —— 它不增加任何保证，只是给一个人人都认识的
标准容器换了个只在本库里存在的名字，读代码的人还得回头查它到底是什么。直接写
`std::vector<signals2::connection>`。

（附：**vector 扩容对 connection 是安全的** —— `connection` 只有一个 `unique_ptr` 指向堆上的
`signal_slot_connection`，signal 侧注册的是指向堆上 `the_slot` 的裸指针，移动外层不影响堆对象地址。）

### 3.3 `SetWithoutNotify` / 静默 set —— 不做

见 §5.1。

### 3.4 表达式模板 DSL（zUI 的 `expression.h`）—— 不做

`stateA + stateB` 构造惰性 `BinaryOpExpr`。只支持 `+ - * /`，无比较和逻辑运算，
`ValueType` 用 `std::common_type_t` 混合类型时行为意外。有了自动依赖收集之后，
`bind([&]{ return a.get() + b.get(); })` 已经足够可读，不值得为省两个 `.get()` 引入一层模板机制。

---

## 4. 不能破坏的不变量

改这份代码前先读这一节。

1. **`tracking_scope` 在 `emit()` 之前退出**（§2.4）。破坏 → 订阅者的读被误记为依赖。
2. **`deps_` 只增不减**（§2.3）。破坏 → 在 signal 发射期间销毁正在执行的 slot，UB。
2b. **epoch 检查在赋值和 `emit()` 之前**（§2.7）。破坏 → 外层用作废的输入覆盖嵌套重算的新结果。
   不要"顺手"加回一个重入门禁把嵌套重算挡掉 —— 那是被推翻的旧方案，四种重入情形全给错值。
2c. **`emit()` 逐个调用 slot，并在调用下一个 slot 前检查当前轮 revision 仍然有效**（§2.7.1）。
   revision 变化时必须结束旧轮，`pending_emit_` 必须随后 flush；破坏任一条都会漏掉最终值或继续发送
   过期值。
3. **`state` / `computed` 不可拷贝不可移动**（§2.9）。破坏 → tracker 里的地址身份失效。
4. **`observable` 的析构是 protected 非虚，派生类 `final`**。不要加虚函数（`dependency_tracker` 的三个虚函数
   只在 `computed` 上，且是 private 继承）。
5. **`sig_` 是 `mutable`**，因为 `get()` 是 const 但要 `connect`。
6. **两个公开头文件都必须自包含。**`signals.h` 和 `state.h` 各自使用 `std::find`，因此各自显式包含
   `<algorithm>`；不要重新引入依赖包含顺序才能编译的隐式前提。

---

## 5. 已知限制

### 5.1 没有双向绑定

刻意不提供"静默 set"。编辑框在自己的 change handler 里回写模型会回声刷新、光标跳。

**为什么不给静默 set**：它会让依赖该值的 `computed` 失去同步，模型内部变得不一致 ——
比回声问题更糟。（`phone::State::SetWithoutUpdateView` 至少还更新 computed，但它的做法是让写入方
挑订阅者，破坏 observer 契约，见 §2.1。）

**当前建议**：在调用点用一个局部 flag 挡住回声。

**如果真需要**：正确方向是给 `connection` 加 scoped block / unblock（"这次通知跳过这条订阅"），
但那要改 `signals.h`。不要用静默 set 凑。

### 5.2 依赖追踪不跨 DLL 边界（有条件）

追踪栈是 per-module 的 `thread_local`。

- **没问题**：compute 函数直接读另一个模块的 state —— 那个 `get()` 在**调用方**模块实例化，
  用的是同一个栈。
- **有问题**：`get()` 发生在另一个模块**导出的非 inline 函数内部** → 该模块的栈是空的，追踪不到。

约束：把一个 `computed` 和它的 compute 函数放在同一个模块里。

### 5.3 无 glitch 消除 / 无批量提交

菱形依赖（一个 computed 依赖两个 dep，同一逻辑操作里两个都变）会重算两次，中间那次订阅者看到
不一致的中间态。§2.7.1 的 revision 只消除**同一个 observable 发射期间**已经作废的通知，不会把
跨对象的整张依赖图变成事务。

这是 §2.1 里 data/view 双通道**想**解决的问题（先让 computed 重算完，再刷 view），但两级硬编码
优先级解决不了链式/菱形依赖，所以没有保留。

**正确方向（如果将来需要）**：批量提交 —— `Batch([&]{ ... })`，作用域结束后统一 flush 订阅者并去重。
**不要**回到加通道的路上。

### 5.4 非线程安全

与 signals2 本身一致。单线程使用（实践中即 UI 线程）。

### 5.5 发散反馈是使用错误，当前不设护栏

见 §2.7。**收敛的反馈是支持的**：值不再变化就会被相等性门禁吸收，per-observable pending 也会在
最终 revision 发完后清空。订阅者里做钳位、规范化这类修正会正常收敛（测试 15、17、22）。

不收敛的反馈没有一致解，例如：

```cpp
x.bind([&]{ return seed.get() + y.get(); });
y.bind([&]{ return x.get(); });
```

当前实现不检查递归深度，也不限制同一个 observable 的 pending 通知轮数。发散传播可能耗尽栈，
也可能一直停留在 pending 循环中。库不静默截断传播，因为停在任意旧值会制造更难发现的不一致。

如果以后加入 Debug assert，应把它作为独立的诊断机制：只报告“很可能发散”，不丢更新、不参与结果
选择，也不把某个固定阈值描述成真正的环检测。

---

## 6. 从 `phone::State` 的迁移映射

| 旧 | 新 |
|---|---|
| `SetViewUpdateWithConnection(ctrl, &C::M)` | 显式用 `get()` 初始化控件，再 `connect(ctrl, &C::M)` |
| `SetViewUpdateWithConnection(cb)` | 显式用 `get()` 初始化，再 `connect(cb)` |
| `SetUpdateWithConnection(cb)` | `connect(cb)` |
| `SetUpdate(...)` / `SetViewUpdate(...)`（连接存错地方） | **删除**，改用 `connect` 并由订阅方持有 connection |
| `SetComputed(fn, &dep1, &dep2)` | `bind(fn)` —— 依赖列表删掉 |
| `SetWithoutUpdateView` / `UpdateView()` | **删除**（原本零调用） |
| `Get()` / `Set()` / `operator=(const T&)` | `get()` / `set()`；赋值运算符不变 |
| `operator T()` 隐式转换 | **删除**，显式 `get()` |
| 5 个成员函数重载 | 1 个 `connect(C*, M)`，靠隐式转换覆盖 `void(const T&)` / `void(T)` / `void(LPCTSTR)` |
| — | 新增 `mutate`、`peek`、`notify`、`recompute`、`bound` |

**原调用点规模**（如需回去改）：`SetViewUpdate*` 约 26 处外部调用，`SetUpdate*` 2 处，
分布在 5 个文件：`phone_foreground_summary_bubble_window.cpp`(12)、
`phone_network_statistics_popup_window.cpp`(9)、`phone_foreground_dashpad_panel.cpp`(2+1)、
`transfer_summary_button.cpp`(2)、`phone_channel_list_window.cpp`(1)。
（早前口头说过的 "57 处" 是含 `State.h`/`State.inl` 内部声明与定义的总匹配数，外部实际调用点是上述数字。）

**迁移时要注意的一个既存 bug 形态**：原代码里 `SetComputed` 是在所有 view 绑定**之后**才调用的。
因为 view 通道 connect 时会立即触发、而 `set` 有相等性门禁，所以当 computed 算出来的值恰好等于
`T()` 时，一次通知都不会发，控件会停在 XML 的初值上。迁移时应先 `bind()`，再用 `get()` 显式初始化
控件，最后 `connect()` 后续变化；逐个核对绑定和初始化顺序。

---

## 7. 测试矩阵

`test/state_test.cpp`，27 组 Catch `TEST_CASE`，链进 `signals2_tests`。带 ★ 的是守护 §4 不变量的，
重构后必须仍然通过。表里的编号对应源文件里的 `// ---- N. ----` 注释（Catch 用例名是描述性的，
不带编号）。

| # | 场景 | 守护什么 |
|---|---|---|
| 1 | set / connect / 相等性门禁 / `operator=` | 基本语义 |
| 2 | `connect()` 不立即触发 | 只观察连接后的通知；初始同步显式完成 |
| 3 | `std::vector<connection>` 析构后不再回调 | connection RAII |
| 4 | 成员函数重载 + 隐式转换 + 零参可调用对象 | 单个 `connect` 重载覆盖多签名 |
| 5 | computed 自动依赖发现（2 个依赖） | §2.2 |
| 6 | 链式 computed（A → B → C） | 嵌套追踪栈（§2.2） |
| 7 ★ | 条件依赖分支切换 + 旧依赖变化无害 | §2.3 只增不减 |
| 8 | 容器上的 `mutate` | 原地修改 + 无条件通知 |
| 9 | 无 operator== 的类型显式提供 BinaryPred | §2.5 严格默认策略 |
| 9b | computed 保存有状态 BinaryPred | §2.5 对象级比较策略 |
| 9c | state / computed 使用 std::in_place 构造值 | 不可移动 state 值、不可默认构造 computed 值、有状态 Equal 和赋值能力约束 |
| 10 ★ | 收敛的反馈环停在不动点 | §2.7 相等性门禁即终止判据 |
| 11 | `peek` 不建立依赖 | 逃生口语义 |
| 12 | `const observable<T>&` 作参数（读） | §3.1 的替代方案 |
| 12b | `const observable<T>&` 上订阅 callable | `connect() const` 重载之一，见 §3.1 末尾 |
| 12c | `const observable<T>&` 上订阅成员函数 | `connect() const` 重载之二（测试 4 走非 const，盖不住） |
| 13 | observer 先死 | 生命周期方向一 |
| 14 ★ | **state 先死、computed 后死** | 生命周期方向二；signals2 的 `weak_ptr<signal_detail>` 兜底 |
| 15 ★ | 订阅者回写依赖 → 嵌套重算落地 | §2.7 epoch；旧方案这里停在 4（应 208）|
| 16 ★ | 作废的计算结果不许提交 | §2.7 epoch；旧方案这里停在 2（应 12）|
| 17 ★ | state 回调连续写入时合并中间值 | §2.7.1 revision + pending；后订阅者只见最终值 |
| 18 ★ | computed 通知中修改另一依赖 | 过期 revision 跳过，所有订阅者收到最终值 |
| 19 | 重入 `notify()` | 同值的新 revision 也按 pending 串行发送 |
| 20 | 重入 `mutate()` | 容器中间态被最终值覆盖 |
| 21 | `always_notify` 用于无相等性的 state | 每次 set 都提交并通知 |
| 21b | `always_notify` 用于 computed | 等价结果也提交并通知 |
| 22 ★ | 回调参数始终等于调用时的 `get()` | revision 检查发生在用户 callback 之前 |

**测试 14 的覆盖力比看上去弱，别当保险：**它只覆盖了**读缓存值**这条路径。那个 compute 函数捕获的
`&tmp` 在块结束后已经悬空，
  测试之后从没再执行过它 —— 因为 `tmp` 一死，它的 signal 也没了，没人能触发重算。
  真调 `c.recompute()` 就是 UB。危险形态是"长命依赖 A + 短命依赖 B，B 先死，之后 A 变化触发重算"
  —— 这条路径没有测试守着。**规则：`computed` 不得比它的 compute 函数能读到的任何 state 活得久。**
  §3.1 否决 `Ref<T>` 的整个论证就站在这条规则上，但那里只是把它当成对现状的观察陈述，
  没有作为使用者必须遵守的前置条件写出来。

---

## 8. 待办 / 悬而未决

1. ~~**命名空间叫 `states`**~~ —— 已定为 `signals2`，与 `signals.h` 同一个。
2. ~~**`state_test.cpp` 还是裸 main + 手写 check**~~ —— 已转成 Catch `TEST_CASE` 并接入
   `signals2_tests`（本仓库用的是 Catch2 v3.15.3 amalgamated，不是原计划的 gtest）。
3. ~~**`signals.h` 缺 `#include <algorithm>`**~~ —— 已补齐，`signals.h` / `state.h` 现在都显式
   包含自己使用的标准库设施，不再依赖包含顺序。
4. **原 `zPhoneUI` 的 26+2 个调用点还没迁**（§6），以及原 `State.h`/`State.inl` 的删除。
5. **§5.1 双向绑定**和**§5.3 批量提交**是两个已知的功能缺口，都有明确的"正确方向"记录在案，
   等真实需求出现再做。
6. **战略问题（未解决）**：win-client 里 `zui::State`/`Bind`/`calc` 几乎无采用
   （只有 2 个 setting panel 引用，`zPhoneUI` 里 0 处 `#include <zUI/...>`）。如果 DuiLib 侧中期
   会迁到 zUI，那这个库会变成第三套要维护的响应式基础设施。这个判断需要看 zUI 的 roadmap。
7. **发散诊断暂缓**：以后可考虑 Debug 下的递归深度 / pending 轮数 assert，但应独立设计，不能改变
   latest-value 通知结果或用截断传播伪装成环处理。
