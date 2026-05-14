/**
 * ============================================================
 *   Multithreaded File Compression Tool
 *   Uses zlib deflate/inflate with a thread-pool for parallel
 *   block-level compression and decompression.
 * ============================================================
 */

#include <iostream>
#include <fstream>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include <atomic>
#include <chrono>
#include <cstring>
#include <cassert>
#include <string>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <zlib.h>

// ─── Constants ────────────────────────────────────────────────────────────────
static constexpr uint32_t MAGIC        = 0x4D54464C;   // "MTFL"
static constexpr uint32_t VERSION      = 1;
static constexpr size_t   BLOCK_SIZE   = 1 * 1024 * 1024;  // 1 MB per block
static constexpr int      ZLIB_LEVEL   = Z_BEST_SPEED;

// ─── File Header ──────────────────────────────────────────────────────────────
struct FileHeader {
    uint32_t magic;
    uint32_t version;
    uint64_t original_size;
    uint32_t num_blocks;
    uint32_t block_size;   // nominal block size used
};

// ─── Block Descriptor (stored after header, one per block) ────────────────────
struct BlockDesc {
    uint64_t compressed_offset;   // offset in .mtfl file where compressed data starts
    uint32_t compressed_size;
    uint32_t original_size;
};

// ─── Thread Pool ──────────────────────────────────────────────────────────────
class ThreadPool {
public:
    explicit ThreadPool(size_t nthreads) : stop_(false) {
        for (size_t i = 0; i < nthreads; ++i)
            workers_.emplace_back([this] { worker_loop(); });
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lk(mtx_);
            stop_ = true;
        }
        cv_.notify_all();
        for (auto& t : workers_) t.join();
    }

    template<typename F>
    void enqueue(F&& f) {
        {
            std::unique_lock<std::mutex> lk(mtx_);
            tasks_.push(std::forward<F>(f));
        }
        cv_.notify_one();
    }

    size_t size() const { return workers_.size(); }

private:
    void worker_loop() {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lk(mtx_);
                cv_.wait(lk, [this]{ return stop_ || !tasks_.empty(); });
                if (stop_ && tasks_.empty()) return;
                task = std::move(tasks_.front());
                tasks_.pop();
            }
            task();
        }
    }

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex mtx_;
    std::condition_variable cv_;
    bool stop_;
};

// ─── Helpers ──────────────────────────────────────────────────────────────────
static std::string human_size(uint64_t bytes) {
    std::ostringstream ss;
    if (bytes < 1024)            ss << bytes << " B";
    else if (bytes < 1024*1024)  ss << std::fixed << std::setprecision(2) << bytes/1024.0 << " KB";
    else                         ss << std::fixed << std::setprecision(2) << bytes/(1024.0*1024) << " MB";
    return ss.str();
}

static std::string human_time(double ms) {
    std::ostringstream ss;
    if (ms < 1000) ss << std::fixed << std::setprecision(1) << ms << " ms";
    else           ss << std::fixed << std::setprecision(2) << ms/1000.0 << " s";
    return ss.str();
}

// ─── Single-block compression (zlib deflate) ──────────────────────────────────
static bool compress_block(const uint8_t* in,  uint32_t in_size,
                            uint8_t*       out, uint32_t& out_size) {
    uLongf bound = compressBound(in_size);
    if (bound > out_size) { out_size = (uint32_t)bound; return false; }
    int rc = compress2(out, &bound, in, in_size, ZLIB_LEVEL);
    out_size = (uint32_t)bound;
    return rc == Z_OK;
}

// ─── Single-block decompression (zlib inflate) ────────────────────────────────
static bool decompress_block(const uint8_t* in,  uint32_t in_size,
                              uint8_t*       out, uint32_t out_size) {
    uLongf dest_len = out_size;
    int rc = uncompress(out, &dest_len, in, in_size);
    return rc == Z_OK && dest_len == out_size;
}

// ─── COMPRESS ─────────────────────────────────────────────────────────────────
bool compress_file(const std::string& input_path,
                   const std::string& output_path,
                   size_t             nthreads,
                   double&            elapsed_ms) {

    // --- Read input file ---
    std::ifstream fin(input_path, std::ios::binary | std::ios::ate);
    if (!fin) { std::cerr << "Cannot open: " << input_path << "\n"; return false; }
    uint64_t file_size = fin.tellg();
    fin.seekg(0);
    std::vector<uint8_t> raw(file_size);
    fin.read(reinterpret_cast<char*>(raw.data()), file_size);
    fin.close();

    uint32_t num_blocks = (uint32_t)((file_size + BLOCK_SIZE - 1) / BLOCK_SIZE);
    if (num_blocks == 0) num_blocks = 1;

    // Per-block compressed buffers
    std::vector<std::vector<uint8_t>> comp_blocks(num_blocks);
    std::vector<uint32_t>             orig_sizes (num_blocks);
    std::mutex err_mtx;
    bool had_error = false;

    auto t0 = std::chrono::high_resolution_clock::now();

    // --- Parallel compression ---
    {
        ThreadPool pool(nthreads);
        std::atomic<size_t> done{0};

        for (uint32_t b = 0; b < num_blocks; ++b) {
            pool.enqueue([b, &raw, &comp_blocks, &orig_sizes,
                           &had_error, &err_mtx, &done, file_size]() {
                size_t offset = (size_t)b * BLOCK_SIZE;
                uint32_t bsz  = (uint32_t)std::min((uint64_t)BLOCK_SIZE,
                                                    (uint64_t)file_size - offset);
                orig_sizes[b] = bsz;

                uint32_t bound = (uint32_t)compressBound(bsz);
                comp_blocks[b].resize(bound);

                if (!compress_block(raw.data() + offset, bsz,
                                    comp_blocks[b].data(), bound)) {
                    std::lock_guard<std::mutex> lk(err_mtx);
                    had_error = true;
                } else {
                    comp_blocks[b].resize(bound);
                }
                ++done;
            });
        }

        // Wait until all blocks done
        while (done.load() < num_blocks)
            std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    if (had_error) { std::cerr << "Compression error in one or more blocks.\n"; return false; }

    // --- Write output file ---
    std::ofstream fout(output_path, std::ios::binary);
    if (!fout) { std::cerr << "Cannot write: " << output_path << "\n"; return false; }

    // Header
    FileHeader hdr;
    hdr.magic         = MAGIC;
    hdr.version       = VERSION;
    hdr.original_size = file_size;
    hdr.num_blocks    = num_blocks;
    hdr.block_size    = (uint32_t)BLOCK_SIZE;
    fout.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));

    // Block descriptors (placeholder offsets, filled after layout)
    uint64_t desc_start = sizeof(FileHeader);
    uint64_t data_start = desc_start + (uint64_t)num_blocks * sizeof(BlockDesc);
    std::vector<BlockDesc> descs(num_blocks);
    uint64_t off = data_start;
    for (uint32_t b = 0; b < num_blocks; ++b) {
        descs[b].compressed_offset = off;
        descs[b].compressed_size   = (uint32_t)comp_blocks[b].size();
        descs[b].original_size     = orig_sizes[b];
        off += comp_blocks[b].size();
    }
    fout.write(reinterpret_cast<const char*>(descs.data()),
               num_blocks * sizeof(BlockDesc));

    // Compressed data
    for (uint32_t b = 0; b < num_blocks; ++b)
        fout.write(reinterpret_cast<const char*>(comp_blocks[b].data()),
                   comp_blocks[b].size());
    fout.close();
    return true;
}

// ─── DECOMPRESS ───────────────────────────────────────────────────────────────
bool decompress_file(const std::string& input_path,
                     const std::string& output_path,
                     size_t             nthreads,
                     double&            elapsed_ms) {

    std::ifstream fin(input_path, std::ios::binary);
    if (!fin) { std::cerr << "Cannot open: " << input_path << "\n"; return false; }

    FileHeader hdr;
    fin.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
    if (hdr.magic != MAGIC || hdr.version != VERSION) {
        std::cerr << "Invalid or unsupported file format.\n"; return false;
    }

    std::vector<BlockDesc> descs(hdr.num_blocks);
    fin.read(reinterpret_cast<char*>(descs.data()),
             hdr.num_blocks * sizeof(BlockDesc));

    // Read all compressed blocks into memory
    std::vector<std::vector<uint8_t>> comp_blocks(hdr.num_blocks);
    for (uint32_t b = 0; b < hdr.num_blocks; ++b) {
        comp_blocks[b].resize(descs[b].compressed_size);
        fin.seekg(descs[b].compressed_offset);
        fin.read(reinterpret_cast<char*>(comp_blocks[b].data()),
                 descs[b].compressed_size);
    }
    fin.close();

    // Output buffer
    std::vector<uint8_t> out(hdr.original_size);
    std::mutex err_mtx;
    bool had_error = false;

    auto t0 = std::chrono::high_resolution_clock::now();

    {
        ThreadPool pool(nthreads);
        std::atomic<size_t> done{0};

        for (uint32_t b = 0; b < hdr.num_blocks; ++b) {
            pool.enqueue([b, &comp_blocks, &descs, &out,
                           &had_error, &err_mtx, &done]() {
                uint64_t out_off = (uint64_t)b * BLOCK_SIZE;
                if (!decompress_block(comp_blocks[b].data(), descs[b].compressed_size,
                                      out.data() + out_off,  descs[b].original_size)) {
                    std::lock_guard<std::mutex> lk(err_mtx);
                    had_error = true;
                }
                ++done;
            });
        }

        while (done.load() < hdr.num_blocks)
            std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    if (had_error) { std::cerr << "Decompression error in one or more blocks.\n"; return false; }

    std::ofstream fout(output_path, std::ios::binary);
    if (!fout) { std::cerr << "Cannot write: " << output_path << "\n"; return false; }
    fout.write(reinterpret_cast<const char*>(out.data()), out.size());
    fout.close();
    return true;
}

// ─── BENCHMARK helper ─────────────────────────────────────────────────────────
struct BenchResult {
    size_t  nthreads;
    double  compress_ms;
    double  decompress_ms;
    uint64_t orig_bytes;
    uint64_t comp_bytes;
};

static BenchResult run_bench(const std::string& input_path, size_t nthreads) {
    std::string comp_path   = input_path + ".t" + std::to_string(nthreads) + ".mtfl";
    std::string decomp_path = input_path + ".t" + std::to_string(nthreads) + ".out";

    BenchResult r{};
    r.nthreads = nthreads;

    compress_file(input_path, comp_path, nthreads, r.compress_ms);
    decompress_file(comp_path, decomp_path, nthreads, r.decompress_ms);

    // Get sizes
    {
        std::ifstream f(input_path, std::ios::ate | std::ios::binary);
        r.orig_bytes = f.tellg();
    }
    {
        std::ifstream f(comp_path, std::ios::ate | std::ios::binary);
        r.comp_bytes = f.tellg();
    }

    // Clean up temp files
    std::remove(comp_path.c_str());
    std::remove(decomp_path.c_str());
    return r;
}

// ─── MAIN ─────────────────────────────────────────────────────────────────────
static void print_usage(const char* prog) {
    std::cerr
        << "Usage:\n"
        << "  " << prog << " compress   <input> <output.mtfl> [threads]\n"
        << "  " << prog << " decompress <input.mtfl> <output>  [threads]\n"
        << "  " << prog << " benchmark  <input> [max_threads]\n";
}

int main(int argc, char* argv[]) {

    if (argc < 3) { print_usage(argv[0]); return 1; }

    std::string mode = argv[1];
    size_t hw = std::max(1u, std::thread::hardware_concurrency());

    // ── compress ──────────────────────────────────────────────────────────────
    if (mode == "compress") {
        if (argc < 4) { print_usage(argv[0]); return 1; }
        std::string inp = argv[2], out = argv[3];
        size_t nthreads = (argc >= 5) ? std::stoul(argv[4]) : hw;
        nthreads = std::max<size_t>(1, nthreads);

        std::cout << "Compressing: " << inp << " → " << out
                  << "  [" << nthreads << " threads]\n";

        double ms = 0;
        if (!compress_file(inp, out, nthreads, ms)) return 1;

        uint64_t isz, osz;
        { std::ifstream f(inp,  std::ios::ate | std::ios::binary); isz = f.tellg(); }
        { std::ifstream f(out,  std::ios::ate | std::ios::binary); osz = f.tellg(); }

        double ratio = (isz > 0) ? (100.0 * osz / isz) : 0.0;
        double speed = (isz / 1024.0 / 1024.0) / (ms / 1000.0);

        std::cout << "  Original size  : " << human_size(isz) << "\n"
                  << "  Compressed size: " << human_size(osz)
                  << "  (" << std::fixed << std::setprecision(1) << ratio << "%)\n"
                  << "  Time           : " << human_time(ms) << "\n"
                  << "  Throughput     : " << std::fixed << std::setprecision(1)
                                           << speed << " MB/s\n";
        return 0;
    }

    // ── decompress ────────────────────────────────────────────────────────────
    if (mode == "decompress") {
        if (argc < 4) { print_usage(argv[0]); return 1; }
        std::string inp = argv[2], out = argv[3];
        size_t nthreads = (argc >= 5) ? std::stoul(argv[4]) : hw;
        nthreads = std::max<size_t>(1, nthreads);

        std::cout << "Decompressing: " << inp << " → " << out
                  << "  [" << nthreads << " threads]\n";

        double ms = 0;
        if (!decompress_file(inp, out, nthreads, ms)) return 1;

        uint64_t osz;
        { std::ifstream f(out, std::ios::ate | std::ios::binary); osz = f.tellg(); }

        double speed = (osz / 1024.0 / 1024.0) / (ms / 1000.0);

        std::cout << "  Decompressed size: " << human_size(osz) << "\n"
                  << "  Time             : " << human_time(ms) << "\n"
                  << "  Throughput       : " << std::fixed << std::setprecision(1)
                                             << speed << " MB/s\n";
        return 0;
    }

    // ── benchmark ─────────────────────────────────────────────────────────────
    if (mode == "benchmark") {
        std::string inp   = argv[2];
        size_t max_thr    = (argc >= 4) ? std::stoul(argv[3]) : hw;
        max_thr = std::max<size_t>(1, max_thr);

        std::cout << "\n╔══════════════════════════════════════════════════════════╗\n"
                  << "║        Multithreaded Compression Benchmark               ║\n"
                  << "╚══════════════════════════════════════════════════════════╝\n"
                  << "  Input file     : " << inp << "\n"
                  << "  Hardware cores : " << hw << "\n"
                  << "  Testing up to  : " << max_thr << " threads\n\n";

        std::vector<size_t> configs;
        for (size_t t = 1; t <= max_thr; t = (t < 4) ? t+1 : t*2)
            configs.push_back(t);
        if (configs.back() != max_thr) configs.push_back(max_thr);

        // Table header
        std::cout
            << std::left  << std::setw(10) << "Threads"
            << std::right << std::setw(14) << "Compress"
            << std::right << std::setw(14) << "C-Spd(MB/s)"
            << std::right << std::setw(14) << "Decompress"
            << std::right << std::setw(14) << "D-Spd(MB/s)"
            << std::right << std::setw(12) << "Ratio%"
            << "\n" << std::string(78, '-') << "\n";

        std::vector<BenchResult> results;
        for (size_t t : configs) {
            std::cout << "  Running " << t << " thread(s)…" << std::flush;
            auto r = run_bench(inp, t);
            results.push_back(r);

            double c_speed = (r.orig_bytes / 1024.0 / 1024.0) / (r.compress_ms / 1000.0);
            double d_speed = (r.orig_bytes / 1024.0 / 1024.0) / (r.decompress_ms / 1000.0);
            double ratio   = (r.orig_bytes > 0) ? 100.0 * r.comp_bytes / r.orig_bytes : 0;

            std::cout << "\r"
                << std::left  << std::setw(10) << t
                << std::right << std::setw(14) << human_time(r.compress_ms)
                << std::right << std::setw(14) << std::fixed << std::setprecision(1) << c_speed
                << std::right << std::setw(14) << human_time(r.decompress_ms)
                << std::right << std::setw(14) << std::fixed << std::setprecision(1) << d_speed
                << std::right << std::setw(12) << std::fixed << std::setprecision(1) << ratio
                << "\n";
            std::cout.flush();
        }

        // Speedup summary
        if (results.size() > 1) {
            double base_c = results[0].compress_ms;
            double base_d = results[0].decompress_ms;
            std::cout << "\n" << std::string(78, '-') << "\n"
                      << "  Speedup vs 1-thread baseline:\n\n"
                      << std::left  << std::setw(10) << "Threads"
                      << std::right << std::setw(20) << "Compress Speedup"
                      << std::right << std::setw(22) << "Decompress Speedup"
                      << "\n" << std::string(52, '-') << "\n";
            for (auto& r : results) {
                std::cout
                    << std::left  << std::setw(10) << r.nthreads
                    << std::right << std::setw(19) << std::fixed << std::setprecision(2)
                                  << (base_c / r.compress_ms) << "x"
                    << std::right << std::setw(21) << std::fixed << std::setprecision(2)
                                  << (base_d / r.decompress_ms) << "x"
                    << "\n";
            }
        }
        std::cout << "\n";
        return 0;
    }

    print_usage(argv[0]);
    return 1;
}
