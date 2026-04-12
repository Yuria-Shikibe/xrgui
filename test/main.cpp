

#include <benchmark/benchmark.h>
#include <vector>
#include <new>
import std;
import mo_yanxi.byte_pool;

using namespace mo_yanxi;




static void BM_System_NewDelete(benchmark::State& state) {
    const size_t size = state.range(0);
    for (auto _ : state) {

        void* ptr = ::operator new(size);
        benchmark::DoNotOptimize(ptr);

        std::memset(ptr, 0, size);
        ::operator delete(ptr);
    }
}




static void BM_StdVector(benchmark::State& state) {
    const size_t size = state.range(0);
    for (auto _ : state) {
        std::vector<std::byte> v(size);
        benchmark::DoNotOptimize(v.data());

    }
}





static void BM_Pool_AcquireRetire(benchmark::State& state) {
    byte_pool<> pool;
    const size_t size = state.range(0);
    

    auto warmup = pool.acquire(size);
    pool.retire(warmup);

    for (auto _ : state) {

        auto buf = pool.acquire(size);
        
        benchmark::DoNotOptimize(buf.data());
        

        if (buf.size() > 0) {
            buf.data<char>()[0] = 'a';
        }


        pool.retire(buf);
    }
}





static void BM_Pool_Borrow(benchmark::State& state) {
    byte_pool<> pool;
    const size_t size = state.range(0);
    

    auto warmup = pool.borrow(size);


    for (auto _ : state) {

        auto borrowed = pool.borrow(size);
        benchmark::DoNotOptimize(borrowed.get().data());

    }
}





static void BM_Pool_BatchAlloc(benchmark::State& state) {
    byte_pool<> pool;
    const size_t size = state.range(0);
    const int batch_count = 100;
    
    std::vector<byte_buffer> preserved;
    preserved.reserve(batch_count);

    for (auto _ : state) {

        for(int i=0; i<batch_count; ++i) {
            preserved.push_back(pool.acquire(size));
        }
        
        benchmark::DoNotOptimize(preserved.data());


        for(auto& buf : preserved) {
            pool.retire(buf);
        }
        preserved.clear();
    }
}








#define REG_BENCH(Func) \
    BENCHMARK(Func)->RangeMultiplier(4)->Range(512, 4 * 1024 * 1024)

REG_BENCH(BM_System_NewDelete);
REG_BENCH(BM_StdVector);
REG_BENCH(BM_Pool_AcquireRetire);
REG_BENCH(BM_Pool_Borrow);
REG_BENCH(BM_Pool_BatchAlloc);

BENCHMARK_MAIN();