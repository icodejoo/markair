# BENCH-C：mdvn 高亮最坏情况基准语料（T54）

> 本文件用于 T54“高亮的体积与首屏增量门禁”任务，记录高亮场景下的最坏情况首屏时间与内存，**不设门禁**（04 的验收标准没有为这份语料定线，只记录）。
> 约 100KB，代码块占比 >= 50%，覆盖全部 11 种受支持的高亮语言：
> C、C++、C#、Java、JavaScript、Python、Go、Rust、JSON、YAML、Shell。

## 第 1 节：本节为 BENCH-C 语料的说明性文字，穿插在代码块之间，用于让代码块占比保持在约 50% 以上但不至于 100%。BENCH-C 是 T54 新增的“高亮最坏情况”记录语料：约 100KB、代码块占比不低于 50%、覆盖全部 11 种受支持的高亮语言（C/C++/C#/Java/JavaScript/Python/Go/Rust/JSON/YAML/Shell）。本文件不设门禁，仅用于记录首屏时间与内存在高亮最坏场景下的数值。

```c
#include <stdio.h>
#include <stdlib.h>

// 计算斐波那契数列第n项，用于BENCH-C语料填充(C语言样例1)
int fib_1(int n) {
    if (n <= 1) return n;
    int a = 0, b = 1;
    for (int i = 2; i <= n; i++) {
        int c = a + b;
        a = b;
        b = c;
    }
    return b;
}

int main(void) {
    for (int i = 0; i < 10; i++) {
        printf("fib(%d) = %d\n", i, fib_1(i));
    }
    return EXIT_SUCCESS;
}
```

```cpp
#include <vector>
#include <string>
#include <iostream>

// 简单的模板类，用于BENCH-C语料填充(C++样例2)
template <typename T>
class Stack2 {
public:
    void push(const T& v) { data_.push_back(v); }
    T pop() {
        T v = data_.back();
        data_.pop_back();
        return v;
    }
    bool empty() const noexcept { return data_.empty(); }
private:
    std::vector<T> data_;
};

int main() {
    Stack2<std::string> s;
    s.push("hello");
    s.push("world");
    while (!s.empty()) {
        std::cout << s.pop() << std::endl;
    }
    return 0;
}
```

```csharp
using System;
using System.Collections.Generic;

// 简单的仓库模式示例，用于BENCH-C语料填充(C#样例3)
namespace BenchSample3
{
    public class Repository<T>
    {
        private readonly List<T> _items = new List<T>();

        public void Add(T item) => _items.Add(item);

        public IEnumerable<T> All() => _items;

        public int Count => _items.Count;
    }

    class Program
    {
        static void Main(string[] args)
        {
            var repo = new Repository<string>();
            repo.Add("alpha");
            repo.Add("beta");
            Console.WriteLine($"count={repo.Count}");
        }
    }
}
```

```java
import java.util.ArrayList;
import java.util.List;

// 简单的观察者模式示例，用于BENCH-C语料填充(Java样例4)
public class Publisher4 {
    private final List<Runnable> listeners = new ArrayList<>();

    public void subscribe(Runnable listener) {
        listeners.add(listener);
    }

    public void publish() {
        for (Runnable listener : listeners) {
            listener.run();
        }
    }

    public static void main(String[] args) {
        Publisher4 pub = new Publisher4();
        pub.subscribe(() -> System.out.println("event fired 4"));
        pub.publish();
    }
}
```

```js
// 简单的事件发射器示例，用于BENCH-C语料填充(JavaScript样例5)
class EventBus5 {
    constructor() {
        this.handlers = {};
    }

    on(name, fn) {
        (this.handlers[name] ||= []).push(fn);
    }

    emit(name, payload) {
        (this.handlers[name] || []).forEach((fn) => fn(payload));
    }
}

const bus = new EventBus5();
bus.on("tick", (n) => console.log(`tick ${n}`));
bus.emit("tick", 5);
```

```python
# 简单的装饰器示例，用于BENCH-C语料填充(Python样例6)
import functools
import time


def timed_6(fn):
    @functools.wraps(fn)
    def wrapper(*args, **kwargs):
        start = time.perf_counter()
        result = fn(*args, **kwargs)
        elapsed = time.perf_counter() - start
        print(f"{fn.__name__} took {elapsed:.4f}s")
        return result
    return wrapper


@timed_6
def compute_6(n):
    return sum(i * i for i in range(n))


if __name__ == "__main__":
    print(compute_6(1000))
```

```go
package main

import (
	"fmt"
	"sync"
)

// 简单的并发计数器示例，用于BENCH-C语料填充(Go样例7)
type Counter7 struct {
	mu    sync.Mutex
	value int
}

func (c *Counter7) Inc() {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.value++
}

func main() {
	c := &Counter7{}
	var wg sync.WaitGroup
	for i := 0; i < 100; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			c.Inc()
		}()
	}
	wg.Wait()
	fmt.Println("value:", c.value)
}
```

```rust
use std::collections::HashMap;

// 简单的LRU缓存骨架示例，用于BENCH-C语料填充(Rust样例8)
struct Cache8 {
    map: HashMap<String, i64>,
    capacity: usize,
}

impl Cache8 {
    fn new(capacity: usize) -> Self {
        Cache8 { map: HashMap::new(), capacity }
    }

    fn put(&mut self, key: String, value: i64) {
        if self.map.len() >= self.capacity {
            if let Some(k) = self.map.keys().next().cloned() {
                self.map.remove(&k);
            }
        }
        self.map.insert(key, value);
    }
}

fn main() {
    let mut cache = Cache8::new(16);
    cache.put("a".to_string(), 8);
    println!("{:?}", cache.map.get("a"));
}
```

```json
{
  "sample_id": 9,
  "name": "bench-c-fixture-9",
  "tags": ["highlight", "bench", "json"],
  "metrics": {
    "first_paint_ms": 61.255,
    "delta_ms": -2.469,
    "exe_delta_bytes": 14336
  },
  "languages": ["c", "cpp", "csharp", "java", "js", "python", "go", "rust", "json", "yaml", "sh"],
  "active": true
}
```

```yaml
# BENCH-C YAML样例10，用于覆盖YAML高亮规则
sample_id: 10
name: bench-c-fixture-10
tags:
  - highlight
  - bench
  - yaml
metrics:
  first_paint_ms: 61.255
  exe_delta_bytes: 14336
active: true
```

```sh
#!/bin/bash
# BENCH-C shell样例11，用于覆盖shell高亮规则
set -euo pipefail

BENCH_ID=11
LOG_FILE="/tmp/bench_11.log"

run_once() {
    echo "running bench iteration ${BENCH_ID}" >> "$LOG_FILE"
    for i in $(seq 1 3); do
        echo "step $i" >> "$LOG_FILE"
    done
}

run_once
```

## 第 2 节：本节为 BENCH-C 语料的说明性文字，穿插在代码块之间，用于让代码块占比保持在约 50% 以上但不至于 100%。BENCH-C 是 T54 新增的“高亮最坏情况”记录语料：约 100KB、代码块占比不低于 50%、覆盖全部 11 种受支持的高亮语言（C/C++/C#/Java/JavaScript/Python/Go/Rust/JSON/YAML/Shell）。本文件不设门禁，仅用于记录首屏时间与内存在高亮最坏场景下的数值。

```c
#include <stdio.h>
#include <stdlib.h>

// 计算斐波那契数列第n项，用于BENCH-C语料填充(C语言样例12)
int fib_12(int n) {
    if (n <= 1) return n;
    int a = 0, b = 1;
    for (int i = 2; i <= n; i++) {
        int c = a + b;
        a = b;
        b = c;
    }
    return b;
}

int main(void) {
    for (int i = 0; i < 10; i++) {
        printf("fib(%d) = %d\n", i, fib_12(i));
    }
    return EXIT_SUCCESS;
}
```

```cpp
#include <vector>
#include <string>
#include <iostream>

// 简单的模板类，用于BENCH-C语料填充(C++样例13)
template <typename T>
class Stack13 {
public:
    void push(const T& v) { data_.push_back(v); }
    T pop() {
        T v = data_.back();
        data_.pop_back();
        return v;
    }
    bool empty() const noexcept { return data_.empty(); }
private:
    std::vector<T> data_;
};

int main() {
    Stack13<std::string> s;
    s.push("hello");
    s.push("world");
    while (!s.empty()) {
        std::cout << s.pop() << std::endl;
    }
    return 0;
}
```

```csharp
using System;
using System.Collections.Generic;

// 简单的仓库模式示例，用于BENCH-C语料填充(C#样例14)
namespace BenchSample14
{
    public class Repository<T>
    {
        private readonly List<T> _items = new List<T>();

        public void Add(T item) => _items.Add(item);

        public IEnumerable<T> All() => _items;

        public int Count => _items.Count;
    }

    class Program
    {
        static void Main(string[] args)
        {
            var repo = new Repository<string>();
            repo.Add("alpha");
            repo.Add("beta");
            Console.WriteLine($"count={repo.Count}");
        }
    }
}
```

```java
import java.util.ArrayList;
import java.util.List;

// 简单的观察者模式示例，用于BENCH-C语料填充(Java样例15)
public class Publisher15 {
    private final List<Runnable> listeners = new ArrayList<>();

    public void subscribe(Runnable listener) {
        listeners.add(listener);
    }

    public void publish() {
        for (Runnable listener : listeners) {
            listener.run();
        }
    }

    public static void main(String[] args) {
        Publisher15 pub = new Publisher15();
        pub.subscribe(() -> System.out.println("event fired 15"));
        pub.publish();
    }
}
```

```js
// 简单的事件发射器示例，用于BENCH-C语料填充(JavaScript样例16)
class EventBus16 {
    constructor() {
        this.handlers = {};
    }

    on(name, fn) {
        (this.handlers[name] ||= []).push(fn);
    }

    emit(name, payload) {
        (this.handlers[name] || []).forEach((fn) => fn(payload));
    }
}

const bus = new EventBus16();
bus.on("tick", (n) => console.log(`tick ${n}`));
bus.emit("tick", 16);
```

```python
# 简单的装饰器示例，用于BENCH-C语料填充(Python样例17)
import functools
import time


def timed_17(fn):
    @functools.wraps(fn)
    def wrapper(*args, **kwargs):
        start = time.perf_counter()
        result = fn(*args, **kwargs)
        elapsed = time.perf_counter() - start
        print(f"{fn.__name__} took {elapsed:.4f}s")
        return result
    return wrapper


@timed_17
def compute_17(n):
    return sum(i * i for i in range(n))


if __name__ == "__main__":
    print(compute_17(1000))
```

```go
package main

import (
	"fmt"
	"sync"
)

// 简单的并发计数器示例，用于BENCH-C语料填充(Go样例18)
type Counter18 struct {
	mu    sync.Mutex
	value int
}

func (c *Counter18) Inc() {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.value++
}

func main() {
	c := &Counter18{}
	var wg sync.WaitGroup
	for i := 0; i < 100; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			c.Inc()
		}()
	}
	wg.Wait()
	fmt.Println("value:", c.value)
}
```

```rust
use std::collections::HashMap;

// 简单的LRU缓存骨架示例，用于BENCH-C语料填充(Rust样例19)
struct Cache19 {
    map: HashMap<String, i64>,
    capacity: usize,
}

impl Cache19 {
    fn new(capacity: usize) -> Self {
        Cache19 { map: HashMap::new(), capacity }
    }

    fn put(&mut self, key: String, value: i64) {
        if self.map.len() >= self.capacity {
            if let Some(k) = self.map.keys().next().cloned() {
                self.map.remove(&k);
            }
        }
        self.map.insert(key, value);
    }
}

fn main() {
    let mut cache = Cache19::new(16);
    cache.put("a".to_string(), 19);
    println!("{:?}", cache.map.get("a"));
}
```

```json
{
  "sample_id": 20,
  "name": "bench-c-fixture-20",
  "tags": ["highlight", "bench", "json"],
  "metrics": {
    "first_paint_ms": 61.255,
    "delta_ms": -2.469,
    "exe_delta_bytes": 14336
  },
  "languages": ["c", "cpp", "csharp", "java", "js", "python", "go", "rust", "json", "yaml", "sh"],
  "active": true
}
```

```yaml
# BENCH-C YAML样例21，用于覆盖YAML高亮规则
sample_id: 21
name: bench-c-fixture-21
tags:
  - highlight
  - bench
  - yaml
metrics:
  first_paint_ms: 61.255
  exe_delta_bytes: 14336
active: true
```

```sh
#!/bin/bash
# BENCH-C shell样例22，用于覆盖shell高亮规则
set -euo pipefail

BENCH_ID=22
LOG_FILE="/tmp/bench_22.log"

run_once() {
    echo "running bench iteration ${BENCH_ID}" >> "$LOG_FILE"
    for i in $(seq 1 3); do
        echo "step $i" >> "$LOG_FILE"
    done
}

run_once
```

## 第 3 节：本节为 BENCH-C 语料的说明性文字，穿插在代码块之间，用于让代码块占比保持在约 50% 以上但不至于 100%。BENCH-C 是 T54 新增的“高亮最坏情况”记录语料：约 100KB、代码块占比不低于 50%、覆盖全部 11 种受支持的高亮语言（C/C++/C#/Java/JavaScript/Python/Go/Rust/JSON/YAML/Shell）。本文件不设门禁，仅用于记录首屏时间与内存在高亮最坏场景下的数值。

```c
#include <stdio.h>
#include <stdlib.h>

// 计算斐波那契数列第n项，用于BENCH-C语料填充(C语言样例23)
int fib_23(int n) {
    if (n <= 1) return n;
    int a = 0, b = 1;
    for (int i = 2; i <= n; i++) {
        int c = a + b;
        a = b;
        b = c;
    }
    return b;
}

int main(void) {
    for (int i = 0; i < 10; i++) {
        printf("fib(%d) = %d\n", i, fib_23(i));
    }
    return EXIT_SUCCESS;
}
```

```cpp
#include <vector>
#include <string>
#include <iostream>

// 简单的模板类，用于BENCH-C语料填充(C++样例24)
template <typename T>
class Stack24 {
public:
    void push(const T& v) { data_.push_back(v); }
    T pop() {
        T v = data_.back();
        data_.pop_back();
        return v;
    }
    bool empty() const noexcept { return data_.empty(); }
private:
    std::vector<T> data_;
};

int main() {
    Stack24<std::string> s;
    s.push("hello");
    s.push("world");
    while (!s.empty()) {
        std::cout << s.pop() << std::endl;
    }
    return 0;
}
```

```csharp
using System;
using System.Collections.Generic;

// 简单的仓库模式示例，用于BENCH-C语料填充(C#样例25)
namespace BenchSample25
{
    public class Repository<T>
    {
        private readonly List<T> _items = new List<T>();

        public void Add(T item) => _items.Add(item);

        public IEnumerable<T> All() => _items;

        public int Count => _items.Count;
    }

    class Program
    {
        static void Main(string[] args)
        {
            var repo = new Repository<string>();
            repo.Add("alpha");
            repo.Add("beta");
            Console.WriteLine($"count={repo.Count}");
        }
    }
}
```

```java
import java.util.ArrayList;
import java.util.List;

// 简单的观察者模式示例，用于BENCH-C语料填充(Java样例26)
public class Publisher26 {
    private final List<Runnable> listeners = new ArrayList<>();

    public void subscribe(Runnable listener) {
        listeners.add(listener);
    }

    public void publish() {
        for (Runnable listener : listeners) {
            listener.run();
        }
    }

    public static void main(String[] args) {
        Publisher26 pub = new Publisher26();
        pub.subscribe(() -> System.out.println("event fired 26"));
        pub.publish();
    }
}
```

```js
// 简单的事件发射器示例，用于BENCH-C语料填充(JavaScript样例27)
class EventBus27 {
    constructor() {
        this.handlers = {};
    }

    on(name, fn) {
        (this.handlers[name] ||= []).push(fn);
    }

    emit(name, payload) {
        (this.handlers[name] || []).forEach((fn) => fn(payload));
    }
}

const bus = new EventBus27();
bus.on("tick", (n) => console.log(`tick ${n}`));
bus.emit("tick", 27);
```

```python
# 简单的装饰器示例，用于BENCH-C语料填充(Python样例28)
import functools
import time


def timed_28(fn):
    @functools.wraps(fn)
    def wrapper(*args, **kwargs):
        start = time.perf_counter()
        result = fn(*args, **kwargs)
        elapsed = time.perf_counter() - start
        print(f"{fn.__name__} took {elapsed:.4f}s")
        return result
    return wrapper


@timed_28
def compute_28(n):
    return sum(i * i for i in range(n))


if __name__ == "__main__":
    print(compute_28(1000))
```

```go
package main

import (
	"fmt"
	"sync"
)

// 简单的并发计数器示例，用于BENCH-C语料填充(Go样例29)
type Counter29 struct {
	mu    sync.Mutex
	value int
}

func (c *Counter29) Inc() {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.value++
}

func main() {
	c := &Counter29{}
	var wg sync.WaitGroup
	for i := 0; i < 100; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			c.Inc()
		}()
	}
	wg.Wait()
	fmt.Println("value:", c.value)
}
```

```rust
use std::collections::HashMap;

// 简单的LRU缓存骨架示例，用于BENCH-C语料填充(Rust样例30)
struct Cache30 {
    map: HashMap<String, i64>,
    capacity: usize,
}

impl Cache30 {
    fn new(capacity: usize) -> Self {
        Cache30 { map: HashMap::new(), capacity }
    }

    fn put(&mut self, key: String, value: i64) {
        if self.map.len() >= self.capacity {
            if let Some(k) = self.map.keys().next().cloned() {
                self.map.remove(&k);
            }
        }
        self.map.insert(key, value);
    }
}

fn main() {
    let mut cache = Cache30::new(16);
    cache.put("a".to_string(), 30);
    println!("{:?}", cache.map.get("a"));
}
```

```json
{
  "sample_id": 31,
  "name": "bench-c-fixture-31",
  "tags": ["highlight", "bench", "json"],
  "metrics": {
    "first_paint_ms": 61.255,
    "delta_ms": -2.469,
    "exe_delta_bytes": 14336
  },
  "languages": ["c", "cpp", "csharp", "java", "js", "python", "go", "rust", "json", "yaml", "sh"],
  "active": true
}
```

```yaml
# BENCH-C YAML样例32，用于覆盖YAML高亮规则
sample_id: 32
name: bench-c-fixture-32
tags:
  - highlight
  - bench
  - yaml
metrics:
  first_paint_ms: 61.255
  exe_delta_bytes: 14336
active: true
```

```sh
#!/bin/bash
# BENCH-C shell样例33，用于覆盖shell高亮规则
set -euo pipefail

BENCH_ID=33
LOG_FILE="/tmp/bench_33.log"

run_once() {
    echo "running bench iteration ${BENCH_ID}" >> "$LOG_FILE"
    for i in $(seq 1 3); do
        echo "step $i" >> "$LOG_FILE"
    done
}

run_once
```

## 第 4 节：本节为 BENCH-C 语料的说明性文字，穿插在代码块之间，用于让代码块占比保持在约 50% 以上但不至于 100%。BENCH-C 是 T54 新增的“高亮最坏情况”记录语料：约 100KB、代码块占比不低于 50%、覆盖全部 11 种受支持的高亮语言（C/C++/C#/Java/JavaScript/Python/Go/Rust/JSON/YAML/Shell）。本文件不设门禁，仅用于记录首屏时间与内存在高亮最坏场景下的数值。

```c
#include <stdio.h>
#include <stdlib.h>

// 计算斐波那契数列第n项，用于BENCH-C语料填充(C语言样例34)
int fib_34(int n) {
    if (n <= 1) return n;
    int a = 0, b = 1;
    for (int i = 2; i <= n; i++) {
        int c = a + b;
        a = b;
        b = c;
    }
    return b;
}

int main(void) {
    for (int i = 0; i < 10; i++) {
        printf("fib(%d) = %d\n", i, fib_34(i));
    }
    return EXIT_SUCCESS;
}
```

```cpp
#include <vector>
#include <string>
#include <iostream>

// 简单的模板类，用于BENCH-C语料填充(C++样例35)
template <typename T>
class Stack35 {
public:
    void push(const T& v) { data_.push_back(v); }
    T pop() {
        T v = data_.back();
        data_.pop_back();
        return v;
    }
    bool empty() const noexcept { return data_.empty(); }
private:
    std::vector<T> data_;
};

int main() {
    Stack35<std::string> s;
    s.push("hello");
    s.push("world");
    while (!s.empty()) {
        std::cout << s.pop() << std::endl;
    }
    return 0;
}
```

```csharp
using System;
using System.Collections.Generic;

// 简单的仓库模式示例，用于BENCH-C语料填充(C#样例36)
namespace BenchSample36
{
    public class Repository<T>
    {
        private readonly List<T> _items = new List<T>();

        public void Add(T item) => _items.Add(item);

        public IEnumerable<T> All() => _items;

        public int Count => _items.Count;
    }

    class Program
    {
        static void Main(string[] args)
        {
            var repo = new Repository<string>();
            repo.Add("alpha");
            repo.Add("beta");
            Console.WriteLine($"count={repo.Count}");
        }
    }
}
```

```java
import java.util.ArrayList;
import java.util.List;

// 简单的观察者模式示例，用于BENCH-C语料填充(Java样例37)
public class Publisher37 {
    private final List<Runnable> listeners = new ArrayList<>();

    public void subscribe(Runnable listener) {
        listeners.add(listener);
    }

    public void publish() {
        for (Runnable listener : listeners) {
            listener.run();
        }
    }

    public static void main(String[] args) {
        Publisher37 pub = new Publisher37();
        pub.subscribe(() -> System.out.println("event fired 37"));
        pub.publish();
    }
}
```

```js
// 简单的事件发射器示例，用于BENCH-C语料填充(JavaScript样例38)
class EventBus38 {
    constructor() {
        this.handlers = {};
    }

    on(name, fn) {
        (this.handlers[name] ||= []).push(fn);
    }

    emit(name, payload) {
        (this.handlers[name] || []).forEach((fn) => fn(payload));
    }
}

const bus = new EventBus38();
bus.on("tick", (n) => console.log(`tick ${n}`));
bus.emit("tick", 38);
```

```python
# 简单的装饰器示例，用于BENCH-C语料填充(Python样例39)
import functools
import time


def timed_39(fn):
    @functools.wraps(fn)
    def wrapper(*args, **kwargs):
        start = time.perf_counter()
        result = fn(*args, **kwargs)
        elapsed = time.perf_counter() - start
        print(f"{fn.__name__} took {elapsed:.4f}s")
        return result
    return wrapper


@timed_39
def compute_39(n):
    return sum(i * i for i in range(n))


if __name__ == "__main__":
    print(compute_39(1000))
```

```go
package main

import (
	"fmt"
	"sync"
)

// 简单的并发计数器示例，用于BENCH-C语料填充(Go样例40)
type Counter40 struct {
	mu    sync.Mutex
	value int
}

func (c *Counter40) Inc() {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.value++
}

func main() {
	c := &Counter40{}
	var wg sync.WaitGroup
	for i := 0; i < 100; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			c.Inc()
		}()
	}
	wg.Wait()
	fmt.Println("value:", c.value)
}
```

```rust
use std::collections::HashMap;

// 简单的LRU缓存骨架示例，用于BENCH-C语料填充(Rust样例41)
struct Cache41 {
    map: HashMap<String, i64>,
    capacity: usize,
}

impl Cache41 {
    fn new(capacity: usize) -> Self {
        Cache41 { map: HashMap::new(), capacity }
    }

    fn put(&mut self, key: String, value: i64) {
        if self.map.len() >= self.capacity {
            if let Some(k) = self.map.keys().next().cloned() {
                self.map.remove(&k);
            }
        }
        self.map.insert(key, value);
    }
}

fn main() {
    let mut cache = Cache41::new(16);
    cache.put("a".to_string(), 41);
    println!("{:?}", cache.map.get("a"));
}
```

```json
{
  "sample_id": 42,
  "name": "bench-c-fixture-42",
  "tags": ["highlight", "bench", "json"],
  "metrics": {
    "first_paint_ms": 61.255,
    "delta_ms": -2.469,
    "exe_delta_bytes": 14336
  },
  "languages": ["c", "cpp", "csharp", "java", "js", "python", "go", "rust", "json", "yaml", "sh"],
  "active": true
}
```

```yaml
# BENCH-C YAML样例43，用于覆盖YAML高亮规则
sample_id: 43
name: bench-c-fixture-43
tags:
  - highlight
  - bench
  - yaml
metrics:
  first_paint_ms: 61.255
  exe_delta_bytes: 14336
active: true
```

```sh
#!/bin/bash
# BENCH-C shell样例44，用于覆盖shell高亮规则
set -euo pipefail

BENCH_ID=44
LOG_FILE="/tmp/bench_44.log"

run_once() {
    echo "running bench iteration ${BENCH_ID}" >> "$LOG_FILE"
    for i in $(seq 1 3); do
        echo "step $i" >> "$LOG_FILE"
    done
}

run_once
```

## 第 5 节：本节为 BENCH-C 语料的说明性文字，穿插在代码块之间，用于让代码块占比保持在约 50% 以上但不至于 100%。BENCH-C 是 T54 新增的“高亮最坏情况”记录语料：约 100KB、代码块占比不低于 50%、覆盖全部 11 种受支持的高亮语言（C/C++/C#/Java/JavaScript/Python/Go/Rust/JSON/YAML/Shell）。本文件不设门禁，仅用于记录首屏时间与内存在高亮最坏场景下的数值。

```c
#include <stdio.h>
#include <stdlib.h>

// 计算斐波那契数列第n项，用于BENCH-C语料填充(C语言样例45)
int fib_45(int n) {
    if (n <= 1) return n;
    int a = 0, b = 1;
    for (int i = 2; i <= n; i++) {
        int c = a + b;
        a = b;
        b = c;
    }
    return b;
}

int main(void) {
    for (int i = 0; i < 10; i++) {
        printf("fib(%d) = %d\n", i, fib_45(i));
    }
    return EXIT_SUCCESS;
}
```

```cpp
#include <vector>
#include <string>
#include <iostream>

// 简单的模板类，用于BENCH-C语料填充(C++样例46)
template <typename T>
class Stack46 {
public:
    void push(const T& v) { data_.push_back(v); }
    T pop() {
        T v = data_.back();
        data_.pop_back();
        return v;
    }
    bool empty() const noexcept { return data_.empty(); }
private:
    std::vector<T> data_;
};

int main() {
    Stack46<std::string> s;
    s.push("hello");
    s.push("world");
    while (!s.empty()) {
        std::cout << s.pop() << std::endl;
    }
    return 0;
}
```

```csharp
using System;
using System.Collections.Generic;

// 简单的仓库模式示例，用于BENCH-C语料填充(C#样例47)
namespace BenchSample47
{
    public class Repository<T>
    {
        private readonly List<T> _items = new List<T>();

        public void Add(T item) => _items.Add(item);

        public IEnumerable<T> All() => _items;

        public int Count => _items.Count;
    }

    class Program
    {
        static void Main(string[] args)
        {
            var repo = new Repository<string>();
            repo.Add("alpha");
            repo.Add("beta");
            Console.WriteLine($"count={repo.Count}");
        }
    }
}
```

```java
import java.util.ArrayList;
import java.util.List;

// 简单的观察者模式示例，用于BENCH-C语料填充(Java样例48)
public class Publisher48 {
    private final List<Runnable> listeners = new ArrayList<>();

    public void subscribe(Runnable listener) {
        listeners.add(listener);
    }

    public void publish() {
        for (Runnable listener : listeners) {
            listener.run();
        }
    }

    public static void main(String[] args) {
        Publisher48 pub = new Publisher48();
        pub.subscribe(() -> System.out.println("event fired 48"));
        pub.publish();
    }
}
```

```js
// 简单的事件发射器示例，用于BENCH-C语料填充(JavaScript样例49)
class EventBus49 {
    constructor() {
        this.handlers = {};
    }

    on(name, fn) {
        (this.handlers[name] ||= []).push(fn);
    }

    emit(name, payload) {
        (this.handlers[name] || []).forEach((fn) => fn(payload));
    }
}

const bus = new EventBus49();
bus.on("tick", (n) => console.log(`tick ${n}`));
bus.emit("tick", 49);
```

```python
# 简单的装饰器示例，用于BENCH-C语料填充(Python样例50)
import functools
import time


def timed_50(fn):
    @functools.wraps(fn)
    def wrapper(*args, **kwargs):
        start = time.perf_counter()
        result = fn(*args, **kwargs)
        elapsed = time.perf_counter() - start
        print(f"{fn.__name__} took {elapsed:.4f}s")
        return result
    return wrapper


@timed_50
def compute_50(n):
    return sum(i * i for i in range(n))


if __name__ == "__main__":
    print(compute_50(1000))
```

```go
package main

import (
	"fmt"
	"sync"
)

// 简单的并发计数器示例，用于BENCH-C语料填充(Go样例51)
type Counter51 struct {
	mu    sync.Mutex
	value int
}

func (c *Counter51) Inc() {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.value++
}

func main() {
	c := &Counter51{}
	var wg sync.WaitGroup
	for i := 0; i < 100; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			c.Inc()
		}()
	}
	wg.Wait()
	fmt.Println("value:", c.value)
}
```

```rust
use std::collections::HashMap;

// 简单的LRU缓存骨架示例，用于BENCH-C语料填充(Rust样例52)
struct Cache52 {
    map: HashMap<String, i64>,
    capacity: usize,
}

impl Cache52 {
    fn new(capacity: usize) -> Self {
        Cache52 { map: HashMap::new(), capacity }
    }

    fn put(&mut self, key: String, value: i64) {
        if self.map.len() >= self.capacity {
            if let Some(k) = self.map.keys().next().cloned() {
                self.map.remove(&k);
            }
        }
        self.map.insert(key, value);
    }
}

fn main() {
    let mut cache = Cache52::new(16);
    cache.put("a".to_string(), 52);
    println!("{:?}", cache.map.get("a"));
}
```

```json
{
  "sample_id": 53,
  "name": "bench-c-fixture-53",
  "tags": ["highlight", "bench", "json"],
  "metrics": {
    "first_paint_ms": 61.255,
    "delta_ms": -2.469,
    "exe_delta_bytes": 14336
  },
  "languages": ["c", "cpp", "csharp", "java", "js", "python", "go", "rust", "json", "yaml", "sh"],
  "active": true
}
```

```yaml
# BENCH-C YAML样例54，用于覆盖YAML高亮规则
sample_id: 54
name: bench-c-fixture-54
tags:
  - highlight
  - bench
  - yaml
metrics:
  first_paint_ms: 61.255
  exe_delta_bytes: 14336
active: true
```

```sh
#!/bin/bash
# BENCH-C shell样例55，用于覆盖shell高亮规则
set -euo pipefail

BENCH_ID=55
LOG_FILE="/tmp/bench_55.log"

run_once() {
    echo "running bench iteration ${BENCH_ID}" >> "$LOG_FILE"
    for i in $(seq 1 3); do
        echo "step $i" >> "$LOG_FILE"
    done
}

run_once
```

## 第 6 节：本节为 BENCH-C 语料的说明性文字，穿插在代码块之间，用于让代码块占比保持在约 50% 以上但不至于 100%。BENCH-C 是 T54 新增的“高亮最坏情况”记录语料：约 100KB、代码块占比不低于 50%、覆盖全部 11 种受支持的高亮语言（C/C++/C#/Java/JavaScript/Python/Go/Rust/JSON/YAML/Shell）。本文件不设门禁，仅用于记录首屏时间与内存在高亮最坏场景下的数值。

```c
#include <stdio.h>
#include <stdlib.h>

// 计算斐波那契数列第n项，用于BENCH-C语料填充(C语言样例56)
int fib_56(int n) {
    if (n <= 1) return n;
    int a = 0, b = 1;
    for (int i = 2; i <= n; i++) {
        int c = a + b;
        a = b;
        b = c;
    }
    return b;
}

int main(void) {
    for (int i = 0; i < 10; i++) {
        printf("fib(%d) = %d\n", i, fib_56(i));
    }
    return EXIT_SUCCESS;
}
```

```cpp
#include <vector>
#include <string>
#include <iostream>

// 简单的模板类，用于BENCH-C语料填充(C++样例57)
template <typename T>
class Stack57 {
public:
    void push(const T& v) { data_.push_back(v); }
    T pop() {
        T v = data_.back();
        data_.pop_back();
        return v;
    }
    bool empty() const noexcept { return data_.empty(); }
private:
    std::vector<T> data_;
};

int main() {
    Stack57<std::string> s;
    s.push("hello");
    s.push("world");
    while (!s.empty()) {
        std::cout << s.pop() << std::endl;
    }
    return 0;
}
```

```csharp
using System;
using System.Collections.Generic;

// 简单的仓库模式示例，用于BENCH-C语料填充(C#样例58)
namespace BenchSample58
{
    public class Repository<T>
    {
        private readonly List<T> _items = new List<T>();

        public void Add(T item) => _items.Add(item);

        public IEnumerable<T> All() => _items;

        public int Count => _items.Count;
    }

    class Program
    {
        static void Main(string[] args)
        {
            var repo = new Repository<string>();
            repo.Add("alpha");
            repo.Add("beta");
            Console.WriteLine($"count={repo.Count}");
        }
    }
}
```

```java
import java.util.ArrayList;
import java.util.List;

// 简单的观察者模式示例，用于BENCH-C语料填充(Java样例59)
public class Publisher59 {
    private final List<Runnable> listeners = new ArrayList<>();

    public void subscribe(Runnable listener) {
        listeners.add(listener);
    }

    public void publish() {
        for (Runnable listener : listeners) {
            listener.run();
        }
    }

    public static void main(String[] args) {
        Publisher59 pub = new Publisher59();
        pub.subscribe(() -> System.out.println("event fired 59"));
        pub.publish();
    }
}
```

```js
// 简单的事件发射器示例，用于BENCH-C语料填充(JavaScript样例60)
class EventBus60 {
    constructor() {
        this.handlers = {};
    }

    on(name, fn) {
        (this.handlers[name] ||= []).push(fn);
    }

    emit(name, payload) {
        (this.handlers[name] || []).forEach((fn) => fn(payload));
    }
}

const bus = new EventBus60();
bus.on("tick", (n) => console.log(`tick ${n}`));
bus.emit("tick", 60);
```

```python
# 简单的装饰器示例，用于BENCH-C语料填充(Python样例61)
import functools
import time


def timed_61(fn):
    @functools.wraps(fn)
    def wrapper(*args, **kwargs):
        start = time.perf_counter()
        result = fn(*args, **kwargs)
        elapsed = time.perf_counter() - start
        print(f"{fn.__name__} took {elapsed:.4f}s")
        return result
    return wrapper


@timed_61
def compute_61(n):
    return sum(i * i for i in range(n))


if __name__ == "__main__":
    print(compute_61(1000))
```

```go
package main

import (
	"fmt"
	"sync"
)

// 简单的并发计数器示例，用于BENCH-C语料填充(Go样例62)
type Counter62 struct {
	mu    sync.Mutex
	value int
}

func (c *Counter62) Inc() {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.value++
}

func main() {
	c := &Counter62{}
	var wg sync.WaitGroup
	for i := 0; i < 100; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			c.Inc()
		}()
	}
	wg.Wait()
	fmt.Println("value:", c.value)
}
```

```rust
use std::collections::HashMap;

// 简单的LRU缓存骨架示例，用于BENCH-C语料填充(Rust样例63)
struct Cache63 {
    map: HashMap<String, i64>,
    capacity: usize,
}

impl Cache63 {
    fn new(capacity: usize) -> Self {
        Cache63 { map: HashMap::new(), capacity }
    }

    fn put(&mut self, key: String, value: i64) {
        if self.map.len() >= self.capacity {
            if let Some(k) = self.map.keys().next().cloned() {
                self.map.remove(&k);
            }
        }
        self.map.insert(key, value);
    }
}

fn main() {
    let mut cache = Cache63::new(16);
    cache.put("a".to_string(), 63);
    println!("{:?}", cache.map.get("a"));
}
```

```json
{
  "sample_id": 64,
  "name": "bench-c-fixture-64",
  "tags": ["highlight", "bench", "json"],
  "metrics": {
    "first_paint_ms": 61.255,
    "delta_ms": -2.469,
    "exe_delta_bytes": 14336
  },
  "languages": ["c", "cpp", "csharp", "java", "js", "python", "go", "rust", "json", "yaml", "sh"],
  "active": true
}
```

```yaml
# BENCH-C YAML样例65，用于覆盖YAML高亮规则
sample_id: 65
name: bench-c-fixture-65
tags:
  - highlight
  - bench
  - yaml
metrics:
  first_paint_ms: 61.255
  exe_delta_bytes: 14336
active: true
```

```sh
#!/bin/bash
# BENCH-C shell样例66，用于覆盖shell高亮规则
set -euo pipefail

BENCH_ID=66
LOG_FILE="/tmp/bench_66.log"

run_once() {
    echo "running bench iteration ${BENCH_ID}" >> "$LOG_FILE"
    for i in $(seq 1 3); do
        echo "step $i" >> "$LOG_FILE"
    done
}

run_once
```

## 第 7 节：本节为 BENCH-C 语料的说明性文字，穿插在代码块之间，用于让代码块占比保持在约 50% 以上但不至于 100%。BENCH-C 是 T54 新增的“高亮最坏情况”记录语料：约 100KB、代码块占比不低于 50%、覆盖全部 11 种受支持的高亮语言（C/C++/C#/Java/JavaScript/Python/Go/Rust/JSON/YAML/Shell）。本文件不设门禁，仅用于记录首屏时间与内存在高亮最坏场景下的数值。

```c
#include <stdio.h>
#include <stdlib.h>

// 计算斐波那契数列第n项，用于BENCH-C语料填充(C语言样例67)
int fib_67(int n) {
    if (n <= 1) return n;
    int a = 0, b = 1;
    for (int i = 2; i <= n; i++) {
        int c = a + b;
        a = b;
        b = c;
    }
    return b;
}

int main(void) {
    for (int i = 0; i < 10; i++) {
        printf("fib(%d) = %d\n", i, fib_67(i));
    }
    return EXIT_SUCCESS;
}
```

```cpp
#include <vector>
#include <string>
#include <iostream>

// 简单的模板类，用于BENCH-C语料填充(C++样例68)
template <typename T>
class Stack68 {
public:
    void push(const T& v) { data_.push_back(v); }
    T pop() {
        T v = data_.back();
        data_.pop_back();
        return v;
    }
    bool empty() const noexcept { return data_.empty(); }
private:
    std::vector<T> data_;
};

int main() {
    Stack68<std::string> s;
    s.push("hello");
    s.push("world");
    while (!s.empty()) {
        std::cout << s.pop() << std::endl;
    }
    return 0;
}
```

```csharp
using System;
using System.Collections.Generic;

// 简单的仓库模式示例，用于BENCH-C语料填充(C#样例69)
namespace BenchSample69
{
    public class Repository<T>
    {
        private readonly List<T> _items = new List<T>();

        public void Add(T item) => _items.Add(item);

        public IEnumerable<T> All() => _items;

        public int Count => _items.Count;
    }

    class Program
    {
        static void Main(string[] args)
        {
            var repo = new Repository<string>();
            repo.Add("alpha");
            repo.Add("beta");
            Console.WriteLine($"count={repo.Count}");
        }
    }
}
```

```java
import java.util.ArrayList;
import java.util.List;

// 简单的观察者模式示例，用于BENCH-C语料填充(Java样例70)
public class Publisher70 {
    private final List<Runnable> listeners = new ArrayList<>();

    public void subscribe(Runnable listener) {
        listeners.add(listener);
    }

    public void publish() {
        for (Runnable listener : listeners) {
            listener.run();
        }
    }

    public static void main(String[] args) {
        Publisher70 pub = new Publisher70();
        pub.subscribe(() -> System.out.println("event fired 70"));
        pub.publish();
    }
}
```

```js
// 简单的事件发射器示例，用于BENCH-C语料填充(JavaScript样例71)
class EventBus71 {
    constructor() {
        this.handlers = {};
    }

    on(name, fn) {
        (this.handlers[name] ||= []).push(fn);
    }

    emit(name, payload) {
        (this.handlers[name] || []).forEach((fn) => fn(payload));
    }
}

const bus = new EventBus71();
bus.on("tick", (n) => console.log(`tick ${n}`));
bus.emit("tick", 71);
```

```python
# 简单的装饰器示例，用于BENCH-C语料填充(Python样例72)
import functools
import time


def timed_72(fn):
    @functools.wraps(fn)
    def wrapper(*args, **kwargs):
        start = time.perf_counter()
        result = fn(*args, **kwargs)
        elapsed = time.perf_counter() - start
        print(f"{fn.__name__} took {elapsed:.4f}s")
        return result
    return wrapper


@timed_72
def compute_72(n):
    return sum(i * i for i in range(n))


if __name__ == "__main__":
    print(compute_72(1000))
```

```go
package main

import (
	"fmt"
	"sync"
)

// 简单的并发计数器示例，用于BENCH-C语料填充(Go样例73)
type Counter73 struct {
	mu    sync.Mutex
	value int
}

func (c *Counter73) Inc() {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.value++
}

func main() {
	c := &Counter73{}
	var wg sync.WaitGroup
	for i := 0; i < 100; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			c.Inc()
		}()
	}
	wg.Wait()
	fmt.Println("value:", c.value)
}
```

```rust
use std::collections::HashMap;

// 简单的LRU缓存骨架示例，用于BENCH-C语料填充(Rust样例74)
struct Cache74 {
    map: HashMap<String, i64>,
    capacity: usize,
}

impl Cache74 {
    fn new(capacity: usize) -> Self {
        Cache74 { map: HashMap::new(), capacity }
    }

    fn put(&mut self, key: String, value: i64) {
        if self.map.len() >= self.capacity {
            if let Some(k) = self.map.keys().next().cloned() {
                self.map.remove(&k);
            }
        }
        self.map.insert(key, value);
    }
}

fn main() {
    let mut cache = Cache74::new(16);
    cache.put("a".to_string(), 74);
    println!("{:?}", cache.map.get("a"));
}
```

```json
{
  "sample_id": 75,
  "name": "bench-c-fixture-75",
  "tags": ["highlight", "bench", "json"],
  "metrics": {
    "first_paint_ms": 61.255,
    "delta_ms": -2.469,
    "exe_delta_bytes": 14336
  },
  "languages": ["c", "cpp", "csharp", "java", "js", "python", "go", "rust", "json", "yaml", "sh"],
  "active": true
}
```

```yaml
# BENCH-C YAML样例76，用于覆盖YAML高亮规则
sample_id: 76
name: bench-c-fixture-76
tags:
  - highlight
  - bench
  - yaml
metrics:
  first_paint_ms: 61.255
  exe_delta_bytes: 14336
active: true
```

```sh
#!/bin/bash
# BENCH-C shell样例77，用于覆盖shell高亮规则
set -euo pipefail

BENCH_ID=77
LOG_FILE="/tmp/bench_77.log"

run_once() {
    echo "running bench iteration ${BENCH_ID}" >> "$LOG_FILE"
    for i in $(seq 1 3); do
        echo "step $i" >> "$LOG_FILE"
    done
}

run_once
```

## 第 8 节：本节为 BENCH-C 语料的说明性文字，穿插在代码块之间，用于让代码块占比保持在约 50% 以上但不至于 100%。BENCH-C 是 T54 新增的“高亮最坏情况”记录语料：约 100KB、代码块占比不低于 50%、覆盖全部 11 种受支持的高亮语言（C/C++/C#/Java/JavaScript/Python/Go/Rust/JSON/YAML/Shell）。本文件不设门禁，仅用于记录首屏时间与内存在高亮最坏场景下的数值。

```c
#include <stdio.h>
#include <stdlib.h>

// 计算斐波那契数列第n项，用于BENCH-C语料填充(C语言样例78)
int fib_78(int n) {
    if (n <= 1) return n;
    int a = 0, b = 1;
    for (int i = 2; i <= n; i++) {
        int c = a + b;
        a = b;
        b = c;
    }
    return b;
}

int main(void) {
    for (int i = 0; i < 10; i++) {
        printf("fib(%d) = %d\n", i, fib_78(i));
    }
    return EXIT_SUCCESS;
}
```

```cpp
#include <vector>
#include <string>
#include <iostream>

// 简单的模板类，用于BENCH-C语料填充(C++样例79)
template <typename T>
class Stack79 {
public:
    void push(const T& v) { data_.push_back(v); }
    T pop() {
        T v = data_.back();
        data_.pop_back();
        return v;
    }
    bool empty() const noexcept { return data_.empty(); }
private:
    std::vector<T> data_;
};

int main() {
    Stack79<std::string> s;
    s.push("hello");
    s.push("world");
    while (!s.empty()) {
        std::cout << s.pop() << std::endl;
    }
    return 0;
}
```

```csharp
using System;
using System.Collections.Generic;

// 简单的仓库模式示例，用于BENCH-C语料填充(C#样例80)
namespace BenchSample80
{
    public class Repository<T>
    {
        private readonly List<T> _items = new List<T>();

        public void Add(T item) => _items.Add(item);

        public IEnumerable<T> All() => _items;

        public int Count => _items.Count;
    }

    class Program
    {
        static void Main(string[] args)
        {
            var repo = new Repository<string>();
            repo.Add("alpha");
            repo.Add("beta");
            Console.WriteLine($"count={repo.Count}");
        }
    }
}
```

```java
import java.util.ArrayList;
import java.util.List;

// 简单的观察者模式示例，用于BENCH-C语料填充(Java样例81)
public class Publisher81 {
    private final List<Runnable> listeners = new ArrayList<>();

    public void subscribe(Runnable listener) {
        listeners.add(listener);
    }

    public void publish() {
        for (Runnable listener : listeners) {
            listener.run();
        }
    }

    public static void main(String[] args) {
        Publisher81 pub = new Publisher81();
        pub.subscribe(() -> System.out.println("event fired 81"));
        pub.publish();
    }
}
```

```js
// 简单的事件发射器示例，用于BENCH-C语料填充(JavaScript样例82)
class EventBus82 {
    constructor() {
        this.handlers = {};
    }

    on(name, fn) {
        (this.handlers[name] ||= []).push(fn);
    }

    emit(name, payload) {
        (this.handlers[name] || []).forEach((fn) => fn(payload));
    }
}

const bus = new EventBus82();
bus.on("tick", (n) => console.log(`tick ${n}`));
bus.emit("tick", 82);
```

```python
# 简单的装饰器示例，用于BENCH-C语料填充(Python样例83)
import functools
import time


def timed_83(fn):
    @functools.wraps(fn)
    def wrapper(*args, **kwargs):
        start = time.perf_counter()
        result = fn(*args, **kwargs)
        elapsed = time.perf_counter() - start
        print(f"{fn.__name__} took {elapsed:.4f}s")
        return result
    return wrapper


@timed_83
def compute_83(n):
    return sum(i * i for i in range(n))


if __name__ == "__main__":
    print(compute_83(1000))
```

```go
package main

import (
	"fmt"
	"sync"
)

// 简单的并发计数器示例，用于BENCH-C语料填充(Go样例84)
type Counter84 struct {
	mu    sync.Mutex
	value int
}

func (c *Counter84) Inc() {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.value++
}

func main() {
	c := &Counter84{}
	var wg sync.WaitGroup
	for i := 0; i < 100; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			c.Inc()
		}()
	}
	wg.Wait()
	fmt.Println("value:", c.value)
}
```

```rust
use std::collections::HashMap;

// 简单的LRU缓存骨架示例，用于BENCH-C语料填充(Rust样例85)
struct Cache85 {
    map: HashMap<String, i64>,
    capacity: usize,
}

impl Cache85 {
    fn new(capacity: usize) -> Self {
        Cache85 { map: HashMap::new(), capacity }
    }

    fn put(&mut self, key: String, value: i64) {
        if self.map.len() >= self.capacity {
            if let Some(k) = self.map.keys().next().cloned() {
                self.map.remove(&k);
            }
        }
        self.map.insert(key, value);
    }
}

fn main() {
    let mut cache = Cache85::new(16);
    cache.put("a".to_string(), 85);
    println!("{:?}", cache.map.get("a"));
}
```

```json
{
  "sample_id": 86,
  "name": "bench-c-fixture-86",
  "tags": ["highlight", "bench", "json"],
  "metrics": {
    "first_paint_ms": 61.255,
    "delta_ms": -2.469,
    "exe_delta_bytes": 14336
  },
  "languages": ["c", "cpp", "csharp", "java", "js", "python", "go", "rust", "json", "yaml", "sh"],
  "active": true
}
```

```yaml
# BENCH-C YAML样例87，用于覆盖YAML高亮规则
sample_id: 87
name: bench-c-fixture-87
tags:
  - highlight
  - bench
  - yaml
metrics:
  first_paint_ms: 61.255
  exe_delta_bytes: 14336
active: true
```

```sh
#!/bin/bash
# BENCH-C shell样例88，用于覆盖shell高亮规则
set -euo pipefail

BENCH_ID=88
LOG_FILE="/tmp/bench_88.log"

run_once() {
    echo "running bench iteration ${BENCH_ID}" >> "$LOG_FILE"
    for i in $(seq 1 3); do
        echo "step $i" >> "$LOG_FILE"
    done
}

run_once
```

## 第 9 节：本节为 BENCH-C 语料的说明性文字，穿插在代码块之间，用于让代码块占比保持在约 50% 以上但不至于 100%。BENCH-C 是 T54 新增的“高亮最坏情况”记录语料：约 100KB、代码块占比不低于 50%、覆盖全部 11 种受支持的高亮语言（C/C++/C#/Java/JavaScript/Python/Go/Rust/JSON/YAML/Shell）。本文件不设门禁，仅用于记录首屏时间与内存在高亮最坏场景下的数值。

```c
#include <stdio.h>
#include <stdlib.h>

// 计算斐波那契数列第n项，用于BENCH-C语料填充(C语言样例89)
int fib_89(int n) {
    if (n <= 1) return n;
    int a = 0, b = 1;
    for (int i = 2; i <= n; i++) {
        int c = a + b;
        a = b;
        b = c;
    }
    return b;
}

int main(void) {
    for (int i = 0; i < 10; i++) {
        printf("fib(%d) = %d\n", i, fib_89(i));
    }
    return EXIT_SUCCESS;
}
```

```cpp
#include <vector>
#include <string>
#include <iostream>

// 简单的模板类，用于BENCH-C语料填充(C++样例90)
template <typename T>
class Stack90 {
public:
    void push(const T& v) { data_.push_back(v); }
    T pop() {
        T v = data_.back();
        data_.pop_back();
        return v;
    }
    bool empty() const noexcept { return data_.empty(); }
private:
    std::vector<T> data_;
};

int main() {
    Stack90<std::string> s;
    s.push("hello");
    s.push("world");
    while (!s.empty()) {
        std::cout << s.pop() << std::endl;
    }
    return 0;
}
```

```csharp
using System;
using System.Collections.Generic;

// 简单的仓库模式示例，用于BENCH-C语料填充(C#样例91)
namespace BenchSample91
{
    public class Repository<T>
    {
        private readonly List<T> _items = new List<T>();

        public void Add(T item) => _items.Add(item);

        public IEnumerable<T> All() => _items;

        public int Count => _items.Count;
    }

    class Program
    {
        static void Main(string[] args)
        {
            var repo = new Repository<string>();
            repo.Add("alpha");
            repo.Add("beta");
            Console.WriteLine($"count={repo.Count}");
        }
    }
}
```

```java
import java.util.ArrayList;
import java.util.List;

// 简单的观察者模式示例，用于BENCH-C语料填充(Java样例92)
public class Publisher92 {
    private final List<Runnable> listeners = new ArrayList<>();

    public void subscribe(Runnable listener) {
        listeners.add(listener);
    }

    public void publish() {
        for (Runnable listener : listeners) {
            listener.run();
        }
    }

    public static void main(String[] args) {
        Publisher92 pub = new Publisher92();
        pub.subscribe(() -> System.out.println("event fired 92"));
        pub.publish();
    }
}
```

```js
// 简单的事件发射器示例，用于BENCH-C语料填充(JavaScript样例93)
class EventBus93 {
    constructor() {
        this.handlers = {};
    }

    on(name, fn) {
        (this.handlers[name] ||= []).push(fn);
    }

    emit(name, payload) {
        (this.handlers[name] || []).forEach((fn) => fn(payload));
    }
}

const bus = new EventBus93();
bus.on("tick", (n) => console.log(`tick ${n}`));
bus.emit("tick", 93);
```

```python
# 简单的装饰器示例，用于BENCH-C语料填充(Python样例94)
import functools
import time


def timed_94(fn):
    @functools.wraps(fn)
    def wrapper(*args, **kwargs):
        start = time.perf_counter()
        result = fn(*args, **kwargs)
        elapsed = time.perf_counter() - start
        print(f"{fn.__name__} took {elapsed:.4f}s")
        return result
    return wrapper


@timed_94
def compute_94(n):
    return sum(i * i for i in range(n))


if __name__ == "__main__":
    print(compute_94(1000))
```

```go
package main

import (
	"fmt"
	"sync"
)

// 简单的并发计数器示例，用于BENCH-C语料填充(Go样例95)
type Counter95 struct {
	mu    sync.Mutex
	value int
}

func (c *Counter95) Inc() {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.value++
}

func main() {
	c := &Counter95{}
	var wg sync.WaitGroup
	for i := 0; i < 100; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			c.Inc()
		}()
	}
	wg.Wait()
	fmt.Println("value:", c.value)
}
```

```rust
use std::collections::HashMap;

// 简单的LRU缓存骨架示例，用于BENCH-C语料填充(Rust样例96)
struct Cache96 {
    map: HashMap<String, i64>,
    capacity: usize,
}

impl Cache96 {
    fn new(capacity: usize) -> Self {
        Cache96 { map: HashMap::new(), capacity }
    }

    fn put(&mut self, key: String, value: i64) {
        if self.map.len() >= self.capacity {
            if let Some(k) = self.map.keys().next().cloned() {
                self.map.remove(&k);
            }
        }
        self.map.insert(key, value);
    }
}

fn main() {
    let mut cache = Cache96::new(16);
    cache.put("a".to_string(), 96);
    println!("{:?}", cache.map.get("a"));
}
```

```json
{
  "sample_id": 97,
  "name": "bench-c-fixture-97",
  "tags": ["highlight", "bench", "json"],
  "metrics": {
    "first_paint_ms": 61.255,
    "delta_ms": -2.469,
    "exe_delta_bytes": 14336
  },
  "languages": ["c", "cpp", "csharp", "java", "js", "python", "go", "rust", "json", "yaml", "sh"],
  "active": true
}
```

```yaml
# BENCH-C YAML样例98，用于覆盖YAML高亮规则
sample_id: 98
name: bench-c-fixture-98
tags:
  - highlight
  - bench
  - yaml
metrics:
  first_paint_ms: 61.255
  exe_delta_bytes: 14336
active: true
```

```sh
#!/bin/bash
# BENCH-C shell样例99，用于覆盖shell高亮规则
set -euo pipefail

BENCH_ID=99
LOG_FILE="/tmp/bench_99.log"

run_once() {
    echo "running bench iteration ${BENCH_ID}" >> "$LOG_FILE"
    for i in $(seq 1 3); do
        echo "step $i" >> "$LOG_FILE"
    done
}

run_once
```

## 第 10 节：本节为 BENCH-C 语料的说明性文字，穿插在代码块之间，用于让代码块占比保持在约 50% 以上但不至于 100%。BENCH-C 是 T54 新增的“高亮最坏情况”记录语料：约 100KB、代码块占比不低于 50%、覆盖全部 11 种受支持的高亮语言（C/C++/C#/Java/JavaScript/Python/Go/Rust/JSON/YAML/Shell）。本文件不设门禁，仅用于记录首屏时间与内存在高亮最坏场景下的数值。

```c
#include <stdio.h>
#include <stdlib.h>

// 计算斐波那契数列第n项，用于BENCH-C语料填充(C语言样例100)
int fib_100(int n) {
    if (n <= 1) return n;
    int a = 0, b = 1;
    for (int i = 2; i <= n; i++) {
        int c = a + b;
        a = b;
        b = c;
    }
    return b;
}

int main(void) {
    for (int i = 0; i < 10; i++) {
        printf("fib(%d) = %d\n", i, fib_100(i));
    }
    return EXIT_SUCCESS;
}
```

```cpp
#include <vector>
#include <string>
#include <iostream>

// 简单的模板类，用于BENCH-C语料填充(C++样例101)
template <typename T>
class Stack101 {
public:
    void push(const T& v) { data_.push_back(v); }
    T pop() {
        T v = data_.back();
        data_.pop_back();
        return v;
    }
    bool empty() const noexcept { return data_.empty(); }
private:
    std::vector<T> data_;
};

int main() {
    Stack101<std::string> s;
    s.push("hello");
    s.push("world");
    while (!s.empty()) {
        std::cout << s.pop() << std::endl;
    }
    return 0;
}
```

```csharp
using System;
using System.Collections.Generic;

// 简单的仓库模式示例，用于BENCH-C语料填充(C#样例102)
namespace BenchSample102
{
    public class Repository<T>
    {
        private readonly List<T> _items = new List<T>();

        public void Add(T item) => _items.Add(item);

        public IEnumerable<T> All() => _items;

        public int Count => _items.Count;
    }

    class Program
    {
        static void Main(string[] args)
        {
            var repo = new Repository<string>();
            repo.Add("alpha");
            repo.Add("beta");
            Console.WriteLine($"count={repo.Count}");
        }
    }
}
```

```java
import java.util.ArrayList;
import java.util.List;

// 简单的观察者模式示例，用于BENCH-C语料填充(Java样例103)
public class Publisher103 {
    private final List<Runnable> listeners = new ArrayList<>();

    public void subscribe(Runnable listener) {
        listeners.add(listener);
    }

    public void publish() {
        for (Runnable listener : listeners) {
            listener.run();
        }
    }

    public static void main(String[] args) {
        Publisher103 pub = new Publisher103();
        pub.subscribe(() -> System.out.println("event fired 103"));
        pub.publish();
    }
}
```

```js
// 简单的事件发射器示例，用于BENCH-C语料填充(JavaScript样例104)
class EventBus104 {
    constructor() {
        this.handlers = {};
    }

    on(name, fn) {
        (this.handlers[name] ||= []).push(fn);
    }

    emit(name, payload) {
        (this.handlers[name] || []).forEach((fn) => fn(payload));
    }
}

const bus = new EventBus104();
bus.on("tick", (n) => console.log(`tick ${n}`));
bus.emit("tick", 104);
```

```python
# 简单的装饰器示例，用于BENCH-C语料填充(Python样例105)
import functools
import time


def timed_105(fn):
    @functools.wraps(fn)
    def wrapper(*args, **kwargs):
        start = time.perf_counter()
        result = fn(*args, **kwargs)
        elapsed = time.perf_counter() - start
        print(f"{fn.__name__} took {elapsed:.4f}s")
        return result
    return wrapper


@timed_105
def compute_105(n):
    return sum(i * i for i in range(n))


if __name__ == "__main__":
    print(compute_105(1000))
```

```go
package main

import (
	"fmt"
	"sync"
)

// 简单的并发计数器示例，用于BENCH-C语料填充(Go样例106)
type Counter106 struct {
	mu    sync.Mutex
	value int
}

func (c *Counter106) Inc() {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.value++
}

func main() {
	c := &Counter106{}
	var wg sync.WaitGroup
	for i := 0; i < 100; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			c.Inc()
		}()
	}
	wg.Wait()
	fmt.Println("value:", c.value)
}
```

```rust
use std::collections::HashMap;

// 简单的LRU缓存骨架示例，用于BENCH-C语料填充(Rust样例107)
struct Cache107 {
    map: HashMap<String, i64>,
    capacity: usize,
}

impl Cache107 {
    fn new(capacity: usize) -> Self {
        Cache107 { map: HashMap::new(), capacity }
    }

    fn put(&mut self, key: String, value: i64) {
        if self.map.len() >= self.capacity {
            if let Some(k) = self.map.keys().next().cloned() {
                self.map.remove(&k);
            }
        }
        self.map.insert(key, value);
    }
}

fn main() {
    let mut cache = Cache107::new(16);
    cache.put("a".to_string(), 107);
    println!("{:?}", cache.map.get("a"));
}
```

```json
{
  "sample_id": 108,
  "name": "bench-c-fixture-108",
  "tags": ["highlight", "bench", "json"],
  "metrics": {
    "first_paint_ms": 61.255,
    "delta_ms": -2.469,
    "exe_delta_bytes": 14336
  },
  "languages": ["c", "cpp", "csharp", "java", "js", "python", "go", "rust", "json", "yaml", "sh"],
  "active": true
}
```

```yaml
# BENCH-C YAML样例109，用于覆盖YAML高亮规则
sample_id: 109
name: bench-c-fixture-109
tags:
  - highlight
  - bench
  - yaml
metrics:
  first_paint_ms: 61.255
  exe_delta_bytes: 14336
active: true
```

```sh
#!/bin/bash
# BENCH-C shell样例110，用于覆盖shell高亮规则
set -euo pipefail

BENCH_ID=110
LOG_FILE="/tmp/bench_110.log"

run_once() {
    echo "running bench iteration ${BENCH_ID}" >> "$LOG_FILE"
    for i in $(seq 1 3); do
        echo "step $i" >> "$LOG_FILE"
    done
}

run_once
```

## 第 11 节：本节为 BENCH-C 语料的说明性文字，穿插在代码块之间，用于让代码块占比保持在约 50% 以上但不至于 100%。BENCH-C 是 T54 新增的“高亮最坏情况”记录语料：约 100KB、代码块占比不低于 50%、覆盖全部 11 种受支持的高亮语言（C/C++/C#/Java/JavaScript/Python/Go/Rust/JSON/YAML/Shell）。本文件不设门禁，仅用于记录首屏时间与内存在高亮最坏场景下的数值。

```c
#include <stdio.h>
#include <stdlib.h>

// 计算斐波那契数列第n项，用于BENCH-C语料填充(C语言样例111)
int fib_111(int n) {
    if (n <= 1) return n;
    int a = 0, b = 1;
    for (int i = 2; i <= n; i++) {
        int c = a + b;
        a = b;
        b = c;
    }
    return b;
}

int main(void) {
    for (int i = 0; i < 10; i++) {
        printf("fib(%d) = %d\n", i, fib_111(i));
    }
    return EXIT_SUCCESS;
}
```

```cpp
#include <vector>
#include <string>
#include <iostream>

// 简单的模板类，用于BENCH-C语料填充(C++样例112)
template <typename T>
class Stack112 {
public:
    void push(const T& v) { data_.push_back(v); }
    T pop() {
        T v = data_.back();
        data_.pop_back();
        return v;
    }
    bool empty() const noexcept { return data_.empty(); }
private:
    std::vector<T> data_;
};

int main() {
    Stack112<std::string> s;
    s.push("hello");
    s.push("world");
    while (!s.empty()) {
        std::cout << s.pop() << std::endl;
    }
    return 0;
}
```

```csharp
using System;
using System.Collections.Generic;

// 简单的仓库模式示例，用于BENCH-C语料填充(C#样例113)
namespace BenchSample113
{
    public class Repository<T>
    {
        private readonly List<T> _items = new List<T>();

        public void Add(T item) => _items.Add(item);

        public IEnumerable<T> All() => _items;

        public int Count => _items.Count;
    }

    class Program
    {
        static void Main(string[] args)
        {
            var repo = new Repository<string>();
            repo.Add("alpha");
            repo.Add("beta");
            Console.WriteLine($"count={repo.Count}");
        }
    }
}
```

```java
import java.util.ArrayList;
import java.util.List;

// 简单的观察者模式示例，用于BENCH-C语料填充(Java样例114)
public class Publisher114 {
    private final List<Runnable> listeners = new ArrayList<>();

    public void subscribe(Runnable listener) {
        listeners.add(listener);
    }

    public void publish() {
        for (Runnable listener : listeners) {
            listener.run();
        }
    }

    public static void main(String[] args) {
        Publisher114 pub = new Publisher114();
        pub.subscribe(() -> System.out.println("event fired 114"));
        pub.publish();
    }
}
```

```js
// 简单的事件发射器示例，用于BENCH-C语料填充(JavaScript样例115)
class EventBus115 {
    constructor() {
        this.handlers = {};
    }

    on(name, fn) {
        (this.handlers[name] ||= []).push(fn);
    }

    emit(name, payload) {
        (this.handlers[name] || []).forEach((fn) => fn(payload));
    }
}

const bus = new EventBus115();
bus.on("tick", (n) => console.log(`tick ${n}`));
bus.emit("tick", 115);
```

```python
# 简单的装饰器示例，用于BENCH-C语料填充(Python样例116)
import functools
import time


def timed_116(fn):
    @functools.wraps(fn)
    def wrapper(*args, **kwargs):
        start = time.perf_counter()
        result = fn(*args, **kwargs)
        elapsed = time.perf_counter() - start
        print(f"{fn.__name__} took {elapsed:.4f}s")
        return result
    return wrapper


@timed_116
def compute_116(n):
    return sum(i * i for i in range(n))


if __name__ == "__main__":
    print(compute_116(1000))
```

```go
package main

import (
	"fmt"
	"sync"
)

// 简单的并发计数器示例，用于BENCH-C语料填充(Go样例117)
type Counter117 struct {
	mu    sync.Mutex
	value int
}

func (c *Counter117) Inc() {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.value++
}

func main() {
	c := &Counter117{}
	var wg sync.WaitGroup
	for i := 0; i < 100; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			c.Inc()
		}()
	}
	wg.Wait()
	fmt.Println("value:", c.value)
}
```

```rust
use std::collections::HashMap;

// 简单的LRU缓存骨架示例，用于BENCH-C语料填充(Rust样例118)
struct Cache118 {
    map: HashMap<String, i64>,
    capacity: usize,
}

impl Cache118 {
    fn new(capacity: usize) -> Self {
        Cache118 { map: HashMap::new(), capacity }
    }

    fn put(&mut self, key: String, value: i64) {
        if self.map.len() >= self.capacity {
            if let Some(k) = self.map.keys().next().cloned() {
                self.map.remove(&k);
            }
        }
        self.map.insert(key, value);
    }
}

fn main() {
    let mut cache = Cache118::new(16);
    cache.put("a".to_string(), 118);
    println!("{:?}", cache.map.get("a"));
}
```

```json
{
  "sample_id": 119,
  "name": "bench-c-fixture-119",
  "tags": ["highlight", "bench", "json"],
  "metrics": {
    "first_paint_ms": 61.255,
    "delta_ms": -2.469,
    "exe_delta_bytes": 14336
  },
  "languages": ["c", "cpp", "csharp", "java", "js", "python", "go", "rust", "json", "yaml", "sh"],
  "active": true
}
```

```yaml
# BENCH-C YAML样例120，用于覆盖YAML高亮规则
sample_id: 120
name: bench-c-fixture-120
tags:
  - highlight
  - bench
  - yaml
metrics:
  first_paint_ms: 61.255
  exe_delta_bytes: 14336
active: true
```

```sh
#!/bin/bash
# BENCH-C shell样例121，用于覆盖shell高亮规则
set -euo pipefail

BENCH_ID=121
LOG_FILE="/tmp/bench_121.log"

run_once() {
    echo "running bench iteration ${BENCH_ID}" >> "$LOG_FILE"
    for i in $(seq 1 3); do
        echo "step $i" >> "$LOG_FILE"
    done
}

run_once
```

## 第 12 节：本节为 BENCH-C 语料的说明性文字，穿插在代码块之间，用于让代码块占比保持在约 50% 以上但不至于 100%。BENCH-C 是 T54 新增的“高亮最坏情况”记录语料：约 100KB、代码块占比不低于 50%、覆盖全部 11 种受支持的高亮语言（C/C++/C#/Java/JavaScript/Python/Go/Rust/JSON/YAML/Shell）。本文件不设门禁，仅用于记录首屏时间与内存在高亮最坏场景下的数值。

```c
#include <stdio.h>
#include <stdlib.h>

// 计算斐波那契数列第n项，用于BENCH-C语料填充(C语言样例122)
int fib_122(int n) {
    if (n <= 1) return n;
    int a = 0, b = 1;
    for (int i = 2; i <= n; i++) {
        int c = a + b;
        a = b;
        b = c;
    }
    return b;
}

int main(void) {
    for (int i = 0; i < 10; i++) {
        printf("fib(%d) = %d\n", i, fib_122(i));
    }
    return EXIT_SUCCESS;
}
```

```cpp
#include <vector>
#include <string>
#include <iostream>

// 简单的模板类，用于BENCH-C语料填充(C++样例123)
template <typename T>
class Stack123 {
public:
    void push(const T& v) { data_.push_back(v); }
    T pop() {
        T v = data_.back();
        data_.pop_back();
        return v;
    }
    bool empty() const noexcept { return data_.empty(); }
private:
    std::vector<T> data_;
};

int main() {
    Stack123<std::string> s;
    s.push("hello");
    s.push("world");
    while (!s.empty()) {
        std::cout << s.pop() << std::endl;
    }
    return 0;
}
```

```csharp
using System;
using System.Collections.Generic;

// 简单的仓库模式示例，用于BENCH-C语料填充(C#样例124)
namespace BenchSample124
{
    public class Repository<T>
    {
        private readonly List<T> _items = new List<T>();

        public void Add(T item) => _items.Add(item);

        public IEnumerable<T> All() => _items;

        public int Count => _items.Count;
    }

    class Program
    {
        static void Main(string[] args)
        {
            var repo = new Repository<string>();
            repo.Add("alpha");
            repo.Add("beta");
            Console.WriteLine($"count={repo.Count}");
        }
    }
}
```

```java
import java.util.ArrayList;
import java.util.List;

// 简单的观察者模式示例，用于BENCH-C语料填充(Java样例125)
public class Publisher125 {
    private final List<Runnable> listeners = new ArrayList<>();

    public void subscribe(Runnable listener) {
        listeners.add(listener);
    }

    public void publish() {
        for (Runnable listener : listeners) {
            listener.run();
        }
    }

    public static void main(String[] args) {
        Publisher125 pub = new Publisher125();
        pub.subscribe(() -> System.out.println("event fired 125"));
        pub.publish();
    }
}
```

```js
// 简单的事件发射器示例，用于BENCH-C语料填充(JavaScript样例126)
class EventBus126 {
    constructor() {
        this.handlers = {};
    }

    on(name, fn) {
        (this.handlers[name] ||= []).push(fn);
    }

    emit(name, payload) {
        (this.handlers[name] || []).forEach((fn) => fn(payload));
    }
}

const bus = new EventBus126();
bus.on("tick", (n) => console.log(`tick ${n}`));
bus.emit("tick", 126);
```

```python
# 简单的装饰器示例，用于BENCH-C语料填充(Python样例127)
import functools
import time


def timed_127(fn):
    @functools.wraps(fn)
    def wrapper(*args, **kwargs):
        start = time.perf_counter()
        result = fn(*args, **kwargs)
        elapsed = time.perf_counter() - start
        print(f"{fn.__name__} took {elapsed:.4f}s")
        return result
    return wrapper


@timed_127
def compute_127(n):
    return sum(i * i for i in range(n))


if __name__ == "__main__":
    print(compute_127(1000))
```

```go
package main

import (
	"fmt"
	"sync"
)

// 简单的并发计数器示例，用于BENCH-C语料填充(Go样例128)
type Counter128 struct {
	mu    sync.Mutex
	value int
}

func (c *Counter128) Inc() {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.value++
}

func main() {
	c := &Counter128{}
	var wg sync.WaitGroup
	for i := 0; i < 100; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			c.Inc()
		}()
	}
	wg.Wait()
	fmt.Println("value:", c.value)
}
```

```rust
use std::collections::HashMap;

// 简单的LRU缓存骨架示例，用于BENCH-C语料填充(Rust样例129)
struct Cache129 {
    map: HashMap<String, i64>,
    capacity: usize,
}

impl Cache129 {
    fn new(capacity: usize) -> Self {
        Cache129 { map: HashMap::new(), capacity }
    }

    fn put(&mut self, key: String, value: i64) {
        if self.map.len() >= self.capacity {
            if let Some(k) = self.map.keys().next().cloned() {
                self.map.remove(&k);
            }
        }
        self.map.insert(key, value);
    }
}

fn main() {
    let mut cache = Cache129::new(16);
    cache.put("a".to_string(), 129);
    println!("{:?}", cache.map.get("a"));
}
```

```json
{
  "sample_id": 130,
  "name": "bench-c-fixture-130",
  "tags": ["highlight", "bench", "json"],
  "metrics": {
    "first_paint_ms": 61.255,
    "delta_ms": -2.469,
    "exe_delta_bytes": 14336
  },
  "languages": ["c", "cpp", "csharp", "java", "js", "python", "go", "rust", "json", "yaml", "sh"],
  "active": true
}
```

```yaml
# BENCH-C YAML样例131，用于覆盖YAML高亮规则
sample_id: 131
name: bench-c-fixture-131
tags:
  - highlight
  - bench
  - yaml
metrics:
  first_paint_ms: 61.255
  exe_delta_bytes: 14336
active: true
```

```sh
#!/bin/bash
# BENCH-C shell样例132，用于覆盖shell高亮规则
set -euo pipefail

BENCH_ID=132
LOG_FILE="/tmp/bench_132.log"

run_once() {
    echo "running bench iteration ${BENCH_ID}" >> "$LOG_FILE"
    for i in $(seq 1 3); do
        echo "step $i" >> "$LOG_FILE"
    done
}

run_once
```

## 第 13 节：本节为 BENCH-C 语料的说明性文字，穿插在代码块之间，用于让代码块占比保持在约 50% 以上但不至于 100%。BENCH-C 是 T54 新增的“高亮最坏情况”记录语料：约 100KB、代码块占比不低于 50%、覆盖全部 11 种受支持的高亮语言（C/C++/C#/Java/JavaScript/Python/Go/Rust/JSON/YAML/Shell）。本文件不设门禁，仅用于记录首屏时间与内存在高亮最坏场景下的数值。

```c
#include <stdio.h>
#include <stdlib.h>

// 计算斐波那契数列第n项，用于BENCH-C语料填充(C语言样例133)
int fib_133(int n) {
    if (n <= 1) return n;
    int a = 0, b = 1;
    for (int i = 2; i <= n; i++) {
        int c = a + b;
        a = b;
        b = c;
    }
    return b;
}

int main(void) {
    for (int i = 0; i < 10; i++) {
        printf("fib(%d) = %d\n", i, fib_133(i));
    }
    return EXIT_SUCCESS;
}
```

```cpp
#include <vector>
#include <string>
#include <iostream>

// 简单的模板类，用于BENCH-C语料填充(C++样例134)
template <typename T>
class Stack134 {
public:
    void push(const T& v) { data_.push_back(v); }
    T pop() {
        T v = data_.back();
        data_.pop_back();
        return v;
    }
    bool empty() const noexcept { return data_.empty(); }
private:
    std::vector<T> data_;
};

int main() {
    Stack134<std::string> s;
    s.push("hello");
    s.push("world");
    while (!s.empty()) {
        std::cout << s.pop() << std::endl;
    }
    return 0;
}
```

```csharp
using System;
using System.Collections.Generic;

// 简单的仓库模式示例，用于BENCH-C语料填充(C#样例135)
namespace BenchSample135
{
    public class Repository<T>
    {
        private readonly List<T> _items = new List<T>();

        public void Add(T item) => _items.Add(item);

        public IEnumerable<T> All() => _items;

        public int Count => _items.Count;
    }

    class Program
    {
        static void Main(string[] args)
        {
            var repo = new Repository<string>();
            repo.Add("alpha");
            repo.Add("beta");
            Console.WriteLine($"count={repo.Count}");
        }
    }
}
```

```java
import java.util.ArrayList;
import java.util.List;

// 简单的观察者模式示例，用于BENCH-C语料填充(Java样例136)
public class Publisher136 {
    private final List<Runnable> listeners = new ArrayList<>();

    public void subscribe(Runnable listener) {
        listeners.add(listener);
    }

    public void publish() {
        for (Runnable listener : listeners) {
            listener.run();
        }
    }

    public static void main(String[] args) {
        Publisher136 pub = new Publisher136();
        pub.subscribe(() -> System.out.println("event fired 136"));
        pub.publish();
    }
}
```

```js
// 简单的事件发射器示例，用于BENCH-C语料填充(JavaScript样例137)
class EventBus137 {
    constructor() {
        this.handlers = {};
    }

    on(name, fn) {
        (this.handlers[name] ||= []).push(fn);
    }

    emit(name, payload) {
        (this.handlers[name] || []).forEach((fn) => fn(payload));
    }
}

const bus = new EventBus137();
bus.on("tick", (n) => console.log(`tick ${n}`));
bus.emit("tick", 137);
```

```python
# 简单的装饰器示例，用于BENCH-C语料填充(Python样例138)
import functools
import time


def timed_138(fn):
    @functools.wraps(fn)
    def wrapper(*args, **kwargs):
        start = time.perf_counter()
        result = fn(*args, **kwargs)
        elapsed = time.perf_counter() - start
        print(f"{fn.__name__} took {elapsed:.4f}s")
        return result
    return wrapper


@timed_138
def compute_138(n):
    return sum(i * i for i in range(n))


if __name__ == "__main__":
    print(compute_138(1000))
```

```go
package main

import (
	"fmt"
	"sync"
)

// 简单的并发计数器示例，用于BENCH-C语料填充(Go样例139)
type Counter139 struct {
	mu    sync.Mutex
	value int
}

func (c *Counter139) Inc() {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.value++
}

func main() {
	c := &Counter139{}
	var wg sync.WaitGroup
	for i := 0; i < 100; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			c.Inc()
		}()
	}
	wg.Wait()
	fmt.Println("value:", c.value)
}
```

```rust
use std::collections::HashMap;

// 简单的LRU缓存骨架示例，用于BENCH-C语料填充(Rust样例140)
struct Cache140 {
    map: HashMap<String, i64>,
    capacity: usize,
}

impl Cache140 {
    fn new(capacity: usize) -> Self {
        Cache140 { map: HashMap::new(), capacity }
    }

    fn put(&mut self, key: String, value: i64) {
        if self.map.len() >= self.capacity {
            if let Some(k) = self.map.keys().next().cloned() {
                self.map.remove(&k);
            }
        }
        self.map.insert(key, value);
    }
}

fn main() {
    let mut cache = Cache140::new(16);
    cache.put("a".to_string(), 140);
    println!("{:?}", cache.map.get("a"));
}
```

```json
{
  "sample_id": 141,
  "name": "bench-c-fixture-141",
  "tags": ["highlight", "bench", "json"],
  "metrics": {
    "first_paint_ms": 61.255,
    "delta_ms": -2.469,
    "exe_delta_bytes": 14336
  },
  "languages": ["c", "cpp", "csharp", "java", "js", "python", "go", "rust", "json", "yaml", "sh"],
  "active": true
}
```

```yaml
# BENCH-C YAML样例142，用于覆盖YAML高亮规则
sample_id: 142
name: bench-c-fixture-142
tags:
  - highlight
  - bench
  - yaml
metrics:
  first_paint_ms: 61.255
  exe_delta_bytes: 14336
active: true
```

```sh
#!/bin/bash
# BENCH-C shell样例143，用于覆盖shell高亮规则
set -euo pipefail

BENCH_ID=143
LOG_FILE="/tmp/bench_143.log"

run_once() {
    echo "running bench iteration ${BENCH_ID}" >> "$LOG_FILE"
    for i in $(seq 1 3); do
        echo "step $i" >> "$LOG_FILE"
    done
}

run_once
```

## 第 14 节：本节为 BENCH-C 语料的说明性文字，穿插在代码块之间，用于让代码块占比保持在约 50% 以上但不至于 100%。BENCH-C 是 T54 新增的“高亮最坏情况”记录语料：约 100KB、代码块占比不低于 50%、覆盖全部 11 种受支持的高亮语言（C/C++/C#/Java/JavaScript/Python/Go/Rust/JSON/YAML/Shell）。本文件不设门禁，仅用于记录首屏时间与内存在高亮最坏场景下的数值。

```c
#include <stdio.h>
#include <stdlib.h>

// 计算斐波那契数列第n项，用于BENCH-C语料填充(C语言样例144)
int fib_144(int n) {
    if (n <= 1) return n;
    int a = 0, b = 1;
    for (int i = 2; i <= n; i++) {
        int c = a + b;
        a = b;
        b = c;
    }
    return b;
}

int main(void) {
    for (int i = 0; i < 10; i++) {
        printf("fib(%d) = %d\n", i, fib_144(i));
    }
    return EXIT_SUCCESS;
}
```

```cpp
#include <vector>
#include <string>
#include <iostream>

// 简单的模板类，用于BENCH-C语料填充(C++样例145)
template <typename T>
class Stack145 {
public:
    void push(const T& v) { data_.push_back(v); }
    T pop() {
        T v = data_.back();
        data_.pop_back();
        return v;
    }
    bool empty() const noexcept { return data_.empty(); }
private:
    std::vector<T> data_;
};

int main() {
    Stack145<std::string> s;
    s.push("hello");
    s.push("world");
    while (!s.empty()) {
        std::cout << s.pop() << std::endl;
    }
    return 0;
}
```

```csharp
using System;
using System.Collections.Generic;

// 简单的仓库模式示例，用于BENCH-C语料填充(C#样例146)
namespace BenchSample146
{
    public class Repository<T>
    {
        private readonly List<T> _items = new List<T>();

        public void Add(T item) => _items.Add(item);

        public IEnumerable<T> All() => _items;

        public int Count => _items.Count;
    }

    class Program
    {
        static void Main(string[] args)
        {
            var repo = new Repository<string>();
            repo.Add("alpha");
            repo.Add("beta");
            Console.WriteLine($"count={repo.Count}");
        }
    }
}
```

```java
import java.util.ArrayList;
import java.util.List;

// 简单的观察者模式示例，用于BENCH-C语料填充(Java样例147)
public class Publisher147 {
    private final List<Runnable> listeners = new ArrayList<>();

    public void subscribe(Runnable listener) {
        listeners.add(listener);
    }

    public void publish() {
        for (Runnable listener : listeners) {
            listener.run();
        }
    }

    public static void main(String[] args) {
        Publisher147 pub = new Publisher147();
        pub.subscribe(() -> System.out.println("event fired 147"));
        pub.publish();
    }
}
```

```js
// 简单的事件发射器示例，用于BENCH-C语料填充(JavaScript样例148)
class EventBus148 {
    constructor() {
        this.handlers = {};
    }

    on(name, fn) {
        (this.handlers[name] ||= []).push(fn);
    }

    emit(name, payload) {
        (this.handlers[name] || []).forEach((fn) => fn(payload));
    }
}

const bus = new EventBus148();
bus.on("tick", (n) => console.log(`tick ${n}`));
bus.emit("tick", 148);
```

```python
# 简单的装饰器示例，用于BENCH-C语料填充(Python样例149)
import functools
import time


def timed_149(fn):
    @functools.wraps(fn)
    def wrapper(*args, **kwargs):
        start = time.perf_counter()
        result = fn(*args, **kwargs)
        elapsed = time.perf_counter() - start
        print(f"{fn.__name__} took {elapsed:.4f}s")
        return result
    return wrapper


@timed_149
def compute_149(n):
    return sum(i * i for i in range(n))


if __name__ == "__main__":
    print(compute_149(1000))
```

```go
package main

import (
	"fmt"
	"sync"
)

// 简单的并发计数器示例，用于BENCH-C语料填充(Go样例150)
type Counter150 struct {
	mu    sync.Mutex
	value int
}

func (c *Counter150) Inc() {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.value++
}

func main() {
	c := &Counter150{}
	var wg sync.WaitGroup
	for i := 0; i < 100; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			c.Inc()
		}()
	}
	wg.Wait()
	fmt.Println("value:", c.value)
}
```

```rust
use std::collections::HashMap;

// 简单的LRU缓存骨架示例，用于BENCH-C语料填充(Rust样例151)
struct Cache151 {
    map: HashMap<String, i64>,
    capacity: usize,
}

impl Cache151 {
    fn new(capacity: usize) -> Self {
        Cache151 { map: HashMap::new(), capacity }
    }

    fn put(&mut self, key: String, value: i64) {
        if self.map.len() >= self.capacity {
            if let Some(k) = self.map.keys().next().cloned() {
                self.map.remove(&k);
            }
        }
        self.map.insert(key, value);
    }
}

fn main() {
    let mut cache = Cache151::new(16);
    cache.put("a".to_string(), 151);
    println!("{:?}", cache.map.get("a"));
}
```

```json
{
  "sample_id": 152,
  "name": "bench-c-fixture-152",
  "tags": ["highlight", "bench", "json"],
  "metrics": {
    "first_paint_ms": 61.255,
    "delta_ms": -2.469,
    "exe_delta_bytes": 14336
  },
  "languages": ["c", "cpp", "csharp", "java", "js", "python", "go", "rust", "json", "yaml", "sh"],
  "active": true
}
```

```yaml
# BENCH-C YAML样例153，用于覆盖YAML高亮规则
sample_id: 153
name: bench-c-fixture-153
tags:
  - highlight
  - bench
  - yaml
metrics:
  first_paint_ms: 61.255
  exe_delta_bytes: 14336
active: true
```

```sh
#!/bin/bash
# BENCH-C shell样例154，用于覆盖shell高亮规则
set -euo pipefail

BENCH_ID=154
LOG_FILE="/tmp/bench_154.log"

run_once() {
    echo "running bench iteration ${BENCH_ID}" >> "$LOG_FILE"
    for i in $(seq 1 3); do
        echo "step $i" >> "$LOG_FILE"
    done
}

run_once
```

## 第 15 节：本节为 BENCH-C 语料的说明性文字，穿插在代码块之间，用于让代码块占比保持在约 50% 以上但不至于 100%。BENCH-C 是 T54 新增的“高亮最坏情况”记录语料：约 100KB、代码块占比不低于 50%、覆盖全部 11 种受支持的高亮语言（C/C++/C#/Java/JavaScript/Python/Go/Rust/JSON/YAML/Shell）。本文件不设门禁，仅用于记录首屏时间与内存在高亮最坏场景下的数值。

```c
#include <stdio.h>
#include <stdlib.h>

// 计算斐波那契数列第n项，用于BENCH-C语料填充(C语言样例155)
int fib_155(int n) {
    if (n <= 1) return n;
    int a = 0, b = 1;
    for (int i = 2; i <= n; i++) {
        int c = a + b;
        a = b;
        b = c;
    }
    return b;
}

int main(void) {
    for (int i = 0; i < 10; i++) {
        printf("fib(%d) = %d\n", i, fib_155(i));
    }
    return EXIT_SUCCESS;
}
```

```cpp
#include <vector>
#include <string>
#include <iostream>

// 简单的模板类，用于BENCH-C语料填充(C++样例156)
template <typename T>
class Stack156 {
public:
    void push(const T& v) { data_.push_back(v); }
    T pop() {
        T v = data_.back();
        data_.pop_back();
        return v;
    }
    bool empty() const noexcept { return data_.empty(); }
private:
    std::vector<T> data_;
};

int main() {
    Stack156<std::string> s;
    s.push("hello");
    s.push("world");
    while (!s.empty()) {
        std::cout << s.pop() << std::endl;
    }
    return 0;
}
```

```csharp
using System;
using System.Collections.Generic;

// 简单的仓库模式示例，用于BENCH-C语料填充(C#样例157)
namespace BenchSample157
{
    public class Repository<T>
    {
        private readonly List<T> _items = new List<T>();

        public void Add(T item) => _items.Add(item);

        public IEnumerable<T> All() => _items;

        public int Count => _items.Count;
    }

    class Program
    {
        static void Main(string[] args)
        {
            var repo = new Repository<string>();
            repo.Add("alpha");
            repo.Add("beta");
            Console.WriteLine($"count={repo.Count}");
        }
    }
}
```

```java
import java.util.ArrayList;
import java.util.List;

// 简单的观察者模式示例，用于BENCH-C语料填充(Java样例158)
public class Publisher158 {
    private final List<Runnable> listeners = new ArrayList<>();

    public void subscribe(Runnable listener) {
        listeners.add(listener);
    }

    public void publish() {
        for (Runnable listener : listeners) {
            listener.run();
        }
    }

    public static void main(String[] args) {
        Publisher158 pub = new Publisher158();
        pub.subscribe(() -> System.out.println("event fired 158"));
        pub.publish();
    }
}
```

```js
// 简单的事件发射器示例，用于BENCH-C语料填充(JavaScript样例159)
class EventBus159 {
    constructor() {
        this.handlers = {};
    }

    on(name, fn) {
        (this.handlers[name] ||= []).push(fn);
    }

    emit(name, payload) {
        (this.handlers[name] || []).forEach((fn) => fn(payload));
    }
}

const bus = new EventBus159();
bus.on("tick", (n) => console.log(`tick ${n}`));
bus.emit("tick", 159);
```

```python
# 简单的装饰器示例，用于BENCH-C语料填充(Python样例160)
import functools
import time


def timed_160(fn):
    @functools.wraps(fn)
    def wrapper(*args, **kwargs):
        start = time.perf_counter()
        result = fn(*args, **kwargs)
        elapsed = time.perf_counter() - start
        print(f"{fn.__name__} took {elapsed:.4f}s")
        return result
    return wrapper


@timed_160
def compute_160(n):
    return sum(i * i for i in range(n))


if __name__ == "__main__":
    print(compute_160(1000))
```

```go
package main

import (
	"fmt"
	"sync"
)

// 简单的并发计数器示例，用于BENCH-C语料填充(Go样例161)
type Counter161 struct {
	mu    sync.Mutex
	value int
}

func (c *Counter161) Inc() {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.value++
}

func main() {
	c := &Counter161{}
	var wg sync.WaitGroup
	for i := 0; i < 100; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			c.Inc()
		}()
	}
	wg.Wait()
	fmt.Println("value:", c.value)
}
```

```rust
use std::collections::HashMap;

// 简单的LRU缓存骨架示例，用于BENCH-C语料填充(Rust样例162)
struct Cache162 {
    map: HashMap<String, i64>,
    capacity: usize,
}

impl Cache162 {
    fn new(capacity: usize) -> Self {
        Cache162 { map: HashMap::new(), capacity }
    }

    fn put(&mut self, key: String, value: i64) {
        if self.map.len() >= self.capacity {
            if let Some(k) = self.map.keys().next().cloned() {
                self.map.remove(&k);
            }
        }
        self.map.insert(key, value);
    }
}

fn main() {
    let mut cache = Cache162::new(16);
    cache.put("a".to_string(), 162);
    println!("{:?}", cache.map.get("a"));
}
```

```json
{
  "sample_id": 163,
  "name": "bench-c-fixture-163",
  "tags": ["highlight", "bench", "json"],
  "metrics": {
    "first_paint_ms": 61.255,
    "delta_ms": -2.469,
    "exe_delta_bytes": 14336
  },
  "languages": ["c", "cpp", "csharp", "java", "js", "python", "go", "rust", "json", "yaml", "sh"],
  "active": true
}
```

```yaml
# BENCH-C YAML样例164，用于覆盖YAML高亮规则
sample_id: 164
name: bench-c-fixture-164
tags:
  - highlight
  - bench
  - yaml
metrics:
  first_paint_ms: 61.255
  exe_delta_bytes: 14336
active: true
```

```sh
#!/bin/bash
# BENCH-C shell样例165，用于覆盖shell高亮规则
set -euo pipefail

BENCH_ID=165
LOG_FILE="/tmp/bench_165.log"

run_once() {
    echo "running bench iteration ${BENCH_ID}" >> "$LOG_FILE"
    for i in $(seq 1 3); do
        echo "step $i" >> "$LOG_FILE"
    done
}

run_once
```

## 第 16 节：本节为 BENCH-C 语料的说明性文字，穿插在代码块之间，用于让代码块占比保持在约 50% 以上但不至于 100%。BENCH-C 是 T54 新增的“高亮最坏情况”记录语料：约 100KB、代码块占比不低于 50%、覆盖全部 11 种受支持的高亮语言（C/C++/C#/Java/JavaScript/Python/Go/Rust/JSON/YAML/Shell）。本文件不设门禁，仅用于记录首屏时间与内存在高亮最坏场景下的数值。

```c
#include <stdio.h>
#include <stdlib.h>

// 计算斐波那契数列第n项，用于BENCH-C语料填充(C语言样例166)
int fib_166(int n) {
    if (n <= 1) return n;
    int a = 0, b = 1;
    for (int i = 2; i <= n; i++) {
        int c = a + b;
        a = b;
        b = c;
    }
    return b;
}

int main(void) {
    for (int i = 0; i < 10; i++) {
        printf("fib(%d) = %d\n", i, fib_166(i));
    }
    return EXIT_SUCCESS;
}
```

```cpp
#include <vector>
#include <string>
#include <iostream>

// 简单的模板类，用于BENCH-C语料填充(C++样例167)
template <typename T>
class Stack167 {
public:
    void push(const T& v) { data_.push_back(v); }
    T pop() {
        T v = data_.back();
        data_.pop_back();
        return v;
    }
    bool empty() const noexcept { return data_.empty(); }
private:
    std::vector<T> data_;
};

int main() {
    Stack167<std::string> s;
    s.push("hello");
    s.push("world");
    while (!s.empty()) {
        std::cout << s.pop() << std::endl;
    }
    return 0;
}
```

```csharp
using System;
using System.Collections.Generic;

// 简单的仓库模式示例，用于BENCH-C语料填充(C#样例168)
namespace BenchSample168
{
    public class Repository<T>
    {
        private readonly List<T> _items = new List<T>();

        public void Add(T item) => _items.Add(item);

        public IEnumerable<T> All() => _items;

        public int Count => _items.Count;
    }

    class Program
    {
        static void Main(string[] args)
        {
            var repo = new Repository<string>();
            repo.Add("alpha");
            repo.Add("beta");
            Console.WriteLine($"count={repo.Count}");
        }
    }
}
```

```java
import java.util.ArrayList;
import java.util.List;

// 简单的观察者模式示例，用于BENCH-C语料填充(Java样例169)
public class Publisher169 {
    private final List<Runnable> listeners = new ArrayList<>();

    public void subscribe(Runnable listener) {
        listeners.add(listener);
    }

    public void publish() {
        for (Runnable listener : listeners) {
            listener.run();
        }
    }

    public static void main(String[] args) {
        Publisher169 pub = new Publisher169();
        pub.subscribe(() -> System.out.println("event fired 169"));
        pub.publish();
    }
}
```

```js
// 简单的事件发射器示例，用于BENCH-C语料填充(JavaScript样例170)
class EventBus170 {
    constructor() {
        this.handlers = {};
    }

    on(name, fn) {
        (this.handlers[name] ||= []).push(fn);
    }

    emit(name, payload) {
        (this.handlers[name] || []).forEach((fn) => fn(payload));
    }
}

const bus = new EventBus170();
bus.on("tick", (n) => console.log(`tick ${n}`));
bus.emit("tick", 170);
```

```python
# 简单的装饰器示例，用于BENCH-C语料填充(Python样例171)
import functools
import time


def timed_171(fn):
    @functools.wraps(fn)
    def wrapper(*args, **kwargs):
        start = time.perf_counter()
        result = fn(*args, **kwargs)
        elapsed = time.perf_counter() - start
        print(f"{fn.__name__} took {elapsed:.4f}s")
        return result
    return wrapper


@timed_171
def compute_171(n):
    return sum(i * i for i in range(n))


if __name__ == "__main__":
    print(compute_171(1000))
```

```go
package main

import (
	"fmt"
	"sync"
)

// 简单的并发计数器示例，用于BENCH-C语料填充(Go样例172)
type Counter172 struct {
	mu    sync.Mutex
	value int
}

func (c *Counter172) Inc() {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.value++
}

func main() {
	c := &Counter172{}
	var wg sync.WaitGroup
	for i := 0; i < 100; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			c.Inc()
		}()
	}
	wg.Wait()
	fmt.Println("value:", c.value)
}
```

```rust
use std::collections::HashMap;

// 简单的LRU缓存骨架示例，用于BENCH-C语料填充(Rust样例173)
struct Cache173 {
    map: HashMap<String, i64>,
    capacity: usize,
}

impl Cache173 {
    fn new(capacity: usize) -> Self {
        Cache173 { map: HashMap::new(), capacity }
    }

    fn put(&mut self, key: String, value: i64) {
        if self.map.len() >= self.capacity {
            if let Some(k) = self.map.keys().next().cloned() {
                self.map.remove(&k);
            }
        }
        self.map.insert(key, value);
    }
}

fn main() {
    let mut cache = Cache173::new(16);
    cache.put("a".to_string(), 173);
    println!("{:?}", cache.map.get("a"));
}
```

```json
{
  "sample_id": 174,
  "name": "bench-c-fixture-174",
  "tags": ["highlight", "bench", "json"],
  "metrics": {
    "first_paint_ms": 61.255,
    "delta_ms": -2.469,
    "exe_delta_bytes": 14336
  },
  "languages": ["c", "cpp", "csharp", "java", "js", "python", "go", "rust", "json", "yaml", "sh"],
  "active": true
}
```

```yaml
# BENCH-C YAML样例175，用于覆盖YAML高亮规则
sample_id: 175
name: bench-c-fixture-175
tags:
  - highlight
  - bench
  - yaml
metrics:
  first_paint_ms: 61.255
  exe_delta_bytes: 14336
active: true
```

```sh
#!/bin/bash
# BENCH-C shell样例176，用于覆盖shell高亮规则
set -euo pipefail

BENCH_ID=176
LOG_FILE="/tmp/bench_176.log"

run_once() {
    echo "running bench iteration ${BENCH_ID}" >> "$LOG_FILE"
    for i in $(seq 1 3); do
        echo "step $i" >> "$LOG_FILE"
    done
}

run_once
```

## 第 17 节：本节为 BENCH-C 语料的说明性文字，穿插在代码块之间，用于让代码块占比保持在约 50% 以上但不至于 100%。BENCH-C 是 T54 新增的“高亮最坏情况”记录语料：约 100KB、代码块占比不低于 50%、覆盖全部 11 种受支持的高亮语言（C/C++/C#/Java/JavaScript/Python/Go/Rust/JSON/YAML/Shell）。本文件不设门禁，仅用于记录首屏时间与内存在高亮最坏场景下的数值。

```c
#include <stdio.h>
#include <stdlib.h>

// 计算斐波那契数列第n项，用于BENCH-C语料填充(C语言样例177)
int fib_177(int n) {
    if (n <= 1) return n;
    int a = 0, b = 1;
    for (int i = 2; i <= n; i++) {
        int c = a + b;
        a = b;
        b = c;
    }
    return b;
}

int main(void) {
    for (int i = 0; i < 10; i++) {
        printf("fib(%d) = %d\n", i, fib_177(i));
    }
    return EXIT_SUCCESS;
}
```

```cpp
#include <vector>
#include <string>
#include <iostream>

// 简单的模板类，用于BENCH-C语料填充(C++样例178)
template <typename T>
class Stack178 {
public:
    void push(const T& v) { data_.push_back(v); }
    T pop() {
        T v = data_.back();
        data_.pop_back();
        return v;
    }
    bool empty() const noexcept { return data_.empty(); }
private:
    std::vector<T> data_;
};

int main() {
    Stack178<std::string> s;
    s.push("hello");
    s.push("world");
    while (!s.empty()) {
        std::cout << s.pop() << std::endl;
    }
    return 0;
}
```

```csharp
using System;
using System.Collections.Generic;

// 简单的仓库模式示例，用于BENCH-C语料填充(C#样例179)
namespace BenchSample179
{
    public class Repository<T>
    {
        private readonly List<T> _items = new List<T>();

        public void Add(T item) => _items.Add(item);

        public IEnumerable<T> All() => _items;

        public int Count => _items.Count;
    }

    class Program
    {
        static void Main(string[] args)
        {
            var repo = new Repository<string>();
            repo.Add("alpha");
            repo.Add("beta");
            Console.WriteLine($"count={repo.Count}");
        }
    }
}
```

```java
import java.util.ArrayList;
import java.util.List;

// 简单的观察者模式示例，用于BENCH-C语料填充(Java样例180)
public class Publisher180 {
    private final List<Runnable> listeners = new ArrayList<>();

    public void subscribe(Runnable listener) {
        listeners.add(listener);
    }

    public void publish() {
        for (Runnable listener : listeners) {
            listener.run();
        }
    }

    public static void main(String[] args) {
        Publisher180 pub = new Publisher180();
        pub.subscribe(() -> System.out.println("event fired 180"));
        pub.publish();
    }
}
```

```js
// 简单的事件发射器示例，用于BENCH-C语料填充(JavaScript样例181)
class EventBus181 {
    constructor() {
        this.handlers = {};
    }

    on(name, fn) {
        (this.handlers[name] ||= []).push(fn);
    }

    emit(name, payload) {
        (this.handlers[name] || []).forEach((fn) => fn(payload));
    }
}

const bus = new EventBus181();
bus.on("tick", (n) => console.log(`tick ${n}`));
bus.emit("tick", 181);
```

```python
# 简单的装饰器示例，用于BENCH-C语料填充(Python样例182)
import functools
import time


def timed_182(fn):
    @functools.wraps(fn)
    def wrapper(*args, **kwargs):
        start = time.perf_counter()
        result = fn(*args, **kwargs)
        elapsed = time.perf_counter() - start
        print(f"{fn.__name__} took {elapsed:.4f}s")
        return result
    return wrapper


@timed_182
def compute_182(n):
    return sum(i * i for i in range(n))


if __name__ == "__main__":
    print(compute_182(1000))
```

```go
package main

import (
	"fmt"
	"sync"
)

// 简单的并发计数器示例，用于BENCH-C语料填充(Go样例183)
type Counter183 struct {
	mu    sync.Mutex
	value int
}

func (c *Counter183) Inc() {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.value++
}

func main() {
	c := &Counter183{}
	var wg sync.WaitGroup
	for i := 0; i < 100; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			c.Inc()
		}()
	}
	wg.Wait()
	fmt.Println("value:", c.value)
}
```

```rust
use std::collections::HashMap;

// 简单的LRU缓存骨架示例，用于BENCH-C语料填充(Rust样例184)
struct Cache184 {
    map: HashMap<String, i64>,
    capacity: usize,
}

impl Cache184 {
    fn new(capacity: usize) -> Self {
        Cache184 { map: HashMap::new(), capacity }
    }

    fn put(&mut self, key: String, value: i64) {
        if self.map.len() >= self.capacity {
            if let Some(k) = self.map.keys().next().cloned() {
                self.map.remove(&k);
            }
        }
        self.map.insert(key, value);
    }
}

fn main() {
    let mut cache = Cache184::new(16);
    cache.put("a".to_string(), 184);
    println!("{:?}", cache.map.get("a"));
}
```

```json
{
  "sample_id": 185,
  "name": "bench-c-fixture-185",
  "tags": ["highlight", "bench", "json"],
  "metrics": {
    "first_paint_ms": 61.255,
    "delta_ms": -2.469,
    "exe_delta_bytes": 14336
  },
  "languages": ["c", "cpp", "csharp", "java", "js", "python", "go", "rust", "json", "yaml", "sh"],
  "active": true
}
```

```yaml
# BENCH-C YAML样例186，用于覆盖YAML高亮规则
sample_id: 186
name: bench-c-fixture-186
tags:
  - highlight
  - bench
  - yaml
metrics:
  first_paint_ms: 61.255
  exe_delta_bytes: 14336
active: true
```

```sh
#!/bin/bash
# BENCH-C shell样例187，用于覆盖shell高亮规则
set -euo pipefail

BENCH_ID=187
LOG_FILE="/tmp/bench_187.log"

run_once() {
    echo "running bench iteration ${BENCH_ID}" >> "$LOG_FILE"
    for i in $(seq 1 3); do
        echo "step $i" >> "$LOG_FILE"
    done
}

run_once
```

## 第 18 节：本节为 BENCH-C 语料的说明性文字，穿插在代码块之间，用于让代码块占比保持在约 50% 以上但不至于 100%。BENCH-C 是 T54 新增的“高亮最坏情况”记录语料：约 100KB、代码块占比不低于 50%、覆盖全部 11 种受支持的高亮语言（C/C++/C#/Java/JavaScript/Python/Go/Rust/JSON/YAML/Shell）。本文件不设门禁，仅用于记录首屏时间与内存在高亮最坏场景下的数值。

```c
#include <stdio.h>
#include <stdlib.h>

// 计算斐波那契数列第n项，用于BENCH-C语料填充(C语言样例188)
int fib_188(int n) {
    if (n <= 1) return n;
    int a = 0, b = 1;
    for (int i = 2; i <= n; i++) {
        int c = a + b;
        a = b;
        b = c;
    }
    return b;
}

int main(void) {
    for (int i = 0; i < 10; i++) {
        printf("fib(%d) = %d\n", i, fib_188(i));
    }
    return EXIT_SUCCESS;
}
```

```cpp
#include <vector>
#include <string>
#include <iostream>

// 简单的模板类，用于BENCH-C语料填充(C++样例189)
template <typename T>
class Stack189 {
public:
    void push(const T& v) { data_.push_back(v); }
    T pop() {
        T v = data_.back();
        data_.pop_back();
        return v;
    }
    bool empty() const noexcept { return data_.empty(); }
private:
    std::vector<T> data_;
};

int main() {
    Stack189<std::string> s;
    s.push("hello");
    s.push("world");
    while (!s.empty()) {
        std::cout << s.pop() << std::endl;
    }
    return 0;
}
```

```csharp
using System;
using System.Collections.Generic;

// 简单的仓库模式示例，用于BENCH-C语料填充(C#样例190)
namespace BenchSample190
{
    public class Repository<T>
    {
        private readonly List<T> _items = new List<T>();

        public void Add(T item) => _items.Add(item);

        public IEnumerable<T> All() => _items;

        public int Count => _items.Count;
    }

    class Program
    {
        static void Main(string[] args)
        {
            var repo = new Repository<string>();
            repo.Add("alpha");
            repo.Add("beta");
            Console.WriteLine($"count={repo.Count}");
        }
    }
}
```

```java
import java.util.ArrayList;
import java.util.List;

// 简单的观察者模式示例，用于BENCH-C语料填充(Java样例191)
public class Publisher191 {
    private final List<Runnable> listeners = new ArrayList<>();

    public void subscribe(Runnable listener) {
        listeners.add(listener);
    }

    public void publish() {
        for (Runnable listener : listeners) {
            listener.run();
        }
    }

    public static void main(String[] args) {
        Publisher191 pub = new Publisher191();
        pub.subscribe(() -> System.out.println("event fired 191"));
        pub.publish();
    }
}
```

```js
// 简单的事件发射器示例，用于BENCH-C语料填充(JavaScript样例192)
class EventBus192 {
    constructor() {
        this.handlers = {};
    }

    on(name, fn) {
        (this.handlers[name] ||= []).push(fn);
    }

    emit(name, payload) {
        (this.handlers[name] || []).forEach((fn) => fn(payload));
    }
}

const bus = new EventBus192();
bus.on("tick", (n) => console.log(`tick ${n}`));
bus.emit("tick", 192);
```

```python
# 简单的装饰器示例，用于BENCH-C语料填充(Python样例193)
import functools
import time


def timed_193(fn):
    @functools.wraps(fn)
    def wrapper(*args, **kwargs):
        start = time.perf_counter()
        result = fn(*args, **kwargs)
        elapsed = time.perf_counter() - start
        print(f"{fn.__name__} took {elapsed:.4f}s")
        return result
    return wrapper


@timed_193
def compute_193(n):
    return sum(i * i for i in range(n))


if __name__ == "__main__":
    print(compute_193(1000))
```

```go
package main

import (
	"fmt"
	"sync"
)

// 简单的并发计数器示例，用于BENCH-C语料填充(Go样例194)
type Counter194 struct {
	mu    sync.Mutex
	value int
}

func (c *Counter194) Inc() {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.value++
}

func main() {
	c := &Counter194{}
	var wg sync.WaitGroup
	for i := 0; i < 100; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			c.Inc()
		}()
	}
	wg.Wait()
	fmt.Println("value:", c.value)
}
```

```rust
use std::collections::HashMap;

// 简单的LRU缓存骨架示例，用于BENCH-C语料填充(Rust样例195)
struct Cache195 {
    map: HashMap<String, i64>,
    capacity: usize,
}

impl Cache195 {
    fn new(capacity: usize) -> Self {
        Cache195 { map: HashMap::new(), capacity }
    }

    fn put(&mut self, key: String, value: i64) {
        if self.map.len() >= self.capacity {
            if let Some(k) = self.map.keys().next().cloned() {
                self.map.remove(&k);
            }
        }
        self.map.insert(key, value);
    }
}

fn main() {
    let mut cache = Cache195::new(16);
    cache.put("a".to_string(), 195);
    println!("{:?}", cache.map.get("a"));
}
```

```json
{
  "sample_id": 196,
  "name": "bench-c-fixture-196",
  "tags": ["highlight", "bench", "json"],
  "metrics": {
    "first_paint_ms": 61.255,
    "delta_ms": -2.469,
    "exe_delta_bytes": 14336
  },
  "languages": ["c", "cpp", "csharp", "java", "js", "python", "go", "rust", "json", "yaml", "sh"],
  "active": true
}
```

```yaml
# BENCH-C YAML样例197，用于覆盖YAML高亮规则
sample_id: 197
name: bench-c-fixture-197
tags:
  - highlight
  - bench
  - yaml
metrics:
  first_paint_ms: 61.255
  exe_delta_bytes: 14336
active: true
```

```sh
#!/bin/bash
# BENCH-C shell样例198，用于覆盖shell高亮规则
set -euo pipefail

BENCH_ID=198
LOG_FILE="/tmp/bench_198.log"

run_once() {
    echo "running bench iteration ${BENCH_ID}" >> "$LOG_FILE"
    for i in $(seq 1 3); do
        echo "step $i" >> "$LOG_FILE"
    done
}

run_once
```

## 第 19 节：本节为 BENCH-C 语料的说明性文字，穿插在代码块之间，用于让代码块占比保持在约 50% 以上但不至于 100%。BENCH-C 是 T54 新增的“高亮最坏情况”记录语料：约 100KB、代码块占比不低于 50%、覆盖全部 11 种受支持的高亮语言（C/C++/C#/Java/JavaScript/Python/Go/Rust/JSON/YAML/Shell）。本文件不设门禁，仅用于记录首屏时间与内存在高亮最坏场景下的数值。

```c
#include <stdio.h>
#include <stdlib.h>

// 计算斐波那契数列第n项，用于BENCH-C语料填充(C语言样例199)
int fib_199(int n) {
    if (n <= 1) return n;
    int a = 0, b = 1;
    for (int i = 2; i <= n; i++) {
        int c = a + b;
        a = b;
        b = c;
    }
    return b;
}

int main(void) {
    for (int i = 0; i < 10; i++) {
        printf("fib(%d) = %d\n", i, fib_199(i));
    }
    return EXIT_SUCCESS;
}
```

```cpp
#include <vector>
#include <string>
#include <iostream>

// 简单的模板类，用于BENCH-C语料填充(C++样例200)
template <typename T>
class Stack200 {
public:
    void push(const T& v) { data_.push_back(v); }
    T pop() {
        T v = data_.back();
        data_.pop_back();
        return v;
    }
    bool empty() const noexcept { return data_.empty(); }
private:
    std::vector<T> data_;
};

int main() {
    Stack200<std::string> s;
    s.push("hello");
    s.push("world");
    while (!s.empty()) {
        std::cout << s.pop() << std::endl;
    }
    return 0;
}
```

```csharp
using System;
using System.Collections.Generic;

// 简单的仓库模式示例，用于BENCH-C语料填充(C#样例201)
namespace BenchSample201
{
    public class Repository<T>
    {
        private readonly List<T> _items = new List<T>();

        public void Add(T item) => _items.Add(item);

        public IEnumerable<T> All() => _items;

        public int Count => _items.Count;
    }

    class Program
    {
        static void Main(string[] args)
        {
            var repo = new Repository<string>();
            repo.Add("alpha");
            repo.Add("beta");
            Console.WriteLine($"count={repo.Count}");
        }
    }
}
```

```java
import java.util.ArrayList;
import java.util.List;

// 简单的观察者模式示例，用于BENCH-C语料填充(Java样例202)
public class Publisher202 {
    private final List<Runnable> listeners = new ArrayList<>();

    public void subscribe(Runnable listener) {
        listeners.add(listener);
    }

    public void publish() {
        for (Runnable listener : listeners) {
            listener.run();
        }
    }

    public static void main(String[] args) {
        Publisher202 pub = new Publisher202();
        pub.subscribe(() -> System.out.println("event fired 202"));
        pub.publish();
    }
}
```

```js
// 简单的事件发射器示例，用于BENCH-C语料填充(JavaScript样例203)
class EventBus203 {
    constructor() {
        this.handlers = {};
    }

    on(name, fn) {
        (this.handlers[name] ||= []).push(fn);
    }

    emit(name, payload) {
        (this.handlers[name] || []).forEach((fn) => fn(payload));
    }
}

const bus = new EventBus203();
bus.on("tick", (n) => console.log(`tick ${n}`));
bus.emit("tick", 203);
```

```python
# 简单的装饰器示例，用于BENCH-C语料填充(Python样例204)
import functools
import time


def timed_204(fn):
    @functools.wraps(fn)
    def wrapper(*args, **kwargs):
        start = time.perf_counter()
        result = fn(*args, **kwargs)
        elapsed = time.perf_counter() - start
        print(f"{fn.__name__} took {elapsed:.4f}s")
        return result
    return wrapper


@timed_204
def compute_204(n):
    return sum(i * i for i in range(n))


if __name__ == "__main__":
    print(compute_204(1000))
```

```go
package main

import (
	"fmt"
	"sync"
)

// 简单的并发计数器示例，用于BENCH-C语料填充(Go样例205)
type Counter205 struct {
	mu    sync.Mutex
	value int
}

func (c *Counter205) Inc() {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.value++
}

func main() {
	c := &Counter205{}
	var wg sync.WaitGroup
	for i := 0; i < 100; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			c.Inc()
		}()
	}
	wg.Wait()
	fmt.Println("value:", c.value)
}
```

```rust
use std::collections::HashMap;

// 简单的LRU缓存骨架示例，用于BENCH-C语料填充(Rust样例206)
struct Cache206 {
    map: HashMap<String, i64>,
    capacity: usize,
}

impl Cache206 {
    fn new(capacity: usize) -> Self {
        Cache206 { map: HashMap::new(), capacity }
    }

    fn put(&mut self, key: String, value: i64) {
        if self.map.len() >= self.capacity {
            if let Some(k) = self.map.keys().next().cloned() {
                self.map.remove(&k);
            }
        }
        self.map.insert(key, value);
    }
}

fn main() {
    let mut cache = Cache206::new(16);
    cache.put("a".to_string(), 206);
    println!("{:?}", cache.map.get("a"));
}
```

```json
{
  "sample_id": 207,
  "name": "bench-c-fixture-207",
  "tags": ["highlight", "bench", "json"],
  "metrics": {
    "first_paint_ms": 61.255,
    "delta_ms": -2.469,
    "exe_delta_bytes": 14336
  },
  "languages": ["c", "cpp", "csharp", "java", "js", "python", "go", "rust", "json", "yaml", "sh"],
  "active": true
}
```

```yaml
# BENCH-C YAML样例208，用于覆盖YAML高亮规则
sample_id: 208
name: bench-c-fixture-208
tags:
  - highlight
  - bench
  - yaml
metrics:
  first_paint_ms: 61.255
  exe_delta_bytes: 14336
active: true
```

```sh
#!/bin/bash
# BENCH-C shell样例209，用于覆盖shell高亮规则
set -euo pipefail

BENCH_ID=209
LOG_FILE="/tmp/bench_209.log"

run_once() {
    echo "running bench iteration ${BENCH_ID}" >> "$LOG_FILE"
    for i in $(seq 1 3); do
        echo "step $i" >> "$LOG_FILE"
    done
}

run_once
```

## 第 20 节：本节为 BENCH-C 语料的说明性文字，穿插在代码块之间，用于让代码块占比保持在约 50% 以上但不至于 100%。BENCH-C 是 T54 新增的“高亮最坏情况”记录语料：约 100KB、代码块占比不低于 50%、覆盖全部 11 种受支持的高亮语言（C/C++/C#/Java/JavaScript/Python/Go/Rust/JSON/YAML/Shell）。本文件不设门禁，仅用于记录首屏时间与内存在高亮最坏场景下的数值。

```c
#include <stdio.h>
#include <stdlib.h>

// 计算斐波那契数列第n项，用于BENCH-C语料填充(C语言样例210)
int fib_210(int n) {
    if (n <= 1) return n;
    int a = 0, b = 1;
    for (int i = 2; i <= n; i++) {
        int c = a + b;
        a = b;
        b = c;
    }
    return b;
}

int main(void) {
    for (int i = 0; i < 10; i++) {
        printf("fib(%d) = %d\n", i, fib_210(i));
    }
    return EXIT_SUCCESS;
}
```

```cpp
#include <vector>
#include <string>
#include <iostream>

// 简单的模板类，用于BENCH-C语料填充(C++样例211)
template <typename T>
class Stack211 {
public:
    void push(const T& v) { data_.push_back(v); }
    T pop() {
        T v = data_.back();
        data_.pop_back();
        return v;
    }
    bool empty() const noexcept { return data_.empty(); }
private:
    std::vector<T> data_;
};

int main() {
    Stack211<std::string> s;
    s.push("hello");
    s.push("world");
    while (!s.empty()) {
        std::cout << s.pop() << std::endl;
    }
    return 0;
}
```

```csharp
using System;
using System.Collections.Generic;

// 简单的仓库模式示例，用于BENCH-C语料填充(C#样例212)
namespace BenchSample212
{
    public class Repository<T>
    {
        private readonly List<T> _items = new List<T>();

        public void Add(T item) => _items.Add(item);

        public IEnumerable<T> All() => _items;

        public int Count => _items.Count;
    }

    class Program
    {
        static void Main(string[] args)
        {
            var repo = new Repository<string>();
            repo.Add("alpha");
            repo.Add("beta");
            Console.WriteLine($"count={repo.Count}");
        }
    }
}
```

```java
import java.util.ArrayList;
import java.util.List;

// 简单的观察者模式示例，用于BENCH-C语料填充(Java样例213)
public class Publisher213 {
    private final List<Runnable> listeners = new ArrayList<>();

    public void subscribe(Runnable listener) {
        listeners.add(listener);
    }

    public void publish() {
        for (Runnable listener : listeners) {
            listener.run();
        }
    }

    public static void main(String[] args) {
        Publisher213 pub = new Publisher213();
        pub.subscribe(() -> System.out.println("event fired 213"));
        pub.publish();
    }
}
```

```js
// 简单的事件发射器示例，用于BENCH-C语料填充(JavaScript样例214)
class EventBus214 {
    constructor() {
        this.handlers = {};
    }

    on(name, fn) {
        (this.handlers[name] ||= []).push(fn);
    }

    emit(name, payload) {
        (this.handlers[name] || []).forEach((fn) => fn(payload));
    }
}

const bus = new EventBus214();
bus.on("tick", (n) => console.log(`tick ${n}`));
bus.emit("tick", 214);
```

```python
# 简单的装饰器示例，用于BENCH-C语料填充(Python样例215)
import functools
import time


def timed_215(fn):
    @functools.wraps(fn)
    def wrapper(*args, **kwargs):
        start = time.perf_counter()
        result = fn(*args, **kwargs)
        elapsed = time.perf_counter() - start
        print(f"{fn.__name__} took {elapsed:.4f}s")
        return result
    return wrapper


@timed_215
def compute_215(n):
    return sum(i * i for i in range(n))


if __name__ == "__main__":
    print(compute_215(1000))
```

```go
package main

import (
	"fmt"
	"sync"
)

// 简单的并发计数器示例，用于BENCH-C语料填充(Go样例216)
type Counter216 struct {
	mu    sync.Mutex
	value int
}

func (c *Counter216) Inc() {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.value++
}

func main() {
	c := &Counter216{}
	var wg sync.WaitGroup
	for i := 0; i < 100; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			c.Inc()
		}()
	}
	wg.Wait()
	fmt.Println("value:", c.value)
}
```

```rust
use std::collections::HashMap;

// 简单的LRU缓存骨架示例，用于BENCH-C语料填充(Rust样例217)
struct Cache217 {
    map: HashMap<String, i64>,
    capacity: usize,
}

impl Cache217 {
    fn new(capacity: usize) -> Self {
        Cache217 { map: HashMap::new(), capacity }
    }

    fn put(&mut self, key: String, value: i64) {
        if self.map.len() >= self.capacity {
            if let Some(k) = self.map.keys().next().cloned() {
                self.map.remove(&k);
            }
        }
        self.map.insert(key, value);
    }
}

fn main() {
    let mut cache = Cache217::new(16);
    cache.put("a".to_string(), 217);
    println!("{:?}", cache.map.get("a"));
}
```

```json
{
  "sample_id": 218,
  "name": "bench-c-fixture-218",
  "tags": ["highlight", "bench", "json"],
  "metrics": {
    "first_paint_ms": 61.255,
    "delta_ms": -2.469,
    "exe_delta_bytes": 14336
  },
  "languages": ["c", "cpp", "csharp", "java", "js", "python", "go", "rust", "json", "yaml", "sh"],
  "active": true
}
```

```yaml
# BENCH-C YAML样例219，用于覆盖YAML高亮规则
sample_id: 219
name: bench-c-fixture-219
tags:
  - highlight
  - bench
  - yaml
metrics:
  first_paint_ms: 61.255
  exe_delta_bytes: 14336
active: true
```

```sh
#!/bin/bash
# BENCH-C shell样例220，用于覆盖shell高亮规则
set -euo pipefail

BENCH_ID=220
LOG_FILE="/tmp/bench_220.log"

run_once() {
    echo "running bench iteration ${BENCH_ID}" >> "$LOG_FILE"
    for i in $(seq 1 3); do
        echo "step $i" >> "$LOG_FILE"
    done
}

run_once
```

