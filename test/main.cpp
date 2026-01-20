

#include <benchmark/benchmark.h>
#include <vector>
#include <new>
import std;
import mo_yanxi.byte_pool;
// import mo_yanxi.aligned_allocator; // 如果想测试特定分配器可取消注释
using namespace mo_yanxi;

// ==========================================
// 基准 1: 系统原生 new/delete (也就是 alloc_sys 的路径)
// ==========================================
static void BM_System_NewDelete(benchmark::State& state) {
    const size_t size = state.range(0);
    for (auto _ : state) {
        // 模拟 byte_buffer 的底层行为
        void* ptr = ::operator new(size);
        benchmark::DoNotOptimize(ptr);
        // 模拟写入以触发生页错误 (Page Fault)，使比较更公平
        std::memset(ptr, 0, size);
        ::operator delete(ptr);
    }
}

// ==========================================
// 基准 2: std::vector<std::byte> (常见用法)
// ==========================================
static void BM_StdVector(benchmark::State& state) {
    const size_t size = state.range(0);
    for (auto _ : state) {
        std::vector<std::byte> v(size);
        benchmark::DoNotOptimize(v.data());
        // std::vector 构造时通常会初始化内存，所以这里不需要额外的 memset
    }
}

// ==========================================
// 测试 1: Byte Pool - 手动 Acquire / Retire (热缓存)
// 预期：最快。因为复用了内存，没有系统调用，只有 vector 的 pop_back/push_back 操作。
// ==========================================
static void BM_Pool_AcquireRetire(benchmark::State& state) {
    byte_pool<> pool;
    const size_t size = state.range(0);
    
    // 预热：先分配一次让池子里有东西 (针对 LIFO 逻辑)
    auto warmup = pool.acquire(size);
    pool.retire(warmup);

    for (auto _ : state) {
        // acquire 内部会计算 bit_ceil 并查找桶
        auto buf = pool.acquire(size);
        
        benchmark::DoNotOptimize(buf.data());
        
        // 简单的写入操作，证明内存可用
        if (buf.size() > 0) {
            buf.data<char>()[0] = 'a';
        }

        // retire 将 buffer 归还到 buckets_
        pool.retire(buf);
    }
}

// ==========================================
// 测试 2: Byte Pool - Borrow (RAII 封装)
// 预期：比 Acquire/Retire 稍慢，因为有 byte_borrow 对象的构造和析构开销
// ==========================================
static void BM_Pool_Borrow(benchmark::State& state) {
    byte_pool<> pool;
    const size_t size = state.range(0);
    
    // 预热
    auto warmup = pool.borrow(size);
    // warmup析构时自动归还

    for (auto _ : state) {
        // borrow 返回 byte_borrow 对象
        auto borrowed = pool.borrow(size);
        benchmark::DoNotOptimize(borrowed.get().data());
        // 退出作用域时自动调用 retire_ 
    }
}

// ==========================================
// 测试 3: 批量分配 (冷启动 vs 缓存容量测试)
// 测试池在连续分配多个块时的表现 (bucket vector 扩容和多次 alloc_sys)
// ==========================================
static void BM_Pool_BatchAlloc(benchmark::State& state) {
    byte_pool<> pool;
    const size_t size = state.range(0);
    const int batch_count = 100;
    
    std::vector<byte_buffer> preserved;
    preserved.reserve(batch_count);

    for (auto _ : state) {
        // 阶段 1: 连续分配，耗尽缓存，触发 alloc_sys
        for(int i=0; i<batch_count; ++i) {
            preserved.push_back(pool.acquire(size));
        }
        
        benchmark::DoNotOptimize(preserved.data());

        // 阶段 2: 全部归还，填充 buckets_
        for(auto& buf : preserved) {
            pool.retire(buf);
        }
        preserved.clear();
    }
}

// ==========================================
// 注册测试用例
// ==========================================

// 测试从小对象 (512B) 到 大对象 (4MB)
// MIN_SHIFT = 9 (512B)
// BUCKET_CNT 覆盖到 16MB，但测试 4MB 足够看趋势
#define REG_BENCH(Func) \
    BENCHMARK(Func)->RangeMultiplier(4)->Range(512, 4 * 1024 * 1024)

REG_BENCH(BM_System_NewDelete);
REG_BENCH(BM_StdVector);
REG_BENCH(BM_Pool_AcquireRetire);
REG_BENCH(BM_Pool_Borrow);
REG_BENCH(BM_Pool_BatchAlloc);

BENCHMARK_MAIN();