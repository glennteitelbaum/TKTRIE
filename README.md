# TKTRIE
Thread optimized vs stp::map and std::unordered map with global mutex

## tktrie Benchmark Results (10 Runs Averaged)

Test of 1000 std::strings

### Raw Performance (ops/sec)

| Threads | tktrie | std::map | std::unordered_map | trie/map | trie/umap |
|--------:|-------:|---------:|-------------------:|---------:|----------:|
| 1 | 5,716,266 | 11,256,898 | 20,252,963 | 0.51x | 0.28x |
| 2 | 8,749,116 | 1,926,037 | 3,451,321 | 4.48x | 2.53x |
| 4 | 8,612,382 | 1,006,434 | 1,633,702 | 8.59x | 5.22x |
| 8 | 6,730,150 | 765,390 | 1,132,692 | 8.52x | 5.94x |
| 16 | 2,887,377 | 685,456 | 1,088,412 | 4.25x | 2.65x |

### Single-Threaded Performance (No Locks)
| Container | ops/sec | vs tktrie |
|-----------|--------:|----------:|
| tktrie | 14,760,839 | 100% |
| std::map | 14,086,580 | 95% |
| std::unordered_map | 37,268,632 | 252% |


### Key Observations

- **Single-threaded**: tktrie with locks slower due to locking overhead
- **Sweet spot**: 4-8 threads
- **Peak throughput**: ~10M ops/sec at 2-4 threads
- **Scalability**: Maintains advantage even at 16 threads
