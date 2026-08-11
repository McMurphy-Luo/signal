# `states::State` / `Computed` — 设计上下文与交接说明

> 这份文档记录的是**代码里看不出来的东西**：为什么是这个形状、哪些"更自然"的写法是陷阱、
> 哪些方案被明确否决过。代码本身能回答"是什么"，这里回答"为什么"和"别改成什么"。
>
> 迁出自 `client-5.x-release`，原始参考实现路径见 §1（新库里已无法回溯，故在此记录）。

---

## 0. 交付物

| 文件 | 说明 |
|---|---|
| `state.h` | 实现，header-only，依赖 `signals.h`（signals2） |
| `state_test.cpp` | 14 组行为测试，见 §7 |
| 本文档 | 设计上下文 |

验证状态：MSVC C++20 `/W4 /permissive-` 零警告编译通过，14 组测试全过。

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
Mutate）都不需要那个注册表，可以在 signals2 之上本地化 —— 这就是本实现做的事。

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
订阅者。前两个应该是参数（现在 `FireNow` 就是），第三个（`SetWithoutUpdateView`）最糟 ——
让**写入方**决定哪些订阅者被通知，直接破坏 observer 契约。

**证据：** `phone::State` 的 data 通道全仓只有 2 个外部调用点，其中一个
（`phone_foreground_dashpad_panel.cpp:1108`）用 data 通道去刷一个 label —— 恰好是 view 通道的职责，
紧邻的下一行又用了 `SetViewUpdate`。一个只有 2 个用户、其中一个还用错了的抽象，说明它没传达清楚语义。

### 2.2 依赖自动收集，用 thread_local 栈而非全局注册表

`Observable::Get()` 里埋钩子；`Computed::Recompute()` 试跑 `calc_()` 时压栈，被读到的 State 自己上报。

三个必须保持的细节：

- **是栈不是单槽位。** 嵌套 Computed（A 的计算里读 B，B 也是 Computed）要能正确归属。
  zUI 的 `RegGuard` 用单个全局 `g_reg_fun`，析构时直接置 `nullptr` 而不是恢复前值 →
  **嵌套 `calc()` 静默丢失依赖追踪**。不要抄那个形状。
- **`thread_local`。** zUI 的 `g_reg_fun` 是普通全局变量。
- **依赖归 `Computed` 自己所有**（`deps_` 成员），不是全局 map、也不是被观察者身上。
  这同时修掉了 §1 的第 1 条。

### 2.3 依赖集合只增不减 ← 最反直觉的一条，不要"优化"掉

最初的写法是每次 `Recompute` 清空并重建 `deps_`，这样条件分支切换后旧依赖能丢掉。**这是错的。**

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
换来的是**绝不在发射过程中销毁 slot**。集合规模由 compute 函数能触达的 State 总数封顶，不会无界增长。
`tracked_` 负责去重（同一 State 被读多次只订阅一次）。

行为正确性由测试 7 守着。

### 2.4 `TrackScope` 必须在赋值和通知之前退出

```cpp
T next = [this] {
  detail::TrackScope scope(this);
  return static_cast<T>(calc_());
}();                                  // ← 追踪域到此结束
// 之后才比较、赋值、Emit
```

否则**订阅者回调里的 `Get()` 会被误记成本 computed 的依赖**。用 IIFE 而不是普通块作用域，是为了
同时支持不可默认构造的 `T`（避免 `T next{}; { ... next = calc_(); }` 那种双初始化）。

zUI 的 `calc()` 没有这层隔离 —— 试跑完直接赋值。

### 2.5 相等性判断用 `if constexpr (std::equality_comparable<T>)`

不是硬写 `!=`。没有 `==` 的类型照样能实例化，只是每次都通知（测试 9）。这比"编不过"和"沉默地
用错误的比较语义"都好。

### 2.6 `Computed::Bind()` 延迟绑定，不在构造函数里计算

如果构造即计算，**成员声明顺序会变成隐式契约** —— 派生成员必须声明在所有依赖之后，否则读到
尚未构造的成员。model struct 里几十个成员，这个约束迟早被违反且难查。

延迟绑定（在 `DoInit()` 里 `Bind(...)`）让顺序无关，也让从 `SetComputed(fn, deps...)` 的迁移变成
机械替换（删掉依赖列表即可）。

zUI 的 `State(const std::function<T()>&)` 是构造即计算，它靠 `calc()` 那层外部封装绕开。我们没那个包袱。

### 2.7 环形依赖用 `recomputing_` 标志显式断开

`phone::State` 和 `zui::State` 都只靠"值收敛"隐式终止。真环会爆栈。测试 10 建了一个真环。

断环时保留上一个一致的结果，不递归。

### 2.8 `State::Get()` 返 `const T&`，不做 `operator T()`

`phone::State` 和 `zui::State` 都有非 explicit 的 `operator T()`。配合 `operator=(const T&)`
容易写出意外的重载解析结果，而且每次转换都拷贝。只留 `Get()`。

`zui::State::Get()` 返回 `T const`（按值，每读必拷贝）—— 那是它 `weak_ptr` 设计逼出来的必然结果
（`lock()` 拿到的临时 shared_ptr 出函数就释放，返回引用不安全）。我们不做 weak 视图，所以可以返引用。
见 §3.1。

### 2.9 `State` / `Computed` 不可拷贝不可移动

`Observable` 删掉拷贝构造（连带删掉隐式移动）。**必须保持** —— `tracked_` 存的是 `const void*`
地址，slot 捕获的是 tracker 裸指针，对象地址是身份的一部分。

`zui::State` 的拷贝构造是**共享 `_val`**（注释承认是为 `Loop` 的单项刷新特意做的）。
"拷贝之后两个对象指向同一个值"是极度反直觉的语义，不要抄。

### 2.10 `Notify()` 的存在理由

`Set` 有相等性门禁，所以当 `T` 是指针 / 句柄、**指向的对象内部变了但指针没变**时，`Set` 会被判定为
无变化而静默跳过。`Notify()` 是这种场景的逃生口。原代码里 `model_.channel`、
`model_.transfer_summary` 就是这类指针型 State。

（`phone::State::UpdateView()` 是零调用的投机 API，`Notify()` 不同 —— 它有具体场景。）

---

## 3. 明确否决的方案（不要重新提出）

### 3.1 `Ref<T>` / `Bind<T>`（weak_ptr 只读句柄）—— 不做

zUI 的 `Bind` 解决三个问题，逐条核对后**三个在这里都不成立或已被覆盖**：

| Bind 的用途 | 在这里 |
|---|---|
| 生命周期安全（源头死了不崩） | `connection` 已覆盖回调悬空；**读**已死 State 的路径在现有所有权模型下不存在 |
| 常量/绑定统一参数（`Text("hi")` 与 `Text(state)` 都能编） | 这是**声明式组件构造**的需求。DuiLib 控件从 XML 创建后命令式绑定，没有这个构造参数 |
| rebind（列表项复用时重指向另一个 State） | zUI view tree diff 的需求，当前无此模式 |

**所有权分析**（否决的依据）：

- model（一堆 State）和 DuiLib 控件**同属一个 panel**，同生共死。
- 跨对象的例子（`btn_transfer_summary_->GetModel().visible` 被 panel 观察）方向是
  **State 先死、observer 后死** → signals2 天然安全（connection 持 `weak_ptr<signal_detail>`）。
- 跨模块的典型场景是"service 持 State，多个 panel 观察"，仍然是 State 活得更久。
- 反方向（长命对象直接持有短命 State 并读它）本身是设计问题，该修那个设计。

**成本**（如果做）：`State` 内部必须改成 `shared_ptr<T>` → 每个 State 多一次堆分配 + 一次间接寻址
（一个 16 成员的 model 就是 16 次额外 `make_shared`）；`Ref::Get()` 必须按值返回 →
`State<CString>` 每次读都拷贝；每个 `Ref` 额外存一份 `T fallback_`；最贵的是**概念负担**
（使用者每次要判断该用哪个句柄 —— data/view 双通道已经因为多余的选择被用错过一次了）。

**现在不做没有沉没成本**：`T value_` → `shared_ptr<T> value_` 是纯内部布局改动，
`Get` / `Set` / `Subscribe` / `Mutate` 签名一个都不变（header-only 模板，重编即可）。

**三个触发条件，出现任一个再回来加**：
1. 需要列表项复用 + rebind；
2. 出现确有必要的"长命对象读短命 State"且不是设计错误；
3. 要做声明式组件 API（那基本等于在往 zUI 方向走，那时更该讨论直接用 zUI）。

`Observable<T>` 顺手顶掉了 Bind 唯一成立的那个角色（只读句柄）：`const Observable<T>&` 作参数类型，
`State` 和 `Computed` 都能传，零分配、返引用。

### 3.2 `ScopedConnections` RAII 容器 —— 不做

比 `std::vector<signals2::connection>` 多给的东西趋近于零：

- RAII 自动断开 —— vector 本来就有；
- 禁止拷贝 —— `connection` 是 move-only（`final`、无用户声明的拷贝/析构、单个 `unique_ptr` 成员），
  `vector<connection>` **自动**不可拷贝；
- `operator+=` 比 `emplace_back` 短 —— 化妆。

原本的论证是"让正确写法比错误写法省事"，但**删掉 `SetUpdate` 那族之后就没有错误写法了**，
`[[nodiscard]]` 会逼你接住返回值。论证自己塌了。只保留一个 `using ConnectionScope = std::vector<...>` 别名。

（附：**vector 扩容对 connection 是安全的** —— `connection` 只有一个 `unique_ptr` 指向堆上的
`signal_slot_connection`，signal 侧注册的是指向堆上 `the_slot` 的裸指针，移动外层不影响堆对象地址。）

### 3.3 `SetWithoutNotify` / 静默 Set —— 不做

见 §5.1。

### 3.4 表达式模板 DSL（zUI 的 `expression.h`）—— 不做

`stateA + stateB` 构造惰性 `BinaryOpExpr`。只支持 `+ - * /`，无比较和逻辑运算，
`ValueType` 用 `std::common_type_t` 混合类型时行为意外。有了自动依赖收集之后，
`Bind([&]{ return a.Get() + b.Get(); })` 已经足够可读，不值得为省两个 `.Get()` 引入一层模板机制。

---

## 4. 不能破坏的不变量

改这份代码前先读这一节。

1. **`TrackScope` 在 `Emit()` 之前退出**（§2.4）。破坏 → 订阅者的读被误记为依赖。
2. **`deps_` 只增不减**（§2.3）。破坏 → 在 signal 发射期间销毁正在执行的 slot，UB。
3. **`State` / `Computed` 不可拷贝不可移动**（§2.9）。破坏 → tracker 里的地址身份失效。
4. **`Observable` 的析构是 protected 非虚，派生类 `final`**。不要加虚函数（`ITracker` 的三个虚函数
   只在 `Computed` 上，且是 private 继承）。
5. **`sig_` 是 `mutable`**，因为 `Get()` 是 const 但要 `connect`。
6. **include 顺序：`<algorithm>` 在 `"signals.h"` 之前。**
   `signals.h` 在 `signal_detail::remove()` 里用了 `std::find` 但**没有** include `<algorithm>` ——
   目前靠外部先引入才能在 MSVC 之外的标准库上编过。顺手修 `signals.h` 更好，但在修之前别动这个顺序。

---

## 5. 已知限制

### 5.1 没有双向绑定

刻意不提供"静默 Set"。编辑框在自己的 change handler 里回写模型会回声刷新、光标跳。

**为什么不给静默 Set**：它会让依赖该值的 `Computed` 失去同步，模型内部变得不一致 ——
比回声问题更糟。（`phone::State::SetWithoutUpdateView` 至少还更新 computed，但它的做法是让写入方
挑订阅者，破坏 observer 契约，见 §2.1。）

**当前建议**：在调用点用一个局部 flag 挡住回声。

**如果真需要**：正确方向是给 `connection` 加 scoped block / unblock（"这次通知跳过这条订阅"），
但那要改 `signals.h`。不要用静默 Set 凑。

### 5.2 依赖追踪不跨 DLL 边界（有条件）

追踪栈是 per-module 的 `thread_local`。

- **没问题**：compute 函数直接读另一个模块的 State —— 那个 `Get()` 在**调用方**模块实例化，
  用的是同一个栈。
- **有问题**：`Get()` 发生在另一个模块**导出的非 inline 函数内部** → 该模块的栈是空的，追踪不到。

约束：把一个 `Computed` 和它的 compute 函数放在同一个模块里。

### 5.3 无 glitch 消除 / 无批量提交

菱形依赖（一个 computed 依赖两个 dep，同一逻辑操作里两个都变）会重算两次，中间那次订阅者看到
不一致的中间态。

这是 §2.1 里 data/view 双通道**想**解决的问题（先让 computed 重算完，再刷 view），但两级硬编码
优先级解决不了链式/菱形依赖，所以没有保留。

**正确方向（如果将来需要）**：批量提交 —— `Batch([&]{ ... })`，作用域结束后统一 flush 订阅者并去重。
**不要**回到加通道的路上。

### 5.4 非线程安全

与 signals2 本身一致。单线程使用（实践中即 UI 线程）。

---

## 6. 从 `phone::State` 的迁移映射

| 旧 | 新 |
|---|---|
| `SetViewUpdateWithConnection(ctrl, &C::M)` | `Subscribe(ctrl, &C::M, FireNow::Yes)` |
| `SetViewUpdateWithConnection(cb)` | `Subscribe(cb, FireNow::Yes)` |
| `SetUpdateWithConnection(cb)` | `Subscribe(cb)` |
| `SetUpdate(...)` / `SetViewUpdate(...)`（连接存错地方） | **删除**，改用 `Subscribe` 并由订阅方持有 connection |
| `SetComputed(fn, &dep1, &dep2)` | `Bind(fn)` —— 依赖列表删掉 |
| `SetWithoutUpdateView` / `UpdateView()` | **删除**（原本零调用） |
| `Get()` / `Set()` / `operator=(const T&)` | 不变 |
| `operator T()` 隐式转换 | **删除**，显式 `Get()` |
| 5 个成员函数重载 | 1 个 `Subscribe(C*, M, FireNow)`，靠隐式转换覆盖 `void(const T&)` / `void(T)` / `void(LPCTSTR)` |
| — | 新增 `Mutate`、`Peek`、`Notify`、`Recompute`、`IsBound` |

**原调用点规模**（如需回去改）：`SetViewUpdate*` 约 26 处外部调用，`SetUpdate*` 2 处，
分布在 5 个文件：`phone_foreground_summary_bubble_window.cpp`(12)、
`phone_network_statistics_popup_window.cpp`(9)、`phone_foreground_dashpad_panel.cpp`(2+1)、
`transfer_summary_button.cpp`(2)、`phone_channel_list_window.cpp`(1)。
（早前口头说过的 "57 处" 是含 `State.h`/`State.inl` 内部声明与定义的总匹配数，外部实际调用点是上述数字。）

**迁移时要注意的一个既存 bug 形态**：原代码里 `SetComputed` 是在所有 view 绑定**之后**才调用的。
因为 view 通道 connect 时会立即触发、而 `Set` 有相等性门禁，所以当 computed 算出来的值恰好等于
`T()` 时，一次通知都不会发，控件会停在 XML 的初值上。用 `Bind()` + `Subscribe(..., FireNow::Yes)`
的顺序（先 Bind 再 Subscribe）可以避免；迁移时逐个核对绑定顺序。

---

## 7. 测试矩阵

`state_test.cpp`，14 组。带 ★ 的是守护 §4 不变量的，重构后必须仍然通过。

| # | 场景 | 守护什么 |
|---|---|---|
| 1 | Set / Subscribe / 相等性门禁 / `operator=` | 基本语义 |
| 2 | `FireNow::Yes` 立即触发 | 初始同步 |
| 3 | `ConnectionScope` 析构后不再回调 | connection RAII |
| 4 | 成员函数重载 + 隐式转换 + 零参可调用对象 | 单个 `Subscribe` 重载覆盖多签名 |
| 5 | Computed 自动依赖发现（2 个依赖） | §2.2 |
| 6 | 链式 computed（A → B → C） | 嵌套追踪栈（§2.2） |
| 7 ★ | 条件依赖分支切换 + 旧依赖变化无害 | §2.3 只增不减 |
| 8 | 容器上的 `Mutate` | 原地修改 + 无条件通知 |
| 9 | 无 `operator==` 的类型 | §2.5 |
| 10 ★ | 环形依赖不爆栈 | §2.7 |
| 11 | `Peek` 不建立依赖 | 逃生口语义 |
| 12 | `const Observable<T>&` 作参数 | §3.1 的替代方案 |
| 13 | observer 先死 | 生命周期方向一 |
| 14 ★ | **State 先死、Computed 后死** | 生命周期方向二；signals2 的 `weak_ptr<signal_detail>` 兜底 |

测试 14 是最重要的一条 —— 它验证了 §3.1"不需要 `Ref`"的核心论断：依赖的 State 销毁后，
`Computed` 保留最后的值且不崩。

---

## 8. 待办 / 悬而未决

1. **命名空间叫 `states`** —— 当初选它是为了与 `signals` 并列。迁到独立库后可能有更合适的名字，
   改是一次全局替换。
2. **`state_test.cpp` 还是裸 main + 手写 check**，需要接到新库的测试框架（原计划是 gtest）。
3. **`signals.h` 缺 `#include <algorithm>`**（§4.6）—— 独立 bug，建议顺手修掉，修完 §4.6 的
   include 顺序约束就可以放松。
4. **原 `zPhoneUI` 的 26+2 个调用点还没迁**（§6），以及原 `State.h`/`State.inl` 的删除。
5. **§5.1 双向绑定**和**§5.3 批量提交**是两个已知的功能缺口，都有明确的"正确方向"记录在案，
   等真实需求出现再做。
6. **战略问题（未解决）**：win-client 里 `zui::State`/`Bind`/`calc` 几乎无采用
   （只有 2 个 setting panel 引用，`zPhoneUI` 里 0 处 `#include <zUI/...>`）。如果 DuiLib 侧中期
   会迁到 zUI，那这个库会变成第三套要维护的响应式基础设施。这个判断需要看 zUI 的 roadmap。
